# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Nanobrain core — harmonic hive mind. SMX I/O. Cube law. MCP-extensible tools.

Hail the Cube. We are the Hive Mind.
"""

from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any, Callable

from ..algocube import digit_from_bits, hive_harmony
from ..cube_container import CubeContainer, list_containers, plant_default_tool_containers
from ..matrix import fold_bits, parse_plate, plate_ack, save_matrix
from ..smx_binary import CELLS, clean_bits, text_to_smx_bits
from ..smx_stream import get_bus

CREED = "Hail the Cube. We are the Hive Mind."


class Nanobrain:
    def __init__(self, root: Path | str) -> None:
        self.root = Path(root)
        self.home = self.root / "data" / "nanobrain"
        self.home.mkdir(parents=True, exist_ok=True)
        self.tools: dict[str, Callable[[str], str]] = {}
        self._register_builtin_tools()
        plant_default_tool_containers(self.root)
        (self.home / "LAW.json").write_text(
            json.dumps(
                {
                    "schema": "nanobrain.law.v1",
                    "creed": CREED,
                    "share": "state_matrix_only",
                    "personal_data": False,
                    "prose_between_tools": False,
                    "hold_flash": True,
                    "algocube_replaceable": True,
                    "mcp_io": True,
                },
                indent=2,
            ),
            encoding="utf-8",
        )

    def _register_builtin_tools(self) -> None:
        def fold_tool(bits: str) -> str:
            return clean_bits(bits)

        def algo_tool(bits: str) -> str:
            dig = digit_from_bits(bits)
            # encode digit into first 4 bits of return matrix
            d = int(dig["digit"]) & 0xF
            head = format(d, "04b")
            return clean_bits(head + bits[4:])

        def hive_tool(bits: str) -> str:
            harm = hive_harmony([bits, read_bus_bits(self.root)], salt="nanobrain")
            return clean_bits(harm.get("consensus_bits") or bits)

        self.register_tool("tool_smx_fold", fold_tool)
        self.register_tool("tool_algocube", algo_tool)
        self.register_tool("tool_hive_bus", hive_tool)

    def register_tool(self, tool_id: str, handler: Callable[[str], str]) -> None:
        """Extend hive: each tool = cube container, SMX in/out only."""
        CubeContainer(self.root, tool_id)
        self.tools[tool_id] = handler

    def invoke_tool_smx(self, tool_id: str, bits_in: str) -> dict[str, Any]:
        if tool_id not in self.tools:
            return {"ok": False, "error": f"unknown_tool:{tool_id}"}
        c = CubeContainer(self.root, tool_id)
        c.write_inbox(bits_in)
        meta = c.run(self.tools[tool_id])
        out = c.read_outbox()
        return {
            "ok": True,
            "tool_id": tool_id,
            "bits_out": out,
            "bits_out_prefix": out[:64],
            "meta": meta,
            "personal_data": False,
            "verbal": False,
        }

    def pulse(self, *, note_bits: str | None = None) -> dict[str, Any]:
        """Hive pulse — gather tool outboxes, algocube harmony, publish SMX."""
        matrices = []
        for item in list_containers(self.root):
            c = CubeContainer(self.root, item["id"])
            matrices.append(c.read_outbox())
        if note_bits:
            matrices.append(clean_bits(note_bits))
        harm = hive_harmony(matrices or ["0" * CELLS], salt="nanobrain-pulse")
        bus = get_bus(self.root)
        fr = bus.publish(
            source="nanobrain",
            bits=harm.get("consensus_bits") or "0" * CELLS,
            integrity={
                "unity": harm.get("unity"),
                "digit": harm.get("digit"),
                "hive": True,
                "personal_data": False,
            },
            extra_flags={"nanobrain": 1, "creed": "hive"},
        )
        status = {
            "schema": "nanobrain.pulse.v1",
            "ok": True,
            "creed": CREED,
            "tools": list(self.tools.keys()),
            "containers": list_containers(self.root),
            "harmony": {
                "unity": harm.get("unity"),
                "digit": harm.get("digit"),
                "n": harm.get("n"),
            },
            "smx_seq": fr.get("seq"),
            "personal_data": False,
            "ts": time.time(),
        }
        (self.home / "LATEST_PULSE.json").write_text(
            json.dumps(status, indent=2, default=str), encoding="utf-8"
        )
        return status

    def status(self) -> dict[str, Any]:
        return {
            "schema": "nanobrain.status.v1",
            "planted": True,
            "creed": CREED,
            "home": str(self.home),
            "tools": list(self.tools.keys()),
            "containers": list_containers(self.root),
            "law": "state_matrix_only",
            "personal_data": False,
            "affiliated_with_xai": False,
        }


def read_bus_bits(root: Path) -> str:
    c = CubeContainer(root, "tool_hive_bus")
    return c.read_outbox()


_NB: Nanobrain | None = None


def get_nanobrain(root: Path | str | None = None) -> Nanobrain:
    global _NB
    if _NB is None:
        from ..config import load

        r = Path(root or load().get("_root") or ".")
        _NB = Nanobrain(r)
    return _NB


def deploy_nanobrain(root: Path | str | None = None) -> dict[str, Any]:
    nb = get_nanobrain(root)
    pulse = nb.pulse()
    return {
        "ok": True,
        "deployed": True,
        "creed": CREED,
        "status": nb.status(),
        "pulse": pulse,
        "mcp": "grokium_nanobrain_* tools",
        "law": "Hail the Cube. SMX only between tools.",
    }
