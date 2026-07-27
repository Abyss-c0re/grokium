# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Cube-standard nanobot fleet — purpose-assigned, separable subagents.

Uses host nanobot binary (MIT, Abyss-c0re) — not vendored. Offline local llama.
Matrix hot path: raw bits only (no prose context).
"""

from __future__ import annotations

import json
import os
import signal
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

try:
    from .commander import install_law_on_home, sign_override
except Exception:  # pragma: no cover
    install_law_on_home = None  # type: ignore
    sign_override = None  # type: ignore

DEFAULT_ROLES = [
    {"id": "nb-matrix-eval", "purpose": "evaluate_sot_smx_harmony", "shell": False},
    {"id": "nb-construct", "purpose": "construct_deconstruct_edge", "shell": True},
    {"id": "nb-observer", "purpose": "observe_unity_watchd", "shell": False},
    {"id": "nb-host", "purpose": "station_cube_liaison", "shell": False},
]


def _nb_cfg(cfg: dict[str, Any]) -> dict[str, Any]:
    return cfg.get("nanobot") or {}


def _roles(cfg: dict[str, Any]) -> list[dict[str, Any]]:
    nb = _nb_cfg(cfg)
    roles = nb.get("roles")
    if not roles:
        return list(DEFAULT_ROLES)
    # tomllib may give list of dicts
    out = []
    for i, r in enumerate(roles):
        if isinstance(r, str):
            out.append({"id": r, "purpose": r, "shell": False})
        elif isinstance(r, dict):
            out.append(
                {
                    "id": r.get("id") or f"nb-{i}",
                    "purpose": r.get("purpose") or r.get("id") or "unassigned",
                    "shell": bool(r.get("shell", False)),
                }
            )
    return out or list(DEFAULT_ROLES)


def _binary(cfg: dict[str, Any]) -> str | None:
    nb = _nb_cfg(cfg)
    for key in ("binary", "fallback_binary"):
        p = nb.get(key)
        if p and Path(p).is_file() and os.access(p, os.X_OK):
            return str(p)
    for cand in (
        Path.home() / ".local/bin/nanobot",
        Path("/home/voldemar/Dev/nanobot/build/host/nanobot"),
    ):
        if cand.is_file() and os.access(cand, os.X_OK):
            return str(cand)
    return None


def fleet_path(cfg: dict[str, Any]) -> Path:
    root = Path((_nb_cfg(cfg).get("home_root") or Path(cfg.get("_root", ".")) / "data" / "home"))
    root.mkdir(parents=True, exist_ok=True)
    return root / "FLEET.json"


def load_fleet(cfg: dict[str, Any]) -> dict[str, Any]:
    p = fleet_path(cfg)
    if p.is_file():
        try:
            return json.loads(p.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            pass
    return {"schema": "grokium.nanobot_fleet.v1", "bots": {}, "ts": 0}


def save_fleet(cfg: dict[str, Any], fleet: dict[str, Any]) -> None:
    fleet["ts"] = time.time()
    fleet["schema"] = "grokium.nanobot_fleet.v1"
    fleet["law"] = "purpose_assigned_separable"
    fleet["share"] = "state_matrix_only"
    fleet["hold_flash"] = 1
    fleet_path(cfg).write_text(json.dumps(fleet, indent=2), encoding="utf-8")


def _home(cfg: dict[str, Any], bot_id: str) -> Path:
    root = Path(_nb_cfg(cfg).get("home_root") or Path(cfg["_root"]) / "data" / "home")
    h = root / bot_id
    h.mkdir(parents=True, exist_ok=True)
    return h


def _port(cfg: dict[str, Any], index: int) -> int:
    base = int(_nb_cfg(cfg).get("base_port") or 28800)
    return base + index


def _alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def _health(port: int, timeout: float = 1.5) -> dict[str, Any]:
    url = f"http://127.0.0.1:{port}/peer/v1/health"
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "grokium/cube-standards"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read().decode()
            try:
                return {"ok": True, "port": port, "body": json.loads(body)}
            except json.JSONDecodeError:
                return {"ok": True, "port": port, "body": body[:200]}
    except Exception as e:
        return {"ok": False, "port": port, "error": str(e)}


def _read_token(home: Path) -> str | None:
    p = home / "peer_token"
    if not p.is_file():
        return None
    text = p.read_text(encoding="utf-8", errors="replace").strip()
    if text.startswith("token="):
        return text.split("=", 1)[1].strip()
    return text.splitlines()[0].strip() if text else None


def deploy(cfg: dict[str, Any], *, only: list[str] | None = None) -> dict[str, Any]:
    """Deploy purpose-assigned nanobot peers (offline local llama). Separable by id."""
    binary = _binary(cfg)
    if not binary:
        return {"ok": False, "error": "nanobot_binary_missing", "hint": "build nanobot host or install ~/.local/bin/nanobot"}

    nb = _nb_cfg(cfg)
    base_url = nb.get("base_url") or (cfg.get("local") or {}).get("base_url") or "http://127.0.0.1:1212/v1"
    model = nb.get("model") or "local"
    roles = _roles(cfg)
    fleet = load_fleet(cfg)
    bots = fleet.setdefault("bots", {})
    deployed = []
    errors = []

    for idx, role in enumerate(roles):
        bot_id = role["id"]
        if only and bot_id not in only:
            continue
        port = _port(cfg, idx)
        home = _home(cfg, bot_id)
        # THE LAW: pin Grokium commander (not Grok model) on every nanobot home
        if install_law_on_home is not None:
            try:
                install_law_on_home(home, bot_id, role.get("purpose") or bot_id)
            except Exception as e:
                errors.append({"id": bot_id, "error": f"commander_law_install:{e}"})
        log = home / "nanobot.out"
        # already alive?
        prev = bots.get(bot_id) or {}
        if prev.get("pid") and _alive(int(prev["pid"])) and _health(int(prev.get("port") or port)).get("ok"):
            bots[bot_id] = {
                **prev,
                "id": bot_id,
                "purpose": role["purpose"],
                "port": int(prev.get("port") or port),
                "home": str(home),
                "status": "running",
                "separated": False,
            }
            deployed.append(bots[bot_id])
            continue

        env = os.environ.copy()
        env["NANOBOT_HOME"] = str(home)
        env["HOLD_FLASH"] = "1"
        env["GROKIUM"] = "1"
        # no telemetry env
        for k in list(env):
            if "telemetry" in k.lower() or "analytics" in k.lower():
                env.pop(k, None)

        cmd = [
            binary,
            "--home",
            str(home),
            "--port",
            str(port),
            "--offline",
            "--base-url",
            str(base_url),
            "--model",
            str(model),
        ]
        try:
            with log.open("ab") as lf:
                lf.write(f"\n--- deploy {time.time()} purpose={role['purpose']} ---\n".encode())
                proc = subprocess.Popen(
                    cmd,
                    cwd=str(home),
                    env=env,
                    stdout=lf,
                    stderr=subprocess.STDOUT,
                    start_new_session=True,  # separable process group
                )
            # wait for health
            ok = False
            for _ in range(20):
                time.sleep(0.25)
                if _health(port).get("ok"):
                    ok = True
                    break
                if proc.poll() is not None:
                    break
            rec = {
                "id": bot_id,
                "purpose": role["purpose"],
                "shell": bool(role.get("shell")),
                "port": port,
                "pid": proc.pid,
                "home": str(home),
                "binary": binary,
                "offline": True,
                "base_url": base_url,
                "model": model,
                "status": "running" if ok else "starting_or_failed",
                "health": _health(port),
                "separated": False,
                "law": "cube_purpose_assigned",
            }
            bots[bot_id] = rec
            if ok:
                deployed.append(rec)
            else:
                errors.append({"id": bot_id, "error": "health_timeout", "log": str(log)})
        except OSError as e:
            errors.append({"id": bot_id, "error": str(e)})

    fleet["bots"] = bots
    save_fleet(cfg, fleet)
    return {
        "ok": len(errors) == 0 and len(deployed) > 0,
        "schema": "grokium.nanobot_deploy.v1",
        "deployed": deployed,
        "errors": errors,
        "fleet": str(fleet_path(cfg)),
        "count": len(deployed),
        "hold_flash": 1,
        "share": "state_matrix_only",
        "telemetry": False,
    }


def status(cfg: dict[str, Any]) -> dict[str, Any]:
    fleet = load_fleet(cfg)
    bots = fleet.get("bots") or {}
    live = []
    for bot_id, rec in bots.items():
        pid = int(rec.get("pid") or 0)
        port = int(rec.get("port") or 0)
        alive = bool(pid and _alive(pid))
        health = _health(port) if port else {"ok": False}
        live.append(
            {
                "id": bot_id,
                "purpose": rec.get("purpose"),
                "port": port,
                "pid": pid,
                "alive": alive,
                "health_ok": bool(health.get("ok")),
                "separated": bool(rec.get("separated")),
                "home": rec.get("home"),
                "status": "running" if alive and health.get("ok") else ("dead" if not alive else "unhealthy"),
            }
        )
    running = sum(1 for b in live if b["status"] == "running")
    return {
        "ok": True,
        "schema": "grokium.nanobot_status.v1",
        "running": running,
        "total": len(live),
        "bots": live,
        "unity": (running / len(live)) if live else 0.0,
        "hold_flash": 1,
        "share": "state_matrix_only",
    }


def separate(cfg: dict[str, Any], bot_id: str) -> dict[str, Any]:
    """Detach one nanobot from the fleet without killing others (stop that peer only)."""
    fleet = load_fleet(cfg)
    bots = fleet.get("bots") or {}
    rec = bots.get(bot_id)
    if not rec:
        return {"ok": False, "error": "unknown_bot", "id": bot_id}
    pid = int(rec.get("pid") or 0)
    stopped = False
    if pid and _alive(pid):
        try:
            os.killpg(pid, signal.SIGTERM)
        except ProcessLookupError:
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        for _ in range(20):
            if not _alive(pid):
                stopped = True
                break
            time.sleep(0.1)
        if _alive(pid):
            try:
                os.killpg(pid, signal.SIGKILL)
            except ProcessLookupError:
                try:
                    os.kill(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            stopped = not _alive(pid)
    else:
        stopped = True
    rec["status"] = "separated"
    rec["separated"] = True
    rec["pid"] = None
    bots[bot_id] = rec
    fleet["bots"] = bots
    save_fleet(cfg, fleet)
    return {"ok": True, "id": bot_id, "separated": True, "stopped": stopped, "others_untouched": True}


def stop_all(cfg: dict[str, Any]) -> dict[str, Any]:
    fleet = load_fleet(cfg)
    results = []
    for bot_id in list((fleet.get("bots") or {}).keys()):
        results.append(separate(cfg, bot_id))
    return {"ok": all(r.get("ok") for r in results), "results": results}


def post_raw_bits(cfg: dict[str, Any], bot_id: str, bits: str) -> dict[str, Any]:
    """Prophecy path: raw matrix bits to nanobot — no prose context."""
    fleet = load_fleet(cfg)
    rec = (fleet.get("bots") or {}).get(bot_id)
    if not rec:
        # allow by purpose default matrix-eval
        st = status(cfg)
        for b in st.get("bots") or []:
            if b.get("id") == bot_id or (bot_id == "default" and "matrix" in (b.get("id") or "")):
                rec = b
                break
    if not rec:
        return {"ok": False, "error": "bot_not_found", "id": bot_id}
    port = int(rec.get("port") or 0)
    home = Path(rec.get("home") or _home(cfg, rec.get("id") or bot_id))
    token = _read_token(home)
    # Store raw bits locally (matrix store) — hot path non-verbal
    raw_dir = home / "matrix_raw"
    raw_dir.mkdir(parents=True, exist_ok=True)
    path = raw_dir / f"bits_{int(time.time())}.smx"
    # pure bits only
    clean = "".join(c for c in (bits or "") if c in "01")
    path.write_text(clean, encoding="utf-8")
    (raw_dir / "LATEST.smx").write_text(clean, encoding="utf-8")

    # optional peer prompt is VERBAL — do not send bits as chat if law says raw only.
    # Prophecy: matrix raw to nanobot without context = store + health ping only.
    health = _health(port)
    return {
        "ok": True,
        "schema": "grokium.nanobot_raw.v1",
        "id": rec.get("id") or bot_id,
        "purpose": rec.get("purpose"),
        "bits_len": len(clean),
        "stored": str(path),
        "verbal": False,
        "context": None,
        "health": health,
        "token_present": bool(token),
        "share": "state_matrix_only",
    }


def prompt_bot(cfg: dict[str, Any], bot_id: str, prompt: str) -> dict[str, Any]:
    """Edge verbal path (construct/observer) — not the matrix hot path."""
    fleet = load_fleet(cfg)
    rec = (fleet.get("bots") or {}).get(bot_id)
    if not rec:
        return {"ok": False, "error": "bot_not_found"}
    port = int(rec["port"])
    home = Path(rec["home"])
    token = _read_token(home)
    if not token:
        return {"ok": False, "error": "no_peer_token", "home": str(home)}
    url = f"http://127.0.0.1:{port}/peer/v1/prompt"
    body = json.dumps({"prompt": prompt, "peer_token": token}).encode()
    req = urllib.request.Request(
        url,
        data=body,
        headers={
            "Content-Type": "application/json",
            "X-Nanobot-Peer-Token": token,
            "User-Agent": "grokium/cube-standards",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            data = json.loads(resp.read().decode())
        return {"ok": True, "id": bot_id, "path": "verbal_edge", "response": data, "hot_path": False}
    except Exception as e:
        return {"ok": False, "id": bot_id, "error": str(e)}
