/* SPDX-License-Identifier: Apache-2.0
 * Pure-C dual-wire plate line filter for host TUI tool capture.
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_plate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gkx_is_grokium_plate_line(const char *ln) {
  return ln && ln[0] == '{' && strstr(ln, "\"schema\":\"grokium.") != NULL;
}

int gkx_log_block_keep_line(const char *ln, int debug_mode) {
  if (!ln || !ln[0]) return 0;
  /* Suppress free-form JSON dumps; keep dual-wire honesty plates. */
  if (!debug_mode && ln[0] == '{' && !gkx_is_grokium_plate_line(ln)) return 0;
  return 1;
}

/* Extract compact "key":"value" or "key":true/false/number into dst. */
static int plate_json_str(const char *j, const char *key, char *dst, size_t cap) {
  char pat[80];
  const char *p;
  size_t i = 0;
  if (!j || !key || !dst || cap < 2) return 0;
  dst[0] = 0;
  snprintf(pat, sizeof pat, "\"%s\":\"", key);
  p = strstr(j, pat);
  if (!p) return 0;
  p += strlen(pat);
  while (*p && *p != '"' && i + 1 < cap && i < 64) dst[i++] = *p++;
  dst[i] = 0;
  return i > 0;
}

static int plate_json_bool(const char *j, const char *key) {
  char pat[80];
  const char *p;
  if (!j || !key) return -1;
  snprintf(pat, sizeof pat, "\"%s\":", key);
  p = strstr(j, pat);
  if (!p) return -1;
  p += strlen(pat);
  while (*p == ' ') p++;
  if (strncmp(p, "true", 4) == 0) return 1;
  if (strncmp(p, "false", 5) == 0) return 0;
  return -1;
}

