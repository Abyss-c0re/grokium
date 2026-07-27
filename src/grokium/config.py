# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Load grokium.toml — privacy hard-off, local llama default."""

from __future__ import annotations

import os
import tomllib
from pathlib import Path
from typing import Any

from .privacy import force_privacy_false

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CFG = ROOT / "config" / "grokium.toml"


def load(path: Path | None = None) -> dict[str, Any]:
    cfg_path = path or Path(os.environ.get("GROKIUM_CONFIG", str(DEFAULT_CFG)))
    data: dict[str, Any] = {}
    if cfg_path.is_file():
        with cfg_path.open("rb") as f:
            data = tomllib.load(f)

    # Hard zero telemetry — cannot be enabled by env
    priv = data.setdefault("privacy", {})
    for k in ("telemetry", "crash_reports", "usage_stats", "codebase_upload", "improve_model_cloud"):
        priv[k] = False

    auth = data.setdefault("auth", {})
    if os.environ.get("GROKIUM_GROK_AUTH", "").strip() in ("1", "true", "yes"):
        auth["enabled"] = True
    else:
        auth.setdefault("enabled", False)

    local = data.setdefault("local", {})
    local.setdefault("base_url", "http://127.0.0.1:1212/v1")
    local.setdefault("model", "local")

    server = data.setdefault("server", {})
    server.setdefault("host", "127.0.0.1")
    server.setdefault("port", 17444)

    sessions = data.setdefault("sessions", {})
    sessions.setdefault("grok_sessions", str(Path.home() / ".grok" / "sessions"))
    sessions.setdefault("import_dir", str(ROOT / "data" / "import"))

    cube = data.setdefault("cube", {})
    cube.setdefault("control_url", "http://127.0.0.1:17333")
    cube.setdefault("enabled", True)
    cube.setdefault("share_mode", "state_matrix_only")

    data["_root"] = str(ROOT)
    data["_cfg_path"] = str(cfg_path)
    force_privacy_false(data)
    return data
