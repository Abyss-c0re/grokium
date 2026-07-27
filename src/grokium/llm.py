# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""LLM client: local llama.cpp | opt-in Grok. Streaming + non-streaming."""

from __future__ import annotations

import json
import os
import urllib.error
import urllib.request
from typing import Any, Callable, Iterator

from .privacy import guard_url

# models.resolve used for configurable local ids

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


def _auth_header(url: str) -> dict[str, str]:
    """Bearer from env or ~/.grok/auth.json (same store as original grok login)."""
    from .grok_auth import bearer_headers, get_access_token

    # Always attach JSON content-type; only add Bearer for grok/xai hosts
    if "grok.com" in url or "x.ai" in url or "cli-chat-proxy" in url:
        return bearer_headers()
    h = {"Content-Type": "application/json", "User-Agent": "grokium/0.5 (zero-telemetry; not-xai)"}
    return h


def _resolve_endpoint(cfg: dict[str, Any], backend: str | None) -> tuple[str, str, str, dict]:
    """Returns path, base, model, cfg_for_guard."""
    auth = cfg.get("auth") or {}
    local = cfg.get("local") or {}
    if backend:
        path = "local" if backend in ("local", "llama") else "grok"
    else:
        path = get_backend(cfg)

    cfg2 = dict(cfg)
    if path == "grok":
        from .grok_auth import get_access_token

        tok = get_access_token()
        if not tok.get("ok"):
            raise RuntimeError(
                tok.get("error")
                or "Grok backend needs token: run `grok login` (web) or set GROK_API_KEY"
            )
        cfg2["auth"] = dict(auth)
        cfg2["auth"]["enabled"] = True
        base = (auth.get("base_url") or "https://cli-chat-proxy.grok.com/v1").rstrip("/")
        model = auth.get("model") or "grok-4.5"
    else:
        path = "local"
        base = (local.get("base_url") or "http://127.0.0.1:1212/v1").rstrip("/")
        try:
            from .models import resolve_model_id
            resolved = resolve_model_id(cfg)
            if resolved.get("backend") == "grok":
                # user selected grok via model alias
                return _resolve_endpoint(cfg, "grok")
            model = resolved.get("id") or local.get("model") or "local"
        except Exception:
            model = local.get("model") or "local"
            if model == "local":
                probe = probe_local(cfg)
                if probe.get("ok"):
                    data = (probe.get("models") or {}).get("data") or []
                    if data:
                        model = data[0].get("id") or model
    return path, base, model, cfg2


def _post_json(url: str, body: dict[str, Any], timeout: float = 60.0, cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    guard_url(url, cfg)
    data = json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, headers=_auth_header(url), method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode())


def _get_json(url: str, timeout: float = 5.0, cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    guard_url(url, cfg)
    req = urllib.request.Request(url, headers=_auth_header(url))
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode())


def probe_local(cfg: dict[str, Any]) -> dict[str, Any]:
    base = (cfg.get("local") or {}).get("base_url", "http://127.0.0.1:1212/v1").rstrip("/")
    try:
        models = _get_json(f"{base}/models", timeout=3.0, cfg=cfg)
        return {"ok": True, "base_url": base, "models": models, "path": "local"}
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError) as e:
        return {"ok": False, "base_url": base, "error": str(e), "path": "local"}


def _grok_status_blob(cfg: dict[str, Any]) -> dict[str, Any]:
    from .grok_auth import auth_status

    st = auth_status()
    return {
        "token_present": st.get("token_present"),
        "source": st.get("source"),
        "email": st.get("email"),
        "auth_json_exists": st.get("auth_json_exists"),
        "env_key_set": st.get("env_key_set"),
        "auth_enabled_cfg": bool((cfg.get("auth") or {}).get("enabled")),
        "base_url": (cfg.get("auth") or {}).get("base_url"),
        "login": st.get("login"),
        "affiliated_with_xai": False,
    }


