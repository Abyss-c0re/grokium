#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include "grokium.h"
#include "grokium_chat.h"
#include "grokium_config.h"
#include "grokium_version.h"
#include "grokium_hub.h"
#include "grokium_session.h"
#include "grokium_status.h"
#include "grokium_law.h"
#include "grokium_llama.h"
#include "grokium_consolidator.h"
#include "grokium_smx_filter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>

/* Grokium = nanobot core (agent) + C host (TUI) + optional CubalC board. */

char root[PATH_MAX];
char cubalc_bin[PATH_MAX];
char state_dir[PATH_MAX];
char prog_dir[PATH_MAX];
char nanobot_root[PATH_MAX];

static void die(const char *m) {
  fprintf(stderr, "grokium: %s\n", m);
  exit(2);
}

/* Short machine error token for dual-wire plates (no free-text / path inject). */
static void err_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in || !in[0]) return;
  for (i = 0; in[i] && o + 1 < cap && o < 48; i++) {
    unsigned char c = (unsigned char)in[i];
    if (isalnum(c) || c == '_' || c == '-')
      out[o++] = (char)c;
    else if (c == ' ' || c == ':' || c == '/' || c == '.')
      out[o++] = '_';
  }
  out[o] = 0;
}

int file_ok(const char *p) {
  struct stat st;
  return p && p[0] && stat(p, &st) == 0 && S_ISREG(st.st_mode) && access(p, X_OK) == 0;
}

void resolve_paths(void) {
  const char *e;
  e = getenv("GROKIUM_ROOT");
  if (e && e[0]) {
    snprintf(root, sizeof root, "%s", e);
  } else {
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n > 0) {
      self[n] = 0;
      char *slash = strrchr(self, '/');
      if (slash) *slash = 0; /* out */
      slash = strrchr(self, '/');
      if (slash) *slash = 0; /* host */
      slash = strrchr(self, '/');
      if (slash) *slash = 0; /* grokium */
      snprintf(root, sizeof root, "%s", self);
    } else {
      snprintf(root, sizeof root, ".");
    }
  }
  e = getenv("CUBALC_BIN");
  if (e && e[0]) {
    snprintf(cubalc_bin, sizeof cubalc_bin, "%s", e);
  } else {
    char tryb[PATH_MAX];
    snprintf(tryb, sizeof tryb, "%s/deps/cubalc/out/cubalc", root);
    if (access(tryb, X_OK) == 0)
      snprintf(cubalc_bin, sizeof cubalc_bin, "%s", tryb);
    else {
      e = getenv("CUBALC_ROOT");
      if (e && e[0]) {
        snprintf(tryb, sizeof tryb, "%s/out/cubalc", e);
        if (access(tryb, X_OK) == 0)
          snprintf(cubalc_bin, sizeof cubalc_bin, "%s", tryb);
        else
          snprintf(cubalc_bin, sizeof cubalc_bin, "cubalc");
      } else
        snprintf(cubalc_bin, sizeof cubalc_bin, "cubalc");
    }
  }
  e = getenv("NANOBOT_ROOT");
  if (e && e[0])
    snprintf(nanobot_root, sizeof nanobot_root, "%s", e);
  else {
    snprintf(nanobot_root, sizeof nanobot_root, "%s/deps/nanobot", root);
    if (access(nanobot_root, F_OK) != 0)
      snprintf(nanobot_root, sizeof nanobot_root, "%s/Dev/AI/nanobot",
               getenv("HOME") ? getenv("HOME") : "");
  }
  snprintf(state_dir, sizeof state_dir, "%s/data/cubalc", root);
  snprintf(prog_dir, sizeof prog_dir, "%s/cubalc/programs", root);
  {
    char data[PATH_MAX];
    mkdir(root, 0755);
    snprintf(data, sizeof data, "%s/data", root);
    mkdir(data, 0755);
    mkdir(state_dir, 0755);
  }
}