int gkx_plate_ui_line(const char *ln, int debug_mode, char *out, size_t cap) {
  char schema[64], short_s[48], be[48], model[64], err[48], hint[96];
  char method[32], action[32];
  int ok, hub, tools, ml, has_tok, n, ctx;
  const char *p, *end;
  size_t i = 0;

  if (!out || cap < 8) return -1;
  out[0] = 0;
  if (!ln || !ln[0]) return 0;

  if (debug_mode || !gkx_is_grokium_plate_line(ln)) {
    snprintf(out, cap, "%s", ln);
    return 0;
  }

  schema[0] = be[0] = model[0] = err[0] = hint[0] = method[0] = action[0] =
      short_s[0] = 0;
  plate_json_str(ln, "schema", schema, sizeof schema);
  plate_json_str(ln, "backend", be, sizeof be);
  plate_json_str(ln, "model", model, sizeof model);
  plate_json_str(ln, "active", model, sizeof model); /* models list */
  plate_json_str(ln, "error", err, sizeof err);
  plate_json_str(ln, "hint", hint, sizeof hint);
  plate_json_str(ln, "method", method, sizeof method);
  plate_json_str(ln, "action", action, sizeof action);
  ok = plate_json_bool(ln, "ok");
  hub = plate_json_bool(ln, "hub");
  tools = plate_json_bool(ln, "tools");
  ml = plate_json_bool(ln, "multiline");
  has_tok = plate_json_bool(ln, "has_token");

  /* schema grokium.ready.v1 → ready */
  p = schema;
  if (strncmp(p, "grokium.", 8) == 0) p += 8;
  end = strstr(p, ".v");
  if (!end) end = p + strlen(p);
  while (p < end && i + 1 < sizeof short_s) short_s[i++] = *p++;
  short_s[i] = 0;
  if (!short_s[0]) snprintf(short_s, sizeof short_s, "plate");

  /* Specialize common plates for chat surface */
  if (strcmp(short_s, "ready") == 0) {
    snprintf(out, cap,
             "· ready%s%s%s · Enter=send · Shift/Alt+Enter=nl · /agents /fleet /auth",
             hub == 1 ? " · hub" : "", tools == 1 ? " · tools" : "",
             ml == 1 ? " · ml" : "");
    return 0;
  }
  if (strcmp(short_s, "help") == 0 || strcmp(short_s, "cli_help") == 0) {
    snprintf(out, cap, "· help · /settings /model /backend /fleet /auth /shell /q");
    return 0;
  }
  if (strcmp(short_s, "auth") == 0) {
    snprintf(out, cap, "· auth · token=%s · backend=%s%s",
             has_tok == 1 ? "yes" : "no", be[0] ? be : "?",
             has_tok != 1 ? " · /auth import · /login" : "");
    return 0;
  }
  if (strcmp(short_s, "login") == 0) {
    snprintf(out, cap, "· login · %s · token=%s%s",
             method[0] ? method : "oauth", has_tok == 1 ? "yes" : "no",
             has_tok == 1 ? " · /backend grok" : " · retry /login or /auth import");
    return 0;
  }
  if (strcmp(short_s, "backend") == 0) {
    snprintf(out, cap, "· backend · %s", be[0] ? be : "?");
    return 0;
  }
  if (strcmp(short_s, "model") == 0 || strcmp(short_s, "models") == 0) {
    n = -1;
    {
      const char *np = strstr(ln, "\"n\":");
      if (np) n = atoi(np + 4);
    }
    if (n >= 0)
      snprintf(out, cap, "· models · n=%d · %s · %s", n, be[0] ? be : "?",
               model[0] ? model : "auto");
    else
      snprintf(out, cap, "· model · %s · %s", be[0] ? be : "?",
               model[0] ? model : "?");
    return 0;
  }
  if (strcmp(short_s, "settings") == 0) {
    snprintf(out, cap, "· settings · %s · tools=%s · /settings key=value|save",
             be[0] ? be : "local", tools == 1 ? "on" : (tools == 0 ? "off" : "?"));
    return 0;
  }
  if (strcmp(short_s, "multiline") == 0) {
    snprintf(out, cap, "· multiline · %s · Enter=send · Shift/Alt+Enter=newline",
             ml == 1 || plate_json_bool(ln, "multiline") == 1 ? "on" : "off");
    return 0;
  }
  if (strcmp(short_s, "hub_wait") == 0 || strcmp(short_s, "hub") == 0) {
    snprintf(out, cap, "· hub · %s", be[0] ? be : "wait");
    return 0;
  }
  if (strcmp(short_s, "session_clear") == 0) {
    snprintf(out, cap, "· session · %s", action[0] ? action : "clear");
    return 0;
  }
  if (strcmp(short_s, "spoilers") == 0) {
    snprintf(out, cap, "· spoilers · %s",
             plate_json_bool(ln, "expanded") == 1 ? "expanded" : "collapsed");
    return 0;
  }
  if (strcmp(short_s, "always_approve") == 0) {
    snprintf(out, cap, "· always-approve · %s",
             plate_json_bool(ln, "always_approve") == 1 ? "on" : "off");
    return 0;
  }
  if (strcmp(short_s, "debug") == 0) {
    snprintf(out, cap, "· debug · %s",
             plate_json_bool(ln, "debug") == 1 ? "on" : "off");
    return 0;
  }
  if (strcmp(short_s, "interrupt") == 0) {
    snprintf(out, cap, "· interrupt");
    return 0;
  }
  if (strcmp(short_s, "empty_output") == 0) {
    snprintf(out, cap, "· (empty)");
    return 0;
  }
  if (strcmp(short_s, "context") == 0) {
    ctx = 0;
    {
      const char *cp = strstr(ln, "\"context_window\":");
      if (cp) ctx = atoi(cp + 17);
    }
    snprintf(out, cap, "· context · %d", ctx);
    return 0;
  }
  if (strcmp(short_s, "chat") == 0 || strstr(short_s, "err") || err[0]) {
    snprintf(out, cap, "· %s%s%s%s",
             short_s[0] ? short_s : "err",
             err[0] ? " · " : "", err,
             hint[0] ? " · " : "");
    if (hint[0] && strlen(out) + 2 < cap) {
      size_t L = strlen(out);
      snprintf(out + L, cap - L, "%s", hint);
    }
    return 0;
  }

  /* Generic: · schema · ok/err · backend */
  {
    char tail[160];
    tail[0] = 0;
    if (ok == 0 && err[0])
      snprintf(tail, sizeof tail, " · %s", err);
    else if (be[0])
      snprintf(tail, sizeof tail, " · %s", be);
    else if (model[0])
      snprintf(tail, sizeof tail, " · %s", model);
    snprintf(out, cap, "· %s%s%s", short_s, ok == 0 ? " · deny" : "", tail);
  }
  return 0;
}

int gkx_filter_tool_block(const char *text, int debug_mode, char *out,
                          size_t cap) {
  char *dup, *save = NULL, *ln;
  size_t o = 0;
  int n = 0;
  if (!out || cap < 2) return -1;
  out[0] = 0;
  if (!text || !text[0]) return 0;
  dup = strdup(text);
  if (!dup) return -1;
  for (ln = strtok_r(dup, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
    size_t L;
    if (!gkx_log_block_keep_line(ln, debug_mode)) continue;
    L = strlen(ln);
    /* need L + optional '\n' + NUL */
    if (o + L + 2 > cap) {
      free(dup);
      out[0] = 0;
      return -1;
    }
    memcpy(out + o, ln, L);
    o += L;
    out[o++] = '\n';
    out[o] = 0;
    n++;
  }
  free(dup);
  return n;
}
