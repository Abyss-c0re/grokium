/* SPDX-License-Identifier: Apache-2.0
 * Pure-C session meta plates for loopback / import path.
 * Product bus remains SMX2; peer HTTP = lab_ops only.
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_session.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int gk_session_id_safe(const char *id) {
  size_t i;
  if (!id || !id[0] || strlen(id) > 80) return 0;
  for (i = 0; id[i]; i++) {
    char c = id[i];
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
        (c >= 'A' && c <= 'F') || c == '-')
      continue;
    return 0;
  }
  return 1;
}

/* Echo only hex/dash so deny plates never inject free-text ids. */
static void id_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in) return;
  for (i = 0; in[i] && o + 1 < cap && o < 80; i++) {
    char c = in[i];
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
        (c >= 'A' && c <= 'F') || c == '-')
      out[o++] = c;
  }
  out[o] = 0;
}

static void err_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in || !in[0]) return;
  for (i = 0; in[i] && o + 1 < cap && o < 48; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-')
      out[o++] = (char)c;
    else if (c == ' ' || c == ':' || c == '/' || c == '.')
      out[o++] = '_';
  }
  out[o] = 0;
}

static void json_escape(const char *in, char *out, size_t cap) {
  size_t o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in) return;
  for (; *in && o + 2 < cap; in++) {
    unsigned char c = (unsigned char)*in;
    if (c == '"' || c == '\\') {
      if (o + 3 >= cap) break;
      out[o++] = '\\';
      out[o++] = (char)c;
    } else if (c < 0x20) {
      continue;
    } else {
      out[o++] = (char)c;
    }
  }
  out[o] = 0;
}

int gk_session_pickup_deny_json(const char *id, const char *error, char *out,
                                size_t cap) {
  char err[64], idt[96];
  const char *hint;
  if (!out || cap < 64) return -1;
  err_token(error, err, sizeof err);
  if (!err[0]) snprintf(err, sizeof err, "deny");
  id_token(id, idt, sizeof idt);
  if (strcmp(err, "need_session_id") == 0)
    hint = "pass hex session id · meta only";
  else if (strcmp(err, "not_found") == 0)
    hint = "import meta only · TUI /pickup resumes host-local";
  else if (strcmp(err, "bad_meta") == 0)
    hint = "import meta missing safe id";
  else
    hint = "session id is hex digits and dashes only";
  if (idt[0]) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.session_pickup.v1\",\"ok\":false,"
             "\"error\":\"%s\",\"id\":\"%s\",\"content\":\"meta_only\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false,\"python\":0,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"resume_available\":false,"
             "\"hint\":\"%s\"}",
             err, idt, hint);
  } else {
    snprintf(out, cap,
             "{\"schema\":\"grokium.session_pickup.v1\",\"ok\":false,"
             "\"error\":\"%s\",\"content\":\"meta_only\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false,\"python\":0,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"resume_available\":false,"
             "\"hint\":\"%s\"}",
             err, hint);
  }
  return 0;
}

int gk_session_list_empty_json(const char *q, const char *import_dir,
                               const char *error, char *out, size_t cap) {
  char qe[128], de[512], err[64];
  if (!out || cap < 64) return -1;
  json_escape(q ? q : "", qe, sizeof qe);
  json_escape(import_dir ? import_dir : "", de, sizeof de);
  err_token(error, err, sizeof err);
  if (err[0]) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.sessions.v1\",\"ok\":true,\"n\":0,"
             "\"sessions\":[],\"q\":\"%s\",\"import_dir\":\"%s\","
             "\"error\":\"%s\",\"content\":\"meta_only\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false,\"python\":0,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"telemetry\":\"off\"}",
             qe, de, err);
  } else {
    snprintf(out, cap,
             "{\"schema\":\"grokium.sessions.v1\",\"ok\":true,\"n\":0,"
             "\"sessions\":[],\"q\":\"%s\",\"import_dir\":\"%s\","
             "\"content\":\"meta_only\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false,\"python\":0,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"telemetry\":\"off\"}",
             qe, de);
  }
  return 0;
}

