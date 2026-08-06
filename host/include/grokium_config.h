/* Grokium configuration — pure C, TOML subset, no Python.
 * Load order: explicit path → $GROKIUM_ROOT/config/config.toml → ~/.grokium/config.toml
 * Save: gkx_config_save() writes the user-writable path (prefer ~/.grokium/).
 */
#ifndef GROKIUM_CONFIG_H
#define GROKIUM_CONFIG_H

#include <stddef.h>

#define GKX_URL_MAX 256
#define GKX_MODEL_MAX 512
#define GKX_ID_MAX 64
#define GKX_PATH_MAX 512
#define GKX_NAME_MAX 64
#define GKX_THEME_MAX 32

/* Named ncurses color: "default" | "black" | "red" | "green" | "yellow" |
 * "blue" | "magenta" | "cyan" | "white"  → COLOR_* or -1 for default */
typedef struct {
  char models_default[GKX_ID_MAX];
  char local_model[GKX_MODEL_MAX];
  char local_base_url[GKX_URL_MAX];
  char local_api_backend[32];
  int context_window;
  int max_completion_tokens;
  double temperature;
  double top_p;
  int auto_compact_threshold_percent;
  int auto_version_watch;
  int version_watch_interval_sec;
  char grok_model[GKX_MODEL_MAX];
  char grok_base_url[GKX_URL_MAX];
  char active_backend[32]; /* local | grok */
  char active_model[GKX_MODEL_MAX];

  /* Hub / LLM gate */
  int hub_enabled;
  int llm_slots;
  int hub_port;

  /* Agent / nanobot */
  int agent_max_turns;
  int agent_cmd_timeout_sec;
  int agent_http_timeout_sec;
  int agent_tools;           /* NANOBOT_TOOLS */
  int agent_braincells;      /* NANOBOT_BRAINCELLS */
  int agent_always_approve;  /* yolo-ish for shell when 1 */

  /* Pure C UI */
  char ui_theme[GKX_THEME_MAX];     /* glass | dense | minimal */
  char ui_product_name[GKX_NAME_MAX];
  int ui_multiline;                 /* Enter = newline */
  int ui_mouse;
  int ui_composer_max_rows;
  int ui_show_header;
  int ui_show_status_hints;
  int ui_show_spoiler_count;
  int ui_spoilers_default_open;     /* 0 = collapsed (sleek) */
  int ui_open_tool_spoiler_on_done;
  int ui_stream_redraw;             /* live token paint */
  int ui_clock;                     /* show time in header */
  int ui_show_thinking;             /* 0 = no thinking spam (default) */
  char ui_color_user[16];
  char ui_color_assistant[16];
  char ui_color_meta[16];
  char ui_color_think[16];
  char ui_color_tool[16];
  char ui_color_accent[16];
  char ui_color_ok[16];
  int ui_scroll_step;
  char ui_send_hint[96];
  char ui_welcome_line[160];

  /* Model adapter — different vendors leak different tags / tool styles */
  char adapter_tool_style[24];      /* openai | xml | auto */
  char adapter_thinking_tags[160];  /* comma list: think,thinking,reasoning */

  /* Visualization — no hard-coded VR SDKs; just command templates */
  char viz_desktop_cmd[192];        /* e.g. xdg-open %s */
  char viz_vr_cmd[192];             /* optional; falls back to desktop */
  int viz_term_width;
  int viz_term_height;

  /* Paths (resolved) */
  char config_path[GKX_PATH_MAX];   /* last loaded or save target */
  char state_dir_override[GKX_PATH_MAX];
} gkx_config;

void gkx_config_init(gkx_config *c);

/* Load cascading files. Returns 0 if any file loaded, 1 if defaults only. */
int gkx_config_load(gkx_config *c, const char *path_or_null);
void gkx_config_apply_env(gkx_config *c);

/* Prefer ~/.grokium/config.toml for saves; create parent dir. */
void gkx_config_resolve_save_path(gkx_config *c, char *out, size_t n);
int gkx_config_save(const gkx_config *c, const char *path_or_null);

/* Simple prefs (backend/model) under state_dir — fast session switches */
void gkx_config_load_prefs(gkx_config *c, const char *state_dir);
void gkx_config_save_prefs(const gkx_config *c, const char *state_dir);

