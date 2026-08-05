/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GROKIUM_SMX_FILTER_H
#define GROKIUM_SMX_FILTER_H
/*
 * SMX filter — protection between BrainCube mini-hive and external nanobots.
 * Command center sees MatrixState + sealed contracts only. Prose denied on wire.
 * Law: .agents/laws/06-HIVE_MIND_CORE.md
 */
#include "grokium_smx.h"
#include "grokium_law.h"
#include <stddef.h>
#include <stdint.h>

#define GROKIUM_CONTRACT_ID_LEN   48
#define GROKIUM_CONTRACT_PATH_LEN 512
#define GROKIUM_ACCEPT_HEX_LEN    65

typedef enum {
  GROKIUM_CONTRACT_OPEN = 0,
  GROKIUM_CONTRACT_PROGRESS = 1,
  GROKIUM_CONTRACT_COMPLETE = 2,
  GROKIUM_CONTRACT_VOID = 3
} grokium_contract_status;

typedef struct {
  char id[GROKIUM_CONTRACT_ID_LEN];
  char assignee[64];
  char issuer[64];
  char task_digest[GROKIUM_ACCEPT_HEX_LEN]; /* sha256 hex of task text */
  char accept_smx_sha256[GROKIUM_ACCEPT_HEX_LEN]; /* empty = unused */
  int accept_digit;     /* -1 = unused, else 0..9 */
  int accept_min_set;   /* bits_set floor; 0 = unused */
  int budget;
  int hold_flash;
  grokium_contract_status status;
  int motivate_ticks;   /* manager pressure count */
  int64_t deadline_ts;
  char path[GROKIUM_CONTRACT_PATH_LEN];
} grokium_contract;

/* Filter gate: 1 = allow SMX frame toward core / external; 0 = deny.
 * NEXUS_COORD requires machine plate shape (| key=value |); prefix alone is not enough. */
int grokium_smx_filter_allow_frame(const grokium_law *law,
                                   const uint8_t *frame, size_t n,
                                   int from_external);

/* Deny if buffer looks like prose chat (heuristic hot-path sanitize).
 * Invalid / chat-smuggling NEXUS_COORD counts as prose. */
int grokium_smx_filter_is_prose(const char *buf, size_t n);

/* Form contract for external assignee. Writes JSON under dir (or GROKIUM_CONTRACT_DIR).
 * task may be long; only digest stored on plate. Returns 0 ok. */
int grokium_contract_form(grokium_contract *out, const char *dir,
                          const char *assignee, const char *task,
                          int accept_digit, int accept_min_set,
                          const char *accept_smx_sha256);

/* Load contract plate. Returns 0 ok. */
int grokium_contract_load(grokium_contract *out, const char *path);

/* Validate result SMX against accept criteria. Optional smart-contract:
 * if GROKIUM_CONTRACT_VERIFY is set, runs `$VERIFY <contract_path> <result_bits>`.
 * Returns 1 complete, 0 incomplete, -1 error. */
int grokium_contract_validate(grokium_contract *c, const grokium_smx *result,
                              int algodigit);

/* Manager: tick motivate on all open/progress contracts in dir. Returns count. */
int grokium_manager_motivate_dir(const char *dir);

/*
 * Dual-wire manager-tick ack (schema grokium.manager_tick.v1).
 * Call after grokium_manager_motivate_dir; dir is JSON-escaped on the plate.
 * CLI manager-tick and HTTP /v1/manager/tick share this plate.
 */
void grokium_manager_tick_json(int motivated, const char *dir, char *out,
                               size_t cap);
/* Dual-wire manager-tick deny/help (need_dir_or_run | generic error).
 * Host TUI /manager help and CLI manager-tick help|? share this plate. */
void grokium_manager_tick_err_json(const char *error, char *out, size_t cap);

/*
 * Dual-wire contract form ack (schema grokium.contract_form.v1).
 * Call after grokium_contract_form; id/path/assignee escaped.
 * CLI form and POST /v1/contract/form share this plate.
 */
void grokium_contract_form_json(const grokium_contract *c, char *out,
                                size_t cap);
/* Dual-wire form deny (need_assignee_and_task | form_failed | need_json_body). */
void grokium_contract_form_err_json(const char *error, char *out, size_t cap);

/*
 * Dual-wire contract validate ack (schema grokium.contract_validate.v1).
 * rc is grokium_contract_validate return (1 complete, 0 incomplete, -1 error).
 * CLI validate and POST /v1/contract/validate share this plate.
 */
void grokium_contract_validate_json(const grokium_contract *c, int rc,
                                    int digit, unsigned bits_set, char *out,
                                    size_t cap);
/* Dual-wire validate deny (need_path | contract_not_found | need_json_body). */
void grokium_contract_validate_err_json(const char *error, char *out,
                                        size_t cap);

/* Instinct attitude line for core nanobot system memory (static string). */
const char *grokium_hive_instinct_creed(void);

/*
 * Dual-wire instinct plate (schema grokium.instinct.v1).
 * CLI instinct and GET /v1/instinct share this plate (creed + honesty keys).
 */
void grokium_instinct_json(char *out, size_t cap);

/*
 * Dual-wire SMX allow-check plate (schema grokium.smx_allow.v1).
 * CLI allow-check and host filter selftests share this plate.
 */
void grokium_smx_allow_json(int allow, int prose, char *out, size_t cap);

#endif
