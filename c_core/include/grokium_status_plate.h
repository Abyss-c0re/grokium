/* SPDX-License-Identifier: Apache-2.0
 * Dual-wire status plate formatter (schema grokium.status.v1).
 * Probes stay at the caller (host file/kill vs loopback live fleet/matrix).
 */
#ifndef GROKIUM_STATUS_PLATE_H
#define GROKIUM_STATUS_PLATE_H

#include <stddef.h>

/*
 * Format grokium.status.v1 into out. control_plane and grade are sanitized
 * to machine tokens (no free-text inject). Returns 0 ok, -1 on bad args.
 * Commander ≠ model; product_wire=smx2; peer_http=lab_ops_only.
 */
int gk_status_plate_json(const char *control_plane, int hold_flash, int fleet_n,
                         int fleet_alive, unsigned matrix_bits,
                         const char *grade, char *out, size_t cap);

#endif
