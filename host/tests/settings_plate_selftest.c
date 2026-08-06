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
         strstr(p, "\"llm_is_commander\":false") &&
         strstr(p, "\"python\":0");
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
      !strstr(plate, "\"theme\":\"glass\"") ||
      !strstr(plate, "\"model\"") || !strstr(plate, "\"context_window\"") ||
      !strstr(plate, "\"mouse\"") || !strstr(plate, "\"product\"") ||
      !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: defaults fail: %.400s\n", plate);
    return 1;
  }

  cfg.agent_tools = 0;
  cfg.agent_braincells = 0;
  cfg.ui_multiline = 0;
  cfg.hub_enabled = 0;
  cfg.agent_max_turns = 12;
  cfg.context_window = 8192;
  cfg.ui_mouse = 0;
  snprintf(cfg.active_backend, sizeof cfg.active_backend, "grok");
  snprintf(cfg.active_model, sizeof cfg.active_model, "grok-4");
  snprintf(cfg.ui_theme, sizeof cfg.ui_theme, "dark");
  snprintf(cfg.ui_product_name, sizeof cfg.ui_product_name, "grokium");
  gkx_settings_json(&cfg, 1, plate, sizeof plate);
  if (!strstr(plate, "\"saved\":true") || !strstr(plate, "\"tools\":0") ||
      !strstr(plate, "\"braincells\":0") || !strstr(plate, "\"multiline\":0") ||
      !strstr(plate, "\"hub\":0") || !strstr(plate, "\"turns\":12") ||
      !strstr(plate, "\"backend\":\"grok\"") ||
      !strstr(plate, "\"model\":\"grok-4\"") ||
      !strstr(plate, "\"theme\":\"dark\"") ||
      !strstr(plate, "\"context_window\":8192") ||
      !strstr(plate, "\"mouse\":0") || !strstr(plate, "\"product\":\"grokium\"") ||
      !plate_dual_wire(plate))
    return fail("saved flags dual-wire plate");

  /* Hostile backend/theme/model must not inject JSON (machine token only). */
  snprintf(cfg.active_backend, sizeof cfg.active_backend, "loc\"al;x");
  snprintf(cfg.ui_theme, sizeof cfg.ui_theme, "gla\"ss\\x");
  snprintf(cfg.active_model, sizeof cfg.active_model, "bad\";drop");
  gkx_settings_json(&cfg, 0, plate, sizeof plate);
  /* quote/backslash → _; ';' dropped — never raw metachar on wire. */
  if (strstr(plate, "loc\"al") || strstr(plate, "gla\"ss") ||
      strstr(plate, ";x") || strstr(plate, "\\x") || strstr(plate, "bad\";") ||
      !strstr(plate, "\"backend\":\"loc_alx\"") ||
      !strstr(plate, "\"theme\":\"gla_ss_x\"") ||
      !strstr(plate, "\"model\":\"bad_drop\"") || !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: inject fail: %.400s\n", plate);
    return 1;
  }

  /* /settings path dual-wire (no free-text save path banner). */
  gkx_settings_path_json("/home/me/config/grokium.toml", plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.settings.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"action\":\"path\"") ||
      !strstr(plate, "\"path\":\"/home/me/config/grokium.toml\"") ||
      !plate_dual_wire(plate))
    return fail("settings path dual-wire plate");
  gkx_settings_path_json("evil\";drop path", plate, sizeof plate);
  if (strstr(plate, "evil\";") || strstr(plate, "drop path") ||
      !strstr(plate, "\"action\":\"path\"") || !plate_dual_wire(plate))
    return fail("settings path inject sanitize");

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

  /* /model set dual-wire (LLM ≠ commander · no free-text banner). */
  gkx_model_json("local", "auto", 1, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.model.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"backend\":\"local\"") ||
      !strstr(plate, "\"model\":\"auto\"") || !strstr(plate, "\"saved\":true") ||
      !strstr(plate, "\"python\":0") || !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: model set fail: %.400s\n", plate);
    return 1;
  }
  gkx_model_json("grok", "grok-4", 1, plate, sizeof plate);
  if (!strstr(plate, "\"backend\":\"grok\"") ||
      !strstr(plate, "\"model\":\"grok-4\"") || !plate_dual_wire(plate))
    return fail("model grok dual-wire plate");
  gkx_model_json("loc\"al", "bad\";drop/x", 0, plate, sizeof plate);
  if (strstr(plate, "loc\"al") || strstr(plate, "bad\"") ||
      strstr(plate, ";drop") || !strstr(plate, "\"backend\":\"loc_al\"") ||
      !strstr(plate, "\"model\":\"bad_drop_x\"") ||
      !strstr(plate, "\"saved\":false") || !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: model inject fail: %.400s\n",
            plate);
    return 1;
  }
  gkx_model_json(NULL, NULL, 1, plate, sizeof plate);
  if (!strstr(plate, "\"backend\":\"local\"") ||
      !strstr(plate, "\"model\":\"auto\"") || !plate_dual_wire(plate))
    return fail("model null defaults");

  /* /context|/ctx dual-wire (LLM ≠ commander · no free-text banner). */
  gkx_context_json(65536, 0, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.context.v1\"") ||
      !strstr(plate, "\"ok\":true") ||
      !strstr(plate, "\"context_window\":65536") ||
      !strstr(plate, "\"saved\":false") || !strstr(plate, "\"python\":0") ||
      !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: context show fail: %.400s\n",
            plate);
    return 1;
  }
  gkx_context_json(8192, 1, plate, sizeof plate);
  if (!strstr(plate, "\"context_window\":8192") ||
      !strstr(plate, "\"saved\":true") || !plate_dual_wire(plate))
    return fail("context set dual-wire plate");
  gkx_context_json(-1, 0, plate, sizeof plate);
  if (!strstr(plate, "\"context_window\":0") || !plate_dual_wire(plate))
    return fail("context negative clamps to 0");

  /* /multiline|/ml dual-wire — Enter always sends; Shift/Alt+Enter = newline. */
  gkx_multiline_json(1, 1, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.multiline.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"multiline\":true") ||
      !strstr(plate, "\"saved\":true") ||
      !strstr(plate, "\"enter\":\"send\"") ||
      !strstr(plate, "\"newline\":\"shift_or_alt_enter\"") ||
      !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: multiline on fail: %.400s\n",
            plate);
    return 1;
  }
  gkx_multiline_json(0, 0, plate, sizeof plate);
  if (!strstr(plate, "\"multiline\":false") ||
      !strstr(plate, "\"saved\":false") ||
      !strstr(plate, "\"enter\":\"send\"") ||
      !strstr(plate, "\"newline\":\"disabled\"") || !plate_dual_wire(plate))
    return fail("multiline off dual-wire plate");

  /* /expand|/collapse dual-wire (host UX · no free-text spoilers banner). */
  gkx_spoilers_json(1, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.spoilers.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"expanded\":true") ||
      !strstr(plate, "\"state\":\"expanded\"") || !plate_dual_wire(plate))
    return fail("spoilers expanded dual-wire plate");
  gkx_spoilers_json(0, plate, sizeof plate);
  if (!strstr(plate, "\"expanded\":false") ||
      !strstr(plate, "\"state\":\"collapsed\"") || !plate_dual_wire(plate))
    return fail("spoilers collapsed dual-wire plate");

  /* /debug dual-wire (host UX · no free-text ON/OFF banner). */
  gkx_debug_json(1, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.debug.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"debug\":true") ||
      !plate_dual_wire(plate))
    return fail("debug on dual-wire plate");
  gkx_debug_json(0, plate, sizeof plate);
  if (!strstr(plate, "\"debug\":false") || !plate_dual_wire(plate))
    return fail("debug off dual-wire plate");

  /* /always-approve|/yolo dual-wire (host UX · no free-text ON/OFF banner). */
  gkx_always_approve_json(1, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.always_approve.v1\"") ||
      !strstr(plate, "\"ok\":true") ||
      !strstr(plate, "\"always_approve\":true") || !plate_dual_wire(plate))
    return fail("always_approve on dual-wire plate");
  gkx_always_approve_json(0, plate, sizeof plate);
  if (!strstr(plate, "\"always_approve\":false") || !plate_dual_wire(plate))
    return fail("always_approve off dual-wire plate");

  /* /auth dual-wire (has_token only · never secrets · no free-text banner). */
  gkx_auth_json(1, "local", plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.auth.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"has_token\":true") ||
      !strstr(plate, "\"backend\":\"local\"") ||
      !strstr(plate, "\"telemetry\":\"off\"") || !plate_dual_wire(plate))
    return fail("auth has_token dual-wire plate");
  gkx_auth_json(0, "grok", plate, sizeof plate);
  if (!strstr(plate, "\"has_token\":false") ||
      !strstr(plate, "\"backend\":\"grok\"") || !plate_dual_wire(plate))
    return fail("auth no_token dual-wire plate");
  gkx_auth_json(1, "loc\"al;x", plate, sizeof plate);
  if (strstr(plate, "loc\"al") || strstr(plate, ";x") ||
      !strstr(plate, "\"backend\":\"loc_alx\"") || !plate_dual_wire(plate))
    return fail("auth backend inject sanitize");
  gkx_auth_json(0, NULL, plate, sizeof plate);
  if (!strstr(plate, "\"backend\":\"local\"") || !plate_dual_wire(plate))
    return fail("auth null backend defaults local");

  /* /login|/grok dual-wire (no free-text affiliation / token OK banners). */
  gkx_login_json(1, 0, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.login.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"has_token\":true") ||
      !strstr(plate, "\"method\":\"oauth\"") ||
      !strstr(plate, "\"opt_in\":\"cloud_auth\"") ||
      !strstr(plate, "\"local_first\":true") ||
      !strstr(plate, "\"surface\":\"host_tui\"") ||
      !strstr(plate, "press_enter") ||
      !strstr(plate, "\"commander_is_model\":false") ||
      !strstr(plate, "\"telemetry\":\"off\"") || !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: login oauth fail: %.400s\n", plate);
    return 1;
  }
  gkx_login_json(0, 1, plate, sizeof plate);
  if (!strstr(plate, "\"ok\":false") || !strstr(plate, "\"has_token\":false") ||
      !strstr(plate, "\"method\":\"device_auth\"") || !plate_dual_wire(plate))
    return fail("login device_auth dual-wire plate");

  /* /clear|/cls|/new dual-wire (no free-text (cleared)/(new session)). */
  gkx_session_clear_json(0, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.session_clear.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"action\":\"clear\"") ||
      !strstr(plate, "\"content\":\"host_local\"") || !plate_dual_wire(plate))
    return fail("session clear dual-wire plate");
  gkx_session_clear_json(1, plate, sizeof plate);
  if (!strstr(plate, "\"action\":\"new\"") ||
      !strstr(plate, "\"content\":\"host_local\"") || !plate_dual_wire(plate))
    return fail("session new dual-wire plate");

  /* Ctrl+C empty input dual-wire (no free-text (interrupt) banner). */
  gkx_interrupt_json(plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.interrupt.v1\"") ||
      !strstr(plate, "\"ok\":true") ||
      !strstr(plate, "\"action\":\"interrupt\"") ||
      !strstr(plate, "\"key\":\"ctrl_c\"") || !plate_dual_wire(plate))
    return fail("interrupt dual-wire plate");

  /* c_core capture + shell tool spoiler empty body (no free-text (no output)). */
  gkx_empty_output_json(plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.empty_output.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"empty\":true") ||
      !strstr(plate, "\"content\":\"meta_only\"") || !plate_dual_wire(plate))
    return fail("empty_output dual-wire plate");

  /* TUI /help dual-wire (no free-text multi-line usage dump). */
  gkx_tui_help_json(plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.help.v1\"") ||
      !strstr(plate, "\"ok\":false") ||
      !strstr(plate, "\"error\":\"need_cmd\"") ||
      !strstr(plate, "\"surface\":\"host_tui\"") ||
      !strstr(plate, "/settings") || !strstr(plate, "/fleet") ||
      !strstr(plate, "/auth") || !strstr(plate, "/login") ||
      !strstr(plate, "Enter=send") || !strstr(plate, "Shift/Alt+Enter") ||
      !strstr(plate, "\"commander_is_model\":false") ||
      !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: tui help fail: %.500s\n", plate);
    return 1;
  }

  /* Host CLI help|-h dual-wire (no free-text multi-line usage dump). */
  gkx_cli_help_json(plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.cli_help.v1\"") ||
      !strstr(plate, "\"ok\":true") ||
      !strstr(plate, "\"surface\":\"host_cli\"") ||
      !strstr(plate, "\"local_first\":true") ||
      !strstr(plate, "help|chat|tui") || !strstr(plate, "login|auth") ||
      !strstr(plate, "manager-tick") || !strstr(plate, "backend=local") ||
      !strstr(plate, "\"commander_is_model\":false") ||
      !strstr(plate, "\"telemetry\":\"off\"") || !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: cli help fail: %.500s\n", plate);
    return 1;
  }

  /* TUI /model list + CLI models dual-wire (no free-text / raw OpenAI body). */
  gkx_models_list_json(3, "local", "auto", plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.models.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"n\":3") ||
      !strstr(plate, "\"backend\":\"local\"") ||
      !strstr(plate, "\"active\":\"auto\"") ||
      !strstr(plate, "\"local_first\":true") ||
      !strstr(plate, "/model") || !strstr(plate, "models ·") ||
      !strstr(plate, "\"commander_is_model\":false") ||
      !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: models list fail: %.400s\n", plate);
    return 1;
  }
  gkx_models_list_json(0, "grok", "grok-4.5", plate, sizeof plate);
  if (!strstr(plate, "\"n\":0") || !strstr(plate, "\"backend\":\"grok\"") ||
      !strstr(plate, "\"active\":\"grok-4.5\"") || !plate_dual_wire(plate))
    return fail("models list empty dual-wire plate");
  gkx_models_list_json(-1, "be\";x", "m\"evil", plate, sizeof plate);
  if (strstr(plate, "be\";") || strstr(plate, "m\"evil") ||
      !strstr(plate, "\"n\":0") || !strstr(plate, "\"active\":\"m_evil\"") ||
      !plate_dual_wire(plate))
    return fail("models list inject sanitize");

  /* TUI startup ready dual-wire (no free-text welcome/send-hint dump). */
  gkx_ready_json(1, 1, 1, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.ready.v1\"") ||
      !strstr(plate, "\"ok\":true") ||
      !strstr(plate, "\"surface\":\"host_tui\"") ||
      !strstr(plate, "\"local_first\":true") ||
      !strstr(plate, "\"hub\":true") || !strstr(plate, "\"tools\":true") ||
      !strstr(plate, "\"multiline\":true") ||
      !strstr(plate, "\"telemetry\":\"off\"") ||
      !strstr(plate, "\"vision\":\"perfect_assistant\"") ||
      !strstr(plate, "Enter=send") ||
      !strstr(plate, "\"commander_is_model\":false") ||
      !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: ready on fail: %.400s\n", plate);
    return 1;
  }
  gkx_ready_json(0, 0, 0, plate, sizeof plate);
  if (!strstr(plate, "\"hub\":false") || !strstr(plate, "\"tools\":false") ||
      !strstr(plate, "\"multiline\":false") || !plate_dual_wire(plate))
    return fail("ready off dual-wire plate");

  /* TUI /agents status dual-wire (no free-text multi-line agents dump). */
  gkx_agents_json(1, 1, 96, 900, 2, 8, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.agents.v1\"") ||
      !strstr(plate, "\"ok\":true") || !strstr(plate, "\"tools\":true") ||
      !strstr(plate, "\"braincells\":true") ||
      !strstr(plate, "\"max_turns\":96") ||
      !strstr(plate, "\"timeout_sec\":900") ||
      !strstr(plate, "\"sub_running\":2") || !strstr(plate, "\"sub_max\":8") ||
      !strstr(plate, "\"cores\":\"local_and_grok\"") || !plate_dual_wire(plate)) {
    fprintf(stderr, "settings_plate_selftest: agents fail: %.400s\n", plate);
    return 1;
  }
  gkx_agents_json(0, 0, -1, -5, -3, 9999, plate, sizeof plate);
  if (!strstr(plate, "\"tools\":false") ||
      !strstr(plate, "\"braincells\":false") ||
      !strstr(plate, "\"max_turns\":0") ||
      !strstr(plate, "\"timeout_sec\":0") ||
      !strstr(plate, "\"sub_running\":0") ||
      !strstr(plate, "\"sub_max\":256") || !plate_dual_wire(plate))
    return fail("agents clamp dual-wire plate");

  /* TUI /agents cancel ID dual-wire (id sanitize · no free-text banner). */
  gkx_subagent_cancel_json(1, "worker-1", plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.subagent_cancel.v1\"") ||
      !strstr(plate, "\"ok\":true") ||
      !strstr(plate, "\"action\":\"cancel\"") ||
      !strstr(plate, "\"id\":\"worker-1\"") || !plate_dual_wire(plate))
    return fail("subagent cancel ok dual-wire plate");
  gkx_subagent_cancel_json(0, "bad\";drop/x", plate, sizeof plate);
  if (!strstr(plate, "\"ok\":false") ||
      !strstr(plate, "\"error\":\"cancel_failed\"") ||
      !strstr(plate, "\"id\":\"bad_drop_x\"") || strstr(plate, "\";drop") ||
      !plate_dual_wire(plate))
    return fail("subagent cancel deny sanitize dual-wire plate");

  /* Dual-wire selftest success — no free-text HOST_SETTINGS_PLATE_OK. */
  {
    char okp[768];
    snprintf(okp, sizeof okp,
             "{\"schema\":\"grokium.settings_plate_selftest.v1\","
             "\"ok\":true,\"dual_wire\":true,\"sanitize\":true,"
             "\"saved\":true,\"no_config\":true,\"backend\":true,"
             "\"model\":true,\"context\":true,\"multiline\":true,"
             "\"spoilers\":true,\"debug\":true,\"always_approve\":true,"
             "\"auth\":true,\"login\":true,\"session_clear\":true,"
             "\"interrupt\":true,\"empty_output\":true,\"tui_help\":true,"
             "\"cli_help\":true,\"models_list\":true,\"ready\":true,"
             "\"agents\":true,\"subagent_cancel\":true,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false,\"hold_flash\":1,"
             "\"share\":\"state_matrix_only\",\"python\":0}");
    if (!strstr(okp, "\"schema\":\"grokium.settings_plate_selftest.v1\"") ||
        !strstr(okp, "\"ok\":true") || !strstr(okp, "\"empty_output\":true") ||
        !strstr(okp, "\"ready\":true") || !strstr(okp, "\"agents\":true") ||
        !strstr(okp, "\"subagent_cancel\":true") ||
        !strstr(okp, "\"product_wire\":\"smx2\"") ||
        !strstr(okp, "\"peer_http\":\"lab_ops_only\"") ||
        !strstr(okp, "\"peer_http_is_product_bus\":false") ||
        !strstr(okp, "\"llm_is_commander\":false") ||
        !strstr(okp, "\"hold_flash\":1") ||
        !strstr(okp, "\"share\":\"state_matrix_only\"") ||
        !strstr(okp, "\"python\":0"))
      return fail("settings_plate_selftest plate");
    printf("%s\n", okp);
  }
  return 0;
}
