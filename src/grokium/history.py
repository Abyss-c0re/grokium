# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Normalize Grok Build chat_history.jsonl → OpenAI-style messages for local resume."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

_USER_QUERY = re.compile(r"<user_query>\s*(.*?)\s*</user_query>", re.DOTALL | re.IGNORECASE)


def _flatten_content(content: Any) -> str:
    if content is None:
        return ""
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        parts: list[str] = []
        for p in content:
            if isinstance(p, dict):
                if p.get("type") == "text" or "text" in p:
                    parts.append(str(p.get("text") or ""))
                elif "content" in p:
                    parts.append(_flatten_content(p.get("content")))
                else:
                    parts.append(json.dumps(p, ensure_ascii=False)[:400])
            else:
                parts.append(str(p))
        return "\n".join(x for x in parts if x)
    if isinstance(content, dict):
        return _flatten_content(content.get("text") or content.get("content") or str(content))
    return str(content)


def extract_user_query(text: str) -> str:
    m = _USER_QUERY.search(text or "")
    if m:
        return m.group(1).strip()
    return (text or "").strip()


def load_chat_rows(session_dir: Path) -> list[dict[str, Any]]:
    path = session_dir / "chat_history.jsonl"
    if not path.is_file():
        return []
    rows: list[dict[str, Any]] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return rows


def to_openai_messages(
    rows: list[dict[str, Any]],
    *,
    max_messages: int = 24,
    include_tools: bool = False,
    include_system: bool = False,
    max_chars: int = 12000,
) -> list[dict[str, str]]:
    """Convert Grok history types into role/content pairs for local llama."""
    out: list[dict[str, str]] = []
    for row in rows:
        t = row.get("type") or row.get("role") or ""
        if t == "system":
            if not include_system:
                continue
            text = _flatten_content(row.get("content"))
            if text:
                out.append({"role": "system", "content": text[:4000]})
            continue
        if t == "user":
            text = _flatten_content(row.get("content"))
            # prefer explicit user_query blocks when present
            uq = extract_user_query(text)
            # skip pure compaction meta without query
            if row.get("synthetic_reason") == "compaction_meta" and "<user_query>" not in text:
                # still keep short workspace note truncated
                if len(text) > 800:
                    text = text[:400] + "\n…\n" + text[-200:]
            else:
                text = uq or text
            if text.strip():
                out.append({"role": "user", "content": text.strip()[:4000]})
            continue
        if t == "assistant":
            text = _flatten_content(row.get("content"))
            tools = row.get("tool_calls") or []
            if tools and include_tools:
                names = ", ".join(
                    (tc.get("name") or "?") for tc in tools if isinstance(tc, dict)
                )
                text = (text + f"\n[tools: {names}]").strip()
            elif tools and not text:
                names = ", ".join(
                    (tc.get("name") or "?") for tc in tools if isinstance(tc, dict)
                )
                text = f"[called tools: {names}]"
            if text.strip():
                out.append({"role": "assistant", "content": text.strip()[:4000]})
            continue
        if t == "reasoning":
            # compact reasoning summary only (local) — never encrypted blobs off-box
            summary = row.get("summary") or []
            bits = []
            for s in summary:
                if isinstance(s, dict) and s.get("text"):
                    bits.append(str(s["text"]))
            if bits and include_tools:
                out.append({"role": "assistant", "content": "[reason] " + " ".join(bits)[:1500]})
            continue
        if t == "tool_result" and include_tools:
            content = _flatten_content(row.get("content"))[:1500]
            out.append({"role": "user", "content": f"[tool_result] {content}"})
            continue

    if max_messages and len(out) > max_messages:
        out = out[-max_messages:]

    # enforce total char budget from the end
    total = 0
    kept: list[dict[str, str]] = []
    for m in reversed(out):
        c = len(m.get("content") or "")
        if total + c > max_chars and kept:
            break
        kept.append(m)
        total += c
    kept.reverse()
    return kept


def user_queries(rows: list[dict[str, Any]], limit: int = 20) -> list[str]:
    qs: list[str] = []
    for row in rows:
        if row.get("type") != "user":
            continue
        text = _flatten_content(row.get("content"))
        uq = extract_user_query(text)
        if uq and uq not in qs:
            qs.append(uq)
    return qs[-limit:]


def build_resume_context(session_dir: Path, *, tail: int = 16) -> dict[str, Any]:
    rows = load_chat_rows(session_dir)
    messages = to_openai_messages(rows, max_messages=tail, include_tools=False)
    queries = user_queries(rows, limit=12)
    return {
        "schema": "grokium.resume_context.v1",
        "path": str(session_dir),
        "rows": len(rows),
        "messages_n": len(messages),
        "messages": messages,
        "user_queries": queries,
        "last_user": queries[-1] if queries else None,
    }
