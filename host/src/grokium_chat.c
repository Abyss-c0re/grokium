#define _POSIX_C_SOURCE 200809L
/* Chat path: nanobot agent core (local llama / optional cloud). */
#include "grokium_chat.h"
#include "grokium.h"
#include "grokium_hub.h"

#include "agent.h"
#include "auth.h"
#include "util.h"
#include "ng_sched.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

int grokium_load_grok_token(char *out, size_t outn) {
  if (!out || outn < 16) return -1;
  out[0] = 0;
  const char *e = getenv("XAI_API_KEY");
  if (e && e[0]) {
    snprintf(out, outn, "%s", e);
    return 0;
  }
  e = getenv("GROK_API_KEY");
  if (e && e[0]) {
    snprintf(out, outn, "%s", e);
    return 0;
  }
  const char *home = getenv("HOME");
  if (!home) return -1;
  char path[PATH_MAX];
  snprintf(path, sizeof path, "%s/.grok/auth.json", home);
  FILE *f = fopen(path, "r");
  if (!f) return -1;
  char *buf = malloc(512 * 1024);
  if (!buf) {
    fclose(f);
    return -1;
  }
  size_t n = fread(buf, 1, 512 * 1024 - 1, f);
  buf[n] = 0;
  fclose(f);
  int found = -1;
  char *p = buf;
  while ((p = strstr(p, "\"key\":\"")) != NULL) {
    p += 7;
    if (strncmp(p, "eyJ", 3) != 0) continue;
    size_t k = 0;
    while (*p && *p != '"' && k + 1 < outn) out[k++] = *p++;
    out[k] = 0;
    if (k > 40) {
      found = 0;
      break;
    }
  }
  free(buf);
  return found;
}

static void ensure_nanobot_home(const char *state_dir) {
  char home[PATH_MAX];
  if (state_dir && state_dir[0])
    snprintf(home, sizeof home, "%s/nanobot_home", state_dir);
  else {
    const char *h = getenv("HOME");
    snprintf(home, sizeof home, "%s/.grokium/nanobot", h ? h : "/tmp");
  }
  mkdir(home, 0700);
  setenv("NANOBOT_HOME", home, 1);
}

static int is_auto_model(const char *m) {
  return !m || !m[0] || strcmp(m, "auto") == 0 || strcmp(m, "local") == 0;
}

static int pick_first_model_id(const char *ids_json, char *out, size_t n) {
  if (!ids_json || !out || n < 2) return -1;
  out[0] = 0;
  const char *p = strchr(ids_json, '"');
  if (!p) return -1;
  p++;
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < n) out[i++] = *p++;
  out[i] = 0;
  return i > 0 ? 0 : -1;
}

int grokium_resolve_model(const gkx_config *cfg, char *buf, size_t n) {
  if (!cfg || !buf || n < 2) return -1;
  buf[0] = 0;
  const char *want = cfg->active_model[0] ? cfg->active_model : cfg->local_model;
  int local = (strcmp(cfg->active_backend, "grok") != 0);
  if (!local) {
    snprintf(buf, n, "%s",
             (want && want[0] && !is_auto_model(want)) ? want : cfg->grok_model);
    return 0;
  }
  if (!is_auto_model(want)) {
    snprintf(buf, n, "%s", want);
    return 0;
  }
  ensure_nanobot_home(state_dir);
  ng_limits_init();
  ng_session s;
  ng_session_init(&s);
  ng_agent_cfg c;
  ng_agent_cfg_init(&c);
  c.session = &s;
  ng_agent_set_local_backend(&c, cfg->local_base_url, NG_DEFAULT_LOCAL_MODEL);
  char *body = ng_agent_fetch_models_json(&c);
  char *ids = body ? ng_agent_models_ids_json(body) : NULL;
  free(body);
  int rc = pick_first_model_id(ids, buf, n);
  free(ids);
  ng_agent_cfg_free(&c);
  ng_session_free(&s);
  if (rc != 0)
    snprintf(buf, n, "%s", NG_DEFAULT_LOCAL_MODEL);
  return rc;
}

