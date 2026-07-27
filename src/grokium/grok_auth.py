# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Optional Grok/xAI API credentials — independent product, not official xAI client.

Priority for Bearer token:
  1. GROK_API_KEY / XAI_API_KEY env
  2. ~/.grok/auth.json session tokens (from original `grok login` web/OIDC flow)
  3. Optional: run `grok login` to refresh via the original CLI (web browser)

Grokium does not claim affiliation with xAI. Tokens remain the user's.
"""

from __future__ import annotations

import json
import os
import subprocess
import time
from pathlib import Path
from typing import Any

DEFAULT_AUTH_JSON = Path.home() / ".grok" / "auth.json"
DEFAULT_PROXY = "https://cli-chat-proxy.grok.com/v1"


def auth_json_path() -> Path:
    return Path(os.environ.get("GROK_AUTH_JSON") or DEFAULT_AUTH_JSON)


def _extract_tokens_from_obj(obj: Any, path: str = "") -> list[dict[str, Any]]:
    """Find access/session tokens in auth.json tree (shapes vary by grok CLI version)."""
    found: list[dict[str, Any]] = []
    if isinstance(obj, dict):
        # leaf credential blob
        key = obj.get("key") or obj.get("access_token") or obj.get("token")
        if isinstance(key, str) and len(key) > 20:
            found.append(
                {
                    "token": key,
                    "path": path or "entry",
                    "refresh_token": obj.get("refresh_token"),
                    "email": obj.get("email"),
                    "user_id": obj.get("user_id") or obj.get("principal_id"),
                    "auth_mode": obj.get("auth_mode"),
                    "create_time": obj.get("create_time"),
                    "team_id": obj.get("team_id"),
                }
            )
        for k, v in obj.items():
            # flat "….key" string entries
            if isinstance(v, str) and k.endswith(".key") and len(v) > 20:
                found.append({"token": v, "path": k, "refresh_token": obj.get(k.replace(".key", ".refresh_token"))})
            else:
                found.extend(_extract_tokens_from_obj(v, f"{path}.{k}" if path else str(k)))
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            found.extend(_extract_tokens_from_obj(v, f"{path}[{i}]"))
    return found


def load_auth_json(path: Path | None = None) -> dict[str, Any]:
    p = path or auth_json_path()
    if not p.is_file():
        return {}
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def get_access_token() -> dict[str, Any]:
    """Return best available token without printing it."""
    env = (os.environ.get("GROK_API_KEY") or os.environ.get("XAI_API_KEY") or "").strip()
    if env:
        return {
            "ok": True,
            "source": "env",
            "token": env,
            "email": None,
            "path": "GROK_API_KEY|XAI_API_KEY",
        }

    data = load_auth_json()
    tokens = _extract_tokens_from_obj(data)
    if not tokens:
        return {
            "ok": False,
            "source": None,
            "token": None,
            "error": "no token in env or ~/.grok/auth.json — run: grok login  (or /login in TUI)",
            "auth_json": str(auth_json_path()),
            "auth_json_exists": auth_json_path().is_file(),
        }

    # prefer entries with email / create_time (newest if comparable)
    def sort_key(t: dict[str, Any]) -> tuple:
        ct = t.get("create_time") or ""
        return (1 if t.get("email") else 0, str(ct), len(t.get("token") or ""))

    tokens.sort(key=sort_key, reverse=True)
    best = tokens[0]
    return {
        "ok": True,
        "source": "auth.json",
        "token": best["token"],
        "email": best.get("email"),
        "user_id": best.get("user_id"),
        "path": best.get("path"),
        "auth_json": str(auth_json_path()),
        "candidates": len(tokens),
    }


def auth_status() -> dict[str, Any]:
    t = get_access_token()
    out = {
        "ok": bool(t.get("ok")),
        "source": t.get("source"),
        "email": t.get("email"),
        "auth_json": str(auth_json_path()),
        "auth_json_exists": auth_json_path().is_file(),
        "env_key_set": bool(os.environ.get("GROK_API_KEY") or os.environ.get("XAI_API_KEY")),
        "token_present": bool(t.get("token")),
        "token_len": len(t.get("token") or ""),
        "affiliated_with_xai": False,
        "note": "Uses same ~/.grok/auth.json as original Grok CLI when present. Grokium is not xAI.",
        "login": "grok login  (web/OIDC via original CLI)  or  /login in TUI",
    }
    if not t.get("ok"):
        out["error"] = t.get("error")
    return out


def ensure_token(*, try_login: bool = False, timeout: int = 300) -> dict[str, Any]:
    """Get token; optionally run original `grok login` for web-based auth."""
    cur = get_access_token()
    if cur.get("ok"):
        return {**auth_status(), "action": "reuse"}
    if not try_login:
        return {**auth_status(), "action": "none"}
    return login_web(timeout=timeout)


def login_web(timeout: int = 300) -> dict[str, Any]:
    """Delegate web/OIDC login to original Grok CLI (`grok login`) when installed.

    This is the same browser-based flow users already know — Grokium does not
    reimplement xAI's IdP; it reuses the official CLI credential store.
    """
    grok = os.environ.get("GROK_CLI") or "grok"
    # resolve path
    from shutil import which

    bin_path = which(grok) or which("grok")
    if not bin_path:
        return {
            "ok": False,
            "error": "grok CLI not found on PATH — install Grok Build CLI or set GROK_API_KEY",
            "action": "login_failed",
            "affiliated_with_xai": False,
        }

    try:
        # Interactive: user completes browser login in original CLI
        proc = subprocess.run(
            [bin_path, "login"],
            timeout=timeout,
            capture_output=True,
            text=True,
        )
        # re-read store
        cur = get_access_token()
        if cur.get("ok"):
            return {
                **auth_status(),
                "action": "login_ok",
                "returncode": proc.returncode,
                "stderr_tail": (proc.stderr or "")[-400:],
            }
        return {
            "ok": False,
            "action": "login_no_token",
            "returncode": proc.returncode,
            "stderr_tail": (proc.stderr or "")[-600:],
            "stdout_tail": (proc.stdout or "")[-400:],
            "error": "grok login finished but no token in auth.json",
        }
    except subprocess.TimeoutExpired:
        return {"ok": False, "error": f"grok login timed out after {timeout}s", "action": "timeout"}
    except OSError as e:
        return {"ok": False, "error": str(e), "action": "os_error"}


def bearer_headers(extra: dict[str, str] | None = None) -> dict[str, str]:
    h = {
        "Content-Type": "application/json",
        "User-Agent": "grokium/0.5 (independent; zero-telemetry; not-xai)",
        # Grok CLI chat proxy often expects this marker (see upstream docs)
        "X-XAI-Token-Auth": "xai-grok-cli",
    }
    tok = get_access_token()
    if tok.get("ok") and tok.get("token"):
        h["Authorization"] = f"Bearer {tok['token']}"
    if extra:
        h.update(extra)
    return h
