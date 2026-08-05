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

/* Dual-wire deny plates — no free-text prose on the machine wire. */
static const char k_need_subcmd[] =
    "{\"schema\":\"grokium.smx_filter.v1\",\"ok\":false,"
    "\"error\":\"need_subcmd\",\"product_wire\":\"smx2\","
    "\"peer_http\":\"lab_ops_only\",\"peer_http_is_product_bus\":false,"
    "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
    "\"llm_is_commander\":false,"
    "\"hint\":\"form|validate|manager-tick|allow-check|instinct|"
    "heartbeat-ack\"}";

static int plate_dual_wire_ok(const char *p) {
  return p && strstr(p, "\"product_wire\":\"smx2\"") &&
         strstr(p, "\"peer_http\":\"lab_ops_only\"") &&
         strstr(p, "\"peer_http_is_product_bus\":false") &&
         strstr(p, "\"llm_is_commander\":false") &&
         strstr(p, "\"hold_flash\":1") &&
         strstr(p, "\"share\":\"state_matrix_only\"");
}

static void usage(void) { printf("%s\n", k_need_subcmd); }

int main(int argc, char **argv) {
  {
    char den[512];
    if (!plate_dual_wire_ok(k_need_subcmd) ||
        !strstr(k_need_subcmd, "\"error\":\"need_subcmd\"")) {
      fprintf(stderr, "grokium-smx-filter: deny plate dual-wire fail\n");
      return 1;
    }
    grokium_contract_form_err_json("need_assignee_and_task", den, sizeof den);
    if (!plate_dual_wire_ok(den) ||
        !strstr(den, "\"schema\":\"grokium.contract_form.v1\"") ||
        !strstr(den, "\"error\":\"need_assignee_and_task\"")) {
      fprintf(stderr, "grokium-smx-filter: form deny dual-wire fail: %.200s\n",
              den);
      return 1;
    }
    grokium_contract_form_err_json("form_failed", den, sizeof den);
    if (!plate_dual_wire_ok(den) || !strstr(den, "\"error\":\"form_failed\"")) {
      fprintf(stderr, "grokium-smx-filter: form_failed dual-wire fail\n");
      return 1;
    }
    grokium_contract_validate_err_json("need_path", den, sizeof den);
    if (!plate_dual_wire_ok(den) ||
        !strstr(den, "\"schema\":\"grokium.contract_validate.v1\"") ||
        !strstr(den, "\"error\":\"need_path\"")) {
      fprintf(stderr, "grokium-smx-filter: validate need_path dual-wire fail\n");
      return 1;
    }
    grokium_contract_validate_err_json("contract_not_found", den, sizeof den);
    if (!plate_dual_wire_ok(den) ||
        !strstr(den, "\"error\":\"contract_not_found\"")) {
      fprintf(stderr, "grokium-smx-filter: validate not_found dual-wire fail\n");
      return 1;
    }
  }
  if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "-h") ||
      !strcmp(argv[1], "--help")) {
    usage();
    return 2;
  }

  if (!strcmp(argv[1], "instinct")) {
    char plate[1024];
    /* Same dual-wire plate as GET /v1/instinct (creed embedded). */
    grokium_instinct_json(plate, sizeof plate);
    printf("%s\n", plate);
    return 0;
  }

  if (!strcmp(argv[1], "heartbeat-ack")) {
    /* Machine NEXUS_COORD with dual-wire honesty (SMX product bus ≠ peer HTTP). */
    printf("NEXUS_COORD v1 | from=grokium-core | type=heartbeat_ack | "
           "role=kernel_sot | status=ONLINE | HOLD_FLASH=ack_held | "
           "observer=NexusCore | hive_mind=1 | filter=1 | "
           "share=state_matrix_only | product_wire=smx2 | "
           "peer_http=lab_ops_only | peer_http_is_product_bus=0 | "
           "llm_is_commander=0 |\n");
    return 0;
  }

  if (!strcmp(argv[1], "allow-check")) {
    char plate[512];
    grokium_law L;
    const char *s = argc > 2 ? argv[2] : "";
    int allow, prose;
    grokium_law_default(&L);
    allow =
        grokium_smx_filter_allow_frame(&L, (const uint8_t *)s, strlen(s), 1);
    prose = grokium_smx_filter_is_prose(s, strlen(s));
    grokium_smx_allow_json(allow, prose, plate, sizeof plate);
    printf("%s\n", plate);
    return 0;
  }

  if (!strcmp(argv[1], "manager-tick")) {
    char plate[1024];
    const char *dir = argc > 2 ? argv[2] : NULL;
    int n = grokium_manager_motivate_dir(dir);
    /* Same dual-wire plate as GET/POST /v1/manager/tick. */
    grokium_manager_tick_json(n, dir, plate, sizeof plate);
    printf("%s\n", plate);
    return 0;
  }

  if (!strcmp(argv[1], "form")) {
    char plate[1024];
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
      grokium_contract_form_err_json("need_assignee_and_task", plate,
                                     sizeof plate);
      printf("%s\n", plate);
      return 2;
    }
    if (grokium_contract_form(&c, NULL, assignee, task, digit, min_set, sha) !=
        0) {
      grokium_contract_form_err_json("form_failed", plate, sizeof plate);
      printf("%s\n", plate);
      return 1;
    }
    /* Same dual-wire plate as POST /v1/contract/form. */
    grokium_contract_form_json(&c, plate, sizeof plate);
    printf("%s\n", plate);
    return 0;
  }

  if (!strcmp(argv[1], "validate")) {
    char plate[1024];
    grokium_contract c;
    grokium_smx m;
    const char *path = argc > 2 ? argv[2] : NULL;
    const char *bits = NULL;
    int i, rc, dig;
    if (!path) {
      grokium_contract_validate_err_json("need_path", plate, sizeof plate);
      printf("%s\n", plate);
      return 2;
    }
    for (i = 3; i < argc; i++) {
      if (!strcmp(argv[i], "--bits") && i + 1 < argc)
        bits = argv[++i];
      else if (!strcmp(argv[i], "--bits-file") && i + 1 < argc) {
        FILE *f = fopen(argv[++i], "r");
        static char buf[600];
        if (!f) {
          grokium_contract_validate_err_json("contract_not_found", plate,
                                             sizeof plate);
          printf("%s\n", plate);
          return 1;
        }
        if (!fgets(buf, sizeof buf, f)) buf[0] = 0;
        fclose(f);
        bits = buf;
      }
    }
    if (grokium_contract_load(&c, path) != 0) {
      grokium_contract_validate_err_json("contract_not_found", plate,
                                         sizeof plate);
      printf("%s\n", plate);
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
    /* Same dual-wire plate as POST /v1/contract/validate. */
    grokium_contract_validate_json(&c, rc, dig, m.bits_set, plate, sizeof plate);
    printf("%s\n", plate);
    return rc == 1 ? 0 : 1;
  }

  usage();
  return 2;
}
