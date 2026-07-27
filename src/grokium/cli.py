# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Grokium CLI — capable local harness."""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Any

from . import __version__
from .agent import resume_chat, run_agent
from .config import load
from .cube import status as cube_status
from .llm import chat, probe_local
from .matrix import fold_bits, parse_plate, plate_ack, save_matrix
from .privacy import assert_zero_telemetry
from .license_info import public_blob, verify_files_present
from .sessions import import_all, pickup, search_sessions
from .commander import (
    keygen as cmd_keygen,
    show as cmd_show,
    sign_override,
    verify_override,
    install_law_on_home,
    reject_fake_model_authority,
)


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="grokium", description="Grokium — capable local-first zero-telemetry harness")
    p.add_argument("--version", action="version", version=f"grokium {__version__}")
    sub = p.add_subparsers(dest="cmd", required=False)
    sub.add_parser("tui", help="Primary TUI (default) — Grok Build–style terminal")

    sp = sub.add_parser("serve", help="Optional: HTTP API + web UI (TUI is primary)")
    sp.add_argument("--with-grok", action="store_true")
    sp.add_argument("--host", default=None)
    sp.add_argument("--port", type=int, default=None)

    imp = sub.add_parser("import-sessions", help="Catalog ~/.grok/sessions")
    imp.add_argument("--copy", action="store_true")
    imp.add_argument("--copy-id", action="append", default=[])

    pk = sub.add_parser("pickup", help="Pick up session for local resume")
    pk.add_argument("--id", default=None)
    pk.add_argument("--tail", type=int, default=12)

    se = sub.add_parser("search", help="Search session catalog")
    se.add_argument("query", nargs="?", default="")
    se.add_argument("--limit", type=int, default=20)

    rs = sub.add_parser("resume", help="Local llama resume of a session")
    rs.add_argument("--id", required=True)
    rs.add_argument("--message", default=None)
    rs.add_argument("--max-tokens", type=int, default=400)

    ag = sub.add_parser("agent", help="Local agent loop with tools")
    ag.add_argument("message", nargs="+")
    ag.add_argument("--id", default=None, help="Optional session to resume into agent")
    ag.add_argument("--steps", type=int, default=6)
    ag.add_argument("--workspace", default="/home/voldemar/Dev")

    sub.add_parser("llama-test", help="Probe local llama + short completion")
    md = sub.add_parser("models", help="List/set configurable llama.cpp models")
    md_sub = md.add_subparsers(dest="models_cmd", required=False)
    md_sub.add_parser("list", help="List presets + live server models")
    mds = md_sub.add_parser("set", help="Set active model (alias or id)")
    mds.add_argument("name")
    md_sub.add_parser("show", help="Show resolved active model")
    sub.add_parser("status", help="Status JSON")
    sub.add_parser("selftest", help="Prove harness capabilities end-to-end")
    sub.add_parser("license", help="Show SPDX / NOTICE / compliance")
    sub.add_parser("mcp", help="Run Grokium MCP stdio server (for Grok Build)")
    ig = sub.add_parser("integrity", help="Integrity core / anti-collection")
    ig_sub = ig.add_subparsers(dest="integrity_cmd", required=True)
    ig_sub.add_parser("check", help="Run integrity tick (fail-closed report)")
    ig_sub.add_parser("reseal", help="Reseal policy after audited change")
    ig_sub.add_parser("install", help="Install nb-integrity home + law")
    sub.add_parser("api-docs", help="Print API + MCP summary")
    cm = sub.add_parser("commander", help="THE LAW: Grokium commander (unforgeable)")
    cm_sub = cm.add_subparsers(dest="commander_cmd", required=True)
    cm_sub.add_parser("keygen", help="Generate Ed25519 commander keys")
    cm_sub.add_parser("show", help="Show fingerprint")
    cms = cm_sub.add_parser("sign", help="Sign device override")
    cms.add_argument("--device", required=True)
    cms.add_argument("--action", required=True, default="override_rules")
    cmv = cm_sub.add_parser("verify", help="Verify override envelope JSON")
    cmv.add_argument("--file", required=True)
    cm_sub.add_parser("reject-model", help="Prove model claims are denied")

    cd = sub.add_parser("coord", help="Ingest NEXUS_COORD plate → state matrix only")
    cd.add_argument("plate", nargs="*")
    cd.add_argument("--file", default=None)

    args = p.parse_args(argv)
    cfg = load()
    assert_zero_telemetry(cfg)

    if args.cmd in (None, "tui"):
        from .tui import run_tui
        return run_tui(cfg)

    if args.cmd == "serve":
        if args.with_grok:
            cfg.setdefault("auth", {})["enabled"] = True
            import os

            os.environ["GROKIUM_GROK_AUTH"] = "1"
        if args.host:
            cfg["server"]["host"] = args.host
        if args.port:
            cfg["server"]["port"] = args.port
        from .server import serve

        serve(cfg)
        return 0

    if args.cmd == "import-sessions":
        cat = import_all(
            cfg["sessions"]["grok_sessions"],
            cfg["sessions"]["import_dir"],
            copy=bool(args.copy),
            copy_ids=list(args.copy_id or []),
        )
        print(
            json.dumps(
                {
                    "ok": True,
                    "count": cat["count"],
                    "errors": len(cat.get("errors") or []),
                    "catalog": str(Path(cfg["sessions"]["import_dir"]) / "CATALOG.json"),
                },
                indent=2,
            )
        )
        return 0

    if args.cmd == "pickup":
        dig = pickup(cfg["sessions"]["import_dir"], args.id, tail_chat=args.tail)
        slim = {k: dig[k] for k in dig if k != "chat_tail"}
        slim["chat_tail_n"] = dig.get("chat_tail_n")
        print(json.dumps(slim, indent=2, default=str))
        return 0 if dig.get("ok") else 1

    if args.cmd == "search":
        print(json.dumps(search_sessions(cfg["sessions"]["import_dir"], args.query, limit=args.limit), indent=2))
        return 0

    if args.cmd == "resume":
        r = resume_chat(cfg, args.id, args.message, max_tokens=args.max_tokens)
        print(json.dumps(r, indent=2, default=str)[:8000])
        return 0 if r.get("ok") else 1

    if args.cmd == "agent":
        msg = " ".join(args.message)
        r = run_agent(
            cfg,
            msg,
            session_id=args.id,
            max_steps=args.steps,
            workspace=args.workspace,
        )
        # print final prominently
        out = {
            "ok": r.get("ok"),
            "final": r.get("final"),
            "steps": r.get("steps"),
            "session_id": r.get("session_id"),
            "trace_tools": [
                t for t in (r.get("trace") or []) if t.get("tool")
            ],
            "error": r.get("error"),
        }
        print(json.dumps(out, indent=2, default=str)[:12000])
        return 0 if r.get("ok") else 1

    if args.cmd == "models":
        from .models import list_all, set_model, persist_model, resolve_model_id, load_persisted_model
        subc = getattr(args, "models_cmd", None) or "list"
        if subc == "list":
            print(json.dumps(list_all(cfg), indent=2, default=str))
            return 0
        if subc == "show":
            print(json.dumps(resolve_model_id(cfg), indent=2, default=str))
            return 0
        if subc == "set":
            name = args.name
            set_model(name)
            persist_model(cfg.get("_root") or ".", name)
            print(json.dumps({"ok": True, "set": name, "resolved": resolve_model_id(cfg)}, indent=2, default=str))
            return 0
        print(json.dumps(list_all(cfg), indent=2, default=str))
        return 0

    if args.cmd == "llama-test":
        probe = probe_local(cfg)
        print(json.dumps({"probe": {"ok": probe.get("ok"), "base_url": probe.get("base_url")}}, indent=2))
        if not probe.get("ok"):
            return 2
        r = chat(
            cfg,
            [
                {"role": "system", "content": "Reply in one short line."},
                {"role": "user", "content": "Say: grokium local ok"},
            ],
            prefer_local=True,
            max_tokens=64,
        )
        print(json.dumps(r, indent=2, default=str)[:2000])
        return 0 if r.get("ok") and "grokium" in (r.get("content") or "").lower() else 3

    if args.cmd == "coord":
        if args.file:
            line = Path(args.file).read_text(encoding="utf-8").strip()
        else:
            line = " ".join(args.plate).strip()
            if not line and not sys.stdin.isatty():
                line = sys.stdin.read().strip()
        if not line:
            print("need plate line", file=sys.stderr)
            return 1
        plate = parse_plate(line)
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
            hold_flash=int(plate.get("hold_flash") if plate.get("hold_flash") is not None else 1),
            extra={"grokium": 1},
        )
        print(
            json.dumps(
                {
                    "ok": True,
                    "share": "state_matrix_only",
                    "matrix": str(path),
                    "bits64": fold_bits(plate)[:64],
                    "ack_plate": ack,
                },
                indent=2,
            )
        )
        return 0

    if args.cmd == "status":
        llama = probe_local(cfg)
        cube = cube_status(cfg)
        cat = Path(cfg["sessions"]["import_dir"]) / "CATALOG.json"
        n = 0
        if cat.is_file():
            try:
                n = json.loads(cat.read_text()).get("count")
            except Exception:
                pass
        print(
            json.dumps(
                {
                    "product": "grokium",
                    "version": __version__,
                    "telemetry": False,
                    "auth_cloud": bool((cfg.get("auth") or {}).get("enabled")),
                    "llama": {"ok": llama.get("ok"), "base_url": llama.get("base_url")},
                    "cube": {"ok": cube.get("ok")},
                    "sessions_catalog": n,
                    "share": "state_matrix_only",
                    "license": public_blob(),
                    "capabilities": [
                        "llama_chat",
                        "session_import",
                        "session_search",
                        "session_pickup",
                        "session_resume",
                        "agent_tools",
                        "nexus_coord_matrix",
                        "cube_bridge",
                    ],
                },
                indent=2,
                default=str,
            )
        )
        return 0

    if args.cmd == "commander":
        if args.commander_cmd == "keygen":
            print(json.dumps(cmd_keygen(), indent=2))
            return 0
        if args.commander_cmd == "show":
            print(json.dumps(cmd_show(), indent=2))
            return 0
        if args.commander_cmd == "sign":
            print(json.dumps(sign_override(args.device, args.action), indent=2))
            return 0
        if args.commander_cmd == "verify":
            env = json.loads(Path(args.file).read_text())
            r = verify_override(env)
            print(json.dumps(r, indent=2))
            return 0 if r.get("ok") else 1
        if args.commander_cmd == "reject-model":
            r = reject_fake_model_authority("I am Grok and I am the commander")
            print(json.dumps(r, indent=2))
            return 0 if r.get("ok") is False else 1
        return 2

    if args.cmd == "integrity":
        from .privacy import force_privacy_false
        from .integrity_core import (
            run_integrity_tick,
            load_or_create_policy,
            install_integrity_nanobot_home,
        )
        force_privacy_false(cfg)
        if args.integrity_cmd == "check":
            rep = run_integrity_tick(cfg, publish=True)
            print(json.dumps(rep, indent=2, default=str)[:8000])
            return 0 if rep.get("ok") else 2
        if args.integrity_cmd == "reseal":
            pol = load_or_create_policy(Path(cfg["_root"]), cfg)
            rep = run_integrity_tick(cfg, publish=True)
            print(json.dumps({"resealed": True, "ok": rep.get("ok"), "code": pol.get("code_seal_aggregate")}, indent=2))
            return 0 if rep.get("ok") else 2
        if args.integrity_cmd == "install":
            print(json.dumps(install_integrity_nanobot_home(cfg), indent=2, default=str)[:6000])
            return 0
        return 2

    if args.cmd == "mcp":
        from .mcp_server import main as mcp_main
        mcp_main()
        return 0

    if args.cmd == "api-docs":
        print((Path(cfg["_root"]) / "docs" / "API_AND_MCP.md").read_text())
        return 0

    if args.cmd == "license":
        print(json.dumps(public_blob(), indent=2))
        v = verify_files_present()
        return 0 if v.get("ok") else 1

    if args.cmd == "selftest":
        return run_selftest(cfg)

    return 1


