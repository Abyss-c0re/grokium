# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Markdown → TUI segments (clean headers, fences, lists)."""

from __future__ import annotations

import re

Seg = tuple[str, str]  # style, text


def render_markdown_lines(text: str, width: int = 80) -> list[Seg]:
    if not text:
        return [("dim", "")]
    # normalize common model glitches
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    text = re.sub(r"(?m)^[ \t]*\|[ \t]*", "", text)  # strip table pipes used as rules
    text = re.sub(r"(?m)^[-=]{3,}\s*$", "", text)

    lines_out: list[Seg] = []
    in_fence = False
    fence_lang = ""

    for raw in text.splitlines():
        stripped = raw.strip()
        # fence open/close
        if stripped.startswith("```"):
            if not in_fence:
                in_fence = True
                fence_lang = stripped[3:].strip() or "code"
                lines_out.append(("dim", f"  ┌ {fence_lang}"))
            else:
                in_fence = False
                lines_out.append(("dim", "  └"))
            continue
        if in_fence:
            for chunk in _wrap(raw, max(8, width - 4)):
                lines_out.append(("code", "  │ " + chunk))
            continue

        # skip empty
        if not stripped:
            lines_out.append(("normal", ""))
            continue

        # headers — show clean title, not raw ###
        m = re.match(r"^(#{1,6})\s+(.*)$", stripped)
        if m:
            level = len(m.group(1))
            title = _inline(m.group(2))
            mark = "▸" if level <= 2 else "•"
            for i, chunk in enumerate(_wrap(f"{mark} {title}", width)):
                lines_out.append(("header", chunk))
            continue

        # quotes
        if stripped.startswith(">"):
            body = _inline(stripped.lstrip(">").strip())
            for chunk in _wrap(body, width - 2):
                lines_out.append(("quote", "│ " + chunk))
            continue

        # lists
        if re.match(r"^[-*+]\s+", stripped) or re.match(r"^\d+\.\s+", stripped):
            body = re.sub(r"^[-*+]\s+", "• ", stripped)
            body = re.sub(r"^(\d+)\.\s+", r"\1. ", body)
            body = _inline(body)
            for i, chunk in enumerate(_wrap(body, width - 2)):
                lines_out.append(("list", chunk if i == 0 else "  " + chunk))
            continue

        # hr junk from models
        if re.match(r"^[\-\*_]{3,}$", stripped):
            continue

        plain = _inline(stripped)
        style = "bold" if (stripped.startswith("**") and stripped.endswith("**")) else "normal"
        for chunk in _wrap(plain, width):
            lines_out.append((style, chunk))

    return lines_out or [("normal", "")]


def _inline(s: str) -> str:
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
