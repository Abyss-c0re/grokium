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

  /* write shared dual-wire compat plate (CLI version watch ≠ product bus). */
  {
    char path[PATH_MAX], dir[PATH_MAX], plate[768];
    FILE *f;
    snprintf(dir, sizeof dir, "%s/data", root ? root : ".");
    mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/grok_build_compat.json", dir);
    gkx_version_compat_json(st, 1, plate, sizeof plate);
    f = fopen(path, "w");
    if (f) {
      fprintf(f, "%s\n", plate);
      fclose(f);
    }
  }
  snprintf(st->last_seen, sizeof st->last_seen, "%s", ver);
  return 0;
}

void gkx_version_json(char *out, size_t cap) {
  if (!out || cap < 64) return;
  /* Shared dual-wire product version plate (host CLI version). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.version.v1\",\"ok\":true,"
           "\"product\":\"grokium\",\"version\":\"%s\",\"core\":\"nanobot\","
           "\"host\":\"C\",\"python\":0,\"tok\":\"%s\","
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false}",
           GROKIUM_VERSION, GROKIUM_TOK);
}

void gkx_version_compat_json(const gkx_version_state *st, int ok, char *out,
                             size_t cap) {
  char official[64];
  size_t i, o = 0;
  int changed = 0;
  if (!out || cap < 64) return;
  official[0] = 0;
  if (st) {
    changed = st->changed ? 1 : 0;
    /* Sanitize version token for JSON (no free-text / inject). */
    for (i = 0; st->official[i] && o + 1 < sizeof official; i++) {
      unsigned char c = (unsigned char)st->official[i];
      if (isalnum(c) || c == '.' || c == '-' || c == '_')
        official[o++] = (char)c;
    }
    official[o] = 0;
  }
  if (!official[0]) snprintf(official, sizeof official, "none");
  /* Shared dual-wire compat plate (CLI compat + on-disk refresh · py=0). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.version_compat.v1\",\"ok\":%s,"
           "\"reported_grok_build_version\":\"%s\","
           "\"grokium_version\":\"%s\",\"changed\":%s,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false,\"model_is_not_commander\":true,"
           "\"python\":0}",
           ok ? "true" : "false", official, GROKIUM_VERSION,
           changed ? "true" : "false");
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
