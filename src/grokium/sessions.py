# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Import and pick up Grok Build sessions from ~/.grok/sessions — local only."""

from __future__ import annotations

import json
import os
import shutil
import time
from pathlib import Path
from typing import Any, Iterator
from urllib.parse import unquote


def _is_session_dir(p: Path) -> bool:
    if not p.is_dir():
        return False
    return (p / "summary.json").is_file() or (p / "chat_history.jsonl").is_file()


def iter_session_dirs(grok_sessions: Path) -> Iterator[Path]:
    if not grok_sessions.is_dir():
        return
    for root, dirs, _files in os.walk(grok_sessions):
        # skip deep compaction noise for discovery; still import full tree on copy
        rp = Path(root)
        if _is_session_dir(rp):
            yield rp
            dirs.clear()  # don't walk into session children as sessions


def read_summary(session_dir: Path) -> dict[str, Any]:
    sp = session_dir / "summary.json"
    if sp.is_file():
        try:
            return json.loads(sp.read_text(encoding="utf-8", errors="replace"))
        except json.JSONDecodeError:
            pass
    return {"info": {"id": session_dir.name}, "session_summary": ""}


def workspace_label(session_dir: Path, grok_root: Path) -> str:
    try:
        rel = session_dir.relative_to(grok_root)
        parts = rel.parts
        if parts:
            return unquote(parts[0])
    except ValueError:
        pass
    return unquote(session_dir.parent.name)


def import_all(
    grok_sessions: str | Path,
    import_dir: str | Path,
    *,
    copy: bool = False,
    copy_ids: list[str] | None = None,
) -> dict[str, Any]:
    """Index all Grok sessions. Full tree copy only when copy=True or id in copy_ids.

    Default is catalog + meta only (light). Pickup can still read source paths.
    """
    src = Path(grok_sessions).expanduser()
    dst_root = Path(import_dir)
    dst_root.mkdir(parents=True, exist_ok=True)
    index: list[dict[str, Any]] = []
    errors: list[str] = []
    want_copy = set(copy_ids or [])

    for sdir in iter_session_dirs(src):
        sid = sdir.name
        summary = read_summary(sdir)
        ws = workspace_label(sdir, src)
        target = dst_root / sid
        do_copy = copy or sid in want_copy
        meta = {
            "id": sid,
            "workspace": ws,
            "source": str(sdir),
            "import_path": str(target if do_copy else sdir),
            "copied": False,
            "title": summary.get("generated_title")
            or summary.get("session_summary")
            or sid,
            "num_messages": summary.get("num_messages"),
            "num_chat_messages": summary.get("num_chat_messages"),
            "updated_at": summary.get("updated_at") or summary.get("last_active_at"),
            "model": summary.get("current_model_id"),
        }
        try:
            if do_copy:
                if not target.exists():
                    shutil.copytree(
                        sdir,
                        target,
                        ignore=shutil.ignore_patterns("*.lock", "compaction"),
                        dirs_exist_ok=False,
                    )
                meta["import_path"] = str(target)
                meta["copied"] = True
            (dst_root / f"{sid}.meta.json").write_text(
                json.dumps(meta, indent=2), encoding="utf-8"
            )
            index.append(meta)
        except OSError as e:
            errors.append(f"{sid}: {e}")

    index.sort(key=lambda m: m.get("updated_at") or "", reverse=True)
    catalog = {
        "schema": "grokium.session_catalog.v1",
        "ts": time.time(),
        "count": len(index),
        "source": str(src),
        "import_dir": str(dst_root),
        "sessions": index,
        "errors": errors,
        "telemetry": False,
    }
    (dst_root / "CATALOG.json").write_text(json.dumps(catalog, indent=2), encoding="utf-8")
    return catalog


