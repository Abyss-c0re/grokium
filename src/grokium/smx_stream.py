# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Real-time StateMatrix streaming — bits only, never prose / sessions."""

from __future__ import annotations

import hashlib
import json
import os
import threading
import time
from pathlib import Path
from typing import Any, Callable, Iterator

from .matrix import fold_bits, parse_plate

CELLS = 512


class SmxStreamBus:
    """In-process bus of SMX frames for realtime subscribers (loopback)."""

    def __init__(self, root: Path) -> None:
        self.root = root
        self.dir = root / "data" / "matrix" / "stream"
        self.dir.mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()
        self._seq = 0
        self._latest: dict[str, Any] | None = None
        self._subs: list[Callable[[dict[str, Any]], None]] = []

    def publish(
        self,
        *,
        source: str,
        bits: str | None = None,
        plate: dict[str, Any] | None = None,
        integrity: dict[str, Any] | None = None,
        extra_flags: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Publish one SMX frame. Rejects payloads that look like prose dumps."""
        if plate and any(
            k in plate
            for k in ("chat", "messages", "transcript", "session_text", "content", "prompt")
        ):
            raise RuntimeError("SMX stream refuses prose fields (no data collection path)")

        if bits is None:
            if plate:
                bits = fold_bits(plate, n=CELLS)
            elif integrity:
                bits = fold_bits({"integrity": integrity, "ts": time.time()}, n=CELLS)
            else:
                bits = "0" * CELLS
        # sanitize bits
        bits = "".join(c for c in bits if c in "01")
        if len(bits) < CELLS:
            bits = (bits + "0" * CELLS)[:CELLS]
        else:
            bits = bits[:CELLS]

        with self._lock:
            self._seq += 1
            seq = self._seq
            frame = {
                "schema": "grokium.smx_stream.v1",
                "seq": seq,
                "ts": time.time(),
                "source": source,
                "cells": CELLS,
                "bits": bits,
                "bits_set": bits.count("1"),
                "share": "state_matrix_only",
                "prose": False,
                "telemetry": False,
                "integrity": integrity or {},
                "flags": extra_flags or {},
                "sha256": hashlib.sha256(bits.encode()).hexdigest(),
            }
            self._latest = frame
            # ring file + latest
            (self.dir / "LATEST.smx.json").write_text(
                json.dumps(frame, indent=2), encoding="utf-8"
            )
            # append-only compact jsonl (bits only path for tailers)
            with (self.dir / "live.jsonl").open("a", encoding="utf-8") as f:
                f.write(
                    json.dumps(
                        {
                            "seq": seq,
                            "ts": frame["ts"],
                            "source": source,
                            "bits": bits,
                            "sha256": frame["sha256"],
                            "integrity_ok": (integrity or {}).get("ok"),
                        },
                        separators=(",", ":"),
                    )
                    + "\n"
                )
            for cb in list(self._subs):
                try:
                    cb(frame)
                except Exception:
                    pass
        return frame

    def latest(self) -> dict[str, Any] | None:
        with self._lock:
            return dict(self._latest) if self._latest else None

    def iter_sse(self, poll_s: float = 0.25) -> Iterator[str]:
        """Server-Sent Events: only SMX frames (no session bodies)."""
        last_seq = 0
        yield "event: hello\ndata: {\"schema\":\"grokium.smx_sse.v1\",\"share\":\"state_matrix_only\"}\n\n"
        while True:
            frame = self.latest()
            if frame and int(frame.get("seq") or 0) > last_seq:
                last_seq = int(frame["seq"])
                # strip any accidental large fields
                slim = {
                    k: frame[k]
                    for k in (
                        "schema",
                        "seq",
                        "ts",
                        "source",
                        "cells",
                        "bits",
                        "bits_set",
                        "share",
                        "prose",
                        "telemetry",
                        "sha256",
                        "integrity",
                        "flags",
                    )
                    if k in frame
                }
                yield f"event: smx\ndata: {json.dumps(slim, separators=(',', ':'))}\n\n"
            else:
                yield ": keepalive\n\n"
            time.sleep(poll_s)


_BUS: SmxStreamBus | None = None


def get_bus(root: str | Path | None = None) -> SmxStreamBus:
    global _BUS
    if _BUS is None:
        r = Path(root or os.environ.get("GROKIUM_ROOT") or Path(__file__).resolve().parents[2])
        _BUS = SmxStreamBus(r)
    return _BUS
