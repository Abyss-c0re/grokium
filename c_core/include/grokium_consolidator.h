/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GROKIUM_CONSOLIDATOR_H
#define GROKIUM_CONSOLIDATOR_H
/*
 * Knowledge consolidator → StateMatrix (Cube hot path).
 * Inspired by dual-plane knowledge consolidation concepts
 * (episodes → recency/novelty → distilled concepts → binary SoT).
 * No LLM on this path. No prose on the lattice.
 */
#include "grokium_smx.h"
#include <stdint.h>

#define GK_MAX_ITEMS    256
#define GK_MAX_CONCEPTS 64
#define GK_ID_LEN       48
#define GK_SEAL_TTL_SEC 900

typedef struct {
  char id[GK_ID_LEN];
  uint8_t hash[32];       /* content fingerprint */
  double ts;              /* unix */
  float recency;          /* 0..1 after decay */
  float novelty;          /* 0..1 */
  float align;            /* goal align 0..1 */
  int promoted;           /* distilled to concept */
} gk_item;

typedef struct {
  char id[GK_ID_LEN];
  uint8_t hash[32];
  int n_sources;
  float strength;
} gk_concept;

typedef struct {
  gk_item items[GK_MAX_ITEMS];
  int n_items;
  gk_concept concepts[GK_MAX_CONCEPTS];
  int n_concepts;
  grokium_smx matrix;     /* SoT after consolidate */
  grokium_smx concept_mx; /* distilled concept lattice */
  double last_seal_ts;
  char grade[16];         /* STRONG|OK|WEAK|EMPTY */
  int seal_ok;
  uint64_t pack_seq;
} gk_consolidator;

void gk_init(gk_consolidator *C, const char *host_id);
/* Ingest knowledge fragment (text/bytes → item). Dedup by hash. */
int  gk_ingest(gk_consolidator *C, const char *id, const void *data, size_t n, double now_ts);
/* Recency decay + novelty vs existing; build StateMatrix + concepts */
int  gk_consolidate(gk_consolidator *C, double now_ts);
/* Ability card: grade + seal (no LLM heartbeat eval) */
int  gk_ability(const gk_consolidator *C, double now_ts,
                char *json_out, size_t cap);
/*
 * Dual-wire SMX snapshot plate (schema grokium.smx.v1, full bits).
 * Lab/ops only — product multi-peer bus remains SMX2. Returns 0 ok.
 */
int  gk_matrix_json(const gk_consolidator *C, char *out, size_t cap);
/*
 * Dual-wire AlgoCube bridge plate (schema grokium.cube_status.v1).
 * Digit/blueprint from matrix; path counts under data_root (lab/ops).
 */
int  gk_cube_status_json(const gk_consolidator *C, int hold_flash,
                         const char *data_root, char *out, size_t cap);
/*
 * Dual-wire coord ingest ack (schema grokium.coord.v1).
 * Call after gk_ingest + gk_consolidate; grade/bits/seq/sha honest.
 * Loopback /v1/coord, consolidate CLI, and host /coord share this plate.
 */
void gk_coord_json(const gk_consolidator *C, char *out, size_t cap);
/* Dual-wire coord deny (need_plate | smx_filter_deny | ingest_failed | …). */
void gk_coord_err_json(const char *error, char *out, size_t cap);
/*
 * Dual-wire save ack plate (schema grokium.consolidator_save.v1).
 * Call after successful gk_save_dir. dir is JSON-escaped; grade machine-token.
 * Consolidator CLI `save` shares this builder (no free-text-only usage).
 */
void gk_save_json(const gk_consolidator *C, const char *dir, char *out,
                  size_t cap);
/*
 * Persist matrix.bin/json + dual-wire LATEST.json under dir.
 * When dir basename is "knowledge", also writes sibling matrix/LATEST.json
 * (canonical host TUI /smx · status probe path under data/).
 */
int  gk_save_dir(const gk_consolidator *C, const char *dir);
int  gk_load_dir(gk_consolidator *C, const char *dir);
#endif
