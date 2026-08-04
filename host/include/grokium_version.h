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

#endif
