# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Local-first LLM client. Optional Grok only when auth.enabled."""

from __future__ import annotations

import json
import os
import urllib.error
import urllib.request
from typing import Any

from .privacy import guard_url, host_allowed


def _post_json(url: str, body: dict[str, Any], timeout: float = 60.0, cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    guard_url(url, cfg)
    data = json.dumps(body).encode()
    req = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/json", "User-Agent": "grokium/0.1 (zero-telemetry)"},
        method="POST",
    )
    # Optional Grok key only when targeting auth base
    key = os.environ.get("GROK_API_KEY") or os.environ.get("XAI_API_KEY")
    if key and "grok.com" in url:
        req.add_header("Authorization", f"Bearer {key}")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode())


def _get_json(url: str, timeout: float = 5.0, cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    guard_url(url, cfg)
    req = urllib.request.Request(url, headers={"User-Agent": "grokium/0.1 (zero-telemetry)"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode())


def probe_local(cfg: dict[str, Any]) -> dict[str, Any]:
    base = (cfg.get("local") or {}).get("base_url", "http://127.0.0.1:1212/v1").rstrip("/")
    try:
        models = _get_json(f"{base}/models", timeout=3.0, cfg=cfg)
        return {"ok": True, "base_url": base, "models": models, "path": "local"}
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError) as e:
        return {"ok": False, "base_url": base, "error": str(e), "path": "local"}


def chat(
    cfg: dict[str, Any],
    messages: list[dict[str, str]],
    *,
    prefer_local: bool = True,
    max_tokens: int = 256,
    temperature: float = 0.2,
) -> dict[str, Any]:
    """Complete chat. Local llama first unless prefer_local=False and auth on."""
    auth = cfg.get("auth") or {}
    local = cfg.get("local") or {}
    use_cloud = (not prefer_local) and bool(auth.get("enabled"))

    if use_cloud:
        base = (auth.get("base_url") or "").rstrip("/")
        model = auth.get("model") or "grok-4.5"
        path = "grok_cloud"
    else:
        base = (local.get("base_url") or "http://127.0.0.1:1212/v1").rstrip("/")
        model = local.get("model") or "local"
        # resolve model id from live server if model==local
        if model == "local":
            probe = probe_local(cfg)
            if probe.get("ok"):
                data = (probe.get("models") or {}).get("data") or []
                if data:
                    model = data[0].get("id") or model
        path = "local"

    url = f"{base}/chat/completions"
    body = {
        "model": model,
        "messages": messages,
        "max_tokens": max_tokens,
        "temperature": temperature,
        "stream": False,
        # Qwen3 thinking models: prefer direct answer for harness probes
        "chat_template_kwargs": {"enable_thinking": False},
    }
    try:
        resp = _post_json(url, body, timeout=120.0, cfg=cfg)
        choice = (resp.get("choices") or [{}])[0]
        msg = choice.get("message") or {}
        content = msg.get("content") or ""
        if not content:
            # Qwen-style reasoning models may put text in reasoning fields
            content = (
                msg.get("reasoning_content")
                or msg.get("reasoning")
                or choice.get("text")
                or ""
            )
        if isinstance(content, list):
            content = "".join(
                (p.get("text") if isinstance(p, dict) else str(p)) for p in content
            )
        return {
            "ok": True,
            "path": path,
            "model": model,
            "content": (content or "").strip(),
            "finish_reason": choice.get("finish_reason"),
            "usage": resp.get("usage"),
            "telemetry": False,
            "raw_message_keys": sorted(msg.keys()),
        }
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError, RuntimeError) as e:
        return {"ok": False, "path": path, "model": model, "error": str(e), "telemetry": False}
