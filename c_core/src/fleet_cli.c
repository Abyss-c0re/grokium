/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_fleet.h"
#include "grokium_law.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Shared dual-wire need_subcmd (host TUI /fleet help same builder). */
static const char k_fleet_hint[] =
    "defaults|deploy|save|status|spawn|spawn-all|note-pid|"
    "separate|stop-all|selftest";

/* unknown_bot for generic fleet denials (note-pid uses schema-scoped helper). */
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

static void usage(void) {
  char plate[512];
  grokium_need_subcmd_json("fleet", k_fleet_hint, plate, sizeof plate);
  printf("%s\n", plate);
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
  {
    char den[512];
    grokium_need_subcmd_json("fleet", k_fleet_hint, den, sizeof den);
    if (!strstr(den, "\"error\":\"need_subcmd\"") ||
        !strstr(den, "\"schema\":\"grokium.fleet.v1\"") ||
        !plate_dual_wire_ok(den) || !plate_dual_wire_ok(k_unknown_bot)) {
      fprintf(stderr, "selftest: fleet deny plate dual-wire fail\n");
      return 1;
    }
    fleet_spawn_err_json("need_bot_id", den, sizeof den);
    if (!strstr(den, "\"schema\":\"grokium.nanobot_spawn.v1\"") ||
        !strstr(den, "\"error\":\"need_bot_id\"") || !plate_dual_wire_ok(den)) {
      fprintf(stderr, "selftest: spawn need_bot_id dual-wire fail: %.200s\n",
              den);
      return 1;
    }
    fleet_spawn_err_json("spawn_failed", den, sizeof den);
    if (!strstr(den, "\"schema\":\"grokium.nanobot_spawn.v1\"") ||
        !strstr(den, "\"error\":\"spawn_failed\"") ||
        !plate_dual_wire_ok(den)) {
      fprintf(stderr, "selftest: spawn_failed dual-wire fail: %.200s\n", den);
      return 1;
    }
    fleet_separate_err_json("need_bot_id", den, sizeof den);
    if (!strstr(den, "\"schema\":\"grokium.nanobot_separate.v1\"") ||
        !strstr(den, "\"error\":\"need_bot_id\"") || !plate_dual_wire_ok(den)) {
      fprintf(stderr, "selftest: separate need_bot_id dual-wire fail: %.200s\n",
              den);
      return 1;
    }
    fleet_separate_err_json("unknown_bot", den, sizeof den);
    if (!strstr(den, "\"schema\":\"grokium.nanobot_separate.v1\"") ||
        !strstr(den, "\"error\":\"unknown_bot\"") || !plate_dual_wire_ok(den)) {
      fprintf(stderr, "selftest: separate unknown_bot dual-wire fail: %.200s\n",
              den);
      return 1;
    }
    fleet_note_pid_err_json("need_bot_id_pid", den, sizeof den);
    if (!strstr(den, "\"schema\":\"grokium.nanobot_note_pid.v1\"") ||
        !strstr(den, "\"error\":\"need_bot_id_pid\"") ||
        !plate_dual_wire_ok(den)) {
      fprintf(stderr, "selftest: note_pid need dual-wire fail: %.200s\n", den);
      return 1;
    }
    fleet_note_pid_err_json("unknown_bot", den, sizeof den);
    if (!strstr(den, "\"schema\":\"grokium.nanobot_note_pid.v1\"") ||
        !strstr(den, "\"error\":\"unknown_bot\"") || !plate_dual_wire_ok(den)) {
      fprintf(stderr, "selftest: note_pid unknown dual-wire fail: %.200s\n",
              den);
      return 1;
    }
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
  /* On-disk FLEET.json must JSON-escape path inject (home_root/binary). */
  {
    char inj_path[512], inj_body[4096];
    FILE *injf;
    snprintf(F.home_root, sizeof F.home_root, "data/h\"ome");
    snprintf(F.binary, sizeof F.binary, "nano\\bot");
    snprintf(inj_path, sizeof inj_path, "%s/FLEET_inj.json", td);
    if (fleet_save(&F, inj_path) != 0) {
      fprintf(stderr, "selftest: inject save failed\n");
      return 1;
    }
    injf = fopen(inj_path, "r");
    if (!injf) {
      fprintf(stderr, "selftest: inject plate open failed\n");
      return 1;
    }
    n = fread(inj_body, 1, sizeof inj_body - 1, injf);
    inj_body[n] = 0;
    fclose(injf);
    if (!strstr(inj_body, "data/h\\\"ome") ||
        !strstr(inj_body, "nano\\\\bot") ||
        strstr(inj_body, "\"home_root\": \"data/h\"ome\"") ||
        !strstr(inj_body, "\"product_wire\": \"smx2\"")) {
      fprintf(stderr, "selftest: FLEET path inject not escaped: %.300s\n",
              inj_body);
      return 1;
    }
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
  /* CLI status + HTTP /v1/nanobot/status share fleet_status_json. */
  {
    char st[4096];
    fleet_status_json(&F, st, sizeof st);
    if (!plate_dual_wire_ok(st) ||
        !strstr(st, "\"schema\":\"grokium.nanobot_status.v1\"") ||
        !strstr(st, "\"ok\":true") || !strstr(st, "\"nb_manager\":true") ||
        !strstr(st, "\"commander_is_model\":false") ||
        !strstr(st, "nb-manager") || !strstr(st, "\"wire\":\"smx_motivate\"") ||
        !strstr(st, "\"bots\":[") || strstr(st, "\"wire_product\"") ||
        strstr(st, "999999999")) {
      fprintf(stderr, "selftest: fleet_status_json dual-wire fail: %.300s\n",
              st);
      return 1;
    }
  }
  /* CLI deploy + HTTP /v1/nanobot/deploy share fleet_deploy_json. */
  {
    char st[1024];
    fleet_deploy_json(&F, path, st, sizeof st);
    if (!plate_dual_wire_ok(st) ||
        !strstr(st, "\"schema\":\"grokium.nanobot_deploy.v1\"") ||
        !strstr(st, "\"ok\":true") || !strstr(st, "\"nb_manager\":true") ||
        !strstr(st, "\"spawn\":\"host_responsibility\"") ||
        !strstr(st, "\"wire\":\"smx2\"")) {
      fprintf(stderr, "selftest: fleet_deploy_json dual-wire fail: %.250s\n",
              st);
      return 1;
    }
  }
  /* CLI spawn/separate + HTTP share nanobot_spawn / nanobot_separate plates. */
  {
    char st[1024];
    fleet_spawn_json(&F, "nb-manager", 1, path, st, sizeof st);
    if (!plate_dual_wire_ok(st) ||
        !strstr(st, "\"schema\":\"grokium.nanobot_spawn.v1\"") ||
        !strstr(st, "\"ok\":true") || !strstr(st, "\"id\":\"nb-manager\"") ||
        !strstr(st, "\"wire\":\"smx_motivate\"") ||
        !strstr(st, "\"spawned\":1") || !strstr(st, "\"pid\":")) {
      fprintf(stderr, "selftest: fleet_spawn_json dual-wire fail: %.250s\n",
              st);
      return 1;
    }
    fleet_spawn_json(&F, "*", 0, path, st, sizeof st);
    if (!plate_dual_wire_ok(st) ||
        !strstr(st, "\"schema\":\"grokium.nanobot_spawn.v1\"") ||
        !strstr(st, "\"id\":\"*\"") || !strstr(st, "\"alive\":")) {
      fprintf(stderr, "selftest: fleet_spawn_json * dual-wire fail: %.250s\n",
              st);
      return 1;
    }
    fleet_separate_json("nb-host", path, st, sizeof st);
    if (!plate_dual_wire_ok(st) ||
        !strstr(st, "\"schema\":\"grokium.nanobot_separate.v1\"") ||
        !strstr(st, "\"ok\":true") || !strstr(st, "\"id\":\"nb-host\"") ||
        !strstr(st, "\"status\":\"separated\"") ||
        !strstr(st, "\"pid\":null") || !strstr(st, "\"wire\":\"smx2\"")) {
      fprintf(stderr, "selftest: fleet_separate_json dual-wire fail: %.250s\n",
              st);
      return 1;
    }
    /* note-pid: live manager honest; dead host cleared to null. */
    fleet_note_pid_json(&F, "nb-manager", path, st, sizeof st);
    if (!plate_dual_wire_ok(st) ||
        !strstr(st, "\"schema\":\"grokium.nanobot_note_pid.v1\"") ||
        !strstr(st, "\"ok\":true") || !strstr(st, "\"id\":\"nb-manager\"") ||
        !strstr(st, "\"wire\":\"smx_motivate\"") ||
        !strstr(st, "\"running\":true") || !strstr(st, "\"pid\":")) {
      fprintf(stderr, "selftest: fleet_note_pid_json live fail: %.250s\n", st);
      return 1;
    }
    fleet_note_pid_json(&F, "nb-host", path, st, sizeof st);
    if (!plate_dual_wire_ok(st) ||
        !strstr(st, "\"schema\":\"grokium.nanobot_note_pid.v1\"") ||
        !strstr(st, "\"pid\":null") || !strstr(st, "\"running\":false") ||
        !strstr(st, "\"status\":\"separated\"")) {
      fprintf(stderr, "selftest: fleet_note_pid_json dead fail: %.250s\n", st);
      return 1;
    }
    /* stop-all plate: dual-wire + honest alive after clear.
     * Drop noted self-pid first so fleet_stop_all does not SIGTERM us. */
    for (i = 0; i < F.n; i++)
      (void)fleet_note_pid(&F, F.bots[i].id, -1);
    fleet_stop_all(&F);
    fleet_stop_json(&F, path, st, sizeof st);
    if (!plate_dual_wire_ok(st) ||
        !strstr(st, "\"schema\":\"grokium.nanobot_stop.v1\"") ||
        !strstr(st, "\"ok\":true") || !strstr(st, "\"stopped\":true") ||
        !strstr(st, "\"alive\":0") || !strstr(st, "\"nb_manager\":true")) {
      fprintf(stderr, "selftest: fleet_stop_json dual-wire fail: %.250s\n", st);
      return 1;
    }
    fleet_save_json(&F, path, st, sizeof st);
    if (!plate_dual_wire_ok(st) ||
        !strstr(st, "\"schema\":\"grokium.nanobot_save.v1\"") ||
        !strstr(st, "\"ok\":true") || !strstr(st, "\"saved\":") ||
        !strstr(st, "\"nb_manager\":true") || !strstr(st, "\"alive\":0")) {
      fprintf(stderr, "selftest: fleet_save_json dual-wire fail: %.250s\n", st);
      return 1;
    }
  }
  printf("FLEET_SELFTEST_OK n=%d alive=0 nb_manager=1 product_wire=smx2 "
         "peer_http=lab_ops_only bot_dual_wire=1 purpose=honest "
         "status_plate=nanobot_status_v1 deploy_plate=nanobot_deploy_v1 "
         "spawn_plate=nanobot_spawn_v1 separate_plate=nanobot_separate_v1 "
         "note_pid_plate=nanobot_note_pid_v1 stop_plate=nanobot_stop_v1 "
         "save_plate=nanobot_save_v1\n",
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
    char plate[1024];
    if (argc > 2) path = argv[2];
    fleet_default_roles(&F);
    fleet_deploy(&F);
    fleet_save(&F, path);
    /* Same dual-wire deploy plate as POST /v1/nanobot/deploy. */
    fleet_deploy_json(&F, path, plate, sizeof plate);
    printf("%s\n", plate);
    return 0;
  }
  if (!strcmp(argv[1], "save")) {
    char plate[1024];
    if (argc > 2) path = argv[2];
    fleet_load(&F, path);
    fleet_save(&F, path);
    /* Same dual-wire plate as POST /v1/nanobot/save. */
    fleet_save_json(&F, path, plate, sizeof plate);
    printf("%s\n", plate);
    return 0;
  }
  if (!strcmp(argv[1], "status")) {
    char plate[4096];
    if (argc > 2) path = argv[2];
    fleet_load(&F, path);
    /* Persist kill(0) result so on-disk FLEET.json stays honest. */
    (void)fleet_status(&F);
    (void)fleet_save(&F, path);
    /* Same dual-wire plate as GET /v1/nanobot/status (bots pid/status). */
    fleet_status_json(&F, plate, sizeof plate);
    printf("%s\n", plate);
    return 0;
  }
  if (!strcmp(argv[1], "selftest"))
    return fleet_selftest();
  if (!strcmp(argv[1], "spawn")) {
    char plate[1024];
    if (argc < 3) {
      fleet_spawn_err_json("need_bot_id", plate, sizeof plate);
      printf("%s\n", plate);
      return 2;
    }
    if (argc > 3) path = argv[3];
    fleet_load(&F, path);
    if (fleet_spawn(&F, argv[2]) != 0) {
      fleet_spawn_err_json("spawn_failed", plate, sizeof plate);
      printf("%s\n", plate);
      return 1;
    }
    fleet_save(&F, path);
    /* Same dual-wire plate as POST /v1/nanobot/spawn. */
    fleet_spawn_json(&F, argv[2], 1, path, plate, sizeof plate);
    printf("%s\n", plate);
    return 0;
  }
  if (!strcmp(argv[1], "spawn-all")) {
    char plate[1024];
    int n;
    if (argc > 2) path = argv[2];
    fleet_load(&F, path);
    n = fleet_spawn_all(&F);
    if (n < 0) {
      fleet_spawn_err_json("spawn_all_failed", plate, sizeof plate);
      printf("%s\n", plate);
      return 1;
    }
    fleet_save(&F, path);
    /* Shared nanobot_spawn plate (id="*") — same as HTTP empty-body spawn. */
    fleet_spawn_json(&F, "*", n, path, plate, sizeof plate);
    printf("%s\n", plate);
    return n > 0 ? 0 : 1;
  }
  if (!strcmp(argv[1], "note-pid")) {
    char plate[1024];
    int pid;
    if (argc < 4) {
      fleet_note_pid_err_json("need_bot_id_pid", plate, sizeof plate);
      printf("%s\n", plate);
      return 2;
    }
    if (argc > 4) path = argv[4];
    fleet_load(&F, path);
    pid = atoi(argv[3]);
    if (fleet_note_pid(&F, argv[2], pid) != 0) {
      fleet_note_pid_err_json("unknown_bot", plate, sizeof plate);
      printf("%s\n", plate);
      return 1;
    }
    fleet_save(&F, path);
    /* Same dual-wire plate as POST /v1/nanobot/note-pid. */
    fleet_note_pid_json(&F, argv[2], path, plate, sizeof plate);
    printf("%s\n", plate);
    return 0;
  }
  if (!strcmp(argv[1], "separate")) {
    char plate[1024];
    if (argc < 3) {
      fleet_separate_err_json("need_bot_id", plate, sizeof plate);
      printf("%s\n", plate);
      return 2;
    }
    if (argc > 3) path = argv[3];
    fleet_load(&F, path);
    if (fleet_separate(&F, argv[2]) != 0) {
      fleet_separate_err_json("unknown_bot", plate, sizeof plate);
      printf("%s\n", plate);
      return 1;
    }
    fleet_save(&F, path);
    /* Same dual-wire plate as POST /v1/nanobot/separate. */
    fleet_separate_json(argv[2], path, plate, sizeof plate);
    printf("%s\n", plate);
    return 0;
  }
  if (!strcmp(argv[1], "stop-all")) {
    char plate[1024];
    if (argc > 2) path = argv[2];
    fleet_load(&F, path);
    fleet_stop_all(&F);
    fleet_save(&F, path);
    /* Same dual-wire plate as POST /v1/nanobot/stop-all. */
    fleet_stop_json(&F, path, plate, sizeof plate);
    printf("%s\n", plate);
    return 0;
  }
  usage();
  return 2;
}
