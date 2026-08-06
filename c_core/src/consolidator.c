/* SPDX-License-Identifier: Apache-2.0
 * Knowledge consolidator → StateMatrix (no LLM, no prose on lattice).
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_consolidator.h"
#include "grokium_algocube.h"
#include "sha256.h"
#include <dirent.h>
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

/* Machine token for plate fields (host/grade — no free-text inject). */
static void plate_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in || !in[0]) return;
  for (i = 0; in[i] && o + 1 < cap && o < 48; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')
      out[o++] = (char)c;
    else if (c == ' ' || c == '/' || c == ':' || c == '"' || c == '\\' ||
             c == '\'' || c == ',' || c == ';')
      out[o++] = '_';
  }
  out[o] = 0;
}

/* Shared dual-wire tails for coord ingest plates (CLI + HTTP + host; py=0). */
#define COORD_DUAL_WIRE_TAIL                                               \
  "\"share\":\"state_matrix_only\",\"hold_flash\":1,"                      \
  "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","              \
  "\"peer_http_is_product_bus\":false,"                                    \
  "\"llm_on_hot_path\":false,\"llm_is_commander\":false,\"python\":0"

void gk_coord_err_json(const char *error, char *out, size_t cap) {
  char err_tok[56];
  if (!out || cap < 64) return;
  /* Machine token only — no free-text / quote inject on error leaf. */
  plate_token(error && error[0] ? error : "ingest_failed", err_tok,
              sizeof err_tok);
  if (!err_tok[0]) snprintf(err_tok, sizeof err_tok, "ingest_failed");
  if (!strcmp(err_tok, "need_plate")) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.coord.v1\",\"ok\":false,"
             "\"error\":\"need_plate\",\"content\":\"meta_only\","
             COORD_DUAL_WIRE_TAIL ","
             "\"hint\":\"pass NEXUS_COORD or SMX 01-bits plate\"}");
    return;
  }
  snprintf(out, cap,
           "{\"schema\":\"grokium.coord.v1\",\"ok\":false,"
           "\"error\":\"%s\",\"content\":\"meta_only\"," COORD_DUAL_WIRE_TAIL
           "}",
           err_tok);
}

void gk_coord_json(const gk_consolidator *C, char *out, size_t cap) {
  char hex[65], grade_tok[32];
  if (!out || cap < 64) return;
  if (!C) {
    gk_coord_err_json("no_consolidator", out, cap);
    return;
  }
  smx_sha256_hex(&C->matrix, hex);
  plate_token(C->grade, grade_tok, sizeof grade_tok);
  if (!grade_tok[0]) snprintf(grade_tok, sizeof grade_tok, "EMPTY");
  /* Lab/ops ingest plate; product multi-peer bus remains SMX2. */
  snprintf(out, cap,
           "{\"schema\":\"grokium.coord.v1\",\"ok\":true,"
           "\"ingested\":true,\"grade\":\"%s\","
           "\"bits_set\":%u,\"seq\":%llu,\"sha256\":\"%s\","
           COORD_DUAL_WIRE_TAIL "}",
           grade_tok, C->matrix.bits_set, (unsigned long long)C->matrix.seq,
           hex);
}

int gk_matrix_json(const gk_consolidator *C, char *out, size_t cap) {
  char hex[65];
  char bits[GROKIUM_CELLS + 1];
  char host_tok[40], grade_tok[32];
  if (!out || cap < 128) return -1;
  if (!C) {
    snprintf(out, cap,
             "{\"schema\":\"grokium.smx.v1\",\"ok\":false,"
             "\"error\":\"no_matrix\",\"share\":\"state_matrix_only\","
             "\"hold_flash\":1,\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_on_hot_path\":false,\"llm_is_commander\":false,"
             "\"python\":0}");
    return -1;
  }
  smx_sha256_hex(&C->matrix, hex);
  smx_bits_ascii(&C->matrix, bits, sizeof bits);
  plate_token(C->matrix.host_id, host_tok, sizeof host_tok);
  if (!host_tok[0]) snprintf(host_tok, sizeof host_tok, "unknown");
  plate_token(C->grade, grade_tok, sizeof grade_tok);
  if (!grade_tok[0]) snprintf(grade_tok, sizeof grade_tok, "EMPTY");
  /* Lab/ops SMX snapshot: bits only on wire; dual-wire honesty plate. */
  snprintf(out, cap,
           "{\"schema\":\"grokium.smx.v1\",\"ok\":true,"
           "\"seq\":%llu,\"bits_set\":%u,"
           "\"host\":\"%s\",\"sha256\":\"%s\",\"grade\":\"%s\","
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_on_hot_path\":false,\"llm_is_commander\":false,"
           "\"python\":0,\"bits\":\"%s\"}",
           (unsigned long long)C->matrix.seq, C->matrix.bits_set, host_tok,
           hex, grade_tok, bits);
  return 0;
}

