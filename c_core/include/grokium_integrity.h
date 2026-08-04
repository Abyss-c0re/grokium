/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GROKIUM_INTEGRITY_H
#define GROKIUM_INTEGRITY_H
/*
 * Integrity core — code seal + privacy hard-off (fail closed).
 * Law: .agents/laws/02-INTEGRITY.md
 */
#include <stddef.h>

/* Verify CODE_SEAL + privacy plate. Writes LATEST.json. Returns 1 pass, 0 fail, -1 error. */
int gk_integrity_tick(const char *repo_root, char *json_out, size_t cap);

/* Read POLICY.json into out (truncated). Returns 0 ok. */
int gk_integrity_policy(const char *repo_root, char *json_out, size_t cap);

/* Intentional reseal of product tree into data/integrity/CODE_SEAL.json. */
int gk_integrity_reseal(const char *repo_root, char *json_out, size_t cap);

#endif
