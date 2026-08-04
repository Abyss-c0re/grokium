/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GROKIUM_HTTP_H
#define GROKIUM_HTTP_H
#include "grokium_consolidator.h"
#include "grokium_fleet.h"
#include "grokium_law.h"
/*
 * Loopback control plane (default :17444).
 * Product talk remains SMX2; this HTTP surface is lab/ops only.
 * Bind is forced to 127.0.0.1; non-loopback host is refused.
 * Coord/publish paths sanitize via SMX filter (prose denied).
 * GET /v1/stream/smx — short SSE snapshot of latest matrix (bits only).
 * GROKIUM_SERVE_MAX=N exits after N requests (selftest).
 */
int grokium_serve(const char *host, int port,
                  gk_consolidator *C, gk_fleet *F, grokium_law *L,
                  const char *data_root);

/* Probe local llama.cpp (default http://127.0.0.1:1212/v1). LLM ≠ commander. */
int grokium_llama_probe(char *json_out, size_t cap);
#endif
