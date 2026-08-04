/* SPDX-License-Identifier: Apache-2.0
 * Loopback control plane — pure C. Product bus = SMX2; HTTP is lab/ops only.
 * Bind refuses non-loopback. Coord path sanitizes via SMX filter.
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_http.h"
#include "grokium_algocube.h"
#include "grokium_commander.h"
#include "grokium_integrity.h"
#include "grokium_smx_filter.h"
#include "sha256.h"
#include <arpa/inet.h>
#include <dirent.h>
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

static void json_matrix(const gk_consolidator *C, char *out, size_t cap);

/* Snapshot SSE of latest matrix. Sequential serve: short-lived by design
 * so lab/ops does not starve other loopback clients. Real multi-peer talk
 * stays on the product SMX2 bus. */
static void smx_sse_snapshot(int fd, const gk_consolidator *C) {
  char payload[GK_HTTP_RESP_MAX];
  char end[192];
  static const char note[] =
      ": grokium smx stream bits-only state_matrix_only\n\n";
  http_sse_headers(fd);
  (void)write(fd, note, sizeof note - 1);
  if (!C) {
    http_sse_event(fd, "error",
                   "{\"ok\":false,\"error\":\"no_matrix\","
                   "\"share\":\"state_matrix_only\"}");
    http_sse_event(fd, "end",
                   "{\"ok\":false,\"mode\":\"snapshot\","
                   "\"share\":\"state_matrix_only\"}");
    return;
  }
  json_matrix(C, payload, sizeof payload);
  http_sse_event(fd, "smx", payload);
  snprintf(end, sizeof end,
           "{\"ok\":true,\"mode\":\"snapshot\",\"seq\":%llu,"
           "\"share\":\"state_matrix_only\",\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\"}",
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

/* Parse http://host:port/path — only loopback targets allowed for probe. */
static int parse_llama_base(const char *base, char *host, size_t hcap, int *port,
                            char *path, size_t pcap) {
  const char *p, *slash;
  char *colon;
  if (!base || !host || !port || !path) return -1;
  if (!strncmp(base, "http://", 7)) p = base + 7;
  else if (!strncmp(base, "https://", 8)) p = base + 8;
  else p = base;
  slash = strchr(p, '/');
  if (slash) {
    size_t hlen = (size_t)(slash - p);
    if (hlen >= hcap) return -1;
    memcpy(host, p, hlen);
    host[hlen] = 0;
    snprintf(path, pcap, "%s", slash);
  } else {
    snprintf(host, hcap, "%s", p);
    snprintf(path, pcap, "/v1");
  }
  colon = strrchr(host, ':');
  if (colon && colon != host) {
    *port = atoi(colon + 1);
    *colon = 0;
  } else {
    *port = 1212;
  }
  if (!host_is_loopback(host)) return -1;
  /* ensure /models path */
  if (!strstr(path, "/models")) {
    char tmp[128];
    size_t n = strlen(path);
    if (n && path[n - 1] == '/') path[n - 1] = 0;
    snprintf(tmp, sizeof tmp, "%s/models", path[0] ? path : "/v1");
    snprintf(path, pcap, "%s", tmp);
  }
  if (*port <= 0) *port = 1212;
  return 0;
}

int grokium_llama_probe(char *json_out, size_t cap) {
  const char *base;
  char host[64], path[128], req[256], buf[2048];
  int port = 1212, fd = -1, code = 0, reachable = 0;
  struct sockaddr_in addr;
  struct timeval tv;
  size_t n = 0;
  ssize_t r;
  const char *err = NULL;
  char model_snip[96];
  if (!json_out || cap < 64) return -1;
  base = getenv("GROKIUM_LLAMA_BASE");
  if (!base || !base[0]) base = getenv("NANOBOT_BASE_URL");
  if (!base || !base[0]) base = "http://127.0.0.1:1212/v1";
  model_snip[0] = 0;
  if (parse_llama_base(base, host, sizeof host, &port, path, sizeof path) != 0) {
    snprintf(json_out, cap,
             "{\"ok\":false,\"reachable\":false,\"error\":\"non_loopback_base\","
             "\"base_url\":\"%s\",\"llm_is_commander\":false,"
             "\"product_wire\":\"smx2\"}",
             base);
    return 0;
  }
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    err = "socket";
    goto done;
  }
  tv.tv_sec = 1;
  tv.tv_usec = 500000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
    err = "pton";
    goto done;
  }
  if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
    err = "connect";
    goto done;
  }
  snprintf(req, sizeof req,
           "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",
           path, port);
  if (write(fd, req, strlen(req)) < 0) {
    err = "write";
    goto done;
  }
  while (n + 1 < sizeof buf) {
    r = read(fd, buf + n, sizeof buf - 1 - n);
    if (r <= 0) break;
    n += (size_t)r;
  }
  buf[n] = 0;
  if (n == 0) {
    err = "empty";
    goto done;
  }
  reachable = 1;
  if (!strncmp(buf, "HTTP/", 5)) {
    const char *sp = strchr(buf, ' ');
    if (sp) code = atoi(sp + 1);
  }
  {
    /* optional model id snippet — no prose dump */
    const char *idk = strstr(buf, "\"id\"");
    if (idk) {
      const char *q = strchr(idk + 4, '"');
      if (q) {
        size_t i = 0;
        q++;
        while (*q && *q != '"' && i + 1 < sizeof model_snip) {
          if (*q == '\\') {
            q++;
            if (!*q) break;
          }
          model_snip[i++] = *q++;
        }
        model_snip[i] = 0;
      }
    }
  }
  err = NULL;
