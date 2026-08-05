/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_fleet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(void) {
  fprintf(stderr,
          "grokium-fleet defaults|deploy|save [path]|status [path]|"
          "spawn ID [path]|spawn-all [path]|"
          "note-pid ID PID [path]|separate ID [path]|stop-all [path]|"
          "selftest\n"
          "  defaults  — print roles including nb-manager\n"
          "  deploy    — mkdir homes; clear live pids on plate\n"
          "  save      — write honest FLEET.json (loads plate first)\n"
          "  status    — load plate, kill(0) probe, rewrite honest plate\n"
          "  spawn     — fork/exec NANOBOT_BIN (peer HTTP = lab_ops)\n"
          "  spawn-all — spawn every role\n"
          "  note-pid  — record spawn pid; preserves other bots\n"
          "  separate  — SIGTERM bot if live; mark separated\n"
          "  stop-all  — SIGTERM all live bots\n"
          "  selftest  — pure-C pid/status honesty (no spawn)\n");
}

/* Pure-C plate honesty: defaults, note-pid, dead-pid clear, dual-wire fields. */
static int fleet_selftest(void) {
  gk_fleet F;
  char dir[] = "/tmp/gk_fleet_selftest_XXXXXX";
  char path[320], body[8192];
  char *td;
  FILE *f;
  size_t n;
  int alive, i, self_pid, has_mgr = 0, dead = 999999999;
  int live_bot = -1;

  td = mkdtemp(dir);
  if (!td) {
    fprintf(stderr, "selftest: mkdtemp failed\n");
    return 1;
  }
  snprintf(path, sizeof path, "%s/FLEET.json", td);
  fleet_default_roles(&F);
  if (F.n < 6) {
    fprintf(stderr, "selftest: expected >=6 default roles\n");
    return 1;
  }
  for (i = 0; i < F.n; i++)
    if (!strcmp(F.bots[i].id, "nb-manager")) has_mgr = 1;
  if (!has_mgr) {
    fprintf(stderr, "selftest: nb-manager missing from defaults\n");
    return 1;
  }
  if (fleet_deploy(&F) != 0 || fleet_save(&F, path) != 0) {
    fprintf(stderr, "selftest: deploy/save failed\n");
    return 1;
  }
  self_pid = (int)getpid();
  if (fleet_note_pid(&F, "nb-manager", self_pid) != 0) {
    fprintf(stderr, "selftest: note-pid live failed\n");
    return 1;
  }
  if (fleet_note_pid(&F, "nb-host", dead) != 0) {
    fprintf(stderr, "selftest: note-pid dead failed\n");
    return 1;
  }
  /* dead pid must clear immediately */
  for (i = 0; i < F.n; i++) {
    if (!strcmp(F.bots[i].id, "nb-host") && F.bots[i].pid > 0) {
      fprintf(stderr, "selftest: dead pid not cleared on note\n");
      return 1;
    }
    if (!strcmp(F.bots[i].id, "nb-manager")) {
      if (F.bots[i].pid != self_pid || !F.bots[i].running) {
        fprintf(stderr, "selftest: live self pid not running\n");
        return 1;
      }
      live_bot = i;
    }
  }
  if (live_bot < 0) {
    fprintf(stderr, "selftest: nb-manager not found after note\n");
    return 1;
  }
  if (fleet_save(&F, path) != 0) {
    fprintf(stderr, "selftest: save after note failed\n");
    return 1;
  }
  memset(&F, 0, sizeof F);
  if (fleet_load(&F, path) != 0) {
    fprintf(stderr, "selftest: reload failed\n");
    return 1;
  }
  alive = fleet_status(&F);
  if (alive != 1) {
    fprintf(stderr, "selftest: expected alive=1 got %d\n", alive);
    return 1;
  }
  /* rewrite honest plate (status path) */
  if (fleet_save(&F, path) != 0) return 1;
  f = fopen(path, "r");
  if (!f) {
    fprintf(stderr, "selftest: reopen plate failed\n");
    return 1;
  }
  n = fread(body, 1, sizeof body - 1, f);
  body[n] = 0;
  fclose(f);
  if (!strstr(body, "\"share\": \"state_matrix_only\"") ||
      !strstr(body, "\"product_wire\": \"smx2\"") ||
      !strstr(body, "\"peer_http\": \"lab_ops_only\"") ||
      !strstr(body, "\"peer_http_is_product_bus\": false") ||
      !strstr(body, "\"llm_is_commander\": false") ||
      !strstr(body, "\"commander_is_model\": false") ||
      !strstr(body, "\"hold_flash\": 1") || !strstr(body, "nb-manager")) {
    fprintf(stderr, "selftest: plate missing dual-wire honesty fields\n");
    return 1;
  }
  /* ensure dead pid not serialized as positive */
  if (strstr(body, "999999999")) {
    fprintf(stderr, "selftest: dead pid leaked onto plate\n");
    return 1;
  }
  printf("FLEET_SELFTEST_OK n=%d alive=1 nb_manager=1 product_wire=smx2 "
         "peer_http=lab_ops_only\n",
         F.n);
  return 0;
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
    /* CLI ack: dual-wire honesty (on-disk plate already carries these). */
    printf("{\"schema\":\"grokium.fleet_deploy.v1\",\"ok\":true,"
           "\"deployed\":%d,\"path\":\"%s\",\"nb_manager\":true,"
           "\"alive\":%d,\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false}\n",
           F.n, path, fleet_status(&F));
    return 0;
  }
  if (!strcmp(argv[1], "save")) {
    if (argc > 2) path = argv[2];
    fleet_load(&F, path);
    fleet_save(&F, path);
    printf("{\"schema\":\"grokium.fleet_save.v1\",\"ok\":true,"
           "\"saved\":\"%s\",\"n\":%d,\"alive\":%d,\"nb_manager\":true,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false}\n",
           path, F.n, fleet_status(&F));
    return 0;
  }
  if (!strcmp(argv[1], "status")) {
    int alive;
    if (argc > 2) path = argv[2];
    fleet_load(&F, path);
    alive = fleet_status(&F);
    /* Persist kill(0) result so on-disk plate stays honest. */
    (void)fleet_save(&F, path);
    printf("{\"schema\":\"grokium.fleet_status.v1\",\"ok\":true,"
           "\"alive\":%d,\"n\":%d,\"nb_manager\":true,\"probed\":true,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"path\":\"%s\"}\n",
           alive, F.n, path);
    return 0;
  }
  if (!strcmp(argv[1], "selftest"))
    return fleet_selftest();
  if (!strcmp(argv[1], "spawn")) {
    int i;
    if (argc < 3) {
      fprintf(stderr, "usage: grokium-fleet spawn BOT_ID [path]\n");
      return 2;
    }
    if (argc > 3) path = argv[3];
    fleet_load(&F, path);
    if (fleet_spawn(&F, argv[2]) != 0) {
      fprintf(stderr, "spawn failed (binary=%s id=%s)\n", F.binary, argv[2]);
      return 1;
    }
    fleet_save(&F, path);
    for (i = 0; i < F.n; i++) {
      if (strcmp(F.bots[i].id, argv[2]) != 0) continue;
      /* Peer HTTP on bot ports is lab/ops; product talk stays SMX2. */
      printf("{\"schema\":\"grokium.fleet_spawn.v1\",\"ok\":true,"
             "\"id\":\"%s\",\"pid\":%d,\"running\":%s,"
             "\"status\":\"%s\",\"path\":\"%s\",\"wire\":\"%s\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false}\n",
             F.bots[i].id, F.bots[i].pid > 0 ? F.bots[i].pid : 0,
             F.bots[i].running ? "true" : "false",
             F.bots[i].running ? "running" : "separated", path,
             strcmp(F.bots[i].id, "nb-manager") == 0 ? "smx_motivate" : "smx2");
      return 0;
    }
    return 1;
  }
  if (!strcmp(argv[1], "spawn-all")) {
    int n;
    if (argc > 2) path = argv[2];
    fleet_load(&F, path);
    n = fleet_spawn_all(&F);
    fleet_save(&F, path);
    if (n < 0) return 1;
    printf("{\"schema\":\"grokium.fleet_spawn_all.v1\",\"ok\":true,"
           "\"spawned\":%d,\"n\":%d,\"alive\":%d,\"path\":\"%s\","
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false}\n",
           n, F.n, fleet_status(&F), path);
    return n > 0 ? 0 : 1;
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
      printf("{\"schema\":\"grokium.fleet_note_pid.v1\",\"ok\":true,"
             "\"id\":\"%s\",\"pid\":%s,\"running\":%s,"
             "\"status\":\"%s\",\"path\":\"%s\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false}\n",
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
      printf("{\"schema\":\"grokium.fleet_separate.v1\",\"ok\":true,"
             "\"id\":\"%s\",\"status\":\"separated\","
             "\"pid\":null,\"path\":\"%s\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false}\n",
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
    printf("{\"schema\":\"grokium.fleet_stop.v1\",\"ok\":true,"
           "\"stopped\":true,\"n\":%d,\"path\":\"%s\",\"alive\":0,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false}\n",
           F.n, path);
    return 0;
  }
  usage();
  return 2;
}