def pickup(
    import_dir: str | Path,
    session_id: str | None = None,
    *,
    tail_chat: int = 12,
) -> dict[str, Any]:
    """Load an imported (or still-on-disk) session for local resume — no cloud."""
    root = Path(import_dir)
    catalog_path = root / "CATALOG.json"
    sessions: list[dict[str, Any]] = []
    if catalog_path.is_file():
        try:
            sessions = json.loads(catalog_path.read_text(encoding="utf-8")).get("sessions") or []
        except json.JSONDecodeError:
            sessions = []

    chosen: dict[str, Any] | None = None
    if session_id:
        for s in sessions:
            if s.get("id") == session_id or session_id in (s.get("id") or ""):
                chosen = s
                break
        if not chosen:
            # direct path under import or absolute
            p = root / session_id
            if p.is_dir():
                chosen = {"id": session_id, "import_path": str(p), "source": str(p)}
    else:
        chosen = sessions[0] if sessions else None

    if not chosen:
        return {"ok": False, "error": "no_session", "hint": "run import-sessions first"}

    path = Path(chosen.get("import_path") or chosen.get("source") or "")
    if not path.is_dir():
        # fallback to source
        path = Path(chosen.get("source") or "")
    if not path.is_dir():
        return {"ok": False, "error": "missing_dir", "meta": chosen}

    summary = read_summary(path)
    chat_tail: list[Any] = []
    chat_path = path / "chat_history.jsonl"
    if chat_path.is_file():
        lines = chat_path.read_text(encoding="utf-8", errors="replace").splitlines()
        for line in lines[-tail_chat:]:
            line = line.strip()
            if not line:
                continue
            try:
                chat_tail.append(json.loads(line))
            except json.JSONDecodeError:
                chat_tail.append({"raw": line[:500]})

    # compact pickup digest for local llama (no off-box share of chat text)
    digest = {
        "ok": True,
        "schema": "grokium.pickup.v1",
        "id": chosen.get("id") or path.name,
        "title": summary.get("generated_title") or summary.get("session_summary"),
        "workspace": chosen.get("workspace"),
        "num_messages": summary.get("num_messages"),
        "num_chat_messages": summary.get("num_chat_messages"),
        "updated_at": summary.get("updated_at") or summary.get("last_active_at"),
        "path": str(path),
        "chat_tail_n": len(chat_tail),
        "chat_tail": chat_tail,
        "resume_hint": "local_only",
        "share": "state_matrix_only",
    }
    runs = root.parent / "runs" if root.name == "import" else root / "runs"
    # data/runs
    runs = Path(root).resolve().parent / "runs"
    runs.mkdir(parents=True, exist_ok=True)
    out = runs / f"pickup_{digest['id'][:12]}_{int(time.time())}.json"
    # store full pickup locally; matrix share stays bits-only
    out.write_text(json.dumps(digest, indent=2, default=str), encoding="utf-8")
    digest["saved"] = str(out)
    return digest


def search_sessions(
    import_dir: str | Path,
    query: str,
    *,
    limit: int = 30,
) -> dict[str, Any]:
    """Filter CATALOG by id/title/workspace substring (case-insensitive)."""
    root = Path(import_dir)
    cat_path = root / "CATALOG.json"
    if not cat_path.is_file():
        return {"ok": False, "error": "no_catalog", "matches": []}
    try:
        cat = json.loads(cat_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        return {"ok": False, "error": str(e), "matches": []}
    q = (query or "").strip().lower()
    sessions = cat.get("sessions") or []
    if not q:
        matches = sessions[:limit]
    else:
        matches = []
        for s in sessions:
            blob = " ".join(
                str(s.get(k) or "")
                for k in ("id", "title", "workspace", "model", "source")
            ).lower()
            if q in blob:
                matches.append(s)
            if len(matches) >= limit:
                break
    return {
        "ok": True,
        "query": query,
        "count": len(matches),
        "matches": matches,
        "catalog_total": cat.get("count"),
    }


def resolve_session_path(import_dir: str | Path, session_id: str) -> Path | None:
    root = Path(import_dir)
    meta = root / f"{session_id}.meta.json"
    if meta.is_file():
        try:
            m = json.loads(meta.read_text(encoding="utf-8"))
            for key in ("import_path", "source"):
                p = Path(m.get(key) or "")
                if p.is_dir():
                    return p
        except json.JSONDecodeError:
            pass
    for cand in (root / session_id,):
        if cand.is_dir():
            return cand
    return None