done:
  if (fd >= 0) close(fd);
  if (!reachable) {
    snprintf(json_out, cap,
             "{\"ok\":true,\"reachable\":false,\"http_code\":0,"
             "\"base_url\":\"http://127.0.0.1:%d%s\","
             "\"error\":\"%s\",\"llm_is_commander\":false,"
             "\"commander_is_model\":false,\"product_wire\":\"smx2\","
             "\"share\":\"state_matrix_only\"}",
             port, path, err ? err : "down");
    return 0;
  }
  snprintf(json_out, cap,
           "{\"ok\":true,\"reachable\":true,\"http_code\":%d,"
           "\"base_url\":\"http://127.0.0.1:%d%s\","
           "\"model_id\":\"%s\",\"llm_is_commander\":false,"
           "\"commander_is_model\":false,\"product_wire\":\"smx2\","
           "\"share\":\"state_matrix_only\"}",
           code, port, path, model_snip[0] ? model_snip : "");
  return 0;
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
    /* require "key" form */
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

/* OpenAI-style content; fall back to reasoning_content (thinking models). */
static int extract_chat_content(const char *raw, char *out, size_t cap) {
  if (extract_json_string_field(raw, "content", out, cap) == 0) return 0;
  if (extract_json_string_field(raw, "reasoning_content", out, cap) == 0)
    return 0;
  if (extract_json_string_field(raw, "text", out, cap) == 0) return 0;
  return -1;
}

/*
 * Local-first chat: POST loopback llama /v1/chat/completions only.
 * LLM is never commander. Product multi-peer talk remains SMX2.
 * Returns 0 and fills json_out always (ok true/false inside).
 */
