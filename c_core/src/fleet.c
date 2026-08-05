/* SPDX-License-Identifier: Apache-2.0
 * Separable nanobot fleet plate — purpose-assigned Hive Mind peers.
 * Includes nb-manager (motivate incomplete contracts for NexusCore).
 * Fleet plate is honest: pid/status/offline reflect kill(0) probes.
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_fleet.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int pid_alive(int pid) {
  if (pid <= 0) return 0;
  if (kill(pid, 0) == 0) return 1;
  return errno == EPERM; /* exists, not ours — still alive */
}

static void bot_set(gk_bot *b, const char *id, const char *purpose, int port,
                    int shell, const char *home_root) {
  memset(b, 0, sizeof *b);
  snprintf(b->id, sizeof b->id, "%s", id);
  snprintf(b->purpose, sizeof b->purpose, "%s", purpose);
  b->port = port;
  b->pid = -1;
  b->shell = shell;
  b->running = 0;
  b->separated = 1;
  snprintf(b->home, sizeof b->home, "%s/%s", home_root, id);
}

void fleet_default_roles(gk_fleet *F) {
  const char *root;
  if (!F) return;
  memset(F, 0, sizeof *F);
  root = getenv("GROKIUM_HOME_ROOT");
  if (!root || !root[0]) root = "data/home";
  snprintf(F->home_root, sizeof F->home_root, "%s", root);
  snprintf(F->binary, sizeof F->binary, "%s",
           getenv("NANOBOT_BIN") && getenv("NANOBOT_BIN")[0]
               ? getenv("NANOBOT_BIN")
               : "nanobot");
  snprintf(F->base_url, sizeof F->base_url, "%s",
           "http://127.0.0.1:1212/v1");
  snprintf(F->model, sizeof F->model, "local");
  F->base_port = 28800;
  /* Cube Standards + Hive Mind manager */
  bot_set(&F->bots[0], "nb-matrix-eval", "evaluate_sot_smx_harmony",
          F->base_port + 0, 0, F->home_root);
  bot_set(&F->bots[1], "nb-construct", "construct_deconstruct_edge",
          F->base_port + 1, 1, F->home_root);
  bot_set(&F->bots[2], "nb-observer", "observe_unity_watchd",
          F->base_port + 2, 0, F->home_root);
  bot_set(&F->bots[3], "nb-host", "station_peer_cube_control",
          F->base_port + 3, 0, F->home_root);
  bot_set(&F->bots[4], "nb-manager", "motivate_incomplete_contracts",
          F->base_port + 4, 0, F->home_root);
  bot_set(&F->bots[5], "nb-integrity", "integrity_no_leak_core",
          F->base_port + 5, 0, F->home_root);
  F->n = 6;
}

int fleet_deploy(gk_fleet *F) {
  int i;
  if (!F) return -1;
  if (F->n <= 0) fleet_default_roles(F);
  mkdir(F->home_root, 0755);
  for (i = 0; i < F->n; i++) {
    mkdir(F->bots[i].home, 0755);
    /* deploy = plate homes only; process spawn is host/hub responsibility */
    F->bots[i].running = 0;
    F->bots[i].pid = -1;
    F->bots[i].separated = 1;
  }
  return 0;
}

int fleet_status(gk_fleet *F) {
  int i, alive = 0;
  if (!F) return -1;
  for (i = 0; i < F->n; i++) {
    gk_bot *b = &F->bots[i];
    if (b->pid > 0 && pid_alive(b->pid)) {
      b->running = 1;
      b->separated = 0;
      alive++;
    } else {
      if (b->pid > 0) b->pid = -1; /* was claimed, now dead */
      b->running = 0;
      b->separated = 1;
    }
  }
  return alive;
}

int fleet_note_pid(gk_fleet *F, const char *bot_id, int pid) {
  int i;
  if (!F || !bot_id) return -1;
  for (i = 0; i < F->n; i++) {
    if (strcmp(F->bots[i].id, bot_id) != 0) continue;
    if (pid > 0) {
      F->bots[i].pid = pid;
      F->bots[i].running = pid_alive(pid) ? 1 : 0;
      F->bots[i].separated = F->bots[i].running ? 0 : 1;
      if (!F->bots[i].running) F->bots[i].pid = -1;
    } else {
      F->bots[i].pid = -1;
      F->bots[i].running = 0;
      F->bots[i].separated = 1;
    }
    return 0;
  }
  return -1;
}

