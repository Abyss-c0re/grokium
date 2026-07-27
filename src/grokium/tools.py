# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Local agent tools — workspace-scoped, no telemetry, no off-box share of results."""

from __future__ import annotations

import json
import os
import subprocess
import urllib.request
from pathlib import Path
from typing import Any, Callable

# Default workspace
DEFAULT_ROOT = Path("/home/voldemar/Dev")

# Deny path fragments for shell/read
_DENY = (
    "/.ssh/",
    "id_rsa",
    "id_ed25519",
    "auth.json",
    "private_key",
    "/etc/shadow",
)


def _safe_path(path: str, root: Path) -> Path | None:
    try:
        p = Path(path).expanduser()
        if not p.is_absolute():
            p = (root / p).resolve()
        else:
            p = p.resolve()
        root_r = root.resolve()
        # must stay under root or grokium or .grok sessions (read-only sessions ok)
        allowed_roots = [
            root_r,
            Path("/home/voldemar/Dev/grokium").resolve(),
            Path.home().joinpath(".grok").resolve(),
            Path("/tmp").resolve(),
        ]
        if not any(str(p).startswith(str(r) + os.sep) or p == r for r in allowed_roots):
            return None
        sp = str(p)
        if any(d in sp for d in _DENY):
            return None
        return p
    except OSError:
        return None


def tool_list_dir(path: str = ".", *, root: Path = DEFAULT_ROOT, limit: int = 80) -> dict[str, Any]:
    p = _safe_path(path, root)
    if not p or not p.is_dir():
        return {"ok": False, "error": "bad_dir", "path": path}
    entries = []
    try:
        for i, child in enumerate(sorted(p.iterdir(), key=lambda x: (not x.is_dir(), x.name.lower()))):
            if i >= limit:
                entries.append({"name": "…", "note": f"truncated at {limit}"})
                break
            entries.append(
                {
                    "name": child.name,
                    "type": "dir" if child.is_dir() else "file",
                    "size": child.stat().st_size if child.is_file() else None,
                }
            )
    except OSError as e:
        return {"ok": False, "error": str(e)}
    return {"ok": True, "path": str(p), "entries": entries}


def tool_read_file(path: str, *, root: Path = DEFAULT_ROOT, max_bytes: int = 48_000) -> dict[str, Any]:
    p = _safe_path(path, root)
    if not p or not p.is_file():
        return {"ok": False, "error": "bad_file", "path": path}
    try:
        data = p.read_bytes()[:max_bytes]
        text = data.decode("utf-8", errors="replace")
        return {
            "ok": True,
            "path": str(p),
            "bytes": len(data),
            "truncated": p.stat().st_size > max_bytes,
            "content": text,
        }
    except OSError as e:
        return {"ok": False, "error": str(e)}


def tool_grep(pattern: str, path: str = ".", *, root: Path = DEFAULT_ROOT, max_hits: int = 40) -> dict[str, Any]:
    p = _safe_path(path, root)
    if not p:
        return {"ok": False, "error": "bad_path"}
    try:
        r = subprocess.run(
            ["rg", "-n", "--max-count", "5", "-m", str(max_hits), "-S", pattern, str(p)],
            capture_output=True,
            text=True,
            timeout=15,
        )
        lines = (r.stdout or "").splitlines()[:max_hits]
        return {"ok": True, "hits": lines, "count": len(lines), "returncode": r.returncode}
    except FileNotFoundError:
        # fallback grep
        try:
            r = subprocess.run(
                ["grep", "-RIn", "--max-count=5", pattern, str(p)],
                capture_output=True,
                text=True,
                timeout=15,
            )
            lines = (r.stdout or "").splitlines()[:max_hits]
            return {"ok": True, "hits": lines, "count": len(lines), "engine": "grep"}
        except Exception as e:
            return {"ok": False, "error": str(e)}
    except Exception as e:
        return {"ok": False, "error": str(e)}


_SHELL_DENY = (
    "rm -rf /",
    "mkfs",
    "dd if=",
    ":(){",
    "shutdown",
    "reboot",
    "fastboot",
    "flash",
    "curl ",
    "wget ",
    "nc ",
    "ncat ",
)


def tool_shell(command: str, *, root: Path = DEFAULT_ROOT, timeout: int = 30) -> dict[str, Any]:
    """Restricted local shell — cwd under workspace, no network tools, no flash."""
    cmd = (command or "").strip()
    if not cmd:
        return {"ok": False, "error": "empty"}
    low = cmd.lower()
    for d in _SHELL_DENY:
        if d in low:
            return {"ok": False, "error": f"denied_pattern:{d.strip()}"}
    try:
        r = subprocess.run(
            ["bash", "-lc", cmd],
            cwd=str(root),
            capture_output=True,
            text=True,
            timeout=timeout,
            env={**os.environ, "GROKIUM": "1"},
        )
        out = (r.stdout or "")[-8000:]
        err = (r.stderr or "")[-2000:]
        return {
            "ok": r.returncode == 0,
            "returncode": r.returncode,
            "stdout": out,
            "stderr": err,
        }
    except subprocess.TimeoutExpired:
        return {"ok": False, "error": "timeout"}
    except OSError as e:
        return {"ok": False, "error": str(e)}



