/* SPDX-License-Identifier: Apache-2.0 */
#ifndef GROKIUM_ALGOCUBE_H
#define GROKIUM_ALGOCUBE_H
#include "grokium_smx.h"
/* Mathematical digit 0-9 from matrix + salt (law enforce, not LLM) */
int algocube_digit(const grokium_smx *m, const char *salt);
void algocube_blueprint10(const grokium_smx *m, uint8_t dig[10]);
#endif
