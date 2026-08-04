/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_fleet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
  fprintf(stderr,
          "grokium-fleet defaults|deploy|save [path]|status [path]|"
          "note-pid ID PID [path]|separate ID [path]|stop-all [path]\n"
          "  defaults  — print roles including nb-manager\n"
          "  deploy    — mkdir homes; clear live pids on plate\n"
          "  save      — write honest FLEET.json (loads plate first)\n"
          "  status    — load plate, kill(0) probe, alive count\n"
          "  note-pid  — record spawn pid; preserves other bots\n"
          "  separate  — SIGTERM bot if live; mark separated\n"
          "  stop-all  — SIGTERM all live bots\n");
}

int main(int argc, char **argv) {
  gk_fleet F;
  const char *path = "data/home/FLEET.json";
  if (argc < 2) {
    usage();
    return 2;
  }
  if (!strcmp(argv[1], "defaults")) {
    int i;
    fleet_default_roles(&F);
    for (i = 0; i < F.n; i++)
      printf("%s\t%s\t%d\t%s\n", F.bots[i].id, F.bots[i].purpose,
             F.bots[i].port, F.bots[i].home);
    return 0;
  }
  if (!strcmp(argv[1], "deploy")) {
    if (argc > 2) path = argv[2];
    fleet_default_roles(&F);
    fleet_deploy(&F);
    fleet_save(&F, path);
    printf("{\"ok\":true,\"deployed\":%d,\"path\":\"%s\",\"nb_manager\":true}\n",
           F.n, path);
    return 0;
  }
  if (!strcmp(argv[1], "save")) {
    if (argc > 2) path = argv[2];
    fleet_load(&F, path);
    fleet_save(&F, path);
    printf("{\"ok\":true,\"saved\":\"%s\",\"n\":%d,\"alive\":%d}\n", path, F.n,
           fleet_status(&F));
    return 0;
  }
  if (!strcmp(argv[1], "status")) {
    int alive;
    if (argc > 2) path = argv[2];
    fleet_load(&F, path);
    alive = fleet_status(&F);
    printf("{\"alive\":%d,\"n\":%d,\"nb_manager\":true,\"probed\":true,"
           "\"path\":\"%s\"}\n",
           alive, F.n, path);
    return 0;
  }
  if (!strcmp(argv[1], "note-pid")) {
    int pid, i;
    if (argc < 4) {
      fprintf(stderr, "usage: grokium-fleet note-pid BOT_ID PID [path]\n");
      return 2;
    }
    if (argc > 4) path = argv[4];
    fleet_load(&F, path);
    pid = atoi(argv[3]);
    if (fleet_note_pid(&F, argv[2], pid) != 0) {
      fprintf(stderr, "unknown bot id\n");
      return 1;
    }
    fleet_save(&F, path);
    for (i = 0; i < F.n; i++) {
      if (strcmp(F.bots[i].id, argv[2]) != 0) continue;
      printf("{\"ok\":true,\"id\":\"%s\",\"pid\":%s,\"running\":%s,"
             "\"status\":\"%s\",\"path\":\"%s\"}\n",
             F.bots[i].id, F.bots[i].pid > 0 ? argv[3] : "null",
             F.bots[i].running ? "true" : "false",
             F.bots[i].running ? "running" : "separated", path);
      return 0;
    }
    return 1;
  }
  if (!strcmp(argv[1], "separate")) {
    int i;
    if (argc < 3) {
      fprintf(stderr, "usage: grokium-fleet separate BOT_ID [path]\n");
      return 2;
    }
    if (argc > 3) path = argv[3];
    fleet_load(&F, path);
    if (fleet_separate(&F, argv[2]) != 0) {
      fprintf(stderr, "unknown bot id\n");
      return 1;
    }
    fleet_save(&F, path);
    for (i = 0; i < F.n; i++) {
      if (strcmp(F.bots[i].id, argv[2]) != 0) continue;
      printf("{\"ok\":true,\"id\":\"%s\",\"status\":\"separated\","
             "\"pid\":null,\"path\":\"%s\"}\n",
             F.bots[i].id, path);
      return 0;
    }
    return 1;
  }
  if (!strcmp(argv[1], "stop-all")) {
    if (argc > 2) path = argv[2];
    fleet_load(&F, path);
    fleet_stop_all(&F);
    fleet_save(&F, path);
    printf("{\"ok\":true,\"stopped\":true,\"n\":%d,\"path\":\"%s\"}\n", F.n,
           path);
    return 0;
  }
  usage();
  return 2;
}
