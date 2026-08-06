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

/*
 * Machine token for error leaves and bot ids on plates.
 * Allows '*' (spawn-all wildcard); drops quote/control inject.
 */
static void err_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in || !in[0]) return;
  for (i = 0; in[i] && o + 1 < cap && o < 48; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
        c == '*')
      out[o++] = (char)c;
    else if (c == ' ' || c == '/' || c == ':' || c == '"' || c == '\\' ||
             c == '\'' || c == ',' || c == ';')
      out[o++] = '_';
  }
  out[o] = 0;
}

/* Forward: used by fleet_defaults_json before definition. */
static void path_escape(const char *in, char *out, size_t cap);

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

void fleet_defaults_json(const gk_fleet *F, char *out, size_t cap) {
  int i;
  size_t used;
  if (!out || cap < 64) return;
  if (!F || F->n <= 0) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.fleet_defaults.v1\",\"ok\":false,"
             "\"error\":\"no_fleet\",\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"python\":0,\"bots\":[]}");
    return;
  }
  /* Role templates only — not live status; spawn remains host responsibility. */
  used = (size_t)snprintf(out, cap,
                          "{\"schema\":\"grokium.fleet_defaults.v1\","
                          "\"ok\":true,\"action\":\"defaults\",\"n\":%d,"
                          "\"nb_manager\":true,"
                          "\"content\":\"role_templates\","
                          "\"spawn\":\"host_responsibility\","
                          "\"product_wire\":\"smx2\","
                          "\"peer_http\":\"lab_ops_only\","
                          "\"peer_http_is_product_bus\":false,"
                          "\"llm_is_commander\":false,"
                          "\"commander_is_model\":false,"
                          "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
                          "\"python\":0,\"bots\":[",
                          F->n);
  for (i = 0; i < F->n && used + 160 < cap; i++) {
    const gk_bot *b = &F->bots[i];
    char id_tok[56], pur_tok[72], home_esc[320];
    int n;
    err_token(b->id, id_tok, sizeof id_tok);
    if (!id_tok[0]) snprintf(id_tok, sizeof id_tok, "bot");
    err_token(b->purpose, pur_tok, sizeof pur_tok);
    if (!pur_tok[0]) snprintf(pur_tok, sizeof pur_tok, "assigned");
    path_escape(b->home, home_esc, sizeof home_esc);
    n = snprintf(out + used, cap - used,
                 "%s{\"id\":\"%s\",\"purpose\":\"%s\",\"port\":%d,"
                 "\"home\":\"%s\",\"wire\":\"%s\"}",
                 i ? "," : "", id_tok, pur_tok, b->port, home_esc,
                 strcmp(b->id, "nb-manager") == 0 ? "smx_motivate" : "smx2");
    if (n < 0) break;
    used += (size_t)n;
  }
  if (used + 3 < cap)
    snprintf(out + used, cap - used, "]}");
}

/* Per-bot purpose plate: dual-wire honesty (SMX2 ≠ peer HTTP lab/ops). */
static int write_purpose_plate(const gk_bot *b) {
  char purpose[320];
  FILE *pf;
  if (!b || !b->home[0]) return -1;
  mkdir(b->home, 0755);
  snprintf(purpose, sizeof purpose, "%s/PURPOSE.txt", b->home);
  pf = fopen(purpose, "w");
  if (!pf) return -1;
  fprintf(pf,
          "id=%s\npurpose=%s\nwire=%s\nproduct_wire=smx2\nhold_flash=1\n"
          "share=state_matrix_only\npeer_http=lab_ops_only\n"
          "peer_http_is_product_bus=0\nllm_is_commander=0\n"
          "python=0\nobserver=NexusCore\n",
          b->id, b->purpose,
          strcmp(b->id, "nb-manager") == 0 ? "smx_motivate" : "smx2");
  fclose(pf);
  return 0;
}

