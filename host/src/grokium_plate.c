/* SPDX-License-Identifier: Apache-2.0
 * Pure-C dual-wire plate line filter for host TUI tool capture.
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_plate.h"
#include <stdlib.h>
#include <string.h>

int gkx_is_grokium_plate_line(const char *ln) {
  return ln && ln[0] == '{' && strstr(ln, "\"schema\":\"grokium.") != NULL;
}

int gkx_log_block_keep_line(const char *ln, int debug_mode) {
  if (!ln || !ln[0]) return 0;
  /* Suppress free-form JSON dumps; keep dual-wire honesty plates. */
  if (!debug_mode && ln[0] == '{' && !gkx_is_grokium_plate_line(ln)) return 0;
  return 1;
}

int gkx_filter_tool_block(const char *text, int debug_mode, char *out,
                          size_t cap) {
  char *dup, *save = NULL, *ln;
  size_t o = 0;
  int n = 0;
  if (!out || cap < 2) return -1;
  out[0] = 0;
  if (!text || !text[0]) return 0;
  dup = strdup(text);
  if (!dup) return -1;
  for (ln = strtok_r(dup, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
    size_t L;
    if (!gkx_log_block_keep_line(ln, debug_mode)) continue;
    L = strlen(ln);
    /* need L + optional '\n' + NUL */
    if (o + L + 2 > cap) {
      free(dup);
      out[0] = 0;
      return -1;
    }
    memcpy(out + o, ln, L);
    o += L;
    out[o++] = '\n';
    out[o] = 0;
    n++;
  }
  free(dup);
  return n;
}
