# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Product branding — independent of xAI / Grok. Always show disclaimers."""

from __future__ import annotations

PRODUCT = "Grokium"
PRODUCT_SLUG = "grokium"
VERSION_LABEL = "independent local harness"

# Short lines for TUI / headers / MCP
DISCLAIMER_SHORT = "Not affiliated with xAI or Grok"
DISCLAIMER_MEDIUM = (
    "Grokium is independent open source — not affiliated with, endorsed by, "
    "or sponsored by xAI or Grok / Grok Build."
)
DISCLAIMER_LONG = (
    "Grokium is an independent open-source agent harness (Apache-2.0). "
    "It is NOT affiliated with, endorsed by, sponsored by, or partnered with "
    "xAI, SpaceXAI, or the Grok / Grok Build products. "
    "Optional use of Grok cloud APIs with your own credentials does not make "
    "this software an official xAI product. "
    "Trademarks remain with their owners and are used only for interoperability."
)

# Never present as official
FORBIDDEN_CLAIMS = (
    "official xai",
    "official grok",
    "xai product",
    "by xai",
    "powered by xai",  # allowed only as optional API path description with disclaimer
)


def banner_lines() -> list[str]:
    return [
        f"{PRODUCT} — {VERSION_LABEL}",
        DISCLAIMER_SHORT,
        "Local-first · zero telemetry · optional cloud is your key, not our brand",
    ]


def status_disclaimer() -> dict:
    return {
        "product": PRODUCT_SLUG,
        "affiliated_with_xai": False,
        "affiliated_with_grok": False,
        "official_xai_client": False,
        "disclaimer": DISCLAIMER_MEDIUM,
    }
