# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""LLM client: local llama.cpp | opt-in Grok. Streaming, failover, session-safe switch."""

from __future__ import annotations

import json
import os
import urllib.error
import urllib.request
from typing import Any, Callable

from .privacy import guard_url

_RUNTIME_BACKEND: str | None = None
# none | local_then_grok | grok_then_local | auto
_RUNTIME_FAILOVER: str | None = None


def set_backend(name: str) -> str:
    """Switch active backend. Does not clear chat — session continuity is caller's history."""
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


def set_failover(mode: str) -> str:
    """Backup when primary fails: none|auto|local_then_grok|grok_then_local."""
    global _RUNTIME_FAILOVER
    m = (mode or "none").strip().lower().replace("-", "_")
    aliases = {
        "off": "none",
        "no": "none",
        "disabled": "none",
        "local": "local_then_grok",
        "llama": "local_then_grok",
        "llama_then_grok": "local_then_grok",
        "grok": "grok_then_local",
        "cloud_then_local": "grok_then_local",
        "both": "auto",
        "backup": "auto",
        "on": "auto",
    }
    m = aliases.get(m, m)
    if m not in ("none", "local_then_grok", "grok_then_local", "auto"):
        raise ValueError("failover: none|auto|local_then_grok|grok_then_local")
    _RUNTIME_FAILOVER = m
    return m


def get_failover(cfg: dict[str, Any] | None = None) -> str:
    if _RUNTIME_FAILOVER:
        return _RUNTIME_FAILOVER
    if cfg:
        f = (cfg.get("local") or {}).get("failover") or (cfg.get("auth") or {}).get("failover")
        if f:
            return str(f).strip().lower().replace("-", "_")
    return (os.environ.get("GROKIUM_FAILOVER") or "auto").strip().lower().replace("-", "_")


def _failover_order(cfg: dict[str, Any], primary: str | None) -> list[str]:
    prim = primary or get_backend(cfg)
    if prim not in ("local", "grok"):
        prim = "local"
    mode = get_failover(cfg)
    if mode in ("none", "off", "disabled"):
        return [prim]
    if mode == "local_then_grok":
        return ["local", "grok"]
    if mode == "grok_then_local":
        return ["grok", "local"]
    other = "grok" if prim == "local" else "local"
    return [prim, other]


def _auth_header(url: str) -> dict[str, str]:
    from .grok_auth import bearer_headers

    if "grok.com" in url or "x.ai" in url or "cli-chat-proxy" in url:
        return bearer_headers()
    return {"Content-Type": "application/json", "User-Agent": "grokium/0.5 (zero-telemetry; not-xai)"}


def _resolve_endpoint(cfg: dict[str, Any], backend: str | None) -> tuple[str, str, str, dict]:
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
                or "Grok backend needs token: /login or GROK_API_KEY"
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
        "failover": get_failover(cfg),
        "failover_order": _failover_order(cfg, b),
        "stream": True,
        "session_safe_switch": True,
        "switch": "/model list · /model local|grok · /failover auto|none",
        "telemetry": False,
    }


def _extract_delta(chunk: dict[str, Any], *, last_full: str = "") -> tuple[str, str]:
    choices = chunk.get("choices") or []
    if not choices:
        return "", last_full
    c0 = choices[0]
    delta = c0.get("delta") or {}
    if delta.get("content"):
        return str(delta["content"]), last_full
    msg = c0.get("message") or {}
    if msg.get("content"):
        full = str(msg["content"])
        if last_full and full.startswith(last_full):
            return full[len(last_full) :], full
        if full == last_full:
            return "", full
        return full, full
    if c0.get("text"):
        return str(c0["text"]), last_full
    return "", last_full


def _chat_one(
    cfg: dict[str, Any],
    messages: list[dict[str, str]],
    *,
    max_tokens: int = 256,
    temperature: float = 0.2,
    backend: str | None = None,
) -> dict[str, Any]:
    try:
        path, base, model, cfg2 = _resolve_endpoint(cfg, backend)
    except RuntimeError as e:
        return {"ok": False, "error": str(e), "path": backend or "local", "telemetry": False}

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
        return {
            "ok": False,
            "path": path,
            "model": model,
            "error": str(e),
            "telemetry": False,
            "backend": path,
        }


