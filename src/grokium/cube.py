# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Local Cube control bridge (loopback). Never phones home."""

from __future__ import annotations

import json
import urllib.error
import urllib.request
from typing import Any

from .privacy import host_allowed


def _get(url: str, timeout: float = 3.0) -> dict[str, Any]:
    if not host_allowed(url):
        return {"ok": False, "error": "host_denied"}
    # only allow loopback cube
    if not any(h in url for h in ("127.0.0.1", "localhost", "::1")):
        return {"ok": False, "error": "cube_bridge_loopback_only"}
    req = urllib.request.Request(url, headers={"User-Agent": "grokium/0.1 (zero-telemetry)"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode())
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError) as e:
        return {"ok": False, "error": str(e)}


def status(cfg: dict[str, Any]) -> dict[str, Any]:
    cube = cfg.get("cube") or {}
    if not cube.get("enabled", True):
        return {"ok": False, "error": "cube_disabled"}
    base = (cube.get("control_url") or "http://127.0.0.1:17333").rstrip("/")
    st = _get(f"{base}/v1/status")
    return {"ok": bool(st.get("ok") if isinstance(st, dict) else False), "control_url": base, "status": st}
