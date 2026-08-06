#define _POSIX_C_SOURCE 200809L
#include "grokium_config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>
#include <unistd.h>
#ifndef GKX_CONFIG_NO_NCURSES
#include <ncurses.h>
#endif

void gkx_config_init(gkx_config *c) {
  if (!c) return;
  memset(c, 0, sizeof *c);
  snprintf(c->models_default, sizeof c->models_default, "local");
  snprintf(c->local_model, sizeof c->local_model, "auto");
  snprintf(c->local_base_url, sizeof c->local_base_url, "http://127.0.0.1:1212/v1");
  snprintf(c->local_api_backend, sizeof c->local_api_backend, "chat_completions");
  c->context_window = 65536;
  c->max_completion_tokens = 8192;
  c->temperature = 0.7;
  c->top_p = 0.95;
  c->auto_compact_threshold_percent = 85;
  c->auto_version_watch = 1;
  c->version_watch_interval_sec = 10800;
  snprintf(c->grok_model, sizeof c->grok_model, "grok-4.5");
  snprintf(c->grok_base_url, sizeof c->grok_base_url,
           "https://cli-chat-proxy.grok.com/v1");
  snprintf(c->active_backend, sizeof c->active_backend, "local");
  snprintf(c->active_model, sizeof c->active_model, "auto");
  c->hub_enabled = 1;
  c->llm_slots = 1;
  c->hub_port = 8787;
  c->agent_max_turns = 64;
  c->agent_cmd_timeout_sec = 900;
  c->agent_http_timeout_sec = 600;
  c->agent_tools = 1;
  c->agent_braincells = 1;
  c->agent_always_approve = 0;
  snprintf(c->ui_theme, sizeof c->ui_theme, "glass");
  snprintf(c->ui_product_name, sizeof c->ui_product_name, "Grokium");
  c->ui_multiline = 1;
  c->ui_mouse = 1;
  c->ui_composer_max_rows = 8;
  c->ui_show_header = 1;
  c->ui_show_status_hints = 1;
  c->ui_show_spoiler_count = 1;
  c->ui_spoilers_default_open = 0;
  c->ui_open_tool_spoiler_on_done = 1;
  c->ui_stream_redraw = 1;
  c->ui_clock = 0;
  c->ui_show_thinking = 0; /* no thinking spam by default */
  snprintf(c->ui_color_user, sizeof c->ui_color_user, "red");
  snprintf(c->ui_color_assistant, sizeof c->ui_color_assistant, "cyan");
  snprintf(c->ui_color_meta, sizeof c->ui_color_meta, "default");
  snprintf(c->ui_color_think, sizeof c->ui_color_think, "magenta");
  snprintf(c->ui_color_tool, sizeof c->ui_color_tool, "yellow");
  snprintf(c->ui_color_accent, sizeof c->ui_color_accent, "cyan");
  snprintf(c->ui_color_ok, sizeof c->ui_color_ok, "green");
  c->ui_scroll_step = 5;
  snprintf(c->ui_send_hint, sizeof c->ui_send_hint, "Enter=nl · Alt+Enter/Ctrl+S=send");
  snprintf(c->ui_welcome_line, sizeof c->ui_welcome_line,
           "local-first · pure C · /settings · /help");
  snprintf(c->adapter_tool_style, sizeof c->adapter_tool_style, "auto");
  snprintf(c->adapter_thinking_tags, sizeof c->adapter_thinking_tags,
           "think,thinking,reasoning,redacted_reasoning");
  /* template keeps literal %%s for later path fill — never pass as format alone */
  snprintf(c->viz_desktop_cmd, sizeof c->viz_desktop_cmd, "%s", "xdg-open %s");
  c->viz_vr_cmd[0] = 0; /* empty = use desktop cmd */
  c->viz_term_width = 64;
  c->viz_term_height = 16;
}

static void trim(char *s) {
  char *e;
  while (*s == ' ' || *s == '\t') memmove(s, s + 1, strlen(s));
  e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
    *--e = 0;
}

