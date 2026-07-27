# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Configurable models for llama.cpp and optional Grok backend."""

from __future__ import annotations

import json
import os
import tomllib
from pathlib import Path
from typing import Any

from .llm import probe_local

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MODELS_TOML = ROOT / "config" / "models.toml"
PREF_PATH = lambda root: Path(root) / "data" / "model_pref.json"

_RUNTIME_MODEL: str | None = None  # alias or id override


def models_toml_path(cfg: dict[str, Any] | None = None) -> Path:
    if cfg:
        p = (cfg.get("local") or {}).get("models_file")
        if p:
            return Path(p)
    env = os.environ.get("GROKIUM_MODELS_FILE")
    if env:
        return Path(env)
    return DEFAULT_MODELS_TOML


def load_catalog(cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    path = models_toml_path(cfg)
    data: dict[str, Any] = {"defaults": {}, "models": []}
    if path.is_file():
        with path.open("rb") as f:
            raw = tomllib.load(f)
        data["defaults"] = raw.get("defaults") or {}
        data["models"] = list(raw.get("models") or [])
    # merge [local.models] from main cfg if present
    if cfg:
        local = cfg.get("local") or {}
        extra = local.get("models")
        if isinstance(extra, list):
            data["models"].extend(extra)
        if local.get("model") and not data["defaults"].get("local_model"):
            data["defaults"]["local_model"] = local["model"]
        if local.get("gguf_dir"):
            data["defaults"]["gguf_dir"] = local["gguf_dir"]
    return data


def set_model(name: str) -> str:
    """Runtime select by alias or exact id."""
    global _RUNTIME_MODEL
    _RUNTIME_MODEL = (name or "").strip()
    if not _RUNTIME_MODEL:
        raise ValueError("empty model")
    return _RUNTIME_MODEL


def clear_model_override() -> None:
    global _RUNTIME_MODEL
    _RUNTIME_MODEL = None


def get_runtime_model() -> str | None:
    return _RUNTIME_MODEL


def persist_model(root: str | Path, name: str) -> Path:
    p = PREF_PATH(root)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(
        json.dumps({"schema": "grokium.model_pref.v1", "model": name, "telemetry": False}, indent=2),
        encoding="utf-8",
    )
    return p


def load_persisted_model(root: str | Path) -> str | None:
    p = PREF_PATH(root)
    if not p.is_file():
        return None
    try:
        return json.loads(p.read_text()).get("model")
    except Exception:
        return None


def list_live_server_models(cfg: dict[str, Any]) -> list[dict[str, Any]]:
    probe = probe_local(cfg)
    out: list[dict[str, Any]] = []
    if not probe.get("ok"):
        return out
    models = probe.get("models") or {}
    for row in models.get("data") or []:
        mid = row.get("id") or row.get("name") or ""
        out.append({"id": mid, "source": "llama_server", "raw": row})
    # ollama-style
    for row in models.get("models") or []:
        if isinstance(row, dict):
            mid = row.get("id") or row.get("name") or row.get("model") or ""
            if mid and not any(x["id"] == mid for x in out):
                out.append({"id": mid, "source": "llama_server", "raw": row})
    return out


def resolve_model_id(cfg: dict[str, Any], name: str | None = None) -> dict[str, Any]:
    """Resolve alias/id/env/pref to concrete model id + backend."""
    catalog = load_catalog(cfg)
    aliases = {m.get("alias"): m for m in catalog.get("models") or [] if m.get("alias")}
    by_id = {m.get("id"): m for m in catalog.get("models") or [] if m.get("id")}

    candidates: list[str] = []
    if name:
        candidates.append(name)
    if _RUNTIME_MODEL:
        candidates.append(_RUNTIME_MODEL)
    env = os.environ.get("GROKIUM_LOCAL_MODEL") or os.environ.get("GROKIUM_MODEL")
    if env:
        candidates.append(env)
    pref = load_persisted_model(cfg.get("_root") or ROOT)
    if pref:
        candidates.append(pref)
    local = cfg.get("local") or {}
    if local.get("model"):
        candidates.append(str(local["model"]))
    if catalog.get("defaults", {}).get("local_model"):
        candidates.append(str(catalog["defaults"]["local_model"]))
    candidates.append("local")

    chosen = None
    meta: dict[str, Any] = {}
    for c in candidates:
        if not c:
            continue
        if c in aliases:
            meta = dict(aliases[c])
            chosen = meta.get("id") or c
            break
        if c in by_id:
            meta = dict(by_id[c])
            chosen = c
            break
        # substring match on live server ids
        live = list_live_server_models(cfg)
        for row in live:
            if c == row["id"] or c in row["id"] or row["id"].endswith(c):
                chosen = row["id"]
                meta = {"alias": c, "id": chosen, "backend": "local", "label": chosen}
                break
        if chosen:
            break
        # treat as raw id
        chosen = c
        meta = {"id": c, "backend": "local" if c != "grok-4.5" else "grok", "alias": c}
        break

    backend = meta.get("backend") or "local"
    if chosen in ("grok", "grok-4.5") or backend == "grok":
        backend = "grok"
        auth = cfg.get("auth") or {}
        chosen = auth.get("model") or meta.get("id") or "grok-4.5"

    if backend == "local" and chosen in (None, "", "local"):
        live = list_live_server_models(cfg)
        if live:
            chosen = live[0]["id"]
            meta = {"alias": "local", "id": chosen, "backend": "local", "label": "server default"}

    return {
        "ok": bool(chosen),
        "id": chosen,
        "alias": meta.get("alias"),
        "label": meta.get("label") or chosen,
        "backend": backend,
        "notes": meta.get("notes"),
        "catalog_path": str(models_toml_path(cfg)),
    }


def list_all(cfg: dict[str, Any]) -> dict[str, Any]:
    from .llm import get_backend, get_failover, probe_local
    from .grok_auth import auth_status

    catalog = load_catalog(cfg)
    live = list_live_server_models(cfg)
    active = resolve_model_id(cfg)
    be = get_backend(cfg)
    loc = probe_local(cfg)
    ga = auth_status()
    return {
        "schema": "grokium.models.v1",
        "active": active,
        "active_backend": be,
        "failover": get_failover(cfg),
        "session_safe_switch": True,
        "backends": {
            "local": {
                "label": "llama.cpp",
                "ok": loc.get("ok"),
                "base_url": loc.get("base_url"),
                "live_models": [{"id": x["id"]} for x in live],
                "select": "/model local  or  /model <alias|gguf-id>",
            },
            "grok": {
                "label": "Grok cloud (opt-in; not xAI product)",
                "token_present": ga.get("token_present"),
                "source": ga.get("source"),
                "email": ga.get("email"),
                "select": "/model grok  (needs /login or API key)",
            },
        },
        "presets": catalog.get("models") or [],
        "defaults": catalog.get("defaults") or {},
        "live_server": [{"id": x["id"], "source": x["source"]} for x in live],
        "env": {
            "GROKIUM_LOCAL_MODEL": os.environ.get("GROKIUM_LOCAL_MODEL"),
            "GROKIUM_MODEL": os.environ.get("GROKIUM_MODEL"),
            "GROKIUM_FAILOVER": os.environ.get("GROKIUM_FAILOVER"),
        },
        "how": {
            "tui": "/model list · /model local|grok|<alias> · /failover auto|none",
            "note": "Switching model/backend does NOT clear the chat session.",
        },
    }
