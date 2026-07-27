# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Lightweight markdown → styled line segments for TUI (curses).

Not a full MD engine — renders what matters in a terminal:
  # headers, **bold**, `code`, ``` fences, - lists, > quotes
"""

from __future__ import annotations

import re
from typing import Iterator

# styles: normal | bold | dim | header | code | quote | list
Seg = tuple[str, str]  # (style, text)


def render_markdown_lines(text: str, width: int = 80) -> list[Seg]:
    """Expand markdown text to (style, line) rows ready for curses."""
    if not text:
        return [("dim", "")]
    lines_out: list[Seg] = []
    in_fence = False
    fence_lang = ""
    for raw in text.splitlines():
        if raw.strip().startswith("```"):
            if not in_fence:
                in_fence = True
                fence_lang = raw.strip()[3:].strip()
                lines_out.append(("dim", f"┌── {fence_lang or 'code'} "))
            else:
                in_fence = False
                lines_out.append(("dim", "└──"))
            continue
        if in_fence:
            for chunk in _wrap(raw, width - 2):
                lines_out.append(("code", "│ " + chunk))
            continue

        s = raw
        # headers
        m = re.match(r"^(#{1,6})\s+(.*)$", s)
        if m:
            level = len(m.group(1))
            title = m.group(2)
            prefix = "#" * level + " "
            for i, chunk in enumerate(_wrap(title, width - len(prefix))):
                lines_out.append(("header", (prefix if i == 0 else " " * len(prefix)) + chunk))
            continue
        # quote
        if s.lstrip().startswith(">"):
            body = s.lstrip()[1:].lstrip()
            for chunk in _wrap(body, width - 2):
                lines_out.append(("quote", "│ " + chunk))
            continue
        # list
        if re.match(r"^\s*[-*+]\s+", s) or re.match(r"^\s*\d+\.\s+", s):
            body = re.sub(r"^\s*[-*+]\s+", "• ", s)
            body = re.sub(r"^\s*\d+\.\s+", lambda m: f"{m.group(0).strip()} ", body)
            for i, chunk in enumerate(_wrap(body, width - 2)):
                lines_out.append(("list", chunk if i == 0 else "  " + chunk))
            continue
        # empty
        if not s.strip():
            lines_out.append(("normal", ""))
            continue
        # inline bold/code → approximate: strip markers but mark bold lines if whole-line bold
        plain = _inline_strip(s)
        style = "bold" if re.match(r"^\*\*.*\*\*$", s.strip()) or re.match(r"^__.*__$", s.strip()) else "normal"
        for chunk in _wrap(plain, width):
            lines_out.append((style, chunk))
    return lines_out or [("normal", "")]


def _inline_strip(s: str) -> str:
    s = re.sub(r"\*\*(.+?)\*\*", r"\1", s)
    s = re.sub(r"__(.+?)__", r"\1", s)
    s = re.sub(r"`([^`]+)`", r"‹\1›", s)
    s = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", s)
    return s


def _wrap(s: str, width: int) -> list[str]:
    if width < 8:
        width = 8
    if not s:
        return [""]
    out = []
    while len(s) > width:
        cut = s.rfind(" ", 0, width)
        if cut < width // 2:
            cut = width
        out.append(s[:cut])
        s = s[cut:].lstrip()
    out.append(s)
    return out
