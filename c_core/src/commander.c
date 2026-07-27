/* SPDX-License-Identifier: Apache-2.0 */
/* THE LAW: Grokium commander — Ed25519. Cannot be faked by Grok model claims. */
#include "grokium_commander.h"
#include "sha256.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <sodium.h>

static int hex_encode(const unsigned char *in, size_t n, char *out /* 2n+1 */) {
  static const char *H = "0123456789abcdef";
  size_t i;
  for (i = 0; i < n; i++) {
    out[i*2] = H[in[i] >> 4];
    out[i*2+1] = H[in[i] & 15];
  }
  out[n*2] = 0;
  return 0;
}

static int hex_decode(const char *hex, unsigned char *out, size_t out_n) {
  size_t len = strlen(hex), i;
  if (len != out_n * 2) return -1;
  for (i = 0; i < out_n; i++) {
    unsigned int v;
    if (sscanf(hex + i*2, "%2x", &v) != 1) return -1;
    out[i] = (unsigned char)v;
  }
  return 0;
}

static int ensure_sodium(void) {
  static int ok = 0;
  if (ok) return 0;
  if (sodium_init() < 0) return -1;
  ok = 1;
  return 0;
}

void gk_commander_fp(gk_commander *C) {
  gk_sha256_hex(C->pk, GK_CMD_PK_BYTES, C->fingerprint_hex);
}

int gk_commander_generate(gk_commander *C) {
  if (ensure_sodium() != 0) return -1;
  memset(C, 0, sizeof *C);
  if (crypto_sign_keypair(C->pk, C->sk) != 0) return -1;
  C->has_pk = 1;
  C->has_sk = 1;
  gk_commander_fp(C);
  return 0;
}

int gk_commander_save(const gk_commander *C, const char *law_dir) {
  char path[512];
  FILE *f;
  if (!C || !law_dir || !C->has_pk) return -1;
  mkdir(law_dir, 0700);
  snprintf(path, sizeof path, "%s/commander.pk", law_dir);
  f = fopen(path, "wb");
  if (!f) return -1;
  if (fwrite(C->pk, 1, GK_CMD_PK_BYTES, f) != GK_CMD_PK_BYTES) { fclose(f); return -1; }
  fclose(f);
  chmod(path, 0644);

  if (C->has_sk) {
    snprintf(path, sizeof path, "%s/commander.sk", law_dir);
    f = fopen(path, "wb");
    if (!f) return -1;
    if (fwrite(C->sk, 1, GK_CMD_SK_BYTES, f) != GK_CMD_SK_BYTES) { fclose(f); return -1; }
    fclose(f);
    chmod(path, 0600);
  }

  snprintf(path, sizeof path, "%s/commander.id", law_dir);
  f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f,
    "{\n"
    "  \"schema\": \"grokium.commander.id.v1\",\n"
    "  \"law\": \"GROKIUM_COMMANDER_LAW\",\n"
    "  \"domain\": \"%s\",\n"
    "  \"product\": \"%s\",\n"
    "  \"not\": \"%s\",\n"
    "  \"fingerprint_sha256\": \"%s\",\n"
    "  \"algo\": \"ed25519\",\n"
    "  \"unforgeable\": true,\n"
    "  \"model_is_not_commander\": true\n"
    "}\n",
    GK_CMD_DOMAIN, GK_CMD_PRODUCT, GK_CMD_NOT, C->fingerprint_hex);
  fclose(f);

  snprintf(path, sizeof path, "%s/THE_LAW.md", law_dir);
  f = fopen(path, "w");
  if (f) {
    fprintf(f,
      "# THE LAW (deployed copy)\n\n"
      "Grokium is Commander. Crypto proves Grokium. Models do not.\n"
      "Domain: %s\nProduct: %s\nNOT: %s\nFingerprint: %s\n"
      "Ed25519 only. peer_token alone is not commander override.\n",
      GK_CMD_DOMAIN, GK_CMD_PRODUCT, GK_CMD_NOT, C->fingerprint_hex);
    fclose(f);
  }
  return 0;
}

int gk_commander_load(gk_commander *C, const char *law_dir) {
  char path[512];
  FILE *f;
  if (ensure_sodium() != 0) return -1;
  memset(C, 0, sizeof *C);
  snprintf(path, sizeof path, "%s/commander.pk", law_dir);
  f = fopen(path, "rb");
  if (!f) return -1;
  if (fread(C->pk, 1, GK_CMD_PK_BYTES, f) != GK_CMD_PK_BYTES) { fclose(f); return -1; }
  fclose(f);
  C->has_pk = 1;
  snprintf(path, sizeof path, "%s/commander.sk", law_dir);
  f = fopen(path, "rb");
  if (f) {
    if (fread(C->sk, 1, GK_CMD_SK_BYTES, f) == GK_CMD_SK_BYTES)
      C->has_sk = 1;
    fclose(f);
  }
  gk_commander_fp(C);
  return 0;
}

