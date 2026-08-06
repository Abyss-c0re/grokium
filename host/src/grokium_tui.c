#define _POSIX_C_SOURCE 200809L
/* Grokium TUI — spoiler blocks for thoughts + tools. Sleek, human-usable. */
#include "grokium.h"
#include "grokium_chat.h"
#include "grokium_config.h"
#include "grokium_version.h"
#include "grokium_hub.h"
#include "grokium_media.h"
#include "grokium_session.h"
#include "grokium_status.h"
#include "grokium_law.h"
#include "grokium_llama.h"
#include "grokium_plate.h"
#include "grokium_smx_filter.h"
#include "grokium_smx.h"
#include "grokium_consolidator.h"
#include "util.h"
#include "ng_sched.h"
#include "shell.h"
#include <ncurses.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

extern char root[];
extern char cubalc_bin[];
extern char state_dir[];
extern char prog_dir[];
extern void resolve_paths(void);
extern int file_ok(const char *p);

#define BLK_MAX 200
#define IN_MAX 8192
#define BODY_CHUNK 4096

enum {
  BK_USER = 1,
  BK_ASST,
  BK_META,
  BK_THINK, /* spoiler */
  BK_TOOL   /* spoiler */
};

typedef struct {
  int kind;
  int open; /* spoilers: 0 collapsed, 1 expanded */
  char head[96];
  char *body;
  size_t len, cap;
} blk_t;

static blk_t blks[BLK_MAX];
static int blk_n;
static int focus_sp = -1; /* index of focused spoiler block, or -1 */
static char input[IN_MAX];
static int in_len;
static int in_cur;           /* cursor index in input[0..in_len] */
static int pending_esc;
static int scroll_off;
static int debug_mode;
static gkx_config cfg;

/* Color pairs filled from config */
enum {
  CP_USER = 1,
  CP_ASST = 2,
  CP_TOOL = 3,
  CP_OK = 4,
  CP_META = 5,
  CP_THINK = 6,
  CP_ACCENT = 7
};

static void log_add(const char *line);
static int is_spoiler_kind(int kind);

static void ui_apply_colors(void) {
  if (!has_colors()) return;
  start_color();
  use_default_colors();
  init_pair(CP_USER, gkx_color_id(cfg.ui_color_user), -1);
  init_pair(CP_ASST, gkx_color_id(cfg.ui_color_assistant), -1);
  init_pair(CP_TOOL, gkx_color_id(cfg.ui_color_tool), -1);
  init_pair(CP_OK, gkx_color_id(cfg.ui_color_ok), -1);
  init_pair(CP_META, gkx_color_id(cfg.ui_color_meta), -1);
  init_pair(CP_THINK, gkx_color_id(cfg.ui_color_think), -1);
  init_pair(CP_ACCENT, gkx_color_id(cfg.ui_color_accent), -1);
}

static int composer_max_rows(void) {
  int n = cfg.ui_composer_max_rows;
  if (n < 1) n = 1;
  if (n > 16) n = 16;
  return n;
}

static void config_persist(void) {
  char path[PATH_MAX], plate[512];
  gkx_config_resolve_save_path(&cfg, path, sizeof path);
  if (gkx_config_save(&cfg, path) == 0) {
    /* Shared dual-wire settings plate — no free-text path dump. */
    gkx_settings_json(&cfg, 1, plate, sizeof plate);
    log_add(plate);
  } else {
    /* Shared dual-wire save_failed (match /settings deny schema). */
    grokium_err_json("settings", "save_failed",
                     "/settings save|reload|path|show", plate, sizeof plate);
    log_add(plate);
  }
}
static gkx_version_state ver_st;
static time_t last_ver_check;
static int always_approve;
static char **g_argv;
static int g_argc;
static int live_think = -1; /* open think block during stream */
static int live_tool = -1;
static int live_asst = -1;

static void draw(void);

static void blk_free_all(void) {
  for (int i = 0; i < blk_n; i++) {
    free(blks[i].body);
    blks[i].body = NULL;
  }
  blk_n = 0;
  focus_sp = -1;
  live_think = live_tool = live_asst = -1;
}

static int blk_push(int kind, const char *head) {
  if (blk_n >= BLK_MAX) {
    /* drop oldest half */
    int drop = BLK_MAX / 4;
    for (int i = 0; i < drop; i++) free(blks[i].body);
    memmove(blks, blks + drop, (size_t)(blk_n - drop) * sizeof blks[0]);
    blk_n -= drop;
    if (live_think >= 0) live_think -= drop;
    if (live_tool >= 0) live_tool -= drop;
    if (live_asst >= 0) live_asst -= drop;
    if (focus_sp >= 0) focus_sp -= drop;
  }
  blk_t *b = &blks[blk_n];
  memset(b, 0, sizeof *b);
  b->kind = kind;
  b->open = is_spoiler_kind(kind) ? cfg.ui_spoilers_default_open : 0;
  if (head)
    snprintf(b->head, sizeof b->head, "%s", head);
  b->body = NULL;
  b->len = b->cap = 0;
  return blk_n++;
}

static void blk_append(int ix, const char *s, size_t n) {
  if (ix < 0 || ix >= blk_n || !s || !n) return;
  blk_t *b = &blks[ix];
  if (b->len + n + 1 > b->cap) {
    size_t nc = b->cap ? b->cap * 2 : BODY_CHUNK;
    while (nc < b->len + n + 1) nc *= 2;
    char *p = realloc(b->body, nc);
    if (!p) return;
    b->body = p;
    b->cap = nc;
  }
  memcpy(b->body + b->len, s, n);
  b->len += n;
  b->body[b->len] = 0;
}

static void blk_append_str(int ix, const char *s) {
  if (s) blk_append(ix, s, strlen(s));
}

static void log_add(const char *line) {
  if (!line) return;
  int ix = blk_push(BK_META, NULL);
  blk_append_str(ix, line);
}

static void log_add_block(const char *text) {
  if (!text || !text[0]) return;
  char *dup = strdup(text);
  if (!dup) return;
  char *save = NULL;
  for (char *ln = strtok_r(dup, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
    /* Shared pure-C filter: dual-wire grokium.* plates keep; free JSON drops. */
    if (!gkx_log_block_keep_line(ln, debug_mode)) continue;
    log_add(ln);
  }
  free(dup);
}

/* ---- spoiler helpers ---- */

static int is_spoiler_kind(int kind) { return kind == BK_THINK || kind == BK_TOOL; }
#define is_spoiler is_spoiler_kind

static int count_spoilers(void) {
  int n = 0;
  for (int i = 0; i < blk_n; i++)
    if (is_spoiler(blks[i].kind)) n++;
  return n;
}

static int spoiler_at_rank(int rank) {
  int n = 0;
  for (int i = 0; i < blk_n; i++) {
    if (!is_spoiler(blks[i].kind)) continue;
    if (n == rank) return i;
    n++;
  }
  return -1;
}

static int last_spoiler(void) {
  for (int i = blk_n - 1; i >= 0; i--)
    if (is_spoiler(blks[i].kind)) return i;
  return -1;
}

static void spoilers_set_all(int open) {
  for (int i = 0; i < blk_n; i++)
    if (is_spoiler(blks[i].kind)) blks[i].open = open;
}

static void toggle_spoiler(int ix) {
  if (ix < 0 || ix >= blk_n || !is_spoiler(blks[ix].kind)) return;
  blks[ix].open = !blks[ix].open;
  focus_sp = ix;
}

static void focus_next_spoiler(int dir) {
  if (count_spoilers() == 0) {
    focus_sp = -1;
    return;
  }
  int start = focus_sp < 0 ? (dir > 0 ? -1 : blk_n) : focus_sp;
  if (dir > 0) {
    for (int i = start + 1; i < blk_n; i++)
      if (is_spoiler(blks[i].kind)) {
        focus_sp = i;
        return;
      }
    for (int i = 0; i < blk_n; i++)
      if (is_spoiler(blks[i].kind)) {
        focus_sp = i;
        return;
      }
  } else {
    for (int i = start - 1; i >= 0; i--)
      if (is_spoiler(blks[i].kind)) {
        focus_sp = i;
        return;
      }
    for (int i = blk_n - 1; i >= 0; i--)
      if (is_spoiler(blks[i].kind)) {
        focus_sp = i;
        return;
      }
  }
}

static void refresh_think_head(int ix) {
  if (ix < 0 || ix >= blk_n) return;
  size_t n = blks[ix].len;
  if (n < 80)
    snprintf(blks[ix].head, sizeof blks[ix].head, "thought · %zu chars", n);
  else if (n < 1000)
    snprintf(blks[ix].head, sizeof blks[ix].head, "thought · %zu chars", n);
  else
    snprintf(blks[ix].head, sizeof blks[ix].head, "thought · %.1fk", n / 1000.0);
}

static void short_model(char *out, size_t n) {
  const char *md = cfg.active_model;
  if (md && strrchr(md, '/')) md = strrchr(md, '/') + 1;
  snprintf(out, n, "%.40s", md && md[0] ? md : "auto");
}

/* Extract "key":"value" from small JSON event (naive). */
static int json_str_field(const char *j, const char *key, char *out, size_t outn) {
  char pat[64];
  snprintf(pat, sizeof pat, "\"%s\"", key);
  const char *p = strstr(j, pat);
  if (!p) return -1;
  p = strchr(p + strlen(pat), ':');
  if (!p) return -1;
  p++;
  while (*p == ' ') p++;
  if (*p != '"') return -1;
  p++;
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < outn) {
    if (*p == '\\' && p[1]) {
      p++;
      if (*p == 'n') {
        out[i++] = '\n';
        p++;
        continue;
      }
      if (*p == 't') {
        out[i++] = '\t';
        p++;
        continue;
      }
    }
    out[i++] = *p++;
  }
  out[i] = 0;
  return 0;
}

static void handle_struct_event(const char *json) {
  char type[32] = "";
  if (json_str_field(json, "type", type, sizeof type) != 0) return;

  if (strcmp(type, "thinking") == 0) {
    if (!cfg.ui_show_thinking) return; /* no thinking spam */
    char text[2048];
    if (json_str_field(json, "text", text, sizeof text) != 0) return;
    if (live_think < 0 || live_think >= blk_n || blks[live_think].kind != BK_THINK) {
      live_think = blk_push(BK_THINK, "thought");
      blks[live_think].open = 0;
    }
    blk_append_str(live_think, text);
    refresh_think_head(live_think);
    return;
  }

  if (strcmp(type, "tool") == 0) {
    char phase[16] = "", name[64] = "", id[40] = "";
    char args[900] = "", output[900] = "";
    json_str_field(json, "phase", phase, sizeof phase);
    json_str_field(json, "name", name, sizeof name);
    json_str_field(json, "id", id, sizeof id);
    json_str_field(json, "args", args, sizeof args);
    json_str_field(json, "output", output, sizeof output);
    if (!name[0]) snprintf(name, sizeof name, "tool");

    if (strcmp(phase, "start") == 0 || !phase[0]) {
      char head[96];
      snprintf(head, sizeof head, "tool · %s", name);
      live_tool = blk_push(BK_TOOL, head);
      blks[live_tool].open = 0;
      if (id[0]) {
        blk_append_str(live_tool, "id: ");
        blk_append_str(live_tool, id);
        blk_append_str(live_tool, "\n");
      }
      if (args[0]) {
        blk_append_str(live_tool, "args: ");
        blk_append_str(live_tool, args);
        blk_append_str(live_tool, "\n");
      }
    } else if (strcmp(phase, "done") == 0) {
      if (live_tool < 0) {
        char head[96];
        snprintf(head, sizeof head, "tool · %s · done", name);
        live_tool = blk_push(BK_TOOL, head);
      } else {
        snprintf(blks[live_tool].head, sizeof blks[live_tool].head, "tool · %s · done",
                 name);
      if (cfg.ui_open_tool_spoiler_on_done) blks[live_tool].open = 1;
      }
      if (output[0]) {
        blk_append_str(live_tool, "\n— result —\n");
        blk_append_str(live_tool, output);
      }
      live_tool = -1;
    }
  }
}

