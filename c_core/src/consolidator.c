/* SPDX-License-Identifier: Apache-2.0
 * Knowledge consolidator → StateMatrix (no LLM, no prose on lattice).
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_consolidator.h"
#include "sha256.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef M_E
#define M_E 2.71828182845904523536
#endif

void gk_init(gk_consolidator *C, const char *host_id) {
  if (!C) return;
  memset(C, 0, sizeof *C);
  smx_clear(&C->matrix, host_id ? host_id : "consolidator");
  smx_clear(&C->concept_mx, host_id ? host_id : "concepts");
  snprintf(C->grade, sizeof C->grade, "EMPTY");
  C->seal_ok = 0;
  C->pack_seq = 0;
  C->last_seal_ts = 0;
}

static int find_item_by_hash(const gk_consolidator *C, const uint8_t h[32]) {
  int i;
  for (i = 0; i < C->n_items; i++)
    if (memcmp(C->items[i].hash, h, 32) == 0) return i;
  return -1;
}

int gk_ingest(gk_consolidator *C, const char *id, const void *data, size_t n,
              double now_ts) {
  uint8_t h[32];
  int idx;
  gk_item *it;
  if (!C || !data || n == 0) return -1;
  if (C->n_items >= GK_MAX_ITEMS) return -1;
  gk_sha256(data, n, h);
  idx = find_item_by_hash(C, h);
  if (idx >= 0) {
    C->items[idx].ts = now_ts > 0 ? now_ts : (double)time(NULL);
    return 0; /* dedup refresh */
  }
  it = &C->items[C->n_items++];
  memset(it, 0, sizeof *it);
  if (id && id[0])
    snprintf(it->id, sizeof it->id, "%s", id);
  else
    snprintf(it->id, sizeof it->id, "i%03d", C->n_items);
  memcpy(it->hash, h, 32);
  it->ts = now_ts > 0 ? now_ts : (double)time(NULL);
  it->recency = 1.0f;
  it->novelty = 1.0f;
  it->align = 0.5f;
  it->promoted = 0;
  /* fold bytes into SoT matrix (bits only) */
  smx_ingest_bytes(&C->matrix, (const uint8_t *)data,
                   n < GROKIUM_CELLS ? n : GROKIUM_CELLS);
  return 1;
}

static float hamming32(const uint8_t *a, const uint8_t *b) {
  int i, d = 0;
  for (i = 0; i < 32; i++) {
    unsigned x = a[i] ^ b[i];
    while (x) {
      d += x & 1;
      x >>= 1;
    }
  }
  return (float)d / 256.0f;
}

int gk_consolidate(gk_consolidator *C, double now_ts) {
  int i, j;
  double now = now_ts > 0 ? now_ts : (double)time(NULL);
  float best_str = 0;
  if (!C) return -1;
  if (C->n_items == 0) {
    snprintf(C->grade, sizeof C->grade, "EMPTY");
    C->seal_ok = 0;
    return 0;
  }

  /* recency decay + novelty vs nearest neighbor */
  for (i = 0; i < C->n_items; i++) {
    double age = now - C->items[i].ts;
    if (age < 0) age = 0;
    /* half-life ~ 1 hour */
    C->items[i].recency = (float)pow(0.5, age / 3600.0);
    {
      float min_d = 1.0f;
      for (j = 0; j < C->n_items; j++) {
        float d;
        if (i == j) continue;
        d = hamming32(C->items[i].hash, C->items[j].hash);
        if (d < min_d) min_d = d;
      }
      C->items[i].novelty = C->n_items <= 1 ? 1.0f : min_d;
    }
    C->items[i].align =
        0.4f * C->items[i].recency + 0.6f * C->items[i].novelty;
  }

  /* promote high-align items to concepts (distill) */
  C->n_concepts = 0;
  smx_clear(&C->concept_mx, C->matrix.host_id);
  for (i = 0; i < C->n_items && C->n_concepts < GK_MAX_CONCEPTS; i++) {
    if (C->items[i].align < 0.35f) continue;
    {
      gk_concept *c = &C->concepts[C->n_concepts++];
      memset(c, 0, sizeof *c);
      snprintf(c->id, sizeof c->id, "c_%s", C->items[i].id);
      memcpy(c->hash, C->items[i].hash, 32);
      c->n_sources = 1;
      c->strength = C->items[i].align;
      if (c->strength > best_str) best_str = c->strength;
      C->items[i].promoted = 1;
      /* fold concept hash into concept lattice */
      smx_ingest_bytes(&C->concept_mx, c->hash, 32);
      smx_xor_fold(&C->matrix, &C->concept_mx);
    }
  }
  C->matrix.seq = ++C->pack_seq;
  C->last_seal_ts = now;

  if (C->n_concepts == 0 || best_str < 0.25f)
    snprintf(C->grade, sizeof C->grade, "WEAK");
  else if (best_str >= 0.7f && C->n_concepts >= 3)
    snprintf(C->grade, sizeof C->grade, "STRONG");
  else
    snprintf(C->grade, sizeof C->grade, "OK");

  C->seal_ok = (C->matrix.bits_set > 0 && strcmp(C->grade, "EMPTY") != 0);
  return C->n_concepts;
}

