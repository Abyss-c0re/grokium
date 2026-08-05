/* SPDX-License-Identifier: Apache-2.0
 * Host session meta plates — pure C, no nanobot/ncurses.
 * Product bus remains SMX2; peer HTTP = lab_ops only; share = state_matrix_only.
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_session.h"
#include <stdio.h>
#include <string.h>

int gkx_session_id_safe(const char *id) {
  size_t i;
  if (!id || !id[0] || strlen(id) > 80) return 0;
  for (i = 0; id[i]; i++) {
    char c = id[i];
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
        (c >= 'A' && c <= 'F') || c == '-')
      continue;
    return 0;
  }
  return 1;
}

/* Echo only hex/dash so deny plates never inject free-text ids. */
static void id_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in) return;
  for (i = 0; in[i] && o + 1 < cap && o < 80; i++) {
    char c = in[i];
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
        (c >= 'A' && c <= 'F') || c == '-')
      out[o++] = c;
  }
  out[o] = 0;
}

/* Short machine error token (drop path/prose injection). */
static void err_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in || !in[0]) return;
  for (i = 0; in[i] && o + 1 < cap && o < 48; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-')
      out[o++] = (char)c;
    else if (c == ' ' || c == ':' || c == '/' || c == '.')
      out[o++] = '_';
  }
  out[o] = 0;
}

static void json_escape(const char *in, char *out, size_t cap) {
  size_t o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in) return;
  for (; *in && o + 2 < cap; in++) {
    unsigned char c = (unsigned char)*in;
    if (c == '"' || c == '\\') {
      if (o + 3 >= cap) break;
      out[o++] = '\\';
      out[o++] = (char)c;
    } else if (c < 0x20) {
      continue;
    } else {
      out[o++] = (char)c;
    }
  }
  out[o] = 0;
}

int gkx_session_pickup_deny_json(const char *id, const char *error, char *out,
                                 size_t cap) {
  char err[64], idt[96];
  const char *hint;
  if (!out || cap < 64) return -1;
  err_token(error, err, sizeof err);
  if (!err[0]) snprintf(err, sizeof err, "deny");
  id_token(id, idt, sizeof idt);
  if (strcmp(err, "need_session_id") == 0)
    hint = "pass hex session id · meta only";
  else if (strcmp(err, "not_found") == 0)
    hint = "import meta only · TUI /pickup resumes host-local";
  else
    hint = "session id is hex digits and dashes only";
  /* Dual-wire honesty: SMX2 product bus ≠ peer HTTP lab ops. */
  if (idt[0]) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.session_pickup.v1\",\"ok\":false,"
             "\"error\":\"%s\",\"id\":\"%s\",\"content\":\"meta_only\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"resume_available\":false,"
             "\"hint\":\"%s\"}",
             err, idt, hint);
  } else {
    snprintf(out, cap,
             "{\"schema\":\"grokium.session_pickup.v1\",\"ok\":false,"
             "\"error\":\"%s\",\"content\":\"meta_only\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"resume_available\":false,"
             "\"hint\":\"%s\"}",
             err, hint);
  }
  return 0;
}

int gkx_session_list_empty_json(const char *q, const char *import_dir,
                                const char *error, char *out, size_t cap) {
  char qe[128], de[512], err[64];
  if (!out || cap < 64) return -1;
  json_escape(q ? q : "", qe, sizeof qe);
  json_escape(import_dir ? import_dir : "", de, sizeof de);
  err_token(error, err, sizeof err);
  if (err[0]) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.sessions.v1\",\"ok\":true,\"n\":0,"
             "\"sessions\":[],\"q\":\"%s\",\"import_dir\":\"%s\","
             "\"error\":\"%s\",\"content\":\"meta_only\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"telemetry\":\"off\"}",
             qe, de, err);
  } else {
    snprintf(out, cap,
             "{\"schema\":\"grokium.sessions.v1\",\"ok\":true,\"n\":0,"
             "\"sessions\":[],\"q\":\"%s\",\"import_dir\":\"%s\","
             "\"content\":\"meta_only\","
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false,"
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"telemetry\":\"off\"}",
             qe, de);
  }
  return 0;
}
