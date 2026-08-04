/* SPDX-License-Identifier: Apache-2.0
 * Loopback control plane — pure C. Product bus = SMX2; HTTP is lab/ops only.
 * Bind refuses non-loopback. Coord path sanitizes via SMX filter.
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_http.h"
#include "grokium_algocube.h"
#include "grokium_smx_filter.h"
#include "sha256.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define GK_HTTP_REQ_MAX  16384
#define GK_HTTP_BODY_MAX 8192
#define GK_HTTP_RESP_MAX (GROKIUM_CELLS + 2048)

static int host_is_loopback(const char *host) {
  if (!host || !host[0]) return 1;
  if (!strcmp(host, "127.0.0.1")) return 1;
  if (!strcmp(host, "localhost")) return 1;
  if (!strcmp(host, "::1")) return 1;
  /* reject 0.0.0.0 / public — control plane is loopback-only by law */
  return 0;
}

static const char *reason_phrase(int code) {
  switch (code) {
  case 200: return "OK";
  case 400: return "Bad Request";
  case 403: return "Forbidden";
  case 404: return "Not Found";
  case 405: return "Method Not Allowed";
  case 413: return "Payload Too Large";
  case 500: return "Internal Server Error";
  default: return "Error";
  }
}

static void http_reply(int fd, int code, const char *ctype, const char *body) {
  char hdr[384];
  size_t blen = body ? strlen(body) : 0;
  int n = snprintf(hdr, sizeof hdr,
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %zu\r\n"
                   "Connection: close\r\n"
                   "X-Grokium-Telemetry: off\r\n"
                   "X-Grokium-Share: state_matrix_only\r\n"
                   "X-Grokium-Product-Wire: smx2\r\n"
                   "X-Grokium-Peer-HTTP: lab_ops_only\r\n"
                   "\r\n",
                   code, reason_phrase(code),
                   ctype ? ctype : "application/json", blen);
  if (n > 0) (void)write(fd, hdr, (size_t)n);
  if (body && blen) (void)write(fd, body, blen);
}

static int read_request(int fd, char *buf, size_t cap, size_t *out_n) {
  size_t n = 0;
  ssize_t r;
  if (!buf || cap < 64) return -1;
  while (n + 1 < cap) {
    r = read(fd, buf + n, cap - 1 - n);
    if (r < 0) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (r == 0) break;
    n += (size_t)r;
    buf[n] = 0;
    if (strstr(buf, "\r\n\r\n")) break;
    if (n >= cap - 1) break;
  }
  buf[n] = 0;
  if (out_n) *out_n = n;
  return n > 0 ? 0 : -1;
}

static int parse_request(const char *req, size_t req_n, char *method, size_t mcap,
                         char *path, size_t pcap, const char **body,
                         size_t *body_n) {
  const char *sp, *sp2, *hdr_end, *cl;
  size_t mlen, plen, content_len = 0;
  if (!req || !method || !path || mcap < 4 || pcap < 2) return -1;
  sp = strchr(req, ' ');
  if (!sp) return -1;
  mlen = (size_t)(sp - req);
  if (mlen >= mcap) return -1;
  memcpy(method, req, mlen);
  method[mlen] = 0;
  sp2 = strchr(sp + 1, ' ');
  if (!sp2) return -1;
  plen = (size_t)(sp2 - (sp + 1));
  if (plen >= pcap) return -1;
  memcpy(path, sp + 1, plen);
  path[plen] = 0;
  /* strip query */
  {
    char *q = strchr(path, '?');
    if (q) *q = 0;
  }
  hdr_end = strstr(req, "\r\n\r\n");
  if (!hdr_end) return -1;
  cl = strstr(req, "\r\nContent-Length:");
  if (!cl || cl >= hdr_end) cl = strstr(req, "\r\ncontent-length:");
  if (cl && cl < hdr_end) {
    const char *colon = strchr(cl + 2, ':');
    if (colon && colon < hdr_end)
      content_len = (size_t)strtoul(colon + 1, NULL, 10);
    if (content_len > GK_HTTP_BODY_MAX) return -2;
  }
  *body = hdr_end + 4;
  {
    size_t have = 0;
    if (req_n > (size_t)(*body - req))
      have = req_n - (size_t)(*body - req);
    if (content_len > 0) {
      if (have > content_len) have = content_len;
      *body_n = have;
      /* short body still usable if full length not yet buffered */
    } else {
      /* no Content-Length: treat remaining as body (simple clients) */
      *body_n = have;
    }
  }
  return 0;
}