def backend_status(cfg: dict[str, Any]) -> dict[str, Any]:
    b = get_backend(cfg)
    local = probe_local(cfg)
    try:
        from .models import resolve_model_id
        active_model = resolve_model_id(cfg)
    except Exception as e:
        active_model = {"error": str(e)}
    return {
        "active": b,
        "active_model": active_model,
        "local": {"ok": local.get("ok"), "base_url": local.get("base_url")},
        "grok": _grok_status_blob(cfg),
        "stream": True,
        "switch": "/backend local|grok",
        "cores_unmixed": True,
        "telemetry": False,
    }


def _extract_delta(chunk: dict[str, Any]) -> str:
    choices = chunk.get("choices") or []
    if not choices:
        return ""
    c0 = choices[0]
    delta = c0.get("delta") or {}
    if delta.get("content"):
        return str(delta["content"])
    # some servers put full message on stream
    msg = c0.get("message") or {}
    if msg.get("content"):
        return str(msg["content"])
    if c0.get("text"):
        return str(c0["text"])
    return ""


def chat_stream(
    cfg: dict[str, Any],
    messages: list[dict[str, str]],
    *,
    max_tokens: int = 512,
    temperature: float = 0.2,
    backend: str | None = None,
    on_token: Callable[[str], None] | None = None,
) -> dict[str, Any]:
    """Stream tokens. on_token called for each delta. Returns final assembly."""
    try:
        path, base, model, cfg2 = _resolve_endpoint(cfg, backend)
    except RuntimeError as e:
        return {"ok": False, "error": str(e), "path": backend or get_backend(cfg), "telemetry": False}

    url = f"{base}/chat/completions"
    body = {
        "model": model,
        "messages": messages,
        "max_tokens": max_tokens,
        "temperature": temperature,
        "stream": True,
        "chat_template_kwargs": {"enable_thinking": False},
    }
    guard_url(url, cfg2)
    data = json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, headers=_auth_header(url), method="POST")
    parts: list[str] = []
    try:
        with urllib.request.urlopen(req, timeout=180.0) as resp:
            while True:
                raw = resp.readline()
                if not raw:
                    break
                line = raw.decode("utf-8", "replace").strip()
                if not line:
                    continue
                if line.startswith("data:"):
                    line = line[5:].strip()
                if line in ("[DONE]", "data: [DONE]"):
                    break
                try:
                    chunk = json.loads(line)
                except json.JSONDecodeError:
                    continue
                delta = _extract_delta(chunk)
                if delta:
                    parts.append(delta)
                    if on_token:
                        on_token(delta)
        content = "".join(parts).strip()
        return {
            "ok": True,
            "path": path,
            "model": model,
            "content": content,
            "streamed": True,
            "backend": path,
            "telemetry": False,
        }
    except (urllib.error.URLError, TimeoutError, OSError, RuntimeError) as e:
        # fallback non-stream if server rejects stream
        if "stream" in str(e).lower() or True:
            r = chat(cfg, messages, max_tokens=max_tokens, temperature=temperature, backend=path, prefer_local=(path == "local"))
            if r.get("ok") and on_token and r.get("content"):
                # fake stream in chunks for TUI
                c = r["content"]
                step = max(8, len(c) // 40)
                for i in range(0, len(c), step):
                    on_token(c[i : i + step])
            return {**r, "streamed": False, "fallback": str(e)}
        return {"ok": False, "path": path, "error": str(e), "telemetry": False}


def chat(
    cfg: dict[str, Any],
    messages: list[dict[str, str]],
    *,
    prefer_local: bool | None = None,
    max_tokens: int = 256,
    temperature: float = 0.2,
    backend: str | None = None,
) -> dict[str, Any]:
    if backend is None:
        if prefer_local is False:
            backend = "grok"
        elif prefer_local is True:
            backend = "local"
    try:
        path, base, model, cfg2 = _resolve_endpoint(cfg, backend)
    except RuntimeError as e:
        return {"ok": False, "error": str(e), "path": "grok", "telemetry": False}

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
        resp = _post_json(url, body, timeout=120.0, cfg=cfg2)
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
            "streamed": False,
        }
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError, RuntimeError) as e:
        return {"ok": False, "path": path, "model": model, "error": str(e), "telemetry": False, "backend": path}
