/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GROKIUM_SMX_H
#define GROKIUM_SMX_H
#include <stddef.h>
#include <stdint.h>
#include "grokium_law.h"

/* StateMatrix — Cube SoT, 8^3 = 512 bits. Non-verbal. */
typedef struct {
  uint8_t cell[GROKIUM_CELLS]; /* 0 or 1 only */
  uint32_t ticks;
  uint32_t bits_set;
  uint64_t seq;
  char host_id[32];
} grokium_smx;

void smx_clear(grokium_smx *m, const char *host_id);
void smx_set(grokium_smx *m, int x, int y, int z, int bit);
int  smx_get(const grokium_smx *m, int x, int y, int z);
void smx_ingest_bytes(grokium_smx *m, const uint8_t *bytes, size_t n);
void smx_ingest_bits_ascii(grokium_smx *m, const char *bits01);
/* XOR-fold another matrix (hive unity seek) */
void smx_xor_fold(grokium_smx *dst, const grokium_smx *src);
void smx_recompute(grokium_smx *m);
size_t smx_raw(const grokium_smx *m, uint8_t *out, size_t cap);
size_t smx_bits_ascii(const grokium_smx *m, char *out, size_t cap);
int smx_save_bin(const grokium_smx *m, const char *path);
int smx_load_bin(grokium_smx *m, const char *path);
/*
 * Compact dual-wire SMX plate (schema grokium.smx.v1, bits truncated).
 * Lab/ops disk/inspect; product multi-peer bus remains SMX2. Returns 0 ok.
 */
int smx_plate_json(const grokium_smx *m, int algodigit, char *out, size_t cap);
int smx_save_json(const grokium_smx *m, const char *path, int algodigit);
/* SHA-256 of cells (hex out[65]) */
void smx_sha256_hex(const grokium_smx *m, char out_hex[65]);
#endif