def _chat_stream_one(
    cfg: dict[str, Any],
    messages: list[dict[str, str]],
    *,
    max_tokens: int = 512,
    temperature: float = 0.2,
    backend: str | None = None,
    on_token: Callable[[str], None] | None = None,
) -> dict[str, Any]:
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
    chunks_ok = 0
    last_full = ""
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
                chunks_ok += 1
                delta, last_full = _extract_delta(chunk, last_full=last_full)
                if delta:
                    parts.append(delta)
                    if on_token:
                        on_token(delta)
        content = "".join(parts).strip()
        if not content and chunks_ok == 0:
            r = _chat_one(cfg, messages, max_tokens=max_tokens, temperature=temperature, backend=path)
            if r.get("ok") and on_token and r.get("content"):
                c = r["content"]
                step = max(8, len(c) // 40)
                for i in range(0, len(c), step):
                    on_token(c[i : i + step])
            return {**r, "streamed": False, "fallback": "empty_stream"}
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
        err = str(e).lower()
        stream_reject = any(
            s in err
            for s in ("stream", "event-stream", "unsupported", "not supported", "400", "invalid")
        )
        if stream_reject and not parts:
            r = _chat_one(cfg, messages, max_tokens=max_tokens, temperature=temperature, backend=path)
            if r.get("ok") and on_token and r.get("content"):
                c = r["content"]
                step = max(8, len(c) // 40)
                for i in range(0, len(c), step):
                    on_token(c[i : i + step])
            return {**r, "streamed": False, "fallback": str(e)}
        return {
            "ok": False,
            "path": path,
            "error": str(e),
            "telemetry": False,
            "partial": "".join(parts),
        }


def chat_stream(
    cfg: dict[str, Any],
    messages: list[dict[str, str]],
    *,
    max_tokens: int = 512,
    temperature: float = 0.2,
    backend: str | None = None,
    on_token: Callable[[str], None] | None = None,
    allow_failover: bool = True,
) -> dict[str, Any]:
    """Stream with optional backup backend. Session history is the caller's messages."""
    order = _failover_order(cfg, backend) if allow_failover else [backend or get_backend(cfg)]
    seen: set[str] = set()
    order = [x for x in order if not (x in seen or seen.add(x))]
    errors: list[dict[str, Any]] = []
    for i, be in enumerate(order):
        # Only stream tokens to UI on first attempt; on failover, clear note then stream
        def _tok(d: str, _i=i) -> None:
            if on_token:
                on_token(d)

        if i > 0 and on_token:
            on_token(f"\n\n‹failover → {be}›\n\n")
        r = _chat_stream_one(
            cfg, messages, max_tokens=max_tokens, temperature=temperature, backend=be, on_token=_tok
        )
        if r.get("ok"):
            if i > 0:
                r["failover_used"] = True
                r["failover_from"] = order[0]
                r["failover_to"] = be
            r["failover_order"] = order
            return r
        errors.append({"backend": be, "error": r.get("error")})
        # if partial tokens already shown, do not silently continue (would mix answers)
        if r.get("partial"):
            r["failover_errors"] = errors
            return r
        if not allow_failover:
            return r
    return {
        "ok": False,
        "error": f"all backends failed: {errors}",
        "path": order[0] if order else "none",
        "failover_errors": errors,
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
    allow_failover: bool = True,
) -> dict[str, Any]:
    if backend is None:
        if prefer_local is False:
            backend = "grok"
        elif prefer_local is True:
            backend = "local"
    order = _failover_order(cfg, backend) if allow_failover else [backend or get_backend(cfg)]
    seen: set[str] = set()
    order = [x for x in order if not (x in seen or seen.add(x))]
    errors: list[dict[str, Any]] = []
    for i, be in enumerate(order):
        r = _chat_one(cfg, messages, max_tokens=max_tokens, temperature=temperature, backend=be)
        if r.get("ok"):
            if i > 0:
                r["failover_used"] = True
                r["failover_from"] = order[0]
                r["failover_to"] = be
            r["failover_order"] = order
            return r
        errors.append({"backend": be, "error": r.get("error")})
        if not allow_failover:
            return r
    return {
        "ok": False,
        "error": f"all backends failed: {errors}",
        "path": order[0] if order else "none",
        "failover_errors": errors,
        "telemetry": False,
    }