static int parse_bool(const char *v) {
  return (strcmp(v, "true") == 0 || strcmp(v, "1") == 0 || strcmp(v, "yes") == 0 ||
          strcmp(v, "on") == 0);
}

static void unquote(char *v) {
  size_t n = strlen(v);
  if (n >= 2 && ((v[0] == '"' && v[n - 1] == '"') || (v[0] == '\'' && v[n - 1] == '\''))) {
    memmove(v, v + 1, n - 2);
    v[n - 2] = 0;
  }
}

static void set_str(char *dst, size_t n, const char *val) {
  snprintf(dst, n, "%s", val);
}

static void apply_kv(gkx_config *c, const char *sec, const char *key, char *val) {
  unquote(val);
  if (strcmp(sec, "models") == 0) {
    if (strcmp(key, "default") == 0) set_str(c->models_default, sizeof c->models_default, val);
  } else if (strcmp(sec, "model.local") == 0) {
    if (strcmp(key, "model") == 0) set_str(c->local_model, sizeof c->local_model, val);
    else if (strcmp(key, "base_url") == 0) set_str(c->local_base_url, sizeof c->local_base_url, val);
    else if (strcmp(key, "api_backend") == 0)
      set_str(c->local_api_backend, sizeof c->local_api_backend, val);
    else if (strcmp(key, "context_window") == 0) c->context_window = atoi(val);
    else if (strcmp(key, "max_completion_tokens") == 0) c->max_completion_tokens = atoi(val);
    else if (strcmp(key, "temperature") == 0) c->temperature = atof(val);
    else if (strcmp(key, "top_p") == 0) c->top_p = atof(val);
  } else if (strcmp(sec, "model.grok") == 0) {
    if (strcmp(key, "model") == 0) set_str(c->grok_model, sizeof c->grok_model, val);
    else if (strcmp(key, "base_url") == 0) set_str(c->grok_base_url, sizeof c->grok_base_url, val);
  } else if (strcmp(sec, "session") == 0) {
    if (strcmp(key, "auto_compact_threshold_percent") == 0)
      c->auto_compact_threshold_percent = atoi(val);
  } else if (strcmp(sec, "cli") == 0) {
    if (strcmp(key, "auto_version_watch") == 0) c->auto_version_watch = parse_bool(val);
    else if (strcmp(key, "version_watch_interval_sec") == 0)
      c->version_watch_interval_sec = atoi(val);
  } else if (strcmp(sec, "hub") == 0) {
    if (strcmp(key, "enabled") == 0) c->hub_enabled = parse_bool(val);
    else if (strcmp(key, "port") == 0) c->hub_port = atoi(val);
    else if (strcmp(key, "llm_slots") == 0) {
      int n = atoi(val);
      if (n < 1) n = 1;
      if (n > 8) n = 8;
      c->llm_slots = n;
    }
  } else if (strcmp(sec, "agent") == 0) {
    if (strcmp(key, "max_turns") == 0) c->agent_max_turns = atoi(val);
    else if (strcmp(key, "cmd_timeout_sec") == 0) c->agent_cmd_timeout_sec = atoi(val);
    else if (strcmp(key, "http_timeout_sec") == 0) c->agent_http_timeout_sec = atoi(val);
    else if (strcmp(key, "tools") == 0) c->agent_tools = parse_bool(val);
    else if (strcmp(key, "braincells") == 0) c->agent_braincells = parse_bool(val);
    else if (strcmp(key, "always_approve") == 0) c->agent_always_approve = parse_bool(val);
  } else if (strcmp(sec, "ui") == 0) {
    if (strcmp(key, "theme") == 0) set_str(c->ui_theme, sizeof c->ui_theme, val);
    else if (strcmp(key, "product_name") == 0)
      set_str(c->ui_product_name, sizeof c->ui_product_name, val);
    else if (strcmp(key, "multiline") == 0) c->ui_multiline = parse_bool(val);
    else if (strcmp(key, "mouse") == 0) c->ui_mouse = parse_bool(val);
    else if (strcmp(key, "composer_max_rows") == 0) {
      int n = atoi(val);
      if (n < 1) n = 1;
      if (n > 16) n = 16;
      c->ui_composer_max_rows = n;
    } else if (strcmp(key, "show_header") == 0) c->ui_show_header = parse_bool(val);
    else if (strcmp(key, "show_status_hints") == 0) c->ui_show_status_hints = parse_bool(val);
    else if (strcmp(key, "show_spoiler_count") == 0) c->ui_show_spoiler_count = parse_bool(val);
    else if (strcmp(key, "spoilers_default_open") == 0)
      c->ui_spoilers_default_open = parse_bool(val);
    else if (strcmp(key, "open_tool_spoiler_on_done") == 0)
      c->ui_open_tool_spoiler_on_done = parse_bool(val);
    else if (strcmp(key, "stream_redraw") == 0) c->ui_stream_redraw = parse_bool(val);
    else if (strcmp(key, "clock") == 0) c->ui_clock = parse_bool(val);
    else if (strcmp(key, "show_thinking") == 0) c->ui_show_thinking = parse_bool(val);
    else if (strcmp(key, "color_user") == 0) set_str(c->ui_color_user, sizeof c->ui_color_user, val);
    else if (strcmp(key, "color_assistant") == 0)
      set_str(c->ui_color_assistant, sizeof c->ui_color_assistant, val);
    else if (strcmp(key, "color_meta") == 0) set_str(c->ui_color_meta, sizeof c->ui_color_meta, val);
    else if (strcmp(key, "color_think") == 0)
      set_str(c->ui_color_think, sizeof c->ui_color_think, val);
    else if (strcmp(key, "color_tool") == 0) set_str(c->ui_color_tool, sizeof c->ui_color_tool, val);
    else if (strcmp(key, "color_accent") == 0)
      set_str(c->ui_color_accent, sizeof c->ui_color_accent, val);
    else if (strcmp(key, "color_ok") == 0) set_str(c->ui_color_ok, sizeof c->ui_color_ok, val);
    else if (strcmp(key, "scroll_step") == 0) {
      int n = atoi(val);
      if (n < 1) n = 1;
      if (n > 50) n = 50;
      c->ui_scroll_step = n;
    } else if (strcmp(key, "send_hint") == 0) set_str(c->ui_send_hint, sizeof c->ui_send_hint, val);
    else if (strcmp(key, "welcome_line") == 0)
      set_str(c->ui_welcome_line, sizeof c->ui_welcome_line, val);
  } else if (strcmp(sec, "adapter") == 0 || strcmp(sec, "model.adapter") == 0) {
    if (strcmp(key, "tool_style") == 0)
      set_str(c->adapter_tool_style, sizeof c->adapter_tool_style, val);
    else if (strcmp(key, "thinking_tags") == 0)
      set_str(c->adapter_thinking_tags, sizeof c->adapter_thinking_tags, val);
  } else if (strcmp(sec, "viz") == 0) {
    if (strcmp(key, "desktop_cmd") == 0)
      set_str(c->viz_desktop_cmd, sizeof c->viz_desktop_cmd, val);
    else if (strcmp(key, "vr_cmd") == 0)
      set_str(c->viz_vr_cmd, sizeof c->viz_vr_cmd, val);
    else if (strcmp(key, "term_width") == 0) c->viz_term_width = atoi(val);
    else if (strcmp(key, "term_height") == 0) c->viz_term_height = atoi(val);
  } else if (strcmp(sec, "paths") == 0) {
    if (strcmp(key, "state_dir") == 0)
      set_str(c->state_dir_override, sizeof c->state_dir_override, val);
  }
}

