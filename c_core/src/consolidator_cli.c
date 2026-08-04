/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_consolidator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv) {
  gk_consolidator C;
  char ability[512];
  const char *dir = "data/knowledge";
  double now = (double)time(NULL);
  if (argc < 2) {
    fprintf(stderr,
            "grokium-consolidate selftest|ingest TEXT|ability [dir]|save [dir]\n");
    return 2;
  }
  gk_init(&C, "grokium-core");
  if (!strcmp(argv[1], "selftest")) {
    gk_ingest(&C, "ep1", "NEXUS_COORD v1 | type=seed | HOLD_FLASH=1 |", 40, now);
    gk_ingest(&C, "ep2", "SMX bits 01010101111000001111", 28, now - 100);
    gk_ingest(&C, "ep3", "NEXUS_COORD v1 | type=seed | HOLD_FLASH=1 |", 40, now);
    /* ep3 dedups with ep1 */
    if (C.n_items != 2) {
      fprintf(stderr, "dedup fail n=%d\n", C.n_items);
      return 1;
    }
    gk_consolidate(&C, now);
    gk_ability(&C, now, ability, sizeof ability);
    printf("%s\n", ability);
    if (!C.seal_ok) return 1;
    printf("CONSOLIDATOR_OK grade=%s concepts=%d bits=%u\n", C.grade,
           C.n_concepts, C.matrix.bits_set);
    return 0;
  }
  if (!strcmp(argv[1], "ingest") && argc > 2) {
    int i;
    for (i = 2; i < argc; i++)
      gk_ingest(&C, NULL, argv[i], strlen(argv[i]), now);
    gk_consolidate(&C, now);
    if (argc > 1) {
      /* fall through to ability */
    }
    gk_ability(&C, now, ability, sizeof ability);
    printf("%s\n", ability);
    return C.seal_ok ? 0 : 1;
  }
  if (!strcmp(argv[1], "ability")) {
    if (argc > 2) dir = argv[2];
    if (gk_load_dir(&C, dir) != 0) gk_init(&C, "empty");
    gk_ability(&C, now, ability, sizeof ability);
    printf("%s\n", ability);
    return 0;
  }
  if (!strcmp(argv[1], "save")) {
    if (argc > 2) dir = argv[2];
    gk_ingest(&C, "boot", "NEXUS_COORD v1 | from=consolidate | HOLD_FLASH=ack_held |",
              56, now);
    gk_consolidate(&C, now);
    if (gk_save_dir(&C, dir) != 0) return 1;
    printf("{\"ok\":true,\"dir\":\"%s\",\"grade\":\"%s\"}\n", dir, C.grade);
    return 0;
  }
  return 2;
}