int fleet_deploy(gk_fleet *F) {
  int i;
  if (!F) return -1;
  if (F->n <= 0) fleet_default_roles(F);
  mkdir(F->home_root, 0755);
  for (i = 0; i < F->n; i++) {
    mkdir(F->bots[i].home, 0755);
    (void)write_purpose_plate(&F->bots[i]);
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

void fleet_status_json(gk_fleet *F, char *out, size_t cap) {
  int i, alive;
  size_t used;
  if (!out || cap < 64) return;
  if (!F) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.nanobot_status.v1\",\"ok\":false,"
             "\"error\":\"no_fleet\",\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"python\":0,\"bots\":[]}");
    return;
  }
  alive = fleet_status(F);
  /* product_wire (not wire_product) — dual-wire honesty with host plates. */
  used = (size_t)snprintf(out, cap,
                          "{\"schema\":\"grokium.nanobot_status.v1\","
                          "\"ok\":true,\"alive\":%d,\"n\":%d,"
                          "\"nb_manager\":true,"
                          "\"product_wire\":\"smx2\","
                          "\"peer_http\":\"lab_ops_only\","
                          "\"peer_http_is_product_bus\":false,"
                          "\"llm_is_commander\":false,"
                          "\"commander_is_model\":false,"
                          "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
                          "\"python\":0,\"bots\":[",
                          alive, F->n);
  for (i = 0; i < F->n && used + 128 < cap; i++) {
    const gk_bot *b = &F->bots[i];
    char pid_buf[24], id_tok[56];
    int n;
    if (b->pid > 0)
      snprintf(pid_buf, sizeof pid_buf, "%d", b->pid);
    else
      snprintf(pid_buf, sizeof pid_buf, "null");
    err_token(b->id, id_tok, sizeof id_tok);
    if (!id_tok[0]) snprintf(id_tok, sizeof id_tok, "bot");
    n = snprintf(out + used, cap - used,
                 "%s{\"id\":\"%s\",\"port\":%d,\"pid\":%s,"
                 "\"status\":\"%s\",\"offline\":%s,\"wire\":\"%s\"}",
                 i ? "," : "", id_tok, b->port, pid_buf,
                 b->running ? "running" : "separated",
                 b->running ? "false" : "true",
                 strcmp(b->id, "nb-manager") == 0 ? "smx_motivate" : "smx2");
    if (n < 0) break;
    used += (size_t)n;
  }
  if (used + 3 < cap)
    snprintf(out + used, cap - used, "]}");
}

static void path_escape(const char *in, char *out, size_t cap) {
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

void fleet_deploy_json(gk_fleet *F, const char *path, char *out, size_t cap) {
  char path_esc[640];
  int alive;
  if (!out || cap < 64) return;
  if (!F) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.nanobot_deploy.v1\",\"ok\":false,"
             "\"error\":\"no_fleet\",\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false,\"python\":0}");
    return;
  }
  alive = fleet_status(F);
  path_escape(path ? path : "", path_esc, sizeof path_esc);
  /* Deploy plate: homes only; process spawn is host/hub responsibility. */
  snprintf(out, cap,
           "{\"schema\":\"grokium.nanobot_deploy.v1\",\"ok\":true,"
           "\"deployed\":%d,\"path\":\"%s\",\"nb_manager\":true,"
           "\"alive\":%d,\"spawn\":\"host_responsibility\","
           "\"product_wire\":\"smx2\",\"wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false,\"python\":0}",
           F->n, path_esc, alive);
}

/* Shared dual-wire tails — CLI + HTTP use identical product honesty (py=0). */
#define FLEET_DUAL_WIRE_TAIL                                               \
  "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","              \
  "\"peer_http_is_product_bus\":false,"                                    \
  "\"share\":\"state_matrix_only\",\"hold_flash\":1,"                      \
  "\"llm_is_commander\":false,\"python\":0"

