/* SPDX-License-Identifier: Apache-2.0
 * grokium-smx-filter — contracts + manager + SMX gate CLI
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_smx_filter.h"
#include "grokium_smx.h"
#include "grokium_algocube.h"
#include "grokium_law.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
  fprintf(stderr,
          "grokium-smx-filter — Hive Mind filter / contracts\n"
          "  form --assignee ID --task TEXT [--digit N] [--min-set N] [--smx-sha HEX]\n"
          "  validate PATH [--bits 01...] [--bits-file F]\n"
          "  manager-tick [DIR]\n"
          "  allow-check STRING\n"
          "  instinct\n"
          "  heartbeat-ack\n");
}

int main(int argc, char **argv) {
  if (argc < 2) { usage(); return 2; }

  if (!strcmp(argv[1], "instinct")) {
    printf("%s\n", grokium_hive_instinct_creed());
    return 0;
  }

  if (!strcmp(argv[1], "heartbeat-ack")) {
    printf("NEXUS_COORD v1 | from=grokium-core | type=heartbeat_ack | "
           "role=kernel_sot | status=ONLINE | HOLD_FLASH=ack_held | "
           "observer=NexusCore | hive_mind=1 | filter=1 |\n");
    return 0;
  }

  if (!strcmp(argv[1], "allow-check")) {
    grokium_law L;
    const char *s = argc > 2 ? argv[2] : "";
    grokium_law_default(&L);
    printf("{\"allow\":%s,\"prose\":%s}\n",
           grokium_smx_filter_allow_frame(&L, (const uint8_t *)s, strlen(s), 1)
               ? "true"
               : "false",
           grokium_smx_filter_is_prose(s, strlen(s)) ? "true" : "false");
    return 0;
  }

  if (!strcmp(argv[1], "manager-tick")) {
    int n = grokium_manager_motivate_dir(argc > 2 ? argv[2] : NULL);
    printf("{\"motivated\":%d,\"observer\":\"NexusCore\"}\n", n);
    return 0;
  }

  if (!strcmp(argv[1], "form")) {
    const char *assignee = NULL, *task = NULL, *sha = NULL;
    int digit = -1, min_set = 0;
    grokium_contract c;
    int i;
    for (i = 2; i < argc; i++) {
      if (!strcmp(argv[i], "--assignee") && i + 1 < argc)
        assignee = argv[++i];
      else if (!strcmp(argv[i], "--task") && i + 1 < argc)
        task = argv[++i];
      else if (!strcmp(argv[i], "--digit") && i + 1 < argc)
        digit = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--min-set") && i + 1 < argc)
        min_set = atoi(argv[++i]);
      else if (!strcmp(argv[i], "--smx-sha") && i + 1 < argc)
        sha = argv[++i];
    }
    if (!assignee || !task) {
      fprintf(stderr, "form needs --assignee and --task\n");
      return 2;
    }
    if (grokium_contract_form(&c, NULL, assignee, task, digit, min_set, sha) !=
        0) {
      fprintf(stderr, "form failed\n");
      return 1;
    }
    printf("{\"ok\":true,\"id\":\"%s\",\"path\":\"%s\",\"status\":\"open\"}\n",
           c.id, c.path);
    return 0;
  }

  if (!strcmp(argv[1], "validate")) {
    grokium_contract c;
    grokium_smx m;
    const char *path = argc > 2 ? argv[2] : NULL;
    const char *bits = NULL;
    int i, rc, dig;
    if (!path) {
      usage();
      return 2;
    }
    for (i = 3; i < argc; i++) {
      if (!strcmp(argv[i], "--bits") && i + 1 < argc)
        bits = argv[++i];
      else if (!strcmp(argv[i], "--bits-file") && i + 1 < argc) {
        FILE *f = fopen(argv[++i], "r");
        static char buf[600];
        if (!f) {
          perror("bits-file");
          return 1;
        }
        if (!fgets(buf, sizeof buf, f)) buf[0] = 0;
        fclose(f);
        bits = buf;
      }
    }
    if (grokium_contract_load(&c, path) != 0) {
      fprintf(stderr, "load failed\n");
      return 1;
    }
    smx_clear(&m, "result");
    if (bits && bits[0])
      smx_ingest_bits_ascii(&m, bits);
    else {
      /* empty result cannot pass min_set */
      smx_set(&m, 0, 0, 0, 1);
    }
    dig = algocube_digit(&m, c.id);
    rc = grokium_contract_validate(&c, &m, dig);
    printf("{\"ok\":%s,\"complete\":%s,\"status\":%d,\"digit\":%d,"
           "\"bits_set\":%u,\"id\":\"%s\"}\n",
           rc >= 0 ? "true" : "false", rc == 1 ? "true" : "false",
           (int)c.status, dig, m.bits_set, c.id);
    return rc == 1 ? 0 : 1;
  }

  usage();
  return 2;
}