char *grokium_models_json(const gkx_config *cfg, char *out_err, size_t err_n) {
  if (out_err && err_n) out_err[0] = 0;
  if (!cfg) {
    if (out_err) snprintf(out_err, err_n, "no config");
    return NULL;
  }
  ensure_nanobot_home(state_dir);
  ng_limits_init();
  ng_session s;
  ng_session_init(&s);
  ng_agent_cfg c;
  ng_agent_cfg_init(&c);
  c.session = &s;
  int local = (strcmp(cfg->active_backend, "grok") != 0);
  if (local) {
    ng_agent_set_local_backend(&c, cfg->local_base_url, cfg->local_model);
  } else {
    ng_session_try_import_grok_cli(&s);
    ng_session_load(&s);
    ng_agent_set_grok_backend(&c, cfg->grok_model);
  }
  char *body = ng_agent_fetch_models_json(&c);
  if (!body || (body[0] == '{' && strstr(body, "\"error\""))) {
    if (out_err)
      snprintf(out_err, err_n, "%s", body ? body : "models fetch failed");
    free(body);
    body = NULL;
  }
  ng_agent_cfg_free(&c);
  ng_session_free(&s);
  return body;
}

typedef struct {
  gkx_stream_fn fn;
  void *ud;
  FILE *cap;
} stream_wrap;

static void on_delta_filter(void *userdata, const char *chunk, size_t n) {
  stream_wrap *w = userdata;
  if (!chunk || !n) return;
  /* Structured events: tools always; thinking only if SHOW_THINKING */
  if (n > 0 && (unsigned char)chunk[0] == 0x1e) {
    int show_th = getenv("NANOBOT_SHOW_THINKING") &&
                  (getenv("NANOBOT_SHOW_THINKING")[0] == '1');
    if (!show_th && strstr(chunk + 1, "\"type\":\"thinking\""))
      return;
    if (w->fn) w->fn(w->ud, chunk, n);
    return;
  }
  if (strstr(chunk, "\"type\":\"thinking\""))
    return;
  /* Drop leaked thinking-tag fragments */
  if (strstr(chunk, "<think") || strstr(chunk, "</think") ||
      strstr(chunk, "<thinking") || strstr(chunk, "reasoning_content"))
    return;
  if (w->fn) w->fn(w->ud, chunk, n);
  if (w->cap) fwrite(chunk, 1, n, w->cap);
}

