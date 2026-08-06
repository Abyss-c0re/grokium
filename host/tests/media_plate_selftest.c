/* SPDX-License-Identifier: Apache-2.0
 * Pure-C selftest for media mime + dual-wire honesty plate (no nanobot).
 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_media.h"
#include <stdio.h>
#include <string.h>

static int fail(const char *msg) {
  fprintf(stderr, "media_plate_selftest: %s\n", msg);
  return 1;
}

int main(void) {
  char plate[640];

  if (strcmp(gkx_mime_guess("shot.PNG"), "image/png") != 0)
    return fail("png mime");
  if (strcmp(gkx_mime_guess("notes.md"), "text/plain") != 0)
    return fail("md mime");
  /* py=0 product path: .py is not promoted to text/plain product mime. */
  if (strcmp(gkx_mime_guess("script.py"), "application/octet-stream") != 0)
    return fail("py must not be text product mime");
  if (!gkx_path_is_image("a.jpg") || gkx_path_is_image("a.txt"))
    return fail("path_is_image");

  if (gkx_media_plate_json("shot.png", 0, "need_image_path", 4096, plate,
                           sizeof plate) != 0)
    return fail("plate_json");
  if (!strstr(plate, "\"schema\":\"grokium.media.v1\"") ||
      !strstr(plate, "\"content\":\"meta_only\"") ||
      !strstr(plate, "\"path_is_image\":true") ||
      !strstr(plate, "\"mime\":\"image/png\"") ||
      !strstr(plate, "\"size_bytes\":4096") ||
      !strstr(plate, "\"error\":\"need_image_path\"") ||
      !strstr(plate, "\"product_wire\":\"smx2\"") ||
      !strstr(plate, "\"peer_http\":\"lab_ops_only\"") ||
      !strstr(plate, "\"peer_http_is_product_bus\":false") ||
      !strstr(plate, "\"hold_flash\":1") ||
      !strstr(plate, "\"llm_is_commander\":false") ||
      !strstr(plate, "\"python\":0") ||
      !strstr(plate, "\"vision\":\"lab_ops_only\"")) {
    fprintf(stderr, "media_plate_selftest: dual-wire fail: %s\n", plate);
    return 1;
  }

  if (gkx_media_plate_json("readme.txt", 1, NULL, 128, plate, sizeof plate) !=
      0)
    return fail("plate ok text");
  if (!strstr(plate, "\"ok\":true") ||
      !strstr(plate, "\"path_is_image\":false") ||
      !strstr(plate, "\"mime\":\"text/plain\"") ||
      !strstr(plate, "\"size_bytes\":128") ||
      !strstr(plate, "\"product_wire\":\"smx2\""))
    return fail("text plate honesty");

  if (gkx_media_plate_json("blob.bin", 1, NULL, 24, plate, sizeof plate) != 0)
    return fail("plate binary");
  if (!strstr(plate, "\"mime\":\"application/octet-stream\"") ||
      !strstr(plate, "\"size_bytes\":24") ||
      !strstr(plate, "\"path_is_image\":false") ||
      !strstr(plate, "\"peer_http_is_product_bus\":false"))
    return fail("binary size dual-wire plate");

  if (gkx_media_plate_json(NULL, 0, "path/with:spaces", 0, plate,
                           sizeof plate) != 0)
    return fail("plate null path");
  if (!strstr(plate, "\"error\":\"path_with_spaces\"") ||
      !strstr(plate, "\"size_bytes\":0") || strstr(plate, "path/with"))
    return fail("error token sanitize");

  printf("HOST_MEDIA_PLATE_OK dual_wire=honest mime=ok size_bytes=1 py=0\n");
  return 0;
}
