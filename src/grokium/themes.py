# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""TUI themes — Crimson Cube OS energy. Curses color pairs + web CSS vars."""

from __future__ import annotations

import curses
from typing import Any

# Pair ids (1+)
P_HEADER = 1
P_SUB = 2
P_USER = 3
P_ASSIST = 4
P_SYSTEM = 5
P_TOOL = 6
P_CODE = 7
P_HEADER_MD = 8
P_QUOTE = 9
P_LIST = 10
P_INPUT = 11
P_SIDE = 12
P_SIDE_SEL = 13
P_DIV = 14
P_STATUS = 15
P_ACCENT = 16
P_WARN = 17
P_OK = 18
P_BAD = 19

Theme = dict[str, Any]

THEMES: dict[str, Theme] = {
    "crimson": {
        "name": "Crimson Cube OS",
        "blurb": "Black lattice · blood crimson · gold edge — Cube portal energy",
        "default": True,
        # fg, bg as curses COLOR_* or -1 for default
        "colors": {
            P_HEADER: (curses.COLOR_BLACK, curses.COLOR_RED),
            P_SUB: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_USER: (curses.COLOR_WHITE, curses.COLOR_BLACK),
            P_ASSIST: (curses.COLOR_WHITE, curses.COLOR_BLACK),
            P_SYSTEM: (curses.COLOR_RED, curses.COLOR_BLACK),
            P_TOOL: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_CODE: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_HEADER_MD: (curses.COLOR_RED, curses.COLOR_BLACK),
            P_QUOTE: (curses.COLOR_MAGENTA, curses.COLOR_BLACK),
            P_LIST: (curses.COLOR_CYAN, curses.COLOR_BLACK),
            P_INPUT: (curses.COLOR_BLACK, curses.COLOR_RED),
            P_SIDE: (curses.COLOR_WHITE, curses.COLOR_BLACK),
            P_SIDE_SEL: (curses.COLOR_BLACK, curses.COLOR_YELLOW),
            P_DIV: (curses.COLOR_RED, curses.COLOR_BLACK),
            P_STATUS: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_ACCENT: (curses.COLOR_RED, curses.COLOR_BLACK),
            P_WARN: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_OK: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_BAD: (curses.COLOR_RED, curses.COLOR_BLACK),
        },
        "attrs": {
            P_HEADER: curses.A_BOLD,
            P_HEADER_MD: curses.A_BOLD | curses.A_UNDERLINE,
            P_USER: curses.A_BOLD,
            P_INPUT: curses.A_BOLD,
            P_SIDE_SEL: curses.A_BOLD,
            P_ACCENT: curses.A_BOLD,
        },
        "css": {
            "--bg": "#0a0406",
            "--bg2": "#14080c",
            "--panel": "#1a0c10",
            "--line": "#3f151c",
            "--fg": "#f5e6e8",
            "--mut": "#8a5a62",
            "--acc": "#e11d48",
            "--acc2": "#fbbf24",
            "--ok": "#4ade80",
            "--bad": "#fb7185",
            "--chat-user": "#1f0a10",
            "--chat-bot": "#12080a",
        },
    },
    "matrix": {
        "name": "StateMatrix Rain",
        "blurb": "Phosphor green on void — SMX bits only vibes",
        "colors": {
            P_HEADER: (curses.COLOR_BLACK, curses.COLOR_GREEN),
            P_SUB: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_USER: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_ASSIST: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_SYSTEM: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_TOOL: (curses.COLOR_CYAN, curses.COLOR_BLACK),
            P_CODE: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_HEADER_MD: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_QUOTE: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_LIST: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_INPUT: (curses.COLOR_BLACK, curses.COLOR_GREEN),
            P_SIDE: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_SIDE_SEL: (curses.COLOR_BLACK, curses.COLOR_GREEN),
            P_DIV: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_STATUS: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_ACCENT: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_WARN: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_OK: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_BAD: (curses.COLOR_RED, curses.COLOR_BLACK),
        },
        "attrs": {
            P_HEADER: curses.A_BOLD,
            P_HEADER_MD: curses.A_BOLD,
            P_USER: curses.A_BOLD,
            P_INPUT: curses.A_BOLD,
            P_SIDE_SEL: curses.A_BOLD,
            P_SYSTEM: curses.A_DIM,
        },
        "css": {
            "--bg": "#020805",
            "--bg2": "#04120a",
            "--panel": "#0a1a12",
            "--line": "#14532d",
            "--fg": "#bbf7d0",
            "--mut": "#4d7c5c",
            "--acc": "#22c55e",
            "--acc2": "#86efac",
            "--ok": "#4ade80",
            "--bad": "#f87171",
            "--chat-user": "#052e16",
            "--chat-bot": "#022c22",
        },
    },
    "void": {
        "name": "Void Cyan",
        "blurb": "Cold station blue — BlackCube night watch",
        "colors": {
            P_HEADER: (curses.COLOR_BLACK, curses.COLOR_CYAN),
            P_SUB: (curses.COLOR_CYAN, curses.COLOR_BLACK),
            P_USER: (curses.COLOR_WHITE, curses.COLOR_BLACK),
            P_ASSIST: (curses.COLOR_CYAN, curses.COLOR_BLACK),
            P_SYSTEM: (curses.COLOR_BLUE, curses.COLOR_BLACK),
            P_TOOL: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_CODE: (curses.COLOR_CYAN, curses.COLOR_BLACK),
            P_HEADER_MD: (curses.COLOR_CYAN, curses.COLOR_BLACK),
            P_QUOTE: (curses.COLOR_BLUE, curses.COLOR_BLACK),
            P_LIST: (curses.COLOR_WHITE, curses.COLOR_BLACK),
            P_INPUT: (curses.COLOR_BLACK, curses.COLOR_CYAN),
            P_SIDE: (curses.COLOR_WHITE, curses.COLOR_BLACK),
            P_SIDE_SEL: (curses.COLOR_BLACK, curses.COLOR_CYAN),
            P_DIV: (curses.COLOR_CYAN, curses.COLOR_BLACK),
            P_STATUS: (curses.COLOR_CYAN, curses.COLOR_BLACK),
            P_ACCENT: (curses.COLOR_CYAN, curses.COLOR_BLACK),
            P_WARN: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_OK: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_BAD: (curses.COLOR_RED, curses.COLOR_BLACK),
        },
        "attrs": {
            P_HEADER: curses.A_BOLD,
            P_USER: curses.A_BOLD,
            P_INPUT: curses.A_BOLD,
            P_SIDE_SEL: curses.A_BOLD,
            P_HEADER_MD: curses.A_BOLD | curses.A_UNDERLINE,
        },
        "css": {
            "--bg": "#04080f",
            "--bg2": "#0a1220",
            "--panel": "#0f172a",
            "--line": "#1e3a5f",
            "--fg": "#e0f2fe",
            "--mut": "#64748b",
            "--acc": "#22d3ee",
            "--acc2": "#38bdf8",
            "--ok": "#4ade80",
            "--bad": "#f87171",
            "--chat-user": "#0c1929",
            "--chat-bot": "#082f49",
        },
    },
    "gold": {
        "name": "Commander Gold",
        "blurb": "Amber command plate — HOLD_FLASH solemn",
        "colors": {
            P_HEADER: (curses.COLOR_BLACK, curses.COLOR_YELLOW),
            P_SUB: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_USER: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_ASSIST: (curses.COLOR_WHITE, curses.COLOR_BLACK),
            P_SYSTEM: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_TOOL: (curses.COLOR_CYAN, curses.COLOR_BLACK),
            P_CODE: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_HEADER_MD: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_QUOTE: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_LIST: (curses.COLOR_WHITE, curses.COLOR_BLACK),
            P_INPUT: (curses.COLOR_BLACK, curses.COLOR_YELLOW),
            P_SIDE: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_SIDE_SEL: (curses.COLOR_BLACK, curses.COLOR_YELLOW),
            P_DIV: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_STATUS: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_ACCENT: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_WARN: (curses.COLOR_YELLOW, curses.COLOR_BLACK),
            P_OK: (curses.COLOR_GREEN, curses.COLOR_BLACK),
            P_BAD: (curses.COLOR_RED, curses.COLOR_BLACK),
        },
        "attrs": {
            P_HEADER: curses.A_BOLD,
            P_USER: curses.A_BOLD,
            P_INPUT: curses.A_BOLD,
            P_SIDE_SEL: curses.A_BOLD,
            P_HEADER_MD: curses.A_BOLD,
        },
        "css": {
            "--bg": "#0c0a04",
            "--bg2": "#1a1408",
            "--panel": "#221a0c",
            "--line": "#4a3a12",
            "--fg": "#fef3c7",
            "--mut": "#a16207",
            "--acc": "#fbbf24",
            "--acc2": "#f59e0b",
            "--ok": "#4ade80",
            "--bad": "#f87171",
            "--chat-user": "#1c1408",
            "--chat-bot": "#15100a",
        },
    },
    "mono": {
        "name": "Mono Terminal",
        "blurb": "No color drama — classic teletype",
        "colors": {},  # attrs only
        "attrs": {
            P_HEADER: curses.A_REVERSE | curses.A_BOLD,
            P_USER: curses.A_BOLD,
            P_SYSTEM: curses.A_DIM,
            P_TOOL: curses.A_DIM,
            P_CODE: curses.A_DIM,
            P_HEADER_MD: curses.A_BOLD | curses.A_UNDERLINE,
            P_QUOTE: curses.A_DIM,
            P_INPUT: curses.A_REVERSE,
            P_SIDE_SEL: curses.A_REVERSE,
            P_STATUS: curses.A_DIM,
        },
        "css": {
            "--bg": "#0b0d10",
            "--bg2": "#11151a",
            "--panel": "#151a21",
            "--line": "#2a3340",
            "--fg": "#c8d0d8",
            "--mut": "#6a7380",
            "--acc": "#c8d0d8",
            "--acc2": "#9aa4b0",
            "--ok": "#c8d0d8",
            "--bad": "#c8d0d8",
            "--chat-user": "#151a21",
            "--chat-bot": "#11151a",
        },
    },
}

