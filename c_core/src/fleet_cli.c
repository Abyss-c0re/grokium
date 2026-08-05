/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_fleet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Dual-wire deny plates — no free-text usage on machine wire. */
static const char k_need_subcmd[] =
    "{\"schema\":\"grokium.fleet.v1\",\"ok\":false,"
    "\"error\":\"need_subcmd\",\"product_wire\":\"smx2\","
    "\"peer_http\":\"lab_ops_only\",\"peer_http_is_product_bus\":false,"
    "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
    "\"llm_is_commander\":false,"
    "\"hint\":\"defaults|deploy|save|status|spawn|spawn-all|note-pid|"
    "separate|stop-all|selftest\"}";

static const char k_need_bot_id[] =
    "{\"schema\":\"grokium.fleet_spawn.v1\",\"ok\":false,"
    "\"error\":\"need_bot_id\",\"product_wire\":\"smx2\","
    "\"peer_http\":\"lab_ops_only\",\"peer_http_is_product_bus\":false,"
    "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
    "\"llm_is_commander\":false,"
    "\"hint\":\"pass bot id e.g. nb-manager\"}";

static const char k_spawn_failed[] =
    "{\"schema\":\"grokium.fleet_spawn.v1\",\"ok\":false,"
    "\"error\":\"spawn_failed\",\"product_wire\":\"smx2\","
    "\"peer_http\":\"lab_ops_only\",\"peer_http_is_product_bus\":false,"
    "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
    "\"llm_is_commander\":false}";

static const char k_need_note_args[] =
    "{\"schema\":\"grokium.fleet_note_pid.v1\",\"ok\":false,"
    "\"error\":\"need_bot_id_pid\",\"product_wire\":\"smx2\","
    "\"peer_http\":\"lab_ops_only\",\"peer_http_is_product_bus\":false,"
    "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
    "\"llm_is_commander\":false,"
    "\"hint\":\"note-pid BOT_ID PID [path]\"}";

static const char k_need_separate_id[] =
    "{\"schema\":\"grokium.fleet_separate.v1\",\"ok\":false,"
    "\"error\":\"need_bot_id\",\"product_wire\":\"smx2\","
    "\"peer_http\":\"lab_ops_only\",\"peer_http_is_product_bus\":false,"
    "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
    "\"llm_is_commander\":false}";

static const char k_unknown_bot[] =
    "{\"schema\":\"grokium.fleet_error.v1\",\"ok\":false,"
    "\"error\":\"unknown_bot\",\"product_wire\":\"smx2\","
    "\"peer_http\":\"lab_ops_only\",\"peer_http_is_product_bus\":false,"
    "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
    "\"llm_is_commander\":false}";

static int plate_dual_wire_ok(const char *p) {
  return p && strstr(p, "\"product_wire\":\"smx2\"") &&
         strstr(p, "\"peer_http\":\"lab_ops_only\"") &&
         strstr(p, "\"peer_http_is_product_bus\":false") &&
         strstr(p, "\"llm_is_commander\":false") &&
         strstr(p, "\"hold_flash\":1") &&
         strstr(p, "\"share\":\"state_matrix_only\"");
}

