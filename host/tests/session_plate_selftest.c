/* SPDX-License-Identifier: Apache-2.0
 * Pure-C selftest for shared c_core session dual-wire plates (no nanobot).
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_session.h"
#include <stdio.h>
#include <string.h>

static int fail(const char *msg) {
  fprintf(stderr, "session_plate_selftest: %s\n", msg);
  return 1;
}

static int plate_dual_wire(const char *plate) {
  return strstr(plate, "\"content\":\"meta_only\"") &&
         strstr(plate, "\"product_wire\":\"smx2\"") &&
         strstr(plate, "\"peer_http\":\"lab_ops_only\"") &&
         strstr(plate, "\"peer_http_is_product_bus\":false") &&
         strstr(plate, "\"llm_is_commander\":false") &&
         strstr(plate, "\"python\":0") &&
         strstr(plate, "\"share\":\"state_matrix_only\"") &&
         strstr(plate, "\"hold_flash\":1");
}

int main(void) {
  char plate[768];

  if (gk_session_id_safe(NULL) || gk_session_id_safe("") ||
      gk_session_id_safe("not a real id") ||
      gk_session_id_safe("../../etc/passwd") ||
      gk_session_id_safe("abc;rm"))
    return fail("unsafe ids must reject");
  if (!gk_session_id_safe("a1b2c3d4") ||
      !gk_session_id_safe("A1B2-c3d4-ef00") ||
      !gk_session_id_safe("01234567-89ab-cdef-0123-456789abcdef"))
    return fail("safe hex/dash ids must accept");

  if (gk_session_pickup_deny_json(NULL, "bad_session_id", plate,
                                   sizeof plate) != 0)
    return fail("deny bad_session_id");
  if (!strstr(plate, "\"schema\":\"grokium.session_pickup.v1\"") ||
      !strstr(plate, "\"ok\":false") ||
      !strstr(plate, "\"error\":\"bad_session_id\"") ||
      !strstr(plate, "\"resume_available\":false") || !plate_dual_wire(plate)) {
    fprintf(stderr, "session_plate_selftest: bad_id plate: %s\n", plate);
    return 1;
  }
  if (strstr(plate, "\"id\":"))
    return fail("bad_session_id must omit id field");

  if (gk_session_pickup_deny_json("deadbeef-01", "not_found", plate,
                                   sizeof plate) != 0)
    return fail("deny not_found");
  if (!strstr(plate, "\"error\":\"not_found\"") ||
      !strstr(plate, "\"id\":\"deadbeef-01\"") || !plate_dual_wire(plate)) {
    fprintf(stderr, "session_plate_selftest: not_found plate: %s\n", plate);
    return 1;
  }

  /* Injection: free-text / path chars must not survive in the id field. */
  if (gk_session_pickup_deny_json("../evil;drop", "not_found", plate,
                                   sizeof plate) != 0)
    return fail("deny inject id");
  if (!strstr(plate, "\"id\":\"ed\"") || strstr(plate, "../") ||
      strstr(plate, "evil") || strstr(plate, ";drop"))
    return fail("id injection not sanitized");

  if (gk_session_pickup_deny_json(NULL, "need_session_id", plate,
                                   sizeof plate) != 0)
    return fail("deny need_session_id");
  if (!strstr(plate, "\"error\":\"need_session_id\"") || !plate_dual_wire(plate))
    return fail("need_session_id dual-wire");

  /* Error token sanitize (path/prose → machine token). */
  if (gk_session_pickup_deny_json(NULL, "path/with:spaces", plate,
                                   sizeof plate) != 0)
    return fail("deny err sanitize");
  if (!strstr(plate, "\"error\":\"path_with_spaces\"") ||
      strstr(plate, "path/with"))
    return fail("error token sanitize");

  if (gk_session_list_empty_json("q\"x", "/tmp/import", "no_import_dir",
                                  plate, sizeof plate) != 0)
    return fail("list empty");
  if (!strstr(plate, "\"schema\":\"grokium.sessions.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"n\":0") ||
      !strstr(plate, "\"sessions\":[]") ||
      !strstr(plate, "\"error\":\"no_import_dir\"") ||
      !strstr(plate, "\"q\":\"q\\\"x\"") || !plate_dual_wire(plate) ||
      !strstr(plate, "\"telemetry\":\"off\"")) {
    fprintf(stderr, "session_plate_selftest: list empty plate: %s\n", plate);
    return 1;
  }

  /* import_dir quote inject must be JSON-escaped on the plate. */
  if (gk_session_list_empty_json("", "data/im\"port", "no_import_dir", plate,
                                  sizeof plate) != 0)
    return fail("list empty dir inject");
  if (!strstr(plate, "\"import_dir\":\"data/im\\\"port\"") ||
      strstr(plate, "im\"port") || !plate_dual_wire(plate))
    return fail("import_dir not escaped");

  if (gk_session_list_empty_json("", "data/import", NULL, plate,
                                  sizeof plate) != 0)
    return fail("list empty no err");
  if (strstr(plate, "\"error\"") || !plate_dual_wire(plate))
    return fail("empty list without error field");

  /* Host CLI sessions help|-h|--help share this dual-wire help plate. */
  if (gk_session_help_json(plate, sizeof plate) != 0)
    return fail("session help");
  if (!strstr(plate, "\"schema\":\"grokium.sessions.v1\"") ||
      !strstr(plate, "\"ok\":false") ||
      !strstr(plate, "\"error\":\"need_query_or_pickup\"") ||
      !strstr(plate, "\"telemetry\":\"off\"") ||
      !strstr(plate, "sessions pickup|load") ||
      !strstr(plate, "no transcripts") || !plate_dual_wire(plate)) {
    fprintf(stderr, "session_plate_selftest: help plate: %s\n", plate);
    return 1;
  }

  printf("HOST_SESSION_PLATE_OK shared=c_core dual_wire=honest id_safe=ok "
         "meta_only=1 help=1 python=0\n");
  return 0;
}
