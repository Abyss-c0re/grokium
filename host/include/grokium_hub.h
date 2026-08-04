/* Grokium LLM hub — manage nanobot peer + shared request gate. */
#ifndef GROKIUM_HUB_H
#define GROKIUM_HUB_H

#include "grokium_config.h"

/* Ensure hub nanobot is running (offline → local llama). Returns 0 ok. */
int gkx_hub_ensure(const gkx_config *cfg);

/* Stop hub if we started it (pid file). */
int gkx_hub_stop(void);

/* Status line into buf; returns 0 if healthy. */
int gkx_hub_status(char *buf, size_t n);

/* Apply shared LLM sched env for this process (and children). */
void gkx_hub_apply_sched_env(const gkx_config *cfg);

#endif
