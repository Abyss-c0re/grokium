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

#endif