int fleet_spawn(gk_fleet *F, const char *bot_id) {
  int i;
  pid_t pid;
  if (!F || !bot_id || !F->binary[0]) return -1;
  (void)fleet_status(F);
  for (i = 0; i < F->n; i++) {
    gk_bot *b = &F->bots[i];
    char port[16], logpath[320], purpose[320];
    FILE *pf;
    int logfd, devnull;
    if (strcmp(b->id, bot_id) != 0) continue;
    if (b->running && b->pid > 0) return 0; /* already live */
    mkdir(F->home_root, 0755);
    mkdir(b->home, 0755);
    snprintf(purpose, sizeof purpose, "%s/PURPOSE.txt", b->home);
    pf = fopen(purpose, "w");
    if (pf) {
      fprintf(pf,
              "id=%s\npurpose=%s\nwire=%s\nhold_flash=1\n"
              "share=state_matrix_only\npeer_http=lab_ops_only\n",
              b->id, b->purpose,
              strcmp(b->id, "nb-manager") == 0 ? "smx_motivate" : "smx2");
      fclose(pf);
    }
    snprintf(port, sizeof port, "%d", b->port);
    pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
      /* detach stdio into home log; peer port is lab ops only */
      devnull = open("/dev/null", O_RDWR);
      if (devnull >= 0) {
        dup2(devnull, STDIN_FILENO);
      }
      snprintf(logpath, sizeof logpath, "%s/nanobot.log", b->home);
      logfd = open(logpath, O_WRONLY | O_CREAT | O_APPEND, 0644);
      if (logfd >= 0) {
        dup2(logfd, STDOUT_FILENO);
        dup2(logfd, STDERR_FILENO);
        if (logfd > STDERR_FILENO) close(logfd);
      } else if (devnull >= 0) {
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
      }
      if (devnull > STDERR_FILENO) close(devnull);
      setenv("NANOBOT_HOME", b->home, 1);
      setenv("NANOBOT_BASE_URL", F->base_url, 1);
      setenv("NANOBOT_MODEL", F->model, 1);
      setenv("GROKIUM_BOT_ID", b->id, 1);
      setenv("GROKIUM_PRODUCT_WIRE", "smx2", 1);
      if (strchr(F->binary, '/')) {
        execl(F->binary, F->binary, "--home", b->home, "--port", port,
              "--offline", "--base-url", F->base_url, "--model", F->model,
              (char *)NULL);
      } else {
        execlp(F->binary, F->binary, "--home", b->home, "--port", port,
               "--offline", "--base-url", F->base_url, "--model", F->model,
               (char *)NULL);
      }
      _exit(127);
    }
    {
      struct timespec ts = {0, 80000000L}; /* 80ms settle */
      nanosleep(&ts, NULL);
    }
    if (!pid_alive((int)pid)) return -1;
    if (fleet_note_pid(F, bot_id, (int)pid) != 0) {
      (void)kill(pid, SIGTERM);
      return -1;
    }
    return 0;
  }
  return -1;
}

int fleet_spawn_all(gk_fleet *F) {
  int i, n = 0;
  if (!F) return -1;
  for (i = 0; i < F->n; i++) {
    if (fleet_spawn(F, F->bots[i].id) == 0) n++;
  }
  return n;
}

static void bot_clear_live(gk_bot *b) {
  if (!b) return;
  if (b->pid > 0) {
    /* polite stop; host may SIGKILL later if needed */
    if (pid_alive(b->pid))
      (void)kill(b->pid, SIGTERM);
  }
  b->pid = -1;
  b->running = 0;
  b->separated = 1;
}

int fleet_separate(gk_fleet *F, const char *bot_id) {
  int i;
  if (!F || !bot_id) return -1;
  for (i = 0; i < F->n; i++) {
    if (!strcmp(F->bots[i].id, bot_id)) {
      bot_clear_live(&F->bots[i]);
      return 0;
    }
  }
  return -1;
}

int fleet_stop_all(gk_fleet *F) {
  int i;
  if (!F) return -1;
  for (i = 0; i < F->n; i++)
    bot_clear_live(&F->bots[i]);
  return 0;
}

int fleet_post_raw_bits(const gk_fleet *F, const char *bot_id,
                        const char *bits01) {
  /* Matrix raw → nanobot: bits only (no chat context). Plate write for now. */
  char path[400];
  FILE *f;
  int i;
  if (!F || !bot_id || !bits01) return -1;
  for (i = 0; i < F->n; i++) {
    if (strcmp(F->bots[i].id, bot_id) != 0) continue;
    snprintf(path, sizeof path, "%s/inbox.smx.bits", F->bots[i].home);
    mkdir(F->bots[i].home, 0755);
    f = fopen(path, "w");
    if (!f) return -1;
    fputs(bits01, f);
    fputc('\n', f);
    fclose(f);
    return 0;
  }
  return -1;
}