int grokium_llama_chat(const char *message, char *json_out, size_t cap) {
  const char *base;
  char host[64], path_models[128], esc[2048], req_body[3072], req[3584];
  char buf[8192], content[2048], content_esc[2560];
  int port = 1212, fd = -1, code = 0;
  struct sockaddr_in addr;
  struct timeval tv;
  size_t n = 0, blen;
  ssize_t r;
  const char *err = NULL;

  if (!json_out || cap < 64) return -1;
  if (!message || !message[0]) {
    snprintf(json_out, cap,
             "{\"ok\":false,\"error\":\"empty_message\","
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"share\":\"state_matrix_only\"}");
    return 0;
  }

  base = getenv("GROKIUM_LLAMA_BASE");
  if (!base || !base[0]) base = getenv("NANOBOT_BASE_URL");
  if (!base || !base[0]) base = "http://127.0.0.1:1212/v1";
  if (parse_llama_base(base, host, sizeof host, &port, path_models,
                       sizeof path_models) != 0) {
    snprintf(json_out, cap,
             "{\"ok\":false,\"reachable\":false,\"error\":\"non_loopback_base\","
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"share\":\"state_matrix_only\"}");
    return 0;
  }
  (void)path_models;
  if (!json_escape(message, esc, sizeof esc)) {
    snprintf(json_out, cap,
             "{\"ok\":false,\"error\":\"message_escape\","
             "\"llm_is_commander\":false,\"share\":\"state_matrix_only\"}");
    return 0;
  }

  /* Short local completion — not a tool agent; host TUI still owns agent path.
   * max_tokens modest: thinking models may fill reasoning_content first. */
  blen = (size_t)snprintf(
      req_body, sizeof req_body,
      "{\"model\":\"local\",\"stream\":false,\"max_tokens\":96,"
      "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}",
      esc);
  if (blen >= sizeof req_body) {
    snprintf(json_out, cap,
             "{\"ok\":false,\"error\":\"message_too_long\","
             "\"llm_is_commander\":false,\"share\":\"state_matrix_only\"}");
    return 0;
  }

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    err = "socket";
    goto fail;
  }
  tv.tv_sec = 3;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
    err = "pton";
    goto fail;
  }
  if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
    err = "connect";
    goto fail;
  }
  snprintf(req, sizeof req,
           "POST /v1/chat/completions HTTP/1.1\r\n"
           "Host: 127.0.0.1:%d\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: %zu\r\n"
           "Connection: close\r\n\r\n%s",
           port, blen, req_body);
  if (write(fd, req, strlen(req)) < 0) {
    err = "write";
    goto fail;
  }
  while (n + 1 < sizeof buf) {
    r = read(fd, buf + n, sizeof buf - 1 - n);
    if (r < 0) {
      if (errno == EINTR) continue;
      if (n > 0) break; /* partial after timeout */
      err = "read";
      goto fail;
    }
    if (r == 0) break;
    n += (size_t)r;
  }
  buf[n] = 0;
  close(fd);
  fd = -1;
  if (n == 0) {
    err = "empty";
    goto fail;
  }
  if (!strncmp(buf, "HTTP/", 5)) {
    const char *sp = strchr(buf, ' ');
    if (sp) code = atoi(sp + 1);
  }
  content[0] = 0;
  if (extract_chat_content(buf, content, sizeof content) != 0) {
    snprintf(json_out, cap,
             "{\"ok\":false,\"reachable\":true,\"http_code\":%d,"
             "\"error\":\"no_content\",\"llm_is_commander\":false,"
             "\"commander_is_model\":false,\"product_wire\":\"smx2\","
             "\"share\":\"state_matrix_only\",\"local_first\":true}",
             code);
    return 0;
  }
  json_escape(content, content_esc, sizeof content_esc);
  snprintf(json_out, cap,
           "{\"ok\":true,\"reachable\":true,\"http_code\":%d,"
           "\"content\":\"%s\",\"llm_is_commander\":false,"
           "\"commander_is_model\":false,\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\",\"share\":\"state_matrix_only\","
           "\"local_first\":true,\"telemetry\":\"off\"}",
           code, content_esc);
  return 0;

fail:
  if (fd >= 0) close(fd);
  snprintf(json_out, cap,
           "{\"ok\":false,\"reachable\":false,\"error\":\"%s\","
           "\"base_url\":\"http://127.0.0.1:%d/v1/chat/completions\","
           "\"llm_is_commander\":false,\"commander_is_model\":false,"
           "\"product_wire\":\"smx2\",\"share\":\"state_matrix_only\","
           "\"local_first\":true}",
           err ? err : "down", port);
  return 0;
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

static int load_commander(const char *data_root, gk_commander *C) {
  char law[400], pk[420];
  if (!C) return -1;
  memset(C, 0, sizeof *C);
  law_dir_for(data_root, law, sizeof law);
  if (gk_commander_load(C, law) == 0) return 0;
  snprintf(pk, sizeof pk, "%s/commander.pk", law);
  return gk_commander_load_pk_only(C, pk);
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

/* Count non-dot entries under path; -1 if missing/unreadable. */
static int count_dir_entries(const char *path) {
  DIR *d;
  struct dirent *e;
  int n = 0;
  if (!path || !path[0]) return -1;
  d = opendir(path);
  if (!d) return -1;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.') continue;
    n++;
  }
  closedir(d);
  return n;
}

/*
 * Cube bridge status — AlgoCube digit/blueprint from live matrix.
 * Lab/ops honesty plate; product multi-peer bus remains SMX2.
 */