void fleet_spawn_err_json(const char *error, char *out, size_t cap) {
  char err_tok[56];
  if (!out || cap < 64) return;
  err_token(error && error[0] ? error : "spawn_failed", err_tok,
            sizeof err_tok);
  if (!err_tok[0]) snprintf(err_tok, sizeof err_tok, "spawn_failed");
  if (!strcmp(err_tok, "need_bot_id")) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.nanobot_spawn.v1\",\"ok\":false,"
             "\"error\":\"need_bot_id\"," FLEET_DUAL_WIRE_TAIL ","
             "\"hint\":\"pass bot id e.g. nb-manager\"}");
    return;
  }
  snprintf(out, cap,
           "{\"schema\":\"grokium.nanobot_spawn.v1\",\"ok\":false,"
           "\"error\":\"%s\"," FLEET_DUAL_WIRE_TAIL "}",
           err_tok);
}

void fleet_spawn_json(gk_fleet *F, const char *id, int spawned,
                      const char *path, char *out, size_t cap) {
  char path_esc[640], id_tok[56];
  const char *bid = id && id[0] ? id : "*";
  int alive, i;
  if (!out || cap < 64) return;
  if (!F) {
    fleet_spawn_err_json("no_fleet", out, cap);
    return;
  }
  alive = fleet_status(F);
  path_escape(path ? path : "", path_esc, sizeof path_esc);
  /* Single-bot: honest pid/running/status/wire (manager = smx_motivate). */
  if (strcmp(bid, "*") != 0) {
    for (i = 0; i < F->n; i++) {
      const gk_bot *b = &F->bots[i];
      char pid_buf[24];
      if (strcmp(b->id, bid) != 0) continue;
      if (b->pid > 0)
        snprintf(pid_buf, sizeof pid_buf, "%d", b->pid);
      else
        snprintf(pid_buf, sizeof pid_buf, "null");
      err_token(b->id, id_tok, sizeof id_tok);
      if (!id_tok[0]) snprintf(id_tok, sizeof id_tok, "bot");
      snprintf(out, cap,
               "{\"schema\":\"grokium.nanobot_spawn.v1\",\"ok\":true,"
               "\"spawned\":%d,\"id\":\"%s\",\"pid\":%s,\"running\":%s,"
               "\"status\":\"%s\",\"alive\":%d,\"path\":\"%s\","
               "\"wire\":\"%s\"," FLEET_DUAL_WIRE_TAIL "}",
               spawned > 0 ? spawned : 1, id_tok, pid_buf,
               b->running ? "true" : "false",
               b->running ? "running" : "separated", alive, path_esc,
               strcmp(b->id, "nb-manager") == 0 ? "smx_motivate" : "smx2");
      return;
    }
  }
  /* spawn-all or unknown id after success path: aggregate plate. */
  err_token(bid, id_tok, sizeof id_tok);
  if (!id_tok[0]) snprintf(id_tok, sizeof id_tok, "bot");
  snprintf(out, cap,
           "{\"schema\":\"grokium.nanobot_spawn.v1\",\"ok\":true,"
           "\"spawned\":%d,\"id\":\"%s\",\"alive\":%d,\"path\":\"%s\","
           FLEET_DUAL_WIRE_TAIL "}",
           spawned, id_tok, alive, path_esc);
}

void fleet_separate_err_json(const char *error, char *out, size_t cap) {
  char err_tok[56];
  if (!out || cap < 64) return;
  err_token(error && error[0] ? error : "unknown_bot", err_tok,
            sizeof err_tok);
  if (!err_tok[0]) snprintf(err_tok, sizeof err_tok, "unknown_bot");
  if (!strcmp(err_tok, "need_bot_id")) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.nanobot_separate.v1\",\"ok\":false,"
             "\"error\":\"need_bot_id\"," FLEET_DUAL_WIRE_TAIL ","
             "\"hint\":\"pass bot id e.g. nb-manager\"}");
    return;
  }
  snprintf(out, cap,
           "{\"schema\":\"grokium.nanobot_separate.v1\",\"ok\":false,"
           "\"error\":\"%s\"," FLEET_DUAL_WIRE_TAIL "}",
           err_tok);
}

