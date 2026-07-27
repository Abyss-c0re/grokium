# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Zero telemetry enforcement — no phone-home, no usage stats, no crash cloud."""

from __future__ import annotations

import os
from typing import Any


# Blocked host fragments (never contacted by Grokium itself)
TELEMETRY_DENY = (
    "telemetry",
    "segment.io",
    "amplitude",
    "sentry.io",
    "mixpanel",
    "analytics",
    "x.ai/telemetry",
    "posthog",
)


def assert_zero_telemetry(cfg: dict[str, Any]) -> None:
    priv = cfg.get("privacy") or {}
    for k in ("telemetry", "crash_reports", "usage_stats", "codebase_upload", "improve_model_cloud"):
        if priv.get(k):
            raise RuntimeError(f"Grokium refuses non-zero privacy flag: {k}")
    # Strip any accidental telemetry env
    for e in list(os.environ):
        el = e.lower()
        if "telemetry" in el or "analytics" in el:
            if el.startswith("grokium_"):
                os.environ.pop(e, None)


def host_allowed(url: str) -> bool:
    u = (url or "").lower()
    return not any(d in u for d in TELEMETRY_DENY)
