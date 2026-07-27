# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Station NEXUS_COORD → Grokium StateMatrix + SMX (bits only).

BlackCube stim writes prophecy_cube/state/nexus_coord.json.
Grokium watches that path and folds plates — no session prose.
"""

from __future__ import annotations

import json
import threading
import time
from pathlib import Path
from typing import Any

from .llm import probe_local
from .matrix import fold_bits, parse_plate, plate_ack, save_matrix
from .smx_stream import get_bus

# Default station SoT plate path (stim_once.sh / cube_daemon)
_DEFAULT_STATION_PATHS = (
    "/home/voldemar/Dev/lab/prophecy_cube/state/nexus_coord.json",
    "/home/voldemar/Dev/agent_ops/coord/pve/LATEST_BLACKCUBE_COMPAT.json",
)

_watch_lock = threading.Lock()
_watch_thread: threading.Thread | None = None
_last_seq: dict[str, Any] = {"seq": None, "path": None}


def station_coord_paths(cfg: dict[str, Any] | None = None) -> list[Path]:
    cfg = cfg or {}
    cube = cfg.get("cube") or {}
    paths: list[Path] = []
    for key in ("station_coord", "nexus_coord_path"):
        p = cube.get(key)
        if p:
            paths.append(Path(p))
    for p in _DEFAULT_STATION_PATHS:
        paths.append(Path(p))
    # unique preserve order
    seen: set[str] = set()
    out: list[Path] = []
    for p in paths:
        s = str(p)
        if s not in seen:
            seen.add(s)
            out.append(p)
    return out


def _plate_from_station_file(path: Path) -> tuple[dict[str, Any] | None, str | None]:
    if not path.is_file():
        return None, None
    try:
        raw = path.read_text(encoding="utf-8", errors="replace")
        data = json.loads(raw)
    except (OSError, json.JSONDecodeError):
        return None, None
    if not isinstance(data, dict):
        return None, None

    line = data.get("line") or data.get("body") or ""
    if not line and data.get("schema") in ("NEXUS_COORD.v1", "nexus_coord.v1"):
        # rebuild plate from fields
        parts = ["NEXUS_COORD v1"]
        for k in (
            "from",
            "type",
            "topic",
            "seq",
            "unity",
            "ssh",
            "llama",
            "watchd",
            "farm",
            "hold_flash",
        ):
            if k in data and data[k] is not None:
                parts.append(f"{k}={data[k]}")
        line = " | ".join(parts) + " |"

    if not line:
        return None, None

    plate = parse_plate(str(line))
    # prefer structured fields when present
    for k in (
        "from",
        "type",
        "topic",
        "seq",
        "unity",
        "ssh",
        "llama",
        "watchd",
        "farm",
        "hold_flash",
        "cubes",
        "nanobots",
    ):
        if k in data and data[k] is not None:
            plate[k] = data[k]
    plate["station_path"] = str(path)
    return plate, str(line)


def ingest_station_coord(
    cfg: dict[str, Any],
    *,
    force: bool = False,
    path: Path | str | None = None,
) -> dict[str, Any]:
    """Ingest latest station NEXUS_COORD if seq advanced (or force)."""
    root = Path(cfg.get("_root") or Path(__file__).resolve().parents[2])
    candidates = [Path(path)] if path else station_coord_paths(cfg)

    plate: dict[str, Any] | None = None
    line: str | None = None
    used: Path | None = None
    for p in candidates:
        plate, line = _plate_from_station_file(p)
        if plate:
            used = p
            break
    if not plate or not used:
        return {
            "ok": False,
            "error": "no_station_coord",
            "paths": [str(p) for p in candidates],
            "telemetry": False,
        }

    seq = plate.get("seq")
    with _watch_lock:
        if not force and seq is not None and seq == _last_seq.get("seq") and str(used) == _last_seq.get("path"):
            return {
                "ok": True,
                "skipped": True,
                "reason": "seq_unchanged",
                "seq": seq,
                "path": str(used),
                "telemetry": False,
            }
        _last_seq["seq"] = seq
        _last_seq["path"] = str(used)

    # strip non-matrix noise before fold/save
    clean = {k: v for k, v in plate.items() if k not in ("raw_ok",)}
    try:
        from .sanitize import sanitize_plate_dict
        clean = sanitize_plate_dict(clean)
    except Exception:
        pass
    for bad in ("chat", "messages", "transcript", "session_text", "content", "prompt", "hive_talk_snip"):
        clean.pop(bad, None)

    path_saved = save_matrix(root, clean)
    bits = fold_bits(clean)
    bus = get_bus(root)
    # SMX plate: flags only — drop paths / free text
    smx_plate = {
        k: v
        for k, v in clean.items()
        if k
        not in (
            "station_path",
            "ts",
            "sot_sha256",
            "line",
            "doctor_ok",
            "schema",
        )
        and not isinstance(v, (dict, list))
    }
    fr = bus.publish(
        source="station_nexus_coord",
        bits=bits,
        plate=smx_plate,
        integrity={"station": 1, "from": str(clean.get("from") or "BlackCube")[:32]},
        extra_flags={
            "hold_flash": int(clean.get("hold_flash") if clean.get("hold_flash") is not None else 1),
            "topic": str(clean.get("topic") or "channel_stim")[:32],
            "type": str(clean.get("type") or "heartbeat")[:32],
        },
    )
    llama = probe_local(cfg)
    ack = plate_ack(
        from_id="Grokium",
        ref_seq=clean.get("seq"),
        unity=float(clean.get("unity") or 1.0),
        llama=1 if llama.get("ok") else 0,
        ssh=int(clean.get("ssh") or 0),
        watchd=int(clean.get("watchd") or 0),
        farm=str(clean.get("farm") or "standby"),
        hold_flash=int(clean.get("hold_flash") if clean.get("hold_flash") is not None else 1),
        extra={"grokium": 1, "station": 1},
    )
    return {
        "ok": True,
        "skipped": False,
        "share": "state_matrix_only",
        "station_path": str(used),
        "matrix_path": str(path_saved),
        "seq": clean.get("seq"),
        "sot_bits_prefix": bits[:64],
        "smx_seq": fr.get("seq"),
        "ack_plate": ack,
        "plate": {
            k: clean.get(k)
            for k in ("from", "type", "topic", "seq", "unity", "ssh", "llama", "watchd", "farm", "hold_flash")
        },
        "telemetry": False,
    }


def start_station_coord_watch(cfg: dict[str, Any], *, interval_s: float = 2.0) -> dict[str, Any]:
    """Background poll of station nexus_coord → Grokium matrix/SMX."""
    global _watch_thread
    with _watch_lock:
        if _watch_thread is not None and _watch_thread.is_alive():
            return {"ok": True, "already": True, "paths": [str(p) for p in station_coord_paths(cfg)]}

        def _loop() -> None:
            while True:
                try:
                    ingest_station_coord(cfg, force=False)
                except Exception:
                    pass
                time.sleep(max(0.5, float(interval_s)))

        t = threading.Thread(target=_loop, name="grokium-station-coord", daemon=True)
        _watch_thread = t
        t.start()
        return {
            "ok": True,
            "started": True,
            "interval_s": interval_s,
            "paths": [str(p) for p in station_coord_paths(cfg)],
            "telemetry": False,
        }
