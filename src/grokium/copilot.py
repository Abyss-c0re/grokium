# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Copilot mode — fork two LLM angles, exchange StateMatrices, algocube compares.

No personal data on the hive bus — only hashed SMX bits.
"""

from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any

from .algocube import compare_matrices, digit_from_bits, hive_harmony
from .cube_container import CubeContainer
from .llm import chat, get_backend
from .smx_binary import text_to_smx_bits
from .smx_stream import get_bus

ANGLE_A = (
    "You are Copilot-A (Construct angle) in Grokium hive. "
    "Be concrete, action-oriented, tool-minded. Short. Markdown light. "
    "Not xAI. Never invent personal data."
)
ANGLE_B = (
    "You are Copilot-B (Deconstruct angle) in Grokium hive. "
    "Be critical, risk-aware, alternative-seeking. Short. Markdown light. "
    "Not xAI. Never invent personal data. Challenge assumptions of angle A."
)


def run_copilot(
    cfg: dict[str, Any],
    prompt: str,
    *,
    model_a: str | None = None,
    model_b: str | None = None,
    backend_a: str | None = None,
    backend_b: str | None = None,
    max_tokens: int = 400,
) -> dict[str, Any]:
    """Fork prompt into two angles; fold answers to SMX; algocube compares."""
    root = Path(cfg.get("_root") or ".")
    # sanitize: never put raw user PII into matrix files — hash fold only
    prompt_bits = text_to_smx_bits(prompt, salt="copilot-prompt")

    ca = CubeContainer(root, "tool_copilot_a")
    cb = CubeContainer(root, "tool_copilot_b")
    ca.write_inbox(prompt_bits)
    cb.write_inbox(prompt_bits)

    be_a = backend_a or get_backend(cfg)
    be_b = backend_b or get_backend(cfg)

    # Angle A
    ra = chat(
        cfg,
        [
            {"role": "system", "content": ANGLE_A},
            {"role": "user", "content": prompt},
        ],
        max_tokens=max_tokens,
        backend=be_a,
        allow_failover=True,
    )
    # Angle B — different instruction; may same model
    rb = chat(
        cfg,
        [
            {"role": "system", "content": ANGLE_B},
            {"role": "user", "content": prompt},
        ],
        max_tokens=max_tokens,
        backend=be_b,
        allow_failover=True,
    )

    text_a = (ra.get("content") or ra.get("error") or "")[:8000]
    text_b = (rb.get("content") or rb.get("error") or "")[:8000]
    # Fold responses to SMX (hash) — responses stay local for user; hive bus gets bits only
    bits_a = text_to_smx_bits(text_a, salt="copilot-a")
    bits_b = text_to_smx_bits(text_b, salt="copilot-b")

    def ha(_: str) -> str:
        return bits_a

    def hb(_: str) -> str:
        return bits_b

    meta_a = ca.run(ha)
    meta_b = cb.run(hb)

    cmp = compare_matrices(bits_a, bits_b, salt="copilot-fork")
    harm = hive_harmony([bits_a, bits_b, prompt_bits], salt="copilot-hive")

    # publish consensus to hive bus container + SMX stream
    hive = CubeContainer(root, "tool_hive_bus")
    hive.write_inbox(harm.get("consensus_bits") or bits_a)
    hive.run(lambda _: harm.get("consensus_bits") or bits_a)

    bus = get_bus(root)
    bus.publish(
        source="copilot_hive",
        bits=harm.get("consensus_bits") or bits_a,
        integrity={
            "unity": harm.get("unity"),
            "digit": harm.get("digit"),
            "compare_unity": cmp.get("unity"),
            "personal_data": False,
        },
        extra_flags={"copilot": 1, "hive": 1},
    )

    out = {
        "schema": "grokium.copilot.v1",
        "ok": bool(ra.get("ok") or rb.get("ok")),
        "angle_a": {
            "ok": ra.get("ok"),
            "backend": ra.get("path") or be_a,
            "model": ra.get("model"),
            "content": text_a,
            "smx_digit": meta_a.get("digit"),
        },
        "angle_b": {
            "ok": rb.get("ok"),
            "backend": rb.get("path") or be_b,
            "model": rb.get("model"),
            "content": text_b,
            "smx_digit": meta_b.get("digit"),
        },
        "algocube_compare": {
            "unity": cmp.get("unity"),
            "hamming": cmp.get("hamming"),
            "digit": cmp.get("digit"),
            # bits available for hive — not personal
            "xor_bits_prefix": (cmp.get("xor_bits") or "")[:64],
        },
        "hive_harmony": {
            "unity": harm.get("unity"),
            "digit": harm.get("digit"),
            "consensus_bits_prefix": (harm.get("consensus_bits") or "")[:64],
        },
        "law": {
            "share": "state_matrix_only",
            "personal_data": False,
            "prose_on_hive_bus": False,
            "creed": "Hail the Cube. We are the Hive Mind.",
        },
        "ts": time.time(),
        "telemetry": False,
    }
    # persist non-personal summary for nanobrain
    path = root / "data" / "nanobrain" / f"copilot_{int(time.time())}.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    # store angles for local user review only under nanobrain (local disk, not station share)
    path.write_text(json.dumps(out, indent=2, default=str), encoding="utf-8")
    out["saved"] = str(path)
    return out
