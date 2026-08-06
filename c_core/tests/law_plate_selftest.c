/* SPDX-License-Identifier: Apache-2.0
 * Pure-C selftest for c_core law/license/mode dual-wire plates (no HTTP).
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_law.h"
#include <stdio.h>
#include <string.h>

static int fail(const char *msg) {
  fprintf(stderr, "law_plate_selftest: %s\n", msg);
  return 1;
}

static int plate_dual_wire(const char *p) {
  return p && strstr(p, "\"product_wire\":\"smx2\"") &&
         strstr(p, "\"peer_http\":\"lab_ops_only\"") &&
         strstr(p, "\"peer_http_is_product_bus\":false") &&
         strstr(p, "\"llm_is_commander\":false") &&
         strstr(p, "\"share\":\"state_matrix_only\"") &&
         strstr(p, "\"hold_flash\":1");
}

int main(void) {
  char plate[768];
  grokium_law L;

  grokium_law_json(NULL, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.law.v1\"") ||
      !strstr(plate, "\"ok\":true") ||
      !strstr(plate, "\"commander\":\"ed25519\"") ||
      !strstr(plate, "\"not\":\"grok_model\"") ||
      !strstr(plate, "\"python\":0") || !strstr(plate, "\"host\":\"C\"") ||
      !plate_dual_wire(plate))
    return fail("law defaults dual-wire plate");

  grokium_law_default(&L);
  L.hold_flash = 0;
  grokium_law_json(&L, plate, sizeof plate);
  if (!strstr(plate, "\"hold_flash\":0") ||
      !strstr(plate, "\"llm_is_commander\":false") ||
      !strstr(plate, "\"product_wire\":\"smx2\"") ||
      !strstr(plate, "\"peer_http_is_product_bus\":false"))
    return fail("law hold_flash override plate");

  grokium_license_json(plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.license.v1\"") ||
      !strstr(plate, "\"ok\":true") ||
      !strstr(plate, "\"license\":\"Apache-2.0\"") ||
      !strstr(plate, "\"affiliation\":\"not_affiliated_with_xAI\"") ||
      !strstr(plate, "\"commander_is_not_model\":true") ||
      !strstr(plate, "\"python\":0") || !plate_dual_wire(plate))
    return fail("license dual-wire plate");

  grokium_mode_json(0, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.mode.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"mode\":\"chat\"") ||
      !strstr(plate, "\"tools\":0") ||
      !strstr(plate, "\"resume\":\"host_local_not_smx\"") ||
      !plate_dual_wire(plate))
    return fail("mode chat dual-wire plate");

  grokium_mode_json(1, plate, sizeof plate);
  if (!strstr(plate, "\"mode\":\"agent\"") || !strstr(plate, "\"tools\":1") ||
      !strstr(plate, "\"resume\":\"host_local_not_smx\"") ||
      !plate_dual_wire(plate))
    return fail("mode agent dual-wire plate");

  grokium_need_subcmd_json(
      "mode", "/mode chat|agent|resume|show resume=host_local_not_smx", plate,
      sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.mode.v1\"") ||
      !strstr(plate, "\"ok\":false") ||
      !strstr(plate, "\"error\":\"need_subcmd\"") ||
      !strstr(plate, "resume=host_local_not_smx") || !plate_dual_wire(plate))
    return fail("mode need_subcmd dual-wire plate");

  /* Schema/error inject must sanitize (no free-text quote breakout). */
  grokium_err_json("x\",\"evil\":true", "bad\";drop", "a\"b", plate,
                   sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.xeviltrue.v1\"") ||
      strstr(plate, "\"evil\"") || !strstr(plate, "\"error\":\"baddrop\"") ||
      !strstr(plate, "a_b") || !plate_dual_wire(plate))
    return fail("err_json inject sanitize");

  printf("C_CORE_LAW_PLATE_OK dual_wire=honest law=1 license=1 mode=1 "
         "need_subcmd=1 inject=1\n");
  return 0;
}
