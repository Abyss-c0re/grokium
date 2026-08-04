/* SPDX-License-Identifier: Apache-2.0 */
#include "grokium_law.h"
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
