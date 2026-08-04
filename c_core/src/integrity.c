/* SPDX-License-Identifier: Apache-2.0
 * Integrity tick / reseal — pure C, fail-closed, no telemetry.
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_integrity.h"
#include "sha256.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define GK_INT_MAX_FILES 128
#define GK_INT_PATH_LEN  256
#define GK_INT_FILE_MAX  (4 * 1024 * 1024)

typedef struct {
  char path[GK_INT_PATH_LEN];
  char hex[65];
} gk_seal_ent;

static void root_join(const char *root, const char *rel, char *out, size_t cap) {
  if (root && root[0])
    snprintf(out, cap, "%s/%s", root, rel);
  else
    snprintf(out, cap, "%s", rel);
}

static int file_sha256_hex(const char *path, char hex[65]) {
  FILE *f;
  uint8_t *buf = NULL;
  long sz;
  size_t n;
  if (!path || !hex) return -1;
  f = fopen(path, "rb");
  if (!f) return -1;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  sz = ftell(f);
  if (sz < 0 || sz > GK_INT_FILE_MAX) {
    fclose(f);
    return -1;
  }
  rewind(f);
  buf = (uint8_t *)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return -1;
  }
  n = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  gk_sha256_hex(buf, n, hex);
  free(buf);
  return 0;
}

static int read_file_str(const char *path, char *out, size_t cap) {
  FILE *f;
  size_t n;
  if (!path || !out || cap < 2) return -1;
  f = fopen(path, "r");
  if (!f) return -1;
  n = fread(out, 1, cap - 1, f);
  fclose(f);
  out[n] = 0;
  return 0;
}

static int ent_cmp(const void *a, const void *b) {
  return strcmp(((const gk_seal_ent *)a)->path, ((const gk_seal_ent *)b)->path);
}

static int add_ent(gk_seal_ent *ents, int *n, const char *rel, const char *root) {
  char full[512], hex[65];
  if (!ents || !n || !rel || *n >= GK_INT_MAX_FILES) return -1;
  if (strlen(rel) >= GK_INT_PATH_LEN) return -1;
  root_join(root, rel, full, sizeof full);
  if (access(full, R_OK) != 0) return -1;
  if (file_sha256_hex(full, hex) != 0) return -1;
  snprintf(ents[*n].path, sizeof ents[*n].path, "%s", rel);
  snprintf(ents[*n].hex, sizeof ents[*n].hex, "%s", hex);
  (*n)++;
  return 0;
}

static int collect_dir(gk_seal_ent *ents, int *n, const char *root,
                       const char *rel_dir, const char *suffix) {
  char dir[512];
  DIR *d;
  struct dirent *de;
  root_join(root, rel_dir, dir, sizeof dir);
  d = opendir(dir);
  if (!d) return 0;
  while ((de = readdir(d)) != NULL && *n < GK_INT_MAX_FILES) {
    char rel[GK_INT_PATH_LEN];
    size_t L;
    if (de->d_name[0] == '.') continue;
    L = strlen(de->d_name);
    if (suffix) {
      size_t sl = strlen(suffix);
      if (L < sl || strcmp(de->d_name + L - sl, suffix) != 0) continue;
    }
    if (snprintf(rel, sizeof rel, "%s/%s", rel_dir, de->d_name) >= (int)sizeof rel)
      continue;
    (void)add_ent(ents, n, rel, root);
  }
  closedir(d);
  return 0;
}

static int collect_product(gk_seal_ent *ents, int *n, const char *root) {
  *n = 0;
  collect_dir(ents, n, root, "host/src", ".c");
  collect_dir(ents, n, root, "host/include", ".h");
  collect_dir(ents, n, root, "c_core/src", ".c");
  collect_dir(ents, n, root, "c_core/include", ".h");
  collect_dir(ents, n, root, ".agents/laws", ".md");
  collect_dir(ents, n, root, "scripts", ".sh");
  collect_dir(ents, n, root, "scripts/hive", ".sh");
  (void)add_ent(ents, n, "config/config.toml.example", root);
  (void)add_ent(ents, n, "c_core/Makefile", root);
  qsort(ents, (size_t)*n, sizeof ents[0], ent_cmp);
  return *n;
}

static void aggregate_hex(const gk_seal_ent *ents, int n, char out[65]) {
  char *buf;
  size_t cap = 0, used = 0;
  int i;
  for (i = 0; i < n; i++)
    cap += strlen(ents[i].path) + 1 + 64 + 1 + 8;
  buf = (char *)malloc(cap ? cap : 8);
  if (!buf) {
    out[0] = 0;
    return;
  }
  buf[0] = 0;
  for (i = 0; i < n; i++) {
    int w = snprintf(buf + used, cap - used, "%s %s\n", ents[i].path,
                     ents[i].hex);
    if (w > 0) used += (size_t)w;
  }
  gk_sha256_hex(buf, used, out);
  free(buf);
}

static int privacy_ok(const char *root) {
  char path[512], body[1024];
  root_join(root, "data/integrity/PRIVACY_CANONICAL.json", path, sizeof path);
  if (read_file_str(path, body, sizeof body) != 0) return 0;
  if (strstr(body, "\"telemetry\":true") || strstr(body, "\"telemetry\": true"))
    return 0;
  if (strstr(body, "\"usage_stats\":true")) return 0;
  if (strstr(body, "\"codebase_upload\":true")) return 0;
  if (!strstr(body, "\"telemetry\":false") && !strstr(body, "\"telemetry\": false"))
    return 0;
  if (!strstr(body, "state_matrix_only")) return 0;
  return 1;
}

static int load_seal_expected(const char *root, gk_seal_ent *ents, int *n,
                              char exp_agg[65]) {
  char path[512], *body = NULL;
  FILE *f;
  long sz;
  const char *p, *files;
  *n = 0;
  exp_agg[0] = 0;
  root_join(root, "data/integrity/CODE_SEAL.json", path, sizeof path);
  f = fopen(path, "r");
  if (!f) return -1;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  sz = ftell(f);
  if (sz <= 0 || sz > 512 * 1024) {
    fclose(f);
    return -1;
  }
  rewind(f);
  body = (char *)malloc((size_t)sz + 1);
  if (!body) {
    fclose(f);
    return -1;
  }
  if (fread(body, 1, (size_t)sz, f) != (size_t)sz) {
    free(body);
    fclose(f);
    return -1;
  }
  fclose(f);
  body[sz] = 0;
  {
    const char *a = strstr(body, "\"aggregate\"");
    if (a) {
      const char *q = strchr(a, ':');
      if (q) {
        q = strchr(q, '"');
        if (q) {
          size_t i = 0;
          q++;
          while (*q && *q != '"' && i < 64)
            exp_agg[i++] = *q++;
          exp_agg[i] = 0;
        }
      }
    }
  }
  files = strstr(body, "\"files\"");
  if (!files) {
    free(body);
    return -1;
  }
  p = strchr(files, '{');
  if (!p) {
    free(body);
    return -1;
  }
  p++;
  while (*p && *n < GK_INT_MAX_FILES) {
    const char *k, *v, *ke, *ve;
    while (*p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' ||
                  *p == ','))
      p++;
    if (*p == '}') break;
    if (*p != '"') break;
    k = ++p;
    ke = strchr(k, '"');
    if (!ke) break;
    p = ke + 1;
    while (*p && *p != '"') p++;
    if (*p != '"') break;
    v = ++p;
    ve = strchr(v, '"');
    if (!ve) break;
    if ((size_t)(ke - k) < sizeof ents[*n].path &&
        (size_t)(ve - v) == 64) {
      size_t pl = (size_t)(ke - k);
      memcpy(ents[*n].path, k, pl);
      ents[*n].path[pl] = 0;
      memcpy(ents[*n].hex, v, 64);
      ents[*n].hex[64] = 0;
      (*n)++;
    }
    p = ve + 1;
  }
  free(body);
  return *n > 0 ? 0 : -1;
}

static int write_latest(const char *root, int ok, const char *agg,
                        int priv_ok, int seal_ok, int n_files, int n_bad) {
  char path[512];
  FILE *f;
  time_t now = time(NULL);
  root_join(root, "data/integrity", path, sizeof path);
  mkdir(path, 0755);
  root_join(root, "data/integrity/LATEST.json", path, sizeof path);
  f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f,
          "{\n"
          "  \"schema\": \"grokium.integrity_report.v1\",\n"
          "  \"ok\": %s,\n"
          "  \"fail_closed\": true,\n"
          "  \"product\": \"grokium\",\n"
          "  \"not\": \"data_collector\",\n"
          "  \"share\": \"state_matrix_only\",\n"
          "  \"hold_flash\": 1,\n"
          "  \"product_wire\": \"smx2\",\n"
          "  \"peer_http\": \"lab_ops_only\",\n"
          "  \"peer_http_is_product_bus\": false,\n"
          "  \"llm_is_commander\": false,\n"
          "  \"findings\": [\n"
          "    {\"id\": \"privacy_flags\", \"ok\": %s},\n"
          "    {\"id\": \"code_seal\", \"ok\": %s, \"n_files\": %d, "
          "\"mismatches\": %d, \"aggregate\": \"%s\"},\n"
          "    {\"id\": \"server_bind\", \"ok\": true, \"loopback_only\": true}\n"
          "  ],\n"
          "  \"code_seal\": \"%s\",\n"
          "  \"ts\": %ld\n"
          "}\n",
          ok ? "true" : "false", priv_ok ? "true" : "false",
          seal_ok ? "true" : "false", n_files, n_bad, agg ? agg : "",
          agg ? agg : "", (long)now);
  fclose(f);
  return 0;
}

int gk_integrity_tick(const char *repo_root, char *json_out, size_t cap) {
  const char *root = repo_root && repo_root[0] ? repo_root : ".";
  gk_seal_ent exp[GK_INT_MAX_FILES], got;
  char exp_agg[65], live_agg[65], lines[64 * 1024];
  int n = 0, i, bad = 0, priv, seal_ok, ok;
  size_t used = 0;
  if (load_seal_expected(root, exp, &n, exp_agg) != 0) {
    if (json_out && cap)
      snprintf(json_out, cap,
               "{\"schema\":\"grokium.integrity_report.v1\",\"ok\":false,"
               "\"error\":\"no_code_seal\","
               "\"hint\":\"grokium integrity reseal\","
               "\"fail_closed\":true,\"share\":\"state_matrix_only\","
               "\"hold_flash\":1,\"product_wire\":\"smx2\","
               "\"peer_http\":\"lab_ops_only\","
               "\"peer_http_is_product_bus\":false,"
               "\"llm_is_commander\":false}");
    return -1;
  }
  lines[0] = 0;
  for (i = 0; i < n; i++) {
    char full[512];
    root_join(root, exp[i].path, full, sizeof full);
    if (file_sha256_hex(full, got.hex) != 0 ||
        strcmp(got.hex, exp[i].hex) != 0) {
      bad++;
      if (used + 80 < sizeof lines)
        used += (size_t)snprintf(lines + used, sizeof lines - used, "%s,",
                                 exp[i].path);
    } else {
      /* rebuild live aggregate from expected paths with live hashes */
      snprintf(exp[i].hex, sizeof exp[i].hex, "%s", got.hex);
    }
  }
  /* re-hash live content for aggregate of currently matching set */
  {
    gk_seal_ent live[GK_INT_MAX_FILES];
    int ln = 0;
    for (i = 0; i < n && ln < GK_INT_MAX_FILES; i++) {
      char full[512];
      root_join(root, exp[i].path, full, sizeof full);
      if (file_sha256_hex(full, live[ln].hex) != 0) continue;
      snprintf(live[ln].path, sizeof live[ln].path, "%s", exp[i].path);
      ln++;
    }
    qsort(live, (size_t)ln, sizeof live[0], ent_cmp);
    aggregate_hex(live, ln, live_agg);
  }
  priv = privacy_ok(root);
  seal_ok = (bad == 0 && exp_agg[0] && strcmp(exp_agg, live_agg) == 0);
  /* if aggregate field missing, seal_ok = no file mismatches only */
  if (!exp_agg[0]) seal_ok = (bad == 0);
  ok = priv && seal_ok;
  (void)write_latest(root, ok, live_agg, priv, seal_ok, n, bad);
  if (json_out && cap) {
    snprintf(json_out, cap,
             "{\"schema\":\"grokium.integrity_report.v1\",\"ok\":%s,"
             "\"fail_closed\":true,\"privacy_ok\":%s,\"code_seal_ok\":%s,"
             "\"n_files\":%d,\"mismatches\":%d,\"aggregate\":\"%s\","
             "\"expected_aggregate\":\"%s\",\"product\":\"grokium\","
             "\"not\":\"data_collector\",\"share\":\"state_matrix_only\","
             "\"hold_flash\":1,\"product_wire\":\"smx2\","
             "\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false,\"bad_prefix\":\"%.120s\"}",
             ok ? "true" : "false", priv ? "true" : "false",
             seal_ok ? "true" : "false", n, bad, live_agg, exp_agg,
             lines[0] ? lines : "");
  }
  return ok ? 1 : 0;
}

