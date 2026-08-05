/* SPDX-License-Identifier: Apache-2.0 */
/* grokium-commander — keygen / sign / verify / install-law */
#include "grokium_commander.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Dual-wire deny plates — no free-text usage on the machine wire.
 * Commander is Ed25519 law identity only; LLM is never commander. */
static const char k_need_subcmd[] =
    "{\"schema\":\"grokium.commander.v1\",\"ok\":false,"
    "\"error\":\"need_subcmd\",\"product_wire\":\"smx2\","
    "\"peer_http\":\"lab_ops_only\",\"peer_http_is_product_bus\":false,"
    "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
    "\"llm_is_commander\":false,\"commander_is_model\":false,"
    "\"commander\":\"ed25519\",\"not\":\"grok_model\","
    "\"hint\":\"keygen|show|sign|verify|install-law --law-dir DIR\"}";

static const char k_need_law_dir[] =
    "{\"schema\":\"grokium.commander.v1\",\"ok\":false,"
    "\"error\":\"need_law_dir\",\"product_wire\":\"smx2\","
    "\"peer_http\":\"lab_ops_only\",\"peer_http_is_product_bus\":false,"
    "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
    "\"llm_is_commander\":false,\"commander_is_model\":false,"
    "\"commander\":\"ed25519\",\"not\":\"grok_model\","
    "\"hint\":\"pass --law-dir DIR (data/law)\"}";

static const char k_need_sign_args[] =
    "{\"schema\":\"grokium.commander.v1\",\"ok\":false,"
    "\"error\":\"need_sign_args\",\"product_wire\":\"smx2\","
    "\"peer_http\":\"lab_ops_only\",\"peer_http_is_product_bus\":false,"
    "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
    "\"llm_is_commander\":false,\"commander_is_model\":false,"
    "\"commander\":\"ed25519\",\"not\":\"grok_model\","
    "\"hint\":\"sign --law-dir DIR --device ID --action ACT\"}";

static const char k_need_verify_args[] =
    "{\"schema\":\"grokium.commander_verify.v1\",\"ok\":false,"
    "\"error\":\"need_verify_args\",\"product_wire\":\"smx2\","
    "\"peer_http\":\"lab_ops_only\",\"peer_http_is_product_bus\":false,"
    "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
    "\"llm_is_commander\":false,\"commander_is_model\":false,"
    "\"commander\":\"ed25519\",\"not\":\"grok_model\","
    "\"hint\":\"verify --law-dir DIR|--pk FILE --device ID --action ACT "
    "--nonce H --ts N --sig H\"}";

static const char k_need_install_args[] =
    "{\"schema\":\"grokium.commander.v1\",\"ok\":false,"
    "\"error\":\"need_install_args\",\"product_wire\":\"smx2\","
    "\"peer_http\":\"lab_ops_only\",\"peer_http_is_product_bus\":false,"
    "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
    "\"llm_is_commander\":false,\"commander_is_model\":false,"
    "\"commander\":\"ed25519\",\"not\":\"grok_model\","
    "\"hint\":\"install-law --law-dir DIR --home NANOBOT_HOME\"}";

static int plate_dual_wire_ok(const char *p) {
  return p && strstr(p, "\"product_wire\":\"smx2\"") &&
         strstr(p, "\"peer_http\":\"lab_ops_only\"") &&
         strstr(p, "\"peer_http_is_product_bus\":false") &&
         strstr(p, "\"llm_is_commander\":false") &&
         strstr(p, "\"hold_flash\":1") &&
         strstr(p, "\"share\":\"state_matrix_only\"");
}

static void usage(void) { printf("%s\n", k_need_subcmd); }

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

  /* Static plate dual-wire self-check (fail-closed before any crypto). */
  if (!plate_dual_wire_ok(k_need_subcmd) || !plate_dual_wire_ok(k_need_law_dir) ||
      !plate_dual_wire_ok(k_need_sign_args) ||
      !plate_dual_wire_ok(k_need_verify_args) ||
      !plate_dual_wire_ok(k_need_install_args) ||
      !strstr(k_need_subcmd, "\"commander\":\"ed25519\"") ||
      !strstr(k_need_subcmd, "\"not\":\"grok_model\"")) {
    fprintf(stderr, "grokium-commander: deny plate dual-wire fail\n");
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
    if (!law_dir) { printf("%s\n", k_need_law_dir); return 2; }
    if (gk_commander_generate(&C) != 0) { fprintf(stderr, "keygen failed\n"); return 1; }
    if (gk_commander_save(&C, law_dir) != 0) { fprintf(stderr, "save failed\n"); return 1; }
    printf("{\"schema\":\"grokium.commander.v1\",\"ok\":true,"
           "\"product\":\"grokium\",\"not\":\"grok_model\","
           "\"fingerprint\":\"%s\",\"law_dir\":\"%s\",\"unforgeable\":true,"
           "\"llm_is_commander\":false,\"commander_is_model\":false,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1}\n",
           C.fingerprint_hex, law_dir);
    return 0;
  }

  if (!strcmp(cmd, "show")) {
    if (!law_dir) { printf("%s\n", k_need_law_dir); return 2; }
    if (gk_commander_load(&C, law_dir) != 0) { fprintf(stderr, "load failed\n"); return 1; }
    printf("{\"schema\":\"grokium.commander.v1\",\"ok\":true,"
           "\"product\":\"grokium\",\"not\":\"grok_model\","
           "\"domain\":\"%s\",\"fingerprint\":\"%s\",\"has_sk\":%s,"
           "\"unforgeable\":true,\"llm_is_commander\":false,"
           "\"commander_is_model\":false,\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1}\n",
           GK_CMD_DOMAIN, C.fingerprint_hex, C.has_sk ? "true" : "false");
    return 0;
  }

  if (!strcmp(cmd, "sign")) {
    char nonce_hex[65], sig_hex[129], env[2048];
    unsigned char *body = NULL; size_t blen = 0;
    if (!law_dir || !device || !action) {
      printf("%s\n", k_need_sign_args);
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
      printf("%s\n", k_need_verify_args);
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
    printf("{\"schema\":\"grokium.commander_verify.v1\",\"ok\":%s,"
           "\"commander\":%s,\"not\":\"grok_model\",\"unforgeable\":true,"
           "\"llm_is_commander\":false,\"commander_is_model\":false,"
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1}\n",
           ok ? "true" : "false", ok ? "\"grokium\"" : "null");
    return ok ? 0 : 1;
  }

  if (!strcmp(cmd, "install-law")) {
    if (!law_dir || !home) {
      printf("%s\n", k_need_install_args);
      return 2;
    }
    if (gk_commander_load(&C, law_dir) != 0) { fprintf(stderr, "load failed\n"); return 1; }
    if (gk_commander_install_nanobot_law(&C, home, bot ? bot : "nb", purpose ? purpose : "assigned") != 0) {
      fprintf(stderr, "install failed\n"); return 1;
    }
    printf("{\"schema\":\"grokium.commander.v1\",\"ok\":true,"
           "\"home\":\"%s\",\"bot\":\"%s\",\"fingerprint\":\"%s\","
           "\"law\":\"installed\",\"not\":\"grok_model\","
           "\"llm_is_commander\":false,\"product_wire\":\"smx2\","
           "\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1}\n",
           home, bot ? bot : "nb", C.fingerprint_hex);
    return 0;
  }

  usage();
  return 2;
}
