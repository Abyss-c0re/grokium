/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_smx.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>

/* Machine token for host_id plate field (no JSON/path inject). */
static void host_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in || !in[0]) return;
  for (i = 0; in[i] && o + 1 < cap; i++) {
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

void smx_clear(grokium_smx *m, const char *host_id) {
  if (!m) return;
  memset(m, 0, sizeof *m);
  if (host_id && host_id[0]) {
    host_token(host_id, m->host_id, sizeof m->host_id);
    if (!m->host_id[0])
      snprintf(m->host_id, sizeof m->host_id, "host");
  }
}

void smx_set(grokium_smx *m, int x, int y, int z, int bit) {
  int i;
  if (!m || x < 0 || y < 0 || z < 0 || x >= GROKIUM_EDGE ||
      y >= GROKIUM_EDGE || z >= GROKIUM_EDGE)
    return;
  i = x + y * GROKIUM_EDGE + z * GROKIUM_EDGE * GROKIUM_EDGE;
  m->cell[i] = bit ? 1 : 0;
  smx_recompute(m);
}

int smx_get(const grokium_smx *m, int x, int y, int z) {
  int i;
  if (!m || x < 0 || y < 0 || z < 0 || x >= GROKIUM_EDGE ||
      y >= GROKIUM_EDGE || z >= GROKIUM_EDGE)
    return 0;
  i = x + y * GROKIUM_EDGE + z * GROKIUM_EDGE * GROKIUM_EDGE;
  return m->cell[i] ? 1 : 0;
}

void smx_recompute(grokium_smx *m) {
  uint32_t i, set = 0;
  if (!m) return;
  for (i = 0; i < GROKIUM_CELLS; i++)
    if (m->cell[i]) set++;
  m->bits_set = set;
  m->ticks++;
}

void smx_ingest_bytes(grokium_smx *m, const uint8_t *bytes, size_t n) {
  size_t i;
  if (!m || !bytes) return;
  for (i = 0; i < n && i < GROKIUM_CELLS; i++)
    m->cell[i] = bytes[i] & 1;
  for (; i < GROKIUM_CELLS; i++)
    m->cell[i] = 0;
  smx_recompute(m);
}

void smx_ingest_bits_ascii(grokium_smx *m, const char *bits01) {
  size_t i = 0;
  if (!m) return;
  memset(m->cell, 0, sizeof m->cell);
  if (bits01) {
    const char *p = bits01;
    while (*p && i < GROKIUM_CELLS) {
      if (*p == '0' || *p == '1') {
        m->cell[i++] = (uint8_t)(*p - '0');
      }
      p++;
    }
  }
  smx_recompute(m);
}

void smx_xor_fold(grokium_smx *dst, const grokium_smx *src) {
  uint32_t i;
  if (!dst || !src) return;
  for (i = 0; i < GROKIUM_CELLS; i++)
    dst->cell[i] ^= src->cell[i] ? 1 : 0;
  smx_recompute(dst);
}

size_t smx_raw(const grokium_smx *m, uint8_t *out, size_t cap) {
  size_t need = (GROKIUM_CELLS + 7) / 8;
  size_t i;
  if (!m || !out || cap < need) return 0;
  memset(out, 0, need);
  for (i = 0; i < GROKIUM_CELLS; i++)
    if (m->cell[i]) out[i / 8] |= (uint8_t)(1u << (i % 8));
  return need;
}

size_t smx_bits_ascii(const grokium_smx *m, char *out, size_t cap) {
  size_t i;
  if (!m || !out || cap < GROKIUM_CELLS + 1) return 0;
  for (i = 0; i < GROKIUM_CELLS; i++)
    out[i] = m->cell[i] ? '1' : '0';
  out[GROKIUM_CELLS] = 0;
  return GROKIUM_CELLS;
}

int smx_save_bin(const grokium_smx *m, const char *path) {
  FILE *f;
  if (!m || !path) return -1;
  f = fopen(path, "wb");
  if (!f) return -1;
  if (fwrite(m, 1, sizeof *m, f) != sizeof *m) {
    fclose(f);
    return -1;
  }
  fclose(f);
  return 0;
}

int smx_load_bin(grokium_smx *m, const char *path) {
  FILE *f;
  if (!m || !path) return -1;
  f = fopen(path, "rb");
  if (!f) return -1;
  if (fread(m, 1, sizeof *m, f) != sizeof *m) {
    fclose(f);
    return -1;
  }
  fclose(f);
  smx_recompute(m);
  return 0;
}

int smx_plate_json(const grokium_smx *m, int algodigit, char *out, size_t cap) {
  char bits[GROKIUM_CELLS + 1];
  char hex[65];
  char host_tok[32];
  if (!m || !out || cap < 128) return -1;
  smx_bits_ascii(m, bits, sizeof bits);
  smx_sha256_hex(m, hex);
  host_token(m->host_id, host_tok, sizeof host_tok);
  if (!host_tok[0]) snprintf(host_tok, sizeof host_tok, "host");
  /* Compact plate: dual-wire honesty · py=0; bits truncated (disk/lab inspect). */
  snprintf(out, cap,
           "{\"schema\":\"grokium.smx.v1\",\"ok\":true,"
           "\"seq\":%llu,\"bits_set\":%u,\"ticks\":%u,"
           "\"host\":\"%s\",\"digit\":%d,\"sha256\":\"%s\","
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_on_hot_path\":false,\"llm_is_commander\":false,"
           "\"python\":0,\"bits\":\"%.64s...\"}",
           (unsigned long long)m->seq, m->bits_set, m->ticks, host_tok,
           algodigit, hex, bits);
  return 0;
}

int smx_save_json(const grokium_smx *m, const char *path, int algodigit) {
  FILE *f;
  char plate[768];
  if (!m || !path) return -1;
  if (smx_plate_json(m, algodigit, plate, sizeof plate) != 0) return -1;
  f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f, "%s\n", plate);
  fclose(f);
  return 0;
}