int gk_ability(const gk_consolidator *C, double now_ts, char *json_out,
               size_t cap) {
  double age;
  int fresh;
  if (!C || !json_out || cap < 32) return -1;
  age = (now_ts > 0 ? now_ts : (double)time(NULL)) - C->last_seal_ts;
  fresh = (C->last_seal_ts > 0 && age <= (double)GK_SEAL_TTL_SEC);
  /* Dual-wire honesty: ability is StateMatrix grade only — not product chat. */
  snprintf(json_out, cap,
           "{\"schema\":\"grokium.ability.v1\",\"ok\":true,"
           "\"grade\":\"%s\",\"seal_ok\":%s,\"fresh\":%s,"
           "\"n_items\":%d,\"n_concepts\":%d,\"bits_set\":%u,"
           "\"pack_seq\":%llu,\"ttl_sec\":%d,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm\":false,\"llm_is_commander\":false,"
           "\"llm_on_hot_path\":false,\"python\":0}",
           C->grade, C->seal_ok ? "true" : "false", fresh ? "true" : "false",
           C->n_items, C->n_concepts, C->matrix.bits_set,
           (unsigned long long)C->pack_seq, GK_SEAL_TTL_SEC);
  return 0;
}

int gk_save_dir(const gk_consolidator *C, const char *dir) {
  char path[512];
  FILE *f;
  char bits[GROKIUM_CELLS + 1];
  char hex[65];
  char ability[512];
  if (!C || !dir) return -1;
  mkdir(dir, 0755);
  snprintf(path, sizeof path, "%s/matrix.bin", dir);
  if (smx_save_bin(&C->matrix, path) != 0) return -1;
  snprintf(path, sizeof path, "%s/concept.bin", dir);
  smx_save_bin(&C->concept_mx, path);
  smx_bits_ascii(&C->matrix, bits, sizeof bits);
  smx_sha256_hex(&C->matrix, hex);
  gk_ability(C, 0, ability, sizeof ability);
  snprintf(path, sizeof path, "%s/ABILITY.json", dir);
  f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f, "%s\n", ability);
  fclose(f);
  snprintf(path, sizeof path, "%s/CONSOLIDATE.json", dir);
  f = fopen(path, "w");
  if (!f) return -1;
  /* On-disk consolidator plate: dual-wire honesty (lab/ops ≠ product bus). */
  fprintf(f,
          "{\"schema\":\"grokium.consolidate.v1\",\"ok\":true,"
          "\"grade\":\"%s\",\"n_items\":%d,\"n_concepts\":%d,"
          "\"bits_set\":%u,\"sha256\":\"%s\",\"pack_seq\":%llu,"
          "\"seal_ok\":%s,\"share\":\"state_matrix_only\",\"hold_flash\":1,"
          "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
          "\"peer_http_is_product_bus\":false,"
          "\"llm_is_commander\":false,\"llm_on_hot_path\":false,"
          "\"python\":0}\n",
          C->grade, C->n_items, C->n_concepts, C->matrix.bits_set, hex,
          (unsigned long long)C->pack_seq, C->seal_ok ? "true" : "false");
  fclose(f);
  return 0;
}

int gk_load_dir(gk_consolidator *C, const char *dir) {
  char path[512];
  if (!C || !dir) return -1;
  gk_init(C, "loaded");
  snprintf(path, sizeof path, "%s/matrix.bin", dir);
  if (smx_load_bin(&C->matrix, path) != 0) return -1;
  snprintf(path, sizeof path, "%s/concept.bin", dir);
  smx_load_bin(&C->concept_mx, path);
  C->seal_ok = C->matrix.bits_set > 0;
  snprintf(C->grade, sizeof C->grade, C->seal_ok ? "OK" : "EMPTY");
  return 0;
}
