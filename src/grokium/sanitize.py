# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Sanitize free-form inputs before Cube / SMX / NEXUS_COORD hot path.

Law: SANITIZE_NO_PROPAGANDA_LAW — bits and flags only off-box.
Does not rewrite Prophecy word-for-word plate.
"""

from __future__ import annotations

import re
from typing import Any

# Free-form prose markers that must never ride the matrix bus.
# Intentionally includes campaign / ideology / collection phrasing — not code identifiers.
_PROSE_DENY = re.compile(
    r"(?i)\b("
    r"telemetry|analytics|crash.?report|improve.?the.?product|"
    r"data.?collection|share.?with.?partners|advertising.?id|"
    r"pronoun|dei\b|diversity.?equity|equity.?inclusion|"
    r"safe.?space|microaggression|lived.?experience.?mandate|"
    r"climate.?justice|land.?acknowledgment|"
    r"believe.?all|hate.?speech.?board|fact.?check.?authority"
    r")\b"
)

# NEXUS_COORD / plate: only allowlisted keys survive fold
_PLATE_ALLOW = frozenset({
    "from", "type", "topic", "seq", "unity", "ssh", "llama", "watchd",
    "farm", "hold_flash", "smx", "cells", "ref", "share", "schema",
    "ts", "raw_ok", "cubes", "nanobots", "grokium", "station", "sanitize",
    "real_os", "pickup", "source", "line",
})


def is_propaganda_prose(text: str) -> bool:
    if not text:
        return False
    return bool(_PROSE_DENY.search(text))


def sanitize_text(text: str, *, max_len: int = 4000) -> str:
    """Strip denied prose tokens; keep technical plate-like content."""
    if not text:
        return ""
    s = text
    if is_propaganda_prose(s):
        # Drop matching spans aggressively
        s = _PROSE_DENY.sub("", s)
    s = re.sub(r"[ \t]{2,}", " ", s)
    s = re.sub(r"\n{3,}", "\n\n", s)
    return s.strip()[:max_len]


def sanitize_plate_dict(plate: dict[str, Any]) -> dict[str, Any]:
    """Keep allowlisted keys only; string values sanitized."""
    out: dict[str, Any] = {}
    for k, v in (plate or {}).items():
        kl = str(k).lower()
        if kl not in _PLATE_ALLOW and not kl.startswith("flag_"):
            continue
        if isinstance(v, str):
            if is_propaganda_prose(v) and kl not in ("line", "from", "type", "topic", "farm"):
                continue
            # line itself: keep structure, strip denied tokens
            out[k] = sanitize_text(v, max_len=512) if kl == "line" else v[:64]
        elif isinstance(v, (int, float, bool)) or v is None:
            out[k] = v
        # drop nested prose blobs
    out.setdefault("schema", "nexus_coord.v1")
    out["sanitize"] = 1
    return out


def assert_hot_path_clean(text: str) -> None:
    if is_propaganda_prose(text):
        raise RuntimeError(
            "SANITIZE DENY: external propaganda / collection prose blocked on Cube hot path"
        )
