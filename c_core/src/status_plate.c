/* SPDX-License-Identifier: Apache-2.0
 * Dual-wire status plate — shared by host probes and loopback HTTP.
 */
#include "grokium_status_plate.h"
#include <stdio.h>

/* Short machine token (drop free-text / path inject on plate fields). */
static void machine_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in || !in[0]) return;
  for (i = 0; in[i] && o + 1 < cap && o < 48; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-')
      out[o++] = (char)c;
    else if (c == ' ' || c == ':' || c == '/' || c == '.' || c == '\\' ||
             c == '"' || c == '\'')
      out[o++] = '_';
  }
  out[o] = 0;
}

int gk_status_plate_json(const char *control_plane, int hold_flash, int fleet_n,
                         int fleet_alive, unsigned matrix_bits,
                         const char *grade, char *out, size_t cap) {
  char plane_tok[40], grade_tok[32];
  if (!out || cap < 64) return -1;
  machine_token(control_plane, plane_tok, sizeof plane_tok);
  if (!plane_tok[0]) snprintf(plane_tok, sizeof plane_tok, "host");
  machine_token(grade, grade_tok, sizeof grade_tok);
  if (!grade_tok[0]) snprintf(grade_tok, sizeof grade_tok, "EMPTY");
  if (fleet_n < 0) fleet_n = 0;
  if (fleet_alive < 0) fleet_alive = 0;
  if (fleet_alive > fleet_n) fleet_alive = fleet_n;
  /* Dual-wire honesty: SMX2 product bus; peer HTTP lab/ops only. */
  snprintf(out, cap,
           "{\"schema\":\"grokium.status.v1\",\"ok\":true,"
           "\"product\":\"grokium\",\"control_plane\":\"%s\","
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":%d,"
           "\"telemetry\":\"off\",\"fleet_n\":%d,\"fleet_alive\":%d,"
           "\"matrix_bits\":%u,\"grade\":\"%s\","
           "\"llm_on_hot_path\":false,\"llm_is_commander\":false,"
           "\"commander\":\"ed25519\",\"python\":0,\"host\":\"C\"}",
           plane_tok, hold_flash ? 1 : 0, fleet_n, fleet_alive, matrix_bits,
           grade_tok);
  return 0;
}

void gk_healthz_json(char *out, size_t cap) {
  if (!out || cap < 64) return;
  /* Shared dual-wire liveness: GET /healthz · / · serve CLI healthz. */
  snprintf(out, cap,
           "{\"schema\":\"grokium.healthz.v1\",\"ok\":true,"
           "\"service\":\"grokium-loopback\","
           "\"control_plane\":\"loopback_http\","
           "\"telemetry\":\"off\",\"share\":\"state_matrix_only\","
           "\"hold_flash\":1,\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_is_commander\":false,\"python\":0}");
}
