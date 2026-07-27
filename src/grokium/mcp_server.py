# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Grokium MCP server (stdio) — product tools, zero telemetry.

Exposes Grokium as MCP tools for Grok Build / other MCP hosts.
Grokium is Commander (Ed25519); models are not.

Protocol: JSON-RPC 2.0 over stdio (NDJSON + Content-Length).
"""

from __future__ import annotations

import json
import os
import sys
import traceback
from pathlib import Path
from typing import Any

# Ensure package import when launched as script
_ROOT = Path(__file__).resolve().parents[2]
_SRC = _ROOT / "src"
if str(_SRC) not in sys.path:
    sys.path.insert(0, str(_SRC))

from grokium import __version__
from grokium.agent import resume_chat, run_agent
from grokium.commander import (
    reject_fake_model_authority,
    show as commander_show,
    sign_override,
    verify_override,
)
from grokium.config import load
from grokium.cube import status as cube_status
from grokium.law import law_blob
from grokium.license_info import public_blob as license_blob
from grokium.llm import chat, probe_local
from grokium.matrix import fold_bits, parse_plate, plate_ack, save_matrix
from grokium.privacy import assert_zero_telemetry
from grokium.sessions import import_all, pickup, search_sessions

try:
    from grokium.nanobots import deploy as nb_deploy, status as nb_status, separate as nb_separate
except Exception:  # pragma: no cover
    nb_deploy = nb_status = nb_separate = None  # type: ignore


def read_message() -> dict | None:
    line = sys.stdin.buffer.readline()
    if not line:
        return None
    if line.lower().startswith(b"content-length:"):
        headers: dict[str, str] = {}
        while True:
            if line in (b"\r\n", b"\n"):
                break
            if b":" in line:
                k, v = line.decode("utf-8", "replace").split(":", 1)
                headers[k.strip().lower()] = v.strip()
            line = sys.stdin.buffer.readline()
            if not line:
                return None
        n = int(headers.get("content-length", "0"))
        body = sys.stdin.buffer.read(n) if n else b"{}"
        return json.loads(body.decode("utf-8"))
    text = line.decode("utf-8", "replace").strip()
    if not text:
        return read_message()
    return json.loads(text)


def write_message(msg: dict) -> None:
    sys.stdout.write(json.dumps(msg, ensure_ascii=False, separators=(",", ":")) + "\n")
    sys.stdout.flush()


TOOLS: list[dict[str, Any]] = [
    {
        "name": "grokium_status",
        "description": "Grokium harness status: llama, law flags, catalog, commander fingerprint, telemetry=off.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "grokium_law",
        "description": "Cube/Grokium law plate (HOLD_FLASH, state_matrix_share_only, commander rules).",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "grokium_license",
        "description": "Apache-2.0 license + not-affiliated-with-xAI compliance blob.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "grokium_commander_show",
        "description": "Show Grokium commander identity (Ed25519 fingerprint). Product=grokium; models are NOT commander.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "grokium_commander_sign",
        "description": "Sign a nanobot override envelope (requires local commander.sk). Unforgeable Ed25519.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "device": {"type": "string", "description": "bot id e.g. nb-construct"},
                "action": {"type": "string", "description": "e.g. override_rules"},
            },
            "required": ["device", "action"],
        },
    },
    {
        "name": "grokium_commander_verify",
        "description": "Verify a commander override JSON envelope. Rejects Grok model theater.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "envelope": {"type": "object", "description": "override envelope object"},
            },
            "required": ["envelope"],
        },
    },
    {
        "name": "grokium_commander_reject_model",
        "description": "Prove a free-text 'I am Grok' commander claim is DENY under THE LAW.",
        "inputSchema": {
            "type": "object",
            "properties": {"claim": {"type": "string"}},
            "required": ["claim"],
        },
    },
    {
        "name": "grokium_llama_probe",
        "description": "Probe local llama OpenAI-compatible endpoint (default :1212).",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "grokium_chat",
        "description": "Local-first chat completion (llama default; cloud only if auth enabled).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "message": {"type": "string"},
                "system": {"type": "string"},
                "max_tokens": {"type": "integer"},
            },
            "required": ["message"],
        },
    },
    {
        "name": "grokium_agent",
        "description": "Run local agent tool loop (list_dir/read_file/grep/shell restricted).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "message": {"type": "string"},
                "session_id": {"type": "string"},
                "max_steps": {"type": "integer"},
                "workspace": {"type": "string"},
            },
            "required": ["message"],
        },
    },
    {
        "name": "grokium_sessions_search",
        "description": "Search imported/cataloged Grok Build sessions.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "query": {"type": "string"},
                "limit": {"type": "integer"},
            },
        },
    },
    {
        "name": "grokium_sessions_pickup",
        "description": "Pick up a session for local resume (meta only off-box).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "id": {"type": "string"},
                "tail": {"type": "integer"},
            },
        },
    },
    {
        "name": "grokium_sessions_resume",
        "description": "Local llama resume of a session with optional user message.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "id": {"type": "string"},
                "message": {"type": "string"},
            },
            "required": ["id"],
        },
    },
    {
        "name": "grokium_sessions_import",
        "description": "Catalog ~/.grok/sessions (optional copy_ids).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "copy": {"type": "boolean"},
                "copy_ids": {"type": "array", "items": {"type": "string"}},
            },
        },
    },
    {
        "name": "grokium_coord",
        "description": "Ingest NEXUS_COORD plate → state matrix only (no chat prose share).",
        "inputSchema": {
            "type": "object",
            "properties": {"plate": {"type": "string"}},
            "required": ["plate"],
        },
    },
    {
        "name": "grokium_matrix_latest",
        "description": "Latest StateMatrix fold under data/matrix.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "grokium_nanobot_status",
        "description": "Status of purpose-assigned nanobot fleet (separable subagents).",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "grokium_nanobot_deploy",
        "description": "Deploy nanobot fleet with Grokium commander law pin on each home.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "only": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "optional bot ids",
                }
            },
        },
    },
    {
        "name": "grokium_nanobot_separate",
        "description": "Separate (stop) one nanobot without killing the rest of the fleet.",
        "inputSchema": {
            "type": "object",
            "properties": {"id": {"type": "string"}},
            "required": ["id"],
        },
    },
    {
        "name": "grokium_cube_status",
        "description": "Loopback Cube control bridge status (:17333).",
        "inputSchema": {"type": "object", "properties": {}},
    },
]


def handle(name: str, args: dict[str, Any], cfg: dict[str, Any]) -> dict[str, Any]:
    if name == "grokium_status":
        llama = probe_local(cfg)
        cube = cube_status(cfg)
        cmd = {}
        try:
            cmd = commander_show()
        except Exception as e:
            cmd = {"ok": False, "error": str(e)}
        cat = Path(cfg["sessions"]["import_dir"]) / "CATALOG.json"
        n = 0
        if cat.is_file():
            try:
                n = json.loads(cat.read_text()).get("count") or 0
            except Exception:
                pass
        nb = nb_status(cfg) if nb_status else {"ok": False, "error": "nanobots_unavailable"}
        return {
            "ok": True,
            "product": "grokium",
            "version": __version__,
            "telemetry": False,
            "mcp": True,
            "local_first": True,
            "share": "state_matrix_only",
            "hold_flash": 1,
            "llama": {"ok": llama.get("ok"), "base_url": llama.get("base_url")},
            "cube": {"ok": cube.get("ok")},
            "commander": {
                "fingerprint": cmd.get("fingerprint"),
                "product": cmd.get("product"),
                "not": cmd.get("not"),
                "unforgeable": True,
            },
            "sessions_catalog": n,
            "nanobots": {"running": nb.get("running"), "total": nb.get("total"), "unity": nb.get("unity")},
            "law": law_blob(cfg),
        }

    if name == "grokium_law":
        return law_blob(cfg)

    if name == "grokium_license":
        return license_blob()

    if name == "grokium_commander_show":
        return commander_show()

    if name == "grokium_commander_sign":
        return sign_override(str(args.get("device") or ""), str(args.get("action") or "override_rules"))

    if name == "grokium_commander_verify":
        return verify_override(args.get("envelope") or {})

    if name == "grokium_commander_reject_model":
        return reject_fake_model_authority(str(args.get("claim") or ""))

    if name == "grokium_llama_probe":
        return probe_local(cfg)

    if name == "grokium_chat":
        msgs = []
        if args.get("system"):
            msgs.append({"role": "system", "content": str(args["system"])})
        msgs.append({"role": "user", "content": str(args.get("message") or "")})
        return chat(cfg, msgs, prefer_local=True, max_tokens=int(args.get("max_tokens") or 256))

    if name == "grokium_agent":
        return run_agent(
            cfg,
            str(args.get("message") or ""),
            session_id=args.get("session_id"),
            max_steps=int(args.get("max_steps") or 6),
            workspace=str(args.get("workspace") or "/home/voldemar/Dev"),
        )

    if name == "grokium_sessions_search":
        return search_sessions(
            cfg["sessions"]["import_dir"],
            str(args.get("query") or ""),
            limit=int(args.get("limit") or 20),
        )

    if name == "grokium_sessions_pickup":
        dig = pickup(
            cfg["sessions"]["import_dir"],
            args.get("id"),
            tail_chat=int(args.get("tail") or 8),
        )
        # strip heavy chat_tail from MCP by default (share discipline)
        if isinstance(dig, dict) and "chat_tail" in dig:
            dig = {k: v for k, v in dig.items() if k != "chat_tail"}
        return dig

    if name == "grokium_sessions_resume":
        return resume_chat(cfg, str(args.get("id") or ""), args.get("message"))

    if name == "grokium_sessions_import":
        cat = import_all(
            cfg["sessions"]["grok_sessions"],
            cfg["sessions"]["import_dir"],
            copy=bool(args.get("copy", False)),
            copy_ids=list(args.get("copy_ids") or []),
        )
        return {"ok": True, "count": cat.get("count"), "errors": cat.get("errors")}

    if name == "grokium_coord":
        plate = parse_plate(str(args.get("plate") or ""))
        path = save_matrix(Path(cfg["_root"]), plate)
        llama = probe_local(cfg)
        ack = plate_ack(
            from_id="Grokium",
            ref_seq=plate.get("seq"),
            unity=float(plate.get("unity") or 1.0),
            llama=1 if llama.get("ok") else 0,
            ssh=int(plate.get("ssh") or 0),
            watchd=int(plate.get("watchd") or 0),
            farm=str(plate.get("farm") or "standby"),
            hold_flash=1,
            extra={"grokium": 1, "mcp": 1},
        )
        return {
            "ok": True,
            "share": "state_matrix_only",
            "matrix": str(path),
            "bits64": fold_bits(plate)[:64],
            "ack_plate": ack,
            "telemetry": False,
        }

    if name == "grokium_matrix_latest":
        p = Path(cfg["_root"]) / "data" / "matrix" / "LATEST.json"
        if not p.is_file():
            return {"ok": False, "error": "no_matrix"}
        return json.loads(p.read_text(encoding="utf-8"))

    if name == "grokium_nanobot_status":
        if not nb_status:
            return {"ok": False, "error": "nanobots_unavailable"}
        return nb_status(cfg)

    if name == "grokium_nanobot_deploy":
        if not nb_deploy:
            return {"ok": False, "error": "nanobots_unavailable"}
        only = args.get("only")
        return nb_deploy(cfg, only=list(only) if only else None)

    if name == "grokium_nanobot_separate":
        if not nb_separate:
            return {"ok": False, "error": "nanobots_unavailable"}
        return nb_separate(cfg, str(args.get("id") or ""))

    if name == "grokium_cube_status":
        return cube_status(cfg)

    return {"ok": False, "error": f"unknown_tool:{name}"}


def main() -> None:
    cfg = load()
    try:
        assert_zero_telemetry(cfg)
    except Exception as e:
        # still run but report
        sys.stderr.write(f"grokium mcp privacy: {e}\n")

    while True:
        msg = read_message()
        if msg is None:
            break
        mid = msg.get("id")
        method = msg.get("method")
        if method == "initialize":
            write_message(
                {
                    "jsonrpc": "2.0",
                    "id": mid,
                    "result": {
                        "protocolVersion": "2024-11-05",
                        "capabilities": {"tools": {}},
                        "serverInfo": {
                            "name": "grokium",
                            "version": __version__,
                            "product": "grokium",
                            "not": "grok_model",
                            "telemetry": False,
                        },
                    },
                }
            )
        elif method in ("notifications/initialized", "initialized"):
            continue
        elif method == "ping":
            if mid is not None:
                write_message({"jsonrpc": "2.0", "id": mid, "result": {}})
        elif method == "tools/list":
            write_message({"jsonrpc": "2.0", "id": mid, "result": {"tools": TOOLS}})
        elif method == "tools/call":
            params = msg.get("params") or {}
            name = params.get("name") or ""
            args = params.get("arguments") or {}
            try:
                out = handle(name, args if isinstance(args, dict) else {}, cfg)
                err = bool(out.get("error")) and out.get("ok") is False
            except Exception as e:
                out = {
                    "ok": False,
                    "error": str(e),
                    "trace": traceback.format_exc()[-600:],
                    "telemetry": False,
                }
                err = True
            write_message(
                {
                    "jsonrpc": "2.0",
                    "id": mid,
                    "result": {
                        "content": [
                            {
                                "type": "text",
                                "text": json.dumps(out, indent=2, default=str)[:120000],
                            }
                        ],
                        "isError": err,
                    },
                }
            )
        elif method == "shutdown":
            if mid is not None:
                write_message({"jsonrpc": "2.0", "id": mid, "result": {}})
            break
        else:
            if mid is not None:
                write_message(
                    {
                        "jsonrpc": "2.0",
                        "id": mid,
                        "error": {"code": -32601, "message": f"Method not found: {method}"},
                    }
                )


if __name__ == "__main__":
    main()