static int run_cubalc_program(const char *name, const char *plate) {
  char prog[PATH_MAX], tmp[PATH_MAX];
  snprintf(prog, sizeof prog, "%s/%s", prog_dir, name);
  if (access(prog, R_OK) != 0) {
    /* Machine missing_program — dual-wire (no free-text path echo). */
    printf("{\"schema\":\"grokium.cubalc.v1\",\"ok\":false,"
           "\"error\":\"missing_program\",\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false,"
           "\"hint\":\"cubalc/programs/<name.cubalc>\"}\n");
    return 2;
  }
  if (!file_ok(cubalc_bin)) {
    printf("{\"schema\":\"grokium.cubalc.v1\",\"ok\":false,"
           "\"error\":\"missing_binary\",\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false,"
           "\"hint\":\"./scripts/sync_cubalc.sh && make -C deps/cubalc all\"}\n");
    return 99;
  }

  char *run_path = prog;
  if (plate && plate[0]) {
    FILE *in = fopen(prog, "r");
    if (!in) return 2;
    snprintf(tmp, sizeof tmp, "%s/_run.cubalc", state_dir);
    FILE *out = fopen(tmp, "w");
    if (!out) {
      fclose(in);
      return 2;
    }
    fprintf(out, "[hold]\n[genesis \"%s\"]\n", plate);
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    run_path = tmp;
  }

  pid_t pid = fork();
  if (pid < 0) die("fork");
  if (pid == 0) {
    setenv("CUBALC_STATE", state_dir, 1);
    setenv("GROKIUM_ROOT", root, 1);
    execl(cubalc_bin, "cubalc", "run", run_path, (char *)NULL);
    fprintf(stderr, "grokium: exec cubalc: %s\n", strerror(errno));
    _exit(127);
  }
  int st = 0;
  if (waitpid(pid, &st, 0) < 0) die("waitpid");
  if (WIFEXITED(st)) return WEXITSTATUS(st);
  return 1;
}