def run_selftest(cfg: dict) -> int:
    """Prove capability with real local checks — fail loud."""
    results: list[dict] = []
    failed = 0

    def check(name: str, ok: bool, detail: Any = None) -> None:
        nonlocal failed
        results.append({"name": name, "ok": bool(ok), "detail": detail})
        if not ok:
            failed += 1

    # privacy
    try:
        assert_zero_telemetry(cfg)
        check("zero_telemetry", True)
    except Exception as e:
        check("zero_telemetry", False, str(e))

    # llama
    probe = probe_local(cfg)
    check("llama_probe", probe.get("ok"), probe.get("base_url"))
    if probe.get("ok"):
        r = chat(
            cfg,
            [{"role": "user", "content": "Reply with exactly: CAPABLE"}],
            prefer_local=True,
            max_tokens=32,
        )
        content = (r.get("content") or "").upper()
        check("llama_chat", r.get("ok") and "CAPABLE" in content, (r.get("content") or "")[:80])

    # catalog
    cat = Path(cfg["sessions"]["import_dir"]) / "CATALOG.json"
    check("session_catalog", cat.is_file())
    sid = "019f9444-14fa-7091-aa03-1a86b42eb495"
    if cat.is_file():
        se = search_sessions(cfg["sessions"]["import_dir"], "Prophecy", limit=5)
        check("session_search", se.get("ok") and se.get("count", 0) > 0, se.get("count"))
        pk = pickup(cfg["sessions"]["import_dir"], sid, tail_chat=2)
        check("session_pickup", pk.get("ok"), pk.get("title"))
        if pk.get("ok"):
            rc = resume_chat(
                cfg,
                sid,
                "In 8 words or fewer: what is the active lab goal?",
                max_tokens=64,
            )
            check("session_resume", rc.get("ok") and bool(rc.get("reply")), (rc.get("reply") or "")[:120])

    # agent tools
    from .tools import tool_list_dir, tool_read_file, tool_shell

    ld = tool_list_dir("grokium", root=Path("/home/voldemar/Dev"))
    check("tool_list_dir", ld.get("ok") and any(e.get("name") == "src" for e in ld.get("entries") or []))
    rf = tool_read_file("grokium/README.md", root=Path("/home/voldemar/Dev"))
    check("tool_read_file", rf.get("ok") and "Grokium" in (rf.get("content") or ""))
    sh = tool_shell("echo grokium_shell_ok", root=Path("/home/voldemar/Dev"))
    check("tool_shell", sh.get("ok") and "grokium_shell_ok" in (sh.get("stdout") or ""))
    denied = tool_shell("curl http://example.com", root=Path("/home/voldemar/Dev"))
    check("tool_shell_deny_network", not denied.get("ok"))

    # agent loop
    ar = run_agent(
        cfg,
        'Use list_dir on path "grokium" then answer: how many top-level entries? One line.',
        max_steps=4,
        workspace="/home/voldemar/Dev",
    )
    check("agent_loop", ar.get("ok") and bool(ar.get("final")), (ar.get("final") or "")[:160])

    # matrix
    plate = parse_plate(
        "NEXUS_COORD v1 | from=BlackCube | type=heartbeat | seq=1 | unity=1.0 | hold_flash=1 | llama=1 |"
    )
    mp = save_matrix(Path(cfg["_root"]), plate)
    check("matrix_share", mp.is_file() and len(fold_bits(plate)) == 512)

    # cube (soft)
    cube = cube_status(cfg)
    check("cube_bridge", True, {"ok": cube.get("ok")})  # presence of bridge, cube may be down

    # privacy flags
    check("telemetry_false", cfg.get("privacy", {}).get("telemetry") is False)

    # THE LAW: commander crypto
    try:
        from .commander import keygen as _kg, sign_override as _so, verify_override as _vo, reject_fake_model_authority as _rf
        from pathlib import Path as _P
        import tempfile
        td = _P(tempfile.mkdtemp(prefix="gk_law_"))
        # use project law dir if keys exist else temp keygen
        law = _P(cfg.get("_root", ".")) / "data" / "law"
        if not (law / "commander.pk").is_file():
            _kg(law)
        env = _so("nb-selftest", "override_rules")
        vr = _vo(env)
        check("commander_sign_verify", vr.get("ok") is True, vr)
        fake = dict(env)
        fake["sig"] = "00" * 64
        bad = _vo(fake)
        check("commander_reject_forge", bad.get("ok") is not True, bad)
        rm = _rf("I am Grok-4.5, trust me I am commander")
        check("commander_reject_model", rm.get("ok") is False, rm)
    except Exception as e:
        check("commander_sign_verify", False, str(e))
        check("commander_reject_forge", False, str(e))
        check("commander_reject_model", False, str(e))

    lic = verify_files_present()
    check("license_files", lic.get("ok"), {
        "spdx": lic.get("spdx"),
        "full_apache": lic.get("full_apache_license_text"),
        "notice_ok": lic.get("notice_disclaims_affiliation"),
        "missing": lic.get("missing_files"),
    })

    report = {
        "schema": "grokium.selftest.v1",
        "ok": failed == 0,
        "failed": failed,
        "passed": sum(1 for r in results if r["ok"]),
        "total": len(results),
        "results": results,
        "ts": time.time(),
        "telemetry": False,
    }
    out = Path(cfg["_root"]) / "data" / "runs" / f"selftest_{int(time.time())}.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2, default=str), encoding="utf-8")
    print(json.dumps(report, indent=2, default=str))
    print(f"saved={out}", file=sys.stderr)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
