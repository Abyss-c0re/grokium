/* Track local official CLI version (if installed) + seamless self-restart. */
#ifndef GROKIUM_VERSION_H
#define GROKIUM_VERSION_H

#include <stddef.h>

typedef struct {
  char official[64];
  char last_seen[64];
  int changed; /* 1 if official != last_seen after refresh */
} gkx_version_state;

void gkx_version_init(gkx_version_state *st, const char *root);
/* Read grok --version / ~/.grok/.metadata_version; update compat JSON. */
int gkx_version_refresh(gkx_version_state *st, const char *root);
/* If changed and auto_watch, execve self with same argv (returns only on fail). */
int gkx_version_maybe_restart(const gkx_version_state *st, int argc, char **argv);

/*
 * Dual-wire product version plate (schema grokium.version.v1).
 * Host CLI `version` shares this builder (lab/ops ≠ product bus).
 */
void gkx_version_json(char *out, size_t cap);

/*
 * Dual-wire version-compat plate (schema grokium.version_compat.v1).
 * Sanitizes st->official; ok=1 when refresh found an official version.
 * Host CLI `compat` + on-disk data/grok_build_compat.json share this.
 */
void gkx_version_compat_json(const gkx_version_state *st, int ok, char *out,
                             size_t cap);

#endif
