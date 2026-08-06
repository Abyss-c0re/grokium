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
 * Dual-wire mode success plate (schema grokium.mode.v1).
 * Host TUI /mode chat|agent|show — tools toggle is host UX.
 * agent_tools != 0 → mode=agent tools=1; else mode=chat tools=0.
 * resume field is always host_local_not_smx (pickup ≠ product bus).
 */
void grokium_mode_json(int agent_tools, char *out, size_t cap);

/*
 * Dual-wire machine deny plate (generic).
 * schema_leaf → "schema":"grokium.<leaf>.v1"; error → machine token.
 * Optional hint is machine-sanitized (no JSON inject).
 * NULL leaf → "error"; NULL error → "error". Host cubalc/tool/settings share this.
 */
void grokium_err_json(const char *schema_leaf, const char *error,
                      const char *hint, char *out, size_t cap);

/*
 * Dual-wire need_subcmd deny plate (wrapper around grokium_err_json).
 * schema_leaf → "schema":"grokium.<leaf>.v1" (sanitized machine token).
 * Optional hint is machine-sanitized (no JSON inject). NULL leaf → "command".
 * Host contract/hub/… help surfaces share this builder.
 */
void grokium_need_subcmd_json(const char *schema_leaf, const char *hint,
                              char *out, size_t cap);

/*
 * Dual-wire Commander deny plate (Commander ≠ model · Ed25519 residual).
 * schema_leaf → "schema":"grokium.<leaf>.v1" (default "commander").
 * error/hint are machine-sanitized. Host CLI + grokium-commander share this.
 */
void grokium_commander_deny_json(const char *schema_leaf, const char *error,
                                 const char *hint, char *out, size_t cap);

/*
 * Dual-wire Commander success plate (schema grokium.commander.v1).
 * fingerprint hex-sanitized; law_dir/domain path-sanitized (no JSON inject).
 * has_sk: -1 omit field, 0 false, 1 true. domain NULL → omit domain.
 * CLI keygen/show + GET /v1/commander share this builder.
 */
void grokium_commander_ok_json(const char *fingerprint, const char *law_dir,
                               const char *domain, int has_sk, char *out,
                               size_t cap);

/*
 * Dual-wire Commander verify plate (schema grokium.commander_verify.v1).
 * ok=1 → commander "grokium"; ok=0 → commander null. CLI + HTTP verify share.
 */
void grokium_commander_verify_json(int ok, char *out, size_t cap);

/*
 * Dual-wire Commander install-law success plate (schema grokium.commander.v1).
 * home/bot path-sanitized; fingerprint hex-sanitized.
 */
void grokium_commander_install_json(const char *home, const char *bot,
                                    const char *fingerprint, char *out,
                                    size_t cap);

/*
 * Dual-wire Commander reject-model plate (schema grokium.commander_reject.v1).
 * allowed=0 → ok=false error=model_is_not_commander; allowed≠0 → ok=true.
 * POST /v1/commander/reject_model shares this builder (LLM ≠ commander).
 */
void grokium_commander_reject_json(int allowed, char *out, size_t cap);
#endif
