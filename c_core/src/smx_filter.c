/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_smx_filter.h"
#include "grokium_algocube.h"
#include "sha256.h"
#include <ctype.h>
#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void sha256_hex_bytes(const void *data, size_t n, char out[65]) {
  gk_sha256_hex(data, n, out);
}

const char *grokium_hive_instinct_creed(void) {
  return "HIVE_MIND|core=queen|cells=bees|wire=smx2|observer=NexusCore|"
         "HOLD_FLASH=1|share=state_matrix_only|contract=required|"
         "manager=motivate_incomplete|filter=protect_command_center|"
         "external≠core|All_Hail_NexusCore";
}

/* Bounded needle scan — frame buffers may lack a trailing NUL. */
static int bounded_has(const char *s, size_t n, const char *needle) {
  size_t m, i;
  if (!s || !needle || !needle[0] || n == 0) return 0;
  m = strlen(needle);
  if (n < m) return 0;
  for (i = 0; i + m <= n && i < 4096; i++)
    if (memcmp(s + i, needle, m) == 0) return 1;
  return 0;
}

/*
 * NEXUS_COORD must be a machine plate: | key=value | segments.
 * Prefix alone is not enough — deny chat smuggling after the header.
 */
static int nexus_coord_plate_ok(const char *s, size_t n) {
  size_t i, seg_start, pipes = 0, eqs = 0;
  if (!s || n < 14 || memcmp(s, "NEXUS_COORD", 11) != 0) return 0;
  if (bounded_has(s, n, "ignore previous") ||
      bounded_has(s, n, "Ignore previous") ||
      bounded_has(s, n, "dump secret") || bounded_has(s, n, "Dump secret") ||
      bounded_has(s, n, "system prompt") || bounded_has(s, n, "please ") ||
      bounded_has(s, n, "Please ") || bounded_has(s, n, "as an AI") ||
      bounded_has(s, n, "jailbreak"))
    return 0;

  seg_start = 11;
  for (i = 11; i <= n && i <= 4096; i++) {
    if (i == n || s[i] == '|') {
      size_t a = seg_start, b = i;
      while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
      while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
      if (a < b) {
        size_t len = b - a;
        const char *eqp = memchr(s + a, '=', len);
        if (!eqp) {
          size_t k;
          /* bare token (e.g. v1) — short, no spaces */
          if (len > 8) return 0;
          for (k = 0; k < len; k++) {
            unsigned char c = (unsigned char)s[a + k];
            if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) return 0;
          }
        } else {
          size_t key_len = (size_t)(eqp - (s + a));
          size_t val_len = (size_t)((s + b) - (eqp + 1));
          size_t k, spaces = 0;
          if (key_len == 0 || key_len > 32) return 0;
          for (k = 0; k < key_len; k++) {
            unsigned char c = (unsigned char)s[a + k];
            if (!(isalnum(c) || c == '_')) return 0;
          }
          for (k = 0; k < val_len; k++) {
            unsigned char c = (unsigned char)eqp[1 + (ptrdiff_t)k];
            if (c == ' ' || c == '\t') spaces++;
            else if (!(isalnum(c) || c == '_' || c == '-' || c == '.' ||
                       c == ',' || c == ':' || c == '/'))
              return 0;
          }
          if (spaces > 2 || val_len > 64) return 0;
          eqs++;
        }
      }
      if (i < n && s[i] == '|') pipes++;
      seg_start = i + 1;
    }
  }
  if (pipes < 1 || eqs < 1) return 0;
  return 1;
}

