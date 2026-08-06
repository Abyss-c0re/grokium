/* Grokium LLM hub — manage nanobot peer + shared request gate. */
#ifndef GROKIUM_HUB_H
#define GROKIUM_HUB_H

#include "grokium_config.h"

/* Ensure hub nanobot is running (offline → local llama). Returns 0 ok. */
int gkx_hub_ensure(const gkx_config *cfg);

/* Stop hub if we started it (pid file). */
int gkx_hub_stop(void);

/*
 * Dual-wire hub stop ack plate (schema grokium.hub_status.v1).
 * Call after gkx_hub_stop; stopped=true · product bus SMX2 · peer HTTP lab/ops.
 * Host CLI + TUI /hub stop share this builder (no free-text-only usage).
 */
void gkx_hub_stop_json(char *buf, size_t n);

/* Dual-wire hub status plate into buf; returns 0 if healthy (alive+http). */
int gkx_hub_status(char *buf, size_t n);

/*
 * Dual-wire hub gate/wait plate (schema grokium.hub_wait.v1).
 * TUI chat_send replaces free-text "hub gate" / "waiting for LLM slot".
 * waiting!=0 → blocked on shared LLM slot; model is machine-tokenized.
 */
void gkx_hub_wait_json(int waiting, const char *model, char *buf, size_t n);

/* Apply shared LLM sched env for this process (and children). */
void gkx_hub_apply_sched_env(const gkx_config *cfg);

#endif
