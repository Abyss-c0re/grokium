/* Raw file + vision + pluggable visualization (no hard-coded VR SDKs). */
#ifndef GROKIUM_MEDIA_H
#define GROKIUM_MEDIA_H

#include "grokium_config.h"
#include <stddef.h>

/* Read file as malloc'd bytes; *out_len set. Caller frees. */
unsigned char *gkx_file_read_raw(const char *path, size_t *out_len);

/* Guess mime from extension (images, text, or application/octet-stream). */
const char *gkx_mime_guess(const char *path);

/* Base64 encode (malloc). */
char *gkx_b64_encode(const unsigned char *data, size_t n);

/* Detect image by mime/path. */
int gkx_path_is_image(const char *path);

/*
 * Dual-wire honesty plate for media/vision lab path.
 * content is always meta_only (no image bytes on the plate).
 * error should be a short machine token (no free-text prose).
 * size_bytes is honest file length (0 when unknown/unread).
 */
int gkx_media_plate_json(const char *path, int ok, const char *error,
                         size_t size_bytes, char *out, size_t cap);

/* Vision chat: image path + prompt via nanobot vision path. */
int gkx_chat_vision(const gkx_config *cfg, const char *prompt,
                    const char *image_path, char *out_reply, size_t reply_n,
                    char *out_err, size_t err_n);

/* Terminal 2D: plot y values as ASCII blocks. Returns malloc text. */
char *gkx_viz_term2d_bars(const double *ys, int n, int width, int height);

/* Open path with desktop or VR viewer command from config (%s = path). */
int gkx_viz_open(const gkx_config *cfg, const char *path, int vr);

#endif
