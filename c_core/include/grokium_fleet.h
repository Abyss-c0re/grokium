/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GROKIUM_FLEET_H
#define GROKIUM_FLEET_H
/* Separable nanobot subagents — purpose assigned, Cube Standards */
#include <stddef.h>

#define GK_FLEET_MAX 8
typedef struct {
  char id[32];
  char purpose[64];
  int port;
  int pid;
  int shell;
  int running;
  int separated;
  char home[256];
} gk_bot;

typedef struct {
  gk_bot bots[GK_FLEET_MAX];
  int n;
  char binary[256];
  char base_url[128];
  char model[64];
  int base_port;
  char home_root[256];
} gk_fleet;

void fleet_default_roles(gk_fleet *F);
int  fleet_deploy(gk_fleet *F);
/* Probe PIDs with kill(0); clear dead; return alive count. */
int  fleet_status(gk_fleet *F);
/*
 * Dual-wire nanobot status plate (schema grokium.nanobot_status.v1).
 * Probes PIDs first; pid/status/offline honest; meta_only bots list (no logs).
 */
void fleet_status_json(gk_fleet *F, char *out, size_t cap);
/* Host/hub records a spawned bot pid (or -1 to clear). */
int  fleet_note_pid(gk_fleet *F, const char *bot_id, int pid);
/* Fork/exec F->binary (--home/--port/--offline). Peer HTTP = lab_ops only. */
int  fleet_spawn(gk_fleet *F, const char *bot_id);
/* Spawn every role; returns count started (already-up counts as started). */
int  fleet_spawn_all(gk_fleet *F);
/* SIGTERM live pid if any, then mark separated. */
int  fleet_separate(gk_fleet *F, const char *bot_id);
/* SIGTERM all live bots. */
int  fleet_stop_all(gk_fleet *F);
int  fleet_post_raw_bits(const gk_fleet *F, const char *bot_id,
                         const char *bits01);
/* Refresh live status then write honest plate (pid/status/offline). */
int  fleet_save(gk_fleet *F, const char *path);
/* Defaults + overlay pids from plate; dead pids dropped. */
int  fleet_load(gk_fleet *F, const char *path);
#endif
