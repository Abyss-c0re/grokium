/* SPDX-License-Identifier: Apache-2.0 */
/* grokium-commander — keygen / sign / verify / install-law */
#include "grokium_commander.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
  fprintf(stderr,
    "grokium-commander — THE LAW (Ed25519, unforgeable)\n"
    "  keygen --law-dir DIR\n"
    "  show   --law-dir DIR\n"
    "  sign   --law-dir DIR --device ID --action ACT [--body FILE|-]\n"
    "  verify --law-dir DIR|--pk FILE --device ID --action ACT --nonce H --ts N --sig H [--body FILE|-]\n"
    "  install-law --law-dir DIR --home NANOBOT_HOME --bot ID --purpose P\n"
    "Grok model claims are never commander. Crypto only.\n");
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

  if (argc < 2) { usage(); return 2; }
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
    if (!law_dir) { usage(); return 2; }
    if (gk_commander_generate(&C) != 0) { fprintf(stderr, "keygen failed\n"); return 1; }
    if (gk_commander_save(&C, law_dir) != 0) { fprintf(stderr, "save failed\n"); return 1; }
    printf("{\"ok\":true,\"product\":\"grokium\",\"not\":\"grok_model\",\"fingerprint\":\"%s\",\"law_dir\":\"%s\",\"unforgeable\":true}\n",
           C.fingerprint_hex, law_dir);
    return 0;
  }

  if (!strcmp(cmd, "show")) {
    if (!law_dir) { usage(); return 2; }
    if (gk_commander_load(&C, law_dir) != 0) { fprintf(stderr, "load failed\n"); return 1; }
    printf("{\"ok\":true,\"product\":\"grokium\",\"not\":\"grok_model\",\"domain\":\"%s\",\"fingerprint\":\"%s\",\"has_sk\":%s,\"unforgeable\":true}\n",
           GK_CMD_DOMAIN, C.fingerprint_hex, C.has_sk ? "true" : "false");
    return 0;
  }

  if (!strcmp(cmd, "sign")) {
    char nonce_hex[65], sig_hex[129], env[2048];
    unsigned char *body = NULL; size_t blen = 0;
    if (!law_dir || !device || !action) { usage(); return 2; }
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
      usage(); return 2;
    }
    if (law_dir) {
      if (gk_commander_load(&C, law_dir) != 0) { fprintf(stderr, "load failed\n"); return 1; }
    } else {
      if (gk_commander_load_pk_only(&C, pk) != 0) { fprintf(stderr, "pk load failed\n"); return 1; }
    }
    if (body_path) read_all(body_path, &body, &blen);
    ok = gk_commander_verify_override(&C, device, action, nonce, ts, body, blen, sig);
    free(body);
    printf("{\"ok\":%s,\"commander\":%s,\"not\":\"grok_model\",\"unforgeable\":true}\n",
           ok ? "true" : "false", ok ? "\"grokium\"" : "null");
    return ok ? 0 : 1;
  }

  if (!strcmp(cmd, "install-law")) {
    if (!law_dir || !home) { usage(); return 2; }
    if (gk_commander_load(&C, law_dir) != 0) { fprintf(stderr, "load failed\n"); return 1; }
    if (gk_commander_install_nanobot_law(&C, home, bot ? bot : "nb", purpose ? purpose : "assigned") != 0) {
      fprintf(stderr, "install failed\n"); return 1;
    }
    printf("{\"ok\":true,\"home\":\"%s\",\"bot\":\"%s\",\"fingerprint\":\"%s\",\"law\":\"installed\"}\n",
           home, bot ? bot : "nb", C.fingerprint_hex);
    return 0;
  }

  usage();
  return 2;
}