int gk_session_help_json(char *out, size_t cap) {
  if (!out || cap < 64) return -1;
  /* Shared dual-wire help: meta only · lab/ops ≠ product bus · py=0 · no transcripts. */
  snprintf(out, cap,
           "{\"schema\":\"grokium.sessions.v1\",\"ok\":false,"
           "\"error\":\"need_query_or_pickup\",\"content\":\"meta_only\","
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false,\"python\":0,\"telemetry\":\"off\","
           "\"hint\":\"sessions [q] | sessions pickup|load <id> | no "
           "transcripts\"}");
  return 0;
}

static int json_get_str(const char *body, size_t n, const char *key, char *out,
                        size_t cap) {
  const char *p, *end, *q;
  size_t klen, i;
  if (!body || !key || !out || cap < 2 || n == 0) return -1;
  out[0] = 0;
  klen = strlen(key);
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
    if (p < end && ((*p >= '0' && *p <= '9') || *p == '-'))
      return atoi(p);
    break;
  }
  (void)pat;
  return def;
}

static int contains_ci(const char *hay, const char *needle) {
  size_t nlen, hlen, i, j;
  if (!needle || !needle[0]) return 1;
  if (!hay) return 0;
  nlen = strlen(needle);
  hlen = strlen(hay);
  if (nlen > hlen) return 0;
  for (i = 0; i + nlen <= hlen; i++) {
    for (j = 0; j < nlen; j++) {
      char a = hay[i + j], b = needle[j];
      if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
      if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
      if (a != b) break;
    }
    if (j == nlen) return 1;
  }
  return 0;
}

/* Compact meta plate — no transcript (meta_only). */
static int session_compact_from_meta(const char *meta, size_t n, char *out,
                                     size_t cap) {
  char id[96], title[96], updated[48], model[48];
  int msgs = 0;
  char title_esc[128], model_esc[64], updated_esc[96];
  if (!meta || !out || cap < 32) return -1;
  id[0] = title[0] = updated[0] = model[0] = 0;
  json_get_str(meta, n, "id", id, sizeof id);
  json_get_str(meta, n, "title", title, sizeof title);
  json_get_str(meta, n, "updated_at", updated, sizeof updated);
  json_get_str(meta, n, "model", model, sizeof model);
  msgs = json_get_int(meta, n, "num_chat_messages", 0);
  if (!gk_session_id_safe(id)) return -1;
  if (!title[0]) snprintf(title, sizeof title, "%s", id);
  if (strlen(title) > 48) title[48] = 0;
  json_escape(title, title_esc, sizeof title_esc);
  json_escape(model, model_esc, sizeof model_esc);
  json_escape(updated, updated_esc, sizeof updated_esc);
  snprintf(out, cap,
           "{\"id\":\"%s\",\"title\":\"%s\",\"updated_at\":\"%s\","
           "\"num_chat_messages\":%d,\"model\":\"%s\"}",
           id, title_esc, updated_esc, msgs, model_esc);
  return 0;
}

