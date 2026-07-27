# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Algocube — mathematical digit 0–9 from StateMatrix bits + LAW_BLUEPRINT.

Non-verbal. Not an LLM. Enforces Cube law on the hot path.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any


def load_blueprint(path: str | Path) -> dict[str, Any]:
    p = Path(path)
    if not p.is_file():
        return {
            "schema": "cube.law_blueprint.v1",
            "digit_range": [0, 9],
            "seed_salt": "grokium-default",
            "hold_flash": True,
        }
    return json.loads(p.read_text(encoding="utf-8"))


def digit_from_bits(bits: str, blueprint: dict[str, Any] | None = None) -> dict[str, Any]:
    bp = blueprint or {}
    salt = str(bp.get("seed_salt") or "grokium")
    lo, hi = 0, 9
    dr = bp.get("digit_range") or [0, 9]
    if isinstance(dr, (list, tuple)) and len(dr) == 2:
        lo, hi = int(dr[0]), int(dr[1])
    span = max(1, hi - lo + 1)
    # fold bits + salt → uniform digit (math only)
    material = (salt + "|" + (bits or "")).encode()
    h = hashlib.sha256(material).digest()
    n = int.from_bytes(h[:8], "big")
    digit = lo + (n % span)
    return {
        "schema": "cube.algocube.v1",
        "digit": digit,
        "range": [lo, hi],
        "law": "algocube_enforces_law",
        "hold_flash": bool(bp.get("hold_flash", True)),
        "bits_len": len(bits or ""),
        "verbal": False,
    }


def evaluate(bits: str, blueprint_path: str | Path) -> dict[str, Any]:
    bp = load_blueprint(blueprint_path)
    out = digit_from_bits(bits, bp)
    out["blueprint_schema"] = bp.get("schema")
    return out
