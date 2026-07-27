# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Grokium TUI — primary product surface (web UI is optional).

Grok Build–style terminal harness: chat, agent, sessions, integrity, SMX.
curses only (no rich/textual required).
"""

from __future__ import annotations

import curses
import json
import textwrap
import time
from pathlib import Path
from typing import Any

from . import __version__
from .agent import resume_chat, run_agent
from .commander import show as commander_show
from .config import load
from .integrity_core import run_integrity_tick
from .law import law_blob
from .llm import chat, probe_local
from .privacy import force_privacy_false, assert_zero_telemetry
from .sessions import pickup, search_sessions
from .smx_stream import get_bus


HELP = """\
Grokium TUI — primary surface (web is optional: `grokium serve`)

  Enter          send message (mode: chat | agent | resume)
  /help          this help
  /status        harness + llama + integrity
  /integrity     integrity tick
  /law           law plate
  /commander     commander fingerprint
  /sessions [q]  search sessions
  /pickup <id>   pick session for resume
  /mode chat|agent|resume
  /smx           latest state matrix bits (preview)
  /clear         clear transcript
  /quit          exit

Product=grokium · models are not commander · SMX share only · zero telemetry
"""


class GrokiumTUI:
    def __init__(self, stdscr: Any, cfg: dict[str, Any]) -> None:
        self.stdscr = stdscr
        self.cfg = cfg
        self.mode = "chat"  # chat | agent | resume
        self.session_id: str | None = None
        self.session_title = ""
        self.lines: list[tuple[str, str]] = []  # (role, text)
        self.input = ""
        self.status = "ready"
        self.busy = False
        self.scroll = 0  # lines from bottom
        self._add("system", f"Grokium TUI v{__version__} — primary surface. Web UI optional.")
        self._add("system", "Local-first · zero telemetry · type /help")

    def _add(self, role: str, text: str) -> None:
        self.lines.append((role, text))
        self.scroll = 0

    def refresh_header_bits(self) -> str:
        try:
            llama = probe_local(self.cfg)
            lh = "hot" if llama.get("ok") else "cold"
        except Exception:
            lh = "?"
        try:
            ig = run_integrity_tick(self.cfg, publish=True)
            integ = "ok" if ig.get("ok") else "FAIL"
        except Exception:
            integ = "?"
        sess = (self.session_id or "—")[:13]
        return f" llama:{lh}  integrity:{integ}  mode:{self.mode}  sess:{sess}  share:smx  telemetry:off "

    def draw(self) -> None:
        scr = self.stdscr
        scr.erase()
        h, w = scr.getmaxyx()
        if h < 8 or w < 40:
            scr.addstr(0, 0, "terminal too small")
            scr.refresh()
            return

        # header
        title = f" GROKIUM  v{__version__}  ·  TUI primary  ·  not xAI / not grok-model-commander "
        try:
            scr.attron(curses.A_REVERSE)
            scr.addstr(0, 0, title[: w - 1].ljust(w - 1))
            scr.attroff(curses.A_REVERSE)
        except curses.error:
            pass

        sub = self.refresh_header_bits()[: w - 1]
        try:
            scr.addstr(1, 0, sub.ljust(w - 1), curses.A_DIM)
        except curses.error:
            pass

        # transcript area: rows 3 .. h-4
        top, bottom = 3, h - 4
        height = max(1, bottom - top)
        # wrap lines
        wrapped: list[tuple[str, str]] = []
        for role, text in self.lines:
            prefix = {"user": "you> ", "assistant": "gk>  ", "system": "···  "}.get(role, "?    ")
            for i, para in enumerate((text or "").splitlines() or [""]):
                chunks = textwrap.wrap(para, width=max(20, w - 6 - len(prefix))) or [""]
                for j, ch in enumerate(chunks):
                    p = prefix if (i == 0 and j == 0) else " " * len(prefix)
                    wrapped.append((role, p + ch))

        view = wrapped
        if self.scroll:
            end = max(0, len(view) - self.scroll)
            start = max(0, end - height)
            view = view[start:end]
        else:
            view = view[-height:]

        for i, (role, line) in enumerate(view):
            attr = curses.A_NORMAL
            if role == "user":
                attr = curses.A_BOLD
            elif role == "system":
                attr = curses.A_DIM
            try:
                scr.addstr(top + i, 0, line[: w - 1], attr)
            except curses.error:
                pass

        # status line
        try:
            scr.addstr(h - 3, 0, (" " + self.status)[: w - 1].ljust(w - 1), curses.A_DIM)
        except curses.error:
            pass

        # input
        prompt = f"[{self.mode}] "
        try:
            scr.attron(curses.A_REVERSE)
            scr.addstr(h - 2, 0, " " * (w - 1))
            shown = prompt + self.input
            if len(shown) > w - 2:
                shown = "…" + shown[-(w - 3) :]
            scr.addstr(h - 2, 0, shown[: w - 1])
            scr.attroff(curses.A_REVERSE)
        except curses.error:
            pass

        try:
            scr.addstr(h - 1, 0, " Enter send · /help · /quit · PgUp/PgDn scroll "[: w - 1], curses.A_DIM)
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
        if cmd == "/clear":
            self.lines.clear()
            self._add("system", "cleared")
            return
        if cmd == "/status":
            llama = probe_local(self.cfg)
            ig = run_integrity_tick(self.cfg, publish=True)
            cmd_i = {}
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
                        "telemetry": False,
                        "llama": {"ok": llama.get("ok"), "base": llama.get("base_url")},
                        "integrity": ig.get("ok"),
                        "commander_fp": (cmd_i.get("fingerprint") or "")[:24],
                        "mode": self.mode,
                        "session": self.session_id,
                        "ui": "tui_primary",
                        "web": "optional_serve",
                    },
                    indent=2,
                ),
            )
            return
        if cmd == "/integrity":
            ig = run_integrity_tick(self.cfg, publish=True)
            fails = [f for f in (ig.get("findings") or []) if not f.get("ok")]
            self._add(
                "system",
                f"integrity={'OK' if ig.get('ok') else 'FAIL'}\n"
                + json.dumps(fails or [{"all": "pass"}], indent=2)[:1500],
            )
            return
        if cmd == "/law":
            self._add("system", json.dumps(law_blob(self.cfg), indent=2)[:1200])
            return
        if cmd == "/commander":
            try:
                self._add("system", json.dumps(commander_show(), indent=2))
            except Exception as e:
                self._add("system", f"commander error: {e}")
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
            q = arg.strip()
            r = search_sessions(self.cfg["sessions"]["import_dir"], q, limit=15)
            if not r.get("ok"):
                self._add("system", str(r))
                return
            lines = [f"sessions ({r.get('count')}):"]
            for s in r.get("matches") or []:
                lines.append(f"  {(s.get('id') or '')[:18]}  {s.get('title') or ''}")
            self._add("system", "\n".join(lines) or "none")
            return
        if cmd == "/pickup":
            sid = arg.strip()
            if not sid:
                self._add("system", "usage: /pickup <session-id>")
                return
            pk = pickup(self.cfg["sessions"]["import_dir"], sid, tail_chat=0)
            if not pk.get("ok"):
                self._add("system", str(pk))
                return
            self.session_id = pk.get("id") or sid
            self.session_title = pk.get("title") or sid
            self.mode = "resume"
            self._add(
                "system",
                f"pickup ok · {self.session_title}\n"
                f"msgs={pk.get('num_messages')} · mode=resume · id={self.session_id}",
            )
            return
        if cmd == "/smx":
            bus = get_bus(self.cfg.get("_root"))
            fr = bus.latest()
            if not fr:
                run_integrity_tick(self.cfg, publish=True)
                fr = bus.latest()
            if not fr:
                self._add("system", "no SMX frame yet")
                return
            bits = fr.get("bits") or ""
            self._add(
                "system",
                f"SMX seq={fr.get('seq')} src={fr.get('source')} set={fr.get('bits_set')}\n"
                f"{bits[:128]}…\n(share=state_matrix_only prose={fr.get('prose')})",
            )
            return
        self._add("system", f"unknown command {cmd} — /help")

    def send_message(self, text: str) -> None:
        self._add("user", text)
        self.status = "thinking…"
        self.draw()
        try:
            if self.mode == "chat":
                hist = []
                for role, content in self.lines:
                    if role in ("user", "assistant"):
                        hist.append({"role": role, "content": content})
                # last turns only
                hist = hist[-16:]
                msgs = [
                    {
                        "role": "system",
                        "content": "You are Grokium TUI (local harness). Concise. Zero telemetry. Not cloud Grok.",
                    }
                ] + hist
                r = chat(self.cfg, msgs, prefer_local=True, max_tokens=500)
                self._add("assistant", r.get("content") if r.get("ok") else f"error: {r.get('error')}")
            elif self.mode == "agent":
                r = run_agent(
                    self.cfg,
                    text,
                    session_id=self.session_id,
                    max_steps=6,
                    workspace="/home/voldemar/Dev",
                )
                tools = [t.get("tool") for t in (r.get("trace") or []) if t.get("tool")]
                body = r.get("final") or r.get("error") or ""
                if tools:
                    body = f"[tools: {', '.join(tools)}]\n\n{body}"
                self._add("assistant", body)
            else:  # resume
                if not self.session_id:
                    self._add("system", "no session — /sessions then /pickup <id>, or /mode chat")
                else:
                    r = resume_chat(self.cfg, self.session_id, text, max_tokens=500)
                    self._add("assistant", r.get("reply") if r.get("ok") else f"error: {r.get('error')}")
        except Exception as e:
            self._add("system", f"exception: {e}")
        self.status = "ready"

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
                self.scroll = min(self.scroll + 5, max(0, len(self.lines) * 3))
                continue
            if ch == curses.KEY_NPAGE:
                self.scroll = max(0, self.scroll - 5)
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