def _rockctl_base() -> str:
    return (
        os.environ.get("ROCKCTL_URL")
        or os.environ.get("CLANKER_ROCKCTL")
        or "http://192.168.8.209:8080"
    ).rstrip("/")


def _http_json(method: str, url: str, body: dict | None = None, timeout: float = 20.0) -> dict[str, Any]:
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Content-Type", "application/json")
    req.add_header("User-Agent", "grokium/0.5 (lab; not-xai)")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode()
            try:
                return json.loads(raw)
            except json.JSONDecodeError:
                return {"ok": True, "raw": raw[:2000]}
    except Exception as e:
        return {"ok": False, "error": str(e), "url": url}


def tool_clanker_status() -> dict[str, Any]:
    base = _rockctl_base()
    r = _http_json("GET", f"{base}/api/v1/status")
    r["device"] = "clanker_roborock"
    r["hint"] = "vacuum robot — not mpd"
    return r


def tool_clanker_music(state: str = "on") -> dict[str, Any]:
    """Robot music feature (rockctl), not host mpd."""
    base = _rockctl_base()
    st = (state or "on").strip().lower()
    if st in ("on", "1", "true", "play", "start"):
        # try common rockctl music endpoints
        for path in ("/api/v1/music/on", "/api/v1/assist/music", "/api/v1/music"):
            r = _http_json("POST", f"{base}{path}", {"state": "on", "action": "on"})
            if r.get("ok") is not False and "error" not in r:
                return {"ok": True, "action": "music_on", "via": path, "result": r}
        # instruct script fallback
        return tool_clanker_instruct("play music")
    if st in ("off", "0", "false", "stop"):
        for path in ("/api/v1/music/off", "/api/v1/assist/music", "/api/v1/music"):
            r = _http_json("POST", f"{base}{path}", {"state": "off", "action": "off"})
            if r.get("ok") is not False and "error" not in r:
                return {"ok": True, "action": "music_off", "via": path, "result": r}
        return tool_clanker_instruct("stop music")
    return tool_clanker_instruct(f"music {st}")


def tool_clanker_speak(text: str, volume: int | None = None) -> dict[str, Any]:
    base = _rockctl_base()
    body: dict[str, Any] = {"text": text or ""}
    if volume is not None:
        body["volume"] = volume
    r = _http_json("POST", f"{base}/api/v1/assist/speak", body)
    if r.get("ok") is False or r.get("error"):
        r2 = _http_json("POST", f"{base}/api/v1/speak/test", body)
        return {"ok": not r2.get("error"), "action": "speak", "result": r2, "fallback": True}
    return {"ok": True, "action": "speak", "result": r}


def tool_clanker_instruct(prompt: str) -> dict[str, Any]:
    """NL router script used by the lab."""
    script = Path("/home/voldemar/Dev/clanker/scripts/clanker_instruct.py")
    if not script.is_file():
        script = Path("/home/voldemar/Dev/Clanker/scripts/clanker_instruct.py")
    if not script.is_file():
        return {"ok": False, "error": "clanker_instruct.py not found"}
    try:
        r = subprocess.run(
            ["python3", str(script), prompt],
            capture_output=True,
            text=True,
            timeout=60,
            cwd=str(script.parent.parent),
        )
        out = (r.stdout or "")[-4000:]
        err = (r.stderr or "")[-1000:]
        return {
            "ok": r.returncode == 0,
            "returncode": r.returncode,
            "stdout": out,
            "stderr": err,
            "prompt": prompt,
        }
    except Exception as e:
        return {"ok": False, "error": str(e)}



TOOL_SPECS = [
    {
        "type": "function",
        "function": {
            "name": "list_dir",
            "description": "List directory under workspace",
            "parameters": {
                "type": "object",
                "properties": {"path": {"type": "string"}},
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "read_file",
            "description": "Read a text file under allowed roots",
            "parameters": {
                "type": "object",
                "properties": {"path": {"type": "string"}},
                "required": ["path"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "grep",
            "description": "Search files with ripgrep/grep",
            "parameters": {
                "type": "object",
                "properties": {
                    "pattern": {"type": "string"},
                    "path": {"type": "string"},
                },
                "required": ["pattern"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "shell",
            "description": "Run a restricted local bash command (no network/flash)",
            "parameters": {
                "type": "object",
                "properties": {"command": {"type": "string"}},
                "required": ["command"],
            },
        },
    },
]


def dispatch(name: str, args: dict[str, Any], *, root: Path = DEFAULT_ROOT) -> dict[str, Any]:
    if name == "list_dir":
        return tool_list_dir(args.get("path") or ".", root=root)
    if name == "read_file":
        return tool_read_file(args.get("path") or "", root=root)
    if name == "grep":
        return tool_grep(args.get("pattern") or "", args.get("path") or ".", root=root)
    if name == "shell":
        return tool_shell(args.get("command") or "", root=root)
    if name == "clanker_status":
        return tool_clanker_status()
    if name == "clanker_music":
        return tool_clanker_music(str(args.get("state") or "on"))
    if name == "clanker_speak":
        return tool_clanker_speak(str(args.get("text") or ""), args.get("volume"))
    if name == "clanker_instruct":
        return tool_clanker_instruct(str(args.get("prompt") or args.get("command") or ""))
    return {"ok": False, "error": f"unknown_tool:{name}"}
