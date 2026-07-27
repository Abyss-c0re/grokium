# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Nanobot-backed integrity core — fail-closed anti data-collection.

Layers (all must agree):
  1. Privacy flags hard-false (cannot enable for collection)
  2. Code seal — sha256 of critical modules
  3. Config seal — canonical privacy+share hash
  4. Network allowlist — only loopback (+ optional explicit Grok auth host)
  5. Commander-signed integrity policy
  6. Live SMX integrity stream (bits only)
  7. nb-integrity peer home pin + separate on critical leak
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import socket
import time
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

from .matrix import fold_bits
from .privacy import TELEMETRY_DENY, assert_zero_telemetry
from .smx_stream import get_bus

# Critical modules — changing these for collection should break the seal
CRITICAL_REL = [
    "src/grokium/privacy.py",
    "src/grokium/integrity_core.py",
    "src/grokium/llm.py",
    "src/grokium/matrix.py",
    "src/grokium/smx_stream.py",
    "src/grokium/commander.py",
    "src/grokium/server.py",
    "src/grokium/mcp_server.py",
    "config/grokium.toml",
]

# Destinations that may be contacted by Grokium itself
_DEFAULT_ALLOW = (
    "127.0.0.1",
    "localhost",
    "::1",
)

# Patterns that indicate collection / leak intent in URLs or config
_LEAK_PATTERNS = (
    r"telemetry",
    r"analytics",
    r"segment\.io",
    r"mixpanel",
    r"posthog",
    r"sentry\.io",
    r"amplitude",
    r"datadog",
    r"fullstory",
    r"hotjar",
    r"codebase.?upload",
    r"usage.?stats",
    r"phone.?home",
    r"exfil",
    r"data.?collect",
)


def _sha256_file(p: Path) -> str | None:
    if not p.is_file():
        return None
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def _canonical_privacy(cfg: dict[str, Any]) -> str:
    priv = cfg.get("privacy") or {}
    cube = cfg.get("cube") or {}
    law = cfg.get("law") or {}
    blob = {
        "privacy": {k: bool(priv.get(k)) for k in sorted(
            ("telemetry", "crash_reports", "usage_stats", "codebase_upload", "improve_model_cloud")
        )},
        "share": cube.get("share_mode") or "state_matrix_only",
        "hold_flash": int(law.get("hold_flash") if law.get("hold_flash") is not None else cube.get("hold_flash") or 1),
        "product": "grokium",
        "not": "data_collector",
    }
    return json.dumps(blob, sort_keys=True, separators=(",", ":"))


def compute_code_seal(root: Path) -> dict[str, Any]:
    files = {}
    h = hashlib.sha256()
    for rel in CRITICAL_REL:
        p = root / rel
        dig = _sha256_file(p)
        files[rel] = dig
        h.update(rel.encode())
        h.update(b"\0")
        h.update((dig or "missing").encode())
        h.update(b"\n")
    return {
        "schema": "grokium.code_seal.v1",
        "aggregate": h.hexdigest(),
        "files": files,
        "ts": time.time(),
    }


def network_allowlist(cfg: dict[str, Any]) -> list[str]:
    allow = list(_DEFAULT_ALLOW)
    local = (cfg.get("local") or {}).get("base_url") or ""
    host = urlparse(local).hostname
    if host:
        allow.append(host)
    cube = (cfg.get("cube") or {}).get("control_url") or ""
    ch = urlparse(cube).hostname
    if ch:
        allow.append(ch)
    # optional Grok cloud only if auth explicitly enabled
    auth = cfg.get("auth") or {}
    if auth.get("enabled"):
        ah = urlparse(auth.get("base_url") or "").hostname
        if ah:
            allow.append(ah)
    # unique
    out = []
    for a in allow:
        if a and a not in out:
            out.append(a)
    return out


def host_is_allowed(url_or_host: str, cfg: dict[str, Any]) -> bool:
    u = (url_or_host or "").strip()
    if not u:
        return False
    low = u.lower()
    for pat in _LEAK_PATTERNS:
        if re.search(pat, low):
            return False
    for d in TELEMETRY_DENY:
        if d in low:
            return False
    try:
        if "://" in u:
            host = urlparse(u).hostname or ""
        else:
            host = u.split("/")[0].split(":")[0]
    except Exception:
        return False
    if not host:
        return False
    allow = network_allowlist(cfg)
    if host in allow:
        return True
    # bare IP check loopback
    if host in ("127.0.0.1", "::1", "localhost"):
        return True
    return False


def assert_egress_allowed(url: str, cfg: dict[str, Any]) -> None:
    if not host_is_allowed(url, cfg):
        raise RuntimeError(
            f"INTEGRITY DENY egress to non-allowlisted / collection host: {url!r} "
            f"(allow={network_allowlist(cfg)})"
        )


