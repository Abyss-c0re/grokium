/* SPDX-License-Identifier: Apache-2.0 */
/* grokium-commander — keygen / sign / verify / install-law */
#include "grokium_commander.h"
#include "grokium_law.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Dual-wire Commander deny plates — shared builder (Commander ≠ model). */
static const char k_hint_subcmd[] =
    "keygen|show|sign|verify|install-law --law-dir DIR";
static const char k_hint_law_dir[] = "pass --law-dir DIR (data/law)";
static const char k_hint_sign[] =
    "sign --law-dir DIR --device ID --action ACT";
static const char k_hint_verify[] =
    "verify --law-dir DIR|--pk FILE --device ID --action ACT "
    "--nonce H --ts N --sig H";
static const char k_hint_install[] =
    "install-law --law-dir DIR --home NANOBOT_HOME";

static int plate_dual_wire_ok(const char *p) {
  return p && strstr(p, "\"product_wire\":\"smx2\"") &&
         strstr(p, "\"peer_http\":\"lab_ops_only\"") &&
         strstr(p, "\"peer_http_is_product_bus\":false") &&
         strstr(p, "\"llm_is_commander\":false") &&
         strstr(p, "\"hold_flash\":1") &&
         strstr(p, "\"share\":\"state_matrix_only\"") &&
         strstr(p, "\"commander\":\"ed25519\"") &&
         strstr(p, "\"not\":\"grok_model\"") &&
         strstr(p, "\"commander_is_model\":false");
}

static void emit_deny(const char *leaf, const char *error, const char *hint) {
  char plate[640];
  grokium_commander_deny_json(leaf, error, hint, plate, sizeof plate);
  printf("%s\n", plate);
}

static void usage(void) {
  emit_deny("commander", "need_subcmd", k_hint_subcmd);
}

static int read_all(const char *path, unsigned char **out, size_t *n) {
  FILE *f = (!path || !strcmp(path, "-")) ? stdin : fopen(path, "rb");
  unsigned char *buf = NULL; size_t cap = 0, len = 0;
  if (!f) return -1;
  for (;;) {
    unsigned char chunk[4096];
    size_t r = fread(chunk, 1, sizeof chunk, f);
    if (r) {
      if (len + r > cap) {
        cap = (cap ? cap * 2 : 8192) + r;
        buf = realloc(buf, cap);
      }
      memcpy(buf + len, chunk, r);
      len += r;
    }
    if (r < sizeof chunk) break;
  }
  if (f != stdin) fclose(f);
  *out = buf; *n = len;
  return 0;
}

