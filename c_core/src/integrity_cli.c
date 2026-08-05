/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_integrity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Dual-wire deny plate — no free-text usage on the machine wire. */
static const char k_need_subcmd[] =
    "{\"schema\":\"grokium.integrity_report.v1\",\"ok\":false,"
    "\"error\":\"need_subcmd\",\"product_wire\":\"smx2\","
    "\"peer_http\":\"lab_ops_only\",\"peer_http_is_product_bus\":false,"
    "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
    "\"llm_is_commander\":false,"
    "\"hint\":\"tick|policy|reseal\"}";

static int plate_dual_wire_ok(const char *p) {
  return p && strstr(p, "\"product_wire\":\"smx2\"") &&
         strstr(p, "\"peer_http\":\"lab_ops_only\"") &&
         strstr(p, "\"peer_http_is_product_bus\":false") &&
         strstr(p, "\"llm_is_commander\":false") &&
         strstr(p, "\"hold_flash\":1") &&
         strstr(p, "\"share\":\"state_matrix_only\"");
}

int main(int argc, char **argv) {
  const char *root = getenv("GROKIUM_ROOT");
  char out[4096];
  int rc;
  if (!root || !root[0]) root = ".";

  if (!plate_dual_wire_ok(k_need_subcmd) ||
      !strstr(k_need_subcmd, "\"error\":\"need_subcmd\"")) {
    fprintf(stderr, "grokium-integrity: deny plate dual-wire fail\n");
    return 1;
  }

  if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "-h") ||
      !strcmp(argv[1], "--help")) {
    printf("%s\n", k_need_subcmd);
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
  printf("%s\n", k_need_subcmd);
  return 2;
}
