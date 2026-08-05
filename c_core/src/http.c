/* SPDX-License-Identifier: Apache-2.0
 * Loopback control plane — pure C. Product bus = SMX2; HTTP is lab/ops only.
 * Bind refuses non-loopback. Coord path sanitizes via SMX filter.
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_http.h"
#include "grokium_algocube.h"
#include "grokium_commander.h"
#include "grokium_integrity.h"
#include "grokium_session.h"
#include "grokium_smx_filter.h"
#include "grokium_status_plate.h"
#include "sha256.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define GK_HTTP_REQ_MAX  16384
#define GK_HTTP_BODY_MAX 8192
#define GK_HTTP_RESP_MAX (GROKIUM_CELLS + 8192)

/* Short machine token for JSON plates (drop free-text / path inject). */
static void machine_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in || !in[0]) return;
  for (i = 0; in[i] && o + 1 < cap && o < 64; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
        c == '/')
      out[o++] = (char)c;
    else if (c == ' ' || c == ':' || c == '\\' || c == '"' || c == '\'')
      out[o++] = '_';
  }
  out[o] = 0;
}

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
  case 501: return "Not Implemented";
  case 502: return "Bad Gateway";
  case 503: return "Service Unavailable";
  default: return "Error";
  }
}

/* Detect tools:true / tools:1 / use_tools:true in JSON body (ops honesty). */
static int body_requests_tools(const char *body, size_t n) {
  char tmp[GK_HTTP_BODY_MAX + 1];
  size_t i;
  if (!body || n == 0) return 0;
  if (n >= sizeof tmp) n = sizeof tmp - 1;
  memcpy(tmp, body, n);
  tmp[n] = 0;
  for (i = 0; tmp[i]; i++) {
    if (tmp[i] >= 'A' && tmp[i] <= 'Z') tmp[i] = (char)(tmp[i] + 32);
  }
  if (strstr(tmp, "\"tools\":true") || strstr(tmp, "\"tools\": true"))
    return 1;
  if (strstr(tmp, "\"tools\":1") || strstr(tmp, "\"tools\": 1")) return 1;
  if (strstr(tmp, "\"use_tools\":true") || strstr(tmp, "\"use_tools\": true"))
    return 1;
  if (strstr(tmp, "\"tool_agent\":true")) return 1;
  return 0;
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

/* Lab/ops error plate: dual-wire honesty; error is machine token only. */
static void http_reply_err(int fd, int code, const char *err) {
  char body[384], tok[64];
  machine_token(err, tok, sizeof tok);
  if (!tok[0]) snprintf(tok, sizeof tok, "error");
  snprintf(body, sizeof body,
           "{\"schema\":\"grokium.error.v1\",\"ok\":false,\"error\":\"%s\","
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_on_hot_path\":false,\"llm_is_commander\":false}",
           tok);
  http_reply(fd, code, "application/json", body);
}

/* SSE helpers — lab/ops only; product bus remains SMX2. Bits only. */
static void http_sse_headers(int fd) {
  static const char hdr[] =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream\r\n"
      "Cache-Control: no-cache\r\n"
      "Connection: close\r\n"
      "X-Grokium-Telemetry: off\r\n"
      "X-Grokium-Share: state_matrix_only\r\n"
      "X-Grokium-Product-Wire: smx2\r\n"
      "X-Grokium-Peer-HTTP: lab_ops_only\r\n"
      "\r\n";
  (void)write(fd, hdr, sizeof hdr - 1);
}

static void http_sse_event(int fd, const char *event, const char *data) {
  char line[GK_HTTP_RESP_MAX + 64];
  int n;
  if (!data) data = "";
  if (event && event[0]) {
    n = snprintf(line, sizeof line, "event: %s\ndata: %s\n\n", event, data);
  } else {
    n = snprintf(line, sizeof line, "data: %s\n\n", data);
  }
  if (n > 0) (void)write(fd, line, (size_t)n);
}

/* Snapshot SSE of latest matrix. Sequential serve: short-lived by design
 * so lab/ops does not starve other loopback clients. Real multi-peer talk
 * stays on the product SMX2 bus. */
static void smx_sse_snapshot(int fd, const gk_consolidator *C) {
  char payload[GK_HTTP_RESP_MAX];
  char end[320];
  static const char note[] =
      ": grokium smx stream bits-only state_matrix_only\n\n";
  /* SSE control events carry dual-wire honesty (lab/ops ≠ product bus). */
  http_sse_headers(fd);
  (void)write(fd, note, sizeof note - 1);
  if (!C) {
    http_sse_event(fd, "error",
                   "{\"ok\":false,\"error\":\"no_matrix\","
                   "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
                   "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
                   "\"peer_http_is_product_bus\":false,"
                   "\"llm_is_commander\":false}");
    http_sse_event(fd, "end",
                   "{\"ok\":false,\"mode\":\"snapshot\","
                   "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
                   "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
                   "\"peer_http_is_product_bus\":false,"
                   "\"llm_is_commander\":false}");
    return;
  }
  (void)gk_matrix_json(C, payload, sizeof payload);
  http_sse_event(fd, "smx", payload);
  snprintf(end, sizeof end,
           "{\"ok\":true,\"mode\":\"snapshot\",\"seq\":%llu,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_is_commander\":false}",
           (unsigned long long)C->matrix.seq);
  http_sse_event(fd, "end", end);
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
                         char *path, size_t pcap, char *query, size_t qcap,
                         const char **body, size_t *body_n) {
  const char *sp, *sp2, *hdr_end, *cl;
  size_t mlen, plen, content_len = 0;
  if (!req || !method || !path || mcap < 4 || pcap < 2) return -1;
  if (query && qcap) query[0] = 0;
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
  /* capture + strip query */
  {
    char *q = strchr(path, '?');
    if (q) {
      if (query && qcap > 1)
        snprintf(query, qcap, "%s", q + 1);
      *q = 0;
    }
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

/* query key=value (&-separated); minimal + / %20 decode */
static void query_get_param(const char *query, const char *key, char *out,
                            size_t cap) {
  char pat[72];
  const char *p, *amp;
  size_t klen, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!query || !key || !key[0]) return;
  snprintf(pat, sizeof pat, "%s=", key);
  klen = strlen(pat);
  p = query;
  while (p && *p) {
    if (!strncmp(p, pat, klen) && (p == query || p[-1] == '&')) {
      p += klen;
      amp = strchr(p, '&');
      while (*p && p != amp && o + 1 < cap) {
        if (*p == '+') {
          out[o++] = ' ';
          p++;
        } else if (*p == '%' && p[1] && p[2]) {
          unsigned int v = 0;
          if (sscanf(p + 1, "%2x", &v) == 1) {
            out[o++] = (char)v;
            p += 3;
          } else {
            out[o++] = *p++;
          }
        } else {
          out[o++] = *p++;
        }
      }
      out[o] = 0;
      return;
    }
    amp = strchr(p, '&');
    p = amp ? amp + 1 : NULL;
  }
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

/* Escape for JSON string value; drops other C0 controls. */
static size_t json_escape(const char *in, char *out, size_t cap) {
  size_t o = 0;
  if (!out || cap < 2) return 0;
  out[0] = 0;
  if (!in) return 0;
  for (; *in && o + 2 < cap; in++) {
    unsigned char c = (unsigned char)*in;
    if (c == '"' || c == '\\') {
      if (o + 3 >= cap) break;
      out[o++] = '\\';
      out[o++] = (char)c;
    } else if (c == '\n') {
      if (o + 3 >= cap) break;
      out[o++] = '\\';
      out[o++] = 'n';
    } else if (c == '\r') {
      if (o + 3 >= cap) break;
      out[o++] = '\\';
      out[o++] = 'r';
    } else if (c == '\t') {
      if (o + 3 >= cap) break;
      out[o++] = '\\';
      out[o++] = 't';
    } else if (c < 0x20) {
      continue;
    } else {
      out[o++] = (char)c;
    }
  }
  out[o] = 0;
  return o;
}

/* Pull first non-empty JSON string field "key":"..." from body. */
static int extract_json_string_field(const char *raw, const char *key, char *out,
                                     size_t cap) {
  const char *p;
  size_t klen;
  if (!raw || !key || !out || cap < 2) return -1;
  out[0] = 0;
  klen = strlen(key);
  p = raw;
  while ((p = strstr(p, key)) != NULL) {
    const char *v;
    size_t o = 0;
    if (p > raw && p[-1] != '"') {
      p += klen;
      continue;
    }
    if (p[klen] != '"') {
      p += klen;
      continue;
    }
    v = p + klen + 1;
    while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
    if (*v != ':') {
      p += klen;
      continue;
    }
    v++;
    while (*v == ' ' || *v == '\t') v++;
    if (*v == 'n' && !strncmp(v, "null", 4)) {
      p = v + 4;
      continue;
    }
    if (*v != '"') {
      p += klen;
      continue;
    }
    v++;
    while (*v && o + 1 < cap) {
      if (*v == '\\' && v[1]) {
        v++;
        if (*v == 'n')
          out[o++] = '\n';
        else if (*v == 'r')
          out[o++] = '\r';
        else if (*v == 't')
          out[o++] = '\t';
        else
          out[o++] = *v;
        v++;
        continue;
      }
      if (*v == '"') break;
      out[o++] = *v++;
    }
    out[o] = 0;
    if (o > 0) return 0;
    p = v;
  }
  return -1;
}

static int law_dir_for(const char *data_root, char *out, size_t cap) {
  const char *e = getenv("GROKIUM_LAW_DIR");
  if (e && e[0]) {
    snprintf(out, cap, "%s", e);
    return 0;
  }
  snprintf(out, cap, "%s/law", data_root && data_root[0] ? data_root : "data");
  return 0;
}

/* Match smx_filter contract_dir: env override, else {data_root}/contracts. */
static void contract_dir_for(const char *data_root, char *out, size_t cap) {
  const char *e = getenv("GROKIUM_CONTRACT_DIR");
  if (e && e[0]) {
    snprintf(out, cap, "%s", e);
    return;
  }
  snprintf(out, cap, "%s/contracts",
           data_root && data_root[0] ? data_root : "data");
}

static int load_commander(const char *data_root, gk_commander *C) {
  char law[400], pk[420];
  if (!C) return -1;
  memset(C, 0, sizeof *C);
  law_dir_for(data_root, law, sizeof law);
  if (gk_commander_load(C, law) == 0) return 0;
  snprintf(pk, sizeof pk, "%s/commander.pk", law);
  return gk_commander_load_pk_only(C, pk);
}

static void json_status(const grokium_law *L, const gk_consolidator *C,
                        const gk_fleet *F, int alive, char *out, size_t cap) {
  /* Shared dual-wire plate (host CLI/TUI use same formatter). */
  (void)gk_status_plate_json("loopback_http", L ? L->hold_flash : 1,
                             F ? F->n : 0, alive, C ? C->matrix.bits_set : 0,
                             C ? C->grade : "EMPTY", out, cap);
}

static void handle(int cfd, gk_consolidator *C, gk_fleet *F, grokium_law *L,
                   const char *data_root) {
  char req[GK_HTTP_REQ_MAX];
  char method[16], path[256], query[256];
  char resp[GK_HTTP_RESP_MAX];
  const char *body = NULL;
  size_t req_n = 0, body_n = 0;
  int pr;
  double now = (double)time(NULL);
  const char *root = data_root && data_root[0] ? data_root : "data";

  if (read_request(cfd, req, sizeof req, &req_n) != 0) {
    http_reply_err(cfd, 400, "empty_request");
    return;
  }
  pr = parse_request(req, req_n, method, sizeof method, path, sizeof path,
                     query, sizeof query, &body, &body_n);
  if (pr == -2) {
    http_reply_err(cfd, 413, "body_too_large");
    return;
  }
  if (pr != 0) {
    http_reply_err(cfd, 400, "bad_request");
    return;
  }

  if (!strcmp(path, "/healthz") || !strcmp(path, "/")) {
    if (strcmp(method, "GET") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    /* Shared dual-wire liveness plate (serve CLI healthz same builder). */
    gk_healthz_json(resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  /* Minimal lab/ops UI — not product chat; dual-wire honesty plate. */
  if (!strcmp(path, "/ui") || !strcmp(path, "/ui/")) {
    char html[4096], grade_tok[32];
    int n;
    if (strcmp(method, "GET") != 0) {
      http_reply(cfd, 405, "text/plain", "method\n");
      return;
    }
    machine_token(C ? C->grade : "EMPTY", grade_tok, sizeof grade_tok);
    if (!grade_tok[0]) snprintf(grade_tok, sizeof grade_tok, "EMPTY");
    n = snprintf(
        html, sizeof html,
        "<!DOCTYPE html>\n"
        "<html lang=\"en\"><head><meta charset=\"utf-8\"/>\n"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"/>\n"
        "<meta name=\"grokium-telemetry\" content=\"off\"/>\n"
        "<title>Grokium lab/ops</title>\n"
        "<style>\n"
        "body{font:15px/1.45 system-ui,sans-serif;max-width:44rem;margin:2rem auto;"
        "padding:0 1rem;background:#0b0f14;color:#e6edf3}\n"
        "h1{font-size:1.25rem;margin:0 0 .5rem}\n"
        "code,a{color:#7ee787} a{text-decoration:none} a:hover{text-decoration:underline}\n"
        "ul{padding-left:1.2rem} .plate{border:1px solid #30363d;border-radius:8px;"
        "padding:1rem;background:#161b22;margin:1rem 0}\n"
        ".muted{color:#8b949e;font-size:.9rem}\n"
        "</style></head><body>\n"
        "<h1>Grokium · loopback lab/ops</h1>\n"
        "<p class=\"muted\">Not affiliated with xAI. Product bus = "
        "<strong>SMX2</strong>; this HTTP surface is lab/ops only. "
        "Commander ≠ model. Telemetry off. Share = state matrix only.</p>\n"
        "<div class=\"plate\">\n"
        "<div><b>product_wire</b>: smx2</div>\n"
        "<div><b>peer_http</b>: lab_ops_only</div>\n"
        "<div><b>matrix_bits</b>: %u · <b>grade</b>: %s</div>\n"
        "<div><b>fleet_n</b>: %d · <b>hold_flash</b>: %d</div>\n"
        "<div><b>llm_is_commander</b>: false</div>\n"
        "</div>\n"
        "<p><b>JSON routes</b></p>\n"
        "<ul>\n"
        "<li><a href=\"/healthz\"><code>/healthz</code></a></li>\n"
        "<li><a href=\"/v1/status\"><code>/v1/status</code></a></li>\n"
        "<li><a href=\"/v1/cube/status\"><code>/v1/cube/status</code></a></li>\n"
        "<li><a href=\"/v1/matrix/latest\"><code>/v1/matrix/latest</code></a></li>\n"
        "<li><a href=\"/v1/stream/smx\"><code>/v1/stream/smx</code></a> (SSE snapshot)</li>\n"
        "<li><a href=\"/v1/sessions\"><code>/v1/sessions</code></a> (meta only)</li>\n"
        "<li><a href=\"/v1/nanobot/status\"><code>/v1/nanobot/status</code></a></li>\n"
        "<li><a href=\"/v1/llama/probe\"><code>/v1/llama/probe</code></a></li>\n"
        "<li><a href=\"/v1/commander\"><code>/v1/commander</code></a></li>\n"
        "<li><a href=\"/v1/integrity\"><code>/v1/integrity</code></a></li>\n"
        "</ul>\n"
        "<p class=\"muted\">POST <code>/v1/chat</code> / <code>/v1/agent</code> "
        "(chat-only) and <code>/v1/coord</code>; shell tools remain host TUI / "
        "nanobot. HOLD_FLASH ack_held.</p>\n"
        "</body></html>\n",
        C ? C->matrix.bits_set : 0, grade_tok, F ? F->n : 0,
        L ? L->hold_flash : 1);
    if (n < 0 || (size_t)n >= sizeof html) {
      http_reply(cfd, 500, "text/plain", "ui_overflow\n");
      return;
    }
    http_reply(cfd, 200, "text/html; charset=utf-8", html);
    return;
  }

  if (!strcmp(path, "/v1/status")) {
    if (strcmp(method, "GET") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    json_status(L, C, F, F ? fleet_status(F) : 0, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/cube/status") || !strcmp(path, "/v1/cube")) {
    if (strcmp(method, "GET") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    /* Shared consolidator AlgoCube bridge plate (dual-wire). */
    (void)gk_cube_status_json(C, L ? L->hold_flash : 1, root, resp,
                              sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  /* Imported Grok Build session metas — list/search/pickup (no transcripts). */
  if (!strcmp(path, "/v1/sessions") || !strcmp(path, "/v1/sessions/search")) {
    char q[128];
    q[0] = 0;
    if (strcmp(method, "GET") == 0) {
      query_get_param(query, "q", q, sizeof q);
    } else if (strcmp(method, "POST") == 0) {
      if (body && body_n > 0) {
        if (body[0] == '{')
          json_get_str(body, body_n, "q", q, sizeof q);
        else {
          size_t n = body_n < sizeof q - 1 ? body_n : sizeof q - 1;
          memcpy(q, body, n);
          q[n] = 0;
        }
      }
    } else {
      http_reply_err(cfd, 405, "method");
      return;
    }
    gk_session_list_json(root, q, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/sessions/pickup") ||
      !strncmp(path, "/v1/sessions/", 13)) {
    char id[96];
    int rc;
    id[0] = 0;
    if (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!strcmp(path, "/v1/sessions/pickup")) {
      query_get_param(query, "id", id, sizeof id);
      if (!id[0] && body && body_n > 0) {
        if (body[0] == '{')
          json_get_str(body, body_n, "id", id, sizeof id);
        else {
          size_t n = body_n < sizeof id - 1 ? body_n : sizeof id - 1;
          memcpy(id, body, n);
          id[n] = 0;
        }
      }
    } else {
      /* /v1/sessions/<id> */
      snprintf(id, sizeof id, "%s", path + 13);
    }
    if (!id[0]) {
      /* Meta-only deny plate: dual-wire honesty (lab/ops ≠ product bus). */
      (void)gk_session_pickup_deny_json(NULL, "need_session_id", resp,
                                        sizeof resp);
      http_reply(cfd, 400, "application/json", resp);
      return;
    }
    rc = gk_session_pickup_json(root, id, resp, sizeof resp);
    http_reply(cfd, rc == 0 ? 200 : 404, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/law")) {
    if (strcmp(method, "GET") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    /* Shared Cube Standards dual-wire plate (host CLI / TUI same builder). */
    grokium_law_json(L, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/nanobot/status")) {
    if (strcmp(method, "GET") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!F) {
      http_reply_err(cfd, 500, "no_fleet");
      return;
    }
    /* Shared fleet plate (pid/status honest; dual-wire). */
    fleet_status_json(F, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/nanobot/deploy")) {
    char plate[512];
    if (strcmp(method, "POST") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!F) {
      http_reply_err(cfd, 500, "no_fleet");
      return;
    }
    fleet_deploy(F);
    snprintf(plate, sizeof plate, "%s/home/FLEET.json", root);
    fleet_save(F, plate);
    /* Shared deploy ack (homes only; spawn is host responsibility). */
    fleet_deploy_json(F, plate, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/nanobot/separate") ||
      !strcmp(path, "/v1/nanobot/spawn")) {
    char plate[512], id[64], id_raw[64], id_tok[64];
    const char *src;
    size_t i, j;
    int do_spawn = !strcmp(path, "/v1/nanobot/spawn");
    if (strcmp(method, "POST") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!F) {
      /* Schema-scoped fleet deny (shared with fleet_cli dual-wire). */
      if (do_spawn)
        fleet_spawn_err_json("no_fleet", resp, sizeof resp);
      else
        fleet_separate_err_json("no_fleet", resp, sizeof resp);
      http_reply(cfd, 500, "application/json", resp);
      return;
    }
    /* body: raw bot id, empty=spawn-all, or {"id":"nb-…"} — token only. */
    id_raw[0] = id[0] = 0;
    src = body && body_n ? body : "";
    if (src[0] == '{') {
      const char *k = strstr(src, "\"id\"");
      if (k) {
        k = strchr(k + 4, '"');
        if (k) {
          k++;
          for (j = 0; j + 1 < sizeof id_raw && k[j] && k[j] != '"'; j++)
            id_raw[j] = k[j];
          id_raw[j] = 0;
        }
      }
    } else {
      for (i = 0, j = 0; i < body_n && j + 1 < sizeof id_raw; i++) {
        if (src[i] == '\n' || src[i] == '\r' || src[i] == ' ') continue;
        id_raw[j++] = src[i];
      }
      id_raw[j] = 0;
    }
    /* Bot ids are machine tokens only (alnum/_/-); drop inject chars. */
    machine_token(id_raw, id_tok, sizeof id_tok);
    {
      size_t o = 0;
      for (i = 0; id_tok[i] && o + 1 < sizeof id; i++) {
        unsigned char c = (unsigned char)id_tok[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-')
          id[o++] = (char)c;
      }
      id[o] = 0;
    }
    if (do_spawn) {
      int n;
      if (id[0]) {
        if (fleet_spawn(F, id) != 0) {
          fleet_spawn_err_json("spawn_failed", resp, sizeof resp);
          http_reply(cfd, 500, "application/json", resp);
          return;
        }
        n = 1;
      } else {
        n = fleet_spawn_all(F);
        if (n < 0) {
          fleet_spawn_err_json("spawn_all_failed", resp, sizeof resp);
          http_reply(cfd, 500, "application/json", resp);
          return;
        }
      }
      snprintf(plate, sizeof plate, "%s/home/FLEET.json", root);
      fleet_save(F, plate);
      /* Shared spawn plate with fleet CLI (pid honesty when single bot). */
      fleet_spawn_json(F, id[0] ? id : "*", n, plate, resp, sizeof resp);
      http_reply(cfd, 200, "application/json", resp);
      return;
    }
    if (!id[0]) {
      fleet_separate_err_json("need_bot_id", resp, sizeof resp);
      http_reply(cfd, 400, "application/json", resp);
      return;
    }
    if (fleet_separate(F, id) != 0) {
      fleet_separate_err_json("unknown_bot", resp, sizeof resp);
      http_reply(cfd, 404, "application/json", resp);
      return;
    }
    snprintf(plate, sizeof plate, "%s/home/FLEET.json", root);
    fleet_save(F, plate);
    fleet_separate_json(id, plate, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  /* Host/hub records external spawn pid (or clear with pid<=0 / null). */
  if (!strcmp(path, "/v1/nanobot/note-pid")) {
    char plate[512], id[64], id_raw[64], id_tok[64], pid_tok[32];
    const char *src;
    size_t i, j, o;
    int pid = 0, have_pid = 0, is_json;
    if (strcmp(method, "POST") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!F) {
      fleet_note_pid_err_json("no_fleet", resp, sizeof resp);
      http_reply(cfd, 500, "application/json", resp);
      return;
    }
    id_raw[0] = id[0] = pid_tok[0] = 0;
    src = body && body_n ? body : "";
    is_json = src[0] == '{';
    if (is_json) {
      const char *k = strstr(src, "\"id\"");
      const char *p = strstr(src, "\"pid\"");
      if (k) {
        k = strchr(k + 4, '"');
        if (k) {
          k++;
          for (j = 0; j + 1 < sizeof id_raw && k[j] && k[j] != '"'; j++)
            id_raw[j] = k[j];
          id_raw[j] = 0;
        }
      }
      if (p) {
        p = strchr(p + 5, ':');
        if (p) {
          p++;
          while (*p == ' ' || *p == '\t') p++;
          have_pid = 1;
          if (*p == 'n' || *p == 'N')
            pid = -1; /* JSON null → clear */
          else
            pid = (int)strtol(p, NULL, 10);
        }
      }
    } else {
      /* body: "BOT_ID PID" whitespace-separated machine tokens. */
      o = 0;
      for (i = 0; i < body_n && src[i] && src[i] != ' ' && src[i] != '\t' &&
                  src[i] != '\n' && src[i] != '\r' && o + 1 < sizeof id_raw;
           i++)
        id_raw[o++] = src[i];
      id_raw[o] = 0;
      while (i < body_n &&
             (src[i] == ' ' || src[i] == '\t' || src[i] == '\n' ||
              src[i] == '\r'))
        i++;
      o = 0;
      for (; i < body_n && src[i] && src[i] != ' ' && src[i] != '\t' &&
             src[i] != '\n' && src[i] != '\r' && o + 1 < sizeof pid_tok;
           i++)
        pid_tok[o++] = src[i];
      pid_tok[o] = 0;
      if (pid_tok[0]) {
        have_pid = 1;
        pid = (int)strtol(pid_tok, NULL, 10);
      }
    }
    machine_token(id_raw, id_tok, sizeof id_tok);
    o = 0;
    for (i = 0; id_tok[i] && o + 1 < sizeof id; i++) {
      unsigned char c = (unsigned char)id_tok[i];
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')
        id[o++] = (char)c;
    }
    id[o] = 0;
    if (!id[0] || !have_pid) {
      fleet_note_pid_err_json("need_bot_id_pid", resp, sizeof resp);
      http_reply(cfd, 400, "application/json", resp);
      return;
    }
    if (fleet_note_pid(F, id, pid) != 0) {
      fleet_note_pid_err_json("unknown_bot", resp, sizeof resp);
      http_reply(cfd, 404, "application/json", resp);
      return;
    }
    snprintf(plate, sizeof plate, "%s/home/FLEET.json", root);
    fleet_save(F, plate);
    fleet_note_pid_json(F, id, plate, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  /* SIGTERM all live bots; honest alive count after clear. */
  if (!strcmp(path, "/v1/nanobot/stop-all")) {
    char plate[512];
    if (strcmp(method, "POST") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!F) {
      fleet_stop_err_json("no_fleet", resp, sizeof resp);
      http_reply(cfd, 500, "application/json", resp);
      return;
    }
    fleet_stop_all(F);
    snprintf(plate, sizeof plate, "%s/home/FLEET.json", root);
    fleet_save(F, plate);
    fleet_stop_json(F, plate, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  /* Re-probe pids and rewrite honest FLEET.json plate. */
  if (!strcmp(path, "/v1/nanobot/save")) {
    char plate[512];
    if (strcmp(method, "POST") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!F) {
      fleet_save_err_json("no_fleet", resp, sizeof resp);
      http_reply(cfd, 500, "application/json", resp);
      return;
    }
    snprintf(plate, sizeof plate, "%s/home/FLEET.json", root);
    fleet_save(F, plate);
    fleet_save_json(F, plate, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/matrix/latest") || !strcmp(path, "/v1/stream/smx/latest")) {
    if (strcmp(method, "GET") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!C) {
      http_reply_err(cfd, 500, "no_matrix");
      return;
    }
    /* Shared consolidator dual-wire SMX plate (full bits, grade). */
    (void)gk_matrix_json(C, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  /* SSE snapshot of latest SMX (bits only). Not long-lived fan-out;
   * sequential accept loop; product multi-peer bus remains SMX2. */
  if (!strcmp(path, "/v1/stream/smx")) {
    if (strcmp(method, "GET") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    smx_sse_snapshot(cfd, C);
    return;
  }

  if (!strcmp(path, "/v1/ability")) {
    if (strcmp(method, "GET") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!C) {
      http_reply_err(cfd, 500, "no_consolidator");
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
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!C || !body || body_n == 0) {
      /* Shared coord deny (CLI consolidate + host /coord). */
      gk_coord_err_json("need_plate", resp, sizeof resp);
      http_reply(cfd, 400, "application/json", resp);
      return;
    }
    /* sanitize: prose / hold_flash=0 / non-SMX denied (external origin) */
    allow = grokium_smx_filter_allow_frame(L, (const uint8_t *)body, body_n, 1);
    if (!allow) {
      gk_coord_err_json("smx_filter_deny", resp, sizeof resp);
      http_reply(cfd, 403, "application/json", resp);
      return;
    }
    snprintf(id, sizeof id, "coord_%llu", (unsigned long long)C->pack_seq + 1);
    gk_ingest(C, id, body, body_n, now);
    gk_consolidate(C, now);
    /* Shared coord success plate with consolidate CLI / host path. */
    gk_coord_json(C, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  /* Contract lifecycle — product bus accepts sealed plates only; HTTP is ops */
  if (!strcmp(path, "/v1/contract/form")) {
    char assignee[64], task[512], sha[72], cdir[400];
    int digit = -1, min_set = 0;
    grokium_contract c;
    if (strcmp(method, "POST") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!body || body_n == 0) {
      /* Schema-scoped dual-wire (shared with smx-filter CLI). */
      grokium_contract_form_err_json("need_json_body", resp, sizeof resp);
      http_reply(cfd, 400, "application/json", resp);
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
      grokium_contract_form_err_json("need_assignee_and_task", resp,
                                     sizeof resp);
      http_reply(cfd, 400, "application/json", resp);
      return;
    }
    contract_dir_for(root, cdir, sizeof cdir);
    if (grokium_contract_form(&c, cdir, assignee, task, digit, min_set,
                              sha[0] ? sha : NULL) != 0) {
      grokium_contract_form_err_json("form_failed", resp, sizeof resp);
      http_reply(cfd, 500, "application/json", resp);
      return;
    }
    /* Shared form plate with smx-filter CLI (product bus remains SMX2). */
    grokium_contract_form_json(&c, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/contract/validate")) {
    char cpath[512], bits[GROKIUM_CELLS + 8];
    grokium_contract c;
    grokium_smx m;
    int rc, dig;
    if (strcmp(method, "POST") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!body || body_n == 0) {
      grokium_contract_validate_err_json("need_json_body", resp, sizeof resp);
      http_reply(cfd, 400, "application/json", resp);
      return;
    }
    cpath[0] = bits[0] = 0;
    json_get_str(body, body_n, "path", cpath, sizeof cpath);
    json_get_str(body, body_n, "bits", bits, sizeof bits);
    if (!cpath[0]) {
      grokium_contract_validate_err_json("need_path", resp, sizeof resp);
      http_reply(cfd, 400, "application/json", resp);
      return;
    }
    if (grokium_contract_load(&c, cpath) != 0) {
      grokium_contract_validate_err_json("contract_not_found", resp,
                                         sizeof resp);
      http_reply(cfd, 404, "application/json", resp);
      return;
    }
    smx_clear(&m, "validate");
    if (bits[0])
      smx_ingest_bits_ascii(&m, bits);
    else
      smx_set(&m, 0, 0, 0, 1);
    dig = algocube_digit(&m, c.id);
    rc = grokium_contract_validate(&c, &m, dig);
    /* Shared validate plate with smx-filter CLI. */
    grokium_contract_validate_json(&c, rc, dig, m.bits_set, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/manager/tick") || !strcmp(path, "/v1/contract/manager-tick")) {
    char cdir[400];
    int n;
    if (strcmp(method, "POST") != 0 && strcmp(method, "GET") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    contract_dir_for(root, cdir, sizeof cdir);
    n = grokium_manager_motivate_dir(cdir);
    /* Shared manager-tick plate with smx-filter CLI (lab/ops only). */
    grokium_manager_tick_json(n, cdir, resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/instinct")) {
    if (strcmp(method, "GET") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    /* Shared instinct plate with smx-filter CLI (product bus remains SMX2). */
    grokium_instinct_json(resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/license")) {
    if (strcmp(method, "GET") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    /* Shared dual-wire license plate (host CLI/TUI · serve CLI same builder). */
    grokium_license_json(resp, sizeof resp);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/llama/probe") || !strcmp(path, "/v1/llama")) {
    if (strcmp(method, "GET") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (grokium_llama_probe(resp, sizeof resp) != 0) {
      http_reply_err(cfd, 500, "probe_failed");
      return;
    }
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  /* Local-first chat (lab/ops). LLM ≠ commander. Tool agent remains host path. */
  if (!strcmp(path, "/v1/chat")) {
    char msg[2048];
    int code;
    if (strcmp(method, "POST") != 0) {
      /* Shared dual-wire chat deny (serve CLI / host same builder). */
      grokium_chat_err_json("method", NULL, resp, sizeof resp);
      http_reply(cfd, 405, "application/json", resp);
      return;
    }
    msg[0] = 0;
    if (body && body_n > 0) {
      if (body[0] == '{') {
        if (json_get_str(body, body_n, "message", msg, sizeof msg) != 0 &&
            json_get_str(body, body_n, "content", msg, sizeof msg) != 0 &&
            json_get_str(body, body_n, "prompt", msg, sizeof msg) != 0)
          msg[0] = 0;
      } else {
        size_t n = body_n < sizeof msg - 1 ? body_n : sizeof msg - 1;
        memcpy(msg, body, n);
        msg[n] = 0;
        /* trim trailing CR/LF */
        while (n > 0 && (msg[n - 1] == '\n' || msg[n - 1] == '\r'))
          msg[--n] = 0;
      }
    }
    if (!msg[0]) {
      grokium_chat_err_json("need_message", "{\"message\":\"...\"}", resp,
                            sizeof resp);
      http_reply(cfd, 400, "application/json", resp);
      return;
    }
    if (grokium_llama_chat(msg, resp, sizeof resp) != 0) {
      grokium_chat_err_json("chat_failed", NULL, resp, sizeof resp);
      http_reply(cfd, 500, "application/json", resp);
      return;
    }
    if (strstr(resp, "\"ok\":true"))
      code = 200;
    else if (strstr(resp, "\"reachable\":false"))
      code = 503;
    else
      code = 502;
    http_reply(cfd, code, "application/json", resp);
    return;
  }

  /*
   * Lab/ops agent-lite: local chat only. Shell/tool loops stay on host
   * nanobot (embeddable core). Never elevates LLM to Commander.
   */
  if (!strcmp(path, "/v1/agent")) {
    char msg[2048], chat[GK_HTTP_RESP_MAX], content[2048];
    char errf[96];
    int code, ok;
    if (strcmp(method, "POST") != 0) {
      /* Shared dual-wire agent deny (tools always off on lab/ops path). */
      grokium_agent_err_json("method", NULL, resp, sizeof resp);
      http_reply(cfd, 405, "application/json", resp);
      return;
    }
    if (body_requests_tools(body, body_n)) {
      grokium_agent_err_json("tools_not_on_lab_ops", NULL, resp, sizeof resp);
      http_reply(cfd, 501, "application/json", resp);
      return;
    }
    msg[0] = 0;
    if (body && body_n > 0) {
      if (body[0] == '{') {
        if (json_get_str(body, body_n, "message", msg, sizeof msg) != 0 &&
            json_get_str(body, body_n, "content", msg, sizeof msg) != 0 &&
            json_get_str(body, body_n, "prompt", msg, sizeof msg) != 0 &&
            json_get_str(body, body_n, "task", msg, sizeof msg) != 0)
          msg[0] = 0;
      } else {
        size_t n = body_n < sizeof msg - 1 ? body_n : sizeof msg - 1;
        memcpy(msg, body, n);
        msg[n] = 0;
        while (n > 0 && (msg[n - 1] == '\n' || msg[n - 1] == '\r'))
          msg[--n] = 0;
      }
    }
    if (!msg[0]) {
      grokium_agent_err_json("need_message", NULL, resp, sizeof resp);
      http_reply(cfd, 400, "application/json", resp);
      return;
    }
    if (grokium_llama_chat(msg, chat, sizeof chat) != 0) {
      grokium_agent_err_json("agent_chat_failed", NULL, resp, sizeof resp);
      http_reply(cfd, 500, "application/json", resp);
      return;
    }
    ok = strstr(chat, "\"ok\":true") != NULL;
    content[0] = errf[0] = 0;
    if (ok) {
      if (extract_json_string_field(chat, "content", content, sizeof content) !=
          0)
        content[0] = 0;
    } else {
      if (extract_json_string_field(chat, "error", errf, sizeof errf) != 0)
        snprintf(errf, sizeof errf, "chat_failed");
    }
    if (ok) {
      /* Shared dual-wire success (content escaped inside builder). */
      grokium_agent_ok_json(content, resp, sizeof resp);
      code = 200;
    } else if (strstr(chat, "\"reachable\":false")) {
      /* Shared dual-wire deny; errf is free-text so builder sanitizes. */
      grokium_agent_err_json(errf[0] ? errf : "unreachable", NULL, resp,
                             sizeof resp);
      code = 503;
    } else {
      grokium_agent_err_json(errf[0] ? errf : "no_content", NULL, resp,
                             sizeof resp);
      code = 502;
    }
    http_reply(cfd, code, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/integrity") || !strcmp(path, "/v1/integrity/tick")) {
    int rc;
    const char *rroot = getenv("GROKIUM_ROOT");
    if (strcmp(method, "GET") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!rroot || !rroot[0]) rroot = ".";
    rc = gk_integrity_tick(rroot, resp, sizeof resp);
    http_reply(cfd, rc == 1 ? 200 : 503, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/integrity/policy")) {
    const char *rroot = getenv("GROKIUM_ROOT");
    if (strcmp(method, "GET") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!rroot || !rroot[0]) rroot = ".";
    if (gk_integrity_policy(rroot, resp, sizeof resp) != 0) {
      http_reply(cfd, 404, "application/json", resp);
      return;
    }
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/integrity/reseal")) {
    const char *rroot = getenv("GROKIUM_ROOT");
    if (strcmp(method, "POST") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!rroot || !rroot[0]) rroot = ".";
    if (gk_integrity_reseal(rroot, resp, sizeof resp) != 0) {
      http_reply(cfd, 500, "application/json", resp);
      return;
    }
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  /* Commander = Ed25519 law identity only — never a model claim */
  if (!strcmp(path, "/v1/commander") || !strcmp(path, "/v1/commander/show")) {
    gk_commander cmd;
    char law[400];
    if (strcmp(method, "GET") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    law_dir_for(root, law, sizeof law);
    if (load_commander(root, &cmd) != 0) {
      snprintf(resp, sizeof resp,
               "{\"schema\":\"grokium.commander.v1\",\"ok\":false,"
               "\"error\":\"no_commander_pk\",\"law_dir\":\"%s\","
               "\"hint\":\"grokium-commander keygen --law-dir DIR\","
               "\"not\":\"grok_model\",\"llm_is_commander\":false,"
               "\"commander_is_model\":false,\"product_wire\":\"smx2\","
               "\"peer_http\":\"lab_ops_only\","
               "\"peer_http_is_product_bus\":false,"
               "\"share\":\"state_matrix_only\",\"hold_flash\":1}",
               law);
      http_reply(cfd, 404, "application/json", resp);
      return;
    }
    /* never emit sk; has_sk only for ops honesty on loopback */
    snprintf(resp, sizeof resp,
             "{\"schema\":\"grokium.commander.v1\",\"ok\":true,"
             "\"product\":\"grokium\",\"not\":\"grok_model\","
             "\"domain\":\"%s\",\"fingerprint\":\"%s\",\"has_sk\":%s,"
             "\"unforgeable\":true,\"law_dir\":\"%s\","
             "\"commander_is_model\":false,\"llm_is_commander\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1}",
             GK_CMD_DOMAIN, cmd.fingerprint_hex,
             cmd.has_sk ? "true" : "false", law);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/commander/reject_model")) {
    int deny;
    if (strcmp(method, "POST") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    deny = 1;
    if (body && body_n > 0) {
      /* any model-as-authority claim is denied */
      if (gk_commander_is_grokium_not_model(body))
        deny = 0; /* explicit grokium law plate — not a model claim */
      else
        deny = 1;
    }
    if (deny) {
      http_reply(cfd, 403, "application/json",
                 "{\"schema\":\"grokium.commander_reject.v1\",\"ok\":false,"
                 "\"allowed\":false,\"error\":\"model_is_not_commander\","
                 "\"product\":\"grokium\",\"not\":\"grok_model\","
                 "\"unforgeable\":true,\"llm_is_commander\":false,"
                 "\"commander_is_model\":false,\"product_wire\":\"smx2\","
                 "\"peer_http\":\"lab_ops_only\","
                 "\"peer_http_is_product_bus\":false,"
                 "\"share\":\"state_matrix_only\",\"hold_flash\":1}");
      return;
    }
    http_reply(cfd, 200, "application/json",
               "{\"schema\":\"grokium.commander_reject.v1\",\"ok\":true,"
               "\"allowed\":true,\"product\":\"grokium\","
               "\"not\":\"grok_model\",\"llm_is_commander\":false,"
               "\"commander_is_model\":false,\"product_wire\":\"smx2\","
               "\"peer_http\":\"lab_ops_only\","
               "\"peer_http_is_product_bus\":false,"
               "\"share\":\"state_matrix_only\",\"hold_flash\":1}");
    return;
  }

  if (!strcmp(path, "/v1/commander/verify")) {
    gk_commander cmd;
    char device[64], action[64], nonce[80], sig[160];
    int64_t ts = 0;
    int ok;
    if (strcmp(method, "POST") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    if (!body || body_n == 0) {
      http_reply_err(cfd, 400, "need_json_body");
      return;
    }
    if (load_commander(root, &cmd) != 0 || !cmd.has_pk) {
      http_reply_err(cfd, 404, "no_commander_pk");
      return;
    }
    device[0] = action[0] = nonce[0] = sig[0] = 0;
    json_get_str(body, body_n, "device", device, sizeof device);
    json_get_str(body, body_n, "action", action, sizeof action);
    json_get_str(body, body_n, "nonce", nonce, sizeof nonce);
    json_get_str(body, body_n, "sig", sig, sizeof sig);
    ts = (int64_t)json_get_int(body, body_n, "ts", 0);
    if (!device[0] || !action[0] || !nonce[0] || !sig[0] || ts == 0) {
      http_reply_err(cfd, 400, "need_device_action_nonce_ts_sig");
      return;
    }
    ok = gk_commander_verify_override(&cmd, device, action, nonce, ts, NULL, 0,
                                      sig);
    snprintf(resp, sizeof resp,
             "{\"schema\":\"grokium.commander_verify.v1\",\"ok\":%s,"
             "\"commander\":%s,\"not\":\"grok_model\","
             "\"unforgeable\":true,\"product\":\"grokium\","
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1}",
             ok ? "true" : "false", ok ? "\"grokium\"" : "null");
    http_reply(cfd, ok ? 200 : 403, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/commander/sign")) {
    gk_commander cmd;
    char device[64], action[64], nonce_hex[65], sig_hex[129], env[2048];
    int64_t ts = 0;
    if (strcmp(method, "POST") != 0) {
      http_reply_err(cfd, 405, "method");
      return;
    }
    /* loopback-only already enforced by bind; still require sk on disk */
    if (load_commander(root, &cmd) != 0 || !cmd.has_sk) {
      http_reply(cfd, 403, "application/json",
                 "{\"schema\":\"grokium.commander.v1\",\"ok\":false,"
                 "\"error\":\"no_commander_sk\","
                 "\"hint\":\"sign only with local commander.sk (never commit)\","
                 "\"not\":\"grok_model\",\"llm_is_commander\":false,"
                 "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
                 "\"peer_http_is_product_bus\":false,"
                 "\"share\":\"state_matrix_only\",\"hold_flash\":1}");
      return;
    }
    if (!body || body_n == 0) {
      http_reply_err(cfd, 400, "need_json_body");
      return;
    }
    device[0] = action[0] = 0;
    json_get_str(body, body_n, "device", device, sizeof device);
    json_get_str(body, body_n, "action", action, sizeof action);
    if (!device[0] || !action[0]) {
      http_reply_err(cfd, 400, "need_device_and_action");
      return;
    }
    if (gk_commander_sign_override(&cmd, device, action, NULL, 0, nonce_hex, &ts,
                                   sig_hex) != 0) {
      http_reply_err(cfd, 500, "sign_failed");
      return;
    }
    gk_commander_envelope_json(&cmd, device, action, nonce_hex, ts, sig_hex,
                               NULL, env, sizeof env);
    http_reply(cfd, 200, "application/json", env);
    return;
  }

  /* Unknown route — dual-wire honesty (lab/ops ≠ product bus; LLM ≠ commander). */
  http_reply(cfd, 404, "application/json",
             "{\"schema\":\"grokium.error.v1\",\"ok\":false,"
             "\"error\":\"not_found\","
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_on_hot_path\":false,\"llm_is_commander\":false,"
             "\"hint\":\"/ui /healthz /v1/status /v1/cube/status /v1/sessions "
             "/v1/commander /v1/chat /v1/agent /v1/coord /v1/stream/smx "
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