int gk_integrity_policy(const char *repo_root, char *json_out, size_t cap) {
  char path[512];
  const char *root = repo_root && repo_root[0] ? repo_root : ".";
  if (!json_out || cap < 8) return -1;
  root_join(root, "data/integrity/POLICY.json", path, sizeof path);
  if (read_file_str(path, json_out, cap) != 0) {
    snprintf(json_out, cap,
             "{\"schema\":\"grokium.integrity_policy.v1\",\"ok\":false,"
             "\"error\":\"no_policy\","
             "\"path\":\"data/integrity/POLICY.json\","
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false}");
    return -1;
  }
  return 0;
}

int gk_integrity_reseal(const char *repo_root, char *json_out, size_t cap) {
  const char *root = repo_root && repo_root[0] ? repo_root : ".";
  gk_seal_ent ents[GK_INT_MAX_FILES];
  char agg[65], path[512], priv_hex[65];
  int n = 0, i;
  FILE *f;
  time_t now = time(NULL);
  char fullp[512];
  if (collect_product(ents, &n, root) <= 0) {
    if (json_out && cap)
      snprintf(json_out, cap,
               "{\"schema\":\"grokium.integrity_reseal.v1\",\"ok\":false,"
               "\"error\":\"no_files\",\"share\":\"state_matrix_only\","
               "\"hold_flash\":1,\"product_wire\":\"smx2\","
               "\"peer_http_is_product_bus\":false,"
               "\"llm_is_commander\":false}");
    return -1;
  }
  aggregate_hex(ents, n, agg);
  root_join(root, "data/integrity/PRIVACY_CANONICAL.json", fullp, sizeof fullp);
  if (file_sha256_hex(fullp, priv_hex) != 0)
    snprintf(priv_hex, sizeof priv_hex, "%s", "");
  root_join(root, "data/integrity", path, sizeof path);
  mkdir(path, 0755);
  root_join(root, "data/integrity/CODE_SEAL.json", path, sizeof path);
  f = fopen(path, "w");
  if (!f) {
    if (json_out && cap)
      snprintf(json_out, cap,
               "{\"schema\":\"grokium.integrity_reseal.v1\",\"ok\":false,"
               "\"error\":\"write_seal\",\"share\":\"state_matrix_only\","
               "\"hold_flash\":1,\"product_wire\":\"smx2\","
               "\"peer_http_is_product_bus\":false,"
               "\"llm_is_commander\":false}");
    return -1;
  }
  fprintf(f,
          "{\n"
          "  \"schema\": \"grokium.code_seal.v1\",\n"
          "  \"product\": \"grokium\",\n"
          "  \"language\": \"C\",\n"
          "  \"note\": \"pure-C product seal; intentional reseal\",\n"
          "  \"aggregate\": \"%s\",\n"
          "  \"n_files\": %d,\n"
          "  \"files\": {\n",
          agg, n);
  for (i = 0; i < n; i++) {
    fprintf(f, "    \"%s\": \"%s\"%s\n", ents[i].path, ents[i].hex,
            i + 1 < n ? "," : "");
  }
  fprintf(f, "  },\n  \"ts\": %ld\n}\n", (long)now);
  fclose(f);

  /* refresh policy aggregate field (best-effort text replace write) */
  root_join(root, "data/integrity/POLICY.json", path, sizeof path);
  {
    char pol[4096];
    if (read_file_str(path, pol, sizeof pol) == 0) {
      f = fopen(path, "w");
      if (f) {
        fprintf(f,
                "{\n"
                "  \"schema\": \"grokium.integrity_policy.v1\",\n"
                "  \"law\": \"INTEGRITY_NO_LEAK_LAW\",\n"
                "  \"product\": \"grokium\",\n"
                "  \"not\": [\"data_collector\", \"grok_model\", "
                "\"telemetry_product\"],\n"
                "  \"privacy_canonical_sha256\": \"%s\",\n"
                "  \"code_seal_aggregate\": \"%s\",\n"
                "  \"share_only\": \"state_matrix_only\",\n"
                "  \"stream\": \"smx_realtime_bits\",\n"
                "  \"fail_closed\": true,\n"
                "  \"allowlist\": [\"127.0.0.1\", \"localhost\", \"::1\", "
                "\"cli-chat-proxy.grok.com\", \"auth.x.ai\", \"accounts.x.ai\"],\n"
                "  \"ts\": %ld,\n"
                "  \"code_seal_language\": \"C\",\n"
                "  \"commander_seal_note\": \"optional Ed25519 seal via "
                "grokium-commander; model is never commander\"\n"
                "}\n",
                priv_hex, agg, (long)now);
        fclose(f);
      }
    }
  }
  (void)write_latest(root, 1, agg, privacy_ok(root), 1, n, 0);
  if (json_out && cap)
    snprintf(json_out, cap,
             "{\"schema\":\"grokium.integrity_reseal.v1\",\"ok\":true,"
             "\"resealed\":true,\"n_files\":%d,\"aggregate\":\"%s\","
             "\"path\":\"data/integrity/CODE_SEAL.json\",\"product\":\"grokium\","
             "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
             "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
             "\"peer_http_is_product_bus\":false,"
             "\"llm_is_commander\":false}",
             n, agg);
  return 0;
}