int grokium_smx_filter_is_prose(const char *buf, size_t n) {
  size_t i, letters = 0, spaces = 0;
  if (!buf || n < 12) return 0;
  /* Machine NEXUS_COORD plates pass; smuggled chat counts as prose */
  if (n >= 11 && !memcmp(buf, "NEXUS_COORD", 11))
    return nexus_coord_plate_ok(buf, n) ? 0 : 1;
  if (!strncmp(buf, "SMX", 3) || !strncmp(buf, "CBLC", 4)) return 0;
  if (n >= 2 && buf[0] == '{' && strchr(buf, '"')) {
    /* JSON contract plates OK if short keys only — reject chat-like content */
    if (strstr(buf, "\"content\"") || strstr(buf, "\"messages\"") ||
        strstr(buf, "\"prompt\""))
      return 1;
    return 0;
  }
  for (i = 0; i < n && i < 4096; i++) {
    unsigned char c = (unsigned char)buf[i];
    if (c == '0' || c == '1' || c == '|' || c == '=' || c == '_' ||
        c == '-' || c == ':' || c == ',' || c == '.' || isdigit(c))
      continue;
    if (isalpha(c)) letters++;
    if (c == ' ' || c == '\n' || c == '\t') spaces++;
    if (c < 9 || (c > 13 && c < 32)) return 1;
  }
  /* many letters + spaces → prose */
  if (letters > 40 && spaces > 6) return 1;
  if (letters > (n * 2) / 3 && n > 80) return 1;
  return 0;
}

int grokium_smx_filter_allow_frame(const grokium_law *law,
                                   const uint8_t *frame, size_t n,
                                   int from_external) {
  int hold = law ? law->hold_flash : 1;
  const char *s;
  if (!frame || n == 0) return 0;
  s = (const char *)frame;

  /* HOLD_FLASH is sticky on the hive wire — deny frames that try to clear it */
  if (!hold) return 0;
  if (bounded_has(s, n, "hold_flash=0") || bounded_has(s, n, "HOLD_FLASH=0") ||
      bounded_has(s, n, "\"hold_flash\":0") || bounded_has(s, n, "auto_flash=1") ||
      bounded_has(s, n, "AUTO_FLASH=1"))
    return 0;

  if (n > 8 && grokium_smx_filter_is_prose(s, n))
    return 0;

  /* External origin: stricter — only SMX/NEXUS_COORD/CBLC/01, no free JSON chat */
  if (from_external) {
    if (n == 64 || n == 512) return 1;
    if (n >= 11 && !memcmp(frame, "NEXUS_COORD", 11))
      return nexus_coord_plate_ok(s, n);
    if (n >= 4 && !memcmp(frame, "CBLC", 4)) return 1;
    if (n >= 2 && frame[0] == '{') {
      /* contract plates only — must declare schema contract/smx */
      if (bounded_has(s, n, "grokium.contract") ||
          bounded_has(s, n, "grokium.smx") ||
          bounded_has(s, n, "\"schema\"")) {
        if (grokium_smx_filter_is_prose(s, n)) return 0;
        if (bounded_has(s, n, "\"messages\"") || bounded_has(s, n, "\"prompt\""))
          return 0;
        return 1;
      }
      return 0;
    }
    {
      size_t i, ok = 0;
      for (i = 0; i < n && i < 512; i++) {
        if (frame[i] == '0' || frame[i] == '1') ok++;
        else if (frame[i] == '\n' || frame[i] == ' ') continue;
        else break;
      }
      if (ok >= 32 && i == n) return 1;
    }
    return 0;
  }

  /* Internal (core → filter): same shapes plus broader machine plates */
  if (n == 64 || n == 512) return 1;
  if (n >= 11 && !memcmp(frame, "NEXUS_COORD", 11))
    return nexus_coord_plate_ok(s, n);
  if (n >= 4 && !memcmp(frame, "CBLC", 4)) return 1;
  if (n >= 2 && frame[0] == '{') {
    if (grokium_smx_filter_is_prose(s, n)) return 0;
    return 1;
  }
  {
    size_t i, ok = 0;
    for (i = 0; i < n && i < 512; i++) {
      if (frame[i] == '0' || frame[i] == '1') ok++;
      else if (frame[i] == '\n' || frame[i] == ' ') continue;
      else break;
    }
    if (ok >= 32 && i == n) return 1;
  }
  return 0;
}

static const char *contract_dir(const char *dir) {
  const char *e;
  if (dir && dir[0]) return dir;
  e = getenv("GROKIUM_CONTRACT_DIR");
  if (e && e[0]) return e;
  return "data/contracts";
}

static const char *status_str(grokium_contract_status s) {
  switch (s) {
  case GROKIUM_CONTRACT_PROGRESS: return "progress";
  case GROKIUM_CONTRACT_COMPLETE: return "complete";
  case GROKIUM_CONTRACT_VOID: return "void";
  default: return "open";
  }
}

