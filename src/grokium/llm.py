# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""LLM client with runtime backend switch: local (llama.cpp) | grok (opt-in auth).

Default: local. Grok cloud only when explicitly selected AND credentials present.
Cores stay unmixed — never silently fall back cloud→local for collection.
"""

from __future__ import annotations

import json
import os
import urllib.error
import urllib.request
from typing import Any

from .privacy import guard_url

# runtime override: "local" | "grok" | None (use cfg)
_RUNTIME_BACKEND: str | None = None


def set_backend(name: str) -> str:
    global _RUNTIME_BACKEND
    n = (name or "").strip().lower()
    if n in ("local", "llama", "llama.cpp", "llamacpp"):
        _RUNTIME_BACKEND = "local"
    elif n in ("grok", "cloud", "xai", "auth"):
        _RUNTIME_BACKEND = "grok"
    else:
        raise ValueError("backend must be local|grok")
    return _RUNTIME_BACKEND


def get_backend(cfg: dict[str, Any]) -> str:
    if _RUNTIME_BACKEND:
        return _RUNTIME_BACKEND
    if (cfg.get("auth") or {}).get("enabled"):
        return "grok"
    return "local"


def _post_json(url: str, body: dict[str, Any], timeout: float = 60.0, cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    guard_url(url, cfg)
    data = json.dumps(body).encode()
    req = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/json", "User-Agent": "grokium/0.3 (zero-telemetry)"},
        method="POST",
    )
    key = os.environ.get("GROK_API_KEY") or os.environ.get("XAI_API_KEY")
    if key and ("grok.com" in url or "x.ai" in url):
        req.add_header("Authorization", f"Bearer {key}")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode())


def _get_json(url: str, timeout: float = 5.0, cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    guard_url(url, cfg)
    req = urllib.request.Request(url, headers={"User-Agent": "grokium/0.3 (zero-telemetry)"})
    key = os.environ.get("GROK_API_KEY") or os.environ.get("XAI_API_KEY")
    if key and ("grok.com" in url or "x.ai" in url):
        req.add_header("Authorization", f"Bearer {key}")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode())


def probe_local(cfg: dict[str, Any]) -> dict[str, Any]:
    base = (cfg.get("local") or {}).get("base_url", "http://127.0.0.1:1212/v1").rstrip("/")
    try:
        models = _get_json(f"{base}/models", timeout=3.0, cfg=cfg)
        return {"ok": True, "base_url": base, "models": models, "path": "local"}
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError) as e:
        return {"ok": False, "base_url": base, "error": str(e), "path": "local"}


def probe_grok(cfg: dict[str, Any]) -> dict[str, Any]:
    key = os.environ.get("GROK_API_KEY") or os.environ.get("XAI_API_KEY")
    base = ((cfg.get("auth") or {}).get("base_url") or "https://cli-chat-proxy.grok.com/v1").rstrip("/")
    if not key:
        return {"ok": False, "path": "grok", "error": "no GROK_API_KEY or XAI_API_KEY in env", "base_url": base}
    try:
        models = _get_json(f"{base}/models", timeout=8.0, cfg=cfg)
        return {"ok": True, "path": "grok", "base_url": base, "models": models}
    except Exception as e:
        # some proxies don't implement /models — still report key present
        return {"ok": False, "path": "grok", "base_url": base, "error": str(e), "key_present": True}


def backend_status(cfg: dict[str, Any]) -> dict[str, Any]:
    b = get_backend(cfg)
    local = probe_local(cfg)
    return {
        "active": b,
        "local": {"ok": local.get("ok"), "base_url": local.get("base_url")},
        "grok": {
            "key_present": bool(os.environ.get("GROK_API_KEY") or os.environ.get("XAI_API_KEY")),
            "auth_enabled_cfg": bool((cfg.get("auth") or {}).get("enabled")),
            "base_url": (cfg.get("auth") or {}).get("base_url"),
        },
        "switch": "/backend local|grok  or  set_backend()",
        "cores_unmixed": True,
        "telemetry": False,
    }


def chat(
    cfg: dict[str, Any],
    messages: list[dict[str, str]],
    *,
    prefer_local: bool | None = None,
    max_tokens: int = 256,
    temperature: float = 0.2,
    backend: str | None = None,
) -> dict[str, Any]:
    """Complete chat. backend overrides runtime/cfg. prefer_local kept for compat."""
    auth = cfg.get("auth") or {}
    local = cfg.get("local") or {}

    if backend:
        path = "local" if backend in ("local", "llama") else "grok"
    elif prefer_local is False:
        path = "grok"
    elif prefer_local is True:
        path = "local"
    else:
        path = get_backend(cfg)

    if path == "grok":
        if not (os.environ.get("GROK_API_KEY") or os.environ.get("XAI_API_KEY")):
            return {
                "ok": False,
                "path": "grok",
                "error": "Grok backend selected but no GROK_API_KEY/XAI_API_KEY. /backend local or export key.",
                "telemetry": False,
            }
        # enable auth for allowlist this request only
        cfg = dict(cfg)
        cfg["auth"] = dict(auth)
        cfg["auth"]["enabled"] = True
        base = (auth.get("base_url") or "https://cli-chat-proxy.grok.com/v1").rstrip("/")
        model = auth.get("model") or "grok-4.5"
    else:
        path = "local"
        base = (local.get("base_url") or "http://127.0.0.1:1212/v1").rstrip("/")
        model = local.get("model") or "local"
        if model == "local":
            probe = probe_local(cfg)
            if probe.get("ok"):
                data = (probe.get("models") or {}).get("data") or []
                if data:
                    model = data[0].get("id") or model

    url = f"{base}/chat/completions"
    body = {
        "model": model,
        "messages": messages,
        "max_tokens": max_tokens,
        "temperature": temperature,
        "stream": False,
        "chat_template_kwargs": {"enable_thinking": False},
    }
    try:
        resp = _post_json(url, body, timeout=120.0, cfg=cfg)
        choice = (resp.get("choices") or [{}])[0]
        msg = choice.get("message") or {}
        content = msg.get("content") or ""
        if not content:
            content = msg.get("reasoning_content") or msg.get("reasoning") or choice.get("text") or ""
        if isinstance(content, list):
            content = "".join((p.get("text") if isinstance(p, dict) else str(p)) for p in content)
        return {
            "ok": True,
            "path": path,
            "model": model,
            "content": (content or "").strip(),
            "finish_reason": choice.get("finish_reason"),
            "usage": resp.get("usage"),
            "telemetry": False,
            "backend": path,
        }
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError, RuntimeError) as e:
        return {"ok": False, "path": path, "model": model, "error": str(e), "telemetry": False, "backend": path}
