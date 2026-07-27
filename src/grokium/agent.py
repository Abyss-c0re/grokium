# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Local agent loop — llama + tools, zero telemetry, local-only results."""

from __future__ import annotations

import json
import re
import time
from pathlib import Path
from typing import Any

from .history import build_resume_context, to_openai_messages, load_chat_rows
from .llm import chat
from .sessions import pickup, read_summary
from .tools import TOOL_SPECS, dispatch

# Simple XML tool protocol — works with models that ignore OpenAI tools API
_TOOL_RE = re.compile(
    r"<tool\s+name=\"([a-z_]+)\"\s*>\s*(\{.*?\})\s*</tool>",
    re.DOTALL | re.IGNORECASE,
)

SYSTEM = """You are Grokium, a local-first coding agent (zero telemetry).
You work only on this machine. Prefer small concrete steps.

When you need a tool, output EXACTLY one block:
<tool name="NAME">{"arg":"value"}</tool>
Available tools: list_dir, read_file, grep, shell.

Rules:
- shell: no network (curl/wget), no flash/fastboot/reboot
- After tool results arrive, continue until you can answer
- Final answer: plain text without tool tags
- Never invent tool results
"""


def _parse_tool(text: str) -> tuple[str, dict[str, Any]] | None:
    m = _TOOL_RE.search(text or "")
    if not m:
        return None
    name = m.group(1)
    try:
        args = json.loads(m.group(2))
    except json.JSONDecodeError:
        args = {}
    if not isinstance(args, dict):
        args = {}
    return name, args


def run_agent(
    cfg: dict[str, Any],
    user_message: str,
    *,
    session_id: str | None = None,
    max_steps: int = 6,
    workspace: str = "/home/voldemar/Dev",
) -> dict[str, Any]:
    root = Path(workspace)
    messages: list[dict[str, str]] = [{"role": "system", "content": SYSTEM}]

    if session_id:
        pk = pickup(cfg["sessions"]["import_dir"], session_id, tail_chat=4)
        if pk.get("ok") and pk.get("path"):
            ctx = build_resume_context(Path(pk["path"]), tail=10)
            # inject compact history
            for m in ctx.get("messages") or []:
                messages.append(m)
            messages.append(
                {
                    "role": "system",
                    "content": (
                        f"Resumed session {session_id} title={pk.get('title')}. "
                        f"Last user queries: {(ctx.get('user_queries') or [])[-3:]}"
                    )[:1500],
                }
            )

    messages.append({"role": "user", "content": user_message})
    trace: list[dict[str, Any]] = []
    final = ""

    for step in range(max_steps):
        r = chat(cfg, messages, prefer_local=True, max_tokens=512, temperature=0.2)
        if not r.get("ok"):
            return {
                "ok": False,
                "error": r.get("error"),
                "trace": trace,
                "step": step,
                "telemetry": False,
            }
        content = r.get("content") or ""
        trace.append({"step": step, "assistant": content[:4000], "usage": r.get("usage")})
        tool = _parse_tool(content)
        if not tool:
            final = content
            break
        name, args = tool
        result = dispatch(name, args, root=root)
        # truncate large results for context
        result_s = json.dumps(result, default=str)
        if len(result_s) > 6000:
            result_s = result_s[:6000] + "…"
        trace.append({"step": step, "tool": name, "args": args, "result_ok": result.get("ok")})
        messages.append({"role": "assistant", "content": content})
        messages.append(
            {
                "role": "user",
                "content": f"Tool {name} result:\n{result_s}\nContinue. Use another tool or give the final answer.",
            }
        )
    else:
        final = (trace[-1].get("assistant") if trace else "") or ""

    return {
        "ok": True,
        "schema": "grokium.agent_run.v1",
        "final": final,
        "steps": len(trace),
        "trace": trace,
        "session_id": session_id,
        "telemetry": False,
        "ts": time.time(),
    }


def resume_chat(
    cfg: dict[str, Any],
    session_id: str,
    user_message: str | None = None,
    *,
    max_tokens: int = 400,
) -> dict[str, Any]:
    """One-shot local completion with session context (no tool loop)."""
    pk = pickup(cfg["sessions"]["import_dir"], session_id, tail_chat=2)
    if not pk.get("ok"):
        return pk
    path = Path(pk["path"])
    ctx = build_resume_context(path, tail=14)
    messages = [{"role": "system", "content": "You are Grokium local resume. Be concise. Local only. Zero telemetry."}]
    messages.extend(ctx.get("messages") or [])
    um = user_message or (
        "Summarize where this session left off and the single next engineering step. "
        "One short paragraph."
    )
    messages.append({"role": "user", "content": um})
    r = chat(cfg, messages, prefer_local=True, max_tokens=max_tokens)
    return {
        "ok": bool(r.get("ok")),
        "schema": "grokium.resume_chat.v1",
        "session_id": session_id,
        "title": pk.get("title"),
        "context_messages": ctx.get("messages_n"),
        "user_queries_tail": (ctx.get("user_queries") or [])[-5:],
        "reply": r.get("content"),
        "error": r.get("error"),
        "usage": r.get("usage"),
        "path": "local",
        "telemetry": False,
    }
