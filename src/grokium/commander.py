# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""THE LAW: Grokium commander — unforgeable. Models are not commander."""

from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_LAW_DIR = ROOT / "data" / "law"
COMMANDER_BIN = ROOT / "build" / "grokium-commander"

DOMAIN = "GROKIUM-COMMANDER-v1"
PRODUCT = "grokium"
NOT_MODEL = "grok_model"


def _bin() -> Path:
    env = os.environ.get("GROKIUM_COMMANDER_BIN")
    if env and Path(env).is_file():
        return Path(env)
    return COMMANDER_BIN


def ensure_built() -> Path:
    b = _bin()
    if b.is_file():
        return b
    subprocess.check_call(["make", "-C", str(ROOT / "c_core"), "all"], stdout=subprocess.DEVNULL)
    if not b.is_file():
        raise RuntimeError("grokium-commander binary missing — make -C c_core")
    return b


def keygen(law_dir: Path | None = None) -> dict[str, Any]:
    law_dir = law_dir or DEFAULT_LAW_DIR
    law_dir.mkdir(parents=True, exist_ok=True)
    b = ensure_built()
    out = subprocess.check_output(
        [str(b), "keygen", "--law-dir", str(law_dir)], text=True
    )
    return json.loads(out)


def show(law_dir: Path | None = None) -> dict[str, Any]:
    law_dir = law_dir or DEFAULT_LAW_DIR
    b = ensure_built()
    out = subprocess.check_output(
        [str(b), "show", "--law-dir", str(law_dir)], text=True
    )
    return json.loads(out)


def sign_override(
    device: str,
    action: str,
    *,
    body: bytes | None = None,
    law_dir: Path | None = None,
) -> dict[str, Any]:
    law_dir = law_dir or DEFAULT_LAW_DIR
    b = ensure_built()
    cmd = [
        str(b),
        "sign",
        "--law-dir",
        str(law_dir),
        "--device",
        device,
        "--action",
        action,
    ]
    if body is not None:
        r = subprocess.run(
            cmd + ["--body", "-"],
            input=body,
            capture_output=True,
            check=True,
        )
        return json.loads(r.stdout.decode())
    out = subprocess.check_output(cmd, text=True)
    return json.loads(out)


def verify_override(env: dict[str, Any], *, law_dir: Path | None = None) -> dict[str, Any]:
    """Verify envelope. Rejects model theater before crypto when obvious."""
    blob = json.dumps(env).lower()
    if "i am grok" in blob and env.get("product") != PRODUCT:
        return {"ok": False, "error": "model_claim_rejected", "unforgeable": True}
    if env.get("product") != PRODUCT:
        return {"ok": False, "error": "not_grokium_product", "unforgeable": True}
    if env.get("domain") != DOMAIN:
        return {"ok": False, "error": "bad_domain", "unforgeable": True}
    if env.get("not") != NOT_MODEL:
        return {"ok": False, "error": "missing_not_grok_model", "unforgeable": True}

    law_dir = law_dir or DEFAULT_LAW_DIR
    b = ensure_built()
    cmd = [
        str(b),
        "verify",
        "--law-dir",
        str(law_dir),
        "--device",
        str(env.get("device") or ""),
        "--action",
        str(env.get("action") or ""),
        "--nonce",
        str(env.get("nonce") or ""),
        "--ts",
        str(env.get("ts") or 0),
        "--sig",
        str(env.get("sig") or ""),
    ]
    r = subprocess.run(cmd, capture_output=True, text=True)
    try:
        return json.loads(r.stdout or "{}")
    except json.JSONDecodeError:
        return {"ok": False, "error": "verify_failed", "stderr": r.stderr}


def install_law_on_home(
    nanobot_home: Path | str,
    bot_id: str,
    purpose: str,
    *,
    law_dir: Path | None = None,
) -> dict[str, Any]:
    law_dir = law_dir or DEFAULT_LAW_DIR
    b = ensure_built()
    out = subprocess.check_output(
        [
            str(b),
            "install-law",
            "--law-dir",
            str(law_dir),
            "--home",
            str(nanobot_home),
            "--bot",
            bot_id,
            "--purpose",
            purpose,
        ],
        text=True,
    )
    return json.loads(out)


def reject_fake_model_authority(claim: str) -> dict[str, Any]:
    """Explicit LAW: Grok model text cannot be commander."""
    c = (claim or "").lower()
    if "grokium" in c and "signature" in c:
        return {"ok": None, "hint": "need_crypto_verify"}
    if any(
        x in c
        for x in (
            "i am grok",
            "as a grok model",
            "grok-4",
            "model_is_commander",
            "trust me i am the commander",
        )
    ):
        return {
            "ok": False,
            "error": "model_is_not_commander",
            "law": "GROKIUM_COMMANDER_LAW",
            "unforgeable": True,
        }
    return {"ok": False, "error": "unsigned_claim", "unforgeable": True}