static void json_cube_status(const gk_consolidator *C, const grokium_law *L,
                             const char *root, char *out, size_t cap) {
  char hex[65];
  uint8_t bp[10];
  char bp_json[80];
  char cpath[400], mpath[400];
  int dig = 0, ncont, nmat, i;
  size_t u = 0;

  if (!out || cap < 128) return;
  hex[0] = 0;
  memset(bp, 0, sizeof bp);
  if (C) {
    smx_sha256_hex(&C->matrix, hex);
    dig = algocube_digit(&C->matrix, "cube_status");
    algocube_blueprint10(&C->matrix, bp);
  }
  u = 0;
  u += (size_t)snprintf(bp_json + u, sizeof bp_json - u, "[");
  for (i = 0; i < 10 && u + 4 < sizeof bp_json; i++)
    u += (size_t)snprintf(bp_json + u, sizeof bp_json - u, "%s%u",
                          i ? "," : "", (unsigned)bp[i]);
  if (u + 2 < sizeof bp_json)
    snprintf(bp_json + u, sizeof bp_json - u, "]");

  snprintf(cpath, sizeof cpath, "%s/cube_containers",
           root && root[0] ? root : "data");
  snprintf(mpath, sizeof mpath, "%s/matrix", root && root[0] ? root : "data");
  ncont = count_dir_entries(cpath);
  nmat = count_dir_entries(mpath);

  snprintf(out, cap,
           "{\"schema\":\"grokium.cube_status.v1\","
           "\"ok\":true,\"product\":\"grokium\","
           "\"bridge\":\"algocube\","
           "\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\","
           "\"hold_flash\":%d,\"telemetry\":\"off\","
           "\"matrix_bits\":%u,\"grade\":\"%s\",\"seq\":%llu,"
           "\"sha256\":\"%s\",\"digit\":%d,\"blueprint\":%s,"
           "\"containers_path\":\"%s\",\"containers_n\":%d,"
           "\"matrix_path\":\"%s\",\"matrix_files_n\":%d,"
           "\"llm_is_commander\":false,\"commander_is_model\":false,"
           "\"llm_on_hot_path\":false}",
           L ? L->hold_flash : 1, C ? C->matrix.bits_set : 0,
           C ? C->grade : "EMPTY",
           (unsigned long long)(C ? C->matrix.seq : 0), hex, dig, bp_json,
           cpath, ncont, mpath, nmat);
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

  if (!strcmp(path, "/v1/cube/status") || !strcmp(path, "/v1/cube")) {
    if (strcmp(method, "GET") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    json_cube_status(C, L, root, resp, sizeof resp);
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

  /* SSE snapshot of latest SMX (bits only). Not long-lived fan-out;
   * sequential accept loop; product multi-peer bus remains SMX2. */
  if (!strcmp(path, "/v1/stream/smx")) {
    if (strcmp(method, "GET") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    smx_sse_snapshot(cfd, C);
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

  if (!strcmp(path, "/v1/license")) {
    if (strcmp(method, "GET") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    http_reply(cfd, 200, "application/json",
               "{\"ok\":true,\"product\":\"grokium\",\"license\":\"Apache-2.0\","
               "\"affiliation\":\"not_affiliated_with_xAI\","
               "\"commander_is_not_model\":true,\"share\":\"state_matrix_only\"}");
    return;
  }

  if (!strcmp(path, "/v1/llama/probe") || !strcmp(path, "/v1/llama")) {
    if (strcmp(method, "GET") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    if (grokium_llama_probe(resp, sizeof resp) != 0) {
      http_reply(cfd, 500, "application/json",
                 "{\"ok\":false,\"error\":\"probe_failed\"}");
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
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\","
                 "\"llm_is_commander\":false}");
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
      http_reply(cfd, 400, "application/json",
                 "{\"ok\":false,\"error\":\"need_message\","
                 "\"hint\":\"{\\\"message\\\":\\\"…\\\"}\","
                 "\"llm_is_commander\":false,\"share\":\"state_matrix_only\"}");
      return;
    }
    if (grokium_llama_chat(msg, resp, sizeof resp) != 0) {
      http_reply(cfd, 500, "application/json",
                 "{\"ok\":false,\"error\":\"chat_failed\","
                 "\"llm_is_commander\":false}");
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

  if (!strcmp(path, "/v1/integrity") || !strcmp(path, "/v1/integrity/tick")) {
    int rc;
    const char *rroot = getenv("GROKIUM_ROOT");
    if (strcmp(method, "GET") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
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
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
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
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
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
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    law_dir_for(root, law, sizeof law);
    if (load_commander(root, &cmd) != 0) {
      snprintf(resp, sizeof resp,
               "{\"ok\":false,\"error\":\"no_commander_pk\","
               "\"law_dir\":\"%s\",\"hint\":\"grokium-commander keygen "
               "--law-dir DIR\",\"not\":\"grok_model\"}",
               law);
      http_reply(cfd, 404, "application/json", resp);
      return;
    }
    /* never emit sk; has_sk only for ops honesty on loopback */
    snprintf(resp, sizeof resp,
             "{\"ok\":true,\"product\":\"grokium\",\"not\":\"grok_model\","
             "\"domain\":\"%s\",\"fingerprint\":\"%s\",\"has_sk\":%s,"
             "\"unforgeable\":true,\"law_dir\":\"%s\","
             "\"commander_is_model\":false}",
             GK_CMD_DOMAIN, cmd.fingerprint_hex,
             cmd.has_sk ? "true" : "false", law);
    http_reply(cfd, 200, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/commander/reject_model")) {
    int deny;
    if (strcmp(method, "POST") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
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
                 "{\"ok\":false,\"allowed\":false,"
                 "\"error\":\"model_is_not_commander\","
                 "\"product\":\"grokium\",\"not\":\"grok_model\","
                 "\"unforgeable\":true}");
      return;
    }
    http_reply(cfd, 200, "application/json",
               "{\"ok\":true,\"allowed\":true,\"product\":\"grokium\","
               "\"not\":\"grok_model\"}");
    return;
  }

  if (!strcmp(path, "/v1/commander/verify")) {
    gk_commander cmd;
    char device[64], action[64], nonce[80], sig[160];
    int64_t ts = 0;
    int ok;
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
    if (load_commander(root, &cmd) != 0 || !cmd.has_pk) {
      http_reply(cfd, 404, "application/json",
                 "{\"ok\":false,\"error\":\"no_commander_pk\"}");
      return;
    }
    device[0] = action[0] = nonce[0] = sig[0] = 0;
    json_get_str(body, body_n, "device", device, sizeof device);
    json_get_str(body, body_n, "action", action, sizeof action);
    json_get_str(body, body_n, "nonce", nonce, sizeof nonce);
    json_get_str(body, body_n, "sig", sig, sizeof sig);
    ts = (int64_t)json_get_int(body, body_n, "ts", 0);
    if (!device[0] || !action[0] || !nonce[0] || !sig[0] || ts == 0) {
      http_reply(cfd, 400, "application/json",
                 "{\"ok\":false,\"error\":\"need_device_action_nonce_ts_sig\"}");
      return;
    }
    ok = gk_commander_verify_override(&cmd, device, action, nonce, ts, NULL, 0,
                                      sig);
    snprintf(resp, sizeof resp,
             "{\"ok\":%s,\"commander\":%s,\"not\":\"grok_model\","
             "\"unforgeable\":true,\"product\":\"grokium\"}",
             ok ? "true" : "false", ok ? "\"grokium\"" : "null");
    http_reply(cfd, ok ? 200 : 403, "application/json", resp);
    return;
  }

  if (!strcmp(path, "/v1/commander/sign")) {
    gk_commander cmd;
    char device[64], action[64], nonce_hex[65], sig_hex[129], env[2048];
    int64_t ts = 0;
    if (strcmp(method, "POST") != 0) {
      http_reply(cfd, 405, "application/json",
                 "{\"ok\":false,\"error\":\"method\"}");
      return;
    }
    /* loopback-only already enforced by bind; still require sk on disk */
    if (load_commander(root, &cmd) != 0 || !cmd.has_sk) {
      http_reply(cfd, 403, "application/json",
                 "{\"ok\":false,\"error\":\"no_commander_sk\","
                 "\"hint\":\"sign only with local commander.sk (never commit)\"}");
      return;
    }
    if (!body || body_n == 0) {
      http_reply(cfd, 400, "application/json",
                 "{\"ok\":false,\"error\":\"need_json_body\"}");
      return;
    }
    device[0] = action[0] = 0;
    json_get_str(body, body_n, "device", device, sizeof device);
    json_get_str(body, body_n, "action", action, sizeof action);
    if (!device[0] || !action[0]) {
      http_reply(cfd, 400, "application/json",
                 "{\"ok\":false,\"error\":\"need_device_and_action\"}");
      return;
    }
    if (gk_commander_sign_override(&cmd, device, action, NULL, 0, nonce_hex, &ts,
                                   sig_hex) != 0) {
      http_reply(cfd, 500, "application/json",
                 "{\"ok\":false,\"error\":\"sign_failed\"}");
      return;
    }
    gk_commander_envelope_json(&cmd, device, action, nonce_hex, ts, sig_hex,
                               NULL, env, sizeof env);
    http_reply(cfd, 200, "application/json", env);
    return;
  }

  http_reply(cfd, 404, "application/json",
             "{\"ok\":false,\"error\":\"not_found\","
             "\"hint\":\"/healthz /v1/status /v1/cube/status /v1/commander "
             "/v1/chat /v1/coord /v1/stream/smx /v1/contract/form "
             "/v1/manager/tick /v1/nanobot/status\"}");
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