/* Minimal JSON field extractors (no full parser; ops control plane only). */
static int json_get_str(const char *body, size_t n, const char *key, char *out,
                        size_t cap) {
  char pat[80];
  const char *p, *end, *q;
  size_t klen, i;
  if (!body || !key || !out || cap < 2 || n == 0) return -1;
  out[0] = 0;
  klen = strlen(key);
  if (klen + 3 >= sizeof pat) return -1;
  snprintf(pat, sizeof pat, "\"%s\"", key);
  end = body + n;
  p = body;
  for (;;) {
    const char *hit = NULL;
    size_t rem = (size_t)(end - p);
    if (rem < klen + 2) break;
    for (i = 0; i + klen + 2 <= rem; i++) {
      if (p[i] == '"' && i + 1 + klen < rem &&
          !memcmp(p + i + 1, key, klen) && p[i + 1 + klen] == '"') {
        hit = p + i;
        break;
      }
    }
    if (!hit) break;
    p = hit + klen + 2;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
      p++;
    if (p >= end || *p != ':') continue;
    p++;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (p >= end || *p != '"') continue;
    p++;
    q = p;
    while (q < end && *q != '"') {
      if (*q == '\\' && q + 1 < end) q += 2;
      else q++;
    }
    {
      size_t len = (size_t)(q - p);
      if (len >= cap) len = cap - 1;
      memcpy(out, p, len);
      out[len] = 0;
      return 0;
    }
  }
  (void)pat;
  return -1;
}

static int json_get_int(const char *body, size_t n, const char *key, int def) {
  char pat[80];
  const char *p, *end;
  size_t klen, i;
  if (!body || !key || n == 0) return def;
  klen = strlen(key);
  if (klen + 3 >= sizeof pat) return def;
  end = body + n;
  p = body;
  for (;;) {
    const char *hit = NULL;
    size_t rem = (size_t)(end - p);
    if (rem < klen + 2) break;
    for (i = 0; i + klen + 2 <= rem; i++) {
      if (p[i] == '"' && i + 1 + klen < rem &&
          !memcmp(p + i + 1, key, klen) && p[i + 1 + klen] == '"') {
        hit = p + i;
        break;
      }
    }
    if (!hit) break;
    p = hit + klen + 2;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
      p++;
    if (p >= end || *p != ':') continue;
    p++;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (p < end && (*p == '-' || (*p >= '0' && *p <= '9')))
      return atoi(p);
    break;
  }
  (void)pat;
  return def;
}

static void json_law(const grokium_law *L, char *out, size_t cap) {
  snprintf(out, cap,
           "{\"schema\":\"grokium.law.v1\","
           "\"hold_flash\":%d,\"no_brain_wires\":%d,"
           "\"state_matrix_key\":%d,\"cores_unmixed\":%d,"
           "\"face_blur\":%d,\"zero_telemetry\":%d,"
           "\"commander_only_residual\":%d,"
           "\"share\":\"state_matrix_only\"}",
           L ? L->hold_flash : 1, L ? L->no_brain_wires : 1,
           L ? L->state_matrix_key : 1, L ? L->cores_unmixed : 1,
           L ? L->face_blur : 1, L ? L->zero_telemetry : 1,
           L ? L->commander_only_residual : 1);
}

