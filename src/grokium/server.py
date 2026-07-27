# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Minimal local HTTP control plane — 127.0.0.1 only, zero telemetry."""

from __future__ import annotations

import json
import traceback
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse

from . import __version__
from .agent import resume_chat, run_agent
from .config import load
from .cube import status as cube_status
from .history import build_resume_context
from .llm import chat, probe_local
from .matrix import fold_bits, parse_plate, plate_ack, save_matrix
from .privacy import assert_zero_telemetry
from .license_info import public_blob as license_blob, verify_files_present
from .sessions import import_all, pickup, resolve_session_path, search_sessions


def _json_response(handler: BaseHTTPRequestHandler, code: int, obj: Any) -> None:
    body = json.dumps(obj, default=str).encode()
    handler.send_response(code)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(body)))
    handler.send_header("Cache-Control", "no-store")
    handler.send_header("X-Grokium-Telemetry", "off")
    handler.end_headers()
    handler.wfile.write(body)


def make_handler(cfg: dict[str, Any]):
    class H(BaseHTTPRequestHandler):
        def log_message(self, fmt: str, *args) -> None:
            pass

        def _read_json(self) -> dict[str, Any]:
            n = int(self.headers.get("Content-Length") or 0)
            if n <= 0:
                return {}
            raw = self.rfile.read(n)
            try:
                return json.loads(raw.decode())
            except json.JSONDecodeError:
                return {"_raw": raw.decode(errors="replace")[:2000]}

        def do_GET(self) -> None:  # noqa: N802
            try:
                self._dispatch_get()
            except Exception as e:
                _json_response(self, 500, {"ok": False, "error": str(e)})

        def do_POST(self) -> None:  # noqa: N802
            try:
                self._dispatch_post()
            except Exception as e:
                _json_response(
                    self,
                    500,
                    {"ok": False, "error": str(e), "trace": traceback.format_exc()[-800:]},
                )

        def _dispatch_get(self) -> None:
            u = urlparse(self.path)
            path = u.path.rstrip("/") or "/"
            q = parse_qs(u.query)

            if path in ("/", "/v1/status"):
                llama = probe_local(cfg)
                cube = cube_status(cfg) if (cfg.get("cube") or {}).get("enabled") else {"ok": False}
                cat = Path(cfg["sessions"]["import_dir"]) / "CATALOG.json"
                n_sess = 0
                if cat.is_file():
                    try:
                        n_sess = int(json.loads(cat.read_text()).get("count") or 0)
                    except Exception:
                        pass
                _json_response(
                    self,
                    200,
                    {
                        "ok": True,
                        "product": "grokium",
                        "version": __version__,
                        "telemetry": False,
                        "auth_cloud": bool((cfg.get("auth") or {}).get("enabled")),
                        "local_first": True,
                        "capabilities": [
                            "status",
                            "llama_chat",
                            "session_import",
                            "session_search",
                            "session_pickup",
                            "session_resume",
                            "agent_tools",
                            "nexus_coord_matrix",
                            "cube_bridge",
                        ],
                        "llama": {"ok": llama.get("ok"), "base_url": llama.get("base_url")},
                        "cube": {"ok": cube.get("ok")},
                        "sessions_catalog": n_sess,
                        "share": "state_matrix_only",
                        "hold_flash": 1,
                        "license": license_blob(),
                    },
                )
                return

            if path == "/v1/license":
                _json_response(self, 200, license_blob())
                return

            if path == "/v1/llama/probe":
                _json_response(self, 200, probe_local(cfg))
                return

            if path == "/v1/cube/status":
                _json_response(self, 200, cube_status(cfg))
                return

            if path == "/v1/sessions":
                cat = Path(cfg["sessions"]["import_dir"]) / "CATALOG.json"
                if cat.is_file():
                    data = json.loads(cat.read_text(encoding="utf-8"))
                    # optional limit
                    lim = int((q.get("limit") or ["50"])[0])
                    data = dict(data)
                    data["sessions"] = (data.get("sessions") or [])[:lim]
                    _json_response(self, 200, data)
                else:
                    _json_response(self, 200, {"ok": True, "count": 0, "sessions": []})
                return

            if path == "/v1/sessions/search":
                query = (q.get("q") or q.get("query") or [""])[0]
                _json_response(
                    self,
                    200,
                    search_sessions(cfg["sessions"]["import_dir"], query, limit=int((q.get("limit") or ["30"])[0])),
                )
                return

            if path == "/v1/matrix/latest":
                p = Path(cfg["_root"]) / "data" / "matrix" / "LATEST.json"
                if p.is_file():
                    _json_response(self, 200, json.loads(p.read_text(encoding="utf-8")))
                else:
                    _json_response(self, 404, {"ok": False, "error": "no_matrix"})
                return

            if path.startswith("/v1/sessions/pickup"):
                sid = (q.get("id") or [None])[0]
                _json_response(
                    self,
                    200,
                    pickup(cfg["sessions"]["import_dir"], sid, tail_chat=int((q.get("tail") or ["8"])[0])),
                )
                return

            if path.startswith("/v1/sessions/context"):
                sid = (q.get("id") or [None])[0]
                if not sid:
                    _json_response(self, 400, {"ok": False, "error": "id_required"})
                    return
                sp = resolve_session_path(cfg["sessions"]["import_dir"], sid)
                if not sp:
                    pk = pickup(cfg["sessions"]["import_dir"], sid, tail_chat=0)
                    sp = Path(pk["path"]) if pk.get("path") else None
                if not sp or not Path(sp).is_dir():
                    _json_response(self, 404, {"ok": False, "error": "session_not_found"})
                    return
                _json_response(self, 200, build_resume_context(Path(sp), tail=int((q.get("tail") or ["16"])[0])))
                return

            if path == "/healthz":
                _json_response(self, 200, {"ok": True})
                return

            if path in ("/ui", "/index.html") or path.startswith("/ui"):
                www = Path(cfg["_root"]) / "www" / "index.html"
                if www.is_file():
                    data = www.read_bytes()
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html; charset=utf-8")
                    self.send_header("Content-Length", str(len(data)))
                    self.send_header("X-Grokium-Telemetry", "off")
                    self.end_headers()
                    self.wfile.write(data)
                    return

            _json_response(self, 404, {"ok": False, "error": "not_found", "path": path})

        def _dispatch_post(self) -> None:
            u = urlparse(self.path)
            path = u.path.rstrip("/") or "/"
            body = self._read_json()

            if path == "/v1/sessions/import":
                cat = import_all(
                    cfg["sessions"]["grok_sessions"],
                    cfg["sessions"]["import_dir"],
                    copy=bool(body.get("copy", False)),
                    copy_ids=list(body.get("copy_ids") or body.get("copy_id") or [] or []),
                )
                # normalize copy_ids if string
                _json_response(self, 200, {"ok": True, "count": cat.get("count"), "errors": cat.get("errors")})
                return

            if path == "/v1/sessions/pickup":
                _json_response(
                    self,
                    200,
                    pickup(
                        cfg["sessions"]["import_dir"],
                        body.get("id") or body.get("session_id"),
                        tail_chat=int(body.get("tail") or 12),
                    ),
                )
                return

            if path == "/v1/sessions/search":
                _json_response(
                    self,
                    200,
                    search_sessions(
                        cfg["sessions"]["import_dir"],
                        body.get("q") or body.get("query") or "",
                        limit=int(body.get("limit") or 30),
                    ),
                )
                return

            if path == "/v1/sessions/resume":
                sid = body.get("id") or body.get("session_id")
                if not sid:
                    _json_response(self, 400, {"ok": False, "error": "id_required"})
                    return
                _json_response(
                    self,
                    200,
                    resume_chat(cfg, sid, body.get("message"), max_tokens=int(body.get("max_tokens") or 400)),
                )
                return

            if path == "/v1/agent":
                msg = body.get("message") or body.get("prompt") or ""
                if not msg:
                    _json_response(self, 400, {"ok": False, "error": "message_required"})
                    return
                _json_response(
                    self,
                    200,
                    run_agent(
                        cfg,
                        msg,
                        session_id=body.get("session_id") or body.get("id"),
                        max_steps=int(body.get("max_steps") or 6),
                        workspace=body.get("workspace") or "/home/voldemar/Dev",
                    ),
                )
                return

            if path == "/v1/chat":
                msgs = body.get("messages") or []
                prefer_local = body.get("prefer_local", True)
                r = chat(
                    cfg,
                    msgs,
                    prefer_local=prefer_local,
                    max_tokens=int(body.get("max_tokens") or 256),
                )
                _json_response(self, 200 if r.get("ok") else 502, r)
                return

            if path == "/v1/coord":
                plate_line = body.get("plate") or body.get("line") or ""
                if not plate_line and body:
                    plate = dict(body)
                    plate.setdefault("schema", "nexus_coord.v1")
                else:
                    plate = parse_plate(plate_line)
                path_saved = save_matrix(Path(cfg["_root"]), plate)
                llama = probe_local(cfg)
                ack = plate_ack(
                    from_id="Grokium",
                    ref_seq=plate.get("seq"),
                    unity=float(plate.get("unity") or 1.0),
                    llama=1 if llama.get("ok") else 0,
                    ssh=int(plate.get("ssh") or 0),
                    watchd=int(plate.get("watchd") or 0),
                    farm=str(plate.get("farm") or "standby"),
                    hold_flash=int(plate.get("hold_flash") if plate.get("hold_flash") is not None else 1),
                    extra={"grokium": 1, "pickup": 1 if body.get("pickup") else 0},
                )
                _json_response(
                    self,
                    200,
                    {
                        "ok": True,
                        "share": "state_matrix_only",
                        "matrix_path": str(path_saved),
                        "sot_bits_prefix": fold_bits(plate)[:64],
                        "ack_plate": ack,
                        "telemetry": False,
                    },
                )
                return

            _json_response(self, 404, {"ok": False, "error": "not_found"})

    return H


def serve(cfg: dict[str, Any] | None = None) -> None:
    cfg = cfg or load()
    assert_zero_telemetry(cfg)
    host = cfg["server"]["host"]
    port = int(cfg["server"]["port"])
    if host not in ("127.0.0.1", "localhost", "::1"):
        host = "127.0.0.1"
    httpd = ThreadingHTTPServer((host, port), make_handler(cfg))
    print(f"grokium serve http://{host}:{port} telemetry=off local_first=1 capable=1", flush=True)
    httpd.serve_forever()
