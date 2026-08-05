/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_integrity.h"
#include "grokium_law.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int plate_dual_wire_ok(const char *p) {
  return p && strstr(p, "\"product_wire\":\"smx2\"") &&
         strstr(p, "\"peer_http\":\"lab_ops_only\"") &&
         strstr(p, "\"peer_http_is_product_bus\":false") &&
         strstr(p, "\"llm_is_commander\":false") &&
         strstr(p, "\"hold_flash\":1") &&
         strstr(p, "\"share\":\"state_matrix_only\"");
}

/* Shared dual-wire need_subcmd (host integrity help same builder). */
static void emit_need_subcmd(void) {
  char plate[512];
  grokium_need_subcmd_json("integrity_report", "tick|policy|reseal", plate,
                           sizeof plate);
  printf("%s\n", plate);
}

int main(int argc, char **argv) {
  const char *root = getenv("GROKIUM_ROOT");
  char out[4096], plate[512];
  int rc;
  if (!root || !root[0]) root = ".";

  grokium_need_subcmd_json("integrity_report", "tick|policy|reseal", plate,
                           sizeof plate);
  if (!plate_dual_wire_ok(plate) ||
      !strstr(plate, "\"error\":\"need_subcmd\"") ||
      !strstr(plate, "\"schema\":\"grokium.integrity_report.v1\"")) {
    fprintf(stderr, "grokium-integrity: deny plate dual-wire fail\n");
    return 1;
  }

  if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "-h") ||
      !strcmp(argv[1], "--help")) {
    emit_need_subcmd();
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
  /* Unknown subcmd — same dual-wire need_subcmd plate. */
  emit_need_subcmd();
  return 2;
}