/* Map color name → ncurses COLOR_* or -1 (default pair fg). */
int gkx_color_id(const char *name);

/* One-line summary for /settings */
void gkx_config_summary(const gkx_config *c, char *out, size_t n);

/*
 * Dual-wire settings plate (schema grokium.settings.v1).
 * Host TUI /settings show|save|key=value — tools/brain/hub flags machine-safe.
 * saved=1 after successful persist; backend/theme machine-tokenized (no inject).
 * product bus SMX2; peer HTTP lab/ops only; LLM ≠ commander.
 */
void gkx_settings_json(const gkx_config *c, int saved, char *out, size_t cap);

/*
 * Dual-wire backend plate (schema grokium.backend.v1).
 * Host TUI /backend [local|grok] and /logout — backend machine-tokenized.
 * saved=1 after prefs persist; LLM ≠ commander; product bus SMX2.
 */
void gkx_backend_json(const char *backend, int saved, char *out, size_t cap);

/*
 * Dual-wire model plate (schema grokium.model.v1).
 * Host TUI /model <id|local|grok> after prefs persist — backend/model tokenized.
 * LLM ≠ commander; product bus SMX2; peer HTTP lab/ops only.
 */
void gkx_model_json(const char *backend, const char *model, int saved,
                    char *out, size_t cap);

/*
 * Dual-wire context plate (schema grokium.context.v1).
 * Host TUI /context|/ctx [N] — context_window honesty (LLM ≠ commander).
 * saved=1 when N was applied; product bus SMX2; peer HTTP lab/ops only.
 */
void gkx_context_json(int context_window, int saved, char *out, size_t cap);

/*
 * Dual-wire multiline plate (schema grokium.multiline.v1).
 * Host TUI /multiline|/ml [on|off] — composer newline UX (LLM ≠ commander).
 * on!=0 → multiline enabled; saved=1 after prefs persist.
 */
void gkx_multiline_json(int on, int saved, char *out, size_t cap);

/*
 * Dual-wire spoilers plate (schema grokium.spoilers.v1).
 * Host TUI /expand|/collapse — no free-text spoilers: expanded banner.
 * expanded!=0 → state=expanded; else state=collapsed.
 */
void gkx_spoilers_json(int expanded, char *out, size_t cap);

/*
 * Dual-wire debug plate (schema grokium.debug.v1).
 * Host TUI /debug toggle — no free-text debug ON/OFF banner.
 */
void gkx_debug_json(int on, char *out, size_t cap);

/*
 * Dual-wire always-approve plate (schema grokium.always_approve.v1).
 * Host TUI /always-approve|/yolo toggle — no free-text ON/OFF banner.
 * on!=0 → shell auto-approve (NANOBOT_ALWAYS_APPROVE).
 */
void gkx_always_approve_json(int on, char *out, size_t cap);

/*
 * Dual-wire auth plate (schema grokium.auth.v1).
 * Host TUI /auth — has_token only (never echoes secrets); backend tokenized.
 * Replaces free-text auth=yes backend=… banner.
 */
void gkx_auth_json(int has_token, const char *backend, char *out, size_t cap);

/*
 * Dual-wire session clear plate (schema grokium.session_clear.v1).
 * Host TUI /clear|/cls|/new — action=clear|new; host-local UX only.
 * Replaces free-text (cleared)/(new session) banners.
 */
void gkx_session_clear_json(int is_new, char *out, size_t cap);

/*
 * Dual-wire interrupt plate (schema grokium.interrupt.v1).
 * Host TUI Ctrl+C with empty input — no free-text (interrupt) banner.
 */
void gkx_interrupt_json(char *out, size_t cap);

/*
 * Dual-wire empty-output plate (schema grokium.empty_output.v1).
 * Host TUI c_core capture / tool with no stdout — no free-text (no output).
 */
void gkx_empty_output_json(char *out, size_t cap);

/*
 * Dual-wire TUI help plate (schema grokium.help.v1).
 * Host TUI /help|/h|/? — command index + dual-wire honesty (no free-text dump).
 */
void gkx_tui_help_json(char *out, size_t cap);

#endif
