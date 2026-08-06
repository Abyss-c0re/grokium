/* SPDX-License-Identifier: Apache-2.0
 * Local-first llama.cpp client — loopback only. LLM is never commander.
 * Product multi-peer talk remains SMX2; peer HTTP = lab/ops only.
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_llama.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

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
  return 0;
}

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
    /* No raw env base_url on wire — machine token only (sanitize). */
    snprintf(json_out, cap,
             "{\"schema\":\"grokium.llama_probe.v1\",\"ok\":false,"
             "\"reachable\":false,\"error\":\"non_loopback_base\","
             "\"llm_is_commander\":false,"
             "\"commander_is_model\":false,\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,\"python\":0}");
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
  {
    char path_tok[96], model_tok[96], err_tok[64];
    machine_token(path, path_tok, sizeof path_tok);
    if (!path_tok[0]) snprintf(path_tok, sizeof path_tok, "/v1/models");
    machine_token(model_snip, model_tok, sizeof model_tok);
    machine_token(err ? err : "down", err_tok, sizeof err_tok);
    if (!err_tok[0]) snprintf(err_tok, sizeof err_tok, "down");
    if (!reachable) {
      snprintf(json_out, cap,
               "{\"schema\":\"grokium.llama_probe.v1\",\"ok\":true,"
               "\"reachable\":false,\"http_code\":0,"
               "\"base_url\":\"http://127.0.0.1:%d%s\","
               "\"error\":\"%s\",\"llm_is_commander\":false,"
               "\"commander_is_model\":false,\"product_wire\":\"smx2\","
               "\"peer_http\":\"lab_ops_only\","
               "\"peer_http_is_product_bus\":false,"
               "\"share\":\"state_matrix_only\",\"hold_flash\":1,\"python\":0}",
               port, path_tok, err_tok);
      return 0;
    }
    snprintf(json_out, cap,
             "{\"schema\":\"grokium.llama_probe.v1\",\"ok\":true,"
             "\"reachable\":true,\"http_code\":%d,"
             "\"base_url\":\"http://127.0.0.1:%d%s\","
             "\"model_id\":\"%s\",\"llm_is_commander\":false,"
             "\"commander_is_model\":false,\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,\"python\":0}",
             code, port, path_tok, model_tok);
  }
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
             "{\"schema\":\"grokium.llama_chat.v1\",\"ok\":false,"
             "\"error\":\"empty_message\","
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,\"python\":0}");
    return 0;
  }

  base = getenv("GROKIUM_LLAMA_BASE");
  if (!base || !base[0]) base = getenv("NANOBOT_BASE_URL");
  if (!base || !base[0]) base = "http://127.0.0.1:1212/v1";
  if (parse_llama_base(base, host, sizeof host, &port, path_models,
                       sizeof path_models) != 0) {
    snprintf(json_out, cap,
             "{\"schema\":\"grokium.llama_chat.v1\",\"ok\":false,"
             "\"reachable\":false,\"error\":\"non_loopback_base\","
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,\"python\":0}");
    return 0;
  }
  (void)path_models;
  if (!json_escape(message, esc, sizeof esc)) {
    snprintf(json_out, cap,
             "{\"schema\":\"grokium.llama_chat.v1\",\"ok\":false,"
             "\"error\":\"message_escape\","
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,\"python\":0}");
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
             "{\"schema\":\"grokium.llama_chat.v1\",\"ok\":false,"
             "\"error\":\"message_too_long\","
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,\"python\":0}");
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
             "{\"schema\":\"grokium.llama_chat.v1\",\"ok\":false,"
             "\"reachable\":true,\"http_code\":%d,"
             "\"error\":\"no_content\",\"llm_is_commander\":false,"
             "\"commander_is_model\":false,\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"python\":0,\"local_first\":true}",
             code);
    return 0;
  }
  json_escape(content, content_esc, sizeof content_esc);
  snprintf(json_out, cap,
           "{\"schema\":\"grokium.llama_chat.v1\",\"ok\":true,"
           "\"reachable\":true,\"http_code\":%d,"
           "\"content\":\"%s\",\"llm_is_commander\":false,"
           "\"commander_is_model\":false,\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"python\":0,\"local_first\":true,\"telemetry\":\"off\"}",
           code, content_esc);
  return 0;

fail:
  if (fd >= 0) close(fd);
  {
    char err_tok[64];
    machine_token(err ? err : "down", err_tok, sizeof err_tok);
    if (!err_tok[0]) snprintf(err_tok, sizeof err_tok, "down");
    snprintf(json_out, cap,
             "{\"schema\":\"grokium.llama_chat.v1\",\"ok\":false,"
             "\"reachable\":false,\"error\":\"%s\","
             "\"base_url\":\"http://127.0.0.1:%d/v1/chat/completions\","
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"python\":0,\"local_first\":true}",
             err_tok, port);
  }
  return 0;
}

