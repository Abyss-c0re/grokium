/* SPDX-License-Identifier: Apache-2.0 */
/* THE LAW: Grokium commander identity — unforgeable Ed25519. Not a Grok model. */
#ifndef GROKIUM_COMMANDER_H
#define GROKIUM_COMMANDER_H

#include <stddef.h>
#include <stdint.h>

#define GK_CMD_DOMAIN        "GROKIUM-COMMANDER-v1"
#define GK_CMD_PRODUCT       "grokium"
#define GK_CMD_NOT           "grok_model"
#define GK_CMD_PK_BYTES      32
#define GK_CMD_SK_BYTES      64
#define GK_CMD_SIG_BYTES     64
#define GK_CMD_NONCE_BYTES   32
#define GK_CMD_TS_SKEW_SEC   300  /* ±5 min */

typedef struct {
  unsigned char pk[GK_CMD_PK_BYTES];
  unsigned char sk[GK_CMD_SK_BYTES];
  int has_sk;
  int has_pk;
  char fingerprint_hex[65]; /* sha256(pk) hex */
} gk_commander;

/* Generate new commander keypair (law-grade identity). */
int gk_commander_generate(gk_commander *C);

/* Load/save under dir (commander.pk / commander.sk mode 0600). */
int gk_commander_load(gk_commander *C, const char *law_dir);
int gk_commander_save(const gk_commander *C, const char *law_dir);
int gk_commander_load_pk_only(gk_commander *C, const char *pk_path);

/* Build canonical payload string (caller frees with free()). */
char *gk_commander_canonical(const char *device, const char *action,
                             const char *nonce_hex, int64_t ts,
                             const char *body_sha256_hex);

/* Sign override — requires sk. Out: sig_hex[129], nonce_hex[65]. */
int gk_commander_sign_override(const gk_commander *C,
                               const char *device, const char *action,
                               const void *body, size_t body_len,
                               char *nonce_hex_out /*65*/,
                               int64_t *ts_out,
                               char *sig_hex_out /*129*/);

/* Verify — pin pk. Rejects wrong product domain, replay skew, missing fields. */
int gk_commander_verify_override(const gk_commander *C,
                                 const char *device, const char *action,
                                 const char *nonce_hex, int64_t ts,
                                 const void *body, size_t body_len,
                                 const char *sig_hex);

/* True only if product binding says grokium and not grok_model. */
int gk_commander_is_grokium_not_model(const char *canonical_or_json);

/* Install law pin + policy into nanobot home. */
int gk_commander_install_nanobot_law(const gk_commander *C,
                                     const char *nanobot_home,
                                     const char *bot_id,
                                     const char *purpose);

/* JSON envelope for wire (caller provides buf). */
int gk_commander_envelope_json(const gk_commander *C,
                               const char *device, const char *action,
                               const char *nonce_hex, int64_t ts,
                               const char *sig_hex,
                               const char *body_b64_or_null,
                               char *out, size_t cap);

#endif
