#define _POSIX_C_SOURCE 200809L
#include "grokium_version.h"
#include "grokium.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>

static void read_file_trim(const char *path, char *out, size_t n) {
  out[0] = 0;
  FILE *f = fopen(path, "r");
  if (!f) return;
  if (fgets(out, (int)n, f))
    out[strcspn(out, "\r\n")] = 0;
  fclose(f);
}

static int run_capture(const char *cmd, char *out, size_t n) {
  out[0] = 0;
  FILE *p = popen(cmd, "r");
  if (!p) return -1;
  if (fgets(out, (int)n, p))
    out[strcspn(out, "\r\n")] = 0;
  pclose(p);
  return out[0] ? 0 : -1;
}

static void extract_semver(const char *s, char *out, size_t n) {
  out[0] = 0;
  if (!s) return;
  const char *p = s;
  while (*p) {
    if (isdigit((unsigned char)*p)) {
      int a = 0, b = 0, c = 0;
      if (sscanf(p, "%d.%d.%d", &a, &b, &c) == 3) {
        snprintf(out, n, "%d.%d.%d", a, b, c);
        return;
      }
    }
    p++;
  }
}

void gkx_version_init(gkx_version_state *st, const char *root) {
  memset(st, 0, sizeof *st);
  char path[PATH_MAX];
  snprintf(path, sizeof path, "%s/data/grok_build_compat.json", root ? root : ".");
  char buf[4096];
  read_file_trim(path, buf, sizeof buf);
  /* naive last reported */
  const char *k = strstr(buf, "\"reported_grok_build_version\"");
  if (k) {
    k = strchr(k, ':');
    if (k) {
      k++;
      while (*k == ' ' || *k == '"') k++;
      size_t i = 0;
      while (k[i] && k[i] != '"' && i + 1 < sizeof st->last_seen)
        st->last_seen[i] = k[i], i++;
      st->last_seen[i] = 0;
    }
  }
}

int gkx_version_refresh(gkx_version_state *st, const char *root) {
  if (!st) return -1;
  st->changed = 0;
  char raw[256] = "";
  char ver[64] = "";

  const char *home = getenv("HOME");
  if (home) {
    char mp[PATH_MAX];
    snprintf(mp, sizeof mp, "%s/.grok/.metadata_version", home);
    read_file_trim(mp, raw, sizeof raw);
    extract_semver(raw, ver, sizeof ver);
  }
  if (!ver[0]) {
    const char *g = getenv("GROK_CLI");
    char cmd[512];
    if (g && g[0])
      snprintf(cmd, sizeof cmd, "\"%s\" --version 2>/dev/null", g);
    else
      snprintf(cmd, sizeof cmd, "grok --version 2>/dev/null");
    if (run_capture(cmd, raw, sizeof raw) == 0)
      extract_semver(raw, ver, sizeof ver);
  }
  if (!ver[0]) {
    snprintf(st->official, sizeof st->official, "%s",
             st->last_seen[0] ? st->last_seen : "none");
    return 1; /* no official CLI */
  }
  snprintf(st->official, sizeof st->official, "%s", ver);
  if (st->last_seen[0] && strcmp(st->last_seen, ver) != 0)
    st->changed = 1;
  else if (!st->last_seen[0])
    st->changed = 0; /* first capture */

  /* write compat JSON */
  char path[PATH_MAX], dir[PATH_MAX];
  snprintf(dir, sizeof dir, "%s/data", root ? root : ".");
  mkdir(dir, 0755);
  snprintf(path, sizeof path, "%s/grok_build_compat.json", dir);
  FILE *f = fopen(path, "w");
  if (f) {
    /* On-disk compat plate: dual-wire honesty (CLI version watch ≠ product bus). */
    fprintf(f,
            "{\n"
            "  \"schema\": \"grokium.version_compat.v1\",\n"
            "  \"reported_grok_build_version\": \"%s\",\n"
            "  \"grokium_version\": \"%s\",\n"
            "  \"last_source\": \"gkx_version_refresh\",\n"
            "  \"updated_at\": %ld,\n"
            "  \"model_is_not_commander\": true,\n"
            "  \"llm_is_commander\": false,\n"
            "  \"share\": \"state_matrix_only\",\n"
            "  \"hold_flash\": 1,\n"
            "  \"product_wire\": \"smx2\",\n"
            "  \"peer_http\": \"lab_ops_only\",\n"
            "  \"peer_http_is_product_bus\": false\n"
            "}\n",
            ver, GROKIUM_VERSION, (long)time(NULL));
    fclose(f);
  }
  snprintf(st->last_seen, sizeof st->last_seen, "%s", ver);
  return 0;
}

int gkx_version_maybe_restart(const gkx_version_state *st, int argc, char **argv) {
  (void)argc;
  if (!st || !st->changed) return 0;
  fprintf(stderr,
          "grokium: official CLI version now %s — seamless restart\n",
          st->official);
  fflush(stderr);
  /* Mark env so we don't loop immediately */
  setenv("GROKIUM_RESTART_REASON", "upstream_version", 1);
  setenv("GROKIUM_COMPAT_VERSION", st->official, 1);
  execv(argv[0], argv);
  /* try via /proc/self/exe */
  char self[PATH_MAX];
  ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
  if (n > 0) {
    self[n] = 0;
    execv(self, argv);
  }
  return -1;
}
