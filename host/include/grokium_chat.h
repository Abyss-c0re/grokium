/* Grokium chat — nanobot core (local llama OR optional cloud). */
#ifndef GROKIUM_CHAT_H
#define GROKIUM_CHAT_H

#include "grokium_config.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*gkx_stream_fn)(void *userdata, const char *chunk, size_t n);

/* backend: "local" | "grok"
 * model: "auto"/"local" resolves via llama /v1/models
 * Returns 0 ok with text, 1 empty, 2 hard fail
 */
int grokium_chat_request(const char *backend, const char *model,
                         const char *msg,
                         const char *state_dir,
                         char *out_reply, size_t reply_n,
                         char *out_err, size_t err_n);

/* Streaming variant (final answer text via on_delta when possible). */
int grokium_chat_request_ex(const gkx_config *cfg,
                            const char *msg,
                            gkx_stream_fn on_delta, void *userdata,
                            char *out_reply, size_t reply_n,
                            char *out_err, size_t err_n);

/* GET live models JSON (malloc'd) for active backend; caller frees. */
char *grokium_models_json(const gkx_config *cfg, char *out_err, size_t err_n);

/* Resolve "auto"/"local" to first live model id into buf. */
int grokium_resolve_model(const gkx_config *cfg, char *buf, size_t n);

int grokium_load_grok_token(char *out, size_t outn);

/* Host-local only: seed nanobot recent memory with one user/assistant pair
 * (capped/pruned by nanobot memory). Never touches SMX product bus. */
void gkx_memory_seed_exchange(const char *user, const char *assistant);

/* Host-local multi-pair seed for session resume.
 * Last ng_memory_recent_turns() pairs → recent.jsonl (verbatim).
 * Older pairs → one compact line in memory/summary.txt so context past the
 * recent-turns cap is not discarded. Never SMX product bus.
 * Returns number of pairs written to recent (0 if none). */
int gkx_memory_seed_pairs(const char *const *users, const char *const *assts,
                          int n_pairs);

#ifdef __cplusplus
}
#endif
#endif