static void json_escape_path(const char *in, char *out, size_t cap) {
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

void gk_save_json(const gk_consolidator *C, const char *dir, char *out,
                  size_t cap) {
  char dir_esc[256], grade_tok[32];
  if (!out || cap < 64) return;
  /* Escape dir so hostile path args cannot break the dual-wire plate. */
  json_escape_path(dir && dir[0] ? dir : "data/knowledge", dir_esc,
                   sizeof dir_esc);
  if (!dir_esc[0]) snprintf(dir_esc, sizeof dir_esc, "data/knowledge");
  plate_token(C && C->grade[0] ? C->grade : "EMPTY", grade_tok,
              sizeof grade_tok);
  if (!grade_tok[0]) snprintf(grade_tok, sizeof grade_tok, "EMPTY");
  /* Shared dual-wire save ack (CLI save · lab/ops ≠ product bus · py=0). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.consolidator_save.v1\",\"ok\":true,"
           "\"dir\":\"%s\",\"grade\":\"%s\",\"share\":\"state_matrix_only\","
           "\"hold_flash\":1,\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_is_commander\":false,\"llm_on_hot_path\":false,"
           "\"python\":0}",
           dir_esc, grade_tok);
}

static int count_dir_entries(const char *path) {
  DIR *d;
  struct dirent *e;
  int n = 0;
  if (!path || !path[0]) return -1;
  d = opendir(path);
  if (!d) return -1;
  while ((e = readdir(d)) != NULL) {
    if (e->d_name[0] == '.') continue;
    n++;
  }
  closedir(d);
  return n;
}

int gk_cube_status_json(const gk_consolidator *C, int hold_flash,
                        const char *data_root, char *out, size_t cap) {
  char hex[65];
  uint8_t bp[10];
  char bp_json[80];
  char cpath[400], mpath[400], cpath_esc[512], mpath_esc[512], grade_tok[32];
  int dig = 0, ncont, nmat, i;
  size_t u = 0;
  const char *root = data_root && data_root[0] ? data_root : "data";

  if (!out || cap < 128) return -1;
  hex[0] = 0;
  memset(bp, 0, sizeof bp);
  if (C) {
    smx_sha256_hex(&C->matrix, hex);
    dig = algocube_digit(&C->matrix, "cube_status");
    algocube_blueprint10(&C->matrix, bp);
  }
  u = 0;
  u += (size_t)snprintf(bp_json + u, sizeof bp_json - u, "[");
  for (i = 0; i < 10 && u + 4 < sizeof bp_json; i++)
    u += (size_t)snprintf(bp_json + u, sizeof bp_json - u, "%s%u",
                          i ? "," : "", (unsigned)bp[i]);
  if (u + 2 < sizeof bp_json)
    snprintf(bp_json + u, sizeof bp_json - u, "]");

  snprintf(cpath, sizeof cpath, "%s/cube_containers", root);
  snprintf(mpath, sizeof mpath, "%s/matrix", root);
  ncont = count_dir_entries(cpath);
  nmat = count_dir_entries(mpath);
  /* Escape paths/grade so a hostile data_root cannot break the plate. */
  json_escape_path(cpath, cpath_esc, sizeof cpath_esc);
  json_escape_path(mpath, mpath_esc, sizeof mpath_esc);
  plate_token(C ? C->grade : "EMPTY", grade_tok, sizeof grade_tok);
  if (!grade_tok[0]) snprintf(grade_tok, sizeof grade_tok, "EMPTY");

  /* Lab/ops cube bridge; product multi-peer bus remains SMX2. */
  snprintf(out, cap,
           "{\"schema\":\"grokium.cube_status.v1\","
           "\"ok\":true,\"product\":\"grokium\","
           "\"bridge\":\"algocube\","
           "\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\","
           "\"hold_flash\":%d,\"telemetry\":\"off\","
           "\"matrix_bits\":%u,\"grade\":\"%s\",\"seq\":%llu,"
           "\"sha256\":\"%s\",\"digit\":%d,\"blueprint\":%s,"
           "\"containers_path\":\"%s\",\"containers_n\":%d,"
           "\"matrix_path\":\"%s\",\"matrix_files_n\":%d,"
           "\"llm_is_commander\":false,\"commander_is_model\":false,"
           "\"llm_on_hot_path\":false,\"python\":0}",
           hold_flash ? 1 : 0, C ? C->matrix.bits_set : 0, grade_tok,
           (unsigned long long)(C ? C->matrix.seq : 0), hex, dig, bp_json,
           cpath_esc, ncont, mpath_esc, nmat);
  return 0;
}

/* Publish dual-wire LATEST for TUI /smx + status probe (host data/matrix). */
static void publish_matrix_latest(const gk_consolidator *C, const char *dir) {
  char path[640];
  const char *slash, *base;
  size_t n;
  if (!C || !dir || !dir[0]) return;
  /* Always dual-wire LATEST beside knowledge artifacts. */
  snprintf(path, sizeof path, "%s/LATEST.json", dir);
  (void)smx_save_json(&C->matrix, path, 0);
  /*
   * Canonical host plate: when saving under …/knowledge, also write
   * …/matrix/LATEST.json (TUI /smx · status matrix probe).
   */
  slash = strrchr(dir, '/');
  base = slash ? slash + 1 : dir;
  if (strcmp(base, "knowledge") != 0) return;
  n = slash ? (size_t)(slash - dir) : 0;
  if (n == 0 || n + 24 >= sizeof path) return;
  memcpy(path, dir, n);
  path[n] = 0;
  snprintf(path + n, sizeof path - n, "/matrix");
  mkdir(path, 0755);
  snprintf(path + n, sizeof path - n, "/matrix/LATEST.json");
  (void)smx_save_json(&C->matrix, path, 0);
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
  /* Compact dual-wire SMX plate for lab/ops inspection (bits truncated). */
  snprintf(path, sizeof path, "%s/matrix.json", dir);
  if (smx_save_json(&C->matrix, path, 0) != 0) return -1;
  /* Dual-wire LATEST (dir + optional sibling data/matrix for host /smx). */
  publish_matrix_latest(C, dir);
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
