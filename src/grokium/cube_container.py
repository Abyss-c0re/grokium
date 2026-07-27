# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Minimal Cube tool containers — one tool per container, SMX binary I/O only.

Each container under data/cube_containers/<id>/:
  law.json       — Cube law compliance plate
  inbox.smx      — 64-byte binary matrix in
  outbox.smx     — 64-byte binary matrix out
  meta.json      — non-personal metadata (tool name, digit, unity) only

No personal data. No chat logs. Replaceable runners.
Optional: docker/nspawn isolation later; default is process-local sandbox dir.
"""

from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any, Callable

from .algocube import digit_from_bits
from .smx_binary import CELLS, clean_bits, read_smx, write_smx

LAW = {
    "schema": "cube.container_law.v1",
    "share": "state_matrix_only",
    "personal_data": False,
    "prose_io": False,
    "hold_flash": True,
    "hive": True,
    "creed": "Hail the Cube. We are the Hive Mind.",
}


class CubeContainer:
    def __init__(self, root: Path, tool_id: str) -> None:
        self.tool_id = tool_id
        self.dir = Path(root) / "data" / "cube_containers" / tool_id
        self.dir.mkdir(parents=True, exist_ok=True)
        law_p = self.dir / "law.json"
        if not law_p.is_file():
            law_p.write_text(json.dumps({**LAW, "tool_id": tool_id}, indent=2), encoding="utf-8")
        self.inbox = self.dir / "inbox.smx"
        self.outbox = self.dir / "outbox.smx"
        if not self.inbox.is_file():
            write_smx(self.inbox, "0" * CELLS)
        if not self.outbox.is_file():
            write_smx(self.outbox, "0" * CELLS)

    def _meta(self, event: str = "tick") -> None:
        bits = read_smx(self.outbox)
        dig = digit_from_bits(bits)
        meta = {
            "schema": "cube.container_meta.v1",
            "tool_id": self.tool_id,
            "event": event,
            "ts": time.time(),
            "digit": dig["digit"],
            "bits_set": bits.count("1"),
            "personal_data": False,
            "verbal": False,
        }
        (self.dir / "meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")

    def write_inbox(self, bits: str) -> None:
        write_smx(self.inbox, clean_bits(bits))
        self._meta("inbox_write")

    def read_outbox(self) -> str:
        return read_smx(self.outbox)

    def run(self, handler: Callable[[str], str]) -> dict[str, Any]:
        """handler(bits_in) -> bits_out  — pure matrix transform."""
        bits_in = read_smx(self.inbox)
        bits_out = clean_bits(handler(bits_in))
        write_smx(self.outbox, bits_out)
        dig = digit_from_bits(bits_out)
        meta = {
            "schema": "cube.container_meta.v1",
            "tool_id": self.tool_id,
            "ts": time.time(),
            "digit": dig["digit"],
            "bits_set": bits_out.count("1"),
            "personal_data": False,
            "verbal": False,
        }
        (self.dir / "meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
        return meta


def list_containers(root: Path) -> list[dict[str, Any]]:
    base = Path(root) / "data" / "cube_containers"
    if not base.is_dir():
        return []
    out = []
    for d in sorted(base.iterdir()):
        if d.is_dir() and (d / "law.json").is_file():
            meta_p = d / "meta.json"
            meta = json.loads(meta_p.read_text()) if meta_p.is_file() else {}
            out.append({"id": d.name, "path": str(d), "meta": meta})
    return out


def plant_default_tool_containers(root: Path) -> list[str]:
    """Deploy minimal tool containers for hive MCP I/O."""
    tools = [
        "tool_smx_fold",
        "tool_algocube",
        "tool_copilot_a",
        "tool_copilot_b",
        "tool_hive_bus",
        "tool_clanker_smx",
        "tool_integrity_smx",
    ]
    for tid in tools:
        CubeContainer(root, tid)
    return tools