void fleet_separate_json(const char *id, const char *path, char *out,
                         size_t cap) {
  char path_esc[640], id_tok[56];
  const char *bid = id && id[0] ? id : "";
  const char *wire =
      strcmp(bid, "nb-manager") == 0 ? "smx_motivate" : "smx2";
  if (!out || cap < 64) return;
  if (!bid[0]) {
    fleet_separate_err_json("need_bot_id", out, cap);
    return;
  }
  path_escape(path ? path : "", path_esc, sizeof path_esc);
  err_token(bid, id_tok, sizeof id_tok);
  if (!id_tok[0]) snprintf(id_tok, sizeof id_tok, "bot");
  snprintf(out, cap,
           "{\"schema\":\"grokium.nanobot_separate.v1\",\"ok\":true,"
           "\"id\":\"%s\",\"status\":\"separated\",\"pid\":null,"
           "\"path\":\"%s\",\"wire\":\"%s\"," FLEET_DUAL_WIRE_TAIL "}",
           id_tok, path_esc, wire);
}

void fleet_note_pid_err_json(const char *error, char *out, size_t cap) {
  char err_tok[56];
  if (!out || cap < 64) return;
  err_token(error && error[0] ? error : "unknown_bot", err_tok,
            sizeof err_tok);
  if (!err_tok[0]) snprintf(err_tok, sizeof err_tok, "unknown_bot");
  if (!strcmp(err_tok, "need_bot_id_pid")) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.nanobot_note_pid.v1\",\"ok\":false,"
             "\"error\":\"need_bot_id_pid\"," FLEET_DUAL_WIRE_TAIL ","
             "\"hint\":\"id and pid required e.g. nb-manager 1234\"}");
    return;
  }
  snprintf(out, cap,
           "{\"schema\":\"grokium.nanobot_note_pid.v1\",\"ok\":false,"
           "\"error\":\"%s\"," FLEET_DUAL_WIRE_TAIL "}",
           err_tok);
}

void fleet_note_pid_json(gk_fleet *F, const char *id, const char *path,
                         char *out, size_t cap) {
  char path_esc[640], id_tok[56];
  const char *bid = id && id[0] ? id : "";
  int i;
  if (!out || cap < 64) return;
  if (!F) {
    fleet_note_pid_err_json("no_fleet", out, cap);
    return;
  }
  if (!bid[0]) {
    fleet_note_pid_err_json("need_bot_id_pid", out, cap);
    return;
  }
  path_escape(path ? path : "", path_esc, sizeof path_esc);
  for (i = 0; i < F->n; i++) {
    const gk_bot *b = &F->bots[i];
    char pid_buf[24];
    if (strcmp(b->id, bid) != 0) continue;
    if (b->pid > 0)
      snprintf(pid_buf, sizeof pid_buf, "%d", b->pid);
    else
      snprintf(pid_buf, sizeof pid_buf, "null");
    err_token(b->id, id_tok, sizeof id_tok);
    if (!id_tok[0]) snprintf(id_tok, sizeof id_tok, "bot");
    snprintf(out, cap,
             "{\"schema\":\"grokium.nanobot_note_pid.v1\",\"ok\":true,"
             "\"id\":\"%s\",\"pid\":%s,\"running\":%s,\"status\":\"%s\","
             "\"path\":\"%s\",\"wire\":\"%s\"," FLEET_DUAL_WIRE_TAIL "}",
             id_tok, pid_buf, b->running ? "true" : "false",
             b->running ? "running" : "separated", path_esc,
             strcmp(b->id, "nb-manager") == 0 ? "smx_motivate" : "smx2");
    return;
  }
  fleet_note_pid_err_json("unknown_bot", out, cap);
}

void fleet_stop_err_json(const char *error, char *out, size_t cap) {
  char err_tok[56];
  if (!out || cap < 64) return;
  err_token(error && error[0] ? error : "no_fleet", err_tok, sizeof err_tok);
  if (!err_tok[0]) snprintf(err_tok, sizeof err_tok, "no_fleet");
  snprintf(out, cap,
           "{\"schema\":\"grokium.nanobot_stop.v1\",\"ok\":false,"
           "\"error\":\"%s\"," FLEET_DUAL_WIRE_TAIL "}",
           err_tok);
}

