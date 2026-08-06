#define _POSIX_C_SOURCE 200809L
#include "grokium_media.h"
#include "grokium.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef GKX_MEDIA_NO_NANOBOT
#include "agent.h"
#include "auth.h"
#include "util.h"
#endif

extern char state_dir[];

unsigned char *gkx_file_read_raw(const char *path, size_t *out_len) {
  if (out_len) *out_len = 0;
  if (!path || !path[0]) return NULL;
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long sz = ftell(f);
  if (sz < 0 || sz > 64 * 1024 * 1024) { /* 64MB hard cap */
    fclose(f);
    return NULL;
  }
  rewind(f);
  unsigned char *buf = malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t n = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[n] = 0;
  if (out_len) *out_len = n;
  return buf;
}

const char *gkx_mime_guess(const char *path) {
  const char *dot = path ? strrchr(path, '.') : NULL;
  if (!dot) return "application/octet-stream";
  dot++;
  char e[16];
  size_t i;
  for (i = 0; i < sizeof e - 1 && dot[i]; i++)
    e[i] = (char)tolower((unsigned char)dot[i]);
  e[i] = 0;
  if (!strcmp(e, "png")) return "image/png";
  if (!strcmp(e, "jpg") || !strcmp(e, "jpeg")) return "image/jpeg";
  if (!strcmp(e, "gif")) return "image/gif";
  if (!strcmp(e, "webp")) return "image/webp";
  if (!strcmp(e, "bmp")) return "image/bmp";
  if (!strcmp(e, "svg")) return "image/svg+xml";
  /* Product path is pure C (py=0); do not special-case .py as text product. */
  if (!strcmp(e, "txt") || !strcmp(e, "md") || !strcmp(e, "c") || !strcmp(e, "h") ||
      !strcmp(e, "rs") || !strcmp(e, "go") || !strcmp(e, "json") ||
      !strcmp(e, "toml") || !strcmp(e, "yml") || !strcmp(e, "yaml") || !strcmp(e, "sh") ||
      !strcmp(e, "css") || !strcmp(e, "html") || !strcmp(e, "xml") || !strcmp(e, "csv") ||
      !strcmp(e, "log") || !strcmp(e, "cmake") || !strcmp(e, "mk"))
    return "text/plain";
  if (!strcmp(e, "pdf")) return "application/pdf";
  if (!strcmp(e, "gltf") || !strcmp(e, "glb")) return "model/gltf-binary";
  if (!strcmp(e, "obj") || !strcmp(e, "stl") || !strcmp(e, "ply")) return "model/mesh";
  if (!strcmp(e, "mp4") || !strcmp(e, "webm") || !strcmp(e, "mkv")) return "video/mp4";
  if (!strcmp(e, "png.b64")) return "image/png";
  return "application/octet-stream";
}

int gkx_path_is_image(const char *path) {
  const char *m = gkx_mime_guess(path);
  return m && !strncmp(m, "image/", 6);
}