static int load_file(gkx_config *c, const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return -1;
  char line[768], sec[80] = "";
  while (fgets(line, sizeof line, f)) {
    char *p = line;
    trim(p);
    if (!p[0] || p[0] == '#') continue;
    if (p[0] == '[') {
      char *e = strchr(p, ']');
      if (!e) continue;
      *e = 0;
      snprintf(sec, sizeof sec, "%s", p + 1);
      continue;
    }
    char *eq = strchr(p, '=');
    if (!eq) continue;
    *eq = 0;
    char *key = p;
    char *val = eq + 1;
    trim(key);
    trim(val);
    /* strip inline comment outside quotes */
    if (val[0] != '"' && val[0] != '\'') {
      char *h = strchr(val, '#');
      if (h) {
        *h = 0;
        trim(val);
      }
    }
    apply_kv(c, sec, key, val);
  }
  fclose(f);
  snprintf(c->config_path, sizeof c->config_path, "%s", path);
  return 0;
}

int gkx_config_load(gkx_config *c, const char *path_or_null) {
  int loaded = 0;
  gkx_config_init(c);
  if (path_or_null && path_or_null[0] && load_file(c, path_or_null) == 0)
    return 0;
  const char *root = getenv("GROKIUM_ROOT");
  char try[PATH_MAX];
  if (root && root[0]) {
    snprintf(try, sizeof try, "%s/config/config.toml", root);
    if (load_file(c, try) == 0) loaded = 1;
  }
  const char *home = getenv("HOME");
  if (home) {
    snprintf(try, sizeof try, "%s/.grokium/config.toml", home);
    if (load_file(c, try) == 0) loaded = 1; /* user overrides project */
  }
  return loaded ? 0 : 1;
}

