/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "grokium_fleet.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  gk_fleet F;
  const char *path = "data/home/FLEET.json";
  if (argc < 2) {
    fprintf(stderr,
            "grokium-fleet defaults|deploy|save [path]|status\n"
            "  defaults — print roles including nb-manager\n"
            "  deploy   — mkdir homes under data/home\n"
            "  save     — write FLEET.json plate\n");
    return 2;
  }
  fleet_default_roles(&F);
  if (!strcmp(argv[1], "defaults")) {
    int i;
    for (i = 0; i < F.n; i++)
      printf("%s\t%s\t%d\t%s\n", F.bots[i].id, F.bots[i].purpose,
             F.bots[i].port, F.bots[i].home);
    return 0;
  }
  if (!strcmp(argv[1], "deploy")) {
    fleet_deploy(&F);
    if (argc > 2) path = argv[2];
    fleet_save(&F, path);
    printf("{\"ok\":true,\"deployed\":%d,\"path\":\"%s\",\"nb_manager\":true}\n",
           F.n, path);
    return 0;
  }
  if (!strcmp(argv[1], "save")) {
    if (argc > 2) path = argv[2];
    fleet_deploy(&F);
    fleet_save(&F, path);
    printf("{\"ok\":true,\"saved\":\"%s\",\"n\":%d}\n", path, F.n);
    return 0;
  }
  if (!strcmp(argv[1], "status")) {
    printf("{\"alive\":%d,\"n\":%d,\"nb_manager\":true}\n", fleet_status(&F),
           F.n);
    return 0;
  }
  return 2;
}
