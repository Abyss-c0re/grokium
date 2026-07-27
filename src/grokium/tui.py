# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Grokium TUI — primary surface (Grok Build–shaped).

Layout inspired by Grok Build interactive TUI:
  [ sessions ]  [ transcript + tools ]  [ status ]
  [ slash/status bar ]
  [ input ]

Web UI is optional. Nanobot = integrity/fleet subagents, not the chat core
unless you route work through fleet tools.
"""

from __future__ import annotations

import curses
import json
import textwrap
from typing import Any

from . import __version__
from .agent import resume_chat, run_agent
from .commander import show as commander_show
from .config import load
from .integrity_core import run_integrity_tick
from .law import law_blob
from .llm import backend_status, chat, get_backend, probe_local, set_backend
from .privacy import assert_zero_telemetry, force_privacy_false
from .sessions import pickup, search_sessions
from .smx_stream import get_bus

HELP = """\
Grokium TUI  (primary · web optional via `grokium serve`)

BACKENDS (cores unmixed)
  /backend local     llama.cpp  :1212  (default)
  /backend grok      cloud Grok — needs GROK_API_KEY or XAI_API_KEY
  /backend           show active backend

GROK BUILD–LIKE
  /new               clear chat (new session context)
  /load [id|query]   /sessions then pickup  (alias /resume)
  /sessions [q]      list/search imported Grok sessions
  /pickup <id>       bind session · mode=resume
  /model             show model/backend
  /status            full status
  /help              this help
  /clear             clear transcript
  /quit              exit

MODES
  /mode chat|agent|resume
  chat   = direct LLM turns on active backend
  agent  = local tools (list/read/grep/shell) — Grokium tools, not nanobot core
  resume = continue imported Grok Build session via local/grok backend

NANOBOT
  Nanobot is NOT the chat core. It is:
    · integrity core (nb-integrity) — no-leak watch
    · optional fleet peers (construct/observer/host)
  /fleet             fleet status
  /integrity         integrity tick

  Enter send · Shift not needed · PgUp/PgDn scroll · / for commands
