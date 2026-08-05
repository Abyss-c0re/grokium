/* SPDX-License-Identifier: Apache-2.0
 * Local-first llama.cpp client (loopback only). LLM ≠ commander.
 * Product multi-peer bus remains SMX2; this is lab/ops LLM access only.
 */
#ifndef GROKIUM_LLAMA_H
#define GROKIUM_LLAMA_H

#include <stddef.h>

/* Probe local llama.cpp (default http://127.0.0.1:1212/v1). LLM ≠ commander. */
int grokium_llama_probe(char *json_out, size_t cap);

/* Local-first chat completion via loopback llama only. Never commander. */
int grokium_llama_chat(const char *message, char *json_out, size_t cap);

/*
 * Dual-wire chat deny plate (schema grokium.chat.v1).
 * error is sanitized to a machine token; optional hint is sanitized (no JSON inject).
 * NULL/empty hint → error-specific default (need_message / method / generic).
 * POST /v1/chat, serve CLI chat, and host chat/-p share this plate.
 */
void grokium_chat_err_json(const char *error, const char *hint, char *out,
                           size_t cap);

/*
 * Dual-wire agent-lite deny plate (schema grokium.agent.v1).
 * Always tools:false · tool_agent=host_nanobot · LLM ≠ commander.
 * NULL/empty hint → error-specific default (need_message / tools_not_on_lab_ops / method).
 * POST /v1/agent deny paths and serve selftest share this plate.
 */
void grokium_agent_err_json(const char *error, const char *hint, char *out,
                            size_t cap);

#endif