void fleet_stop_json(gk_fleet *F, const char *path, char *out, size_t cap) {
  char path_esc[640];
  int alive;
  if (!out || cap < 64) return;
  if (!F) {
    fleet_stop_err_json("no_fleet", out, cap);
    return;
  }
  alive = fleet_status(F);
  path_escape(path ? path : "", path_esc, sizeof path_esc);
  /* After stop-all: plate records stopped + honest alive (should be 0). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.nanobot_stop.v1\",\"ok\":true,"
           "\"stopped\":true,\"n\":%d,\"alive\":%d,\"path\":\"%s\","
           "\"nb_manager\":true," FLEET_DUAL_WIRE_TAIL "}",
           F->n, alive, path_esc);
}

void fleet_save_err_json(const char *error, char *out, size_t cap) {
  char err_tok[56];
  if (!out || cap < 64) return;
  err_token(error && error[0] ? error : "no_fleet", err_tok, sizeof err_tok);
  if (!err_tok[0]) snprintf(err_tok, sizeof err_tok, "no_fleet");
  snprintf(out, cap,
           "{\"schema\":\"grokium.nanobot_save.v1\",\"ok\":false,"
           "\"error\":\"%s\"," FLEET_DUAL_WIRE_TAIL "}",
           err_tok);
}

void fleet_save_json(gk_fleet *F, const char *path, char *out, size_t cap) {
  char path_esc[640];
  int alive;
  if (!out || cap < 64) return;
  if (!F) {
    fleet_save_err_json("no_fleet", out, cap);
    return;
  }
  alive = fleet_status(F);
  path_escape(path ? path : "", path_esc, sizeof path_esc);
  /* Save ack: path escaped; alive from kill(0) probe (honest plate write). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.nanobot_save.v1\",\"ok\":true,"
           "\"saved\":\"%s\",\"n\":%d,\"alive\":%d,\"nb_manager\":true,"
           FLEET_DUAL_WIRE_TAIL "}",
           path_esc, F->n, alive);
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
    char port[16], logpath[320];
    int logfd, devnull;
    if (strcmp(b->id, bot_id) != 0) continue;
    if (b->running && b->pid > 0) return 0; /* already live */
    mkdir(F->home_root, 0755);
    mkdir(b->home, 0755);
    (void)write_purpose_plate(b);
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
  char home_root_esc[320], binary_esc[320], base_esc[192], model_esc[96];
  if (!F || !path) return -1;
  (void)fleet_status(F); /* plate must match live process reality */
  f = fopen(path, "w");
  if (!f) return -1;
  /* Escape path/model strings so hostile env cannot break on-disk JSON. */
  path_escape(F->home_root, home_root_esc, sizeof home_root_esc);
  path_escape(F->binary, binary_esc, sizeof binary_esc);
  path_escape(F->base_url, base_esc, sizeof base_esc);
  path_escape(F->model, model_esc, sizeof model_esc);
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
          "  \"python\": 0,\n"
          "  \"observer\": \"NexusCore\",\n"
          "  \"bots\": {\n",
          home_root_esc, binary_esc, base_esc, model_esc);
  for (i = 0; i < F->n; i++) {
    const gk_bot *b = &F->bots[i];
    char pid_buf[24], id_esc[48], purpose_esc[96], home_esc[320];
    if (b->pid > 0)
      snprintf(pid_buf, sizeof pid_buf, "%d", b->pid);
    else
      snprintf(pid_buf, sizeof pid_buf, "null");
    path_escape(b->id, id_esc, sizeof id_esc);
    path_escape(b->purpose, purpose_esc, sizeof purpose_esc);
    path_escape(b->home, home_esc, sizeof home_esc);
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
            "      \"llm_is_commander\": false,\n"
            "      \"python\": 0\n"
            "    }%s\n",
            id_esc, id_esc, purpose_esc, b->shell ? "true" : "false", b->port,
            pid_buf, home_esc, binary_esc, b->running ? "false" : "true",
            base_esc, model_esc, b->running ? "running" : "separated",
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
