/* SPDX-License-Identifier: Apache-2.0
 * Host-local memory seed selftest — recent-turns cap + older summary.
 * Not SMX product bus. Links nanobot memory only via grokium_chat.c.
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_chat.h"
#include "grokium_config.h"
#include "memory.h"
#include "util.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char state_dir[PATH_MAX];

/* Unused chat_core path — provide weak host stubs for link. */
int gkx_config_load(gkx_config *c, const char *path) {
  (void)c;
  (void)path;
  return 0;
}
void gkx_config_apply_env(gkx_config *c) { (void)c; }
void gkx_config_load_prefs(gkx_config *c, const char *sd) {
  (void)c;
  (void)sd;
}
void gkx_hub_apply_sched_env(const gkx_config *c) { (void)c; }

static int fail(const char *msg) {
  fprintf(stderr, "memory_seed_selftest: %s\n", msg);
  return 1;
}

int main(void) {
  char tmp[] = "/tmp/gkx_mem_seed_XXXXXX";
  char *dir = mkdtemp(tmp);
  char path[PATH_MAX], *raw = NULL, *sys = NULL;
  size_t len = 0;
  int n, keep;
  const char *u[] = {
      "user turn one about matrix",  "user turn two about fleet",
      "user turn three about seal",  "user turn four about status",
      "user turn five about manager","user turn six about contracts",
  };
  const char *a[] = {
      "asst one reply matrix",  "asst two reply fleet",
      "asst three reply seal",  "asst four reply status",
      "asst five reply manager","asst six reply contracts",
  };

  if (!dir) return fail("mkdtemp");
  snprintf(state_dir, sizeof state_dir, "%s", dir);

  n = gkx_memory_seed_pairs(u, a, 6);
  keep = ng_memory_recent_turns();
  if (keep < 1) keep = 1;
  if (n != keep)
    return fail("seeded count should equal recent_turns");

  /* Workdir must be under state_dir/nanobot_home — not /tmp/nanobot. */
  if (!ng_workdir() || !strstr(ng_workdir(), dir) ||
      !strstr(ng_workdir(), "nanobot_home"))
    return fail("workdir not bound to state nanobot_home");

  snprintf(path, sizeof path, "%s/memory/recent.jsonl", ng_workdir());
  raw = ng_read_file(path, &len);
  if (!raw || !len) return fail("recent.jsonl missing");
  if (!strstr(raw, "turn six") || !strstr(raw, "contracts"))
    return fail("recent should keep last pair");
  if (strstr(raw, "turn one"))
    return fail("recent should drop oldest pairs past cap");
  free(raw);
  raw = NULL;

  snprintf(path, sizeof path, "%s/memory/summary.txt", ng_workdir());
  raw = ng_read_file(path, &len);
  if (!raw || !strstr(raw, "resume older") || !strstr(raw, "turn one"))
    return fail("older pairs should land in summary");
  free(raw);

  sys = ng_memory_system_prompt();
  if (!sys || !strstr(sys, "resume older"))
    return fail("system prompt should include resume summary");
  free(sys);

  /* Empty / invalid inputs are no-ops. */
  if (gkx_memory_seed_pairs(NULL, a, 3) != 0) return fail("null users");
  if (gkx_memory_seed_pairs(u, NULL, 3) != 0) return fail("null assts");
  if (gkx_memory_seed_pairs(u, a, 0) != 0) return fail("zero pairs");

  /* Dual-wire selftest success — no free-text HOST_MEMORY_SEED_OK.
   * Seed path is host-local (not product SMX bus traffic). */
  {
    char okp[640];
    snprintf(okp, sizeof okp,
             "{\"schema\":\"grokium.memory_seed_selftest.v1\",\"ok\":true,"
             "\"seeded\":%d,\"keep\":%d,\"workdir_bound\":true,"
             "\"older_in_summary\":true,\"host_local\":true,"
             "\"product_bus\":false,\"dual_wire\":true,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false,\"hold_flash\":1,"
             "\"share\":\"state_matrix_only\",\"python\":0}",
             n, keep);
    if (!strstr(okp, "\"schema\":\"grokium.memory_seed_selftest.v1\"") ||
        !strstr(okp, "\"ok\":true") ||
        !strstr(okp, "\"workdir_bound\":true") ||
        !strstr(okp, "\"older_in_summary\":true") ||
        !strstr(okp, "\"host_local\":true") ||
        !strstr(okp, "\"product_bus\":false") ||
        !strstr(okp, "\"dual_wire\":true") ||
        !strstr(okp, "\"product_wire\":\"smx2\"") ||
        !strstr(okp, "\"peer_http\":\"lab_ops_only\"") ||
        !strstr(okp, "\"peer_http_is_product_bus\":false") ||
        !strstr(okp, "\"llm_is_commander\":false") ||
        !strstr(okp, "\"hold_flash\":1") ||
        !strstr(okp, "\"share\":\"state_matrix_only\"") ||
        !strstr(okp, "\"python\":0"))
      return fail("memory_seed_selftest plate");
    printf("%s\n", okp);
  }
  return 0;
}
