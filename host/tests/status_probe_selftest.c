/* SPDX-License-Identifier: Apache-2.0
 * Pure-C selftest for dual-wire status probes (no nanobot, no ncurses).
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_status.h"
#include "grokium_status_plate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int write_file(const char *path, const char *body) {
  FILE *f = fopen(path, "w");
  if (!f) return -1;
  fputs(body, f);
  fclose(f);
  return 0;
}

static int fail(const char *msg) {
  fprintf(stderr, "status_probe_selftest: %s\n", msg);
  return 1;
}

int main(void) {
  char tmp[] = "/tmp/gkx_status_probe_XXXXXX";
  char *root = mkdtemp(tmp);
  char path[512], plate[640], grade[32];
  int fleet_n = -1, fleet_alive = -1, self_pid;
  unsigned bits = 99;
  char fleet_json[512];
  const char *matrix_json =
      "{\"schema\":\"grokium.smx.v1\","
      "\"sot_bits\":\"1111000011110000\"}"; /* 8 ones → SPARSE */

  if (!root) return fail("mkdtemp");
  self_pid = (int)getpid();

  snprintf(path, sizeof path, "%s/data", root);
  if (mkdir(path, 0700) != 0) return fail("mkdir data");
  snprintf(path, sizeof path, "%s/data/home", root);
  if (mkdir(path, 0700) != 0) return fail("mkdir home");
  snprintf(path, sizeof path, "%s/data/matrix", root);
  if (mkdir(path, 0700) != 0) return fail("mkdir matrix");

  /*
   * Real default role ids + id fields (fleet_load overlay). One live pid
   * (self), one null — honest kill(0). fleet_n is full default role set.
   */
  snprintf(fleet_json, sizeof fleet_json,
           "{\"schema\":\"grokium.nanobot_fleet.v1\",\"bots\":{"
           "\"nb-manager\":{\"id\":\"nb-manager\","
           "\"purpose\":\"motivate_incomplete_contracts\",\"pid\":%d},"
           "\"nb-host\":{\"id\":\"nb-host\","
           "\"purpose\":\"station_peer_cube_control\",\"pid\":null}}}",
           self_pid);
  snprintf(path, sizeof path, "%s/data/home/FLEET.json", root);
  if (write_file(path, fleet_json) != 0) return fail("write fleet");
  snprintf(path, sizeof path, "%s/data/matrix/LATEST.json", root);
  if (write_file(path, matrix_json) != 0) return fail("write matrix");

  gkx_status_fleet_probe(root, &fleet_n, &fleet_alive);
  if (fleet_n != 6)
    return fail("fleet_n expected 6 (default roles via fleet_load)");
  if (fleet_alive != 1)
    return fail("fleet_alive expected 1 (self pid on nb-manager)");

  gkx_status_matrix_probe(root, &bits, grade, sizeof grade);
  if (bits != 8)
    return fail("matrix_bits expected 8");
  if (strcmp(grade, "SPARSE") != 0)
    return fail("grade expected SPARSE");

  if (gkx_status_plate_json(root, "host_selftest", plate, sizeof plate) != 0)
    return fail("plate_json failed");
  if (!strstr(plate, "\"schema\":\"grokium.status.v1\"") ||
      !strstr(plate, "\"control_plane\":\"host_selftest\"") ||
      !strstr(plate, "\"product_wire\":\"smx2\"") ||
      !strstr(plate, "\"peer_http\":\"lab_ops_only\"") ||
      !strstr(plate, "\"peer_http_is_product_bus\":false") ||
      !strstr(plate, "\"llm_is_commander\":false") ||
      !strstr(plate, "\"commander\":\"ed25519\"") ||
      !strstr(plate, "\"python\":0") ||
      !strstr(plate, "\"fleet_n\":6") ||
      !strstr(plate, "\"fleet_alive\":1") ||
      !strstr(plate, "\"matrix_bits\":8") ||
      !strstr(plate, "\"grade\":\"SPARSE\"")) {
    fprintf(stderr, "status_probe_selftest: plate honesty fail: %s\n", plate);
    return 1;
  }

  /* Dual-wire smx_plate_json shape: bits_set preferred over bit string. */
  {
    const char *dual =
        "{\"schema\":\"grokium.smx.v1\",\"ok\":true,"
        "\"bits_set\":24,\"bits\":\"11110000...\","
        "\"product_wire\":\"smx2\",\"python\":0}";
    if (write_file(path, dual) != 0) return fail("write dual matrix");
    gkx_status_matrix_probe(root, &bits, grade, sizeof grade);
    if (bits != 24)
      return fail("dual-wire bits_set expected 24");
    if (strcmp(grade, "OK") != 0)
      return fail("dual-wire bits_set grade expected OK");
  }
  /* Dual-wire bits string only (no bits_set) — count 1s until quote. */
  {
    const char *dual_bits =
        "{\"schema\":\"grokium.smx.v1\",\"ok\":true,"
        "\"bits\":\"1111111111111111\",\"product_wire\":\"smx2\","
        "\"python\":0}";
    if (write_file(path, dual_bits) != 0) return fail("write dual bits");
    gkx_status_matrix_probe(root, &bits, grade, sizeof grade);
    if (bits != 16)
      return fail("dual-wire bits string expected 16");
    if (strcmp(grade, "OK") != 0)
      return fail("dual-wire bits grade expected OK");
  }

  /* Missing tree → empty probes, still dual-wire honest plate. */
  gkx_status_fleet_probe("/no/such/gkx_root", &fleet_n, &fleet_alive);
  if (fleet_n != 0 || fleet_alive != 0)
    return fail("missing root should report fleet 0/0");
  gkx_status_matrix_probe("/no/such/gkx_root", &bits, grade, sizeof grade);
  if (bits != 0 || strcmp(grade, "EMPTY") != 0)
    return fail("missing root should report EMPTY matrix");
  if (gkx_status_plate_json(NULL, NULL, plate, 8) == 0)
    return fail("tiny plate buffer should fail");

  /* Shared healthz plate (GET /healthz · serve CLI healthz same builder). */
  gk_healthz_json(plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.healthz.v1\"") ||
      !strstr(plate, "\"ok\":true") ||
      !strstr(plate, "\"service\":\"grokium-loopback\"") ||
      !strstr(plate, "\"control_plane\":\"loopback_http\"") ||
      !strstr(plate, "\"product_wire\":\"smx2\"") ||
      !strstr(plate, "\"peer_http\":\"lab_ops_only\"") ||
      !strstr(plate, "\"peer_http_is_product_bus\":false") ||
      !strstr(plate, "\"llm_is_commander\":false") ||
      !strstr(plate, "\"hold_flash\":1") ||
      !strstr(plate, "\"share\":\"state_matrix_only\"") ||
      !strstr(plate, "\"python\":0") ||
      !strstr(plate, "\"telemetry\":\"off\"")) {
    fprintf(stderr, "status_probe_selftest: healthz dual-wire fail: %s\n",
            plate);
    return 1;
  }

  /* Dual-wire selftest success — no free-text HOST_STATUS_PROBE_OK. */
  {
    char okp[640];
    snprintf(okp, sizeof okp,
             "{\"schema\":\"grokium.status_probe_selftest.v1\",\"ok\":true,"
             "\"fleet_n\":6,\"fleet_alive\":1,\"matrix_bits\":8,"
             "\"grade\":\"SPARSE\",\"dual_wire\":true,\"fleet_load\":true,"
             "\"dual_bits\":true,\"healthz\":true,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false,\"hold_flash\":1,"
             "\"share\":\"state_matrix_only\",\"python\":0}");
    if (!strstr(okp, "\"schema\":\"grokium.status_probe_selftest.v1\"") ||
        !strstr(okp, "\"ok\":true") || !strstr(okp, "\"fleet_n\":6") ||
        !strstr(okp, "\"healthz\":true") ||
        !strstr(okp, "\"product_wire\":\"smx2\"") ||
        !strstr(okp, "\"peer_http\":\"lab_ops_only\"") ||
        !strstr(okp, "\"peer_http_is_product_bus\":false") ||
        !strstr(okp, "\"llm_is_commander\":false") ||
        !strstr(okp, "\"hold_flash\":1") ||
        !strstr(okp, "\"share\":\"state_matrix_only\"") ||
        !strstr(okp, "\"python\":0")) {
      fprintf(stderr, "status_probe_selftest: ok plate fail: %.200s\n", okp);
      return 1;
    }
    printf("%s\n", okp);
  }
  return 0;
}