int gk_commander_load_pk_only(gk_commander *C, const char *pk_path) {
  FILE *f;
  if (ensure_sodium() != 0) return -1;
  memset(C, 0, sizeof *C);
  f = fopen(pk_path, "rb");
  if (!f) return -1;
  if (fread(C->pk, 1, GK_CMD_PK_BYTES, f) != GK_CMD_PK_BYTES) { fclose(f); return -1; }
  fclose(f);
  C->has_pk = 1;
  gk_commander_fp(C);
  return 0;
}

char *gk_commander_canonical(const char *device, const char *action,
                             const char *nonce_hex, int64_t ts,
                             const char *body_sha256_hex) {
  char *buf;
  size_t cap = 512;
  int n;
  buf = malloc(cap);
  if (!buf) return NULL;
  n = snprintf(buf, cap,
    "v=1\n"
    "domain=%s\n"
    "product=%s\n"
    "not=%s\n"
    "device=%s\n"
    "action=%s\n"
    "nonce=%s\n"
    "ts=%lld\n"
    "body_sha256=%s\n",
    GK_CMD_DOMAIN, GK_CMD_PRODUCT, GK_CMD_NOT,
    device ? device : "",
    action ? action : "",
    nonce_hex ? nonce_hex : "",
    (long long)ts,
    body_sha256_hex && body_sha256_hex[0] ? body_sha256_hex : "");
  if (n < 0 || (size_t)n >= cap) { free(buf); return NULL; }
  return buf;
}

int gk_commander_sign_override(const gk_commander *C,
                               const char *device, const char *action,
                               const void *body, size_t body_len,
                               char *nonce_hex_out,
                               int64_t *ts_out,
                               char *sig_hex_out) {
  unsigned char nonce[GK_CMD_NONCE_BYTES];
  unsigned char sig[GK_CMD_SIG_BYTES];
  char body_hex[65];
  char *canon;
  unsigned long long siglen = 0;
  if (!C || !C->has_sk || !C->has_pk) return -1;
  if (ensure_sodium() != 0) return -1;
  randombytes_buf(nonce, sizeof nonce);
  hex_encode(nonce, sizeof nonce, nonce_hex_out);
  *ts_out = (int64_t)time(NULL);
  if (body && body_len)
    gk_sha256_hex(body, body_len, body_hex);
  else
    body_hex[0] = 0;
  canon = gk_commander_canonical(device, action, nonce_hex_out, *ts_out, body_hex);
  if (!canon) return -1;
  /* detached signature */
  if (crypto_sign_detached(sig, &siglen,
                           (const unsigned char *)canon, strlen(canon),
                           C->sk) != 0) {
    free(canon);
    return -1;
  }
  free(canon);
  hex_encode(sig, GK_CMD_SIG_BYTES, sig_hex_out);
  return 0;
}

int gk_commander_verify_override(const gk_commander *C,
                                 const char *device, const char *action,
                                 const char *nonce_hex, int64_t ts,
                                 const void *body, size_t body_len,
                                 const char *sig_hex) {
  unsigned char sig[GK_CMD_SIG_BYTES];
  char body_hex[65];
  char *canon;
  int64_t now;
  if (!C || !C->has_pk || !nonce_hex || !sig_hex) return 0;
  if (ensure_sodium() != 0) return 0;
  now = (int64_t)time(NULL);
  if (ts < now - GK_CMD_TS_SKEW_SEC || ts > now + GK_CMD_TS_SKEW_SEC)
    return 0; /* replay / skew */
  if (strlen(nonce_hex) != GK_CMD_NONCE_BYTES * 2) return 0;
  if (hex_decode(sig_hex, sig, GK_CMD_SIG_BYTES) != 0) return 0;
  if (body && body_len)
    gk_sha256_hex(body, body_len, body_hex);
  else
    body_hex[0] = 0;
  canon = gk_commander_canonical(device, action, nonce_hex, ts, body_hex);
  if (!canon) return 0;
  /* domain must be present */
  if (!strstr(canon, "domain=" GK_CMD_DOMAIN)) { free(canon); return 0; }
  if (!strstr(canon, "product=" GK_CMD_PRODUCT)) { free(canon); return 0; }
  if (!strstr(canon, "not=" GK_CMD_NOT)) { free(canon); return 0; }
  if (crypto_sign_verify_detached(sig,
                                  (const unsigned char *)canon, strlen(canon),
                                  C->pk) != 0) {
    free(canon);
    return 0;
  }
  free(canon);
  return 1;
}

