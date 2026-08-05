/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GROKIUM_HTTP_H
#define GROKIUM_HTTP_H
#include "grokium_consolidator.h"
#include "grokium_fleet.h"
#include "grokium_law.h"
#include "grokium_llama.h"
/*
 * Loopback control plane (default :17444).
 * Product talk remains SMX2; this HTTP surface is lab/ops only.
 * Bind is forced to 127.0.0.1; non-loopback host is refused.
 * Coord/publish paths sanitize via SMX filter (prose denied).
 * GET /v1/stream/smx — short SSE snapshot of latest matrix (bits only).
 * GET /v1/cube/status — AlgoCube bridge plate (digit/blueprint, dual-wire).
 * GET/POST /v1/sessions[/search|/pickup|/id] — import metas only (no transcripts).
 * GET /ui — minimal lab/ops HTML plate (dual-wire honesty; not product chat).
 * POST /v1/agent — lab/ops chat-only agent (tools:false; tools → host nanobot).
 * GROKIUM_SERVE_MAX=N exits after N requests (selftest).
 * Local llama probe/chat: see grokium_llama.h (src/llama.c).
 */
int grokium_serve(const char *host, int port,
                  gk_consolidator *C, gk_fleet *F, grokium_law *L,
                  const char *data_root);
#endif