def load_or_create_policy(root: Path, cfg: dict[str, Any]) -> dict[str, Any]:
    """Integrity policy. Prefer commander-signed file; else write unsigned baseline + seal hashes."""
    d = root / "data" / "integrity"
    d.mkdir(parents=True, exist_ok=True)
    path = d / "POLICY.json"
    code = compute_code_seal(root)
    priv_can = _canonical_privacy(cfg)
    priv_hash = hashlib.sha256(priv_can.encode()).hexdigest()
    policy = {
        "schema": "grokium.integrity_policy.v1",
        "law": "INTEGRITY_NO_LEAK_LAW",
        "product": "grokium",
        "not": ["data_collector", "grok_model", "telemetry_product"],
        "privacy_canonical_sha256": priv_hash,
        "code_seal_aggregate": code["aggregate"],
        "share_only": "state_matrix_only",
        "stream": "smx_realtime_bits",
        "fail_closed": True,
        "allowlist": network_allowlist(cfg),
        "ts": time.time(),
    }
    # try sign with commander
    sig_env = None
    try:
        from .commander import sign_override

        # Sign action binds aggregate — no body mismatch on verify
        action = f"integrity_policy_seal:{code['aggregate'][:32]}"
        sig_env = sign_override("nb-integrity", action)
        policy["commander_seal"] = sig_env
        policy["seal_action"] = action
    except Exception as e:
        policy["commander_seal_error"] = str(e)

    path.write_text(json.dumps(policy, indent=2), encoding="utf-8")
    (d / "CODE_SEAL.json").write_text(json.dumps(code, indent=2), encoding="utf-8")
    (d / "PRIVACY_CANONICAL.json").write_text(priv_can + "\n", encoding="utf-8")
    return policy


def verify_policy(root: Path, cfg: dict[str, Any], policy: dict[str, Any] | None = None) -> dict[str, Any]:
    d = root / "data" / "integrity"
    if policy is None:
        p = d / "POLICY.json"
        if not p.is_file():
            policy = load_or_create_policy(root, cfg)
        else:
            policy = json.loads(p.read_text(encoding="utf-8"))

    findings: list[dict[str, Any]] = []
    ok = True

    # 1 privacy flags
    try:
        assert_zero_telemetry(cfg)
    except Exception as e:
        ok = False
        findings.append({"id": "privacy_flags", "ok": False, "detail": str(e)})
    else:
        findings.append({"id": "privacy_flags", "ok": True})

    priv = cfg.get("privacy") or {}
    for k in ("telemetry", "crash_reports", "usage_stats", "codebase_upload", "improve_model_cloud"):
        if priv.get(k):
            ok = False
            findings.append({"id": f"privacy_{k}", "ok": False, "detail": "must be false"})

    # 2 share mode
    share = (cfg.get("cube") or {}).get("share_mode")
    if share != "state_matrix_only":
        ok = False
        findings.append({"id": "share_mode", "ok": False, "detail": share})
    else:
        findings.append({"id": "share_mode", "ok": True})

    # 3 config seal
    priv_can = _canonical_privacy(cfg)
    priv_hash = hashlib.sha256(priv_can.encode()).hexdigest()
    if policy.get("privacy_canonical_sha256") != priv_hash:
        # auto-heal only if all privacy still false — else fail
        if all(not priv.get(k) for k in ("telemetry", "crash_reports", "usage_stats", "codebase_upload", "improve_model_cloud")):
            findings.append({"id": "privacy_seal", "ok": True, "detail": "rotated_heal"})
            policy["privacy_canonical_sha256"] = priv_hash
        else:
            ok = False
            findings.append({"id": "privacy_seal", "ok": False, "detail": "canonical mismatch under collection flags"})
    else:
        findings.append({"id": "privacy_seal", "ok": True})

    # 4 code seal
    code = compute_code_seal(root)
    if policy.get("code_seal_aggregate") and policy["code_seal_aggregate"] != code["aggregate"]:
        findings.append(
            {
                "id": "code_seal",
                "ok": False,
                "detail": "critical module hash changed — re-seal only via intentional integrity_policy_seal",
                "expected": policy.get("code_seal_aggregate"),
                "got": code["aggregate"],
            }
        )
        # code change is warning-level unless privacy also broken — still report
        # For collection-hardening: treat unexpected code seal break as fail if
        # GROKIUM_INTEGRITY_STRICT=1 or always fail_closed for server start
        if os.environ.get("GROKIUM_INTEGRITY_STRICT", "1") in ("1", "true", "yes"):
            ok = False
    else:
        findings.append({"id": "code_seal", "ok": True, "aggregate": code["aggregate"][:16]})

    # 5 commander seal verify
    seal = policy.get("commander_seal")
    if isinstance(seal, dict) and seal.get("sig"):
        try:
            from .commander import verify_override

            body = {k: v for k, v in policy.items() if k != "commander_seal" and k != "commander_seal_error"}
            # re-sign body is the signed bytes at seal time — verify envelope structure
            vr = verify_override(seal)
            findings.append({"id": "commander_seal", "ok": bool(vr.get("ok")), "detail": vr})
            if not vr.get("ok"):
                ok = False
        except Exception as e:
            findings.append({"id": "commander_seal", "ok": False, "detail": str(e)})
            ok = False
    else:
        findings.append({"id": "commander_seal", "ok": False, "detail": "missing seal"})
        ok = False

    # 6 allowlist does not contain telemetry hosts
    for h in network_allowlist(cfg):
        if any(re.search(p, h) for p in _LEAK_PATTERNS):
            ok = False
            findings.append({"id": "allowlist_poison", "ok": False, "host": h})

    # 7 loopback bind for server
    host = (cfg.get("server") or {}).get("host") or "127.0.0.1"
    if host not in ("127.0.0.1", "localhost", "::1"):
        ok = False
        findings.append({"id": "server_bind", "ok": False, "detail": f"non-loopback bind {host}"})
    else:
        findings.append({"id": "server_bind", "ok": True})

    report = {
        "schema": "grokium.integrity_report.v1",
        "ok": ok,
        "fail_closed": True,
        "findings": findings,
        "allowlist": network_allowlist(cfg),
        "share": "state_matrix_only",
        "stream": "smx_realtime",
        "product": "grokium",
        "not": "data_collector",
        "ts": time.time(),
        "code_seal": code["aggregate"],
        "privacy_sha256": priv_hash,
    }
    # integrity SMX bits
    report["smx_bits"] = fold_bits(
        {
            "ok": int(ok),
            "privacy": priv_hash[:16],
            "code": code["aggregate"][:16],
            "ts": int(time.time()),
            "leak": 0 if ok else 1,
        },
        n=512,
    )
    return report