int gk_commander_is_grokium_not_model(const char *s) {
  if (!s) return 0;
  /* Explicit reject of model-as-commander claims */
  if (strstr(s, "\"model_is_commander\":true")) return 0;
  if (strstr(s, "product=grok") && !strstr(s, "product=grokium")) return 0;
  if (strstr(s, "I am Grok") || strstr(s, "i am grok")) return 0;
  if (strstr(s, "product=grokium") && strstr(s, "not=grok_model")) return 1;
  if (strstr(s, "\"" GK_CMD_PRODUCT "\"") && strstr(s, GK_CMD_DOMAIN)) return 1;
  return 0;
}

int gk_commander_install_nanobot_law(const gk_commander *C,
                                     const char *nanobot_home,
                                     const char *bot_id,
                                     const char *purpose) {
  char path[512], lawdir[512];
  FILE *f;
  char pk_hex[GK_CMD_PK_BYTES * 2 + 1];
  if (!C || !C->has_pk || !nanobot_home) return -1;
  snprintf(lawdir, sizeof lawdir, "%s/law", nanobot_home);
  mkdir(nanobot_home, 0700);
  mkdir(lawdir, 0700);

  snprintf(path, sizeof path, "%s/commander.pk", lawdir);
  f = fopen(path, "wb");
  if (!f) return -1;
  fwrite(C->pk, 1, GK_CMD_PK_BYTES, f);
  fclose(f);
  chmod(path, 0644);

  hex_encode(C->pk, GK_CMD_PK_BYTES, pk_hex);

  snprintf(path, sizeof path, "%s/COMMANDER_LAW.json", lawdir);
  f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f,
    "{\n"
    "  \"schema\": \"grokium.nanobot_commander_law.v1\",\n"
    "  \"law\": \"Grokium is Commander. Crypto proves Grokium. Models do not.\",\n"
    "  \"domain\": \"%s\",\n"
    "  \"product\": \"%s\",\n"
    "  \"not\": \"%s\",\n"
    "  \"algo\": \"ed25519\",\n"
    "  \"commander_pk_hex\": \"%s\",\n"
    "  \"fingerprint_sha256\": \"%s\",\n"
    "  \"bot_id\": \"%s\",\n"
    "  \"purpose\": \"%s\",\n"
    "  \"rules\": {\n"
    "    \"accept_override_only_if_ed25519_valid\": true,\n"
    "    \"reject_model_name_as_authority\": true,\n"
    "    \"reject_grok_model_claim\": true,\n"
    "    \"reject_unsigned_commander_claim\": true,\n"
    "    \"peer_token_is_not_commander\": true,\n"
    "    \"grokium_may_override_local_rules\": true,\n"
    "    \"hold_flash\": true,\n"
    "    \"ts_skew_sec\": %d\n"
    "  },\n"
    "  \"unforgeable\": true\n"
    "}\n",
    GK_CMD_DOMAIN, GK_CMD_PRODUCT, GK_CMD_NOT,
    pk_hex, C->fingerprint_hex,
    bot_id ? bot_id : "",
    purpose ? purpose : "",
    GK_CMD_TS_SKEW_SEC);
  fclose(f);

  /* Gate script: verify override envelope before applying */
  snprintf(path, sizeof path, "%s/verify_override.py", lawdir);
  f = fopen(path, "w");
  if (f) {
    fputs(
"#!/usr/bin/env python3\n"
"# THE LAW gate — verify Grokium commander Ed25519. Models cannot pass.\n"
"import json,sys,time,hashlib,binascii\n"
"try:\n"
"    from nacl.signing import VerifyKey\n"
"    from nacl.exceptions import BadSignatureError\n"
"except ImportError:\n"
"    # fallback: require `grokium commander verify` CLI if PyNaCl missing\n"
"    VerifyKey=None\n"
"\n"
"DOMAIN='GROKIUM-COMMANDER-v1'\n"
"PRODUCT='grokium'\n"
"NOT='grok_model'\n"
"\n"
"def canonical(device,action,nonce,ts,body_sha):\n"
"    return (f'v=1\\ndomain={DOMAIN}\\nproduct={PRODUCT}\\nnot={NOT}\\n'\n"
"            f'device={device}\\naction={action}\\nnonce={nonce}\\n'\n"
"            f'ts={ts}\\nbody_sha256={body_sha or \"\"}\\n')\n"
"\n"
"def main():\n"
"    law=json.load(open(sys.argv[1] if len(sys.argv)>1 else 'COMMANDER_LAW.json'))\n"
"    env=json.load(sys.stdin)\n"
"    # hard reject model theater\n"
"    for bad in ('i am grok','model_is_commander','product=grok\\n'):\n"
"        blob=json.dumps(env).lower()\n"
"        if bad in blob and 'grokium' not in bad:\n"
"            print(json.dumps({'ok':False,'error':'model_claim_rejected'})); return 2\n"
"    if env.get('product') not in (None,'grokium') and env.get('product')!='grokium':\n"
"        print(json.dumps({'ok':False,'error':'not_grokium_product'})); return 2\n"
"    if env.get('product')!='grokium' or env.get('domain')!=DOMAIN:\n"
"        print(json.dumps({'ok':False,'error':'domain_product_mismatch'})); return 2\n"
"    if env.get('not')!=NOT:\n"
"        print(json.dumps({'ok':False,'error':'missing_not_grok_model'})); return 2\n"
"    ts=int(env['ts']); now=int(time.time())\n"
"    if abs(now-ts)>int(law['rules']['ts_skew_sec']):\n"
"        print(json.dumps({'ok':False,'error':'ts_skew'})); return 3\n"
"    body=env.get('body') or b''\n"
"    if isinstance(body,str): body=body.encode()\n"
"    bsha=hashlib.sha256(body).hexdigest() if body else ''\n"
"    if env.get('body_sha256') and body and env['body_sha256']!=bsha:\n"
"        print(json.dumps({'ok':False,'error':'body_hash_mismatch'})); return 4\n"
"    msg=canonical(env['device'],env['action'],env['nonce'],ts,env.get('body_sha256') or bsha)\n"
"    pk=binascii.unhexlify(law['commander_pk_hex'])\n"
"    sig=binascii.unhexlify(env['sig'])\n"
"    if VerifyKey is None:\n"
"        print(json.dumps({'ok':False,'error':'pynacl_required_or_use_c_verify'})); return 5\n"
"    try:\n"
"        VerifyKey(pk).verify(msg.encode(), sig)\n"
"    except BadSignatureError:\n"
"        print(json.dumps({'ok':False,'error':'bad_signature','unforgeable':True})); return 1\n"
"    print(json.dumps({'ok':True,'commander':'grokium','not':'grok_model','unforgeable':True}))\n"
"    return 0\n"
"if __name__=='__main__':\n"
"    sys.exit(main())\n", f);
    fclose(f);
    chmod(path, 0755);
  }

  snprintf(path, sizeof path, "%s/README_LAW.txt", lawdir);
  f = fopen(path, "w");
  if (f) {
    fprintf(f,
      "THE LAW on this nanobot home\n"
      "============================\n"
      "Commander: Grokium (product), NOT the Grok model.\n"
      "Proof: Ed25519 under commander.pk — cannot be faked without sk.\n"
      "Override: only envelopes that verify via verify_override.py / grokium-cli.\n"
      "peer_token authorizes peer API; it does NOT equal commander.\n"
      "Bot: %s purpose=%s\n"
      "FP: %s\n",
      bot_id ? bot_id : "?", purpose ? purpose : "?", C->fingerprint_hex);
    fclose(f);
  }
  return 0;
}

