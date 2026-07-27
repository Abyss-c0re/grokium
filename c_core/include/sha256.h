/* SPDX-License-Identifier: Apache-2.0 — compact SHA-256 */
#ifndef GROKIUM_SHA256_H
#define GROKIUM_SHA256_H
#include <stddef.h>
#include <stdint.h>
void gk_sha256(const void *data, size_t len, uint8_t out[32]);
void gk_sha256_hex(const void *data, size_t len, char out_hex[65]);
#endif