/* Hint field: machine-safe subset (no quotes / backslash inject). */
static void hint_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in || !in[0]) return;
  for (i = 0; in[i] && o + 1 < cap && o < 120; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
        c == '/' || c == ' ' || c == '|' || c == '[' || c == ']' ||
        c == '<' || c == '>' || c == ':' || c == '=' || c == '{' ||
        c == '}' || c == '"' || c == '\\' || c == ',' || c == '?' ||
        c == '!' || c == '+' || c == '*' || c == '#' || c == '@' ||
        c == '(' || c == ')' || c == ';') {
      /* Map JSON metacharacters so the plate stays valid. */
      if (c == '"' || c == '\\')
        out[o++] = '_';
      else
        out[o++] = (char)c;
    } else if (c == '\t') {
      out[o++] = ' ';
    }
  }
  out[o] = 0;
}

void grokium_chat_err_json(const char *error, const char *hint, char *out,
                           size_t cap) {
  char err_tok[80], hint_tok[160];
  const char *h;
  if (!out || cap < 64) return;
  machine_token(error && error[0] ? error : "chat_failed", err_tok,
                sizeof err_tok);
  if (!err_tok[0]) snprintf(err_tok, sizeof err_tok, "chat_failed");
  if (hint && hint[0]) {
    hint_token(hint, hint_tok, sizeof hint_tok);
    h = hint_tok[0] ? hint_tok : NULL;
  } else if (!strcmp(err_tok, "need_message")) {
    h = "message required · local llama · LLM≠commander";
  } else if (!strcmp(err_tok, "method")) {
    h = "POST /v1/chat · local llama · LLM≠commander";
  } else {
    h = NULL;
  }
  /* Shared dual-wire plate: HTTP /v1/chat · serve CLI · host chat/-p. */
  if (h && h[0]) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.chat.v1\",\"ok\":false,"
             "\"error\":\"%s\",\"hint\":\"%s\","
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"python\":0,\"local_first\":true,\"telemetry\":\"off\"}",
             err_tok, h);
  } else {
    snprintf(out, cap,
             "{\"schema\":\"grokium.chat.v1\",\"ok\":false,"
             "\"error\":\"%s\","
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"python\":0,\"local_first\":true,\"telemetry\":\"off\"}",
             err_tok);
  }
}

void grokium_agent_err_json(const char *error, const char *hint, char *out,
                            size_t cap) {
  char err_tok[80], hint_tok[160];
  const char *h;
  int tools_denied;
  if (!out || cap < 64) return;
  machine_token(error && error[0] ? error : "agent_chat_failed", err_tok,
                sizeof err_tok);
  if (!err_tok[0]) snprintf(err_tok, sizeof err_tok, "agent_chat_failed");
  tools_denied = !strcmp(err_tok, "tools_not_on_lab_ops");
  if (hint && hint[0]) {
    hint_token(hint, hint_tok, sizeof hint_tok);
    h = hint_tok[0] ? hint_tok : NULL;
  } else if (tools_denied) {
    h = "use host TUI / nanobot for shell tools; POST /v1/agent without tools";
  } else if (!strcmp(err_tok, "need_message")) {
    h = "message required · lab_ops chat-only · tools on host nanobot";
  } else if (!strcmp(err_tok, "method")) {
    h = "POST /v1/agent · lab_ops chat-only · tools:false";
  } else {
    h = NULL;
  }
  /* Shared dual-wire plate: POST /v1/agent denys · serve selftest. */
  if (h && h[0]) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.agent.v1\",\"ok\":false,"
             "\"error\":\"%s\",\"tools\":false,"
             "\"tool_agent\":\"host_nanobot\","
             "\"agent_mode\":\"lab_ops_chat_only\","
             "\"hint\":\"%s\","
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"python\":0,\"local_first\":true,\"telemetry\":\"off\"}",
             err_tok, h);
  } else {
    snprintf(out, cap,
             "{\"schema\":\"grokium.agent.v1\",\"ok\":false,"
             "\"error\":\"%s\",\"tools\":false,"
             "\"tool_agent\":\"host_nanobot\","
             "\"agent_mode\":\"lab_ops_chat_only\","
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"python\":0,\"local_first\":true,\"telemetry\":\"off\"}",
             err_tok);
  }
}

void grokium_agent_ok_json(const char *content, char *out, size_t cap) {
  char content_esc[2560];
  if (!out || cap < 64) return;
  /* JSON-escape body; empty content still emits a valid dual-wire plate. */
  content_esc[0] = 0;
  if (content && content[0])
    (void)json_escape(content, content_esc, sizeof content_esc);
  /* Shared dual-wire success: POST /v1/agent · serve selftest. */
  snprintf(out, cap,
           "{\"schema\":\"grokium.agent.v1\",\"ok\":true,"
           "\"tools\":false,\"tool_agent\":\"host_nanobot\","
           "\"agent_mode\":\"lab_ops_chat_only\","
           "\"content\":\"%s\",\"llm_is_commander\":false,"
           "\"commander_is_model\":false,\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"python\":0,\"local_first\":true,\"telemetry\":\"off\"}",
           content_esc);
}