void gkx_config_apply_env(gkx_config *c) {
  const char *e;
  if ((e = getenv("GROKIUM_LOCAL_BASE")) && e[0])
    set_str(c->local_base_url, sizeof c->local_base_url, e);
  if ((e = getenv("GROKIUM_LOCAL_MODEL")) && e[0])
    set_str(c->local_model, sizeof c->local_model, e);
  if ((e = getenv("GROKIUM_BACKEND")) && e[0])
    set_str(c->active_backend, sizeof c->active_backend, e);
  if ((e = getenv("GROKIUM_MODEL")) && e[0])
    set_str(c->active_model, sizeof c->active_model, e);
  if ((e = getenv("GROKIUM_CONTEXT_WINDOW")) && e[0]) c->context_window = atoi(e);
  if ((e = getenv("GROKIUM_MULTILINE")) && e[0]) c->ui_multiline = parse_bool(e);
  if ((e = getenv("NANOBOT_BRAINCELLS")) && e[0]) c->agent_braincells = parse_bool(e);
  if ((e = getenv("NANOBOT_TOOLS")) && e[0]) c->agent_tools = parse_bool(e);
}

void gkx_config_resolve_save_path(gkx_config *c, char *out, size_t n) {
  const char *home = getenv("HOME");
  if (home && home[0]) {
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/.grokium", home);
    mkdir(dir, 0755);
    snprintf(out, n, "%s/config.toml", dir);
    return;
  }
  if (c && c->config_path[0]) {
    snprintf(out, n, "%s", c->config_path);
    return;
  }
  const char *root = getenv("GROKIUM_ROOT");
  if (root && root[0]) {
    snprintf(out, n, "%s/config/config.toml", root);
    return;
  }
  snprintf(out, n, "config.toml");
}