static int run_cubalc_args(char *const argv[]) {
  if (!file_ok(cubalc_bin)) {
    printf("{\"schema\":\"grokium.cubalc.v1\",\"ok\":false,"
           "\"error\":\"missing_binary\",\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false,"
           "\"hint\":\"./scripts/sync_cubalc.sh && make -C deps/cubalc all\"}\n");
    return 99;
  }
  pid_t pid = fork();
  if (pid < 0) die("fork");
  if (pid == 0) {
    setenv("CUBALC_STATE", state_dir, 1);
    setenv("GROKIUM_ROOT", root, 1);
    execv(cubalc_bin, argv);
    _exit(127);
  }
  int st = 0;
  waitpid(pid, &st, 0);
  return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

/* c_core tools (fleet / serve / SMX filter) — pure C Hive Mind path */
static int resolve_c_core_bin(const char *name, char *out, size_t n) {
  if (!name || !out || n < 8) return -1;
  snprintf(out, n, "%s/build/%s", root, name);
  if (access(out, X_OK) == 0) return 0;
  return -1;
}

static int run_c_core(const char *name, int argc, char **argv) {
  char bin[PATH_MAX];
  char *av[64];
  int i, n = 0;
  pid_t pid;
  int st = 0;
  if (resolve_c_core_bin(name, bin, sizeof bin) != 0) {
    /* Machine missing_c_core — dual-wire honesty (no free-text path). */
    printf("{\"schema\":\"grokium.tool.v1\",\"ok\":false,"
           "\"error\":\"missing_c_core\",\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false,"
           "\"hint\":\"make -C c_core all\"}\n");
    return 99;
  }
  if (argc + 1 >= (int)(sizeof av / sizeof av[0])) {
    printf("{\"schema\":\"grokium.tool.v1\",\"ok\":false,"
           "\"error\":\"too_many_args\",\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false}\n");
    return 2;
  }
  av[n++] = bin;
  for (i = 0; i < argc && n < 63; i++)
    av[n++] = argv[i];
  av[n] = NULL;
  pid = fork();
  if (pid < 0) die("fork");
  if (pid == 0) {
    char cdir[PATH_MAX];
    setenv("GROKIUM_ROOT", root, 1);
    snprintf(cdir, sizeof cdir, "%s/data/contracts", root);
    setenv("GROKIUM_CONTRACT_DIR", cdir, 0); /* keep caller override */
    if (chdir(root) != 0) {
      /* still try; relative data/home paths prefer repo root */
    }
    execv(bin, av);
    fprintf(stderr, "grokium: exec %s: %s\n", bin, strerror(errno));
    _exit(127);
  }
  if (waitpid(pid, &st, 0) < 0) die("waitpid");
  return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

static void usage(void) {
  fprintf(stderr,
          "grokium %s  core=nanobot host=C py=0 tok=%s\n"
          "  (default)              fullscreen TUI\n"
          "  -p|--single PROMPT     one-shot chat (stdout)\n"
          "  --model ID             model id or auto\n"
          "  --backend local|grok   default local (llama.cpp)\n"
          "  models                 list live models\n"
          "  chat <text>            non-TUI chat (streamed)\n"
          "  hub [start|stop|status]  LLM request hub (nanobot peer)\n"
          "  compat                 refresh official CLI version track\n"
          "  serve [port]           loopback control plane (c_core, :17444)\n"
          "  fleet [defaults|deploy|status|…]  c_core plate (or fleet cubalc)\n"
          "  filter <allow-check|…> SMX / NEXUS_COORD sanitize gate\n"
          "  coord|ingest <plate>   fold SMX/NEXUS_COORD via filter (fail-closed)\n"
          "  contract form|validate|…  external cell contracts (c_core)\n"
          "  manager-tick [DIR]     motivate incomplete contracts\n"
          "  commander show|sign|verify|…  Ed25519 law (≠ model)\n"
          "  sessions [q]           import session metas only (no transcripts)\n"
          "  pickup|load <id>       session meta pickup (resume msgs = host)\n"
          "  law [cubalc]           Cube Standards plate (pure C; cubalc opt-in)\n"
          "  license                Apache-2.0 / not xAI dual-wire plate (pure C)\n"
          "  status [cubalc|board]  dual-wire honesty plate (pure C; cubalc opt-in)\n"
          "  llama|llama-probe      local llama.cpp reachability\n"
          "  integrity tick|policy|reseal  code seal / privacy fail-closed\n"
          "  board|selftest         CubalC board (optional)\n"
          "  product_wire=smx2  peer_http=lab_ops_only  Not affiliated with xAI.\n",
          GROKIUM_VERSION, GROKIUM_TOK);
}

/* Pure-C law plate — matches c_core grokium_law_default + dual-wire honesty.
 * Commander is Ed25519 under data/law; LLM is never commander. */
static int cmd_law(int argc, char **argv) {
  const char *sub = (argc >= 1) ? argv[0] : "";
  if (sub[0] && (!strcmp(sub, "cubalc") || !strcmp(sub, "board")))
    return run_cubalc_program("law.cubalc", NULL);
  if (sub[0] && (!strcmp(sub, "help") || !strcmp(sub, "-h") ||
                 !strcmp(sub, "--help"))) {
    /* Machine help plate — dual-wire honesty (no free-text-only usage). */
    printf("{\"schema\":\"grokium.law.v1\",\"ok\":false,"
           "\"error\":\"need_run_or_cubalc\",\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"commander\":\"ed25519\",\"llm_is_commander\":false,"
           "\"python\":0,\"host\":\"C\",\"product\":\"grokium\","
           "\"not\":\"grok_model\","
           "\"hint\":\"law [cubalc] · pure-C Cube Standards plate\"}\n");
    return 0;
  }
  {
    char plate[768];
    /* Shared c_core dual-wire plate (match GET /v1/law). */
    grokium_law_json(NULL, plate, sizeof plate);
    printf("%s\n", plate);
  }
  return 0;
}

/* Dual-wire honesty status — shared probes; matches c_core /v1/status shape. */
static int cmd_status(int argc, char **argv) {
  const char *sub = (argc >= 1) ? argv[0] : "";
  char plate[512];
  if (sub[0] && (!strcmp(sub, "cubalc") || !strcmp(sub, "board")))
    return run_cubalc_program("board.cubalc", NULL);
  if (sub[0] && (!strcmp(sub, "help") || !strcmp(sub, "-h") ||
                 !strcmp(sub, "--help"))) {
    /* Machine help plate — dual-wire honesty (no free-text-only usage). */
    printf("{\"schema\":\"grokium.status.v1\",\"ok\":false,"
           "\"error\":\"need_run_or_cubalc\",\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false,\"python\":0,\"host\":\"C\","
           "\"hint\":\"status [cubalc|board] · pure-C dual-wire plate\"}\n");
    return 0;
  }
  if (gkx_status_plate_json(root, "host_cli", plate, sizeof plate) != 0) {
    /* Machine plate_failed — dual-wire honesty (no free-text-only). */
    printf("{\"schema\":\"grokium.status.v1\",\"ok\":false,"
           "\"error\":\"plate_failed\",\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false,\"python\":0,\"host\":\"C\"}\n");
    return 1;
  }
  printf("%s\n", plate);
  return 0;
}

/* Imported Grok Build metas only — shared c_core plate (no transcripts). */
static int cmd_session_pickup(const char *id) {
  char data_root[PATH_MAX], plate[2048];
  int rc;
  /* Dual-wire pickup plate from c_core (deny fills plate on error). */
  snprintf(data_root, sizeof data_root, "%s/data", root);
  rc = gk_session_pickup_json(data_root, id, plate, sizeof plate);
  printf("%s\n", plate);
  return rc == 0 ? 0 : 1;
}

static int cmd_sessions(int argc, char **argv) {
  char data_root[PATH_MAX], plate[8192];
  const char *q = "";

  if (argc >= 1 &&
      (!strcmp(argv[0], "pickup") || !strcmp(argv[0], "load") ||
       !strcmp(argv[0], "get"))) {
    if (argc < 2) {
      char deny[768];
      if (gk_session_pickup_deny_json(NULL, "need_session_id", deny,
                                       sizeof deny) == 0)
        printf("%s\n", deny);
      return 2;
    }
    return cmd_session_pickup(argv[1]);
  }
  if (argc >= 1 &&
      (!strcmp(argv[0], "help") || !strcmp(argv[0], "-h") ||
       !strcmp(argv[0], "--help"))) {
    /* Machine help plate — dual-wire honesty (no free-text-only usage). */
    printf("{\"schema\":\"grokium.sessions.v1\",\"ok\":false,"
           "\"error\":\"need_query_or_pickup\",\"content\":\"meta_only\","
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false,\"telemetry\":\"off\","
           "\"hint\":\"sessions [q] · sessions pickup|load <id> · no "
           "transcripts\"}\n");
    return 0;
  }
  if (argc >= 1) q = argv[0];

  /* Shared c_core list plate (meta only; import under {root}/data/import). */
  snprintf(data_root, sizeof data_root, "%s/data", root);
  gk_session_list_json(data_root, q, plate, sizeof plate);
  printf("%s\n", plate);
  return 0;
}

static int cmd_models(gkx_config *cfg) {
  char err[400], tok[64];
  char *body = grokium_models_json(cfg, err, sizeof err);
  if (!body) {
    /* Dual-wire fail plate — sanitize free-text backend err to machine token. */
    err_token(err, tok, sizeof tok);
    if (!tok[0]) snprintf(tok, sizeof tok, "models_fail");
    printf("{\"schema\":\"grokium.models.v1\",\"ok\":false,"
           "\"error\":\"%s\",\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false,"
           "\"hint\":\"models · local llama /v1/models\"}\n",
           tok);
    return 2;
  }
  printf("%s\n", body);
  free(body);
  return 0;
}

/* stream helper for -p — text only (tools/thinking go to spoilers in TUI) */
static void stdout_delta(void *ud, const char *chunk, size_t n) {
  (void)ud;
  if (!chunk || !n) return;
  if ((unsigned char)chunk[0] == 0x1e) return; /* structured tool/think event */
  if (strstr(chunk, "\"type\":\"thinking\"") || strstr(chunk, "\"type\":\"tool\""))
    return;
  fwrite(chunk, 1, n, stdout);
  fflush(stdout);
}

static int cmd_prompt(gkx_config *cfg, const char *msg) {
  char reply[16384], err[400];
  reply[0] = err[0] = 0;
  int rc = grokium_chat_request_ex(cfg, msg, stdout_delta, NULL, reply,
                                   sizeof reply, err, sizeof err);
  if (rc == 0) {
    fputc('\n', stdout);
    return 0;
  }
  /* fallback non-stream */
  rc = grokium_chat_request(cfg->active_backend, cfg->active_model, msg, state_dir,
                            reply, sizeof reply, err, sizeof err);
  if (rc == 0 && reply[0]) {
    printf("%s\n", reply);
    return 0;
  }
  {
    char tok[64], plate[512];
    /* Shared dual-wire fail plate — sanitize free-text err (LLM ≠ commander). */
    err_token(err, tok, sizeof tok);
    if (!tok[0]) snprintf(tok, sizeof tok, "chat_fail");
    grokium_chat_err_json(tok, "chat · check local llama / hub", plate,
                          sizeof plate);
    printf("%s\n", plate);
  }
  return 2;
}

int main(int argc, char **argv) {
  resolve_paths();
  setenv("GROKIUM_ROOT", root, 0);

  gkx_config cfg;
  gkx_config_load(&cfg, NULL);
  gkx_config_apply_env(&cfg);
  gkx_config_load_prefs(&cfg, state_dir);

  /* global flags */
  const char *single = NULL;
  int ai = 1;
  while (ai < argc) {
    if (strcmp(argv[ai], "-p") == 0 || strcmp(argv[ai], "--single") == 0) {
      if (ai + 1 >= argc) {
        char plate[512];
        /* Shared dual-wire need_message (HTTP / serve CLI same builder). */
        grokium_chat_err_json("need_message", "-p|--single <prompt>", plate,
                              sizeof plate);
        printf("%s\n", plate);
        return 2;
      }
      single = argv[++ai];
      ai++;
      continue;
    }
    if (strcmp(argv[ai], "-m") == 0 || strcmp(argv[ai], "--model") == 0) {
      if (ai + 1 >= argc) return 2;
      snprintf(cfg.active_model, sizeof cfg.active_model, "%s", argv[++ai]);
      ai++;
      continue;
    }
    if (strcmp(argv[ai], "--backend") == 0) {
      if (ai + 1 >= argc) return 2;
      snprintf(cfg.active_backend, sizeof cfg.active_backend, "%s", argv[++ai]);
      ai++;
      continue;
    }
    if (strcmp(argv[ai], "--help") == 0 || strcmp(argv[ai], "-h") == 0) {
      usage();
      return 0;
    }
    if (strcmp(argv[ai], "--version") == 0) {
      printf("grokium %s (nanobot core)\n", GROKIUM_VERSION);
      return 0;
    }
    break;
  }

  gkx_hub_apply_sched_env(&cfg);
  {
    char slots[8];
    snprintf(slots, sizeof slots, "%d", cfg.llm_slots > 0 ? cfg.llm_slots : 1);
    setenv("NANOBOT_LLM_SLOTS", slots, 1);
  }

  if (single) {
    if (cfg.hub_enabled) gkx_hub_ensure(&cfg);
    return cmd_prompt(&cfg, single);
  }

  const char *cmd = ai < argc ? argv[ai] : "tui";

  if (strcmp(cmd, "help") == 0) {
    usage();
    return 0;
  }
  if (strcmp(cmd, "version") == 0) {
    /* Machine version plate — dual-wire honesty (lab/ops ≠ product bus). */
    printf("{\"schema\":\"grokium.version.v1\",\"ok\":true,"
           "\"product\":\"grokium\",\"version\":\"%s\",\"core\":\"nanobot\","
           "\"host\":\"C\",\"python\":0,\"tok\":\"%s\","
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false}\n",
           GROKIUM_VERSION, GROKIUM_TOK);
    return 0;
  }
  if (strcmp(cmd, "models") == 0) {
    return cmd_models(&cfg);
  }
  if (strcmp(cmd, "compat") == 0) {
    gkx_version_state st;
    char official[64];
    size_t i, o = 0;
    int rc;
    gkx_version_init(&st, root);
    rc = gkx_version_refresh(&st, root);
    /* Sanitize version token for JSON (no free-text / inject). */
    for (i = 0; st.official[i] && o + 1 < sizeof official; i++) {
      unsigned char c = (unsigned char)st.official[i];
      if (isalnum(c) || c == '.' || c == '-' || c == '_')
        official[o++] = (char)c;
    }
    official[o] = 0;
    if (!official[0]) snprintf(official, sizeof official, "none");
    /* Match on-disk grok_build_compat dual-wire plate (CLI version ≠ product). */
    printf("{\"schema\":\"grokium.version_compat.v1\",\"ok\":%s,"
           "\"reported_grok_build_version\":\"%s\","
           "\"grokium_version\":\"%s\",\"changed\":%s,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false,\"model_is_not_commander\":true}\n",
           rc == 0 ? "true" : "false", official, GROKIUM_VERSION,
           st.changed ? "true" : "false");
    return rc == 0 ? 0 : 1;
  }
  if (strcmp(cmd, "hub") == 0) {
    const char *sub = (ai + 1 < argc) ? argv[ai + 1] : "status";
    if (strcmp(sub, "start") == 0 || strcmp(sub, "ensure") == 0) {
      int rc = gkx_hub_ensure(&cfg);
      char buf[640];
      gkx_hub_status(buf, sizeof buf);
      printf("%s\n", buf);
      return rc == 0 ? 0 : 1;
    }
    if (strcmp(sub, "stop") == 0) {
      gkx_hub_stop();
      printf("{\"schema\":\"grokium.hub_status.v1\",\"ok\":true,"
             "\"stopped\":true,\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false}\n");
      return 0;
    }
    if (strcmp(sub, "status") == 0 || strcmp(sub, "show") == 0 ||
        strcmp(sub, "help") == 0 || strcmp(sub, "?") == 0) {
      char buf[640];
      int rc = gkx_hub_status(buf, sizeof buf);
      printf("%s\n", buf);
      return rc;
    }
    /* Machine need_subcmd — dual-wire honesty (match TUI /hub path). */
    printf("{\"schema\":\"grokium.hub.v1\",\"ok\":false,"
           "\"error\":\"need_subcmd\",\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false,"
           "\"hint\":\"hub [start|stop|status]\"}\n");
    return 2;
  }
  if (strcmp(cmd, "board") == 0)
    return run_cubalc_program("board.cubalc", NULL);
  if (strcmp(cmd, "status") == 0)
    return cmd_status(argc - ai - 1, argv + ai + 1);
  if (strcmp(cmd, "law") == 0 || strcmp(cmd, "laws") == 0)
    return cmd_law(argc - ai - 1, argv + ai + 1);
  if (strcmp(cmd, "license") == 0 || strcmp(cmd, "licence") == 0) {
    char plate[512];
    /* Shared c_core dual-wire plate (match GET /v1/license). */
    grokium_license_json(plate, sizeof plate);
    printf("%s\n", plate);
    return 0;
  }
  /* Hive Mind pure-C surface (product bus = SMX2; HTTP loopback = lab/ops) */
  if (strcmp(cmd, "serve") == 0) {
    return run_c_core("grokium-serve", argc - ai - 1, argv + ai + 1);
  }
  if (strcmp(cmd, "filter") == 0 || strcmp(cmd, "smx-filter") == 0) {
    return run_c_core("grokium-smx-filter", argc - ai - 1, argv + ai + 1);
  }
  /* External coord/ingest → host-local SMX filter then consolidator. */
  if (strcmp(cmd, "coord") == 0 || strcmp(cmd, "ingest") == 0 ||
      strcmp(cmd, "smx-ingest") == 0) {
    int i = ai + 1;
    char *av[3];
    char plate[4096], deny[512];
    size_t o = 0;
    grokium_law L;
    if (i >= argc) {
      /* Shared need_plate with POST /v1/coord · TUI /coord · consolidate CLI. */
      gk_coord_err_json("need_plate", deny, sizeof deny);
      printf("%s\n", deny);
      return 2;
    }
    plate[0] = 0;
    for (; i < argc; i++) {
      size_t n = strlen(argv[i]);
      if (o && o + 1 < sizeof plate) plate[o++] = ' ';
      if (o + n >= sizeof plate) break;
      memcpy(plate + o, argv[i], n);
      o += n;
      plate[o] = 0;
    }
    /* Host-local sanitize gate — deny prose before consolidate exec. */
    grokium_law_default(&L);
    if (!grokium_smx_filter_allow_frame(&L, (const uint8_t *)plate, o, 1)) {
      /* Shared smx_filter_deny dual-wire plate (same as TUI / HTTP). */
      gk_coord_err_json("smx_filter_deny", deny, sizeof deny);
      printf("%s\n", deny);
      return 1;
    }
    av[0] = "ingest";
    av[1] = plate;
    av[2] = NULL;
    return run_c_core("grokium-consolidate", 2, av);
  }
  if (strcmp(cmd, "contract") == 0) {
    if (ai + 1 >= argc) {
      /* Machine need_subcmd plate (match filter dual-wire honesty). */
      printf("{\"schema\":\"grokium.contract.v1\",\"ok\":false,"
             "\"error\":\"need_subcmd\",\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false,"
             "\"hint\":\"form|validate|manager-tick\"}\n");
      return 2;
    }
    return run_c_core("grokium-smx-filter", argc - ai - 1, argv + ai + 1);
  }
  if (strcmp(cmd, "manager") == 0 || strcmp(cmd, "manager-tick") == 0) {
    if (strcmp(cmd, "manager-tick") == 0 ||
        (ai + 1 < argc && strcmp(argv[ai + 1], "tick") == 0)) {
      /* grokium manager-tick [DIR]  or  grokium manager tick [DIR] */
      int off = strcmp(cmd, "manager-tick") == 0 ? 1 : 2;
      char *av[4];
      int n = 0;
      av[n++] = "manager-tick";
      if (ai + off < argc) av[n++] = argv[ai + off];
      return run_c_core("grokium-smx-filter", n, av);
    }
    if (ai + 1 >= argc) {
      char *av[] = {"manager-tick"};
      return run_c_core("grokium-smx-filter", 1, av);
    }
    return run_c_core("grokium-smx-filter", argc - ai - 1, argv + ai + 1);
  }
  if (strcmp(cmd, "commander") == 0) {
    if (ai + 1 >= argc ||
        !strcmp(argv[ai + 1], "help") || !strcmp(argv[ai + 1], "-h") ||
        !strcmp(argv[ai + 1], "--help")) {
      /* Machine need_subcmd plate — dual-wire honesty (Commander ≠ model). */
      printf("{\"schema\":\"grokium.commander.v1\",\"ok\":false,"
             "\"error\":\"need_subcmd\",\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"commander\":\"ed25519\",\"not\":\"grok_model\","
             "\"hint\":\"keygen|show|sign|verify|install-law --law-dir "
             "DIR\"}\n");
      return 2;
    }
    return run_c_core("grokium-commander", argc - ai - 1, argv + ai + 1);
  }
  if (strcmp(cmd, "integrity") == 0) {
    if (ai + 1 >= argc ||
        !strcmp(argv[ai + 1], "help") || !strcmp(argv[ai + 1], "-h") ||
        !strcmp(argv[ai + 1], "--help")) {
      /* Machine need_subcmd plate — dual-wire honesty (fail-closed seal). */
      printf("{\"schema\":\"grokium.integrity_report.v1\",\"ok\":false,"
             "\"error\":\"need_subcmd\",\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false,"
             "\"hint\":\"tick|policy|reseal\"}\n");
      return 2;
    }
    return run_c_core("grokium-integrity", argc - ai - 1, argv + ai + 1);
  }
  if (strcmp(cmd, "sessions") == 0 || strcmp(cmd, "session") == 0)
    return cmd_sessions(argc - ai - 1, argv + ai + 1);
  if (strcmp(cmd, "pickup") == 0 || strcmp(cmd, "load") == 0) {
    if (ai + 1 >= argc) {
      char plate[768];
      if (gk_session_pickup_deny_json(NULL, "need_session_id", plate,
                                       sizeof plate) == 0)
        printf("%s\n", plate);
      return 2;
    }
    return cmd_session_pickup(argv[ai + 1]);
  }
  if (strcmp(cmd, "fleet") == 0 || strcmp(cmd, "nanobot") == 0) {
    const char *sub = (ai + 1 < argc) ? argv[ai + 1] : "defaults";
    /* opt into CubalC board fleet when asked; default is c_core plate */
    if (strcmp(sub, "cubalc") == 0)
      return run_cubalc_program("fleet.cubalc", NULL);
    if (ai + 1 >= argc) {
      char *def[] = {"defaults"};
      return run_c_core("grokium-fleet", 1, def);
    }
    return run_c_core("grokium-fleet", argc - ai - 1, argv + ai + 1);
  }
  if (strcmp(cmd, "heartbeat") == 0)
    return run_cubalc_program("heartbeat.cubalc", NULL);
  if (strcmp(cmd, "selftest") == 0)
    return run_cubalc_program("selftest.cubalc", NULL);
  if (strcmp(cmd, "llama") == 0 || strcmp(cmd, "llama-test") == 0 ||
      strcmp(cmd, "llama-probe") == 0) {
    char bin[PATH_MAX];
    /* pure-C probe first; CubalC board optional fallback */
    if (resolve_c_core_bin("grokium-serve", bin, sizeof bin) == 0) {
      char *av[] = {"probe"};
      return run_c_core("grokium-serve", 1, av);
    }
    return run_cubalc_program("llama_probe.cubalc", NULL);
  }

  if (strcmp(cmd, "chat") == 0 || strcmp(cmd, "ask") == 0 || strcmp(cmd, "say") == 0) {
    int i = ai + 1;
    while (i < argc) {
      if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
        snprintf(cfg.active_backend, sizeof cfg.active_backend, "%s", argv[++i]);
        i++;
        continue;
      }
      if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
        snprintf(cfg.active_model, sizeof cfg.active_model, "%s", argv[++i]);
        i++;
        continue;
      }
      break;
    }
    if (i >= argc) {
      char plate[512];
      /* Shared dual-wire need_message (HTTP / serve CLI same builder). */
      grokium_chat_err_json(
          "need_message", "chat [--backend local|grok] [--model ID] <msg>",
          plate, sizeof plate);
      printf("%s\n", plate);
      return 2;
    }
    char msg[2000];
    size_t o = 0;
    msg[0] = 0;
    for (; i < argc; i++) {
      size_t L = strlen(argv[i]);
      if (o + L + 2 >= sizeof msg) break;
      if (o) msg[o++] = ' ';
      memcpy(msg + o, argv[i], L);
      o += L;
      msg[o] = 0;
    }
    return cmd_prompt(&cfg, msg);
  }

  if (strcmp(cmd, "sync") == 0) {
    if (argc > ai + 1) {
      char plate[1024];
      size_t o = 0;
      plate[0] = 0;
      for (int i = ai + 1; i < argc; i++) {
        size_t L = strlen(argv[i]);
        if (o + L + 2 >= sizeof plate) break;
        if (o) plate[o++] = ' ';
        memcpy(plate + o, argv[i], L);
        o += L;
        plate[o] = 0;
      }
      char *av[] = {cubalc_bin, "sync", plate, NULL};
      setenv("CUBALC_STATE", state_dir, 1);
      return run_cubalc_args(av);
    }
    return run_cubalc_program("board.cubalc", NULL);
  }
  if (strcmp(cmd, "run") == 0) {
    if (ai + 1 >= argc) {
      /* Machine need_path plate — dual-wire honesty (CubalC opt-in). */
      printf("{\"schema\":\"grokium.run.v1\",\"ok\":false,"
             "\"error\":\"need_path\",\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false,"
             "\"hint\":\"run <file.cubalc>\"}\n");
      return 2;
    }
    char *av[] = {cubalc_bin, "run", argv[ai + 1], NULL};
    setenv("CUBALC_STATE", state_dir, 1);
    return run_cubalc_args(av);
  }
  if (strcmp(cmd, "tui") == 0) {
    int grokium_tui(int argc, char **argv);
    return grokium_tui(argc, argv);
  }

  /* bare prompt as first arg? treat as -p */
  if (cmd[0] != '-' && ai == 1 && argc >= 2 &&
      strcmp(cmd, "board") != 0 && strcmp(cmd, "fleet") != 0) {
    /* unknown command — if looks like sentence, chat; else error */
    if (strchr(cmd, ' ') || argc == 2) {
      /* assemble all args */
      char msg[2000];
      size_t o = 0;
      msg[0] = 0;
      for (int i = ai; i < argc; i++) {
        size_t L = strlen(argv[i]);
        if (o + L + 2 >= sizeof msg) break;
        if (o) msg[o++] = ' ';
        memcpy(msg + o, argv[i], L);
        o += L;
        msg[o] = 0;
      }
      /* only if not a known subcommand */
      if (strcmp(cmd, "help") != 0 && strcmp(cmd, "version") != 0 &&
          strcmp(cmd, "models") != 0 && strcmp(cmd, "compat") != 0 &&
          strcmp(cmd, "chat") != 0 && strcmp(cmd, "tui") != 0 &&
          strcmp(cmd, "selftest") != 0 && strcmp(cmd, "status") != 0 &&
          strcmp(cmd, "serve") != 0 && strcmp(cmd, "filter") != 0 &&
          strcmp(cmd, "smx-filter") != 0 && strcmp(cmd, "fleet") != 0 &&
          strcmp(cmd, "coord") != 0 && strcmp(cmd, "ingest") != 0 &&
          strcmp(cmd, "smx-ingest") != 0 &&
          strcmp(cmd, "contract") != 0 && strcmp(cmd, "manager") != 0 &&
          strcmp(cmd, "manager-tick") != 0 &&
          strcmp(cmd, "commander") != 0 &&
          strcmp(cmd, "integrity") != 0 &&
          strcmp(cmd, "sessions") != 0 && strcmp(cmd, "session") != 0 &&
          strcmp(cmd, "pickup") != 0 && strcmp(cmd, "load") != 0 &&
          strcmp(cmd, "law") != 0 && strcmp(cmd, "laws") != 0) {
        /* Prefer TUI for bare `grokium`; multi-word → prompt */
        if (argc > 2 || (argc == 2 && strchr(argv[1], ' ')))
          return cmd_prompt(&cfg, msg);
      }
    }
  }

  if (ai >= argc) {
    int grokium_tui(int argc, char **argv);
    return grokium_tui(argc, argv);
  }

  /* Machine need_subcmd — no free-text echo of user argv (dual-wire honesty). */
  printf("{\"schema\":\"grokium.cli.v1\",\"ok\":false,"
         "\"error\":\"need_subcmd\",\"product_wire\":\"smx2\","
         "\"peer_http\":\"lab_ops_only\","
         "\"peer_http_is_product_bus\":false,"
         "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
         "\"llm_is_commander\":false,"
         "\"hint\":\"help|chat|tui|serve|fleet|coord|status|law|integrity\"}\n");
  usage();
  return 2;
}