typedef struct {
  char line[200];
  int len;
  int started;
} stream_acc;

static void stream_cb(void *ud, const char *chunk, size_t n) {
  stream_acc *a = ud;
  if (!chunk || !n) return;

  /* Structured events: 0x1e + json (thinking / tool) → spoilers */
  if ((unsigned char)chunk[0] == 0x1e) {
    handle_struct_event(chunk + 1);
    draw();
    return;
  }
  /* Some paths send thinking JSON without 0x1e */
  if (n > 8 && chunk[0] == '{' && strstr(chunk, "\"type\"")) {
    if (strstr(chunk, "thinking") || strstr(chunk, "\"tool\"")) {
      char *tmp = malloc(n + 1);
      if (tmp) {
        memcpy(tmp, chunk, n);
        tmp[n] = 0;
        handle_struct_event(tmp);
        free(tmp);
        draw();
      }
      return;
    }
  }

  for (size_t i = 0; i < n; i++) {
    char ch = chunk[i];
    if (ch == '\n' || a->len >= (int)sizeof a->line - 1) {
      a->line[a->len] = 0;
      if (a->len > 0) {
        if (live_asst < 0) {
          live_asst = blk_push(BK_ASST, NULL);
          a->started = 1;
        }
        blk_append_str(live_asst, a->line);
        blk_append_str(live_asst, "\n");
        draw();
      }
      a->len = 0;
      if (ch == '\n') continue;
    }
    if ((unsigned char)ch >= 32 || ch == '\t')
      a->line[a->len++] = (ch == '\t') ? ' ' : ch;
  }
  /* partial live line */
  if (a->len > 0) {
    if (live_asst < 0) {
      live_asst = blk_push(BK_ASST, NULL);
      a->started = 1;
    }
    /* paint by temporarily extending body then redraw — keep partial only in acc */
    draw();
  }
}

/* Direct shell — run immediately, then ask the agent to present results. */
static void shell_run_direct(const char *cmd) {
  if (!cmd || !cmd[0]) {
    char plate[512];
    /* Shared dual-wire need_cmd (no free-text-only usage). */
    grokium_err_json("shell", "need_cmd", "! <command> | /shell <command>",
                     plate, sizeof plate);
    log_add(plate);
    return;
  }
  while (*cmd == ' ') cmd++;
  {
    int u = blk_push(BK_USER, NULL);
    blk_append_str(u, "!");
    blk_append_str(u, cmd);
  }
  char head[96];
  snprintf(head, sizeof head, "tool · shell · direct");
  int t = blk_push(BK_TOOL, head);
  blks[t].open = 0;
  blk_append_str(t, "args: ");
  blk_append_str(t, cmd);
  blk_append_str(t, "\n");
  draw();

  {
    char home[PATH_MAX];
    snprintf(home, sizeof home, "%s/nanobot_home", state_dir);
    mkdir(home, 0700);
    setenv("NANOBOT_HOME", home, 1);
    char se[PATH_MAX];
    snprintf(se, sizeof se, "%s/shell_enabled", home);
    FILE *f = fopen(se, "w");
    if (f) {
      fputs("1\n", f);
      fclose(f);
    }
  }
  ng_shell_ensure_policy_files();
  ng_cmd_result cr = ng_run_command(cmd, 60);
  snprintf(blks[t].head, sizeof blks[t].head, "tool · shell · exit %d", cr.exit_code);
  blk_append_str(t, "\n— result —\n");
  if (cr.output && cr.output[0]) {
    blk_append_str(t, cr.output);
  } else {
    char empty_plate[512];
    /* Dual-wire empty shell body — no free-text (no output) placeholder. */
    gkx_empty_output_json(empty_plate, sizeof empty_plate);
    blk_append_str(t, empty_plate);
  }
  blks[t].open = cfg.ui_open_tool_spoiler_on_done;
  draw();

  /* Loop output back into the agent so it presents/summarizes for the user */
  {
    char prompt[IN_MAX + 2048];
    snprintf(prompt, sizeof prompt,
             "I ran this shell command myself:\n```\n%.400s\n```\n"
             "exit code: %d\n"
             "stdout/stderr:\n```\n%.3500s\n```\n"
             "Present these results to me in plain text: what happened, "
             "and the important output lines. Do not re-run the command.",
             cmd, cr.exit_code,
             (cr.output && cr.output[0]) ? cr.output : "");
    /* Use chat without tools for pure presentation */
    setenv("NANOBOT_TOOLS", "0", 1);
    stream_acc acc;
    memset(&acc, 0, sizeof acc);
    live_think = live_tool = live_asst = -1;
    char reply[16384], err[400];
    reply[0] = err[0] = 0;
    int rc = grokium_chat_request_ex(&cfg, prompt, stream_cb, &acc, reply, sizeof reply,
                                     err, sizeof err);
    setenv("NANOBOT_TOOLS", "1", 1);
    if (acc.len > 0) {
      acc.line[acc.len] = 0;
      if (live_asst < 0) live_asst = blk_push(BK_ASST, NULL);
      blk_append_str(live_asst, acc.line);
    }
    if (live_asst < 0) {
      if (rc == 0 && reply[0]) {
        int a = blk_push(BK_ASST, NULL);
        blk_append_str(a, reply);
      } else {
        char plate[512], shell_plate[512], err_tok[48];
        int ex = cr.exit_code;
        /* Shared dual-wire chat deny — shell body stays in tool spoiler only. */
        grokium_chat_err_json(
            err[0] ? err : "chat_fail",
            "shell present failed · results in tool spoiler", plate,
            sizeof plate);
        log_add(plate);
        /* Dual-wire shell exit plate — no free-text "shell exit=N" banner. */
        if (ex < 0 || ex > 9999) ex = 9999;
        snprintf(err_tok, sizeof err_tok, "present_failed_exit_%d", ex);
        grokium_err_json("shell", err_tok, "results in tool spoiler",
                         shell_plate, sizeof shell_plate);
        log_add(shell_plate);
      }
    }
    live_think = live_tool = live_asst = -1;
  }
  ng_cmd_result_free(&cr);
  draw();
}

static void chat_send(const char *msg) {
  live_think = live_tool = live_asst = -1;
  {
    int u = blk_push(BK_USER, NULL);
    blk_append_str(u, msg);
  }
  {
    char sm[48], plate[512];
    short_model(sm, sizeof sm);
    /* Dual-wire hub gate plate — no free-text "… hub gate" banner. */
    gkx_hub_wait_json(0, sm, plate, sizeof plate);
    log_add(plate);
  }
  draw();

  if (!ng_llm_sched_try_acquire()) {
    char sm[48], plate[512];
    short_model(sm, sizeof sm);
    /* Dual-wire wait plate — no free-text "hub: waiting…" banner. */
    gkx_hub_wait_json(1, sm, plate, sizeof plate);
    log_add(plate);
    draw();
    ng_llm_sched_acquire();
  }
  ng_llm_sched_release();

  stream_acc acc;
  memset(&acc, 0, sizeof acc);
  char reply[16384], err[400];
  reply[0] = err[0] = 0;
  int rc = grokium_chat_request_ex(&cfg, msg, stream_cb, &acc, reply, sizeof reply, err,
                                   sizeof err);

  if (acc.len > 0) {
    acc.line[acc.len] = 0;
    if (live_asst < 0) live_asst = blk_push(BK_ASST, NULL);
    blk_append_str(live_asst, acc.line);
  }
  if (live_asst < 0) {
    if (rc == 0 && reply[0]) {
      int a = blk_push(BK_ASST, NULL);
      blk_append_str(a, reply);
    } else {
      char plate[512];
      const char *etok =
          err[0] ? err : (rc == 1 ? "empty_reply" : "chat_fail");
      const char *hint = "chat · check local llama / hub";
      /* Shared dual-wire chat deny — machine err token · no free-text dump. */
      if (!strcmp(etok, "need_auth"))
        hint = "chat · /login or XAI_API_KEY · cloud opt-in";
      grokium_chat_err_json(etok, hint, plate, sizeof plate);
      log_add(plate);
    }
  }
  /* seal think title after stream */
  if (live_think >= 0) refresh_think_head(live_think);
  live_think = live_tool = live_asst = -1;
  draw();
}

static int run_prog_capture(const char *name) {
  char prog[PATH_MAX];
  snprintf(prog, sizeof prog, "%s/%s", prog_dir, name);
  if (access(prog, R_OK) != 0) {
    char plate[512];
    /* Shared dual-wire missing_program (no free-text path echo). */
    grokium_err_json("cubalc", "missing_program",
                     "cubalc/programs/<name.cubalc>", plate, sizeof plate);
    log_add(plate);
    return 2;
  }
  if (!file_ok(cubalc_bin)) {
    char plate[512];
    /* Shared dual-wire missing_binary (CubalC optional · hint says how). */
    grokium_err_json(
        "cubalc", "missing_binary",
        "./scripts/sync_cubalc.sh && make -C deps/cubalc all", plate,
        sizeof plate);
    log_add(plate);
    return 99;
  }
  int pipefd[2];
  if (pipe(pipefd) != 0) return 1;
  pid_t pid = fork();
  if (pid < 0) return 1;
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], 1);
    dup2(pipefd[1], 2);
    close(pipefd[1]);
    setenv("CUBALC_STATE", state_dir, 1);
    setenv("CUBALC_ASCII", "1", 1);
    setenv("GROKIUM_ROOT", root, 1);
    execl(cubalc_bin, "cubalc", "run", prog, (char *)NULL);
    _exit(127);
  }
  close(pipefd[1]);
  char acc[65536];
  size_t o = 0;
  char buf[4096];
  ssize_t n;
  while ((n = read(pipefd[0], buf, sizeof buf)) > 0) {
    if (o + (size_t)n >= sizeof acc) n = (ssize_t)(sizeof acc - 1 - o);
    if (n <= 0) break;
    memcpy(acc + o, buf, (size_t)n);
    o += (size_t)n;
  }
  acc[o] = 0;
  close(pipefd[0]);
  int st = 0;
  waitpid(pid, &st, 0);
  log_add_block(acc);
  return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

