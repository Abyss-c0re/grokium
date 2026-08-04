/* Grokium host — C + nanobot core. Not affiliated with xAI. */
#ifndef GROKIUM_H
#define GROKIUM_H

#define GROKIUM_VERSION "0.4.0-nanobot"
#define GROKIUM_TOK "C3"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

extern char root[];
extern char cubalc_bin[];
extern char state_dir[];
extern char prog_dir[];
extern char nanobot_root[];

void resolve_paths(void);
int file_ok(const char *p);

#endif
