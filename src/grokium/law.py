# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Grokium contributors
"""Cube Standards law surface — flags only on the hot path."""

from __future__ import annotations

from typing import Any

CREED = {
    "cube_is_sot": True,
    "os_is_way": True,
    "share": "state_matrix_only",
    "hold_flash": 1,
    "no_brain_wires": 1,
    "face_blur": 1,
    "state_matrix_key": 1,
    "cores_unmixed": 1,
    "nanobot_raw_bits_only": 1,
    "algocube_digits": 1,
    "commander_only_residual": 1,
    "zero_telemetry": 1,
    "devices_free": 1,
}


def law_blob(cfg: dict[str, Any] | None = None) -> dict[str, Any]:
    cfg = cfg or {}
    law = dict(CREED)
    section = cfg.get("law") or {}
    cube = cfg.get("cube") or {}
    for k in (
        "hold_flash",
        "no_brain_wires",
        "face_blur",
        "state_matrix_key",
        "cores_unmixed",
        "commander_only_residual",
    ):
        if k in section:
            law[k] = int(section[k]) if not isinstance(section[k], bool) else int(bool(section[k]))
    if cube.get("hold_flash") is not None:
        law["hold_flash"] = int(bool(cube.get("hold_flash")))
    if cube.get("share_mode"):
        law["share"] = cube["share_mode"]
    law["schema"] = "grokium.cube_law.v1"
    law["voice"] = "Cube is SoT; OS is way; matrix is binary; HOLD_FLASH"
    return law


def assert_hold_flash(cfg: dict[str, Any], action: str = "flash") -> None:
    law = law_blob(cfg)
    if law.get("hold_flash") and action in ("flash", "fastboot", "wipe", "unlock_bl"):
        raise RuntimeError(f"HOLD_FLASH blocks action={action}")
