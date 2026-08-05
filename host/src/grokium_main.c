#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include "grokium.h"
#include "grokium_chat.h"
#include "grokium_config.h"
#include "grokium_version.h"
#include "grokium_hub.h"
#include "grokium_status.h"
#include <dirent.h>
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
    fprintf(stderr, "grokium: missing CubalC program %s\n", prog);
    return 2;
  }
  if (!file_ok(cubalc_bin)) {
    fprintf(stderr,
            "grokium: CubalC binary missing: %s\n"
            "  build: ./scripts/sync_cubalc.sh && make -C deps/cubalc all\n",
            cubalc_bin);
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
    fprintf(stderr, "grokium: CubalC binary missing: %s\n", cubalc_bin);
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
    fprintf(stderr,
            "grokium: missing build/%s — run: make -C c_core all\n", name);
    return 99;
  }
  if (argc + 1 >= (int)(sizeof av / sizeof av[0])) {
    fprintf(stderr, "grokium: too many args for %s\n", name);
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
    fprintf(stderr,
            "usage: grokium law [cubalc]\n"
            "  pure-C Cube Standards plate (default)\n"
            "  cubalc — optional CubalC board law program\n");
    return 0;
  }
  printf("{\"schema\":\"grokium.law.v1\",\"ok\":true,"
         "\"hold_flash\":1,\"no_brain_wires\":1,\"state_matrix_key\":1,"
         "\"cores_unmixed\":1,\"face_blur\":1,\"zero_telemetry\":1,"
         "\"commander_only_residual\":1,\"share\":\"state_matrix_only\","
         "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
         "\"peer_http_is_product_bus\":false,"
         "\"commander\":\"ed25519\",\"llm_is_commander\":false,"
         "\"python\":0,\"host\":\"C\",\"product\":\"grokium\","
         "\"not\":\"grok_model\"}\n");
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
    fprintf(stderr,
            "usage: grokium status [cubalc|board]\n"
            "  pure-C dual-wire honesty plate (default)\n"
            "  cubalc|board — optional CubalC board status\n");
    return 0;
  }
  if (gkx_status_plate_json(root, "host_cli", plate, sizeof plate) != 0) {
    fprintf(stderr, "grokium status: plate failed\n");
    return 1;
  }
  printf("%s\n", plate);
  return 0;
}

/* Imported Grok Build metas only — never dump chat transcripts on CLI wire. */
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

