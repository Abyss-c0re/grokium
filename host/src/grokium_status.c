/* SPDX-License-Identifier: Apache-2.0
 * Dual-wire honesty status probes (fleet kill(0) + matrix bits).
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_status.h"
#include "grokium_status_plate.h"
#include "grokium_fleet.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

void gkx_status_fleet_probe(const char *repo_root, int *n_out, int *alive_out) {
  char path[PATH_MAX];
  gk_fleet F;
  const char *root = (repo_root && repo_root[0]) ? repo_root : ".";
  int alive;
  if (n_out) *n_out = 0;
  if (alive_out) *alive_out = 0;
  snprintf(path, sizeof path, "%s/data/home/FLEET.json", root);
  /* No plate → no claimed fleet (do not invent default roles as live counts). */
  if (access(path, R_OK) != 0) return;
  memset(&F, 0, sizeof F);
  if (fleet_load(&F, path) != 0) return;
  alive = fleet_status(&F);
  if (alive < 0) alive = 0;
  if (n_out) *n_out = F.n > 0 ? F.n : 0;
  if (alive_out) *alive_out = alive;
}

void gkx_status_matrix_probe(const char *repo_root, unsigned *bits_out,
                             char *grade, size_t gcap) {
  char path[PATH_MAX], body[8192];
  FILE *f;
  size_t nread;
  const char *p, *bits;
  const char *root = (repo_root && repo_root[0]) ? repo_root : ".";
  unsigned cnt = 0;
  if (bits_out) *bits_out = 0;
  if (grade && gcap) snprintf(grade, gcap, "EMPTY");
  snprintf(path, sizeof path, "%s/data/matrix/LATEST.json", root);
  f = fopen(path, "r");
  if (!f) return;
  nread = fread(body, 1, sizeof body - 1, f);
  body[nread] = 0;
  fclose(f);
  bits = strstr(body, "\"sot_bits\"");
  if (bits) {
    bits = strchr(bits, ':');
    if (bits) {
      bits++;
      while (*bits == ' ' || *bits == '\t') bits++;
      if (*bits == '"') {
        bits++;
        for (p = bits; *p && *p != '"'; p++)
          if (*p == '1') cnt++;
      }
    }
  }
  if (bits_out) *bits_out = cnt;
  if (grade && gcap) {
    if (cnt == 0)
      snprintf(grade, gcap, "EMPTY");
    else if (cnt < 16)
      snprintf(grade, gcap, "SPARSE");
    else
      snprintf(grade, gcap, "OK");
  }
}

int gkx_status_plate_json(const char *repo_root, const char *control_plane,
                          char *out, size_t cap) {
  int fleet_n = 0, fleet_alive = 0;
  unsigned matrix_bits = 0;
  char grade[32];
  if (!out || cap < 64) return -1;
  gkx_status_fleet_probe(repo_root, &fleet_n, &fleet_alive);
  gkx_status_matrix_probe(repo_root, &matrix_bits, grade, sizeof grade);
  /* Shared c_core dual-wire formatter (match loopback GET /v1/status). */
  return gk_status_plate_json(control_plane, 1, fleet_n, fleet_alive,
                              matrix_bits, grade, out, cap);
}