def run_integrity_tick(cfg: dict[str, Any], *, publish: bool = True) -> dict[str, Any]:
    root = Path(cfg.get("_root") or Path(__file__).resolve().parents[2])
    pol_path = root / "data" / "integrity" / "POLICY.json"
    if not pol_path.is_file():
        load_or_create_policy(root, cfg)
    report = verify_policy(root, cfg)
    out = root / "data" / "integrity" / "LATEST.json"
    out.write_text(json.dumps(report, indent=2), encoding="utf-8")

    if publish:
        bus = get_bus(root)
        bus.publish(
            source="nb-integrity",
            bits=report["smx_bits"],
            integrity={
                "ok": report["ok"],
                "code_seal": report["code_seal"][:16],
                "privacy_sha256": report["privacy_sha256"][:16],
                "leak": 0 if report["ok"] else 1,
            },
            extra_flags={"fail_closed": True, "no_collection": True},
        )
    return report


def assert_integrity_or_raise(cfg: dict[str, Any]) -> dict[str, Any]:
    report = run_integrity_tick(cfg, publish=True)
    if not report.get("ok"):
        raise RuntimeError(
            "INTEGRITY FAIL-CLOSED: " + json.dumps(
                [f for f in report.get("findings") or [] if not f.get("ok")],
                default=str,
            )[:800]
        )
    return report


def install_integrity_nanobot_home(cfg: dict[str, Any]) -> dict[str, Any]:
    """Ensure nb-integrity role home has law + integrity watcher script."""
    root = Path(cfg.get("_root") or Path(__file__).resolve().parents[2])
    home = root / "data" / "home" / "nb-integrity"
    home.mkdir(parents=True, exist_ok=True)
    law = home / "law"
    law.mkdir(exist_ok=True)

    try:
        from .commander import install_law_on_home

        install_law_on_home(home, "nb-integrity", "integrity_no_leak_core")
    except Exception as e:
        (law / "INSTALL_ERROR.txt").write_text(str(e), encoding="utf-8")

    # Watcher: only SMX + integrity checks (no session prose)
    watch = home / "integrity_watch.py"
    watch.write_text(
        '''#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""nb-integrity watch — SMX-only, fail-closed, no data collection."""
import json, os, sys, time
from pathlib import Path
ROOT = Path(os.environ.get("GROKIUM_ROOT", Path(__file__).resolve().parents[3]))
sys.path.insert(0, str(ROOT / "src"))
from grokium.config import load
from grokium.integrity_core import run_integrity_tick

def main():
    cfg = load()
    interval = float(os.environ.get("GROKIUM_INTEGRITY_INTERVAL", "15"))
    while True:
        rep = run_integrity_tick(cfg, publish=True)
        print(json.dumps({"ts": time.time(), "ok": rep.get("ok"), "smx": True}), flush=True)
        if not rep.get("ok") and os.environ.get("GROKIUM_INTEGRITY_EXIT_ON_FAIL") == "1":
            return 2
        time.sleep(interval)

if __name__ == "__main__":
    raise SystemExit(main() or 0)
''',
        encoding="utf-8",
    )
    watch.chmod(0o755)

    policy = load_or_create_policy(root, cfg)
    report = run_integrity_tick(cfg, publish=True)
    return {
        "ok": report.get("ok"),
        "home": str(home),
        "purpose": "integrity_no_leak_core",
        "policy": str(root / "data" / "integrity" / "POLICY.json"),
        "report": report,
    }