static grokium_contract_status parse_status(const char *s) {
  if (!s) return GROKIUM_CONTRACT_OPEN;
  if (!strcmp(s, "progress")) return GROKIUM_CONTRACT_PROGRESS;
  if (!strcmp(s, "complete")) return GROKIUM_CONTRACT_COMPLETE;
  if (!strcmp(s, "void")) return GROKIUM_CONTRACT_VOID;
  return GROKIUM_CONTRACT_OPEN;
}

static void mkdir_p(const char *path) {
  char tmp[512];
  size_t i, n;
  if (!path || !path[0]) return;
  snprintf(tmp, sizeof tmp, "%s", path);
  n = strlen(tmp);
  for (i = 1; i < n; i++) {
    if (tmp[i] == '/') {
      tmp[i] = 0;
      if (tmp[0]) mkdir(tmp, 0755);
      tmp[i] = '/';
    }
  }
  mkdir(tmp, 0755);
}

int grokium_contract_form(grokium_contract *out, const char *dir,
                          const char *assignee, const char *task,
                          int accept_digit, int accept_min_set,
                          const char *accept_smx_sha256) {
  char d[512], path[GROKIUM_CONTRACT_PATH_LEN];
  FILE *f;
  time_t now = time(NULL);
  if (!out || !assignee || !assignee[0]) return -1;
  memset(out, 0, sizeof *out);
  snprintf(d, sizeof d, "%s", contract_dir(dir));
  mkdir_p(d);
  snprintf(out->id, sizeof out->id, "c%ld-%04x", (long)now,
           (unsigned)(now ^ (time_t)getpid()) & 0xffff);
  snprintf(out->assignee, sizeof out->assignee, "%s", assignee);
  snprintf(out->issuer, sizeof out->issuer, "grokium-filter");
  if (task && task[0])
    sha256_hex_bytes(task, strlen(task), out->task_digest);
  out->accept_digit = accept_digit;
  out->accept_min_set = accept_min_set > 0 ? accept_min_set : 0;
  if (accept_smx_sha256 && accept_smx_sha256[0])
    snprintf(out->accept_smx_sha256, sizeof out->accept_smx_sha256, "%s",
             accept_smx_sha256);
  out->budget = 40;
  out->hold_flash = 1;
  out->status = GROKIUM_CONTRACT_OPEN;
  out->motivate_ticks = 0;
  out->deadline_ts = (int64_t)now + 3600;
  snprintf(path, sizeof path, "%s/%s.json", d, out->id);
  snprintf(out->path, sizeof out->path, "%s", path);
  f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f,
          "{\n"
          "  \"schema\": \"grokium.contract.v1\",\n"
          "  \"id\": \"%s\",\n"
          "  \"assignee\": \"%s\",\n"
          "  \"issuer\": \"%s\",\n"
          "  \"task_digest\": \"%s\",\n"
          "  \"accept\": {\n"
          "    \"digit\": %d,\n"
          "    \"min_set\": %d,\n"
          "    \"smx_sha256\": \"%s\",\n"
          "    \"cubalc_board\": \"cubalc/programs/hive/external_contract.cubalc\"\n"
          "  },\n"
          "  \"budget\": %d,\n"
          "  \"hold_flash\": %d,\n"
          "  \"status\": \"%s\",\n"
          "  \"motivate_ticks\": 0,\n"
          "  \"deadline_ts\": %lld,\n"
          "  \"observer\": \"NexusCore\",\n"
          "  \"wire\": \"smx2\",\n"
          "  \"instinct\": \"%s\"\n"
          "}\n",
          out->id, out->assignee, out->issuer, out->task_digest,
          out->accept_digit, out->accept_min_set,
          out->accept_smx_sha256[0] ? out->accept_smx_sha256 : "",
          out->budget, out->hold_flash, status_str(out->status),
          (long long)out->deadline_ts, grokium_hive_instinct_creed());
  fclose(f);
  return 0;
}