static void json_status(const grokium_law *L, const gk_consolidator *C,
                        const gk_fleet *F, int alive, char *out, size_t cap) {
  snprintf(out, cap,
           "{\"schema\":\"grokium.status.v1\","
           "\"ok\":true,\"product\":\"grokium\","
           "\"control_plane\":\"loopback_http\","
           "\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\","
           "\"hold_flash\":%d,\"telemetry\":\"off\","
           "\"fleet_n\":%d,\"fleet_alive\":%d,"
           "\"matrix_bits\":%u,\"grade\":\"%s\","
           "\"llm_on_hot_path\":false}",
           L ? L->hold_flash : 1, F ? F->n : 0, alive,
           C ? C->matrix.bits_set : 0, C ? C->grade : "EMPTY");
}

static void json_fleet(gk_fleet *F, char *out, size_t cap) {
  int i, alive;
  size_t used;
  if (!F || !out || cap < 64) return;
  alive = fleet_status(F);
  used = (size_t)snprintf(out, cap,
                          "{\"schema\":\"grokium.nanobot_status.v1\","
                          "\"alive\":%d,\"n\":%d,\"nb_manager\":true,"
                          "\"wire_product\":\"smx2\","
                          "\"peer_http\":\"lab_ops_only\","
                          "\"bots\":[",
                          alive, F->n);
  for (i = 0; i < F->n && used + 128 < cap; i++) {
    const gk_bot *b = &F->bots[i];
    char pid_buf[24];
    int n;
    if (b->pid > 0)
      snprintf(pid_buf, sizeof pid_buf, "%d", b->pid);
    else
      snprintf(pid_buf, sizeof pid_buf, "null");
    n = snprintf(out + used, cap - used,
                 "%s{\"id\":\"%s\",\"port\":%d,\"pid\":%s,"
                 "\"status\":\"%s\",\"offline\":%s,\"wire\":\"%s\"}",
                 i ? "," : "", b->id, b->port, pid_buf,
                 b->running ? "running" : "separated",
                 b->running ? "false" : "true",
                 strcmp(b->id, "nb-manager") == 0 ? "smx_motivate" : "smx2");
    if (n < 0) break;
    used += (size_t)n;
  }
  if (used + 3 < cap)
    snprintf(out + used, cap - used, "]}");
}

static void json_matrix(const gk_consolidator *C, char *out, size_t cap) {
  char hex[65];
  char bits[GROKIUM_CELLS + 1];
  if (!C || !out || cap < 128) return;
  smx_sha256_hex(&C->matrix, hex);
  smx_bits_ascii(&C->matrix, bits, sizeof bits);
  snprintf(out, cap,
           "{\"schema\":\"grokium.smx.v1\",\"seq\":%llu,\"bits_set\":%u,"
           "\"host\":\"%s\",\"sha256\":\"%s\",\"grade\":\"%s\","
           "\"share\":\"state_matrix_only\",\"bits\":\"%s\"}",
           (unsigned long long)C->matrix.seq, C->matrix.bits_set,
           C->matrix.host_id, hex, C->grade, bits);
}