void gk_session_list_json(const char *data_root, const char *q, char *out,
                          size_t cap) {
  char dir[400], path[480], meta[2048], entry[320], q_esc[128], dir_esc[512];
  DIR *d;
  struct dirent *e;
  int matched = 0, scanned = 0, total_meta = 0;
  size_t used, nread;
  FILE *f;

  if (!out || cap < 64) return;
  json_escape(q ? q : "", q_esc, sizeof q_esc);
  snprintf(dir, sizeof dir, "%s/import",
           data_root && data_root[0] ? data_root : "data");
  d = opendir(dir);
  if (!d) {
    (void)gk_session_list_empty_json(q, dir, "no_import_dir", out, cap);
    return;
  }
  json_escape(dir, dir_esc, sizeof dir_esc);
  used = (size_t)snprintf(
      out, cap,
      "{\"schema\":\"grokium.sessions.v1\",\"ok\":true,"
      "\"content\":\"meta_only\",\"product_wire\":\"smx2\","
      "\"peer_http\":\"lab_ops_only\","
      "\"peer_http_is_product_bus\":false,"
      "\"llm_is_commander\":false,\"python\":0,"
      "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
      "\"telemetry\":\"off\",\"q\":\"%s\",\"import_dir\":\"%s\","
      "\"sessions\":[",
      q_esc, dir_esc);

  while ((e = readdir(d)) != NULL && matched < GK_SESSIONS_MAX &&
         scanned < GK_SESSIONS_SCAN) {
    size_t len = strlen(e->d_name);
    if (len < 11 || strcmp(e->d_name + len - 10, ".meta.json") != 0)
      continue;
    total_meta++;
    scanned++;
    snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
    f = fopen(path, "r");
    if (!f) continue;
    nread = fread(meta, 1, sizeof meta - 1, f);
    meta[nread] = 0;
    fclose(f);
    if (q && q[0] && !contains_ci(meta, q) && !contains_ci(e->d_name, q))
      continue;
    if (session_compact_from_meta(meta, nread, entry, sizeof entry) != 0)
      continue;
    if (used + strlen(entry) + 4 >= cap) break;
    used += (size_t)snprintf(out + used, cap - used, "%s%s",
                             matched ? "," : "", entry);
    matched++;
  }
  closedir(d);
  if (used + 80 < cap)
    snprintf(out + used, cap - used,
             "],\"n\":%d,\"scanned\":%d,\"meta_seen\":%d,"
             "\"limit\":%d,\"resume\":\"host_tui_pickup\"}",
             matched, scanned, total_meta, GK_SESSIONS_MAX);
}

static int session_resume_available(const char *root, const char *id,
                                    const char *meta, size_t nmeta) {
  char import_path[400], hist[480];
  const char *h;
  size_t hl;
  if (!id || !id[0]) return 0;
  import_path[0] = 0;
  if (meta && nmeta)
    json_get_str(meta, nmeta, "import_path", import_path, sizeof import_path);
  if (!import_path[0] && meta && nmeta)
    json_get_str(meta, nmeta, "source", import_path, sizeof import_path);
  h = getenv("HOME");
  hl = (h && h[0]) ? strlen(h) : 0;
  if (import_path[0] == '/') {
    int allow = 0;
    if (hl && strncmp(import_path, h, hl) == 0 &&
        (import_path[hl] == '/' || import_path[hl] == 0))
      allow = 1;
    if (!allow && root && root[0] && strstr(import_path, "/data/import/"))
      allow = 1;
    if (allow) {
      snprintf(hist, sizeof hist, "%s/chat_history.jsonl", import_path);
      if (access(hist, R_OK) == 0) return 1;
    }
  }
  snprintf(hist, sizeof hist, "%s/import/%s/chat_history.jsonl",
           root && root[0] ? root : "data", id);
  return access(hist, R_OK) == 0 ? 1 : 0;
}

int gk_session_pickup_json(const char *data_root, const char *id, char *out,
                           size_t cap) {
  char path[480], meta[2048], entry[320];
  FILE *f;
  size_t nread;
  int resume_ok;
  const char *root = data_root && data_root[0] ? data_root : "data";
  if (!out || cap < 64 || !gk_session_id_safe(id)) {
    if (out && cap)
      (void)gk_session_pickup_deny_json(NULL, "bad_session_id", out, cap);
    return -1;
  }
  snprintf(path, sizeof path, "%s/import/%s.meta.json", root, id);
  f = fopen(path, "r");
  if (!f) {
    (void)gk_session_pickup_deny_json(id, "not_found", out, cap);
    return -1;
  }
  nread = fread(meta, 1, sizeof meta - 1, f);
  meta[nread] = 0;
  fclose(f);
  if (session_compact_from_meta(meta, nread, entry, sizeof entry) != 0) {
    (void)gk_session_pickup_deny_json(id, "bad_meta", out, cap);
    return -1;
  }
  resume_ok = session_resume_available(root, id, meta, nread);
  snprintf(out, cap,
           "{\"schema\":\"grokium.session_pickup.v1\",\"ok\":true,"
           "\"content\":\"meta_only\",\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_is_commander\":false,\"python\":0,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"telemetry\":\"off\",\"resume\":\"host_tui_pickup\","
           "\"resume_available\":%s,"
           "\"hint\":\"TUI /pickup loads host-local history only\","
           "\"session\":%s}",
           resume_ok ? "true" : "false", entry);
  return 0;
}
