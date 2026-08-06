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
/*
 * Dual-wire default-roles plate (schema grokium.fleet_defaults.v1).
 * Role templates only — not live pid/status (use fleet_status_json for that).
 * Replaces free-text TSV "id purpose port home" dump on CLI defaults.
 */
void fleet_defaults_json(const gk_fleet *F, char *out, size_t cap);
int  fleet_deploy(gk_fleet *F);
/* Probe PIDs with kill(0); clear dead; return alive count. */
int  fleet_status(gk_fleet *F);
/*
 * Dual-wire nanobot status plate (schema grokium.nanobot_status.v1).
 * Probes PIDs first; pid/status/offline honest; meta_only bots list (no logs).
 */
void fleet_status_json(gk_fleet *F, char *out, size_t cap);
/*
 * Dual-wire deploy ack plate (schema grokium.nanobot_deploy.v1).
 * Call after fleet_deploy + fleet_save; path is JSON-escaped on the plate.
 * Spawn remains host responsibility until fleet_spawn.
 */
void fleet_deploy_json(gk_fleet *F, const char *path, char *out, size_t cap);
/*
 * Dual-wire spawn ack (schema grokium.nanobot_spawn.v1).
 * Call after fleet_spawn / fleet_spawn_all + fleet_save.
 * id is bot token or "*" for spawn-all; spawned is count started.
 * Single-bot plates include pid/running/status/wire honesty.
 */
void fleet_spawn_json(gk_fleet *F, const char *id, int spawned,
                      const char *path, char *out, size_t cap);
/* Dual-wire spawn deny (spawn_failed | spawn_all_failed | no_fleet | need_bot_id). */
void fleet_spawn_err_json(const char *error, char *out, size_t cap);
/*
 * Dual-wire separate ack (schema grokium.nanobot_separate.v1).
 * Call after fleet_separate + fleet_save; path is JSON-escaped.
 */
void fleet_separate_json(const char *id, const char *path, char *out,
                         size_t cap);
/* Dual-wire separate deny (need_bot_id | unknown_bot | no_fleet). */
void fleet_separate_err_json(const char *error, char *out, size_t cap);
/*
 * Dual-wire note-pid ack (schema grokium.nanobot_note_pid.v1).
 * Call after fleet_note_pid + fleet_save; honest pid/running/status/wire.
 * Dead pids appear as null + separated (host/hub external spawn record).
 */
void fleet_note_pid_json(gk_fleet *F, const char *id, const char *path,
                         char *out, size_t cap);
/* Dual-wire note-pid deny (need_bot_id_pid | unknown_bot | no_fleet). */
void fleet_note_pid_err_json(const char *error, char *out, size_t cap);
/*
 * Dual-wire stop-all ack (schema grokium.nanobot_stop.v1).
 * Call after fleet_stop_all + fleet_save; path escaped; alive re-probed.
 */
void fleet_stop_json(gk_fleet *F, const char *path, char *out, size_t cap);
/* Dual-wire stop-all deny (no_fleet). */
void fleet_stop_err_json(const char *error, char *out, size_t cap);
/*
 * Dual-wire save ack (schema grokium.nanobot_save.v1).
 * Call after fleet_save; path escaped; alive re-probed (honest plate write).
 */
void fleet_save_json(gk_fleet *F, const char *path, char *out, size_t cap);
/* Dual-wire save deny (no_fleet). */
void fleet_save_err_json(const char *error, char *out, size_t cap);
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
