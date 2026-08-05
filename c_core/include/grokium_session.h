/* SPDX-License-Identifier: Apache-2.0
 * Loopback/import session meta plates — dual-wire honesty (no transcripts).
 */
#ifndef GROKIUM_SESSION_H
#define GROKIUM_SESSION_H

#include <stddef.h>

/* Hex digits + dashes only; length 1..80. */
int gk_session_id_safe(const char *id);

/*
 * Dual-wire pickup deny plate (meta_only; never carries chat turns).
 * error tokens: bad_session_id | not_found | need_session_id | bad_meta
 * id is optional; only hex/dash chars are echoed (injection-safe).
 */
int gk_session_pickup_deny_json(const char *id, const char *error, char *out,
                                size_t cap);

/*
 * Dual-wire sessions list empty/miss plate (content=meta_only).
 * error is a short machine token (e.g. no_import_dir) or empty.
 */
int gk_session_list_empty_json(const char *q, const char *import_dir,
                               const char *error, char *out, size_t cap);

#endif
