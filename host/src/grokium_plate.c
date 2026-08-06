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

/* Compact "key":N integer (or -1 if missing). */
static int plate_json_int(const char *j, const char *key) {
  char pat[80];
  const char *p;
  if (!j || !key) return -1;
  snprintf(pat, sizeof pat, "\"%s\":", key);
  p = strstr(j, pat);
  if (!p) return -1;
  p += strlen(pat);
  while (*p == ' ') p++;
  if (*p == '-' || (*p >= '0' && *p <= '9')) return atoi(p);
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
    {
      int turns = plate_json_int(ln, "max_turns");
      int sub = plate_json_bool(ln, "subagents");
      char vision[32];
      vision[0] = 0;
      plate_json_str(ln, "vision", vision, sizeof vision);
      /* Humanize dual-wire ready — no free-text capability strip elsewhere. */
      snprintf(out, cap,
               "· ready%s%s%s%s%s · turns=%d · Enter=send · /agents /fleet /auth",
               hub == 1 ? " · hub" : "", tools == 1 ? " · tools" : "",
               ml == 1 ? " · ml" : "", vision[0] ? " · vision" : "",
               sub == 1 ? " · subagents" : "", turns > 0 ? turns : 96);
    }
    return 0;
  }
  if (strcmp(short_s, "agents") == 0) {
    {
      int turns = plate_json_int(ln, "max_turns");
      int nrun = plate_json_int(ln, "sub_running");
      int smax = plate_json_int(ln, "sub_max");
      int bc = plate_json_bool(ln, "braincells");
      snprintf(out, cap,
               "· agents · tools=%s · braincells=%s · turns=%d · "
               "sub=%d/%d · /fleet /manager",
               tools == 1 ? "on" : (tools == 0 ? "off" : "?"),
               bc == 1 ? "on" : (bc == 0 ? "off" : "?"),
               turns >= 0 ? turns : 0, nrun >= 0 ? nrun : 0,
               smax >= 0 ? smax : 0);
    }
    return 0;
  }
  if (strcmp(short_s, "subagent_cancel") == 0) {
    {
      char id[48];
      id[0] = 0;
      plate_json_str(ln, "id", id, sizeof id);
      if (ok == 1)
        snprintf(out, cap, "· subagent · cancel · %s · ok",
                 id[0] ? id : "?");
      else
        snprintf(out, cap, "· subagent · cancel · %s · deny",
                 id[0] ? id : "?");
    }
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
  if (strcmp(short_s, "auth_import") == 0) {
    {
      char res[40];
      res[0] = 0;
      plate_json_str(ln, "result", res, sizeof res);
      snprintf(out, cap, "· auth import · %s%s",
               res[0] ? res : (ok == 1 ? "sealed" : "missing"),
               has_tok == 1 ? " · token=yes" : " · token=no · /login");
    }
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
  if (strcmp(short_s, "hub_status") == 0) {
    {
      int alive = plate_json_bool(ln, "alive");
      int managed = plate_json_bool(ln, "managed");
      int http = plate_json_bool(ln, "http");
      int pid = plate_json_int(ln, "pid");
      if (ok == 0)
        snprintf(out, cap, "· hub · deny%s%s%s",
                 alive == 0 ? " · dead" : "",
                 managed == 0 ? " · unmanaged" : "",
                 http == 0 ? " · no_http" : "");
      else
        snprintf(out, cap, "· hub · ok · pid=%d%s%s",
                 pid > 0 ? pid : 0,
                 managed == 1 ? " · managed" : "",
                 http == 1 ? " · http" : "");
    }
    return 0;
  }
  if (strcmp(short_s, "nanobot_status") == 0 ||
      strcmp(short_s, "fleet_defaults") == 0) {
    {
      int nn = plate_json_int(ln, "n");
      int alive = plate_json_int(ln, "alive");
      int mgr = plate_json_bool(ln, "nb_manager");
      if (strcmp(short_s, "fleet_defaults") == 0)
        snprintf(out, cap, "· fleet · defaults · n=%d%s",
                 nn >= 0 ? nn : 0, mgr == 1 ? " · manager" : "");
      else
        snprintf(out, cap, "· fleet · alive=%d · n=%d%s",
                 alive >= 0 ? alive : 0, nn >= 0 ? nn : 0,
                 mgr == 1 ? " · manager" : "");
    }
    return 0;
  }
  if (strcmp(short_s, "nanobot_deploy") == 0 ||
      strcmp(short_s, "nanobot_spawn") == 0 ||
      strcmp(short_s, "nanobot_separate") == 0 ||
      strcmp(short_s, "nanobot_note_pid") == 0 ||
      strcmp(short_s, "nanobot_stop") == 0 ||
      strcmp(short_s, "nanobot_save") == 0) {
    {
      const char *verb = "fleet";
      char id[48];
      int alive = plate_json_int(ln, "alive");
      id[0] = 0;
      plate_json_str(ln, "id", id, sizeof id);
      if (strcmp(short_s, "nanobot_deploy") == 0) verb = "deploy";
      else if (strcmp(short_s, "nanobot_spawn") == 0) verb = "spawn";
      else if (strcmp(short_s, "nanobot_separate") == 0) verb = "separate";
      else if (strcmp(short_s, "nanobot_note_pid") == 0) verb = "note-pid";
      else if (strcmp(short_s, "nanobot_stop") == 0) verb = "stop";
      else if (strcmp(short_s, "nanobot_save") == 0) verb = "save";
      if (ok == 0 && err[0])
        snprintf(out, cap, "· fleet · %s · deny · %s", verb, err);
      else if (id[0] && alive >= 0)
        snprintf(out, cap, "· fleet · %s · %s · alive=%d", verb, id, alive);
      else if (id[0])
        snprintf(out, cap, "· fleet · %s · %s", verb, id);
      else if (alive >= 0)
        snprintf(out, cap, "· fleet · %s · alive=%d", verb, alive);
      else
        snprintf(out, cap, "· fleet · %s", verb);
    }
    return 0;
  }
  if (strcmp(short_s, "manager_tick") == 0) {
    {
      int motivated = plate_json_int(ln, "motivated");
      int incomplete = plate_json_int(ln, "incomplete");
      if (ok == 0 && err[0])
        snprintf(out, cap, "· manager · deny · %s", err);
      else
        snprintf(out, cap, "· manager · tick%s%s",
                 motivated >= 0 ? " · motivated" : "",
                 incomplete >= 0 ? " · incomplete" : "");
    }
    return 0;
  }
  if (strcmp(short_s, "coord") == 0) {
    if (ok == 0 && err[0])
      snprintf(out, cap, "· coord · deny · %s", err);
    else
      snprintf(out, cap, "· coord · ok · smx");
    return 0;
  }
  if (strcmp(short_s, "integrity_report") == 0 ||
      strcmp(short_s, "integrity_policy") == 0 ||
      strcmp(short_s, "integrity_reseal") == 0) {
    {
      int mismatches = plate_json_int(ln, "mismatches");
      int priv = plate_json_bool(ln, "privacy_ok");
      int seal = plate_json_bool(ln, "code_seal_ok");
      const char *verb = "integrity";
      if (strcmp(short_s, "integrity_policy") == 0) verb = "policy";
      else if (strcmp(short_s, "integrity_reseal") == 0) verb = "reseal";
      if (ok == 0 && err[0])
        snprintf(out, cap, "· integrity · %s · deny · %s", verb, err);
      else if (strcmp(short_s, "integrity_report") == 0)
        snprintf(out, cap, "· integrity · tick · %s%s%s%s",
                 ok == 1 ? "ok" : "fail",
                 priv == 1 ? " · privacy" : (priv == 0 ? " · privacy_fail" : ""),
                 seal == 1 ? " · seal" : (seal == 0 ? " · seal_fail" : ""),
                 mismatches > 0 ? " · mismatch" : "");
      else
        snprintf(out, cap, "· integrity · %s · %s", verb,
                 ok == 1 ? "ok" : "deny");
    }
    return 0;
  }
  if (strcmp(short_s, "contract_form") == 0 ||
      strcmp(short_s, "contract_validate") == 0) {
    {
      char assignee[48], id[48];
      const char *verb =
          strcmp(short_s, "contract_form") == 0 ? "form" : "validate";
      assignee[0] = id[0] = 0;
      plate_json_str(ln, "assignee", assignee, sizeof assignee);
      plate_json_str(ln, "id", id, sizeof id);
      if (ok == 0 && err[0])
        snprintf(out, cap, "· contract · %s · deny · %s", verb, err);
      else if (id[0])
        snprintf(out, cap, "· contract · %s · %s%s%s", verb, id,
                 assignee[0] ? " · " : "", assignee);
      else if (assignee[0])
        snprintf(out, cap, "· contract · %s · %s", verb, assignee);
      else
        snprintf(out, cap, "· contract · %s · %s", verb,
                 ok == 1 ? "ok" : "deny");
    }
    return 0;
  }
  if (strcmp(short_s, "ability") == 0) {
    {
      char grade[32];
      grade[0] = 0;
      plate_json_str(ln, "grade", grade, sizeof grade);
      snprintf(out, cap, "· ability · %s%s%s",
               grade[0] ? grade : (ok == 1 ? "ok" : "deny"),
               plate_json_bool(ln, "seal_ok") == 1 ? " · seal" : "",
               plate_json_bool(ln, "fresh") == 1 ? " · fresh" : "");
    }
    return 0;
  }
  if (strcmp(short_s, "cube_status") == 0) {
    {
      int dig = plate_json_int(ln, "digit");
      if (dig >= 0)
        snprintf(out, cap, "· cube · status · d=%d", dig);
      else
        snprintf(out, cap, "· cube · status · %s", ok == 1 ? "ok" : "deny");
    }
    return 0;
  }
  if (strcmp(short_s, "smx") == 0 || strcmp(short_s, "status") == 0) {
    {
      int bits = plate_json_int(ln, "bits_set");
      if (bits < 0) bits = plate_json_int(ln, "matrix_bits");
      char grade[32];
      grade[0] = 0;
      plate_json_str(ln, "grade", grade, sizeof grade);
      if (strcmp(short_s, "smx") == 0)
        snprintf(out, cap, "· smx · bits=%d%s%s", bits >= 0 ? bits : 0,
                 grade[0] ? " · " : "", grade);
      else
        snprintf(out, cap, "· status · bits=%d%s%s", bits >= 0 ? bits : 0,
                 grade[0] ? " · " : "", grade);
    }
    return 0;
  }
  if (strcmp(short_s, "commander") == 0 ||
      strcmp(short_s, "commander_verify") == 0 ||
      strcmp(short_s, "commander_reject") == 0) {
    {
      char fp[40];
      fp[0] = 0;
      plate_json_str(ln, "fingerprint", fp, sizeof fp);
      if (strcmp(short_s, "commander_reject") == 0)
        snprintf(out, cap, "· commander · reject · %s",
                 ok == 1 ? "allow" : "deny");
      else if (strcmp(short_s, "commander_verify") == 0)
        snprintf(out, cap, "· commander · verify · %s",
                 ok == 1 ? "ok" : "deny");
      else if (ok == 0 && err[0])
        snprintf(out, cap, "· commander · deny · %s", err);
      else
        snprintf(out, cap, "· commander · %s%s",
                 fp[0] ? fp : "ed25519",
                 plate_json_bool(ln, "has_sk") == 1 ? " · sk" : "");
    }
    return 0;
  }
  if (strcmp(short_s, "license") == 0) {
    snprintf(out, cap, "· license · Apache-2.0 · not xAI");
    return 0;
  }
  if (strcmp(short_s, "instinct") == 0) {
    snprintf(out, cap, "· instinct · hive · smx2");
    return 0;
  }
  if (strcmp(short_s, "sessions") == 0 ||
      strcmp(short_s, "session_pickup") == 0 ||
      strcmp(short_s, "session_resume") == 0) {
    {
      int nn = plate_json_int(ln, "n");
      char id[48];
      id[0] = 0;
      plate_json_str(ln, "id", id, sizeof id);
      if (ok == 0 && err[0])
        snprintf(out, cap, "· session · deny · %s", err);
      else if (strcmp(short_s, "sessions") == 0)
        snprintf(out, cap, "· session · list · n=%d", nn >= 0 ? nn : 0);
      else if (strcmp(short_s, "session_pickup") == 0)
        snprintf(out, cap, "· session · pickup%s%s", id[0] ? " · " : "",
                 id[0] ? id : (ok == 1 ? "ok" : ""));
      else
        snprintf(out, cap, "· session · resume%s%s", id[0] ? " · " : "",
                 id[0] ? id : (ok == 1 ? "ok" : ""));
    }
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