int gkx_config_save(const gkx_config *c, const char *path_or_null) {
  char path[PATH_MAX];
  if (path_or_null && path_or_null[0])
    snprintf(path, sizeof path, "%s", path_or_null);
  else
    gkx_config_resolve_save_path((gkx_config *)c, path, sizeof path);

  FILE *f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f, "# Grokium config — pure C TUI + agent. Auto-saved / editable by hand.\n");
  fprintf(f, "# Product is not affiliated with xAI.\n\n");

  fprintf(f, "[models]\n");
  fprintf(f, "default = \"%s\"\n\n", c->models_default);

  fprintf(f, "[model.local]\n");
  fprintf(f, "model = \"%s\"\n", c->local_model);
  fprintf(f, "base_url = \"%s\"\n", c->local_base_url);
  fprintf(f, "api_backend = \"%s\"\n", c->local_api_backend);
  fprintf(f, "context_window = %d\n", c->context_window);
  fprintf(f, "max_completion_tokens = %d\n", c->max_completion_tokens);
  fprintf(f, "temperature = %.3f\n", c->temperature);
  fprintf(f, "top_p = %.3f\n\n", c->top_p);

  fprintf(f, "[model.grok]\n");
  fprintf(f, "model = \"%s\"\n", c->grok_model);
  fprintf(f, "base_url = \"%s\"\n\n", c->grok_base_url);

  fprintf(f, "[session]\n");
  fprintf(f, "auto_compact_threshold_percent = %d\n\n", c->auto_compact_threshold_percent);

  fprintf(f, "[cli]\n");
  fprintf(f, "auto_version_watch = %s\n", c->auto_version_watch ? "true" : "false");
  fprintf(f, "version_watch_interval_sec = %d\n\n", c->version_watch_interval_sec);

  fprintf(f, "[hub]\n");
  fprintf(f, "enabled = %s\n", c->hub_enabled ? "true" : "false");
  fprintf(f, "port = %d\n", c->hub_port);
  fprintf(f, "llm_slots = %d\n\n", c->llm_slots);

  fprintf(f, "[agent]\n");
  fprintf(f, "max_turns = %d\n", c->agent_max_turns);
  fprintf(f, "cmd_timeout_sec = %d\n", c->agent_cmd_timeout_sec);
  fprintf(f, "http_timeout_sec = %d\n", c->agent_http_timeout_sec);
  fprintf(f, "tools = %s\n", c->agent_tools ? "true" : "false");
  fprintf(f, "braincells = %s\n", c->agent_braincells ? "true" : "false");
  fprintf(f, "always_approve = %s\n\n", c->agent_always_approve ? "true" : "false");

  fprintf(f, "[ui]\n");
  fprintf(f, "theme = \"%s\"\n", c->ui_theme);
  fprintf(f, "product_name = \"%s\"\n", c->ui_product_name);
  fprintf(f, "multiline = %s\n", c->ui_multiline ? "true" : "false");
  fprintf(f, "mouse = %s\n", c->ui_mouse ? "true" : "false");
  fprintf(f, "composer_max_rows = %d\n", c->ui_composer_max_rows);
  fprintf(f, "show_header = %s\n", c->ui_show_header ? "true" : "false");
  fprintf(f, "show_status_hints = %s\n", c->ui_show_status_hints ? "true" : "false");
  fprintf(f, "show_spoiler_count = %s\n", c->ui_show_spoiler_count ? "true" : "false");
  fprintf(f, "spoilers_default_open = %s\n", c->ui_spoilers_default_open ? "true" : "false");
  fprintf(f, "open_tool_spoiler_on_done = %s\n",
          c->ui_open_tool_spoiler_on_done ? "true" : "false");
  fprintf(f, "stream_redraw = %s\n", c->ui_stream_redraw ? "true" : "false");
  fprintf(f, "clock = %s\n", c->ui_clock ? "true" : "false");
  fprintf(f, "show_thinking = %s\n", c->ui_show_thinking ? "true" : "false");
  fprintf(f, "color_user = \"%s\"\n", c->ui_color_user);
  fprintf(f, "color_assistant = \"%s\"\n", c->ui_color_assistant);
  fprintf(f, "color_meta = \"%s\"\n", c->ui_color_meta);
  fprintf(f, "color_think = \"%s\"\n", c->ui_color_think);
  fprintf(f, "color_tool = \"%s\"\n", c->ui_color_tool);
  fprintf(f, "color_accent = \"%s\"\n", c->ui_color_accent);
  fprintf(f, "color_ok = \"%s\"\n", c->ui_color_ok);
  fprintf(f, "scroll_step = %d\n", c->ui_scroll_step);
  fprintf(f, "send_hint = \"%s\"\n", c->ui_send_hint);
  fprintf(f, "welcome_line = \"%s\"\n\n", c->ui_welcome_line);

  fprintf(f, "[adapter]\n");
  fprintf(f, "tool_style = \"%s\"\n", c->adapter_tool_style);
  fprintf(f, "thinking_tags = \"%s\"\n\n", c->adapter_thinking_tags);

  fprintf(f, "[viz]\n");
  fprintf(f, "desktop_cmd = \"%s\"\n", c->viz_desktop_cmd);
  fprintf(f, "vr_cmd = \"%s\"\n", c->viz_vr_cmd);
  fprintf(f, "term_width = %d\n", c->viz_term_width);
  fprintf(f, "term_height = %d\n", c->viz_term_height);

  fclose(f);
  return 0;
}

