/* SPDX-License-Identifier: Apache-2.0 */
#include "grokium_algocube.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>

int algocube_digit(const grokium_smx *m, const char *salt) {
  uint8_t dig[32];
  char buf[GROKIUM_CELLS + 64];
  size_t n = 0;
  if (!m) return 0;
  if (salt && salt[0]) {
    size_t sl = strlen(salt);
    if (sl > 63) sl = 63;
    memcpy(buf, salt, sl);
    n = sl;
  }
  memcpy(buf + n, m->cell, GROKIUM_CELLS);
  n += GROKIUM_CELLS;
  gk_sha256(buf, n, dig);
  return (int)(dig[0] % 10);
}

void algocube_blueprint10(const grokium_smx *m, uint8_t dig[10]) {
  int i;
  if (!dig) return;
  for (i = 0; i < 10; i++) {
    char salt[8];
    snprintf(salt, sizeof salt, "d%d", i);
    dig[i] = (uint8_t)algocube_digit(m, salt);
  }
}