int main(int argc, char **argv) {
  const char *cmd = NULL, *law_dir = NULL, *pk = NULL;
  const char *device = NULL, *action = NULL, *home = NULL, *bot = NULL, *purpose = NULL;
  const char *nonce = NULL, *sig = NULL, *body_path = NULL;
  int64_t ts = 0;
  int i;
  gk_commander C;
  char plate[640];

  /* Shared plate dual-wire self-check (fail-closed before any crypto). */
  grokium_commander_deny_json("commander", "need_subcmd", k_hint_subcmd, plate,
                              sizeof plate);
  if (!plate_dual_wire_ok(plate) ||
      !strstr(plate, "\"error\":\"need_subcmd\"")) {
    fprintf(stderr, "grokium-commander: deny plate dual-wire fail\n");
    return 1;
  }
  grokium_commander_deny_json("commander", "need_law_dir", k_hint_law_dir,
                              plate, sizeof plate);
  if (!plate_dual_wire_ok(plate) ||
      !strstr(plate, "\"error\":\"need_law_dir\"")) {
    fprintf(stderr, "grokium-commander: need_law_dir dual-wire fail\n");
    return 1;
  }
  grokium_commander_deny_json("commander_verify", "need_verify_args",
                              k_hint_verify, plate, sizeof plate);
  if (!plate_dual_wire_ok(plate) ||
      !strstr(plate, "\"schema\":\"grokium.commander_verify.v1\"") ||
      !strstr(plate, "\"error\":\"need_verify_args\"")) {
    fprintf(stderr, "grokium-commander: need_verify_args dual-wire fail\n");
    return 1;
  }
  /* Success builders: dual-wire + path inject sanitize (no free-text law_dir). */
  grokium_commander_ok_json("deadbeef", "data/law\";drop", "GROKIUM-COMMANDER-v1",
                            1, plate, sizeof plate);
  if (!strstr(plate, "\"ok\":true") ||
      !strstr(plate, "\"fingerprint\":\"deadbeef\"") ||
      !strstr(plate, "\"product_wire\":\"smx2\"") ||
      !strstr(plate, "\"llm_is_commander\":false") ||
      !strstr(plate, "\"has_sk\":true") || strstr(plate, "\";drop") ||
      strstr(plate, "\";") || strstr(plate, "\\\"") ||
      !strstr(plate, "\"law_dir\":\"data/law_drop\"")) {
    fprintf(stderr, "grokium-commander: ok plate dual-wire/sanitize fail\n");
    return 1;
  }
  grokium_commander_verify_json(1, plate, sizeof plate);
  if (!strstr(plate, "\"schema\":\"grokium.commander_verify.v1\"") ||
      !strstr(plate, "\"ok\":true") ||
      !strstr(plate, "\"commander\":\"grokium\"") ||
      !strstr(plate, "\"peer_http_is_product_bus\":false")) {
    fprintf(stderr, "grokium-commander: verify plate dual-wire fail\n");
    return 1;
  }
  grokium_commander_install_json("home\";x", "nb-test", "abc", plate,
                                 sizeof plate);
  if (!strstr(plate, "\"law\":\"installed\"") || strstr(plate, "\";x") ||
      !strstr(plate, "\"home\":\"home_x\"") ||
      !strstr(plate, "\"hold_flash\":1") ||
      !strstr(plate, "\"commander_is_model\":false")) {
    fprintf(stderr, "grokium-commander: install plate sanitize fail\n");
    return 1;
  }

  if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "-h") ||
      !strcmp(argv[1], "--help")) {
    usage();
    return 2;
  }
  cmd = argv[1];
  for (i = 2; i < argc; i++) {
    if (!strcmp(argv[i], "--law-dir") && i+1 < argc) law_dir = argv[++i];
    else if (!strcmp(argv[i], "--pk") && i+1 < argc) pk = argv[++i];
    else if (!strcmp(argv[i], "--device") && i+1 < argc) device = argv[++i];
    else if (!strcmp(argv[i], "--action") && i+1 < argc) action = argv[++i];
    else if (!strcmp(argv[i], "--home") && i+1 < argc) home = argv[++i];
    else if (!strcmp(argv[i], "--bot") && i+1 < argc) bot = argv[++i];
    else if (!strcmp(argv[i], "--purpose") && i+1 < argc) purpose = argv[++i];
    else if (!strcmp(argv[i], "--nonce") && i+1 < argc) nonce = argv[++i];
    else if (!strcmp(argv[i], "--ts") && i+1 < argc) ts = atoll(argv[++i]);
    else if (!strcmp(argv[i], "--sig") && i+1 < argc) sig = argv[++i];
    else if (!strcmp(argv[i], "--body") && i+1 < argc) body_path = argv[++i];
  }

  if (!strcmp(cmd, "keygen")) {
    if (!law_dir) {
      emit_deny("commander", "need_law_dir", k_hint_law_dir);
      return 2;
    }
    if (gk_commander_generate(&C) != 0) { fprintf(stderr, "keygen failed\n"); return 1; }
    if (gk_commander_save(&C, law_dir) != 0) { fprintf(stderr, "save failed\n"); return 1; }
    /* Shared dual-wire success (path-sanitized law_dir · LLM ≠ commander). */
    grokium_commander_ok_json(C.fingerprint_hex, law_dir, NULL, -1, plate,
                              sizeof plate);
    printf("%s\n", plate);
    return 0;
  }

  if (!strcmp(cmd, "show")) {
    if (!law_dir) {
      emit_deny("commander", "need_law_dir", k_hint_law_dir);
      return 2;
    }
    if (gk_commander_load(&C, law_dir) != 0) { fprintf(stderr, "load failed\n"); return 1; }
    /* Shared dual-wire show (domain + has_sk · match GET /v1/commander). */
    grokium_commander_ok_json(C.fingerprint_hex, law_dir, GK_CMD_DOMAIN,
                              C.has_sk ? 1 : 0, plate, sizeof plate);
    printf("%s\n", plate);
    return 0;
  }

  if (!strcmp(cmd, "sign")) {
    char nonce_hex[65], sig_hex[129], env[2048];
    unsigned char *body = NULL; size_t blen = 0;
    if (!law_dir || !device || !action) {
      emit_deny("commander", "need_sign_args", k_hint_sign);
      return 2;
    }
    if (gk_commander_load(&C, law_dir) != 0 || !C.has_sk) {
      fprintf(stderr, "need commander.sk in law-dir\n"); return 1;
    }
    if (body_path) read_all(body_path, &body, &blen);
    if (gk_commander_sign_override(&C, device, action, body, blen,
                                   nonce_hex, &ts, sig_hex) != 0) {
      fprintf(stderr, "sign failed\n"); free(body); return 1;
    }
    gk_commander_envelope_json(&C, device, action, nonce_hex, ts, sig_hex, NULL, env, sizeof env);
    puts(env);
    free(body);
    return 0;
  }

  if (!strcmp(cmd, "verify")) {
    unsigned char *body = NULL; size_t blen = 0;
    int ok;
    if ((!law_dir && !pk) || !device || !action || !nonce || !sig || !ts) {
      emit_deny("commander_verify", "need_verify_args", k_hint_verify);
      return 2;
    }
    if (law_dir) {
      if (gk_commander_load(&C, law_dir) != 0) { fprintf(stderr, "load failed\n"); return 1; }
    } else {
      if (gk_commander_load_pk_only(&C, pk) != 0) { fprintf(stderr, "pk load failed\n"); return 1; }
    }
    if (body_path) read_all(body_path, &body, &blen);
    ok = gk_commander_verify_override(&C, device, action, nonce, ts, body, blen, sig);
    free(body);
    /* Shared dual-wire verify plate (match HTTP /v1/commander/verify). */
    grokium_commander_verify_json(ok, plate, sizeof plate);
    printf("%s\n", plate);
    return ok ? 0 : 1;
  }

  if (!strcmp(cmd, "install-law")) {
    if (!law_dir || !home) {
      emit_deny("commander", "need_install_args", k_hint_install);
      return 2;
    }
    if (gk_commander_load(&C, law_dir) != 0) { fprintf(stderr, "load failed\n"); return 1; }
    if (gk_commander_install_nanobot_law(&C, home, bot ? bot : "nb", purpose ? purpose : "assigned") != 0) {
      fprintf(stderr, "install failed\n"); return 1;
    }
    /* Shared dual-wire install ack (home/bot path-sanitized). */
    grokium_commander_install_json(home, bot ? bot : "nb", C.fingerprint_hex,
                                   plate, sizeof plate);
    printf("%s\n", plate);
    return 0;
  }

  usage();
  return 2;
}
