/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GROKIUM_HTTP_H
#define GROKIUM_HTTP_H
#include "grokium_consolidator.h"
#include "grokium_fleet.h"
#include "grokium_law.h"
/* Loopback control plane :17444 — compatible JSON surface */
int grokium_serve(const char *host, int port,
                  gk_consolidator *C, gk_fleet *F, grokium_law *L,
                  const char *data_root);
#endif