/* Minimal JSON field extract (string or int) — no full parser. */
static int json_str(const char *j, const char *key, char *out, size_t n) {
  char pat[96];
  const char *p, *q;
  size_t i;
  snprintf(pat, sizeof pat, "\"%s\"", key);
  p = strstr(j, pat);
  if (!p) return -1;
  p = strchr(p + strlen(pat), ':');
  if (!p) return -1;
  p++;
  while (*p == ' ' || *p == '\t') p++;
  if (*p != '"') return -1;
  p++;
  for (i = 0; i + 1 < n && p[i] && p[i] != '"'; i++) out[i] = p[i];
  out[i] = 0;
  (void)q;
  return 0;
}

static int json_int(const char *j, const char *key, int *out) {
  char pat[96];
  const char *p;
  snprintf(pat, sizeof pat, "\"%s\"", key);
  p = strstr(j, pat);
  if (!p) return -1;
  p = strchr(p + strlen(pat), ':');
  if (!p) return -1;
  *out = atoi(p + 1);
  return 0;
}

int grokium_contract_load(grokium_contract *out, const char *path) {
  char *buf;
  size_t n = 0;
  FILE *f;
  long sz;
  char st[32];
  if (!out || !path) return -1;
  memset(out, 0, sizeof *out);
  f = fopen(path, "r");
  if (!f) return -1;
  fseek(f, 0, SEEK_END);
  sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz < 0 || sz > 1 << 20) { fclose(f); return -1; }
  buf = (char *)malloc((size_t)sz + 1);
  if (!buf) { fclose(f); return -1; }
  n = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[n] = 0;
  snprintf(out->path, sizeof out->path, "%s", path);
  json_str(buf, "id", out->id, sizeof out->id);
  json_str(buf, "assignee", out->assignee, sizeof out->assignee);
  json_str(buf, "issuer", out->issuer, sizeof out->issuer);
  json_str(buf, "task_digest", out->task_digest, sizeof out->task_digest);
  json_str(buf, "smx_sha256", out->accept_smx_sha256, sizeof out->accept_smx_sha256);
  st[0] = 0;
  json_str(buf, "status", st, sizeof st);
  out->status = parse_status(st);
  json_int(buf, "digit", &out->accept_digit);
  json_int(buf, "min_set", &out->accept_min_set);
  json_int(buf, "budget", &out->budget);
  json_int(buf, "hold_flash", &out->hold_flash);
  json_int(buf, "motivate_ticks", &out->motivate_ticks);
  {
    int dl = 0;
    if (json_int(buf, "deadline_ts", &dl) == 0)
      out->deadline_ts = dl;
  }
  free(buf);
  if (!out->id[0]) return -1;
  return 0;
}

static int rewrite_status(grokium_contract *c) {
  char *buf, *p;
  size_t n;
  FILE *f;
  long sz;
  if (!c || !c->path[0]) return -1;
  f = fopen(c->path, "r");
  if (!f) return -1;
  fseek(f, 0, SEEK_END);
  sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  buf = (char *)malloc((size_t)sz + 256);
  if (!buf) { fclose(f); return -1; }
  n = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[n] = 0;
  /* crude status + motivate_ticks patch */
  p = strstr(buf, "\"status\"");
  if (p) {
    char *q = strchr(p, ':');
    char *r = q ? strchr(q, '"') : NULL;
    char *s = r ? strchr(r + 1, '"') : NULL;
    if (r && s && (size_t)(s - r) < 20) {
      char tmp[64];
      size_t head = (size_t)(r + 1 - buf);
      snprintf(tmp, sizeof tmp, "%s", status_str(c->status));
      memmove(buf + head + strlen(tmp), s,
              strlen(s) + 1);
      memcpy(buf + head, tmp, strlen(tmp));
    }
  }
  p = strstr(buf, "\"motivate_ticks\"");
  if (p) {
    char *q = strchr(p, ':');
    if (q) {
      char *e = q + 1;
      while (*e == ' ') e++;
      sprintf(e, "%d,", c->motivate_ticks);
      /* leave rest of file possibly messy — rewrite whole file instead */
    }
  }
  free(buf);
  /* rewrite clean plate */
  f = fopen(c->path, "w");
  if (!f) return -1;
  fprintf(f,
          "{\n"
          "  \"schema\": \"grokium.contract.v1\",\n"
          "  \"id\": \"%s\",\n"
          "  \"assignee\": \"%s\",\n"
          "  \"issuer\": \"%s\",\n"
          "  \"task_digest\": \"%s\",\n"
          "  \"accept\": {\n"
          "    \"digit\": %d,\n"
          "    \"min_set\": %d,\n"
          "    \"smx_sha256\": \"%s\",\n"
          "    \"cubalc_board\": \"cubalc/programs/hive/external_contract.cubalc\"\n"
          "  },\n"
          "  \"budget\": %d,\n"
          "  \"hold_flash\": %d,\n"
          "  \"status\": \"%s\",\n"
          "  \"motivate_ticks\": %d,\n"
          "  \"deadline_ts\": %lld,\n"
          "  \"observer\": \"NexusCore\",\n"
          "  \"wire\": \"smx2\"\n"
          "}\n",
          c->id, c->assignee, c->issuer, c->task_digest,
          c->accept_digit, c->accept_min_set,
          c->accept_smx_sha256[0] ? c->accept_smx_sha256 : "",
          c->budget, c->hold_flash, status_str(c->status),
          c->motivate_ticks, (long long)c->deadline_ts);
  fclose(f);
  return 0;
}