void gkx_config_load_prefs(gkx_config *c, const char *state_dir) {
  char p[PATH_MAX], line[GKX_MODEL_MAX];
  FILE *f;
  if (!c || !state_dir) return;
  snprintf(p, sizeof p, "%s/backend.txt", state_dir);
  f = fopen(p, "r");
  if (f) {
    if (fgets(line, sizeof line, f)) {
      line[strcspn(line, "\r\n")] = 0;
      if (line[0]) set_str(c->active_backend, sizeof c->active_backend, line);
    }
    fclose(f);
  }
  snprintf(p, sizeof p, "%s/model.txt", state_dir);
  f = fopen(p, "r");
  if (f) {
    if (fgets(line, sizeof line, f)) {
      line[strcspn(line, "\r\n")] = 0;
      if (line[0]) set_str(c->active_model, sizeof c->active_model, line);
    }
    fclose(f);
  }
}

void gkx_config_save_prefs(const gkx_config *c, const char *state_dir) {
  char p[PATH_MAX];
  FILE *f;
  if (!c || !state_dir) return;
  snprintf(p, sizeof p, "%s/backend.txt", state_dir);
  f = fopen(p, "w");
  if (f) {
    fprintf(f, "%s\n", c->active_backend);
    fclose(f);
  }
  snprintf(p, sizeof p, "%s/model.txt", state_dir);
  f = fopen(p, "w");
  if (f) {
    fprintf(f, "%s\n", c->active_model);
    fclose(f);
  }
}

int gkx_color_id(const char *name) {
  if (!name || !name[0] || !strcmp(name, "default") || !strcmp(name, "none"))
    return -1;
#ifndef GKX_CONFIG_NO_NCURSES
  if (!strcmp(name, "black")) return COLOR_BLACK;
  if (!strcmp(name, "red")) return COLOR_RED;
  if (!strcmp(name, "green")) return COLOR_GREEN;
  if (!strcmp(name, "yellow")) return COLOR_YELLOW;
  if (!strcmp(name, "blue")) return COLOR_BLUE;
  if (!strcmp(name, "magenta")) return COLOR_MAGENTA;
  if (!strcmp(name, "cyan")) return COLOR_CYAN;
  if (!strcmp(name, "white")) return COLOR_WHITE;
#endif
  return -1;
}

void gkx_config_summary(const gkx_config *c, char *out, size_t n) {
  if (!c || !out || n < 8) return;
  snprintf(out, n,
           "theme=%s ml=%d tools=%d brain=%d hub=%d turns=%d cmd_t=%d",
           c->ui_theme, c->ui_multiline, c->agent_tools, c->agent_braincells,
           c->hub_enabled, c->agent_max_turns, c->agent_cmd_timeout_sec);
}

/* Machine token for plate fields (backend/theme — no free-text inject). */
static void settings_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in || !in[0]) return;
  for (i = 0; in[i] && o + 1 < cap && o < 40; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')
      out[o++] = (char)c;
    else if (c == ' ' || c == '/' || c == ':' || c == '"' || c == '\\')
      out[o++] = '_';
  }
  out[o] = 0;
}

void gkx_settings_json(const gkx_config *c, int saved, char *out, size_t cap) {
  char backend[48], theme[48];
  if (!out || cap < 64) return;
  if (!c) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.settings.v1\",\"ok\":false,"
             "\"error\":\"no_config\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false,\"python\":0}");
    return;
  }
  settings_token(c->active_backend, backend, sizeof backend);
  if (!backend[0]) snprintf(backend, sizeof backend, "local");
  settings_token(c->ui_theme, theme, sizeof theme);
  if (!theme[0]) snprintf(theme, sizeof theme, "glass");
  /* Shared dual-wire settings plate: TUI show/save · py=0 product path. */
  snprintf(out, cap,
           "{\"schema\":\"grokium.settings.v1\",\"ok\":true,"
           "\"saved\":%s,\"tools\":%d,\"braincells\":%d,"
           "\"multiline\":%d,\"hub\":%d,\"turns\":%d,"
           "\"backend\":\"%s\",\"theme\":\"%s\","
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_is_commander\":false,\"python\":0}",
           saved ? "true" : "false", c->agent_tools ? 1 : 0,
           c->agent_braincells ? 1 : 0, c->ui_multiline ? 1 : 0,
           c->hub_enabled ? 1 : 0, c->agent_max_turns, backend, theme);
}