/* pure-C c_core tools (coord/smx) — never elevate LLM to commander */
static int run_c_core_capture(const char *name, char *const args[]) {
  char bin[PATH_MAX];
  char *av[16];
  int i, n = 0, pipefd[2], st = 0;
  pid_t pid;
  char acc[16384];
  size_t o = 0;
  char buf[2048];
  ssize_t rn;
  if (!name || !args) return -1;
  snprintf(bin, sizeof bin, "%s/build/%s", root, name);
  if (access(bin, X_OK) != 0) {
    char plate[512];
    /* Shared dual-wire missing_c_core (no free-text path). */
    grokium_err_json("tool", "missing_c_core", "make -C c_core all", plate,
                     sizeof plate);
    log_add(plate);
    return 99;
  }
  av[n++] = bin;
  for (i = 0; args[i] && n < 14; i++)
    av[n++] = args[i];
  av[n] = NULL;
  if (pipe(pipefd) != 0) return 1;
  pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return 1;
  }
  if (pid == 0) {
    char cdir[PATH_MAX];
    close(pipefd[0]);
    dup2(pipefd[1], 1);
    dup2(pipefd[1], 2);
    close(pipefd[1]);
    setenv("GROKIUM_ROOT", root, 1);
    snprintf(cdir, sizeof cdir, "%s/data/contracts", root);
    setenv("GROKIUM_CONTRACT_DIR", cdir, 1);
    if (chdir(root) != 0) { /* prefer repo-relative data/ */ }
    execv(bin, av);
    _exit(127);
  }
  close(pipefd[1]);
  while ((rn = read(pipefd[0], buf, sizeof buf)) > 0) {
    if (o + (size_t)rn >= sizeof acc) rn = (ssize_t)(sizeof acc - 1 - o);
    if (rn <= 0) break;
    memcpy(acc + o, buf, (size_t)rn);
    o += (size_t)rn;
  }
  acc[o] = 0;
  close(pipefd[0]);
  waitpid(pid, &st, 0);
  if (acc[0])
    log_add_block(acc);
  else {
    char plate[512];
    /* Dual-wire empty capture — no free-text (no output) banner. */
    gkx_empty_output_json(plate, sizeof plate);
    log_add(plate);
  }
  return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

static void cmd_coord_ingest(const char *plate) {
  char *av[3];
  char deny[512];
  int rc;
  grokium_law L;
  size_t n;
  if (!plate || !plate[0]) {
    /* Shared need_plate with POST /v1/coord + consolidate CLI. */
    gk_coord_err_json("need_plate", deny, sizeof deny);
    log_add(deny);
    return;
  }
  /* Host-local SMX filter gate — fail-closed before consolidate exec. */
  n = strlen(plate);
  grokium_law_default(&L);
  if (!grokium_smx_filter_allow_frame(&L, (const uint8_t *)plate, n, 1)) {
    gk_coord_err_json("smx_filter_deny", deny, sizeof deny);
    log_add(deny);
    return;
  }
  av[0] = "ingest";
  av[1] = (char *)plate;
  av[2] = NULL;
  /* Capture prints shared grokium.coord.v1 — no free-text ok banner. */
  rc = run_c_core_capture("grokium-consolidate", av);
  if (rc != 0 && rc != 99) {
    /* 99 = missing tool already logged; other failures get dual-wire deny. */
    gk_coord_err_json("ingest_failed", deny, sizeof deny);
    log_add(deny);
  }
}

static void cmd_smx_latest(void) {
  char path[PATH_MAX];
  FILE *f;
  char buf[8192], plate[1024];
  size_t n;
  char *av[2];
  snprintf(path, sizeof path, "%s/data/matrix/LATEST.json", root);
  f = fopen(path, "r");
  if (f) {
    n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    /* Dual-wire disk plate — never dump legacy free-form LATEST raw. */
    if (smx_disk_plate_json(buf, plate, sizeof plate) == 0) {
      log_add(plate);
      return;
    }
  }
  /* fallback: consolidator ability plate (no transcript dump) */
  av[0] = "ability";
  av[1] = NULL;
  (void)run_c_core_capture("grokium-consolidate", av);
}

/* Minimal JSON string field extract (meta plates only). */
static int meta_get_str(const char *body, const char *key, char *out, size_t cap) {
  char pat[96];
  const char *p, *q;
  size_t klen, i;
  if (!body || !key || !out || cap < 2) return -1;
  out[0] = 0;
  klen = strlen(key);
  if (klen + 3 >= sizeof pat) return -1;
  snprintf(pat, sizeof pat, "\"%s\"", key);
  p = strstr(body, pat);
  if (!p) return -1;
  p += klen + 2;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  if (*p != ':') return -1;
  p++;
  while (*p == ' ' || *p == '\t') p++;
  if (*p != '"') return -1;
  p++;
  q = p;
  while (*q && *q != '"') {
    if (*q == '\\' && q[1]) q += 2;
    else q++;
  }
  i = (size_t)(q - p);
  if (i >= cap) i = cap - 1;
  memcpy(out, p, i);
  out[i] = 0;
  return 0;
}

/* Imported Grok Build metas only — no chat transcripts on the TUI log. */
static void cmd_sessions_search(const char *q) {
  char data_root[PATH_MAX], plate[8192];

  /* Shared c_core dual-wire list plate (match CLI / loopback). */
  snprintf(data_root, sizeof data_root, "%s/data", root);
  gk_session_list_json(data_root, q ? q : "", plate, sizeof plate);
  log_add(plate);
}

/* Allow only HOME or repo data/ for host-local resume files. */
static int resume_path_allowed(const char *p) {
  char base[PATH_MAX];
  const char *h;
  size_t hl, bl;
  if (!p || p[0] != '/') return 0;
  h = getenv("HOME");
  if (h && h[0]) {
    hl = strlen(h);
    if (strncmp(p, h, hl) == 0 && (p[hl] == '/' || p[hl] == 0))
      return 1;
  }
  snprintf(base, sizeof base, "%s/data", root);
  bl = strlen(base);
  if (strncmp(p, base, bl) == 0 && (p[bl] == '/' || p[bl] == 0))
    return 1;
  return 0;
}

/* Decode a JSON string body (starting after opening quote) into out. */
static int json_decode_string(const char *src, char *out, size_t cap) {
  size_t o = 0;
  if (!src || !out || cap < 2) return -1;
  out[0] = 0;
  while (*src && *src != '"' && o + 1 < cap) {
    if (*src == '\\' && src[1]) {
      src++;
      if (*src == 'n')
        out[o++] = '\n';
      else if (*src == 't')
        out[o++] = '\t';
      else if (*src == 'r')
        out[o++] = '\r';
      else if (*src == '"' || *src == '\\' || *src == '/')
        out[o++] = *src;
      else if (*src == 'u' && src[1] && src[2] && src[3] && src[4])
        src += 4; /* skip unicode escape */
      else
        out[o++] = *src;
      src++;
    } else {
      out[o++] = *src++;
    }
  }
  out[o] = 0;
  return 0;
}

/* Extract user/assistant display text from a chat_history.jsonl line. */
static int resume_line_text(const char *line, int *kind_out, char *out,
                            size_t cap) {
  const char *c, *t, *p;
  size_t o = 0;
  int kind = 0;
  if (!line || !out || cap < 2) return -1;
  out[0] = 0;
  if (strstr(line, "\"type\":\"user\""))
    kind = BK_USER;
  else if (strstr(line, "\"type\":\"assistant\""))
    kind = BK_ASST;
  else
    return -1;
  c = strstr(line, "\"content\"");
  if (!c) return -1;
  c = strchr(c + 9, ':');
  if (!c) return -1;
  c++;
  while (*c == ' ' || *c == '\t') c++;
  if (*c == '"') {
    if (json_decode_string(c + 1, out, cap) != 0) return -1;
  } else if (*c == '[') {
    /* multimodal array: concatenate text parts */
    p = c;
    while ((t = strstr(p, "\"text\"")) != NULL && o + 1 < cap) {
      const char *q = strchr(t + 6, ':');
      char piece[1024];
      if (!q) break;
      q++;
      while (*q == ' ' || *q == '\t') q++;
      if (*q != '"') {
        p = t + 6;
        continue;
      }
      if (json_decode_string(q + 1, piece, sizeof piece) == 0 && piece[0]) {
        size_t pl = strlen(piece);
        if (o && o + 1 < cap) out[o++] = '\n';
        if (o + pl >= cap) pl = cap - 1 - o;
        memcpy(out + o, piece, pl);
        o += pl;
        out[o] = 0;
      }
      p = t + 6;
    }
  } else
    return -1;
  if (!out[0]) return -1;
  /* drop huge system-ish user dumps (keep head for context) */
  if (kind == BK_USER && o > 1200) {
    out[1200] = 0;
    if (o + 16 < cap)
      snprintf(out + 1200, cap - 1200, "\n…");
  } else if (o > 2000) {
    out[2000] = 0;
    snprintf(out + 2000, cap > 2008 ? 8 : cap - 2000, "\n…");
  }
  if (kind_out) *kind_out = kind;
  return 0;
}

#define RESUME_MAX_MSGS 12

/* Host-local visual resume from chat_history.jsonl — never SMX product bus. */
static int session_resume_local(const char *id, const char *meta) {
  char import_path[PATH_MAX], hist[PATH_MAX], line_buf[8192], text[2200];
  char *ring_text[RESUME_MAX_MSGS];
  int ring_kind[RESUME_MAX_MSGS];
  int ring_n = 0, ring_start = 0, loaded = 0, kind, i, ix;
  FILE *f;
  size_t n;

  import_path[0] = 0;
  meta_get_str(meta, "import_path", import_path, sizeof import_path);
  if (!import_path[0])
    meta_get_str(meta, "source", import_path, sizeof import_path);

  hist[0] = 0;
  if (import_path[0] && resume_path_allowed(import_path)) {
    snprintf(hist, sizeof hist, "%s/chat_history.jsonl", import_path);
    if (access(hist, R_OK) != 0) hist[0] = 0;
  }
  if (!hist[0]) {
    snprintf(hist, sizeof hist, "%s/data/import/%s/chat_history.jsonl", root, id);
    if (access(hist, R_OK) != 0) hist[0] = 0;
  }
  if (!hist[0]) return 0;

  f = fopen(hist, "r");
  if (!f) return 0;
  memset(ring_text, 0, sizeof ring_text);
  while (fgets(line_buf, sizeof line_buf, f)) {
    /* skip truncated monster lines */
    n = strlen(line_buf);
    if (n + 1 >= sizeof line_buf && line_buf[n - 1] != '\n') {
      int ch;
      while ((ch = fgetc(f)) != EOF && ch != '\n') {
      }
      continue;
    }
    if (resume_line_text(line_buf, &kind, text, sizeof text) != 0)
      continue;
    if (ring_n == RESUME_MAX_MSGS) {
      free(ring_text[ring_start]);
      ring_text[ring_start] = NULL;
      ring_start = (ring_start + 1) % RESUME_MAX_MSGS;
      ring_n--;
    }
    ix = (ring_start + ring_n) % RESUME_MAX_MSGS;
    ring_text[ix] = strdup(text);
    ring_kind[ix] = kind;
    if (ring_text[ix])
      ring_n++;
  }
  fclose(f);
  if (ring_n <= 0) return 0;

  /* Seed agent memory with complete user/assistant pairs (host-local).
   * Last ng_memory_recent_turns() → recent.jsonl; older → summary.txt.
   * Visual TUI may show more than recent cap. Not SMX product bus. */
  {
    const char *pending_user = NULL;
    const char *pair_u[RESUME_MAX_MSGS];
    const char *pair_a[RESUME_MAX_MSGS];
    int pairs = 0, seeded = 0, older = 0;
    for (i = 0; i < ring_n; i++) {
      ix = (ring_start + i) % RESUME_MAX_MSGS;
      if (!ring_text[ix]) continue;
      if (ring_kind[ix] == BK_USER) {
        pending_user = ring_text[ix];
      } else if (ring_kind[ix] == BK_ASST && pending_user) {
        if (pairs < RESUME_MAX_MSGS) {
          pair_u[pairs] = pending_user;
          pair_a[pairs] = ring_text[ix];
          pairs++;
        }
        pending_user = NULL;
      }
    }
    if (pairs > 0) {
      /* Memory seed is silent host-local side-effect (no free-text banner). */
      seeded = gkx_memory_seed_pairs(pair_u, pair_a, pairs);
      older = pairs - seeded;
      if (older < 0) older = 0;
      (void)seeded;
      (void)older;
    }
  }

  blk_free_all();
  for (i = 0; i < ring_n; i++) {
    ix = (ring_start + i) % RESUME_MAX_MSGS;
    if (!ring_text[ix]) continue;
    {
      int b = blk_push(ring_kind[ix], NULL);
      blk_append_str(b, ring_text[ix]);
      loaded++;
    }
    free(ring_text[ix]);
    ring_text[ix] = NULL;
  }
  return loaded;
}