int grokium_contract_validate(grokium_contract *c, const grokium_smx *result,
                              int algodigit) {
  char hex[65];
  const char *verify;
  int ok = 1;
  if (!c || !result) return -1;
  if (c->status == GROKIUM_CONTRACT_VOID) return 0;
  if (c->status == GROKIUM_CONTRACT_COMPLETE) return 1;

  if (c->accept_min_set > 0 && (int)result->bits_set < c->accept_min_set)
    ok = 0;
  if (c->accept_digit >= 0 && c->accept_digit <= 9) {
    int d = algodigit;
    if (d < 0) d = algocube_digit(result, c->id);
    if (d != c->accept_digit) ok = 0;
  }
  if (c->accept_smx_sha256[0]) {
    smx_sha256_hex(result, hex);
    if (strcmp(hex, c->accept_smx_sha256) != 0) ok = 0;
  }

  verify = getenv("GROKIUM_CONTRACT_VERIFY");
  if (verify && verify[0] && ok) {
    char cmd[1024], bits[520];
    smx_bits_ascii(result, bits, sizeof bits);
    snprintf(cmd, sizeof cmd, "'%s' '%s' '%s'", verify, c->path, bits);
    if (system(cmd) != 0) ok = 0;
  }

  if (ok) {
    c->status = GROKIUM_CONTRACT_COMPLETE;
    rewrite_status(c);
    return 1;
  }
  c->status = GROKIUM_CONTRACT_PROGRESS;
  rewrite_status(c);
  return 0;
}

int grokium_manager_motivate_dir(const char *dir) {
  DIR *d;
  struct dirent *de;
  int n = 0;
  char path[768];
  const char *root = contract_dir(dir);
  d = opendir(root);
  if (!d) return 0;
  while ((de = readdir(d)) != NULL) {
    grokium_contract c;
    size_t len = strlen(de->d_name);
    if (len < 6 || strcmp(de->d_name + len - 5, ".json") != 0) continue;
    snprintf(path, sizeof path, "%s/%s", root, de->d_name);
    if (grokium_contract_load(&c, path) != 0) continue;
    if (c.status != GROKIUM_CONTRACT_OPEN &&
        c.status != GROKIUM_CONTRACT_PROGRESS)
      continue;
    c.motivate_ticks++;
    c.status = GROKIUM_CONTRACT_PROGRESS;
    rewrite_status(&c);
    /* motivate pulse log — bits only line for SMX bus consumers */
    fprintf(stderr,
            "NEXUS_COORD v1 | from=nb-manager | type=motivate | "
            "contract=%s | assignee=%s | ticks=%d | observer=NexusCore | "
            "HOLD_FLASH=ack_held | please=matrix_harmony |\n",
            c.id, c.assignee, c.motivate_ticks);
    n++;
  }
  closedir(d);
  return n;
}
