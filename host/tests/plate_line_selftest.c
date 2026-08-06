/* SPDX-License-Identifier: Apache-2.0
 * Pure-C selftest: TUI tool-capture plate-line filter (no nanobot/ncurses).
 * Dual-wire grokium.* plates surface; free-form JSON dumps stay hidden.
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_plate.h"
#include <stdio.h>
#include <string.h>

static int fail(const char *msg) {
  fprintf(stderr, "plate_line_selftest: %s\n", msg);
  return 1;
}

int main(void) {
  char out[1024];
  int n;
  const char *plate =
      "{\"schema\":\"grokium.coord.v1\",\"ok\":true,"
      "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
      "\"peer_http_is_product_bus\":false,\"hold_flash\":1,"
      "\"share\":\"state_matrix_only\",\"llm_is_commander\":false}";
  const char *free_json = "{\"foo\":1,\"bar\":\"dump\"}";
  const char *prose = "coord> ok · product_wire=smx2";
  const char *spaced =
      "{ \"schema\": \"grokium.coord.v1\", \"ok\": true }"; /* not compact */
  const char *other_schema =
      "{\"schema\":\"other.v1\",\"ok\":true}";

  if (gkx_is_grokium_plate_line(NULL)) return fail("null is plate");
  if (gkx_is_grokium_plate_line("")) return fail("empty is plate");
  if (gkx_is_grokium_plate_line(prose)) return fail("prose is plate");
  if (gkx_is_grokium_plate_line(free_json)) return fail("free_json is plate");
  if (gkx_is_grokium_plate_line(other_schema))
    return fail("other schema must not match");
  /* Strict compact "schema":"grokium. — spaced form is free-form for filter. */
  if (gkx_is_grokium_plate_line(spaced))
    return fail("spaced schema must not match compact plate detector");
  if (!gkx_is_grokium_plate_line(plate)) return fail("coord plate not detected");
  if (!gkx_is_grokium_plate_line(
          "{\"schema\":\"grokium.manager_tick.v1\",\"ok\":false}"))
    return fail("manager plate not detected");
  if (!gkx_is_grokium_plate_line(
          "{\"schema\":\"grokium.license.v1\",\"ok\":true}"))
    return fail("license plate not detected");

  if (!gkx_log_block_keep_line(prose, 0)) return fail("keep prose");
  if (!gkx_log_block_keep_line(plate, 0)) return fail("keep plate !debug");
  if (gkx_log_block_keep_line(free_json, 0))
    return fail("drop free_json !debug");
  if (!gkx_log_block_keep_line(free_json, 1))
    return fail("keep free_json debug");
  if (gkx_log_block_keep_line("", 0)) return fail("drop empty");
  if (gkx_log_block_keep_line(NULL, 0)) return fail("drop null");

  {
    char block[768];
    snprintf(block, sizeof block, "%s\n%s\n%s\n%s\n", prose, free_json, plate,
             other_schema);
    n = gkx_filter_tool_block(block, 0, out, sizeof out);
    if (n != 2) return fail("filter !debug kept count");
    if (!strstr(out, prose)) return fail("filter kept prose");
    if (!strstr(out, "grokium.coord.v1")) return fail("filter kept plate");
    if (strstr(out, "\"foo\":1")) return fail("filter dropped free_json");
    if (strstr(out, "other.v1")) return fail("filter dropped other schema");

    n = gkx_filter_tool_block(block, 1, out, sizeof out);
    if (n != 4) return fail("filter debug kept all JSON lines");
    if (!strstr(out, "\"foo\":1") || !strstr(out, "other.v1"))
      return fail("filter debug keeps free-form JSON");
  }

  if (gkx_filter_tool_block("a\nb\n", 0, out, 4) != -1)
    return fail("overflow should fail");
  if (gkx_filter_tool_block(NULL, 0, out, sizeof out) != 0)
    return fail("null text");
  if (gkx_filter_tool_block("", 0, out, sizeof out) != 0)
    return fail("empty text");

  /* Human UI line — no raw JSON on chat surface */
  if (gkx_plate_ui_line(NULL, 0, out, sizeof out) != 0)
    return fail("ui_line null");
  if (gkx_plate_ui_line(plate, 0, out, sizeof out) != 0)
    return fail("ui_line plate");
  if (!strstr(out, "coord"))
    return fail("ui_line should humanize plate");
  if (strstr(out, "\"schema\""))
    return fail("ui_line must not show raw schema JSON");
  if (gkx_plate_ui_line(plate, 1, out, sizeof out) != 0)
    return fail("ui_line debug");
  if (!strstr(out, "\"schema\""))
    return fail("ui_line debug keeps JSON");
  {
    const char *ready =
        "{\"schema\":\"grokium.ready.v1\",\"ok\":true,\"hub\":true,"
        "\"tools\":true,\"multiline\":true}";
    gkx_plate_ui_line(ready, 0, out, sizeof out);
    if (!strstr(out, "ready") || !strstr(out, "Enter=send"))
      return fail("ui_line ready human");
  }

  printf("HOST_PLATE_LINE_OK dual_wire=keep free_json=drop debug=pass "
         "compact_schema=1 ui_line=human\n");
  return 0;
}
