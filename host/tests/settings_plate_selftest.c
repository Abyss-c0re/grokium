/* SPDX-License-Identifier: Apache-2.0
 * Pure-C selftest for dual-wire settings plate (no nanobot/ncurses).
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_config.h"
#include <stdio.h>
#include <string.h>

static int fail(const char *msg) {
  fprintf(stderr, "settings_plate_selftest: %s\n", msg);
  return 1;
}

static int plate_dual_wire(const char *p) {
  return p && strstr(p, "\"product_wire\":\"smx2\"") &&
         strstr(p, "\"peer_http\":\"lab_ops_only\"") &&
         strstr(p, "\"peer_http_is_product_bus\":false") &&
         strstr(p, "\"share\":\"state_matrix_only\"") &&
         strstr(p, "\"hold_flash\":1") &&
         strstr(p, "\"llm_is_commander\":false");
}

int main(void) {
  gkx_config cfg;
  char plate[640];

  /* NULL config → dual-wire no_config deny. */
  gkx_settings_json(NULL, 0, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.settings.v1\"") ||
      !strstr(plate, "\"ok\":false") ||
      !strstr(plate, "\"error\":\"no_config\"") || !plate_dual_wire(plate))
    return fail("no_config dual-wire plate");

  gkx_config_init(&cfg);
  gkx_settings_json(&cfg, 0, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.settings.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"saved\":false") ||
      !strstr(plate, "\"tools\":1") || !strstr(plate, "\"braincells\":1") ||
      !strstr(plate, "\"multiline\":1") || !strstr(plate, "\"hub\":1") ||
      !strstr(plate, "\"backend\":\"local\"") ||
      !strstr(plate, "\"theme\":\"glass\"") || !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: defaults fail: %.400s\n", plate);
    return 1;
  }

  cfg.agent_tools = 0;
  cfg.agent_braincells = 0;
  cfg.ui_multiline = 0;
  cfg.hub_enabled = 0;
  cfg.agent_max_turns = 12;
  snprintf(cfg.active_backend, sizeof cfg.active_backend, "grok");
  snprintf(cfg.ui_theme, sizeof cfg.ui_theme, "dark");
  gkx_settings_json(&cfg, 1, plate, sizeof plate);
  if (!strstr(plate, "\"saved\":true") || !strstr(plate, "\"tools\":0") ||
      !strstr(plate, "\"braincells\":0") || !strstr(plate, "\"multiline\":0") ||
      !strstr(plate, "\"hub\":0") || !strstr(plate, "\"turns\":12") ||
      !strstr(plate, "\"backend\":\"grok\"") ||
      !strstr(plate, "\"theme\":\"dark\"") || !plate_dual_wire(plate))
    return fail("saved flags dual-wire plate");

  /* Hostile backend/theme must not inject JSON (machine token only). */
  snprintf(cfg.active_backend, sizeof cfg.active_backend, "loc\"al;x");
  snprintf(cfg.ui_theme, sizeof cfg.ui_theme, "gla\"ss\\x");
  gkx_settings_json(&cfg, 0, plate, sizeof plate);
  /* quote/backslash → _; ';' dropped — never raw metachar on wire. */
  if (strstr(plate, "loc\"al") || strstr(plate, "gla\"ss") ||
      strstr(plate, ";x") || strstr(plate, "\\x") ||
      !strstr(plate, "\"backend\":\"loc_alx\"") ||
      !strstr(plate, "\"theme\":\"gla_ss_x\"") || !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: inject fail: %.400s\n", plate);
    return 1;
  }

  /* /backend show|set dual-wire (LLM ≠ commander · no free-text banner). */
  gkx_backend_json("local", 0, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.backend.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"backend\":\"local\"") ||
      !strstr(plate, "\"saved\":false") || !strstr(plate, "\"python\":0") ||
      !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: backend show fail: %.400s\n",
            plate);
    return 1;
  }
  gkx_backend_json("grok", 1, plate, sizeof plate);
  if (!strstr(plate, "\"backend\":\"grok\"") ||
      !strstr(plate, "\"saved\":true") || !plate_dual_wire(plate))
    return fail("backend set dual-wire plate");
  gkx_backend_json("loc\"al;x", 1, plate, sizeof plate);
  if (strstr(plate, "loc\"al") || strstr(plate, ";x") ||
      !strstr(plate, "\"backend\":\"loc_alx\"") || !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: backend inject fail: %.400s\n",
            plate);
    return 1;
  }
  gkx_backend_json(NULL, 0, plate, sizeof plate);
  if (!strstr(plate, "\"backend\":\"local\"") || !plate_dual_wire(plate))
    return fail("backend null defaults to local");

  printf("HOST_SETTINGS_PLATE_OK dual_wire=honest sanitize=1 saved=1 "
         "no_config=1 backend=1\n");
  return 0;
}