_DEFAULT = "crimson"
_current = _DEFAULT
_color_ok = False


def list_themes() -> list[dict[str, str]]:
    return [
        {"id": k, "name": v["name"], "blurb": v.get("blurb", ""), "default": bool(v.get("default"))}
        for k, v in THEMES.items()
    ]


def get_theme_id() -> str:
    return _current


def set_theme(name: str) -> str:
    global _current
    n = (name or "").strip().lower().replace(" ", "_").replace("-", "_")
    aliases = {
        "crimson_cube": "crimson",
        "cube": "crimson",
        "crimson_cube_os": "crimson",
        "os": "crimson",
        "rain": "matrix",
        "green": "matrix",
        "smx": "matrix",
        "cyan": "void",
        "blue": "void",
        "station": "void",
        "amber": "gold",
        "commander": "gold",
        "classic": "mono",
        "default": _DEFAULT,
    }
    n = aliases.get(n, n)
    if n not in THEMES:
        raise ValueError(f"unknown theme {name!r} — try: " + ", ".join(THEMES))
    _current = n
    return _current


def init_curses_theme(stdscr: Any, theme_id: str | None = None) -> str:
    """Apply theme color pairs. Call once after curses.wrapper starts."""
    global _color_ok, _current
    if theme_id:
        set_theme(theme_id)
    tid = _current
    theme = THEMES[tid]
    _color_ok = False
    try:
        if curses.has_colors():
            curses.start_color()
            try:
                curses.use_default_colors()
            except curses.error:
                pass
            colors = theme.get("colors") or {}
            for pid, (fg, bg) in colors.items():
                try:
                    curses.init_pair(pid, fg, bg)
                except curses.error:
                    pass
            _color_ok = bool(colors)
    except curses.error:
        _color_ok = False
    try:
        stdscr.bkgd(" ", _attr(P_SIDE))
    except curses.error:
        pass
    return tid