static void cmd_session_pickup(const char *id) {
  char data_root[PATH_MAX], path[PATH_MAX], meta[2048], plate[2048];
  FILE *f;
  size_t nread;
  int rc, resumed = 0;

  /* Shared c_core dual-wire plate first (meta_only; never transcripts). */
  if (!id || !id[0]) {
    if (gk_session_pickup_deny_json(NULL, "need_session_id", plate,
                                     sizeof plate) == 0)
      log_add(plate);
    return;
  }
  snprintf(data_root, sizeof data_root, "%s/data", root);
  rc = gk_session_pickup_json(data_root, id, plate, sizeof plate);
  /* Shared dual-wire plate only — resume plate below is host-local UX. */
  log_add(plate);
  if (rc != 0) return;

  /* Host-local resume only — not SMX product bus / not peer HTTP. */
  snprintf(path, sizeof path, "%s/data/import/%s.meta.json", root, id);
  f = fopen(path, "r");
  if (!f) return;
  nread = fread(meta, 1, sizeof meta - 1, f);
  meta[nread] = 0;
  fclose(f);
  resumed = session_resume_local(id, meta);
  /* Dual-wire resume result — no free-text resume> loaded/meta banner. */
  if (gk_session_resume_local_json(resumed, RESUME_MAX_MSGS, plate,
                                   sizeof plate) == 0)
    log_add(plate);
}

static void cmd_integrity_tick(void) {
  char *av[2];
  /* Capture prints dual-wire integrity plate — no free-text banner. */
  av[0] = "tick";
  av[1] = NULL;
  (void)run_c_core_capture("grokium-integrity", av);
}

static void cmd_commander_show(void) {
  char *av[4];
  char law[PATH_MAX];
  /* Capture prints dual-wire commander plate — no free-text banner. */
  snprintf(law, sizeof law, "%s/data/law", root);
  av[0] = "show";
  av[1] = "--law-dir";
  av[2] = law;
  av[3] = NULL;
  (void)run_c_core_capture("grokium-commander", av);
}

/* Cube law plate — shared c_core dual-wire builder (Commander ≠ model). */
static void cmd_law_show(void) {
  char plate[768];
  grokium_law_json(NULL, plate, sizeof plate);
  log_add(plate);
}

/* License plate — shared c_core dual-wire (Apache-2.0 · not xAI · ≠ model). */
static void cmd_license_show(void) {
  char plate[512];
  grokium_license_json(plate, sizeof plate);
  log_add(plate);
}

/* Dual-wire honesty status plate (shared probes with host CLI). */
static void cmd_status_show(void) {
  char hub[640], plate[512];
  /* Shared status plate only — no free-text dual-wire banner (match CLI). */
  if (gkx_status_plate_json(root, "host_tui", plate, sizeof plate) == 0) {
    log_add_block(plate);
  } else {
    char deny[512];
    /* Shared dual-wire plate_failed (match CLI status). */
    grokium_err_json("status", "plate_failed", NULL, deny, sizeof deny);
    log_add(deny);
  }
  hub[0] = 0;
  gkx_hub_status(hub, sizeof hub);
  if (hub[0])
    log_add_block(hub);
}

/* Mode is host UX: chat/agent toggle tools; resume = meta pickup honesty. */
static void cmd_mode(const char *arg) {
  char plate[512];
  const char *a = arg ? arg : "";
  while (*a == ' ') a++;
  if (!strcmp(a, "?") || !strcmp(a, "help")) {
    /* Shared dual-wire need_subcmd (resume honesty in hint). */
    grokium_need_subcmd_json(
        "mode", "/mode chat|agent|resume|show resume=host_local_not_smx",
        plate, sizeof plate);
    log_add(plate);
    return;
  }
  if (!a[0] || !strcmp(a, "show")) {
    /* Shared dual-wire mode plate — no free-text mode> banner. */
    grokium_mode_json(cfg.agent_tools, plate, sizeof plate);
    log_add(plate);
    return;
  }
  if (!strcmp(a, "chat")) {
    cfg.agent_tools = 0;
    config_persist();
    /* Shared dual-wire mode plate (tools toggle = host UX). */
    grokium_mode_json(0, plate, sizeof plate);
    log_add(plate);
    return;
  }
  if (!strcmp(a, "agent")) {
    cfg.agent_tools = 1;
    config_persist();
    /* Shared dual-wire mode plate (tools toggle = host UX). */
    grokium_mode_json(1, plate, sizeof plate);
    log_add(plate);
    return;
  }
  if (!strcmp(a, "resume")) {
    /* Shared dual-wire mode=resume plate — no free-text mode> banner. */
    grokium_mode_resume_json(plate, sizeof plate);
    log_add(plate);
    return;
  }
  /* Unknown mode — shared dual-wire need_subcmd. */
  grokium_need_subcmd_json(
      "mode", "/mode chat|agent|resume|show resume=host_local_not_smx", plate,
      sizeof plate);
  log_add(plate);
}

/* Hive Mind manager — motivate incomplete external contracts (SMX2). */
static void cmd_manager_tick(const char *arg) {
  char *av[4];
  char dir[PATH_MAX];
  const char *p = arg ? arg : "";
  int n = 0;
  while (*p == ' ') p++;
  if (p[0] && (!strcmp(p, "help") || !strcmp(p, "?"))) {
    /* Shared c_core dual-wire help plate (CLI manager-tick help|? same). */
    char plate[512];
    grokium_manager_tick_err_json("need_dir_or_run", plate, sizeof plate);
    log_add(plate);
    return;
  }
  /* Capture prints shared grokium.manager_tick.v1 — no free-text banner. */
  av[n++] = "manager-tick";
  if (p[0] && strcmp(p, "tick") != 0) {
    /* optional contract dir (relative under repo) */
    if (strchr(p, '/') || (p[0] != '-' && !strchr(p, ' '))) {
      snprintf(dir, sizeof dir, "%s", p);
      av[n++] = dir;
    }
  }
  av[n] = NULL;
  (void)run_c_core_capture("grokium-smx-filter", av);
}

/* Parse --flag VALUE from plate (VALUE may include spaces until next --). */
static int contract_flag_val(const char *s, const char *flag, char *out,
                             size_t cap) {
  const char *p, *e;
  size_t fl, n;
  if (!s || !flag || !out || cap < 2) return -1;
  out[0] = 0;
  fl = strlen(flag);
  p = strstr(s, flag);
  if (!p) return -1;
  p += fl;
  while (*p == ' ' || *p == '\t') p++;
  if (!*p) return -1;
  e = strstr(p, " --");
  n = e ? (size_t)(e - p) : strlen(p);
  while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
  if (n >= cap) n = cap - 1;
  memcpy(out, p, n);
  out[n] = 0;
  return out[0] ? 0 : -1;
}

/* External cell contracts — form/validate/manager-tick via SMX filter. */
static void cmd_contract(const char *arg) {
  char *av[14];
  char tok[12][256];
  char rest_copy[IN_MAX];
  char assignee[80], task[400], digit[16], minset[16];
  char *save = NULL, *w;
  int n = 0;
  const char *p = arg ? arg : "";
  while (*p == ' ') p++;
  if (!p[0] || !strcmp(p, "help") || !strcmp(p, "?")) {
    char plate[512];
    /* Shared dual-wire need_subcmd (host CLI contract same builder). */
    grokium_need_subcmd_json("contract", "form|validate|manager-tick", plate,
                             sizeof plate);
    log_add(plate);
    return;
  }
  if (!strncmp(p, "manager", 7) || !strcmp(p, "tick") ||
      !strncmp(p, "manager-tick", 12)) {
    const char *sp = strchr(p, ' ');
    cmd_manager_tick(sp ? sp + 1 : "");
    return;
  }
  /* form: multi-word --task supported (until next --flag) */
  if (!strncmp(p, "form", 4) && (p[4] == ' ' || p[4] == 0)) {
    assignee[0] = task[0] = digit[0] = minset[0] = 0;
    (void)contract_flag_val(p, "--assignee", assignee, sizeof assignee);
    (void)contract_flag_val(p, "--task", task, sizeof task);
    (void)contract_flag_val(p, "--digit", digit, sizeof digit);
    (void)contract_flag_val(p, "--min-set", minset, sizeof minset);
    if (!assignee[0] || !task[0]) {
      /* Shared dual-wire plate with smx-filter CLI / HTTP contract form. */
      char plate[512];
      grokium_contract_form_err_json("need_assignee_and_task", plate,
                                     sizeof plate);
      log_add(plate);
      return;
    }
    av[n++] = "form";
    av[n++] = "--assignee";
    av[n++] = assignee;
    av[n++] = "--task";
    av[n++] = task;
    if (digit[0]) {
      av[n++] = "--digit";
      av[n++] = digit;
    }
    if (minset[0]) {
      av[n++] = "--min-set";
      av[n++] = minset;
    }
    av[n] = NULL;
    /* Capture prints shared contract_form plate — no free-text banner. */
    (void)run_c_core_capture("grokium-smx-filter", av);
    return;
  }
  /* validate without path: dual-wire need_path before shell-out */
  if (!strncmp(p, "validate", 8) && (p[8] == ' ' || p[8] == 0)) {
    const char *sp = p + 8;
    while (*sp == ' ') sp++;
    if (!*sp) {
      char plate[512];
      grokium_contract_validate_err_json("need_path", plate, sizeof plate);
      log_add(plate);
      return;
    }
  }
  /* validate / other: tokenize */
  snprintf(rest_copy, sizeof rest_copy, "%s", p);
  for (w = strtok_r(rest_copy, " \t", &save); w && n < 12;
       w = strtok_r(NULL, " \t", &save)) {
    snprintf(tok[n], sizeof tok[n], "%s", w);
    av[n] = tok[n];
    n++;
  }
  av[n] = NULL;
  if (n < 1) {
    char plate[512];
    grokium_need_subcmd_json("contract", "form|validate|manager-tick", plate,
                             sizeof plate);
    log_add(plate);
    return;
  }
  /* Capture prints shared contract validate/form plates — no free-text banner. */
  (void)run_c_core_capture("grokium-smx-filter", av);
}

/* Fleet plate: pure-C grokium-fleet (honest pid/status). CubalC opt-in. */
static void cmd_fleet(const char *arg) {
  char *av[8];
  char sub[64], a1[PATH_MAX], a2[64], a3[PATH_MAX];
  int n = 0;
  const char *p = arg ? arg : "";
  while (*p == ' ') p++;
  sub[0] = a1[0] = a2[0] = a3[0] = 0;
  /* note-pid ID PID [path] needs three args after subcmd. */
  sscanf(p, "%63s %511s %63s %511s", sub, a1, a2, a3);
  if (!sub[0]) {
    /* default: live status probe — dual-wire plate only (no free-text banner). */
    av[0] = "status";
    av[1] = NULL;
    (void)run_c_core_capture("grokium-fleet", av);
    return;
  }
  if (!strcmp(sub, "help") || !strcmp(sub, "?")) {
    char plate[512];
    /* Shared dual-wire need_subcmd (pid honest · no free-text-only usage). */
    grokium_need_subcmd_json(
        "fleet",
        "/fleet [status|defaults|deploy|save|spawn ID|spawn-all|"
        "note-pid ID PID|separate ID|stop-all|cubalc] pid honest",
        plate, sizeof plate);
    log_add(plate);
    return;
  }
  if (!strcmp(sub, "cubalc")) {
    run_prog_capture("fleet.cubalc");
    return;
  }
  /* pass-through to grokium-fleet (up to three args after subcmd). */
  av[n++] = sub;
  if (a1[0]) av[n++] = a1;
  if (a2[0]) av[n++] = a2;
  if (a3[0]) av[n++] = a3;
  av[n] = NULL;
  /* Capture prints shared nanobot_* plates — no free-text dual-wire banner. */
  (void)run_c_core_capture("grokium-fleet", av);
}