static int session_id_safe(const char *id) {
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

/* Detect host-local chat_history without reading transcripts onto the wire. */
static int session_resume_available(const char *id, const char *meta,
                                    char *hist_out, size_t hcap) {
  char import_path[PATH_MAX], try[PATH_MAX];
  const char *h;
  size_t hl;
  if (hist_out && hcap) hist_out[0] = 0;
  if (!id || !id[0]) return 0;
  import_path[0] = 0;
  if (meta) {
    meta_get_str(meta, "import_path", import_path, sizeof import_path);
    if (!import_path[0])
      meta_get_str(meta, "source", import_path, sizeof import_path);
  }
  h = getenv("HOME");
  hl = (h && h[0]) ? strlen(h) : 0;
  if (import_path[0] == '/') {
    int allow = 0;
    if (hl && strncmp(import_path, h, hl) == 0 &&
        (import_path[hl] == '/' || import_path[hl] == 0))
      allow = 1;
    snprintf(try, sizeof try, "%s/data", root);
    if (!allow && strncmp(import_path, try, strlen(try)) == 0)
      allow = 1;
    if (allow) {
      snprintf(try, sizeof try, "%s/chat_history.jsonl", import_path);
      if (access(try, R_OK) == 0) {
        if (hist_out && hcap) snprintf(hist_out, hcap, "%s", try);
        return 1;
      }
    }
  }
  snprintf(try, sizeof try, "%s/data/import/%s/chat_history.jsonl", root, id);
  if (access(try, R_OK) == 0) {
    if (hist_out && hcap) snprintf(hist_out, hcap, "%s", try);
    return 1;
  }
  return 0;
}

static int cmd_session_pickup(const char *id) {
  char path[PATH_MAX], meta[2048], hist[PATH_MAX];
  char title[96], updated[48], model[96];
  char te[128], ue[64], me[128], he[PATH_MAX * 2];
  FILE *f;
  size_t nread;
  int resume_ok;
  if (!session_id_safe(id)) {
    fprintf(stderr,
            "usage: grokium pickup|load <session-id>\n"
            "  meta plate + resume_available (no transcripts on wire)\n");
    return 2;
  }
  snprintf(path, sizeof path, "%s/data/import/%s.meta.json", root, id);
  f = fopen(path, "r");
  if (!f) {
    printf("{\"schema\":\"grokium.session_pickup.v1\",\"ok\":false,"
           "\"error\":\"not_found\",\"id\":\"%s\",\"content\":\"meta_only\","
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_is_commander\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"resume_available\":false,"
           "\"hint\":\"import meta only; TUI /pickup resumes host-local\"}\n",
           id);
    return 1;
  }
  nread = fread(meta, 1, sizeof meta - 1, f);
  meta[nread] = 0;
  fclose(f);
  title[0] = updated[0] = model[0] = hist[0] = 0;
  meta_get_str(meta, "title", title, sizeof title);
  meta_get_str(meta, "updated_at", updated, sizeof updated);
  meta_get_str(meta, "model", model, sizeof model);
  resume_ok = session_resume_available(id, meta, hist, sizeof hist);
  json_escape(title, te, sizeof te);
  json_escape(updated, ue, sizeof ue);
  json_escape(model, me, sizeof me);
  json_escape(hist, he, sizeof he);
  printf("{\"schema\":\"grokium.session_pickup.v1\",\"ok\":true,"
         "\"content\":\"meta_only\",\"product_wire\":\"smx2\","
         "\"peer_http\":\"lab_ops_only\","
         "\"peer_http_is_product_bus\":false,"
         "\"llm_is_commander\":false,"
         "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
         "\"telemetry\":\"off\",\"resume\":\"host_tui\","
         "\"resume_available\":%s,\"resume_hist\":\"%s\","
         "\"hint\":\"TUI /pickup loads last turns host-local only\","
         "\"session\":{\"id\":\"%s\",\"title\":\"%s\",\"updated_at\":\"%s\","
         "\"model\":\"%s\"}}\n",
         resume_ok ? "true" : "false", he, id, te, ue, me);
  return 0;
}

static int cmd_sessions(int argc, char **argv) {
  char dir[PATH_MAX], path[PATH_MAX], meta[2048];
  char id[96], title[96], updated[48], model[96];
  char te[128], q_esc[128];
  const char *q = "";
  DIR *d;
  struct dirent *e;
  int matched = 0, scanned = 0;
  FILE *f;
  size_t nread, tlen;
  int first = 1;

  if (argc >= 1 &&
      (!strcmp(argv[0], "pickup") || !strcmp(argv[0], "load") ||
       !strcmp(argv[0], "get"))) {
    if (argc < 2) {
      fprintf(stderr, "usage: grokium sessions pickup <id>\n");
      return 2;
    }
    return cmd_session_pickup(argv[1]);
  }
  if (argc >= 1 &&
      (!strcmp(argv[0], "help") || !strcmp(argv[0], "-h") ||
       !strcmp(argv[0], "--help"))) {
    fprintf(stderr,
            "usage: grokium sessions [q]\n"
            "       grokium sessions pickup|load <id>\n"
            "  content=meta_only · share=state_matrix_only · no transcripts\n");
    return 0;
  }
  if (argc >= 1) q = argv[0];

  json_escape(q, q_esc, sizeof q_esc);
  snprintf(dir, sizeof dir, "%s/data/import", root);
  d = opendir(dir);
  if (!d) {
    printf("{\"schema\":\"grokium.sessions.v1\",\"ok\":true,\"n\":0,"
           "\"sessions\":[],\"q\":\"%s\",\"import_dir\":\"%s/data/import\","
           "\"error\":\"no_import_dir\",\"content\":\"meta_only\","
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_is_commander\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"telemetry\":\"off\"}\n",
           q_esc, root);
    return 0;
  }
  printf("{\"schema\":\"grokium.sessions.v1\",\"ok\":true,"
         "\"content\":\"meta_only\",\"product_wire\":\"smx2\","
         "\"peer_http\":\"lab_ops_only\","
         "\"peer_http_is_product_bus\":false,"
         "\"llm_is_commander\":false,"
         "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
         "\"telemetry\":\"off\",\"q\":\"%s\",\"import_dir\":\"%s\","
         "\"sessions\":[",
         q_esc, dir);
  while ((e = readdir(d)) != NULL && matched < 24 && scanned < 800) {
    size_t len = strlen(e->d_name);
    if (len < 11 || strcmp(e->d_name + len - 10, ".meta.json") != 0)
      continue;
    scanned++;
    snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
    f = fopen(path, "r");
    if (!f) continue;
    nread = fread(meta, 1, sizeof meta - 1, f);
    meta[nread] = 0;
    fclose(f);
    if (q[0] && !contains_ci(meta, q) && !contains_ci(e->d_name, q))
      continue;
    id[0] = title[0] = updated[0] = model[0] = 0;
    meta_get_str(meta, "id", id, sizeof id);
    meta_get_str(meta, "title", title, sizeof title);
    meta_get_str(meta, "updated_at", updated, sizeof updated);
    meta_get_str(meta, "model", model, sizeof model);
    if (!id[0]) {
      snprintf(id, sizeof id, "%s", e->d_name);
      tlen = strlen(id);
      if (tlen > 10) id[tlen - 10] = 0;
    }
    if (!session_id_safe(id)) continue;
    if (!title[0]) snprintf(title, sizeof title, "%s", id);
    json_escape(title, te, sizeof te);
    printf("%s{\"id\":\"%s\",\"title\":\"%s\",\"updated_at\":\"%s\","
           "\"model\":\"%s\"}",
           first ? "" : ",", id, te, updated, model);
    first = 0;
    matched++;
  }
  closedir(d);
  printf("],\"n\":%d,\"scanned\":%d,\"limit\":24,"
         "\"resume\":\"host_path\"}\n",
         matched, scanned);
  return 0;
}

static int cmd_models(gkx_config *cfg) {
  char err[400];
  char *body = grokium_models_json(cfg, err, sizeof err);
  if (!body) {
    fprintf(stderr, "grokium models: %s\n", err[0] ? err : "fail");
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
  fprintf(stderr, "grokium: %s\n", err[0] ? err : "fail");
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
        fprintf(stderr, "grokium: -p needs prompt\n");
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
    printf("{\"product\":\"grokium\",\"version\":\"%s\",\"core\":\"nanobot\","
           "\"host\":\"C\",\"python\":0,\"tok\":\"%s\"}\n",
           GROKIUM_VERSION, GROKIUM_TOK);
    return 0;
  }
  if (strcmp(cmd, "models") == 0) {
    return cmd_models(&cfg);
  }
  if (strcmp(cmd, "compat") == 0) {
    gkx_version_state st;
    gkx_version_init(&st, root);
    gkx_version_refresh(&st, root);
    printf("official_cli=%s changed=%d\n", st.official, st.changed);
    return 0;
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
    {
      char buf[640];
      int rc = gkx_hub_status(buf, sizeof buf);
      printf("%s\n", buf);
      return rc;
    }
  }
  if (strcmp(cmd, "board") == 0)
    return run_cubalc_program("board.cubalc", NULL);
  if (strcmp(cmd, "status") == 0)
    return cmd_status(argc - ai - 1, argv + ai + 1);
  if (strcmp(cmd, "law") == 0 || strcmp(cmd, "laws") == 0)
    return cmd_law(argc - ai - 1, argv + ai + 1);
  /* Hive Mind pure-C surface (product bus = SMX2; HTTP loopback = lab/ops) */
  if (strcmp(cmd, "serve") == 0) {
    return run_c_core("grokium-serve", argc - ai - 1, argv + ai + 1);
  }
  if (strcmp(cmd, "filter") == 0 || strcmp(cmd, "smx-filter") == 0) {
    return run_c_core("grokium-smx-filter", argc - ai - 1, argv + ai + 1);
  }
  /* External coord/ingest → pure-C consolidator with SMX filter sanitize */
  if (strcmp(cmd, "coord") == 0 || strcmp(cmd, "ingest") == 0 ||
      strcmp(cmd, "smx-ingest") == 0) {
    int i = ai + 1;
    char *av[3];
    char plate[4096];
    size_t o = 0;
    if (i >= argc) {
      fprintf(stderr,
              "usage: grokium coord|ingest <NEXUS_COORD|SMX bits plate>\n"
              "  SMX filter fail-closed; prose denied; product_wire=smx2\n");
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
    av[0] = "ingest";
    av[1] = plate;
    av[2] = NULL;
    return run_c_core("grokium-consolidate", 2, av);
  }
  if (strcmp(cmd, "contract") == 0) {
    if (ai + 1 >= argc) {
      fprintf(stderr,
              "usage: grokium contract form --assignee ID --task TEXT "
              "[--digit N] [--min-set N]\n"
              "       grokium contract validate PATH [--bits 01…]\n"
              "       grokium contract manager-tick [DIR]\n");
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
    if (ai + 1 >= argc) {
      fprintf(stderr,
              "usage: grokium commander keygen|show|sign|verify|install-law "
              "--law-dir DIR …\n"
              "  Commander is Ed25519 law identity — never a Grok model.\n");
      return 2;
    }
    return run_c_core("grokium-commander", argc - ai - 1, argv + ai + 1);
  }
  if (strcmp(cmd, "integrity") == 0) {
    if (ai + 1 >= argc) {
      fprintf(stderr, "usage: grokium integrity tick|policy|reseal\n");
      return 2;
    }
    return run_c_core("grokium-integrity", argc - ai - 1, argv + ai + 1);
  }
  if (strcmp(cmd, "sessions") == 0 || strcmp(cmd, "session") == 0)
    return cmd_sessions(argc - ai - 1, argv + ai + 1);
  if (strcmp(cmd, "pickup") == 0 || strcmp(cmd, "load") == 0) {
    if (ai + 1 >= argc) {
      fprintf(stderr,
              "usage: grokium pickup|load <session-id>\n"
              "  meta only · full resume stays host/nanobot path\n");
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
      fprintf(stderr, "usage: grokium chat [--backend local|grok] [--model ID] <msg>\n");
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
      fprintf(stderr, "usage: grokium run <file.cubalc>\n");
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

  fprintf(stderr, "grokium: unknown cmd '%s'\n", cmd);
  usage();
  return 2;
}
