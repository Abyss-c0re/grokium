# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""StateMatrix binary pack — 512 bits, no personal data."""

from __future__ import annotations

import hashlib
from pathlib import Path

CELLS = 512


def clean_bits(bits: str, n: int = CELLS) -> str:
    b = "".join(c for c in (bits or "") if c in "01")
    if len(b) < n:
        b = (b + "0" * n)[:n]
    return b[:n]


def bits_to_bytes(bits: str) -> bytes:
    b = clean_bits(bits)
    out = bytearray()
    for i in range(0, CELLS, 8):
        byte = 0
        for j in range(8):
            if b[i + j] == "1":
                byte |= 1 << (7 - j)
        out.append(byte)
    return bytes(out)


def bytes_to_bits(data: bytes) -> str:
    bits = []
    for byte in data[: CELLS // 8]:
        for j in range(8):
            bits.append("1" if (byte >> (7 - j)) & 1 else "0")
    return clean_bits("".join(bits))


def write_smx(path: Path | str, bits: str) -> Path:
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    raw = bits_to_bytes(bits)
    p.write_bytes(raw)
    # sidecar hash only (no prose)
    (p.with_suffix(p.suffix + ".sha256")).write_text(
        hashlib.sha256(raw).hexdigest() + "\n", encoding="utf-8"
    )
    return p


def read_smx(path: Path | str) -> str:
    p = Path(path)
    if not p.is_file():
        return "0" * CELLS
    data = p.read_bytes()
    if len(data) < CELLS // 8:
        data = data + b"\x00" * (CELLS // 8 - len(data))
    return bytes_to_bits(data)


def text_to_smx_bits(text: str, *, salt: str = "nanobrain") -> str:
    """Fold non-personal structural text into bits (hash only — never store raw text in SMX)."""
    # reject if looks like email/ssn-ish — still only hash, never write text to matrix file
    h = hashlib.sha512((salt + "|" + (text or "")).encode()).digest()
    bits = []
    for byte in h:
        for j in range(8):
            bits.append("1" if (byte >> (7 - j)) & 1 else "0")
    # extend to 512
    while len(bits) < CELLS:
        h = hashlib.sha512(bytes(int("".join(bits[-64:]), 2).to_bytes(8, "big") if len(bits) >= 64 else h)).digest()
        for byte in h:
            for j in range(8):
                bits.append("1" if (byte >> (7 - j)) & 1 else "0")
            if len(bits) >= CELLS:
                break
    return "".join(bits[:CELLS])
