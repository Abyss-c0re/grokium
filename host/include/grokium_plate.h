/* SPDX-License-Identifier: Apache-2.0
 * TUI tool-capture line filter — dual-wire plates stay visible.
 * Free-form JSON dumps stay hidden unless debug mode.
 * No nanobot / ncurses dependency.
 */
#ifndef GROKIUM_PLATE_H
#define GROKIUM_PLATE_H

#include <stddef.h>

/*
 * 1 if ln is a dual-wire machine plate: JSON object starting with '{' and
 * carrying "schema":"grokium.*". Used by TUI log_add_block.
 */
int gkx_is_grokium_plate_line(const char *ln);

/*
 * 1 if log_add_block should keep the line.
 * Non-JSON lines always keep; free-form '{' dumps drop unless debug_mode;
 * grokium.* dual-wire plates always keep.
 */
int gkx_log_block_keep_line(const char *ln, int debug_mode);

/*
 * Apply the same line filter as TUI log_add_block to a multi-line block.
 * Writes kept lines to out (newline-separated, NUL-terminated).
 * Returns number of lines kept, or -1 on bad args / overflow.
 */
int gkx_filter_tool_block(const char *text, int debug_mode, char *out,
                          size_t cap);

#endif
