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

void grokium_mode_json(int agent_tools, char *out, size_t cap) {
  int tools = agent_tools ? 1 : 0;
  if (!out || cap < 64) return;
  /* Shared dual-wire mode plate: TUI /mode chat|agent|show (host UX). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.mode.v1\",\"ok\":true,"
           "\"mode\":\"%s\",\"tools\":%d,"
           "\"resume\":\"host_local_not_smx\","
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_is_commander\":false,\"python\":0}",
           tools ? "agent" : "chat", tools);
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
        c == '(' || c == ')' || c == ';' || c == '&') {
      out[o++] = (char)c;
    } else if (c == '"' || c == '\\') {
      out[o++] = '_';
    } else if (c == '\t') {
      out[o++] = ' ';
    }
  }
  out[o] = 0;
}

void grokium_err_json(const char *schema_leaf, const char *error,
                      const char *hint, char *out, size_t cap) {
  char leaf[56], err[56], hint_tok[160];
  if (!out || cap < 64) return;
  leaf_token(schema_leaf, leaf, sizeof leaf);
  if (!leaf[0]) snprintf(leaf, sizeof leaf, "error");
  leaf_token(error, err, sizeof err);
  if (!err[0]) snprintf(err, sizeof err, "error");
  if (hint && hint[0]) {
    hint_token(hint, hint_tok, sizeof hint_tok);
  } else {
    hint_tok[0] = 0;
  }
  /* Shared dual-wire deny: product bus SMX2; peer HTTP lab/ops only; py=0. */
  if (hint_tok[0]) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.%s.v1\",\"ok\":false,"
             "\"error\":\"%s\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false,\"python\":0,\"hint\":\"%s\"}",
             leaf, err, hint_tok);
  } else {
    snprintf(out, cap,
             "{\"schema\":\"grokium.%s.v1\",\"ok\":false,"
             "\"error\":\"%s\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false,\"python\":0}",
             leaf, err);
  }
}

void grokium_need_subcmd_json(const char *schema_leaf, const char *hint,
                              char *out, size_t cap) {
  char leaf[56];
  if (!out || cap < 64) return;
  leaf_token(schema_leaf, leaf, sizeof leaf);
  if (!leaf[0]) snprintf(leaf, sizeof leaf, "command");
  /* Shared dual-wire need_subcmd: host contract/hub/… help surfaces. */
  grokium_err_json(leaf, "need_subcmd", hint, out, cap);
}

void grokium_commander_deny_json(const char *schema_leaf, const char *error,
                                 const char *hint, char *out, size_t cap) {
  char leaf[56], err[56], hint_tok[160];
  if (!out || cap < 64) return;
  leaf_token(schema_leaf, leaf, sizeof leaf);
  if (!leaf[0]) snprintf(leaf, sizeof leaf, "commander");
  leaf_token(error, err, sizeof err);
  if (!err[0]) snprintf(err, sizeof err, "need_subcmd");
  if (hint && hint[0]) {
    hint_token(hint, hint_tok, sizeof hint_tok);
  } else {
    hint_tok[0] = 0;
  }
  /* Commander = Ed25519 residual only; LLM is never commander; py=0. */
  if (hint_tok[0]) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.%s.v1\",\"ok\":false,"
             "\"error\":\"%s\",\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"commander\":\"ed25519\",\"not\":\"grok_model\","
             "\"python\":0,\"hint\":\"%s\"}",
             leaf, err, hint_tok);
  } else {
    snprintf(out, cap,
             "{\"schema\":\"grokium.%s.v1\",\"ok\":false,"
             "\"error\":\"%s\",\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"commander\":\"ed25519\",\"not\":\"grok_model\","
             "\"python\":0}",
             leaf, err);
  }
}

/* Path/token field: alnum + safe path punctuation; map JSON metacharacters. */
static void path_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in || !in[0]) return;
  for (i = 0; in[i] && o + 1 < cap && o < 200; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
        c == '/' || c == ':' || c == '@' || c == '+') {
      out[o++] = (char)c;
    } else if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t') {
      out[o++] = '_';
    } else if (c == ' ') {
      out[o++] = '_';
    }
  }
  out[o] = 0;
}

/* Hex fingerprint only (sha256 hex · no free-text inject). */
static void hex_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in || !in[0]) return;
  for (i = 0; in[i] && o + 1 < cap && o < 64; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
        (c >= 'A' && c <= 'F'))
      out[o++] = (char)c;
  }
  out[o] = 0;
}