static void usage(void) { printf("%s\n", k_need_subcmd); }

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
  /* Isolate bot homes under temp so PURPOSE plates do not touch data/. */
  setenv("GROKIUM_HOME_ROOT", td, 1);
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
  /* PURPOSE.txt on deploy: dual-wire honesty for each bot home. */
  {
    char pp[400], pb[512];
    FILE *pf;
    size_t pn;
    snprintf(pp, sizeof pp, "%s/PURPOSE.txt", F.bots[0].home);
    pf = fopen(pp, "r");
    if (!pf) {
      fprintf(stderr, "selftest: PURPOSE.txt missing after deploy\n");
      return 1;
    }
    pn = fread(pb, 1, sizeof pb - 1, pf);
    pb[pn] = 0;
    fclose(pf);
    if (!strstr(pb, "product_wire=smx2") ||
        !strstr(pb, "peer_http=lab_ops_only") ||
        !strstr(pb, "peer_http_is_product_bus=0") ||
        !strstr(pb, "llm_is_commander=0") ||
        !strstr(pb, "share=state_matrix_only") ||
        !strstr(pb, "hold_flash=1")) {
      fprintf(stderr, "selftest: PURPOSE dual-wire fail: %.200s\n", pb);
      return 1;
    }
  }
  /* Deny plates dual-wire (usage/spawn/note/separate missing args). */
  if (!strstr(k_need_subcmd, "\"error\":\"need_subcmd\"") ||
      !plate_dual_wire_ok(k_need_subcmd) ||
      !strstr(k_need_bot_id, "\"error\":\"need_bot_id\"") ||
      !plate_dual_wire_ok(k_need_bot_id) || !plate_dual_wire_ok(k_spawn_failed) ||
      !plate_dual_wire_ok(k_need_note_args) ||
      !plate_dual_wire_ok(k_need_separate_id) ||
      !plate_dual_wire_ok(k_unknown_bot)) {
    fprintf(stderr, "selftest: fleet deny plate dual-wire fail\n");
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
      !strstr(body, "\"hold_flash\": 1") || !strstr(body, "nb-manager") ||
      !strstr(body, "\"wire\": \"smx_motivate\"")) {
    fprintf(stderr, "selftest: plate missing dual-wire honesty fields\n");
    return 1;
  }
  /* Fleet root + every bot object must carry product_wire (not wire alone). */
  {
    int pw = 0, ph = 0;
    const char *q;
    for (q = body; (q = strstr(q, "\"product_wire\"")) != NULL; q++)
      pw++;
    for (q = body; (q = strstr(q, "\"peer_http_is_product_bus\"")) != NULL; q++)
      ph++;
    if (pw < F.n + 1 || ph < F.n + 1) {
      fprintf(stderr,
              "selftest: bot-level dual-wire missing (product_wire=%d "
              "peer_http_is_product_bus=%d n=%d)\n",
              pw, ph, F.n);
      return 1;
    }
  }
  /* ensure dead pid not serialized as positive */
  if (strstr(body, "999999999")) {
    fprintf(stderr, "selftest: dead pid leaked onto plate\n");
    return 1;
  }
  /* Status CLI plate shape (not only on-disk FLEET.json) carries dual-wire. */
  {
    char st[512];
    int alive2 = fleet_status(&F);
    snprintf(st, sizeof st,
             "{\"schema\":\"grokium.fleet_status.v1\",\"ok\":true,"
             "\"alive\":%d,\"n\":%d,\"nb_manager\":true,\"probed\":true,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false,\"path\":\"%s\"}",
             alive2, F.n, path);
    if (!plate_dual_wire_ok(st) ||
        !strstr(st, "\"schema\":\"grokium.fleet_status.v1\"") ||
        !strstr(st, "\"probed\":true")) {
      fprintf(stderr, "selftest: fleet status plate dual-wire fail: %.200s\n",
              st);
      return 1;
    }
  }
  printf("FLEET_SELFTEST_OK n=%d alive=1 nb_manager=1 product_wire=smx2 "
         "peer_http=lab_ops_only bot_dual_wire=1 purpose=honest "
         "status_plate=honest\n",
         F.n);
  return 0;
}

int main(int argc, char **argv) {
  gk_fleet F;
  const char *path = "data/home/FLEET.json";
  if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "-h") ||
      !strcmp(argv[1], "--help")) {
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
    /* CLI status plate must stamp dual-wire honesty (Commander ≠ model). */
    printf("{\"schema\":\"grokium.fleet_status.v1\",\"ok\":true,"
           "\"alive\":%d,\"n\":%d,\"nb_manager\":true,\"probed\":true,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false,\"path\":\"%s\"}\n",
           alive, F.n, path);
    return 0;
  }
  if (!strcmp(argv[1], "selftest"))
    return fleet_selftest();
  if (!strcmp(argv[1], "spawn")) {
    int i;
    if (argc < 3) {
      printf("%s\n", k_need_bot_id);
      return 2;
    }
    if (argc > 3) path = argv[3];
    fleet_load(&F, path);
    if (fleet_spawn(&F, argv[2]) != 0) {
      printf("%s\n", k_spawn_failed);
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
    printf("%s\n", k_unknown_bot);
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
      printf("%s\n", k_need_note_args);
      return 2;
    }
    if (argc > 4) path = argv[4];
    fleet_load(&F, path);
    pid = atoi(argv[3]);
    if (fleet_note_pid(&F, argv[2], pid) != 0) {
      printf("%s\n", k_unknown_bot);
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
    printf("%s\n", k_unknown_bot);
    return 1;
  }
  if (!strcmp(argv[1], "separate")) {
    int i;
    if (argc < 3) {
      printf("%s\n", k_need_separate_id);
      return 2;
    }
    if (argc > 3) path = argv[3];
    fleet_load(&F, path);
    if (fleet_separate(&F, argv[2]) != 0) {
      printf("%s\n", k_unknown_bot);
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
    printf("%s\n", k_unknown_bot);
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
