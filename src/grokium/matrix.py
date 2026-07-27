# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""NEXUS_COORD state-matrix share only — Not For Human Eyes prose dumps.

External exchange with the station is bits/flags plates, never session text.
"""

from __future__ import annotations

import hashlib
import json
import re
import time
from pathlib import Path
from typing import Any

# Plate tokens we accept / emit (no free text payloads off-box)
FLAG_KEYS = (
    "unity",
    "ssh",
    "llama",
    "watchd",
    "farm",
    "hold_flash",
    "seq",
    "type",
    "topic",
    "from",
    "smx",
    "cells",
)


_PLATE_RE = re.compile(
    r"NEXUS_COORD\s+v1\s*\|\s*(.+)$",
    re.IGNORECASE,
)


def parse_plate(line: str) -> dict[str, Any]:
    """Parse NEXUS_COORD v1 | k=v | ... into a dict (state matrix share only).

    Accepts station wrappers like:
      NEXUS_COORD from station: NEXUS_COORD v1 | from=BlackCube | ...
    """
    line = (line or "").strip()
    # strip common station / KDE share prefixes (not part of plate keys)
    for pref in (
        "NEXUS_COORD from station:",
        "Shared text:",
        "sent → Shared text:",
    ):
        if line.lower().startswith(pref.lower()):
            line = line[len(pref) :].strip()
            break
    # if prefix embedded mid-string, search for the plate
    m = _PLATE_RE.search(line)
    if m:
        # re-anchor body to full match start so we don't keep junk prefix keys
        start = m.start()
        line = line[start:]
        m = _PLATE_RE.search(line)
    body = m.group(1) if m else line
    out: dict[str, Any] = {"schema": "nexus_coord.v1", "raw_ok": bool(m)}
    for part in body.split("|"):
        part = part.strip()
        if not part or "=" not in part:
            # bare flag / phrase tokens (e.g. "local first")
            if part:
                key = re.sub(r"[^a-z0-9_]+", "_", part.lower()).strip("_")
                if key:
                    out[key] = 1
            continue
        k, _, v = part.partition("=")
        k = k.strip().lower()
        v = v.strip()
        if v in ("1", "0") and k not in ("seq",):
            out[k] = int(v)
        else:
            try:
                if "." in v:
                    out[k] = float(v)
                else:
                    out[k] = int(v)
            except ValueError:
                out[k] = v
    out["ts"] = time.time()
    try:
        from .sanitize import sanitize_plate_dict
        out = sanitize_plate_dict(out)
    except Exception:
        pass
    return out


def fold_bits(plate: dict[str, Any], n: int = 512) -> str:
    """Deterministic bitstring from plate keys (wireless StateMatrix fold)."""
    payload = json.dumps(
        {k: plate.get(k) for k in sorted(plate) if k not in ("ts", "raw_ok", "schema")},
        sort_keys=True,
        default=str,
    ).encode()
    digest = hashlib.sha512(payload).digest()
    # extend if needed
    bits = []
    while len(bits) < n:
        for b in digest:
            for i in range(8):
                bits.append("1" if (b >> (7 - i)) & 1 else "0")
                if len(bits) >= n:
                    break
            if len(bits) >= n:
                break
        digest = hashlib.sha512(digest).digest()
    return "".join(bits[:n])


def plate_ack(
    *,
    from_id: str = "Grokium",
    ref_seq: Any = None,
    unity: float = 1.0,
    llama: int = 0,
    ssh: int = 0,
    watchd: int = 0,
    farm: str = "standby",
    hold_flash: int = 1,
    extra: dict[str, Any] | None = None,
) -> str:
    seq = int(time.time())
    parts = [
        "NEXUS_COORD v1",
        f"from={from_id}",
        "type=heartbeat",
        "topic=channel_stim",
        f"seq={seq}",
    ]
    if ref_seq is not None:
        parts.append(f"ref={ref_seq}")
    parts.extend(
        [
            f"unity={unity}",
            f"ssh={ssh}",
            f"llama={llama}",
            f"watchd={watchd}",
            f"farm={farm}",
            f"hold_flash={hold_flash}",
            "share=state_matrix_only",
        ]
    )
    if extra:
        for k, v in extra.items():
            if k.startswith("_"):
                continue
            # never attach free-form human prose
            if isinstance(v, (int, float)) or (isinstance(v, str) and len(v) < 48 and " " not in v):
                parts.append(f"{k}={v}")
    return " | ".join(parts) + " |"


def save_matrix(root: Path, plate: dict[str, Any]) -> Path:
    d = root / "data" / "matrix"
    d.mkdir(parents=True, exist_ok=True)
    bits = fold_bits(plate)
    rec = {
        "schema": "grokium.smx.v1",
        "law": "state_matrix_share_only",
        "plate": {k: v for k, v in plate.items() if k != "raw_ok"},
        "cells": 512,
        "sot_bits": bits,
        "ts": time.time(),
    }
    path = d / f"smx_{int(rec['ts'])}.json"
    path.write_text(json.dumps(rec, indent=2), encoding="utf-8")
    (d / "LATEST.json").write_text(json.dumps(rec, indent=2), encoding="utf-8")
    return path
