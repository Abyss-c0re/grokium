/* SPDX-License-Identifier: Apache-2.0 */
#include "grokium_law.h"
#include <stdio.h>
#include <string.h>

void grokium_law_default(grokium_law *L) {
  if (!L) return;
  memset(L, 0, sizeof *L);
  L->hold_flash = GROKIUM_HOLD_FLASH;
  L->no_brain_wires = GROKIUM_NO_BRAIN_WIRES;
  L->state_matrix_key = GROKIUM_STATE_MATRIX_KEY;
  L->cores_unmixed = GROKIUM_CORES_UNMIXED;
  L->face_blur = 1;
  L->zero_telemetry = 1;
  L->commander_only_residual = 1;
}

int grokium_law_blocks_flash(const grokium_law *L) {
  if (!L) return 1;
  return L->hold_flash ? 1 : 0;
}

void grokium_law_json(const grokium_law *L, char *out, size_t cap) {
  grokium_law def;
  const grokium_law *p = L;
  if (!out || cap < 64) return;
  if (!p) {
    grokium_law_default(&def);
    p = &def;
  }
  /* Dual-wire honesty: product_wire=smx2; peer HTTP is lab/ops only. */
  snprintf(out, cap,
           "{\"schema\":\"grokium.law.v1\",\"ok\":true,"
           "\"hold_flash\":%d,\"no_brain_wires\":%d,"
           "\"state_matrix_key\":%d,\"cores_unmixed\":%d,"
           "\"face_blur\":%d,\"zero_telemetry\":%d,"
           "\"commander_only_residual\":%d,"
           "\"share\":\"state_matrix_only\","
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"commander\":\"ed25519\",\"llm_is_commander\":false,"
           "\"python\":0,\"host\":\"C\",\"product\":\"grokium\","
           "\"not\":\"grok_model\"}",
           p->hold_flash ? 1 : 0, p->no_brain_wires ? 1 : 0,
           p->state_matrix_key ? 1 : 0, p->cores_unmixed ? 1 : 0,
           p->face_blur ? 1 : 0, p->zero_telemetry ? 1 : 0,
           p->commander_only_residual ? 1 : 0);
}

void grokium_license_json(char *out, size_t cap) {
  if (!out || cap < 64) return;
  /* Shared dual-wire plate: GET /v1/license · host CLI/TUI · serve CLI. */
  snprintf(out, cap,
           "{\"schema\":\"grokium.license.v1\",\"ok\":true,"
           "\"product\":\"grokium\",\"license\":\"Apache-2.0\","
           "\"affiliation\":\"not_affiliated_with_xAI\","
           "\"commander_is_not_model\":true,"
           "\"llm_is_commander\":false,\"share\":\"state_matrix_only\","
           "\"hold_flash\":1,\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"telemetry\":\"off\",\"python\":0}");
}

/* Machine token for schema leaf / short fields (no free-text inject). */
static void leaf_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in || !in[0]) return;
  for (i = 0; in[i] && o + 1 < cap && o < 48; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-')
      out[o++] = (char)c;
  }
  out[o] = 0;
}

/* Hint field: allow common CLI punctuation; map JSON metacharacters. */
static void hint_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in || !in[0]) return;
  for (i = 0; in[i] && o + 1 < cap && o < 120; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
        c == '/' || c == ' ' || c == '|' || c == '[' || c == ']' ||
        c == '<' || c == '>' || c == ':' || c == '=' || c == '?' ||
        c == '!' || c == '+' || c == '*' || c == '#' || c == '@' ||
        c == '(' || c == ')' || c == ';') {
      out[o++] = (char)c;
    } else if (c == '"' || c == '\\') {
      out[o++] = '_';
    } else if (c == '\t') {
      out[o++] = ' ';
    }
  }
  out[o] = 0;
}

void grokium_need_subcmd_json(const char *schema_leaf, const char *hint,
                              char *out, size_t cap) {
  char leaf[56], hint_tok[160];
  if (!out || cap < 64) return;
  leaf_token(schema_leaf, leaf, sizeof leaf);
  if (!leaf[0]) snprintf(leaf, sizeof leaf, "command");
  if (hint && hint[0]) {
    hint_token(hint, hint_tok, sizeof hint_tok);
  } else {
    hint_tok[0] = 0;
  }
  /* Shared dual-wire need_subcmd: host contract/hub/… help surfaces. */
  if (hint_tok[0]) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.%s.v1\",\"ok\":false,"
             "\"error\":\"need_subcmd\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false,\"hint\":\"%s\"}",
             leaf, hint_tok);
  } else {
    snprintf(out, cap,
             "{\"schema\":\"grokium.%s.v1\",\"ok\":false,"
             "\"error\":\"need_subcmd\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false}",
             leaf);
  }
}