"""


def _wrap(text: str, width: int, prefix: str = "") -> list[str]:
    out: list[str] = []
    pad = " " * len(prefix)
    for i, para in enumerate((text or "").splitlines() or [""]):
        chunks = textwrap.wrap(para, width=max(12, width - len(prefix))) or [""]
        for j, c in enumerate(chunks):
            out.append((prefix if (i == 0 and j == 0) else pad) + c)
    return out


class GrokiumTUI:
    def __init__(self, stdscr: Any, cfg: dict[str, Any]) -> None:
        self.stdscr = stdscr
        self.cfg = cfg
        self.mode = "chat"
        self.session_id: str | None = None
        self.session_title = ""
        self.lines: list[tuple[str, str]] = []
        self.input = ""
        self.status = "ready"
        self.scroll = 0
        self.session_rows: list[dict[str, Any]] = []
        self.sel_sess = 0
        self.focus = "input"  # input | sessions
        self._boot()

    def _boot(self) -> None:
        be = get_backend(self.cfg)
        self._add("system", f"Grokium {__version__} · TUI primary · backend={be}")
        self._add("system", "Not affiliated with xAI. Models ≠ commander. /help · /backend local|grok")
        self._add(
            "system",
            "Nanobot = integrity/fleet subagents (not chat core). Agent tools are Grokium-local.",
        )
        self._reload_sessions("")

    def _add(self, role: str, text: str) -> None:
        self.lines.append((role, text))
        self.scroll = 0

    def _reload_sessions(self, q: str) -> None:
        try:
            r = search_sessions(self.cfg["sessions"]["import_dir"], q, limit=30)
            self.session_rows = r.get("matches") or r.get("sessions") or []
        except Exception:
            self.session_rows = []

    def _backend_label(self) -> str:
        b = get_backend(self.cfg)
        if b == "grok":
            return "grok(auth)"
        return "llama.cpp"

    def draw(self) -> None:
        scr = self.stdscr
        scr.erase()
        h, w = scr.getmaxyx()
        if h < 12 or w < 60:
            try:
                scr.addstr(0, 0, "Need ≥60x12 terminal for Grokium TUI")
            except curses.error:
                pass
            scr.refresh()
            return

        # column widths — Grok Build-ish: left sessions, center chat, thin right meta
        left = max(22, min(28, w // 5))
        right = max(20, min(26, w // 5))
        mid = w - left - right
        if mid < 30:
            right = 0
            mid = w - left

        # === header (like Grok title bar) ===
        be = self._backend_label()
        title = f" Grokium {__version__} │ {be} │ mode:{self.mode} │ sess:{(self.session_id or 'new')[:12]} │ smx │ tele:off "
        try:
            scr.attron(curses.A_REVERSE | curses.A_BOLD)
            scr.addstr(0, 0, title[: w - 1].ljust(w - 1))
            scr.attroff(curses.A_REVERSE | curses.A_BOLD)
        except curses.error:
            pass
        sub = " /help  /backend local|grok  /sessions  /load  /mode agent  /integrity  /fleet  ·  web: optional serve "
        try:
            scr.addstr(1, 0, sub[: w - 1].ljust(w - 1), curses.A_DIM)
        except curses.error:
            pass

        # horizontal rule
        try:
            scr.addstr(2, 0, "─" * (w - 1), curses.A_DIM)
        except curses.error:
            pass

        body_top, body_bot = 3, h - 4
        body_h = max(1, body_bot - body_top)

        # === LEFT: sessions ===
        try:
            scr.addstr(body_top, 0, " SESSIONS ".ljust(left - 1)[: left - 1], curses.A_BOLD)
        except curses.error:
            pass
        for i in range(1, body_h):
            idx = i - 1
            row_y = body_top + i
            if idx < len(self.session_rows):
                s = self.session_rows[idx]
                label = (s.get("title") or s.get("id") or "?")[: left - 3]
                active = (s.get("id") == self.session_id) or (idx == self.sel_sess and self.focus == "sessions")
                attr = curses.A_REVERSE if active else curses.A_NORMAL
                try:
                    scr.addstr(row_y, 0, ("›" + label).ljust(left - 1)[: left - 1], attr)
                except curses.error:
                    pass
            else:
                try:
                    scr.addstr(row_y, 0, " " * (left - 1), curses.A_DIM)
                except curses.error:
                    pass
        # divider
        for y in range(body_top, body_bot):
            try:
                scr.addstr(y, left - 1, "│", curses.A_DIM)
            except curses.error:
                pass

        # === CENTER: transcript ===
        cx = left
        cw = mid - 1
        wrapped: list[tuple[str, str]] = []
        for role, text in self.lines:
            if role == "user":
                pref = "you │ "
            elif role == "assistant":
                pref = "gk  │ "
            elif role == "tool":
                pref = "tool│ "
            else:
                pref = " ·  │ "
            wrapped.extend((role, ln) for ln in _wrap(text, cw - 1, pref))

        if self.scroll:
            end = max(0, len(wrapped) - self.scroll)
            start = max(0, end - body_h)
            view = wrapped[start:end]
        else:
            view = wrapped[-body_h:]

        for i in range(body_h):
            y = body_top + i
            if i < len(view):
                role, line = view[i]
                attr = curses.A_BOLD if role == "user" else (curses.A_DIM if role == "system" else curses.A_NORMAL)
                try:
                    scr.addstr(y, cx, line[:cw].ljust(cw)[:cw], attr)
                except curses.error:
                    pass
            else:
                try:
                    scr.addstr(y, cx, " " * cw)
                except curses.error:
                    pass

        # === RIGHT: status strip ===
        if right > 0:
            rx = left + mid
            for y in range(body_top, body_bot):
                try:
                    scr.addstr(y, rx - 1, "│", curses.A_DIM)
                except curses.error:
                    pass
            meta = [
                " STATUS",
                f" be:{be}",
                f" mode:{self.mode}",
                f" hold_flash",
                f" share:smx",
                "",
                " KEYS",
                " Tab focus",
                " Enter send",
                " / cmds",
                " PgUp/Dn",
            ]
            # live probes (cheap-ish)
            try:
                llama = probe_local(self.cfg)
                meta.insert(2, f" llama:{'up' if llama.get('ok') else 'dn'}")
            except Exception:
                pass
            for i, line in enumerate(meta):
                if body_top + i >= body_bot:
                    break
                try:
                    scr.addstr(body_top + i, rx, line[: right - 1].ljust(right - 1)[: right - 1], curses.A_DIM)
                except curses.error:
                    pass

        # === status + input (bottom, Grok-like) ===
        try:
            scr.addstr(h - 3, 0, (" " + self.status)[: w - 1].ljust(w - 1), curses.A_DIM)
        except curses.error:
            pass
        prompt = f"› {self.mode}/{self._backend_label()} › "
        shown = prompt + self.input
        if len(shown) > w - 2:
            shown = "…" + shown[-(w - 3) :]
        try:
            scr.attron(curses.A_REVERSE)
            scr.addstr(h - 2, 0, shown[: w - 1].ljust(w - 1))
            scr.attroff(curses.A_REVERSE)
        except curses.error:
            pass
        try:
            scr.addstr(
                h - 1,
                0,
                " Grokium TUI primary │ web optional │ nanobot=fleet/integrity not chat-core │ /backend local|grok "[: w - 1],
                curses.A_DIM,
            )
        except curses.error:
            pass
        scr.refresh()

    def run_command(self, raw: str) -> None:
        parts = raw.strip().split(maxsplit=1)
        cmd = parts[0].lower()
        arg = parts[1] if len(parts) > 1 else ""

        if cmd in ("/q", "/quit", "/exit"):
            raise SystemExit(0)
        if cmd in ("/h", "/help", "/?"):
            self._add("system", HELP)
            return
        if cmd in ("/new",):
            self.lines.clear()
            self.session_id = None
            self.session_title = ""
            self.mode = "chat"
            self._add("system", "new context (chat). /backend unchanged.")
            return
        if cmd == "/clear":
            self.lines.clear()
            self._add("system", "cleared")
            return
        if cmd in ("/backend", "/be"):
            if not arg:
                self._add("system", json.dumps(backend_status(self.cfg), indent=2))
                return
            try:
                b = set_backend(arg)
                # persist soft preference on cfg auth.enabled
                if b == "grok":
                    self.cfg.setdefault("auth", {})["enabled"] = True
                else:
                    self.cfg.setdefault("auth", {})["enabled"] = False
                self._add("system", f"backend → {b} (cores unmixed; no silent fallback)")
            except ValueError as e:
                self._add("system", str(e))
            return
        if cmd in ("/model",):
            st = backend_status(self.cfg)
            loc = probe_local(self.cfg)
            mid = "local"
            if loc.get("ok"):
                data = (loc.get("models") or {}).get("data") or []
                if data:
                    mid = (data[0].get("id") or "local")[-48:]
            self._add(
                "system",
                f"active_backend={st['active']}\nlocal_model={mid}\ngrok_key={st['grok']['key_present']}\n"
                f"switch: /backend local  or  /backend grok",
            )
            return
        if cmd == "/status":
            st = backend_status(self.cfg)
            ig = run_integrity_tick(self.cfg, publish=True)
            try:
                cmd_i = commander_show()
            except Exception as e:
                cmd_i = {"error": str(e)}
            self._add(
                "system",
                json.dumps(
                    {
                        "product": "grokium",
                        "version": __version__,
                        "ui": "tui_primary",
                        "web": "optional",
                        "backend": st,
                        "integrity": ig.get("ok"),
                        "commander": (cmd_i.get("fingerprint") or cmd_i.get("error") or "")[:32],
                        "nanobot_role": "fleet+integrity_not_chat_core",
                        "mode": self.mode,
                        "session": self.session_id,
                        "telemetry": False,
                    },
                    indent=2,
                ),
            )
            return
        if cmd == "/integrity":
            ig = run_integrity_tick(self.cfg, publish=True)
            fails = [f for f in (ig.get("findings") or []) if not f.get("ok")]
            self._add("system", f"integrity={'OK' if ig.get('ok') else 'FAIL'}\n" + json.dumps(fails or "pass", indent=2)[:1200])
            return
        if cmd == "/law":
            self._add("system", json.dumps(law_blob(self.cfg), indent=2)[:1200])
            return
        if cmd == "/commander":
            try:
                self._add("system", json.dumps(commander_show(), indent=2))
            except Exception as e:
                self._add("system", str(e))
            return
        if cmd == "/fleet":
            try:
                from .nanobots import status as nb_status

                self._add("system", json.dumps(nb_status(self.cfg), indent=2, default=str)[:2000])
            except Exception as e:
                self._add("system", f"fleet: {e}")
            return
        if cmd == "/mode":
            m = arg.strip().lower()
            if m in ("chat", "agent", "resume"):
                self.mode = m
                self._add("system", f"mode → {m}")
            else:
                self._add("system", "usage: /mode chat|agent|resume")
            return
        if cmd == "/sessions":
            self._reload_sessions(arg.strip())
            lines = [f"{len(self.session_rows)} sessions:"]
            for s in self.session_rows[:20]:
                lines.append(f"  {(s.get('id') or '')[:18]}  {s.get('title') or ''}")
            self._add("system", "\n".join(lines))
            return
        if cmd in ("/pickup", "/load", "/resume"):
            q = arg.strip()
            if not q:
                self._add("system", "usage: /load <session-id|search>  or  /pickup <id>")
                return
            # if looks like uuid-ish use pickup else search first
            sid = q
            if len(q) < 20 or " " in q:
                self._reload_sessions(q)
                if self.session_rows:
                    sid = self.session_rows[0].get("id") or q
                    self._add("system", f"search hit → {sid}")
                else:
                    self._add("system", "no session match")
                    return
            pk = pickup(self.cfg["sessions"]["import_dir"], sid, tail_chat=0)
            if not pk.get("ok"):
                self._add("system", str(pk))
                return
            self.session_id = pk.get("id") or sid
            self.session_title = str(pk.get("title") or sid)
            self.mode = "resume"
            self._add(
                "system",
                f"loaded · {self.session_title}\nmsgs={pk.get('num_messages')} · mode=resume · backend={get_backend(self.cfg)}",
            )
            return
        if cmd == "/smx":
            bus = get_bus(self.cfg.get("_root"))
            fr = bus.latest()
            if not fr:
                run_integrity_tick(self.cfg, publish=True)
                fr = bus.latest()
            if not fr:
                self._add("system", "no SMX frame")
                return
            bits = fr.get("bits") or ""
            self._add(
                "system",
                f"SMX seq={fr.get('seq')} src={fr.get('source')} set={fr.get('bits_set')}\n{bits[:160]}…",
            )
            return
        self._add("system", f"unknown {cmd} — /help")

    def send_message(self, text: str) -> None:
        self._add("user", text)
        self.status = f"thinking ({self._backend_label()})…"
        self.draw()
        be = get_backend(self.cfg)
        try:
            if self.mode == "chat":
                hist = [{"role": r, "content": c} for r, c in self.lines if r in ("user", "assistant")][-16:]
                msgs = [
                    {
                        "role": "system",
                        "content": "You are Grokium (local-first harness TUI). Concise. Zero telemetry. Product=grokium.",
                    }
                ] + hist
                r = chat(self.cfg, msgs, max_tokens=600, backend=be)
                if r.get("ok"):
                    self._add("assistant", r.get("content") or "(empty)")
                else:
                    self._add("system", f"chat error [{r.get('path')}]: {r.get('error')}")
            elif self.mode == "agent":
                self._add("system", "agent tools = Grokium local (not nanobot peer core)")
                r = run_agent(self.cfg, text, session_id=self.session_id, max_steps=6)
                tools = [t.get("tool") for t in (r.get("trace") or []) if t.get("tool")]
                if tools:
                    self._add("tool", "called: " + ", ".join(tools))
                self._add("assistant", r.get("final") or r.get("error") or "")
            else:
                if not self.session_id:
                    self._add("system", "no session — /sessions · /load <id>")
                else:
                    # resume_chat uses local prefer — patch path via cfg backend
                    prefer = be != "grok"
                    # resume_chat doesn't take backend; temporarily set
                    r = resume_chat(self.cfg, self.session_id, text, max_tokens=600)
                    # if grok wanted, do direct chat with context instead
                    if be == "grok":
                        ctx = []
                        r2 = chat(
                            self.cfg,
                            [
                                {"role": "system", "content": f"Resume session {self.session_title}. Be concise."},
                                {"role": "user", "content": text},
                            ],
                            backend="grok",
                            max_tokens=600,
                        )
                        self._add("assistant", r2.get("content") if r2.get("ok") else f"error: {r2.get('error')}")
                    else:
                        self._add("assistant", r.get("reply") if r.get("ok") else f"error: {r.get('error')}")
        except Exception as e:
            self._add("system", f"exception: {e}")
        self.status = f"ready · {self._backend_label()}"

    def loop(self) -> None:
        curses.curs_set(1)
        self.stdscr.keypad(True)
        self.stdscr.timeout(200)
        while True:
            self.draw()
            try:
                ch = self.stdscr.get_wch()
            except curses.error:
                continue
            if ch == curses.KEY_RESIZE:
                continue
            if ch == curses.KEY_PPAGE:
                self.scroll = min(self.scroll + 8, 500)
                continue
            if ch == curses.KEY_NPAGE:
                self.scroll = max(0, self.scroll - 8)
                continue
            if ch == "\t":
                self.focus = "sessions" if self.focus == "input" else "input"
                continue
            if ch == curses.KEY_UP and self.focus == "sessions":
                self.sel_sess = max(0, self.sel_sess - 1)
                continue
            if ch == curses.KEY_DOWN and self.focus == "sessions":
                self.sel_sess = min(max(0, len(self.session_rows) - 1), self.sel_sess + 1)
                continue
            if ch in ("\n", "\r", curses.KEY_ENTER) and self.focus == "sessions":
                if self.session_rows:
                    s = self.session_rows[self.sel_sess]
                    self.run_command(f"/pickup {s.get('id')}")
                    self.focus = "input"
                continue
            if ch in (curses.KEY_BACKSPACE, "\x7f", "\b"):
                self.input = self.input[:-1]
                continue
            if ch in ("\n", "\r", curses.KEY_ENTER):
                text = self.input.strip()
                self.input = ""
                if not text:
                    continue
                if text.startswith("/"):
                    try:
                        self.run_command(text)
                    except SystemExit:
                        return
                    except Exception as e:
                        self._add("system", f"cmd error: {e}")
                else:
                    self.send_message(text)
                continue
            if isinstance(ch, str) and ch.isprintable():
                if len(self.input) < 4000:
                    self.input += ch


def run_tui(cfg: dict[str, Any] | None = None) -> int:
    cfg = cfg or load()
    force_privacy_false(cfg)
    assert_zero_telemetry(cfg)
    try:
        curses.wrapper(lambda stdscr: GrokiumTUI(stdscr, cfg).loop())
    except KeyboardInterrupt:
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(run_tui())
