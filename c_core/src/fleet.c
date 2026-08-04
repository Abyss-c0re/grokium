/* SPDX-License-Identifier: Apache-2.0
 * Separable nanobot fleet plate — purpose-assigned Hive Mind peers.
 * Includes nb-manager (motivate incomplete contracts for NexusCore).
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_fleet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
  for (i = 0; i < F->n; i++)
    if (F->bots[i].running && F->bots[i].pid > 0) alive++;
  return alive;
}

int fleet_separate(gk_fleet *F, const char *bot_id) {
  int i;
  if (!F || !bot_id) return -1;
  for (i = 0; i < F->n; i++) {
    if (!strcmp(F->bots[i].id, bot_id)) {
      F->bots[i].separated = 1;
      F->bots[i].running = 0;
      F->bots[i].pid = -1;
      return 0;
    }
  }
  return -1;
}

int fleet_stop_all(gk_fleet *F) {
  int i;
  if (!F) return -1;
  for (i = 0; i < F->n; i++) {
    F->bots[i].running = 0;
    F->bots[i].pid = -1;
  }
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

int fleet_save(const gk_fleet *F, const char *path) {
  FILE *f;
  int i;
  if (!F || !path) return -1;
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
          "  \"observer\": \"NexusCore\",\n"
          "  \"bots\": {\n",
          F->home_root, F->binary, F->base_url, F->model);
  for (i = 0; i < F->n; i++) {
    const gk_bot *b = &F->bots[i];
    fprintf(f,
            "    \"%s\": {\n"
            "      \"id\": \"%s\",\n"
            "      \"purpose\": \"%s\",\n"
            "      \"shell\": %s,\n"
            "      \"port\": %d,\n"
            "      \"pid\": %s,\n"
            "      \"home\": \"%s\",\n"
            "      \"binary\": \"%s\",\n"
            "      \"offline\": true,\n"
            "      \"base_url\": \"%s\",\n"
            "      \"model\": \"%s\",\n"
            "      \"status\": \"%s\",\n"
            "      \"separated\": true,\n"
            "      \"law\": \"cube_purpose_assigned\",\n"
            "      \"wire\": \"%s\"\n"
            "    }%s\n",
            b->id, b->id, b->purpose, b->shell ? "true" : "false", b->port,
            b->pid > 0 ? "null" : "null", b->home, F->binary, F->base_url,
            F->model, b->running ? "running" : "separated",
            strcmp(b->id, "nb-manager") == 0 ? "smx_motivate" : "smx2",
            i + 1 < F->n ? "," : "");
  }
  fprintf(f, "  }\n}\n");
  fclose(f);
  return 0;
}

int fleet_load(gk_fleet *F, const char *path) {
  /* Minimal: reset defaults if file missing; full JSON load optional later */
  FILE *f;
  if (!F) return -1;
  f = path ? fopen(path, "r") : NULL;
  if (!f) {
    fleet_default_roles(F);
    return 0;
  }
  fclose(f);
  fleet_default_roles(F);
  return 0;
}
