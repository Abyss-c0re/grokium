/* SPDX-License-Identifier: Apache-2.0
 * Dual-wire honesty status probes — shared by host CLI and TUI.
 * Product bus = SMX2; peer HTTP = lab_ops only; Commander ≠ model.
 */
#ifndef GROKIUM_STATUS_H
#define GROKIUM_STATUS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Honest fleet counts from {repo_root}/data/home/FLEET.json via fleet_load
 * + fleet_status (kill(0)); missing plate → 0/0.
 */
void gkx_status_fleet_probe(const char *repo_root, int *n_out, int *alive_out);

/*
 * Count SMX 01 bits from {repo_root}/data/matrix/LATEST.json when present.
 * Prefers dual-wire bits_set / bits (smx_plate_json); legacy sot_bits last.
 */
void gkx_status_matrix_probe(const char *repo_root, unsigned *bits_out,
                             char *grade, size_t gcap);

/* Format grokium.status.v1 plate into out (NUL-terminated). Returns 0 ok. */
int gkx_status_plate_json(const char *repo_root, const char *control_plane,
                          char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
