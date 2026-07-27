# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""License and attribution surfaces — always available at runtime."""

from __future__ import annotations

from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]

SPDX = "Apache-2.0"
COPYRIGHT = "Copyright 2026 Grokium contributors"
NOT_AFFILIATED = (
    "Grokium is not affiliated with, endorsed by, or sponsored by xAI, "
    "SpaceXAI, Grok, or the Grok Build project. Independent open source."
)

REQUIRED_FILES = ("LICENSE", "NOTICE", "THIRD-PARTY-NOTICES", "CREDITS.md")


def root() -> Path:
    return ROOT


def paths() -> dict[str, str]:
    return {name: str(ROOT / name) for name in REQUIRED_FILES}


def verify_files_present() -> dict[str, Any]:
    missing = [n for n in REQUIRED_FILES if not (ROOT / n).is_file()]
    license_path = ROOT / "LICENSE"
    has_full_apache = False
    if license_path.is_file():
        text = license_path.read_text(encoding="utf-8", errors="replace")
        has_full_apache = "Apache License" in text and "2.0" in text and len(text) > 5000
    notice_ok = False
    np = ROOT / "NOTICE"
    if np.is_file():
        nt = np.read_text(encoding="utf-8", errors="replace")
        notice_ok = "not affiliated" in nt.lower() or "NOT affiliated" in nt
    return {
        "ok": not missing and has_full_apache and notice_ok,
        "spdx": SPDX,
        "copyright": COPYRIGHT,
        "missing_files": missing,
        "full_apache_license_text": has_full_apache,
        "notice_disclaims_affiliation": notice_ok,
        "not_affiliated": NOT_AFFILIATED,
        "affiliated_with_xai": False,
        "affiliated_with_grok": False,
        "official_xai_client": False,
        "paths": paths(),
    }


def public_blob() -> dict[str, Any]:
    v = verify_files_present()
    return {
        "spdx": SPDX,
        "copyright": COPYRIGHT,
        "not_affiliated": NOT_AFFILIATED,
        "affiliated_with_xai": False,
        "affiliated_with_grok": False,
        "official_xai_client": False,
        "files": paths(),
        "compliance": {
            "ok": v["ok"],
            "full_apache_license_text": v["full_apache_license_text"],
            "notice_disclaims_affiliation": v["notice_disclaims_affiliation"],
            "missing_files": v["missing_files"],
        },
        "inspired_by": {
            "name": "Grok Build",
            "url": "https://github.com/xai-org/grok-build",
            "license": "Apache-2.0 (upstream first-party)",
            "relationship": "inspiration_and_session_interop_only_not_a_source_copy",
        },
        "session_import_policy": (
            "Imported ~/.grok/sessions remain the user's data; Grokium does not relicense them."
        ),
    }