def _attr(pair: int) -> int:
    theme = THEMES.get(_current) or THEMES[_DEFAULT]
    a = (theme.get("attrs") or {}).get(pair, 0)
    if _color_ok and pair in (theme.get("colors") or {}):
        return curses.color_pair(pair) | a
    return a or curses.A_NORMAL


def attr_for_style(style: str, role: str = "") -> int:
    """Map md/role styles to theme attrs."""
    if role == "user" or style == "bold":
        return _attr(P_USER)
    if role == "system" or style == "dim":
        return _attr(P_SYSTEM)
    if role == "tool":
        return _attr(P_TOOL)
    if style == "header":
        return _attr(P_HEADER_MD)
    if style == "code":
        return _attr(P_CODE)
    if style == "quote":
        return _attr(P_QUOTE)
    if style == "list":
        return _attr(P_LIST)
    if role == "assistant":
        return _attr(P_ASSIST)
    return curses.A_NORMAL


def A_header() -> int:
    return _attr(P_HEADER)


def A_sub() -> int:
    return _attr(P_SUB)


def A_input() -> int:
    return _attr(P_INPUT)


def A_side() -> int:
    return _attr(P_SIDE)


def A_side_sel() -> int:
    return _attr(P_SIDE_SEL)


def A_div() -> int:
    return _attr(P_DIV)


def A_status() -> int:
    return _attr(P_STATUS)


def A_accent() -> int:
    return _attr(P_ACCENT)


def web_css_vars(theme_id: str | None = None) -> str:
    tid = theme_id or _current
    css = (THEMES.get(tid) or THEMES[_DEFAULT]).get("css") or {}
    return "\n".join(f"  {k}: {v};" for k, v in css.items())


def persist_theme(root: str, theme_id: str) -> None:
    from pathlib import Path
    import json

    p = Path(root) / "data" / "ui_theme.json"
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps({"theme": theme_id, "schema": "grokium.theme.v1"}, indent=2), encoding="utf-8")


def load_persisted_theme(root: str) -> str | None:
    from pathlib import Path
    import json

    p = Path(root) / "data" / "ui_theme.json"
    if not p.is_file():
        return None
    try:
        return json.loads(p.read_text()).get("theme")
    except Exception:
        return None
