# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Local agent loop — strong lab-aware core + tools (incl. Clanker)."""

from __future__ import annotations

import json
import re
import time
from pathlib import Path
from typing import Any

from .history import build_resume_context
from .lab_context import system_prompt
from .llm import chat
from .sessions import pickup
from .tools import dispatch

_TOOL_RE = re.compile(
    r"<tool\s+name=\"([a-z_]+)\"\s*>\s*(\{.*?\})\s*</tool>",
    re.DOTALL | re.IGNORECASE,
)

_TOOLS_HINT = (
    "Tools (emit ONE block when needed):\n"
    '<tool name="clanker_music">{"state":"on"}</tool>\n'
    '<tool name="clanker_speak">{"text":"hello"}</tool>\n'
    '<tool name="clanker_instruct">{"prompt":"play music"}</tool>\n'
    '<tool name="clanker_status">{}</tool>\n'
    '<tool name="list_dir">{"path":"."}</tool>\n'
    '<tool name="read_file">{"path":"…"}</tool>\n'
    '<tool name="grep">{"pattern":"…","path":"."}</tool>\n'
    '<tool name="shell">{"command":"…"}</tool>\n'
)


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


def _wants_clanker(msg: str) -> bool:
    m = (msg or "").lower()
    return any(
        w in m
        for w in (
            "clanker",
            "vacuum",
            "roborock",
            "rockctl",
            "play the music",
            "play music",
            "dock",
            "start clean",
        )
    )


def run_agent(
    cfg: dict[str, Any],
    user_message: str,
    *,
    session_id: str | None = None,
    max_steps: int = 8,
    workspace: str = "/home/voldemar/Dev",
) -> dict[str, Any]:
    root = Path(workspace)
    sys = system_prompt(agent=True) + "\n" + _TOOLS_HINT
    messages: list[dict[str, str]] = [{"role": "system", "content": sys}]

    if session_id:
        pk = pickup(cfg["sessions"]["import_dir"], session_id, tail_chat=4)
        if pk.get("ok") and pk.get("path"):
            ctx = build_resume_context(Path(pk["path"]), tail=8)
            for m in ctx.get("messages") or []:
                messages.append(m)

    # Fast path: Clanker intents → tool without waiting for weak model
    if _wants_clanker(user_message):
        low = user_message.lower()
        if "music" in low and any(x in low for x in ("play", "start", "on", "enable")):
            result = dispatch("clanker_music", {"state": "on"}, root=root)
            if not result.get("ok"):
                result = dispatch("clanker_instruct", {"prompt": user_message}, root=root)
            return {
                "ok": True,
                "schema": "grokium.agent_run.v1",
                "final": _format_clanker_result("music_on", result),
                "steps": 1,
                "trace": [{"tool": "clanker_music", "result_ok": result.get("ok")}],
                "fast_path": "clanker",
                "telemetry": False,
                "ts": time.time(),
            }
        if "music" in low and any(x in low for x in ("stop", "off", "mute")):
            result = dispatch("clanker_music", {"state": "off"}, root=root)
            return {
                "ok": True,
                "final": _format_clanker_result("music_off", result),
                "steps": 1,
                "trace": [{"tool": "clanker_music", "result_ok": result.get("ok")}],
                "fast_path": "clanker",
                "telemetry": False,
                "ts": time.time(),
            }
        if any(x in low for x in ("say ", "speak", "tell clanker")):
            # extract speech text roughly
            text = user_message
            for sep in ("say ", "speak ", "tell clanker "):
                if sep in low:
                    text = user_message[low.index(sep) + len(sep) :].strip(" \"'")
                    break
            result = dispatch("clanker_speak", {"text": text or user_message}, root=root)
            return {
                "ok": True,
                "final": _format_clanker_result("speak", result),
                "steps": 1,
                "trace": [{"tool": "clanker_speak", "result_ok": result.get("ok")}],
                "fast_path": "clanker",
                "telemetry": False,
                "ts": time.time(),
            }
        # generic instruct
        result = dispatch("clanker_instruct", {"prompt": user_message}, root=root)
        return {
            "ok": True,
            "final": _format_clanker_result("instruct", result),
            "steps": 1,
            "trace": [{"tool": "clanker_instruct", "result_ok": result.get("ok")}],
            "fast_path": "clanker",
            "telemetry": False,
            "ts": time.time(),
        }

    messages.append({"role": "user", "content": user_message})
    trace: list[dict[str, Any]] = []
    final = ""

    for step in range(max_steps):
        r = chat(cfg, messages, prefer_local=True, max_tokens=700, temperature=0.15)
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
        result_s = json.dumps(result, default=str)
        if len(result_s) > 6000:
            result_s = result_s[:6000] + "…"
        trace.append({"step": step, "tool": name, "args": args, "result_ok": result.get("ok")})
        messages.append({"role": "assistant", "content": content})
        messages.append(
            {
                "role": "user",
                "content": f"Tool {name} result:\n{result_s}\nContinue. Another tool or final answer. Never invent mpd for Clanker.",
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


def _format_clanker_result(action: str, result: dict[str, Any]) -> str:
    ok = result.get("ok")
    if ok:
        return (
            f"## Clanker · {action}\n\n"
            f"OK — Roborock lab robot (not mpd).\n\n"
            f"```json\n{json.dumps(result, indent=2, default=str)[:1500]}\n```"
        )
    return (
        f"## Clanker · {action} failed\n\n"
        f"```json\n{json.dumps(result, indent=2, default=str)[:1500]}\n```\n\n"
        f"Check rockctl at ROCKCTL_URL / 192.168.8.209:8080."
    )


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
    messages = [
        {
            "role": "system",
            "content": "You are Grokium local resume. Be concise. Local only. Zero telemetry.",
        }
    ]
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