static void handle(int cfd, gk_consolidator *C, gk_fleet *F, grokium_law *L,
                   const char *data_root) {
  char req[GK_HTTP_REQ_MAX];
  char method[16], path[256];
  char resp[GK_HTTP_RESP_MAX];
  const char *body = NULL;
  size_t req_n = 0, body_n = 0;
  int pr;
  double now = (double)time(NULL);
  const char *root = data_root && data_root[0] ? data_root : "data";

  if (read_request(cfd, req, sizeof req, &req_n) != 0) {
    http_reply(cfd, 400, "application/json",
               "{\"ok\":false,\"error\":\"empty_request\"}");
    return;
  }
  pr = parse_request(req, req_n, method, sizeof method, path, sizeof path,
                     &body, &body_n);
  if (pr == -2) {
    http_reply(cfd, 413, "application/json",
               "{\"ok\":false,\"error\":\"body_too_large\"}");
    return;
  }
  if (pr != 0) {
    http_reply(cfd, 400, "application/json",
               "{\"ok\":false,\"error\":\"bad_request\"}");
    return;
  }

  if (!strcmp(path, "/healthz") || !strcmp(path, "/")) {
    if (strcmp(method, "GET") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    http_reply(cfd, 200, "application/json",
               "{\"ok\":true,\"service\":\"grokium-loopback\","
               "\"telemetry\":\"off\",\"product_wire\":\"smx2\"}");
    return;
  }

  if (!strcmp(path, "/v1/status")) {
    if (strcmp(method, "GET") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    json_status(L, C, F, F ? fleet_status(F) : 0, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/law")) {
    if (strcmp(method, "GET") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    json_law(L, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/nanobot/status")) {
    if (strcmp(method, "GET") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    if (!F) {
      http_reply(cfd, 500, "application/json",
                 "{\"ok\":false,\"error\":\"no_fleet\"}");
      return;
    }
    json_fleet(F, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/nanobot/deploy")) {
    char plate[512];
    if (strcmp(method, "POST") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    if (!F) {
      http_reply(cfd, 500, "application/json",
                 "{\"ok\":false,\"error\":\"no_fleet\"}");
      return;
    }
    fleet_deploy(F);
    snprintf(plate, sizeof plate, "%s/home/FLEET.json", root);
    fleet_save(F, plate);
    snprintf(resp, sizeof resp,
             "{\"ok\":true,\"deployed\":%d,\"path\":\"%s\","
             "\"spawn\":\"host_responsibility\",\"wire\":\"smx2\"}",
             F->n, plate);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/nanobot/separate") ||
      !strcmp(path, "/v1/nanobot/spawn")) {
    char plate[512];
    char id[64];
    const char *src;
    size_t i, j;
    int do_spawn = !strcmp(path, "/v1/nanobot/spawn");
    if (strcmp(method, "POST") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    if (!F) {
      http_reply(cfd, 500, "application/json",
                 "{\"ok\":false,\"error\":\"no_fleet\"}");
      return;
    }
    /* body: raw bot id, empty=spawn-all, or {"id":"nb-…"} */
    id[0] = 0;
    src = body && body_n ? body : "";
    if (src[0] == '{') {
      const char *k = strstr(src, "\"id\"");
      if (k) {
        k = strchr(k + 4, '"');
        if (k) {
          k++;
          for (j = 0; j + 1 < sizeof id && k[j] && k[j] != '"'; j++)
            id[j] = k[j];
          id[j] = 0;
        }
      }
    } else {
      for (i = 0, j = 0; i < body_n && j + 1 < sizeof id; i++) {
        if (src[i] == '\n' || src[i] == '\r' || src[i] == ' ') continue;
        id[j++] = src[i];
      }
      id[j] = 0;
    }
    if (do_spawn) {
      int n;
      if (id[0]) {
        if (fleet_spawn(F, id) != 0) {
          http_reply(cfd, 500, "application/json",
                     "{\"ok\":false,\"error\":\"spawn_failed\"}");
          return;
        }
        n = 1;
      } else {
        n = fleet_spawn_all(F);
        if (n < 0) {
          http_reply(cfd, 500, "application/json",
                     "{\"ok\":false,\"error\":\"spawn_all_failed\"}");
          return;
        }
      }
      snprintf(plate, sizeof plate, "%s/home/FLEET.json", root);
      fleet_save(F, plate);
      snprintf(resp, sizeof resp,
               "{\"ok\":true,\"spawned\":%d,\"id\":\"%s\",\"alive\":%d,"
               "\"path\":\"%s\",\"product_wire\":\"smx2\","
               "\"peer_http\":\"lab_ops_only\"}",
               n, id[0] ? id : "*", fleet_status(F), plate);
      http_reply(cfd, 200, "application/json", resp);
      return;
    }
    if (!id[0]) {
      http_reply(cfd, 400, "application/json",
                 "{\"ok\":false,\"error\":\"need_bot_id\"}");
      return;
    }
    if (fleet_separate(F, id) != 0) {
      http_reply(cfd, 404, "application/json",
                 "{\"ok\":false,\"error\":\"unknown_bot\"}");
      return;
    }
    snprintf(plate, sizeof plate, "%s/home/FLEET.json", root);
    fleet_save(F, plate);
    snprintf(resp, sizeof resp,
             "{\"ok\":true,\"id\":\"%s\",\"status\":\"separated\","
             "\"path\":\"%s\",\"wire\":\"smx2\"}",
             id, plate);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/matrix/latest") || !strcmp(path, "/v1/stream/smx/latest")) {
    if (strcmp(method, "GET") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    if (!C) {
      http_reply(cfd, 500, "application/json",
                 "{\"ok\":false,\"error\":\"no_matrix\"}");
      return;
    }
    json_matrix(C, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/ability")) {
    if (strcmp(method, "GET") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    if (!C) {
      http_reply(cfd, 500, "application/json",
                 "{\"ok\":false,\"error\":\"no_consolidator\"}");
      return;
    }
    gk_ability(C, now, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/coord") || !strcmp(path, "/v1/stream/smx/publish")) {
    int allow;
    char id[48];
    if (strcmp(method, "POST") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    if (!C || !body || body_n == 0) {
      http_reply(cfd, 400, "application/json",
                 "{\"ok\":false,\"error\":\"empty_body\"}");
      return;
    }
    /* sanitize: prose / hold_flash=0 / non-SMX denied (external origin) */
    allow = grokium_smx_filter_allow_frame(L, (const uint8_t *)body, body_n, 1);
    if (!allow) {
      http_reply(cfd, 403, "application/json",
                 "{\"ok\":false,\"error\":\"smx_filter_deny\","
                 "\"share\":\"state_matrix_only\",\"hold_flash\":1}");
      return;
    }
    snprintf(id, sizeof id, "coord_%llu", (unsigned long long)C->pack_seq + 1);
    gk_ingest(C, id, body, body_n, now);
    gk_consolidate(C, now);
    {
      char hex[65];
      smx_sha256_hex(&C->matrix, hex);
      snprintf(resp, sizeof resp,
               "{\"ok\":true,\"ingested\":true,\"grade\":\"%s\","
               "\"bits_set\":%u,\"seq\":%llu,\"sha256\":\"%s\","
               "\"share\":\"state_matrix_only\"}",
               C->grade, C->matrix.bits_set,
               (unsigned long long)C->matrix.seq, hex);
    }
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  /* Contract lifecycle — product bus accepts sealed plates only; HTTP is ops */
  if (!strcmp(path, "/v1/contract/form")) {
    char assignee[64], task[512], sha[72], cdir[400];
    int digit = -1, min_set = 0;
    grokium_contract c;
    if (strcmp(method, "POST") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    if (!body || body_n == 0) {
      http_reply(cfd, 400, "application/json",
                 "{\"ok\":false,\"error\":\"need_json_body\"}");
      return;
    }
    assignee[0] = task[0] = sha[0] = 0;
    json_get_str(body, body_n, "assignee", assignee, sizeof assignee);
    json_get_str(body, body_n, "task", task, sizeof task);
    json_get_str(body, body_n, "smx_sha256", sha, sizeof sha);
    json_get_str(body, body_n, "smx_sha", sha, sizeof sha);
    digit = json_get_int(body, body_n, "digit", -1);
    min_set = json_get_int(body, body_n, "min_set", 0);
    if (!assignee[0] || !task[0]) {
      http_reply(cfd, 400, "application/json",
                 "{\"ok\":false,\"error\":\"need_assignee_and_task\"}");
      return;
    }
    snprintf(cdir, sizeof cdir, "%s/contracts", root);
    if (grokium_contract_form(&c, cdir, assignee, task, digit, min_set,
                              sha[0] ? sha : NULL) != 0) {
      http_reply(cfd, 500, "application/json",
                 "{\"ok\":false,\"error\":\"form_failed\"}");
      return;
    }
    snprintf(resp, sizeof resp,
             "{\"ok\":true,\"id\":\"%s\",\"path\":\"%s\",\"status\":\"open\","
             "\"assignee\":\"%s\",\"hold_flash\":1,\"wire\":\"smx2\","
             "\"observer\":\"NexusCore\"}",
             c.id, c.path, c.assignee);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/contract/validate")) {
    char cpath[512], bits[GROKIUM_CELLS + 8];
    grokium_contract c;
    grokium_smx m;
    int rc, dig;
    if (strcmp(method, "POST") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    if (!body || body_n == 0) {
      http_reply(cfd, 400, "application/json",
                 "{\"ok\":false,\"error\":\"need_json_body\"}");
      return;
    }
    cpath[0] = bits[0] = 0;
    json_get_str(body, body_n, "path", cpath, sizeof cpath);
    json_get_str(body, body_n, "bits", bits, sizeof bits);
    if (!cpath[0]) {
      http_reply(cfd, 400, "application/json",
                 "{\"ok\":false,\"error\":\"need_path\"}");
      return;
    }
    if (grokium_contract_load(&c, cpath) != 0) {
      http_reply(cfd, 404, "application/json",
                 "{\"ok\":false,\"error\":\"contract_not_found\"}");
      return;
    }
    smx_clear(&m, "validate");
    if (bits[0])
      smx_ingest_bits_ascii(&m, bits);
    else
      smx_set(&m, 0, 0, 0, 1);
    dig = algocube_digit(&m, c.id);
    rc = grokium_contract_validate(&c, &m, dig);
    snprintf(resp, sizeof resp,
             "{\"ok\":true,\"complete\":%s,\"status\":%d,\"id\":\"%s\","
             "\"path\":\"%s\",\"wire\":\"smx2\"}",
             rc == 1 ? "true" : "false", (int)c.status, c.id, c.path);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/manager/tick") || !strcmp(path, "/v1/contract/manager-tick")) {
    char cdir[400];
    int n;
    if (strcmp(method, "POST") != 0 && strcmp(method, "GET") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    snprintf(cdir, sizeof cdir, "%s/contracts", root);
    n = grokium_manager_motivate_dir(cdir);
    snprintf(resp, sizeof resp,
             "{\"ok\":true,\"motivated\":%d,\"observer\":\"NexusCore\","
             "\"dir\":\"%s\",\"wire\":\"smx_motivate\"}",
             n, cdir);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/instinct")) {
    if (strcmp(method, "GET") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    snprintf(resp, sizeof resp, "{\"ok\":true,\"creed\":\"%s\"}",
             grokium_hive_instinct_creed());
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  http_reply(cfd, 404, "application/json",
             "{\"ok\":false,\"error\":\"not_found\","
             "\"hint\":\"/healthz /v1/status /v1/law /v1/coord "
             "/v1/contract/form /v1/manager/tick /v1/nanobot/status\"}");
}

int grokium_serve(const char *host, int port, gk_consolidator *C, gk_fleet *F,
                  grokium_law *L, const char *data_root) {
  int sfd, cfd, on = 1, max_req = 0, served = 0;
  struct sockaddr_in addr;
  const char *max_env;
  const char *bind_host = host && host[0] ? host : "127.0.0.1";

  if (!host_is_loopback(bind_host)) {
    fprintf(stderr,
            "grokium_serve: refuse non-loopback bind '%s' (law: loopback only)\n",
            bind_host);
    return -1;
  }
  if (port <= 0) port = 17444;

  max_env = getenv("GROKIUM_SERVE_MAX");
  if (max_env && max_env[0]) max_req = atoi(max_env);

  sfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sfd < 0) return -1;
  setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
    close(sfd);
    return -1;
  }
  if (bind(sfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
    close(sfd);
    return -1;
  }
  if (listen(sfd, 8) < 0) {
    close(sfd);
    return -1;
  }

  for (;;) {
    cfd = accept(sfd, NULL, NULL);
    if (cfd < 0) {
      if (errno == EINTR) continue;
      break;
    }
    handle(cfd, C, F, L, data_root);
    close(cfd);
    served++;
    if (max_req > 0 && served >= max_req) break;
  }
  close(sfd);
  return 0;
}