void grokium_commander_ok_json(const char *fingerprint, const char *law_dir,
                               const char *domain, int has_sk, char *out,
                               size_t cap) {
  char fp[72], dir[220], dom[96];
  if (!out || cap < 64) return;
  hex_token(fingerprint, fp, sizeof fp);
  if (!fp[0]) snprintf(fp, sizeof fp, "none");
  path_token(law_dir, dir, sizeof dir);
  path_token(domain, dom, sizeof dom);
  /* Shared dual-wire success: Ed25519 residual · LLM ≠ commander. */
  if (dom[0] && has_sk >= 0 && dir[0]) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.commander.v1\",\"ok\":true,"
             "\"product\":\"grokium\",\"not\":\"grok_model\","
             "\"domain\":\"%s\",\"fingerprint\":\"%s\",\"has_sk\":%s,"
             "\"unforgeable\":true,\"law_dir\":\"%s\","
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1}",
             dom, fp, has_sk ? "true" : "false", dir);
  } else if (dom[0] && has_sk >= 0) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.commander.v1\",\"ok\":true,"
             "\"product\":\"grokium\",\"not\":\"grok_model\","
             "\"domain\":\"%s\",\"fingerprint\":\"%s\",\"has_sk\":%s,"
             "\"unforgeable\":true,"
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1}",
             dom, fp, has_sk ? "true" : "false");
  } else if (dir[0]) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.commander.v1\",\"ok\":true,"
             "\"product\":\"grokium\",\"not\":\"grok_model\","
             "\"fingerprint\":\"%s\",\"law_dir\":\"%s\",\"unforgeable\":true,"
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1}",
             fp, dir);
  } else {
    snprintf(out, cap,
             "{\"schema\":\"grokium.commander.v1\",\"ok\":true,"
             "\"product\":\"grokium\",\"not\":\"grok_model\","
             "\"fingerprint\":\"%s\",\"unforgeable\":true,"
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1}",
             fp);
  }
}

void grokium_commander_verify_json(int ok, char *out, size_t cap) {
  if (!out || cap < 64) return;
  /* Shared dual-wire verify plate (CLI + HTTP /v1/commander/verify). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.commander_verify.v1\",\"ok\":%s,"
           "\"commander\":%s,\"not\":\"grok_model\",\"unforgeable\":true,"
           "\"product\":\"grokium\","
           "\"llm_is_commander\":false,\"commander_is_model\":false,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1}",
           ok ? "true" : "false", ok ? "\"grokium\"" : "null");
}

void grokium_commander_install_json(const char *home, const char *bot,
                                    const char *fingerprint, char *out,
                                    size_t cap) {
  char hm[220], bt[64], fp[72];
  if (!out || cap < 64) return;
  path_token(home, hm, sizeof hm);
  if (!hm[0]) snprintf(hm, sizeof hm, "none");
  path_token(bot, bt, sizeof bt);
  if (!bt[0]) snprintf(bt, sizeof bt, "nb");
  hex_token(fingerprint, fp, sizeof fp);
  if (!fp[0]) snprintf(fp, sizeof fp, "none");
  /* Shared dual-wire install-law ack (CLI install-law). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.commander.v1\",\"ok\":true,"
           "\"home\":\"%s\",\"bot\":\"%s\",\"fingerprint\":\"%s\","
           "\"law\":\"installed\",\"product\":\"grokium\","
           "\"not\":\"grok_model\",\"unforgeable\":true,"
           "\"llm_is_commander\":false,\"commander_is_model\":false,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1}",
           hm, bt, fp);
}

void grokium_commander_reject_json(int allowed, char *out, size_t cap) {
  if (!out || cap < 64) return;
  /* Shared dual-wire reject-model plate (POST /v1/commander/reject_model). */
  if (allowed) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.commander_reject.v1\",\"ok\":true,"
             "\"allowed\":true,\"product\":\"grokium\","
             "\"not\":\"grok_model\",\"unforgeable\":true,"
             "\"llm_is_commander\":false,\"commander_is_model\":false,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1}");
  } else {
    snprintf(out, cap,
             "{\"schema\":\"grokium.commander_reject.v1\",\"ok\":false,"
             "\"allowed\":false,\"error\":\"model_is_not_commander\","
             "\"product\":\"grokium\",\"not\":\"grok_model\","
             "\"unforgeable\":true,\"llm_is_commander\":false,"
             "\"commander_is_model\":false,\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1}");
  }
}
