/* SPDX-License-Identifier: Apache-2.0
 * Pure-C selftest for c_core status/healthz dual-wire plates (no HTTP/host).
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_status_plate.h"
#include <stdio.h>
#include <string.h>

static int fail(const char *msg) {
  fprintf(stderr, "status_plate_selftest: %s\n", msg);
  return 1;
}

static int plate_dual_wire(const char *plate) {
  return strstr(plate, "\"product_wire\":\"smx2\"") &&
         strstr(plate, "\"peer_http\":\"lab_ops_only\"") &&
         strstr(plate, "\"peer_http_is_product_bus\":false") &&
         strstr(plate, "\"llm_is_commander\":false") &&
         strstr(plate, "\"python\":0") &&
         strstr(plate, "\"share\":\"state_matrix_only\"") &&
         (strstr(plate, "\"hold_flash\":1") ||
          strstr(plate, "\"hold_flash\":0"));
}

int main(void) {
  char plate[768];

  if (gk_status_plate_json("host_cli", 1, 6, 2, 8u, "SPARSE", plate,
                           sizeof plate) != 0)
    return fail("status plate ok");
  if (!strstr(plate, "\"schema\":\"grokium.status.v1\"") ||
      !strstr(plate, "\"ok\":true") ||
      !strstr(plate, "\"control_plane\":\"host_cli\"") ||
      !strstr(plate, "\"fleet_n\":6") || !strstr(plate, "\"fleet_alive\":2") ||
      !strstr(plate, "\"matrix_bits\":8") ||
      !strstr(plate, "\"grade\":\"SPARSE\"") ||
      !strstr(plate, "\"commander\":\"ed25519\"") ||
      !strstr(plate, "\"telemetry\":\"off\"") ||
      !strstr(plate, "\"llm_on_hot_path\":false") || !plate_dual_wire(plate)) {
    fprintf(stderr, "status_plate_selftest: dual-wire fail: %s\n", plate);
    return 1;
  }

  /* Inject in control_plane/grade → machine tokens only. */
  if (gk_status_plate_json("host\";drop", 1, 3, 9, 0u, "OK\";evil", plate,
                           sizeof plate) != 0)
    return fail("status inject plate");
  if (!strstr(plate, "\"control_plane\":\"host_drop\"") ||
      !strstr(plate, "\"grade\":\"OK_evil\"") ||
      !strstr(plate, "\"fleet_alive\":3") || /* clamp alive to n */
      strstr(plate, "\";drop") || strstr(plate, "\";evil") ||
      !plate_dual_wire(plate))
    return fail("status inject sanitize/clamp");

  /* NULL plane/grade defaults; tiny buffer fails. */
  if (gk_status_plate_json(NULL, 0, -1, -5, 0u, NULL, plate, sizeof plate) != 0)
    return fail("status null defaults");
  if (!strstr(plate, "\"control_plane\":\"host\"") ||
      !strstr(plate, "\"grade\":\"EMPTY\"") ||
      !strstr(plate, "\"fleet_n\":0") || !strstr(plate, "\"fleet_alive\":0") ||
      !strstr(plate, "\"hold_flash\":0") || !plate_dual_wire(plate))
    return fail("status null defaults dual-wire");
  if (gk_status_plate_json("x", 1, 1, 1, 1u, "OK", plate, 8) == 0)
    return fail("tiny buffer must fail");

  /* Shared healthz plate (loopback GET /healthz · CLI healthz). */
  gk_healthz_json(plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.healthz.v1\"") ||
      !strstr(plate, "\"ok\":true") ||
      !strstr(plate, "\"service\":\"grokium-loopback\"") ||
      !strstr(plate, "\"control_plane\":\"loopback_http\"") ||
      !strstr(plate, "\"telemetry\":\"off\"") || !plate_dual_wire(plate)) {
    fprintf(stderr, "status_plate_selftest: healthz dual-wire fail: %s\n",
            plate);
    return 1;
  }
  gk_healthz_json(NULL, 0); /* no crash */

  /* Dual-wire selftest success — no free-text C_CORE_STATUS_PLATE_OK. */
  {
    char okp[512];
    snprintf(okp, sizeof okp,
             "{\"schema\":\"grokium.status_plate_selftest.v1\",\"ok\":true,"
             "\"status\":true,\"healthz\":true,\"sanitize\":true,"
             "\"fleet_clamp\":true,\"dual_wire\":true,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false,\"hold_flash\":1,"
             "\"share\":\"state_matrix_only\",\"python\":0}");
    if (!strstr(okp, "\"schema\":\"grokium.status_plate_selftest.v1\"") ||
        !strstr(okp, "\"ok\":true") || !strstr(okp, "\"status\":true") ||
        !strstr(okp, "\"healthz\":true") ||
        !strstr(okp, "\"sanitize\":true") ||
        !strstr(okp, "\"fleet_clamp\":true") ||
        !strstr(okp, "\"product_wire\":\"smx2\"") ||
        !strstr(okp, "\"peer_http_is_product_bus\":false") ||
        !strstr(okp, "\"python\":0"))
      return fail("status_plate_selftest plate");
    printf("%s\n", okp);
  }
  return 0;
}
