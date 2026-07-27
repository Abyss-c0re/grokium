# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Algocube — non-verbal law digits + matrix compare (replaceable).

Hot path: bits only. No personal data. Responsible for copilot fork harmony.
Swap implementation via GROKIUM_ALGOCUBE=module.path:ClassName
"""

from __future__ import annotations

import hashlib
import importlib
import json
import os
from pathlib import Path
from typing import Any, Protocol

CELLS = 512


class AlgocubeEngine(Protocol):
    def digit_from_bits(self, bits: str, blueprint: dict[str, Any] | None = None) -> dict[str, Any]: ...
    def compare(self, bits_a: str, bits_b: str, *, salt: str = "") -> dict[str, Any]: ...
    def harmony(self, matrices: list[str], *, salt: str = "") -> dict[str, Any]: ...


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


def _clean_bits(bits: str, n: int = CELLS) -> str:
    b = "".join(c for c in (bits or "") if c in "01")
    if len(b) < n:
        b = (b + "0" * n)[:n]
    return b[:n]


def _hamming(a: str, b: str) -> int:
    a, b = _clean_bits(a), _clean_bits(b)
    return sum(x != y for x, y in zip(a, b))


class DefaultAlgocube:
    """Default mathematical algocube (replaceable)."""

    name = "default_algocube"

    def digit_from_bits(self, bits: str, blueprint: dict[str, Any] | None = None) -> dict[str, Any]:
        bp = blueprint or {}
        salt = str(bp.get("seed_salt") or "grokium")
        lo, hi = 0, 9
        dr = bp.get("digit_range") or [0, 9]
        if isinstance(dr, (list, tuple)) and len(dr) == 2:
            lo, hi = int(dr[0]), int(dr[1])
        span = max(1, hi - lo + 1)
        material = (salt + "|" + _clean_bits(bits)).encode()
        h = hashlib.sha256(material).digest()
        n = int.from_bytes(h[:8], "big")
        digit = lo + (n % span)
        return {
            "schema": "cube.algocube.v1",
            "engine": self.name,
            "digit": digit,
            "range": [lo, hi],
            "law": "algocube_enforces_law",
            "hold_flash": bool(bp.get("hold_flash", True)),
            "bits_len": CELLS,
            "verbal": False,
            "personal_data": False,
        }

    def compare(self, bits_a: str, bits_b: str, *, salt: str = "") -> dict[str, Any]:
        a, b = _clean_bits(bits_a), _clean_bits(bits_b)
        dist = _hamming(a, b)
        agree = CELLS - dist
        unity = agree / CELLS
        # XOR matrix for hive exchange (still bits only)
        xbits = "".join("1" if x != y else "0" for x, y in zip(a, b))
        and_bits = "".join("1" if x == "1" and y == "1" else "0" for x, y in zip(a, b))
        dig = self.digit_from_bits(xbits, {"seed_salt": salt or "algocube-compare"})
        return {
            "schema": "cube.algocube.compare.v1",
            "engine": self.name,
            "cells": CELLS,
            "hamming": dist,
            "agree": agree,
            "unity": round(unity, 6),
            "digit": dig["digit"],
            "xor_bits": xbits,
            "and_bits": and_bits,
            "verbal": False,
            "personal_data": False,
            "law": "state_matrix_share_only",
        }

    def harmony(self, matrices: list[str], *, salt: str = "") -> dict[str, Any]:
        mats = [_clean_bits(m) for m in matrices if m]
        if not mats:
            return {"ok": False, "error": "no_matrices", "unity": 0.0}
        if len(mats) == 1:
            dig = self.digit_from_bits(mats[0], {"seed_salt": salt or "harmony"})
            return {
                "ok": True,
                "schema": "cube.algocube.harmony.v1",
                "n": 1,
                "unity": 1.0,
                "digit": dig["digit"],
                "consensus_bits": mats[0],
                "verbal": False,
                "personal_data": False,
            }
        # pairwise mean unity
        unities = []
        for i in range(len(mats)):
            for j in range(i + 1, len(mats)):
                unities.append(self.compare(mats[i], mats[j], salt=salt)["unity"])
        # majority vote per cell
        cons = []
        for k in range(CELLS):
            ones = sum(1 for m in mats if m[k] == "1")
            cons.append("1" if ones * 2 >= len(mats) else "0")
        cbits = "".join(cons)
        dig = self.digit_from_bits(cbits, {"seed_salt": salt or "harmony"})
        return {
            "ok": True,
            "schema": "cube.algocube.harmony.v1",
            "engine": self.name,
            "n": len(mats),
            "unity": round(sum(unities) / len(unities), 6) if unities else 1.0,
            "digit": dig["digit"],
            "consensus_bits": cbits,
            "verbal": False,
            "personal_data": False,
            "law": "hive_seek_unity",
        }


_ENGINE: AlgocubeEngine | None = None


def get_engine() -> AlgocubeEngine:
    global _ENGINE
    if _ENGINE is not None:
        return _ENGINE
    spec = (os.environ.get("GROKIUM_ALGOCUBE") or "").strip()
    if spec:
        # module.path:ClassName
        mod_name, _, cls_name = spec.partition(":")
        mod = importlib.import_module(mod_name)
        _ENGINE = getattr(mod, cls_name)()
    else:
        _ENGINE = DefaultAlgocube()
    return _ENGINE


def set_engine(engine: AlgocubeEngine) -> None:
    global _ENGINE
    _ENGINE = engine


def digit_from_bits(bits: str, blueprint: dict[str, Any] | None = None) -> dict[str, Any]:
    return get_engine().digit_from_bits(bits, blueprint)


def evaluate(bits: str, blueprint_path: str | Path) -> dict[str, Any]:
    bp = load_blueprint(blueprint_path)
    out = digit_from_bits(bits, bp)
    out["blueprint_schema"] = bp.get("schema")
    return out


def compare_matrices(bits_a: str, bits_b: str, *, salt: str = "") -> dict[str, Any]:
    return get_engine().compare(bits_a, bits_b, salt=salt)


def hive_harmony(matrices: list[str], *, salt: str = "") -> dict[str, Any]:
    return get_engine().harmony(matrices, salt=salt)
