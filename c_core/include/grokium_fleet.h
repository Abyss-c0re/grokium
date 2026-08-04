/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GROKIUM_FLEET_H
#define GROKIUM_FLEET_H
/* Separable nanobot subagents — purpose assigned, Cube Standards */
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
/* Host/hub records a spawned bot pid (or -1 to clear). */
int  fleet_note_pid(gk_fleet *F, const char *bot_id, int pid);
int  fleet_separate(gk_fleet *F, const char *bot_id);
int  fleet_stop_all(gk_fleet *F);
int  fleet_post_raw_bits(const gk_fleet *F, const char *bot_id,
                         const char *bits01);
/* Refresh live status then write honest plate (pid/status/offline). */
int  fleet_save(gk_fleet *F, const char *path);
int  fleet_load(gk_fleet *F, const char *path);
#endif