static void do_login(int device) {
  char plate[512], tok[64], dummy[8];
  int has = 0;
  const char *grok;
  char *av[6];
  int ac = 0;
  pid_t pid;

  endwin();
  /* Optional cloud opt-in via external grok CLI — dual-wire only (no free-text
   * affiliation banner / "token OK" prose). */
  grok = getenv("GROK_CLI");
  if (!grok || !grok[0]) grok = "grok";
  av[ac++] = (char *)grok;
  av[ac++] = "login";
  if (device) av[ac++] = "--device-auth";
  else av[ac++] = "--oauth";
  av[ac] = NULL;
  pid = fork();
  if (pid == 0) {
    execvp(grok, av);
    _exit(127);
  }
  if (pid > 0) {
    int st = 0;
    waitpid(pid, &st, 0);
  }
  if (grokium_load_grok_token(tok, sizeof tok) == 0) {
    has = 1;
    snprintf(cfg.active_backend, sizeof cfg.active_backend, "grok");
    snprintf(cfg.active_model, sizeof cfg.active_model, "%s", cfg.grok_model);
    gkx_config_save_prefs(&cfg, state_dir);
  }
  /* Dual-wire login result on stdout (TTY handoff) then TUI log. */
  gkx_login_json(has, device, plate, sizeof plate);
  printf("%s\n", plate);
  (void)fgets(dummy, sizeof dummy, stdin);
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
  if (has_colors()) {
    start_color();
    use_default_colors();
    init_pair(1, COLOR_RED, -1);
    init_pair(2, COLOR_CYAN, -1);
    init_pair(3, COLOR_YELLOW, -1);
    init_pair(4, COLOR_GREEN, -1);
    init_pair(5, COLOR_WHITE, -1);
    init_pair(6, COLOR_MAGENTA, -1);
  }
  curs_set(1);
  log_add(plate);
}

static void cmd_model_list(void) {
  char err[200];
  char *body = grokium_models_json(&cfg, err, sizeof err);
  if (!body) {
    char plate[512];
    /* Shared dual-wire models fail (match CLI) — no free-text err dump. */
    grokium_err_json("models", err[0] ? err : "models_fail",
                     "models local llama /v1/models", plate, sizeof plate);
    log_add(plate);
    return;
  }
  /* Human id list is host UX — no free-text dual-wire banner. */
  {
    const char *p = body;
    int count = 0;
    while ((p = strstr(p, "\"id\"")) != NULL && count < 40) {
      p += 4;
      while (*p && *p != '"') p++;
      if (*p != '"') break;
      p++;
      {
        char id[512];
        size_t i = 0;
        while (*p && *p != '"' && i + 1 < sizeof id) id[i++] = *p++;
        id[i] = 0;
        if (i > 0) {
          char line[240];
          const char *show = strrchr(id, '/') ? strrchr(id, '/') + 1 : id;
          snprintf(line, sizeof line, "  %s %s",
                   strcmp(id, cfg.active_model) == 0 ? "*" : "-", show);
          log_add(line);
          count++;
        }
      }
    }
  }
  free(body);
}

