/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_integrity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  const char *root = getenv("GROKIUM_ROOT");
  char out[4096];
  int rc;
  if (!root || !root[0]) root = ".";
  if (argc < 2) {
    fprintf(stderr,
            "grokium-integrity tick|policy|reseal [ROOT]\n"
            "  tick   — verify CODE_SEAL + privacy (fail-closed)\n"
            "  policy — print POLICY.json\n"
            "  reseal — intentional rewrite of code seal\n");
    return 2;
  }
  if (argc > 2) root = argv[2];
  if (!strcmp(argv[1], "tick")) {
    rc = gk_integrity_tick(root, out, sizeof out);
    puts(out);
    return rc == 1 ? 0 : 1;
  }
  if (!strcmp(argv[1], "policy")) {
    rc = gk_integrity_policy(root, out, sizeof out);
    puts(out);
    return rc == 0 ? 0 : 1;
  }
  if (!strcmp(argv[1], "reseal")) {
    rc = gk_integrity_reseal(root, out, sizeof out);
    puts(out);
    return rc == 0 ? 0 : 1;
  }
  fprintf(stderr, "unknown cmd\n");
  return 2;
}