void gkx_backend_json(const char *backend, int saved, char *out, size_t cap) {
  char be[48];
  if (!out || cap < 64) return;
  settings_token(backend, be, sizeof be);
  if (!be[0]) snprintf(be, sizeof be, "local");
  /* Shared dual-wire backend plate: TUI /backend · /logout (LLM ≠ commander). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.backend.v1\",\"ok\":true,"
           "\"backend\":\"%s\",\"saved\":%s,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_is_commander\":false,\"tools\":false,\"python\":0}",
           be, saved ? "true" : "false");
}

void gkx_model_json(const char *backend, const char *model, int saved,
                    char *out, size_t cap) {
  char be[48], mo[48];
  if (!out || cap < 64) return;
  settings_token(backend, be, sizeof be);
  if (!be[0]) snprintf(be, sizeof be, "local");
  settings_token(model, mo, sizeof mo);
  if (!mo[0]) snprintf(mo, sizeof mo, "auto");
  /* Shared dual-wire model plate: TUI /model set (LLM ≠ commander). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.model.v1\",\"ok\":true,"
           "\"backend\":\"%s\",\"model\":\"%s\",\"saved\":%s,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_is_commander\":false,\"tools\":false,\"python\":0}",
           be, mo, saved ? "true" : "false");
}

void gkx_context_json(int context_window, int saved, char *out, size_t cap) {
  int ctx = context_window > 0 ? context_window : 0;
  if (!out || cap < 64) return;
  /* Shared dual-wire context plate: TUI /context|/ctx (LLM ≠ commander). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.context.v1\",\"ok\":true,"
           "\"context_window\":%d,\"saved\":%s,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_is_commander\":false,\"tools\":false,\"python\":0}",
           ctx, saved ? "true" : "false");
}

void gkx_multiline_json(int on, int saved, char *out, size_t cap) {
  if (!out || cap < 64) return;
  /* Shared dual-wire multiline plate: TUI /multiline|/ml (host UX · py=0). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.multiline.v1\",\"ok\":true,"
           "\"multiline\":%s,\"saved\":%s,"
           "\"enter\":\"%s\",\"send\":\"%s\","
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_is_commander\":false,\"tools\":false,\"python\":0}",
           on ? "true" : "false", saved ? "true" : "false",
           on ? "newline" : "send", on ? "alt_enter_or_ctrl_s" : "alt_enter");
}

void gkx_spoilers_json(int expanded, char *out, size_t cap) {
  if (!out || cap < 64) return;
  /* Shared dual-wire spoilers plate: TUI /expand|/collapse (host UX · py=0). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.spoilers.v1\",\"ok\":true,"
           "\"expanded\":%s,\"state\":\"%s\","
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_is_commander\":false,\"tools\":false,\"python\":0}",
           expanded ? "true" : "false",
           expanded ? "expanded" : "collapsed");
}

void gkx_debug_json(int on, char *out, size_t cap) {
  if (!out || cap < 64) return;
  /* Shared dual-wire debug plate: TUI /debug (host UX · py=0). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.debug.v1\",\"ok\":true,"
           "\"debug\":%s,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_is_commander\":false,\"tools\":false,\"python\":0}",
           on ? "true" : "false");
}

void gkx_always_approve_json(int on, char *out, size_t cap) {
  if (!out || cap < 64) return;
  /* Shared dual-wire always-approve: TUI /always-approve|/yolo (host UX). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.always_approve.v1\",\"ok\":true,"
           "\"always_approve\":%s,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_is_commander\":false,\"tools\":false,\"python\":0}",
           on ? "true" : "false");
}
