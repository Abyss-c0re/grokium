/* SPDX-License-Identifier: Apache-2.0
 * Pure-C selftest for c_core session dual-wire plates (no HTTP).
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
         strstr(plate, "\"share\":\"state_matrix_only\"") &&
         strstr(plate, "\"hold_flash\":1");
}

int main(void) {
  char plate[768];

  if (gk_session_id_safe(NULL) || gk_session_id_safe("") ||
      gk_session_id_safe("../evil") || gk_session_id_safe("abc;rm"))
    return fail("unsafe ids must reject");
  if (!gk_session_id_safe("deadbeef-01") ||
      !gk_session_id_safe("01234567-89ab-cdef-0123-456789abcdef"))
    return fail("safe hex ids must accept");

  if (gk_session_pickup_deny_json(NULL, "bad_session_id", plate, sizeof plate) !=
      0)
    return fail("deny bad_session_id");
  if (!strstr(plate, "\"schema\":\"grokium.session_pickup.v1\"") ||
      !strstr(plate, "\"error\":\"bad_session_id\"") ||
      !plate_dual_wire(plate) || strstr(plate, "\"id\":"))
    return fail("bad_session_id plate");

  if (gk_session_pickup_deny_json(NULL, "need_session_id", plate,
                                  sizeof plate) != 0)
    return fail("deny need_session_id");
  if (!strstr(plate, "\"error\":\"need_session_id\"") || !plate_dual_wire(plate))
    return fail("need_session_id dual-wire");

  if (gk_session_pickup_deny_json("deadbeef", "not_found", plate,
                                  sizeof plate) != 0)
    return fail("deny not_found");
  if (!strstr(plate, "\"id\":\"deadbeef\"") ||
      !strstr(plate, "\"error\":\"not_found\"") || !plate_dual_wire(plate))
    return fail("not_found plate");

  if (gk_session_pickup_deny_json("../evil;drop", "not_found", plate,
                                  sizeof plate) != 0)
    return fail("deny inject");
  if (!strstr(plate, "\"id\":\"ed\"") || strstr(plate, "../") ||
      strstr(plate, "evil") || strstr(plate, ";drop"))
    return fail("id injection not sanitized");

  if (gk_session_list_empty_json("q\"x", "data/import", "no_import_dir", plate,
                                 sizeof plate) != 0)
    return fail("list empty");
  if (!strstr(plate, "\"schema\":\"grokium.sessions.v1\"") ||
      !strstr(plate, "\"error\":\"no_import_dir\"") ||
      !strstr(plate, "\"q\":\"q\\\"x\"") || !plate_dual_wire(plate))
    return fail("list empty plate");

  /* import_dir quote inject must be JSON-escaped on the plate. */
  if (gk_session_list_empty_json("", "data/im\"port", "no_import_dir", plate,
                                 sizeof plate) != 0)
    return fail("list empty dir inject");
  if (!strstr(plate, "\"import_dir\":\"data/im\\\"port\"") ||
      strstr(plate, "im\"port") || !plate_dual_wire(plate))
    return fail("import_dir not escaped");

  printf("C_CORE_SESSION_PLATE_OK dual_wire=honest id_safe=ok meta_only=1\n");
  return 0;
}
