# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Dual versioning: Grokium product vs reported Grok Build client version.

- PRODUCT version (__version__) is ours alone.
- REPORTED version is what we send as `x-grok-client-version` to cli-chat-proxy
  so the upstream gate accepts requests. It must track Grok Build, not our app.

Watcher hot-swaps the reported version live (no TUI restart) for the Cube.
"""

from __future__ import annotations

import json
import os
import re
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

from . import __version__ as GROKIUM_VERSION

ROOT = Path(__file__).resolve().parents[2]
STATE_PATH = ROOT / "data" / "grok_build_compat.json"
MIN_REPORTED = "0.1.202"  # hard floor from proxy error text

# Hot-swappable reported version (thread-safe)
_lock = threading.RLock()
_reported: str = "0.2.112"
_watcher_stop = threading.Event()
_watcher_thread: threading.Thread | None = None
_last_check: float = 0.0
_last_source: str = "bootstrap"


def _parse_semver(v: str) -> tuple[int, ...]:
    parts = re.findall(r"\d+", v or "")
    return tuple(int(x) for x in parts[:4]) if parts else (0,)


def _ge(a: str, b: str) -> bool:
    return _parse_semver(a) >= _parse_semver(b)


def product_version() -> str:
    return GROKIUM_VERSION


def reported_version() -> str:
    with _lock:
        return _reported


def set_reported_version(ver: str, *, source: str = "manual") -> str:
    global _reported, _last_source
    ver = (ver or "").strip().lstrip("v")
    if not ver or not re.match(r"^\d+\.\d+", ver):
        raise ValueError(f"bad version {ver!r}")
    if not _ge(ver, MIN_REPORTED):
        # never report below proxy hard floor
        ver = MIN_REPORTED
    with _lock:
        _reported = ver
        _last_source = source
    _persist()
    return ver


def status() -> dict[str, Any]:
    with _lock:
        return {
            "schema": "grokium.version_compat.v1",
            "product": "grokium",
            "grokium_version": GROKIUM_VERSION,
            "reported_grok_build_version": _reported,
            "min_reported": MIN_REPORTED,
            "header": "x-grok-client-version",
            "last_source": _last_source,
            "last_check_ts": _last_check,
            "watcher_alive": bool(_watcher_thread and _watcher_thread.is_alive()),
            "note": "Reported version is for cli-chat-proxy only — not Grokium app version.",
            "affiliated_with_xai": False,
        }


def _persist() -> None:
    STATE_PATH.parent.mkdir(parents=True, exist_ok=True)
    with _lock:
        blob = {
            "schema": "grokium.version_compat.v1",
            "reported_grok_build_version": _reported,
            "grokium_version": GROKIUM_VERSION,
            "last_source": _last_source,
            "last_check_ts": _last_check,
            "updated_at": time.time(),
        }
    STATE_PATH.write_text(json.dumps(blob, indent=2), encoding="utf-8")


def load_persisted() -> str | None:
    global _reported, _last_source, _last_check
    if not STATE_PATH.is_file():
        return None
    try:
        d = json.loads(STATE_PATH.read_text(encoding="utf-8"))
        ver = d.get("reported_grok_build_version")
        if ver:
            with _lock:
                _reported = ver
                _last_source = d.get("last_source") or "disk"
                _last_check = float(d.get("last_check_ts") or 0)
            return ver
    except Exception:
        pass
    return None


def read_local_grok_cli_version() -> str | None:
    """Prefer installed grok CLI / ~/.grok/version.json."""
    # version.json
    vj = Path.home() / ".grok" / "version.json"
    if vj.is_file():
        try:
            d = json.loads(vj.read_text(encoding="utf-8"))
            for k in ("stable_version", "version", "cli_version"):
                if d.get(k):
                    return str(d[k]).lstrip("v")
        except Exception:
            pass
    # grok --version
    import subprocess
    from shutil import which

    bin_path = which(os.environ.get("GROK_CLI") or "grok")
    if not bin_path:
        return None
    try:
        out = subprocess.check_output([bin_path, "--version"], text=True, timeout=5)
        m = re.search(r"(\d+\.\d+\.\d+)", out)
        if m:
            return m.group(1)
    except Exception:
        pass
    return None


def fetch_upstream_version(timeout: float = 12.0) -> dict[str, Any]:
    """Best-effort discover current Grok Build version without breaking offline use.

    Sources (first success wins for 'remote'):
      1. env GROKIUM_GROK_BUILD_VERSION force
      2. GitHub releases API xai-org/grok-build
      3. local grok CLI / version.json
    """
    forced = (os.environ.get("GROKIUM_GROK_BUILD_VERSION") or "").strip()
    if forced:
        return {"ok": True, "version": forced.lstrip("v"), "source": "env"}

    # GitHub releases latest
    urls = [
        "https://api.github.com/repos/xai-org/grok-build/releases/latest",
        "https://api.github.com/repos/xai-org/grok-build/tags?per_page=5",
    ]
    for url in urls:
        try:
            req = urllib.request.Request(
                url,
                headers={
                    "User-Agent": f"grokium/{GROKIUM_VERSION} (compat-watcher; not-xai)",
                    "Accept": "application/vnd.github+json",
                },
            )
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                data = json.loads(resp.read().decode())
            if isinstance(data, dict) and data.get("tag_name"):
                ver = str(data["tag_name"]).lstrip("v")
                return {"ok": True, "version": ver, "source": "github_release", "raw": data.get("tag_name")}
            if isinstance(data, list) and data:
                name = data[0].get("name") or data[0].get("tag_name") or ""
                m = re.search(r"(\d+\.\d+\.\d+)", name)
                if m:
                    return {"ok": True, "version": m.group(1), "source": "github_tags"}
        except Exception as e:
            last_err = str(e)
            continue

    local = read_local_grok_cli_version()
    if local:
        return {"ok": True, "version": local, "source": "local_grok_cli"}

    return {"ok": False, "error": last_err if "last_err" in dir() else "no source", "source": None}


def refresh_reported(*, force: bool = False) -> dict[str, Any]:
    """Check sources and hot-swap reported version if newer. Live — no UX restart."""
    global _last_check
    _last_check = time.time()
    cur = reported_version()
    up = fetch_upstream_version()
    local = read_local_grok_cli_version()

    candidates = []
    if up.get("ok") and up.get("version"):
        candidates.append((str(up["version"]), str(up.get("source"))))
    if local:
        candidates.append((local, "local_grok_cli"))

    best = cur
    best_src = "unchanged"
    for ver, src in candidates:
        if _ge(ver, best):
            best, best_src = ver, src

    swapped = False
    if best != cur or force:
        set_reported_version(best, source=best_src)
        swapped = best != cur

    # always persist check time
    _persist()
    return {
        "ok": True,
        "swapped": swapped,
        "previous": cur,
        "reported": reported_version(),
        "source": best_src,
        "upstream": up,
        "local_cli": local,
        "grokium_version": GROKIUM_VERSION,
        "live": True,
    }


def start_watcher(interval_sec: float | None = None) -> dict[str, Any]:
    """Background refresh every few hours (default 3h). Idempotent."""
    global _watcher_thread
    interval = float(
        interval_sec
        or os.environ.get("GROKIUM_VERSION_WATCH_SEC")
        or 3 * 3600
    )
    if interval < 60:
        interval = 60

    load_persisted()
    # bootstrap from local CLI immediately
    local = read_local_grok_cli_version()
    if local and _ge(local, reported_version()):
        set_reported_version(local, source="bootstrap_local_cli")

    def _loop() -> None:
        # first tick after short delay so UX starts clean
        if _watcher_stop.wait(15):
            return
        while not _watcher_stop.is_set():
            try:
                refresh_reported()
            except Exception:
                pass
            if _watcher_stop.wait(interval):
                break

    if _watcher_thread and _watcher_thread.is_alive():
        return {"ok": True, "already": True, "interval_sec": interval, "reported": reported_version()}

    _watcher_stop.clear()
    _watcher_thread = threading.Thread(target=_loop, name="grokium-version-watch", daemon=True)
    _watcher_thread.start()
    return {
        "ok": True,
        "started": True,
        "interval_sec": interval,
        "reported": reported_version(),
        "grokium_version": GROKIUM_VERSION,
    }


def stop_watcher() -> None:
    _watcher_stop.set()


# load disk state at import
load_persisted()
_local0 = read_local_grok_cli_version()
if _local0 and _ge(_local0, reported_version()):
    try:
        set_reported_version(_local0, source="import_local_cli")
    except Exception:
        pass