static void do_command(const char *raw) {
  while (*raw == ' ') raw++;
  if (!raw[0]) return;
  /* Bang shell: !cmd  or  ! cmd */
  if (raw[0] == '!') {
    shell_run_direct(raw + 1);
    return;
  }
  if (raw[0] == '/') raw++;

  char cmd[64], rest[IN_MAX];
  rest[0] = 0;
  if (sscanf(raw, "%63s %799[^\n]", cmd, rest) < 1) return;
  for (char *p = cmd; *p; p++) *p = (char)tolower((unsigned char)*p);

  if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
    endwin();
    exit(0);
  }
  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0 || strcmp(cmd, "?") == 0) {
    char plate[768];
    /* Dual-wire help plate — no free-text multi-line usage dump. */
    gkx_tui_help_json(plate, sizeof plate);
    log_add(plate);
    return;
  }
  if (strcmp(cmd, "shell") == 0 || strcmp(cmd, "sh") == 0 || strcmp(cmd, "run") == 0) {
    shell_run_direct(rest);
    return;
  }
  if (strcmp(cmd, "multiline") == 0 || strcmp(cmd, "ml") == 0) {
    char plate[512], path[PATH_MAX];
    int saved;
    if (rest[0]) {
      if (strcmp(rest, "on") == 0 || strcmp(rest, "1") == 0)
        cfg.ui_multiline = 1;
      else if (strcmp(rest, "off") == 0 || strcmp(rest, "0") == 0)
        cfg.ui_multiline = 0;
    } else
      cfg.ui_multiline = !cfg.ui_multiline;
    gkx_config_resolve_save_path(&cfg, path, sizeof path);
    saved = (gkx_config_save(&cfg, path) == 0);
    /* Dual-wire multiline plate only — no free-text ON/OFF banner. */
    if (saved) {
      gkx_multiline_json(cfg.ui_multiline, 1, plate, sizeof plate);
      log_add(plate);
    } else {
      grokium_err_json("settings", "save_failed",
                       "/settings save|reload|path|show", plate, sizeof plate);
      log_add(plate);
    }
    return;
  }
  if (strcmp(cmd, "settings") == 0 || strcmp(cmd, "config") == 0) {
    if (!rest[0] || !strcmp(rest, "show")) {
      char plate[640];
      /* Dual-wire settings plate only — no free-text detail dump. */
      gkx_settings_json(&cfg, 0, plate, sizeof plate);
      log_add(plate);
      return;
    }
    if (!strcmp(rest, "save")) { config_persist(); return; }
    if (!strcmp(rest, "reload")) {
      char plate[640];
      gkx_config_load(&cfg, NULL);
      gkx_config_apply_env(&cfg);
      ui_apply_colors();
      /* Shared dual-wire settings plate after reload (saved=false). */
      gkx_settings_json(&cfg, 0, plate, sizeof plate);
      log_add(plate);
      return;
    }
    if (!strcmp(rest, "path")) {
      char path[PATH_MAX], plate[640];
      gkx_config_resolve_save_path(&cfg, path, sizeof path);
      /* Dual-wire path plate — no free-text save path banner. */
      gkx_settings_path_json(path, plate, sizeof plate);
      log_add(plate);
      return;
    }
    {
      char *eq = strchr(rest, '=');
      char k[80], v[160];
      int on;
      if (!eq) {
        char plate[512];
        /* Shared dual-wire need_key_value. */
        grokium_err_json(
            "settings", "need_key_value",
            "/settings key=value|save|reload|path|show", plate, sizeof plate);
        log_add(plate);
        return;
      }
      *eq = 0;
      snprintf(k, sizeof k, "%s", rest);
      snprintf(v, sizeof v, "%s", eq + 1);
      on = (v[0]=='1'||v[0]=='t'||v[0]=='y'||v[0]=='T'||v[0]=='Y'||!strcmp(v,"on")||!strcmp(v,"true"));
      if (!strcmp(k,"ui.multiline")||!strcmp(k,"multiline")) cfg.ui_multiline = on;
      else if (!strcmp(k,"ui.mouse")||!strcmp(k,"mouse")) cfg.ui_mouse = on;
      else if (!strcmp(k,"ui.theme")||!strcmp(k,"theme"))
        snprintf(cfg.ui_theme, sizeof cfg.ui_theme, "%s", v);
      else if (!strcmp(k,"ui.product_name")||!strcmp(k,"product_name"))
        snprintf(cfg.ui_product_name, sizeof cfg.ui_product_name, "%s", v);
      else if (!strcmp(k,"agent.tools")||!strcmp(k,"tools")) cfg.agent_tools = on;
      else if (!strcmp(k,"agent.braincells")||!strcmp(k,"braincells")) cfg.agent_braincells = on;
      else if (!strcmp(k,"agent.max_turns")||!strcmp(k,"max_turns"))
        cfg.agent_max_turns = atoi(v);
      else if (!strcmp(k,"agent.always_approve")||!strcmp(k,"always_approve"))
        cfg.agent_always_approve = on;
      else if (!strcmp(k,"model.local.base_url")||!strcmp(k,"base_url"))
        snprintf(cfg.local_base_url, sizeof cfg.local_base_url, "%s", v);
      else if (!strcmp(k,"model.local.context_window")||!strcmp(k,"context_window"))
        cfg.context_window = atoi(v);
      else if (!strcmp(k,"hub.enabled")) cfg.hub_enabled = on;
      else if (!strcmp(k,"ui.color_assistant"))
        snprintf(cfg.ui_color_assistant, sizeof cfg.ui_color_assistant, "%s", v);
      else if (!strcmp(k,"ui.color_user"))
        snprintf(cfg.ui_color_user, sizeof cfg.ui_color_user, "%s", v);
      else if (!strcmp(k,"ui.color_think"))
        snprintf(cfg.ui_color_think, sizeof cfg.ui_color_think, "%s", v);
      else if (!strcmp(k,"ui.color_tool"))
        snprintf(cfg.ui_color_tool, sizeof cfg.ui_color_tool, "%s", v);
      else if (!strcmp(k,"ui.spoilers_default_open"))
        cfg.ui_spoilers_default_open = on;
      else if (!strcmp(k,"ui.composer_max_rows"))
        cfg.ui_composer_max_rows = atoi(v);
      else if (!strcmp(k,"ui.stream_redraw"))
        cfg.ui_stream_redraw = on;
      else {
        char plate[512];
        /* Shared dual-wire unknown_key (no free-text key echo). */
        grokium_err_json(
            "settings", "unknown_key",
            "/settings key=value|save|reload|path|show", plate, sizeof plate);
        log_add(plate);
        return;
      }
      always_approve = cfg.agent_always_approve;
      ui_apply_colors();
      /* config_persist emits dual-wire settings plate (saved=true). */
      config_persist();
    }
    return;
  }
  if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0 || strcmp(cmd, "new") == 0) {
    char plate[512];
    int is_new = strcmp(cmd, "new") == 0;
    blk_free_all();
    /* Dual-wire session clear plate — no free-text (cleared)/(new session). */
    gkx_session_clear_json(is_new, plate, sizeof plate);
    log_add(plate);
    return;
  }
  if (strcmp(cmd, "expand") == 0 || strcmp(cmd, "open") == 0) {
    char plate[512];
    spoilers_set_all(1);
    /* Dual-wire spoilers plate — no free-text spoilers: expanded banner. */
    gkx_spoilers_json(1, plate, sizeof plate);
    log_add(plate);
    return;
  }
  if (strcmp(cmd, "collapse") == 0 || strcmp(cmd, "fold") == 0) {
    char plate[512];
    spoilers_set_all(0);
    /* Dual-wire spoilers plate — no free-text spoilers: collapsed banner. */
    gkx_spoilers_json(0, plate, sizeof plate);
    log_add(plate);
    return;
  }
  if (strcmp(cmd, "debug") == 0) {
    char plate[512];
    debug_mode = !debug_mode;
    /* Dual-wire debug plate — no free-text debug ON/OFF banner. */
    gkx_debug_json(debug_mode, plate, sizeof plate);
    log_add(plate);
    return;
  }
  if (strcmp(cmd, "always-approve") == 0 || strcmp(cmd, "yolo") == 0) {
    char plate[512];
    always_approve = !always_approve;
    setenv("NANOBOT_ALWAYS_APPROVE", always_approve ? "1" : "0", 1);
    /* Dual-wire always-approve plate — no free-text ON/OFF banner. */
    gkx_always_approve_json(always_approve, plate, sizeof plate);
    log_add(plate);
    return;
  }
  if (strcmp(cmd, "login") == 0 || strcmp(cmd, "grok") == 0) {
    do_login(rest[0] && strstr(rest, "device") != NULL);
    return;
  }
  if (strcmp(cmd, "logout") == 0) {
    char plate[512];
    snprintf(cfg.active_backend, sizeof cfg.active_backend, "local");
    snprintf(cfg.active_model, sizeof cfg.active_model, "%s", cfg.local_model);
    gkx_config_save_prefs(&cfg, state_dir);
    /* Dual-wire backend plate — no free-text backend= banner. */
    gkx_backend_json(cfg.active_backend, 1, plate, sizeof plate);
    log_add(plate);
    return;
  }
  if (strcmp(cmd, "auth") == 0) {
    char tok[64], plate[512];
    int has = (grokium_load_grok_token(tok, sizeof tok) == 0);
    /* Dual-wire auth plate — has_token only · never free-text token/banner. */
    gkx_auth_json(has, cfg.active_backend, plate, sizeof plate);
    log_add(plate);
    return;
  }
  if (strcmp(cmd, "backend") == 0) {
    if (!rest[0]) {
      char plate[512];
      /* Dual-wire show (saved=false) — no free-text backend= banner. */
      gkx_backend_json(cfg.active_backend, 0, plate, sizeof plate);
      log_add(plate);
      return;
    }
    for (char *p = rest; *p; p++) *p = (char)tolower((unsigned char)*p);
    if (strcmp(rest, "local") == 0 || strcmp(rest, "llama") == 0) {
      char plate[512];
      snprintf(cfg.active_backend, sizeof cfg.active_backend, "local");
      gkx_config_save_prefs(&cfg, state_dir);
      gkx_backend_json(cfg.active_backend, 1, plate, sizeof plate);
      log_add(plate);
    } else if (strcmp(rest, "grok") == 0 || strcmp(rest, "cloud") == 0) {
      char plate[512];
      snprintf(cfg.active_backend, sizeof cfg.active_backend, "grok");
      snprintf(cfg.active_model, sizeof cfg.active_model, "%s", cfg.grok_model);
      gkx_config_save_prefs(&cfg, state_dir);
      gkx_backend_json(cfg.active_backend, 1, plate, sizeof plate);
      log_add(plate);
    } else {
      char plate[512];
      /* Shared dual-wire need_backend (LLM ≠ commander). */
      grokium_err_json("backend", "need_backend", "/backend local|grok", plate,
                       sizeof plate);
      log_add(plate);
    }
    return;
  }
  if (strcmp(cmd, "model") == 0 || strcmp(cmd, "m") == 0) {
    if (!rest[0] || strcmp(rest, "list") == 0) {
      cmd_model_list();
      return;
    }
    if (strcmp(rest, "local") == 0) {
      snprintf(cfg.active_backend, sizeof cfg.active_backend, "local");
      snprintf(cfg.active_model, sizeof cfg.active_model, "auto");
    } else if (strcmp(rest, "grok") == 0) {
      snprintf(cfg.active_backend, sizeof cfg.active_backend, "grok");
      snprintf(cfg.active_model, sizeof cfg.active_model, "%s", cfg.grok_model);
    } else {
      snprintf(cfg.active_model, sizeof cfg.active_model, "%s", rest);
      snprintf(cfg.active_backend, sizeof cfg.active_backend,
               strncmp(rest, "grok", 4) == 0 ? "grok" : "local");
    }
    {
      char plate[512];
      gkx_config_save_prefs(&cfg, state_dir);
      /* Dual-wire model plate — no free-text "model set" banner. */
      gkx_model_json(cfg.active_backend, cfg.active_model, 1, plate,
                     sizeof plate);
      log_add(plate);
    }
    return;
  }
  if (strcmp(cmd, "context") == 0 || strcmp(cmd, "ctx") == 0) {
    char plate[512];
    int saved = 0;
    if (rest[0]) {
      int n = atoi(rest);
      if (n > 1024) {
        cfg.context_window = n;
        saved = 1;
      }
    }
    /* Dual-wire context plate — no free-text context_window= banner. */
    gkx_context_json(cfg.context_window, saved, plate, sizeof plate);
    log_add(plate);
    return;
  }
  if (strcmp(cmd, "status") == 0) {
    cmd_status_show();
    return;
  }
  if (strcmp(cmd, "compat") == 0 || strcmp(cmd, "hub") == 0) {
    char line[640];
    gkx_hub_status(line, sizeof line);
    log_add_block(line);
    {
      char *js = ng_llm_sched_status_json();
      if (js) {
        log_add(js);
        free(js);
      }
    }
    if (rest[0] && strcmp(rest, "start") == 0) {
      if (gkx_hub_ensure(&cfg) == 0) {
        char plate[640];
        /* Shared dual-wire hub status after ensure (match CLI hub start). */
        gkx_hub_status(plate, sizeof plate);
        log_add(plate);
      } else {
        char plate[512];
        /* Shared dual-wire hub_start_failed. */
        grokium_err_json(
            "hub", "hub_start_failed",
            "/hub status check nanobot peer + local llama", plate, sizeof plate);
        log_add(plate);
      }
    } else if (rest[0] && strcmp(rest, "stop") == 0) {
      char plate[640];
      gkx_hub_stop();
      /* Shared dual-wire stop ack (match CLI hub stop). */
      gkx_hub_stop_json(plate, sizeof plate);
      log_add(plate);
    } else if (rest[0] && strcmp(rest, "status") != 0 &&
               strcmp(rest, "show") != 0 && strcmp(rest, "help") != 0 &&
               strcmp(rest, "?") != 0 && strcmp(rest, "compat") != 0) {
      char plate[512];
      /* Shared dual-wire need_subcmd (match CLI hub path). */
      grokium_need_subcmd_json("hub", "/hub [start|stop|status]", plate,
                               sizeof plate);
      log_add(plate);
    }
    return;
  }
  if (strcmp(cmd, "coord") == 0 || strcmp(cmd, "ingest") == 0 ||
      strcmp(cmd, "smx-ingest") == 0) {
    cmd_coord_ingest(rest);
    return;
  }
  if (strcmp(cmd, "smx") == 0 || strcmp(cmd, "matrix") == 0) {
    cmd_smx_latest();
    return;
  }
  if (strcmp(cmd, "sessions") == 0 || strcmp(cmd, "session") == 0) {
    cmd_sessions_search(rest);
    return;
  }
  if (strcmp(cmd, "pickup") == 0 || strcmp(cmd, "load") == 0 ||
      strcmp(cmd, "resume") == 0) {
    cmd_session_pickup(rest);
    return;
  }
  if (strcmp(cmd, "integrity") == 0 || strcmp(cmd, "seal") == 0) {
    cmd_integrity_tick();
    return;
  }
  if (strcmp(cmd, "commander") == 0 || strcmp(cmd, "cmd") == 0) {
    cmd_commander_show();
    return;
  }
  if (strcmp(cmd, "law") == 0 || strcmp(cmd, "laws") == 0) {
    cmd_law_show();
    return;
  }
  if (strcmp(cmd, "license") == 0 || strcmp(cmd, "licence") == 0) {
    cmd_license_show();
    return;
  }
  if (strcmp(cmd, "mode") == 0) {
    cmd_mode(rest);
    return;
  }
  if (strcmp(cmd, "attach") == 0 || strcmp(cmd, "file") == 0 || strcmp(cmd, "open") == 0) {
    if (!rest[0]) {
      char plate[512];
      /* Shared dual-wire need_path (no free-text-only usage). */
      grokium_err_json("attach", "need_path",
                       "/attach <path> [prompt for vision]", plate, sizeof plate);
      log_add(plate);
      return;
    }
    char path[PATH_MAX], prompt[IN_MAX];
    prompt[0] = 0;
    if (sscanf(rest, "%511s %799[^\n]", path, prompt) < 1) {
      char plate[512];
      grokium_err_json("attach", "need_path", "/attach <path>", plate,
                       sizeof plate);
      log_add(plate);
      return;
    }
    size_t raw_n = 0;
    unsigned char *raw = gkx_file_read_raw(path, &raw_n);
    if (!raw) {
      char plate[512];
      /* Shared dual-wire file_unreadable (no free-text path). */
      grokium_err_json("attach", "file_unreadable", "/attach <readable-path>",
                       plate, sizeof plate);
      log_add(plate);
      return;
    }
    {
      char plate[512];
      /* Meta-only dual-wire media plate — size on plate, no free-text path. */
      if (gkx_media_plate_json(path, 1, NULL, raw_n, plate, sizeof plate) == 0)
        log_add(plate);
    }
    if (gkx_path_is_image(path)) {
      char reply[16384], err[400], plate[512];
      reply[0] = err[0] = plate[0] = 0;
      /* Vision runs quietly; result honesty stays on the media plate. */
      draw();
      int rc = gkx_chat_vision(&cfg, prompt[0] ? prompt : "Describe this image in detail.",
                               path, reply, sizeof reply, err, sizeof err);
      int a = blk_push(BK_ASST, NULL);
      if (rc == 0 && reply[0]) {
        blk_append_str(a, reply);
      } else {
        /* Machine token only — free-text backend err stays off chat wire. */
        blk_append_str(a, "vision_failed");
      }
      /* Final dual-wire media plate (ok/error; no image bytes). */
      if (gkx_media_plate_json(path, rc == 0 && reply[0],
                               rc == 0 ? NULL : (err[0] ? err : "vision_failed"),
                               raw_n, plate, sizeof plate) == 0)
        log_add(plate);
    } else {
      const char *mime = gkx_mime_guess(path);
      if (!strncmp(mime, "text/", 5) || strstr(mime, "json") ||
          strstr(mime, "xml")) {
        /* raw text — inject into chat as user attachment context */
        size_t cap = raw_n > 12000 ? 12000 : raw_n;
        char *msg = malloc(cap + 200);
        if (msg) {
          snprintf(msg, cap + 200,
                   "File `%s` (raw, first %zu bytes):\n```\n%.*s\n```\n%s",
                   path, cap, (int)cap, (char *)raw,
                   prompt[0] ? prompt : "Summarize or act on this file.");
          chat_send(msg);
          free(msg);
        }
      } else {
        /* Binary: size lives on media plate; hex head is host UX only. */
        char hex[200];
        size_t i, h = 0;
        h = snprintf(hex, sizeof hex, "raw head:");
        for (i = 0; i < raw_n && i < 24 && h + 4 < sizeof hex; i++)
          h += (size_t)snprintf(hex + h, sizeof hex - h, " %02x", raw[i]);
        log_add(hex);
      }
    }
    free(raw);
    return;
  }
  if (strcmp(cmd, "viz") == 0) {
    char sub[32], arg[PATH_MAX];
    sub[0] = arg[0] = 0;
    sscanf(rest, "%31s %511s", sub, arg);
    if (!sub[0] || !strcmp(sub, "help") || !strcmp(sub, "?")) {
      char plate[512];
      /* Dual-wire need_subcmd only — no free-text viz help banner lines. */
      grokium_need_subcmd_json(
          "viz", "/viz term|open|vr · no hard-coded VR SDK", plate,
          sizeof plate);
      log_add(plate);
      return;
    }
    if (!strcmp(sub, "open")) {
      if (!arg[0]) {
        char plate[512];
        grokium_err_json("viz", "need_path", "/viz open <path>", plate,
                         sizeof plate);
        log_add(plate);
        return;
      }
      if (gkx_viz_open(&cfg, arg, 0) == 0) {
        char plate[512];
        /* Dual-wire open success — no free-text path/banner. */
        if (gkx_viz_plate_json(1, 0, NULL, plate, sizeof plate) == 0)
          log_add(plate);
      } else {
        char plate[512];
        /* Shared dual-wire open_failed (no free-text-only). */
        grokium_err_json("viz", "open_failed",
                         "/viz open <path> set viz.desktop_cmd", plate,
                         sizeof plate);
        log_add(plate);
      }
      return;
    }
    if (!strcmp(sub, "vr")) {
      if (!arg[0]) {
        char plate[512];
        grokium_err_json("viz", "need_path", "/viz vr <path>", plate,
                         sizeof plate);
        log_add(plate);
        return;
      }
      if (gkx_viz_open(&cfg, arg, 1) == 0) {
        char plate[512];
        /* Dual-wire vr success — no free-text path/banner. */
        if (gkx_viz_plate_json(1, 1, NULL, plate, sizeof plate) == 0)
          log_add(plate);
      } else {
        char plate[512];
        grokium_err_json("viz", "open_failed",
                         "/viz vr <path> set viz.vr_cmd", plate, sizeof plate);
        log_add(plate);
      }
      return;
    }
    if (!strcmp(sub, "term") || !strcmp(sub, "2d")) {
      double ys[128];
      int n = 0;
      const char *p = arg[0] ? arg : rest;
      if (!strcmp(sub, "term") || !strcmp(sub, "2d")) {
        /* numbers after sub */
        char *sp = strchr(rest, ' ');
        p = sp ? sp + 1 : rest;
      }
      while (*p && n < 128) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        ys[n++] = atof(p);
        while (*p && *p != ',' && *p != ' ') p++;
      }
      if (n < 1) {
        /* demo wave */
        for (n = 0; n < 32; n++) ys[n] = (n % 7) + (n % 3) * 0.5;
      }
      char *plot = gkx_viz_term2d_bars(ys, n, cfg.viz_term_width, cfg.viz_term_height);
      if (plot) {
        log_add_block(plot);
        free(plot);
      }
      return;
    }
    {
      char plate[512];
      /* Unknown subcmd — shared dual-wire need_subcmd. */
      grokium_need_subcmd_json("viz", "/viz term|open|vr", plate, sizeof plate);
      log_add(plate);
    }
    return;
  }
  if (strcmp(cmd, "board") == 0) {

    run_prog_capture("board.cubalc");
    return;
  }
  if (strcmp(cmd, "fleet") == 0 || strcmp(cmd, "nanobot") == 0) {
    cmd_fleet(rest);
    return;
  }
  if (strcmp(cmd, "manager") == 0 || strcmp(cmd, "manager-tick") == 0) {
    cmd_manager_tick(rest);
    return;
  }
  if (strcmp(cmd, "contract") == 0 || strcmp(cmd, "contracts") == 0) {
    cmd_contract(rest);
    return;
  }
  if (strcmp(cmd, "selftest") == 0) {
    run_prog_capture("selftest.cubalc");
    return;
  }
  chat_send(raw);
}

