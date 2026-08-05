/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GROKIUM_LAW_H
#define GROKIUM_LAW_H
/* Cube Standards flags — non-verbal hot path */
#include <stddef.h>

#define GROKIUM_HOLD_FLASH        1
#define GROKIUM_NO_BRAIN_WIRES    1
#define GROKIUM_STATE_MATRIX_KEY  1
#define GROKIUM_CORES_UNMIXED     1
#define GROKIUM_SHARE_SMX_ONLY    1
#define GROKIUM_CELLS             512
#define GROKIUM_EDGE              8
typedef struct {
  int hold_flash;
  int no_brain_wires;
  int state_matrix_key;
  int cores_unmixed;
  int face_blur;
  int zero_telemetry;
  int commander_only_residual;
} grokium_law;
void grokium_law_default(grokium_law *L);
int  grokium_law_blocks_flash(const grokium_law *L);
/*
 * Dual-wire law plate (schema grokium.law.v1).
 * Commander = Ed25519 residual; LLM is never commander. NULL L → defaults.
 */
void grokium_law_json(const grokium_law *L, char *out, size_t cap);

/*
 * Dual-wire license plate (schema grokium.license.v1).
 * Apache-2.0 · not affiliated with xAI · Commander ≠ model · py=0.
 * GET /v1/license, host CLI/TUI, and serve CLI share this plate.
 */
void grokium_license_json(char *out, size_t cap);

/*
 * Dual-wire need_subcmd deny plate.
 * schema_leaf → "schema":"grokium.<leaf>.v1" (sanitized machine token).
 * Optional hint is machine-sanitized (no JSON inject). NULL leaf → "command".
 * Host contract/hub/… help surfaces share this builder.
 */
void grokium_need_subcmd_json(const char *schema_leaf, const char *hint,
                              char *out, size_t cap);
#endif
