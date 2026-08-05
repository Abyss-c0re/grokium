/* SPDX-License-Identifier: Apache-2.0 */
#include "grokium_law.h"
#include <stdio.h>
#include <string.h>

void grokium_law_default(grokium_law *L) {
  if (!L) return;
  memset(L, 0, sizeof *L);
  L->hold_flash = GROKIUM_HOLD_FLASH;
  L->no_brain_wires = GROKIUM_NO_BRAIN_WIRES;
  L->state_matrix_key = GROKIUM_STATE_MATRIX_KEY;
  L->cores_unmixed = GROKIUM_CORES_UNMIXED;
  L->face_blur = 1;
  L->zero_telemetry = 1;
  L->commander_only_residual = 1;
}

int grokium_law_blocks_flash(const grokium_law *L) {
  if (!L) return 1;
  return L->hold_flash ? 1 : 0;
}

void grokium_law_json(const grokium_law *L, char *out, size_t cap) {
  grokium_law def;
  const grokium_law *p = L;
  if (!out || cap < 64) return;
  if (!p) {
    grokium_law_default(&def);
    p = &def;
  }
  /* Dual-wire honesty: product_wire=smx2; peer HTTP is lab/ops only. */
  snprintf(out, cap,
           "{\"schema\":\"grokium.law.v1\",\"ok\":true,"
           "\"hold_flash\":%d,\"no_brain_wires\":%d,"
           "\"state_matrix_key\":%d,\"cores_unmixed\":%d,"
           "\"face_blur\":%d,\"zero_telemetry\":%d,"
           "\"commander_only_residual\":%d,"
           "\"share\":\"state_matrix_only\","
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"commander\":\"ed25519\",\"llm_is_commander\":false,"
           "\"python\":0,\"host\":\"C\",\"product\":\"grokium\","
           "\"not\":\"grok_model\"}",
           p->hold_flash ? 1 : 0, p->no_brain_wires ? 1 : 0,
           p->state_matrix_key ? 1 : 0, p->cores_unmixed ? 1 : 0,
           p->face_blur ? 1 : 0, p->zero_telemetry ? 1 : 0,
           p->commander_only_residual ? 1 : 0);
}