/* Flatten blocks into visible rows for drawing / hit-test */
typedef struct {
  int blk;
  int is_header; /* spoiler header row */
  const char *text;
  char tmp[240];
} vrow_t;

#define VROW_MAX 2000
static vrow_t vrows[VROW_MAX];
static int vrow_n;

static void rebuild_vrows(void) {
  vrow_n = 0;
  for (int i = 0; i < blk_n && vrow_n < VROW_MAX - 8; i++) {
    blk_t *b = &blks[i];
    if (b->kind == BK_USER) {
      vrows[vrow_n++] = (vrow_t){.blk = i, .is_header = 0, .text = "you>"};
      if (b->body && b->body[0]) {
        char *dup = strdup(b->body);
        if (dup) {
          char *save = NULL;
          for (char *ln = strtok_r(dup, "\n", &save); ln && vrow_n < VROW_MAX;
               ln = strtok_r(NULL, "\n", &save)) {
            vrows[vrow_n].blk = i;
            vrows[vrow_n].is_header = 0;
            snprintf(vrows[vrow_n].tmp, sizeof vrows[vrow_n].tmp, "  %s", ln);
            vrows[vrow_n].text = vrows[vrow_n].tmp;
            vrow_n++;
          }
          free(dup);
        }
      }
    } else if (b->kind == BK_ASST) {
      vrows[vrow_n++] = (vrow_t){.blk = i, .is_header = 0, .text = "cube>"};
      if (b->body && b->body[0]) {
        char *dup = strdup(b->body);
        if (dup) {
          char *save = NULL;
          for (char *ln = strtok_r(dup, "\n", &save); ln && vrow_n < VROW_MAX;
               ln = strtok_r(NULL, "\n", &save)) {
            vrows[vrow_n].blk = i;
            vrows[vrow_n].is_header = 0;
            snprintf(vrows[vrow_n].tmp, sizeof vrows[vrow_n].tmp, "  %s", ln);
            vrows[vrow_n].text = vrows[vrow_n].tmp;
            vrow_n++;
          }
          free(dup);
        }
      }
    } else if (b->kind == BK_META) {
      vrows[vrow_n].blk = i;
      vrows[vrow_n].is_header = 0;
      vrows[vrow_n].text = b->body ? b->body : "";
      vrow_n++;
    } else if (is_spoiler(b->kind)) {
      /* header always visible */
      const char *mark = b->open ? "v" : ">";
      const char *focus = (focus_sp == i) ? "*" : " ";
      vrows[vrow_n].blk = i;
      vrows[vrow_n].is_header = 1;
      snprintf(vrows[vrow_n].tmp, sizeof vrows[vrow_n].tmp, "%s%s %s", focus, mark,
               b->head[0] ? b->head : (b->kind == BK_THINK ? "thought" : "tool"));
      vrows[vrow_n].text = vrows[vrow_n].tmp;
      vrow_n++;
      if (b->open && b->body && b->body[0]) {
        char *dup = strdup(b->body);
        if (dup) {
          char *save = NULL;
          for (char *ln = strtok_r(dup, "\n", &save); ln && vrow_n < VROW_MAX;
               ln = strtok_r(NULL, "\n", &save)) {
            vrows[vrow_n].blk = i;
            vrows[vrow_n].is_header = 0;
            snprintf(vrows[vrow_n].tmp, sizeof vrows[vrow_n].tmp, "    %s", ln);
            vrows[vrow_n].text = vrows[vrow_n].tmp;
            vrow_n++;
          }
          free(dup);
        }
      }
    }
  }
}

/* Count newlines in [0, end) of input for composer height. */
static int input_line_count(void) {
  int n = 1;
  for (int i = 0; i < in_len; i++)
    if (input[i] == '\n') n++;
  if (n > composer_max_rows()) n = composer_max_rows();
  if (n < 1) n = 1;
  return n;
}

/* Cursor (row,col) within composer; row 0 = first line. */
static void input_cursor_rc(int *out_r, int *out_c) {
  int r = 0, c = 0;
  for (int i = 0; i < in_cur && i < in_len; i++) {
    if (input[i] == '\n') {
      r++;
      c = 0;
    } else
      c++;
  }
  *out_r = r;
  *out_c = c;
}

static void input_insert(char ch) {
  if (in_len + 1 >= IN_MAX) return;
  if (in_cur < 0) in_cur = 0;
  if (in_cur > in_len) in_cur = in_len;
  memmove(input + in_cur + 1, input + in_cur, (size_t)(in_len - in_cur));
  input[in_cur] = ch;
  in_len++;
  in_cur++;
  input[in_len] = 0;
}

static void input_backspace(void) {
  if (in_cur <= 0) return;
  memmove(input + in_cur - 1, input + in_cur, (size_t)(in_len - in_cur));
  in_cur--;
  in_len--;
  input[in_len] = 0;
}

static void input_delete(void) {
  if (in_cur >= in_len) return;
  memmove(input + in_cur, input + in_cur + 1, (size_t)(in_len - in_cur));
  in_len--;
  input[in_len] = 0;
}

static void submit_input(void) {
  input[in_len] = 0;
  /* trim trailing newlines only */
  while (in_len > 0 && (input[in_len - 1] == '\n' || input[in_len - 1] == '\r'))
    input[--in_len] = 0;
  if (in_len == 0) {
    if (focus_sp >= 0) toggle_spoiler(focus_sp);
    return;
  }
  char line[IN_MAX];
  memcpy(line, input, (size_t)in_len + 1);
  in_len = in_cur = 0;
  input[0] = 0;
  if (strcmp(line, "q") == 0 || strcmp(line, "quit") == 0) {
    endwin();
    exit(0);
  }
  do_command(line);
  scroll_off = 0;
}