static int chat_core(const gkx_config *cfg, const char *msg,
                     gkx_stream_fn on_delta, void *userdata,
                     char *out_reply, size_t reply_n,
                     char *out_err, size_t err_n) {
  if (out_reply && reply_n) out_reply[0] = 0;
  if (out_err && err_n) out_err[0] = 0;
  if (!cfg || !msg) {
    if (out_err) snprintf(out_err, err_n, "bad args");
    return 2;
  }
  ensure_nanobot_home(state_dir);
  gkx_hub_apply_sched_env(cfg);
  {
    char slots[8];
    snprintf(slots, sizeof slots, "%d", cfg->llm_slots > 0 ? cfg->llm_slots : 1);
    setenv("NANOBOT_LLM_SLOTS", slots, 1);
    setenv("NANOBOT_LLM_SERIAL", "1", 1);
    /* Desktop host: real agent tools + long-running budgets */
    setenv("NANOBOT_TOOLS", cfg->agent_tools ? "1" : "0", 1);
    setenv("NANOBOT_LOCAL_TOOLS", cfg->agent_tools ? "1" : "0", 1);
    setenv("NANOBOT_BRAINCELLS", cfg->agent_braincells ? "1" : "0", 1);
    setenv("NANOBOT_SHOW_THINKING", cfg->ui_show_thinking ? "1" : "0", 1);
    if (cfg->adapter_tool_style[0])
      setenv("NANOBOT_TOOL_STYLE", cfg->adapter_tool_style, 1);
    if (cfg->adapter_thinking_tags[0])
      setenv("NANOBOT_THINKING_TAGS", cfg->adapter_thinking_tags, 1);
    if (cfg->agent_always_approve)
      setenv("NANOBOT_ALWAYS_APPROVE", "1", 1);
    {
      char b[32];
      if (cfg->agent_max_turns > 0) {
        snprintf(b, sizeof b, "%d", cfg->agent_max_turns);
        setenv("NANOBOT_MAX_TURNS", b, 1);
      } else if (!getenv("NANOBOT_MAX_TURNS"))
        setenv("NANOBOT_MAX_TURNS", "64", 1);
      if (cfg->agent_cmd_timeout_sec > 0) {
        snprintf(b, sizeof b, "%d", cfg->agent_cmd_timeout_sec);
        setenv("NANOBOT_CMD_TIMEOUT", b, 1);
      } else if (!getenv("NANOBOT_CMD_TIMEOUT"))
        setenv("NANOBOT_CMD_TIMEOUT", "900", 1);
      if (cfg->agent_http_timeout_sec > 0) {
        snprintf(b, sizeof b, "%d", cfg->agent_http_timeout_sec);
        setenv("NANOBOT_HTTP_TIMEOUT", b, 1);
      } else if (!getenv("NANOBOT_HTTP_TIMEOUT"))
        setenv("NANOBOT_HTTP_TIMEOUT", "600", 1);
    }
    ng_llm_sched_set_enabled(1);
    ng_llm_sched_set_slots(cfg->llm_slots > 0 ? cfg->llm_slots : 1);
  }
  /* Ensure shell is on under this home */
  {
    char se[PATH_MAX];
    snprintf(se, sizeof se, "%s/nanobot_home/shell_enabled", state_dir);
    FILE *f = fopen(se, "w");
    if (f) {
      fputs("1\n", f);
      fclose(f);
    }
  }
  ng_limits_init();
  ng_cli_version_init();
  /* Keep file log; never paint agent noise over a TUI / clean -p */
  ng_log_set_stderr(0);
  ng_log_init(NULL);

  ng_session s;
  ng_session_init(&s);
  ng_agent_cfg c;
  ng_agent_cfg_init(&c);
  c.session = &s;
  c.max_turns = ng_max_turns();
  c.timeout_sec = ng_cmd_timeout_sec();
  if (cfg->context_window > 0) {
    /* nanobot uses env for some limits; set compact-related soft hints */
    char buf[32];
    snprintf(buf, sizeof buf, "%d", cfg->context_window);
    setenv("GROKIUM_CONTEXT_WINDOW", buf, 1);
  }

  int local = (strcmp(cfg->active_backend, "grok") != 0);
  char model[GKX_MODEL_MAX];
  gkx_config tmp = *cfg;
  if (local) {
    if (grokium_resolve_model(&tmp, model, sizeof model) != 0 && is_auto_model(tmp.active_model)) {
      /* still try with local placeholder */
      snprintf(model, sizeof model, "%s",
               tmp.local_model[0] ? tmp.local_model : "local");
    }
    ng_agent_set_local_backend(&c, cfg->local_base_url, model);
  } else {
    ng_session_try_import_grok_cli(&s);
    if (!ng_session_valid(&s))
      ng_session_load(&s);
    snprintf(model, sizeof model, "%s",
             (tmp.active_model[0] && !is_auto_model(tmp.active_model))
                 ? tmp.active_model
                 : cfg->grok_model);
    ng_agent_set_grok_backend(&c, model);
    if (ng_agent_needs_browser_session(&c) && !ng_session_valid(&s)) {
      if (out_err)
        snprintf(out_err, err_n, "cloud needs auth: /login or XAI_API_KEY");
      ng_agent_cfg_free(&c);
      ng_session_free(&s);
      return 2;
    }
  }

  stream_wrap w = {.fn = on_delta, .ud = userdata, .cap = NULL};
  char *reply = ng_agent_run_ex(&c, msg, on_delta ? 1 : 0,
                                on_delta ? on_delta_filter : NULL, &w);

  int rc = 2;
  if (reply && reply[0]) {
    if (out_reply && reply_n) {
      snprintf(out_reply, reply_n, "%s", reply);
    }
    rc = 0;
  } else if (reply) {
    rc = 1;
    if (out_err) snprintf(out_err, err_n, "empty reply");
  } else {
    if (out_err) snprintf(out_err, err_n, "agent failed (backend=%s model=%s)",
                          cfg->active_backend, model);
  }
  free(reply);
  ng_agent_cfg_free(&c);
  ng_session_free(&s);
  return rc;
}

int grokium_chat_request_ex(const gkx_config *cfg, const char *msg,
                            gkx_stream_fn on_delta, void *userdata,
                            char *out_reply, size_t reply_n,
                            char *out_err, size_t err_n) {
  return chat_core(cfg, msg, on_delta, userdata, out_reply, reply_n, out_err, err_n);
}

int grokium_chat_request(const char *backend, const char *model, const char *msg,
                         const char *state_dir_arg,
                         char *out_reply, size_t reply_n,
                         char *out_err, size_t err_n) {
  gkx_config cfg;
  gkx_config_load(&cfg, NULL);
  gkx_config_apply_env(&cfg);
  if (state_dir_arg && state_dir_arg[0])
    gkx_config_load_prefs(&cfg, state_dir_arg);
  if (backend && backend[0])
    snprintf(cfg.active_backend, sizeof cfg.active_backend, "%s", backend);
  if (model && model[0])
    snprintf(cfg.active_model, sizeof cfg.active_model, "%s", model);
  return chat_core(&cfg, msg, NULL, NULL, out_reply, reply_n, out_err, err_n);
}