int fleet_save(gk_fleet *F, const char *path) {
  FILE *f;
  int i;
  if (!F || !path) return -1;
  (void)fleet_status(F); /* plate must match live process reality */
  f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f,
          "{\n  \"schema\": \"grokium.nanobot_fleet.v1\",\n"
          "  \"home_root\": \"%s\",\n"
          "  \"binary\": \"%s\",\n"
          "  \"base_url\": \"%s\",\n"
          "  \"model\": \"%s\",\n"
          "  \"share\": \"state_matrix_only\",\n"
          "  \"hold_flash\": 1,\n"
          "  \"product_wire\": \"smx2\",\n"
          "  \"peer_http\": \"lab_ops_only\",\n"
          "  \"peer_http_is_product_bus\": false,\n"
          "  \"llm_is_commander\": false,\n"
          "  \"commander_is_model\": false,\n"
          "  \"observer\": \"NexusCore\",\n"
          "  \"bots\": {\n",
          F->home_root, F->binary, F->base_url, F->model);
  for (i = 0; i < F->n; i++) {
    const gk_bot *b = &F->bots[i];
    char pid_buf[24];
    if (b->pid > 0)
      snprintf(pid_buf, sizeof pid_buf, "%d", b->pid);
    else
      snprintf(pid_buf, sizeof pid_buf, "null");
    /* Per-bot dual-wire honesty: role wire (smx2 / smx_motivate) ≠ peer HTTP. */
    fprintf(f,
            "    \"%s\": {\n"
            "      \"id\": \"%s\",\n"
            "      \"purpose\": \"%s\",\n"
            "      \"shell\": %s,\n"
            "      \"port\": %d,\n"
            "      \"pid\": %s,\n"
            "      \"home\": \"%s\",\n"
            "      \"binary\": \"%s\",\n"
            "      \"offline\": %s,\n"
            "      \"base_url\": \"%s\",\n"
            "      \"model\": \"%s\",\n"
            "      \"status\": \"%s\",\n"
            "      \"separated\": %s,\n"
            "      \"law\": \"cube_purpose_assigned\",\n"
            "      \"wire\": \"%s\",\n"
            "      \"product_wire\": \"smx2\",\n"
            "      \"peer_http\": \"lab_ops_only\",\n"
            "      \"peer_http_is_product_bus\": false,\n"
            "      \"llm_is_commander\": false\n"
            "    }%s\n",
            b->id, b->id, b->purpose, b->shell ? "true" : "false", b->port,
            pid_buf, b->home, F->binary, b->running ? "false" : "true",
            F->base_url, F->model, b->running ? "running" : "separated",
            b->separated ? "true" : "false",
            strcmp(b->id, "nb-manager") == 0 ? "smx_motivate" : "smx2",
            i + 1 < F->n ? "," : "");
  }
  fprintf(f, "  }\n}\n");
  fclose(f);
  return 0;
}

/* Pull "pid": N|null from a bot object window after its id key. */
static int parse_bot_pid(const char *json, const char *id) {
  char key[96];
  const char *p, *pidk, *win_end;
  int pid;
  if (!json || !id) return -1;
  snprintf(key, sizeof key, "\"id\": \"%s\"", id);
  p = strstr(json, key);
  if (!p) {
    snprintf(key, sizeof key, "\"id\":\"%s\"", id);
    p = strstr(json, key);
  }
  if (!p) return -1;
  win_end = strchr(p, '}');
  pidk = strstr(p, "\"pid\":");
  if (!pidk || (win_end && pidk > win_end)) return -1;
  pidk += 6;
  while (*pidk == ' ' || *pidk == '\t') pidk++;
  if (!strncmp(pidk, "null", 4)) return -1;
  pid = atoi(pidk);
  return pid > 0 ? pid : -1;
}

int fleet_load(gk_fleet *F, const char *path) {
  FILE *f;
  char *buf = NULL;
  long sz;
  int i, nread;
  if (!F) return -1;
  fleet_default_roles(F);
  if (!path || !path[0]) return 0;
  f = fopen(path, "r");
  if (!f) return 0;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return 0;
  }
  sz = ftell(f);
  if (sz <= 0 || sz > 256 * 1024) {
    fclose(f);
    return 0;
  }
  rewind(f);
  buf = (char *)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return -1;
  }
  nread = (int)fread(buf, 1, (size_t)sz, f);
  fclose(f);
  if (nread <= 0) {
    free(buf);
    return 0;
  }
  buf[nread] = 0;
  /* overlay live pids from plate; dead pids cleared by fleet_status */
  for (i = 0; i < F->n; i++) {
    int pid = parse_bot_pid(buf, F->bots[i].id);
    if (pid > 0) {
      F->bots[i].pid = pid;
      F->bots[i].running = pid_alive(pid) ? 1 : 0;
      F->bots[i].separated = F->bots[i].running ? 0 : 1;
      if (!F->bots[i].running) F->bots[i].pid = -1;
    }
  }
  free(buf);
  return 0;
}
