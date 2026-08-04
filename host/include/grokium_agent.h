/* Grokium agent — C tools. CubalC remains board/control plane. */
#ifndef GROKIUM_AGENT_H
#define GROKIUM_AGENT_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* on_event: optional UI hook. kinds: "tool"|"result"|"think"|"final"
 * Returns 0 on final text, 1 empty, 2 fail.
 */
typedef void (*grokium_agent_event_fn)(const char *kind, const char *text, void *ud);

int grokium_agent_run(const char *backend, const char *model,
                      const char *msg,
                      const char *state_dir,
                      char *out_reply, size_t reply_n,
                      char *out_err, size_t err_n,
                      grokium_agent_event_fn on_event, void *ud);

#ifdef __cplusplus
}
#endif
#endif
