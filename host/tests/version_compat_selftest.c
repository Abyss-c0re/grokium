/* SPDX-License-Identifier: Apache-2.0
 * Pure-C selftest for version_compat plate dual-wire honesty
 * (no nanobot, no ncurses).
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_version.h"
#include "grokium.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int fail(const char *msg) {
  fprintf(stderr, "version_compat_selftest: %s\n", msg);
  return 1;
}

static int read_all(const char *path, char *out, size_t n) {
  FILE *f;
  size_t got;
  out[0] = 0;
  f = fopen(path, "r");
  if (!f) return -1;
  got = fread(out, 1, n - 1, f);
  out[got] = 0;
  fclose(f);
  return got > 0 ? 0 : -1;
}

int main(void) {
  char tmp[] = "/tmp/gkx_version_compat_XXXXXX";
  char *root = mkdtemp(tmp);
  char path[512], plate[1024], meta_dir[512];
  gkx_version_state st;
  FILE *mf;

  if (!root) return fail("mkdtemp");

  /* Fake HOME/.grok/.metadata_version so refresh does not need real grok CLI. */
  snprintf(meta_dir, sizeof meta_dir, "%s/.grok", root);
  if (mkdir(meta_dir, 0700) != 0) return fail("mkdir .grok");
  snprintf(path, sizeof path, "%s/.grok/.metadata_version", root);
  mf = fopen(path, "w");
  if (!mf) return fail("open metadata");
  fputs("9.9.9\n", mf);
  fclose(mf);
  if (setenv("HOME", root, 1) != 0) return fail("setenv HOME");
  /* Avoid accidental grok CLI path if metadata parse failed. */
  unsetenv("GROK_CLI");

  memset(&st, 0, sizeof st);
  gkx_version_init(&st, root);
  if (gkx_version_refresh(&st, root) != 0)
    return fail("refresh expected success with fake metadata");
  if (strcmp(st.official, "9.9.9") != 0) {
    fprintf(stderr, "version_compat_selftest: official=%s\n", st.official);
    return fail("official version expected 9.9.9");
  }

  snprintf(path, sizeof path, "%s/data/grok_build_compat.json", root);
  if (read_all(path, plate, sizeof plate) != 0)
    return fail("read grok_build_compat.json");

  if (!strstr(plate, "\"schema\":\"grokium.version_compat.v1\"") ||
      !strstr(plate, "\"reported_grok_build_version\":\"9.9.9\"") ||
      !strstr(plate, "\"grokium_version\":\"" GROKIUM_VERSION "\"") ||
      !strstr(plate, "\"product_wire\":\"smx2\"") ||
      !strstr(plate, "\"peer_http\":\"lab_ops_only\"") ||
      !strstr(plate, "\"peer_http_is_product_bus\":false") ||
      !strstr(plate, "\"llm_is_commander\":false") ||
      !strstr(plate, "\"model_is_not_commander\":true") ||
      !strstr(plate, "\"share\":\"state_matrix_only\"") ||
      !strstr(plate, "\"hold_flash\":1") ||
      !strstr(plate, "\"python\":0") ||
      !strstr(plate, "\"ok\":true")) {
    fprintf(stderr, "version_compat_selftest: plate honesty fail:\n%s\n", plate);
    return 1;
  }

  /* Shared builder must match on-disk plate (CLI compat path). */
  {
    char built[768];
    gkx_version_compat_json(&st, 1, built, sizeof built);
    if (!strstr(built, "\"reported_grok_build_version\":\"9.9.9\"") ||
        !strstr(built, "\"product_wire\":\"smx2\"") ||
        !strstr(built, "\"python\":0") ||
        !strstr(built, "\"changed\":false"))
      return fail("compat builder dual-wire fail");
  }

  /* Shared product version plate (CLI version + --version path). */
  {
    char ver[512];
    gkx_version_json(ver, sizeof ver);
    if (!strstr(ver, "\"schema\":\"grokium.version.v1\"") ||
        !strstr(ver, "\"product\":\"grokium\"") ||
        !strstr(ver, "\"version\":\"" GROKIUM_VERSION "\"") ||
        !strstr(ver, "\"core\":\"nanobot\"") ||
        !strstr(ver, "\"host\":\"C\"") ||
        !strstr(ver, "\"python\":0") ||
        !strstr(ver, "\"product_wire\":\"smx2\"") ||
        !strstr(ver, "\"peer_http_is_product_bus\":false") ||
        !strstr(ver, "\"llm_is_commander\":false") ||
        !strstr(ver, "\"hold_flash\":1") ||
        !strstr(ver, "\"share\":\"state_matrix_only\""))
      return fail("version plate dual-wire fail");
  }

  /* Second refresh with same version: still honest, changed=0. */
  if (gkx_version_refresh(&st, root) != 0)
    return fail("second refresh failed");
  if (st.changed != 0)
    return fail("same version should not set changed");
  if (read_all(path, plate, sizeof plate) != 0)
    return fail("re-read plate");
  if (!strstr(plate, "\"product_wire\":\"smx2\"") ||
      !strstr(plate, "\"llm_is_commander\":false") ||
      !strstr(plate, "\"python\":0"))
    return fail("plate dual-wire lost on rewrite");

  /* Seamless-restart plate (maybe_restart stderr · no free-text banner). */
  {
    char rst[768];
    gkx_version_state ch = st;
    ch.changed = 1;
    snprintf(ch.official, sizeof ch.official, "10.0.0\";drop");
    gkx_version_restart_json(&ch, rst, sizeof rst);
    if (!strstr(rst, "\"schema\":\"grokium.version_restart.v1\"") ||
        !strstr(rst, "\"ok\":true") ||
        !strstr(rst, "\"action\":\"restart\"") ||
        !strstr(rst, "\"reason\":\"upstream_version\"") ||
        !strstr(rst, "\"reported_grok_build_version\":\"10.0.0drop\"") ||
        !strstr(rst, "\"changed\":true") ||
        !strstr(rst, "\"product_wire\":\"smx2\"") ||
        !strstr(rst, "\"peer_http_is_product_bus\":false") ||
        !strstr(rst, "\"llm_is_commander\":false") ||
        !strstr(rst, "\"python\":0") || strstr(rst, "\";drop") ||
        strstr(rst, "seamless restart"))
      return fail("restart plate dual-wire/sanitize fail");
  }

  printf("HOST_VERSION_COMPAT_OK reported=9.9.9 grokium=%s dual_wire=honest "
         "version_plate=1 restart_plate=1 python=0\n",
         GROKIUM_VERSION);
  return 0;
}