/* After "key", skip to value start (post-colon whitespace). */
static const char *json_value_after_key(const char *body, const char *key) {
  char pat[48];
  const char *p;
  if (!body || !key || !key[0]) return NULL;
  snprintf(pat, sizeof pat, "\"%s\"", key);
  p = strstr(body, pat);
  if (!p) return NULL;
  p = strchr(p + strlen(pat), ':');
  if (!p) return NULL;
  p++;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  return p;
}

static unsigned count_bitstring(const char *s) {
  unsigned cnt = 0;
  if (!s) return 0;
  for (; *s && *s != '"'; s++)
    if (*s == '1') cnt++;
  return cnt;
}

/* Copy first n 01 digits into out (NUL-term); returns digits copied. */
static size_t copy_bit_prefix(const char *s, char *out, size_t cap, size_t n) {
  size_t o = 0;
  if (!out || cap < 2) return 0;
  out[0] = 0;
  if (!s) return 0;
  for (; *s && *s != '"' && o + 1 < cap && o < n; s++) {
    if (*s == '0' || *s == '1')
      out[o++] = *s;
  }
  out[o] = 0;
  return o;
}

int smx_disk_plate_json(const char *body, char *out, size_t cap) {
  const char *v;
  unsigned bits_set = 0;
  int have = 0;
  char bit_pref[72];
  size_t npref = 0;
  if (!body || !body[0] || !out || cap < 128) return -1;
  /*
   * Dual-wire smx_plate_json / gk_matrix_json already honest — compact pass
   * through when schema + product_wire + python=0 are present.
   */
  if (strstr(body, "\"schema\":\"grokium.smx.v1\"") &&
      strstr(body, "\"product_wire\":\"smx2\"") &&
      strstr(body, "\"python\":0")) {
    size_t i = 0, j = 0;
    /* Skip leading whitespace; copy one line / compact JSON only. */
    while (body[i] == ' ' || body[i] == '\t' || body[i] == '\n' ||
           body[i] == '\r')
      i++;
    for (; body[i] && body[i] != '\n' && j + 1 < cap; i++)
      out[j++] = body[i];
    out[j] = 0;
    if (j > 0 && strstr(out, "\"product_wire\":\"smx2\""))
      return 0;
    /* Fall through to rebuild if multi-line pretty JSON. */
  }
  /* Prefer dual-wire bits_set, then bits, then legacy sot_bits. */
  v = json_value_after_key(body, "bits_set");
  if (v && *v >= '0' && *v <= '9') {
    unsigned long u = 0;
    while (*v >= '0' && *v <= '9' && u < 1000000UL) {
      u = u * 10UL + (unsigned long)(*v - '0');
      v++;
    }
    bits_set = (unsigned)u;
    have = 1;
  }
  v = json_value_after_key(body, "bits");
  if (v && *v == '"') {
    npref = copy_bit_prefix(v + 1, bit_pref, sizeof bit_pref, 64);
    if (!have)
      bits_set = count_bitstring(v + 1);
    have = 1;
  }
  if (!have || npref == 0) {
    v = json_value_after_key(body, "sot_bits");
    if (v && *v == '"') {
      npref = copy_bit_prefix(v + 1, bit_pref, sizeof bit_pref, 64);
      if (!have)
        bits_set = count_bitstring(v + 1);
      have = 1;
    }
  }
  if (!have) return -1;
  if (npref == 0)
    bit_pref[0] = 0;
  /* Dual-wire rewrite — no nested free-form plate leaf · py=0. */
  snprintf(out, cap,
           "{\"schema\":\"grokium.smx.v1\",\"ok\":true,"
           "\"bits_set\":%u,\"source\":\"disk\","
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"llm_on_hot_path\":false,\"llm_is_commander\":false,"
           "\"python\":0,\"bits\":\"%s%s\"}",
           bits_set, bit_pref, npref >= 64 ? "..." : "");
  return 0;
}

void smx_sha256_hex(const grokium_smx *m, char out_hex[65]) {
  if (!m || !out_hex) return;
  gk_sha256_hex(m->cell, GROKIUM_CELLS, out_hex);
}
