# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Zero telemetry + anti data-collection — fail closed. Extremely hard to weaponize for collection."""

from __future__ import annotations

import os
from typing import Any

# Blocked host fragments (never contacted by Grokium itself for collection)
TELEMETRY_DENY = (
    "telemetry",
    "segment.io",
    "amplitude",
    "sentry.io",
    "mixpanel",
    "analytics",
    "x.ai/telemetry",
    "posthog",
    "datadog",
    "fullstory",
    "hotjar",
    "google-analytics",
    "facebook.com/tr",
    "scorecardresearch",
    "newrelic",
    "bugsnag",
    "appcenter",
    "crashlytics",
    "firebase",
    "intercom.io",
    "customer.io",
    "heap.io",
    "clarity.ms",
)

# Privacy keys that MUST remain false — cannot be turned on for collection
_HARD_FALSE = (
    "telemetry",
    "crash_reports",
    "usage_stats",
    "codebase_upload",
    "improve_model_cloud",
)


def force_privacy_false(cfg: dict[str, Any]) -> dict[str, Any]:
    """Mutate cfg so privacy collection flags are always false (cannot stick)."""
    priv = cfg.setdefault("privacy", {})
    for k in _HARD_FALSE:
        priv[k] = False
    cube = cfg.setdefault("cube", {})
    # share mode cannot be widened to prose export
    if cube.get("share_mode") not in (None, "state_matrix_only"):
        cube["share_mode"] = "state_matrix_only"
    else:
        cube["share_mode"] = "state_matrix_only"
    return cfg


def assert_zero_telemetry(cfg: dict[str, Any]) -> None:
    force_privacy_false(cfg)
    priv = cfg.get("privacy") or {}
    for k in _HARD_FALSE:
        if priv.get(k):
            raise RuntimeError(f"Grokium refuses non-zero privacy flag: {k}")
    # Strip any accidental telemetry env
    for e in list(os.environ):
        el = e.lower()
        if "telemetry" in el or "analytics" in el or "datacollect" in el:
            if el.startswith("grokium_") or el.startswith("grok_"):
                os.environ.pop(e, None)


def host_allowed(url: str) -> bool:
    u = (url or "").lower()
    if not u:
        return False
    if any(d in u for d in TELEMETRY_DENY):
        return False
    return True


def guard_url(url: str, cfg: dict[str, Any] | None = None) -> str:
    """Raise if URL is collection/telemetry or fails integrity allowlist."""
    if not host_allowed(url):
        raise RuntimeError(f"blocked host (telemetry/collection deny): {url}")
    if cfg is not None:
        from .integrity_core import assert_egress_allowed

        assert_egress_allowed(url, cfg)
    return url
