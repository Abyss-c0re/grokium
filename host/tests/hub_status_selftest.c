/* SPDX-License-Identifier: Apache-2.0
 * Pure-C selftest for hub dual-wire status plate (no nanobot, no ncurses).
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_hub.h"
#include "grokium.h"
#include "grokium_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* grokium_hub.c expects host path globals from grokium.h */
char root[PATH_MAX];
char cubalc_bin[PATH_MAX];
char state_dir[PATH_MAX];
char prog_dir[PATH_MAX];
char nanobot_root[PATH_MAX];
void resolve_paths(void) {}
int file_ok(const char *p) {
  (void)p;
  return 0;
}

static int fail(const char *msg) {
  fprintf(stderr, "hub_status_selftest: %s\n", msg);
  return 1;
}

int main(void) {
  char tmp[] = "/tmp/gkx_hub_status_XXXXXX";
  char *td = mkdtemp(tmp);
  char plate[768], pidp[PATH_MAX];
  FILE *f;
  int rc;

  if (!td) return fail("mkdtemp");
  snprintf(root, sizeof root, "%s", td);
  snprintf(pidp, sizeof pidp, "%s/data/hub", td);
  if (mkdir(pidp, 0700) != 0) {
    /* parents */
    snprintf(pidp, sizeof pidp, "%s/data", td);
    if (mkdir(pidp, 0700) != 0) return fail("mkdir data");
    snprintf(pidp, sizeof pidp, "%s/data/hub", td);
    if (mkdir(pidp, 0700) != 0) return fail("mkdir hub");
  }

  /* No pid / no http → ok=false plate must still carry dual-wire honesty. */
  unsetenv("NANOBOT_LLM_LOCK");
  setenv("NANOBOT_LLM_SLOTS", "1", 1);
  rc = gkx_hub_status(plate, sizeof plate);
  if (rc != 1) return fail("expected unhealthy return 1");
  if (!strstr(plate, "\"schema\":\"grokium.hub_status.v1\"") ||
      !strstr(plate, "\"ok\":false") ||
      !strstr(plate, "\"product_wire\":\"smx2\"") ||
      !strstr(plate, "\"peer_http\":\"lab_ops_only\"") ||
      !strstr(plate, "\"peer_http_is_product_bus\":false") ||
      !strstr(plate, "\"llm_is_commander\":false") ||
      !strstr(plate, "\"share\":\"state_matrix_only\"") ||
      !strstr(plate, "\"hold_flash\":1") ||
      !strstr(plate, "\"control_plane\":\"host_hub\"") ||
      !strstr(plate, "\"python\":0")) {
    fprintf(stderr, "hub_status_selftest: plate honesty fail: %s\n", plate);
    return 1;
  }

  /* Honest pid plate: note self pid, still may be http=false (no hub server). */
  snprintf(pidp, sizeof pidp, "%s/data/hub/nanobot.pid", td);
  f = fopen(pidp, "w");
  if (!f) return fail("write pid");
  fprintf(f, "%d\n", (int)getpid());
  fclose(f);
  rc = gkx_hub_status(plate, sizeof plate);
  if (!strstr(plate, "\"alive\":true") ||
      !strstr(plate, "\"product_wire\":\"smx2\"") ||
      !strstr(plate, "\"peer_http_is_product_bus\":false")) {
    fprintf(stderr, "hub_status_selftest: live pid plate fail: %s\n", plate);
    return 1;
  }
  /* Without peer health, overall ok stays false. */
  if (rc != 1 || !strstr(plate, "\"ok\":false"))
    return fail("alive without http should not report ok=true");

  if (gkx_hub_status(plate, 8) == 0 && strlen(plate) >= 8)
    return fail("tiny buffer should not claim full health plate");

  /* Shared stop ack plate (CLI + TUI /hub stop). */
  gkx_hub_stop_json(plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.hub_status.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"stopped\":true") ||
      !strstr(plate, "\"alive\":false") || !strstr(plate, "\"http\":false") ||
      !strstr(plate, "\"product_wire\":\"smx2\"") ||
      !strstr(plate, "\"peer_http\":\"lab_ops_only\"") ||
      !strstr(plate, "\"peer_http_is_product_bus\":false") ||
      !strstr(plate, "\"llm_is_commander\":false") ||
      !strstr(plate, "\"hold_flash\":1") ||
      !strstr(plate, "\"control_plane\":\"host_hub\"")) {
    fprintf(stderr, "hub_status_selftest: stop plate fail: %s\n", plate);
    return 1;
  }

  /* Hostile lock/slots env must not break the dual-wire JSON plate. */
  setenv("NANOBOT_LLM_LOCK", "/tmp/lk\"evil\\x", 1);
  setenv("NANOBOT_LLM_SLOTS", "2\";drop", 1);
  (void)gkx_hub_status(plate, sizeof plate);
  if (strstr(plate, "lk\"evil") || strstr(plate, "evil\\x") ||
      strstr(plate, "2\";drop") || strstr(plate, "\"drop\"") ||
      !strstr(plate, "\"lock\":\"/tmp/lkevilx\"") ||
      !strstr(plate, "\"slots\":\"2;drop\"") ||
      !strstr(plate, "\"product_wire\":\"smx2\"") ||
      !strstr(plate, "\"peer_http_is_product_bus\":false") ||
      !strstr(plate, "\"llm_is_commander\":false") ||
      !strstr(plate, "\"hold_flash\":1")) {
    fprintf(stderr, "hub_status_selftest: lock/slots inject fail: %s\n", plate);
    return 1;
  }
  /* Quote-only lock collapses to safe dash placeholder. */
  setenv("NANOBOT_LLM_LOCK", "\"", 1);
  setenv("NANOBOT_LLM_SLOTS", "\\", 1);
  (void)gkx_hub_status(plate, sizeof plate);
  if (!strstr(plate, "\"lock\":\"-\"") || !strstr(plate, "\"slots\":\"-\"") ||
      strstr(plate, "\"lock\":\"\"") ||
      !strstr(plate, "\"peer_http\":\"lab_ops_only\"")) {
    fprintf(stderr, "hub_status_selftest: empty-token fail: %s\n", plate);
    return 1;
  }

  printf("HOST_HUB_STATUS_OK dual_wire=honest peer_http=lab_ops_only stop=1 "
         "lock_slots_sanitize=1\n");
  return 0;
}