static void draw(void) {
  int rows, cols;
  getmaxyx(stdscr, rows, cols);
  erase();
  rebuild_vrows();

  if (cfg.ui_show_header) {
    attron(A_BOLD | COLOR_PAIR(CP_ACCENT));
    mvprintw(0, 0, " %s", cfg.ui_product_name[0] ? cfg.ui_product_name : "Grokium");
    attroff(A_BOLD | COLOR_PAIR(CP_ACCENT));
    attron(A_DIM);
    printw("  %s", GROKIUM_VERSION);
    attroff(A_DIM);
    if (always_approve || cfg.agent_always_approve) {
      attron(COLOR_PAIR(CP_OK));
      printw("  yolo");
      attroff(COLOR_PAIR(CP_OK));
    }
    if (cfg.ui_multiline) {
      attron(A_DIM | COLOR_PAIR(CP_OK));
      printw("  ml");
      attroff(A_DIM | COLOR_PAIR(CP_OK));
    }
    if (cfg.agent_braincells) {
      attron(A_DIM | COLOR_PAIR(CP_THINK));
      printw("  hive");
      attroff(A_DIM | COLOR_PAIR(CP_THINK));
    }
    int ns = count_spoilers();
    if (cfg.ui_show_spoiler_count && ns > 0) {
      attron(A_DIM | COLOR_PAIR(CP_THINK));
      printw("  [%d spoiler%s]", ns, ns == 1 ? "" : "s");
      attroff(A_DIM | COLOR_PAIR(CP_THINK));
    }
    char sm[48];
    short_model(sm, sizeof sm);
    attron(A_DIM);
    mvprintw(1, 0, " %s · %s · ctx %d · %s", cfg.active_backend, sm,
             cfg.context_window, cfg.ui_theme);
    if (cfg.ui_show_status_hints && cols > 50) {
      const char *hint = cfg.ui_send_hint[0] ? cfg.ui_send_hint :
        (cfg.ui_multiline ? "Enter=nl · Alt+Enter send" : "Enter=send");
      int hl = (int)strlen(hint);
      if (hl < cols - 2)
        mvprintw(1, cols - hl - 1, "%s", hint);
    }
    attroff(A_DIM);
    mvhline(2, 0, ACS_HLINE, cols > 0 ? cols : 1);
  }

  int comp_rows = input_line_count();
  int sep_y = rows - 1 - comp_rows; /* separator above composer */
  if (sep_y < 3) sep_y = 3;

  int log_h = sep_y - 3;
  if (log_h < 2) log_h = 2;
  int start = vrow_n - log_h - scroll_off;
  if (start < 0) start = 0;
  int y = 3;
  for (int i = start; i < vrow_n && y < sep_y; i++, y++) {
    int bi = vrows[i].blk;
    int kind = (bi >= 0 && bi < blk_n) ? blks[bi].kind : BK_META;
    if (kind == BK_USER)
      attron(A_BOLD | COLOR_PAIR(CP_USER));
    else if (kind == BK_ASST)
      attron(A_BOLD | COLOR_PAIR(CP_ASST));
    else if (kind == BK_THINK)
      attron(A_DIM | COLOR_PAIR(CP_THINK));
    else if (kind == BK_TOOL)
      attron(A_DIM | COLOR_PAIR(CP_TOOL));
    else
      attron(A_DIM);
    if (vrows[i].is_header && focus_sp == bi) attron(A_REVERSE);
    mvaddnstr(y, 0, vrows[i].text ? vrows[i].text : "", cols > 1 ? cols - 1 : 1);
    attroff(A_BOLD | A_DIM | A_REVERSE | COLOR_PAIR(CP_USER) | COLOR_PAIR(CP_ASST) | COLOR_PAIR(CP_TOOL) |
            COLOR_PAIR(CP_THINK));
  }

  attron(A_DIM);
  mvhline(sep_y, 0, ACS_HLINE, cols > 0 ? cols : 1);
  attroff(A_DIM);

  /* Multi-line composer */
  int cr, cc;
  input_cursor_rc(&cr, &cc);
  /* If more lines than composer_max_rows(), window so cursor row is visible */
  int view0 = 0;
  if (cr >= composer_max_rows()) view0 = cr - composer_max_rows() + 1;

  {
    int line_i = 0, pos = 0;
    while (line_i < view0 && pos < in_len) {
      if (input[pos] == '\n') line_i++;
      pos++;
    }
    for (int row = 0; row < comp_rows; row++) {
      int yy = sep_y + 1 + row;
      if (yy >= rows) break;
      move(yy, 0);
      clrtoeol();
      if (row == 0) {
        attron(A_BOLD | COLOR_PAIR(CP_ASST));
        mvaddstr(yy, 0, " > ");
        attroff(A_BOLD | COLOR_PAIR(CP_ASST));
      } else {
        attron(A_DIM);
        mvaddstr(yy, 0, " | ");
        attroff(A_DIM);
      }
      int col0 = 3;
      int start_pos = pos;
      while (pos < in_len && input[pos] != '\n') pos++;
      int frag = pos - start_pos;
      int maxw = cols > col0 + 1 ? cols - col0 - 1 : 1;
      if (frag > maxw) frag = maxw;
      if (frag > 0) mvaddnstr(yy, col0, input + start_pos, frag);
      if (pos < in_len && input[pos] == '\n') pos++; /* skip nl */
    }
  }

  /* place cursor */
  {
    int vis_r = cr - view0;
    if (vis_r < 0) vis_r = 0;
    if (vis_r >= comp_rows) vis_r = comp_rows - 1;
    int yy = sep_y + 1 + vis_r;
    int xx = 3 + cc;
    if (xx >= cols) xx = cols - 1;
    if (yy >= rows) yy = rows - 1;
    move(yy, xx);
  }
  refresh();
}

static void maybe_version_watch(void) {
  if (!cfg.auto_version_watch) return;
  time_t now = time(NULL);
  if (last_ver_check && (now - last_ver_check) < cfg.version_watch_interval_sec) return;
  last_ver_check = now;
  char prev[64];
  snprintf(prev, sizeof prev, "%s", ver_st.last_seen);
  gkx_version_refresh(&ver_st, root);
  if (ver_st.changed && prev[0] && strcmp(prev, "none") != 0) {
    endwin();
    if (g_argv && g_argc > 0) gkx_version_maybe_restart(&ver_st, g_argc, g_argv);
  }
}

/* Map mouse y → block toggle if header */
static void mouse_click_y(int my) {
  int rows, cols;
  getmaxyx(stdscr, rows, cols);
  (void)cols;
  int comp_rows = input_line_count();
  int sep_y = rows - 1 - comp_rows;
  if (sep_y < 3) sep_y = 3;
  int log_h = sep_y - 3;
  if (log_h < 2) log_h = 2;
  int start = vrow_n - log_h - scroll_off;
  if (start < 0) start = 0;
  if (my < 3 || my >= sep_y) return;
  int vi = start + (my - 3);
  if (vi < 0 || vi >= vrow_n) return;
  if (vrows[vi].is_header) toggle_spoiler(vrows[vi].blk);
}

int grokium_tui(int argc, char **argv) {
  g_argc = argc;
  g_argv = argv;
  resolve_paths();
  mkdir(state_dir, 0755);
  setenv("CUBALC_ASCII", "1", 1);

  gkx_config_load(&cfg, NULL);
  gkx_config_apply_env(&cfg);
  gkx_config_load_prefs(&cfg, state_dir);
  if (!cfg.active_model[0])
    snprintf(cfg.active_model, sizeof cfg.active_model, "%s", cfg.local_model);
  if (!cfg.active_backend[0])
    snprintf(cfg.active_backend, sizeof cfg.active_backend, "local");

  gkx_version_init(&ver_st, root);
  gkx_version_refresh(&ver_st, root);
  last_ver_check = time(NULL);

  ng_log_set_stderr(0);
  ng_log_init(NULL);
  gkx_hub_apply_sched_env(&cfg);
  {
    char slots[8];
    snprintf(slots, sizeof slots, "%d", cfg.llm_slots > 0 ? cfg.llm_slots : 1);
    setenv("NANOBOT_LLM_SLOTS", slots, 1);
    ng_llm_sched_set_enabled(1);
    ng_llm_sched_set_slots(cfg.llm_slots > 0 ? cfg.llm_slots : 1);
  }
  if (cfg.hub_enabled) gkx_hub_ensure(&cfg);

  always_approve = cfg.agent_always_approve;

  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  if (cfg.ui_mouse) {
    mousemask(ALL_MOUSE_EVENTS, NULL);
    mouseinterval(0);
  }
  ui_apply_colors();
  curs_set(1);

  {
    char plate[640];
    /* Dual-wire ready plate — no free-text welcome/send-hint dump. */
    gkx_ready_json(cfg.hub_enabled, cfg.agent_tools, cfg.ui_multiline, plate,
                   sizeof plate);
    log_add(plate);
  }

  input[0] = 0;
  in_len = in_cur = 0;
  pending_esc = 0;
  for (;;) {
    maybe_version_watch();
    draw();
    int ch = getch();
    if (ch == KEY_RESIZE) continue;
    if (ch == KEY_MOUSE) {
      MEVENT ev;
      if (getmouse(&ev) == OK && (ev.bstate & BUTTON1_CLICKED)) {
        rebuild_vrows();
        mouse_click_y(ev.y);
      }
      continue;
    }
    if (ch == 3) { /* Ctrl+C clear input or interrupt note */
      if (in_len > 0) {
        in_len = in_cur = 0;
        input[0] = 0;
      } else {
        char plate[512];
        /* Dual-wire interrupt plate — no free-text (interrupt) banner. */
        gkx_interrupt_json(plate, sizeof plate);
        log_add(plate);
      }
      continue;
    }

    /* Alt+Enter: ESC then Enter (common terminal sequence) */
    if (ch == 27) {
      pending_esc = 1;
      /* short wait for the next key of Alt-combo */
      timeout(40);
      int ch2 = getch();
      timeout(-1);
      pending_esc = 0;
      if (ch2 == ERR) continue;
      if (ch2 == '\n' || ch2 == '\r' || ch2 == KEY_ENTER) {
        /* Alt+Enter */
        if (cfg.ui_multiline)
          submit_input();
        else
          input_insert('\n');
        continue;
      }
      /* other Alt keys ignored */
      ch = ch2;
    }

    /* Ctrl+S = send (works in both modes) */
    if (ch == 19) { /* ^S */
      submit_input();
      continue;
    }
    /* Ctrl+J = newline when not multiline? both: force newline */
    if (ch == 10 && 0) { /* unused — Enter handling below */
    }

    /* Spoiler keys only when composer empty */
    if (in_len == 0) {
      if (ch == '\t') {
        focus_next_spoiler(1);
        continue;
      }
      if (ch == KEY_BTAB) {
        focus_next_spoiler(-1);
        continue;
      }
      if (ch == 'e') {
        int ix = focus_sp >= 0 ? focus_sp : last_spoiler();
        toggle_spoiler(ix);
        continue;
      }
      if (ch == 'E') {
        spoilers_set_all(1);
        continue;
      }
      if (ch == 'c' || ch == 'C') {
        spoilers_set_all(0);
        continue;
      }
    }

    if (ch == KEY_ENTER || ch == '\n' || ch == '\r') {
      if (cfg.ui_multiline) {
        /* Enter = newline; empty Enter on focus = spoiler */
        if (in_len == 0 && focus_sp >= 0)
          toggle_spoiler(focus_sp);
        else
          input_insert('\n');
      } else {
        submit_input();
      }
      continue;
    }
    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      input_backspace();
      continue;
    }
    if (ch == KEY_DC) {
      input_delete();
      continue;
    }
    if (ch == KEY_LEFT) {
      if (in_cur > 0) in_cur--;
      continue;
    }
    if (ch == KEY_RIGHT) {
      if (in_cur < in_len) in_cur++;
      continue;
    }
    if (ch == KEY_HOME) {
      /* start of current line */
      while (in_cur > 0 && input[in_cur - 1] != '\n') in_cur--;
      continue;
    }
    if (ch == KEY_END) {
      while (in_cur < in_len && input[in_cur] != '\n') in_cur++;
      continue;
    }
    if (ch == KEY_PPAGE) {
      scroll_off += cfg.ui_scroll_step > 0 ? cfg.ui_scroll_step : 5;
      continue;
    }
    if (ch == KEY_NPAGE) {
      if (scroll_off > 0) scroll_off -= cfg.ui_scroll_step > 0 ? cfg.ui_scroll_step : 5;
      continue;
    }
    if (ch == KEY_UP) {
      /* move cursor up a line in composer if multi-line, else scroll */
      int r, c;
      input_cursor_rc(&r, &c);
      if (r > 0) {
        /* walk to previous line, same column */
        int i = in_cur;
        while (i > 0 && input[i - 1] != '\n') i--;
        if (i > 0) i--; /* skip nl */
        int line_start = i;
        while (line_start > 0 && input[line_start - 1] != '\n') line_start--;
        int col = 0;
        in_cur = line_start;
        while (col < c && in_cur < i) {
          in_cur++;
          col++;
        }
      } else
        scroll_off++;
      continue;
    }
    if (ch == KEY_DOWN) {
      int r, c;
      input_cursor_rc(&r, &c);
      int lines = 1;
      for (int i = 0; i < in_len; i++)
        if (input[i] == '\n') lines++;
      if (r + 1 < lines) {
        int i = in_cur;
        while (i < in_len && input[i] != '\n') i++;
        if (i < in_len) i++; /* past nl */
        int col = 0;
        in_cur = i;
        while (col < c && in_cur < in_len && input[in_cur] != '\n') {
          in_cur++;
          col++;
        }
      } else if (scroll_off > 0)
        scroll_off--;
      continue;
    }
    if (ch >= 32 && ch < 127) {
      input_insert((char)ch);
    }
  }
  endwin();
  blk_free_all();
  return 0;
}