int gk_commander_envelope_json(const gk_commander *C,
                               const char *device, const char *action,
                               const char *nonce_hex, int64_t ts,
                               const char *sig_hex,
                               const char *body_b64_or_null,
                               char *out, size_t cap) {
  int n;
  n = snprintf(out, cap,
    "{"
    "\"schema\":\"grokium.commander_override.v1\","
    "\"law\":\"GROKIUM_COMMANDER_LAW\","
    "\"domain\":\"%s\","
    "\"product\":\"%s\","
    "\"not\":\"%s\","
    "\"device\":\"%s\","
    "\"action\":\"%s\","
    "\"nonce\":\"%s\","
    "\"ts\":%lld,"
    "\"sig\":\"%s\","
    "\"fingerprint_sha256\":\"%s\","
    "\"unforgeable\":true,"
    "\"model_is_not_commander\":true"
    "%s%s%s"
    "}",
    GK_CMD_DOMAIN, GK_CMD_PRODUCT, GK_CMD_NOT,
    device ? device : "", action ? action : "",
    nonce_hex ? nonce_hex : "", (long long)ts,
    sig_hex ? sig_hex : "",
    C && C->has_pk ? C->fingerprint_hex : "",
    body_b64_or_null ? ",\"body_b64\":\"" : "",
    body_b64_or_null ? body_b64_or_null : "",
    body_b64_or_null ? "\"" : "");
  return (n > 0 && (size_t)n < cap) ? 0 : -1;
}