char *gkx_b64_encode(const unsigned char *data, size_t n) {
  static const char T[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t out_n = 4 * ((n + 2) / 3);
  char *o = malloc(out_n + 1);
  size_t i, j = 0;
  if (!o) return NULL;
  for (i = 0; i < n; i += 3) {
    unsigned int v = data[i] << 16;
    if (i + 1 < n) v |= data[i + 1] << 8;
    if (i + 2 < n) v |= data[i + 2];
    o[j++] = T[(v >> 18) & 63];
    o[j++] = T[(v >> 12) & 63];
    o[j++] = (i + 1 < n) ? T[(v >> 6) & 63] : '=';
    o[j++] = (i + 2 < n) ? T[v & 63] : '=';
  }
  o[j] = 0;
  return o;
}

/* Short machine error token for plates (drop free-text / path injection). */
static void err_token(const char *in, char *out, size_t cap) {
  size_t i, o = 0;
  if (!out || cap < 2) return;
  out[0] = 0;
  if (!in || !in[0]) return;
  for (i = 0; in[i] && o + 1 < cap && o < 48; i++) {
    unsigned char c = (unsigned char)in[i];
    if (isalnum(c) || c == '_' || c == '-')
      out[o++] = (char)c;
    else if (c == ' ' || c == ':' || c == '/')
      out[o++] = '_';
  }
  out[o] = 0;
}

int gkx_media_plate_json(const char *path, int ok, const char *error,
                         size_t size_bytes, char *out, size_t cap) {
  const char *mime;
  char err[64];
  int is_img;
  if (!out || cap < 64) return -1;
  mime = (path && path[0]) ? gkx_mime_guess(path) : "application/octet-stream";
  is_img = (path && path[0]) ? gkx_path_is_image(path) : 0;
  err_token(error, err, sizeof err);
  /* Lab/ops media plate: never carries image bytes; product bus remains SMX2. */
  snprintf(out, cap,
           "{\"schema\":\"grokium.media.v1\",\"ok\":%s,"
           "\"content\":\"meta_only\","
           "\"path_is_image\":%s,\"mime\":\"%s\","
           "\"size_bytes\":%zu,"
           "\"error\":\"%s\","
           "\"vision\":\"lab_ops_only\","
           "\"product_wire\":\"smx2\",\"peer_http\":\"lab_ops_only\","
           "\"peer_http_is_product_bus\":false,"
           "\"share\":\"state_matrix_only\",\"hold_flash\":1,"
           "\"llm_is_commander\":false,\"tools\":false,\"python\":0}",
           ok ? "true" : "false", is_img ? "true" : "false", mime, size_bytes,
           err[0] ? err : "");
  return 0;
}

#ifndef GKX_MEDIA_NO_NANOBOT
int gkx_chat_vision(const gkx_config *cfg, const char *prompt,
                    const char *image_path, char *out_reply, size_t reply_n,
                    char *out_err, size_t err_n) {
  if (out_reply && reply_n) out_reply[0] = 0;
  if (out_err && err_n) out_err[0] = 0;
  if (!cfg || !image_path) {
    if (out_err) snprintf(out_err, err_n, "need_image_path");
    return 2;
  }
  size_t raw_n = 0;
  unsigned char *raw = gkx_file_read_raw(image_path, &raw_n);
  if (!raw || !raw_n) {
    if (out_err) snprintf(out_err, err_n, "cannot_read_image");
    free(raw);
    return 2;
  }
  if (raw_n > 4 * 1024 * 1024) {
    free(raw);
    if (out_err) snprintf(out_err, err_n, "image_too_large");
    return 2;
  }
  char *b64 = gkx_b64_encode(raw, raw_n);
  free(raw);
  if (!b64) {
    if (out_err) snprintf(out_err, err_n, "oom_base64");
    return 2;
  }

  char home[PATH_MAX];
  snprintf(home, sizeof home, "%s/nanobot_home", state_dir);
  mkdir(home, 0700);
  setenv("NANOBOT_HOME", home, 1);
  /* Bind nanobot workdir — env alone leaves default /tmp/nanobot. */
  ng_set_workdir(home);
  setenv("NANOBOT_SHOW_THINKING", cfg->ui_show_thinking ? "1" : "0", 1);
  setenv("NANOBOT_TOOLS", "0", 1); /* vision turn: no tools */

  ng_limits_init();
  ng_session s;
  ng_session_init(&s);
  ng_agent_cfg c;
  ng_agent_cfg_init(&c);
  c.session = &s;
  char model[GKX_MODEL_MAX];
  if (strcmp(cfg->active_backend, "grok") == 0) {
    ng_session_try_import_grok_cli(&s);
    ng_session_load(&s);
    ng_agent_set_grok_backend(&c, cfg->grok_model);
  } else {
    snprintf(model, sizeof model, "%s",
             cfg->active_model[0] ? cfg->active_model : cfg->local_model);
    ng_agent_set_local_backend(&c, cfg->local_base_url, model);
  }

  const char *mime = gkx_mime_guess(image_path);
  const char *p = prompt && prompt[0] ? prompt : "Describe this image.";
  char *reply = ng_agent_run_vision(&c, p, b64, mime, 0, NULL, NULL);
  free(b64);
  int rc = 2;
  if (reply && reply[0]) {
    if (out_reply && reply_n) snprintf(out_reply, reply_n, "%s", reply);
    rc = 0;
  } else if (out_err)
    snprintf(out_err, err_n, "vision_empty_or_failed");
  free(reply);
  ng_agent_cfg_free(&c);
  ng_session_free(&s);
  return rc;
}
#else
int gkx_chat_vision(const gkx_config *cfg, const char *prompt,
                    const char *image_path, char *out_reply, size_t reply_n,
                    char *out_err, size_t err_n) {
  (void)cfg;
  (void)prompt;
  (void)image_path;
  if (out_reply && reply_n) out_reply[0] = 0;
  if (out_err && err_n) snprintf(out_err, err_n, "vision_not_linked");
  return 2;
}
#endif

char *gkx_viz_term2d_bars(const double *ys, int n, int width, int height) {
  if (!ys || n < 1) return strdup("(no data)");
  if (width < 8) width = 8;
  if (width > 120) width = 120;
  if (height < 4) height = 4;
  if (height > 40) height = 40;
  double lo = ys[0], hi = ys[0];
  int i, r, c;
  for (i = 1; i < n; i++) {
    if (ys[i] < lo) lo = ys[i];
    if (ys[i] > hi) hi = ys[i];
  }
  if (hi <= lo) hi = lo + 1.0;
  size_t cap = (size_t)(height + 2) * (size_t)(width + 4);
  char *out = malloc(cap);
  size_t o = 0;
  if (!out) return NULL;
  o += (size_t)snprintf(out + o, cap - o, "term2d  n=%d  [%.3g .. %.3g]\n", n, lo,
                        hi);
  for (r = height - 1; r >= 0; r--) {
    double thr = lo + (hi - lo) * ((double)r / (double)(height - 1));
    if (o + (size_t)width + 4 >= cap) break;
    out[o++] = '|';
    for (c = 0; c < width; c++) {
      int idx = (int)((double)c / (double)(width - 1) * (n - 1));
      if (idx < 0) idx = 0;
      if (idx >= n) idx = n - 1;
      out[o++] = (ys[idx] >= thr) ? '#' : ' ';
    }
    out[o++] = '\n';
    out[o] = 0;
  }
  for (c = 0; c < width + 1 && o + 2 < cap; c++) out[o++] = '-';
  out[o++] = '\n';
  out[o] = 0;
  return out;
}

int gkx_viz_open(const gkx_config *cfg, const char *path, int vr) {
  if (!path || !path[0]) return -1;
  const char *tmpl = NULL;
  if (vr) {
    tmpl = cfg && cfg->viz_vr_cmd[0] ? cfg->viz_vr_cmd : NULL;
    if (!tmpl || !tmpl[0])
      tmpl = cfg && cfg->viz_desktop_cmd[0] ? cfg->viz_desktop_cmd
                                            : "xdg-open %s";
  } else {
    tmpl = cfg && cfg->viz_desktop_cmd[0] ? cfg->viz_desktop_cmd : "xdg-open %s";
  }
  char cmd[PATH_MAX * 2];
  /* Replace first %s with path (quoted). */
  {
    const char *pct = strstr(tmpl, "%s");
    if (pct) {
      size_t pre = (size_t)(pct - tmpl);
      snprintf(cmd, sizeof cmd, "%.*s'%s'%s", (int)pre, tmpl, path, pct + 2);
    } else {
      snprintf(cmd, sizeof cmd, "%s '%s'", tmpl, path);
    }
  }
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    int dev = open("/dev/null", 1);
    if (dev >= 0) {
      dup2(dev, 1);
      dup2(dev, 2);
      close(dev);
    }
    setsid();
    execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
    _exit(127);
  }
  /* don't wait — desktop/VR viewer independent */
  return 0;
}
