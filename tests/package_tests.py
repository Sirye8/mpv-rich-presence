# SPDX-License-Identifier: AGPL-3.0-or-later
"""Run against an installed plugin in a disposable mpv config directory."""

import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
mpv = sys.argv[2]
subprocess.run(
    [
        mpv,
        "--no-config",
        "--load-scripts=no",
        "--idle=yes",
        "--vo=null",
        "--ao=null",
        "--script=" + str(Path(__file__).with_name("sheet_tests.lua").resolve()),
    ],
    env=dict(os.environ, MPVRP_MODULES=str(root / "scripts/mpv-rich-presence/modules")),
    timeout=30,
    check=True,
)
with tempfile.TemporaryDirectory() as directory:
    config = Path(directory) / "mpv"
    for part in ("scripts", "script-opts"):
        shutil.copytree(root / part, config / part)
    applied = False
    for mode in ("cancel", "apply", "cancel", "position", "inherit-position"):
        log = Path(directory) / "mpv.log"
        subprocess.run(
            [
                mpv,
                "--config-dir=" + str(config),
                "--idle=yes",
                "--no-terminal",
                "--vo=null",
                "--ao=null",
                "--log-file=" + str(log),
                "--script="
                + str(Path(__file__).with_name("menu_actions.lua").resolve()),
            ],
            env=dict(os.environ, MPVRP_MENU_TEST=mode),
            timeout=12,
            check=True,
        )
        text = log.read_text(encoding="utf-8", errors="replace")
        if "stack traceback" in text or "Could not load the Rich Presence menu" in text:
            raise RuntimeError("Packaged Lua module failed to load:\n" + text[-12000:])
        if "Configuration complete received from bridge" not in text:
            raise RuntimeError("Configuration bridge failed")
        with sqlite3.connect(config / "scripts/mpv-rich-presence/data.sqlite3") as db:
            row = db.execute(
                "SELECT value FROM records WHERE category='appearance' AND scope='global'"
            ).fetchone()
            position = db.execute(
                "SELECT value FROM records WHERE category='panel' AND scope='global'"
            ).fetchone()
        db.close()
        if mode == "apply":
            applied = True
        if applied:
            assert row and json.loads(row[0])["preset"] == "clean", (
                "Apply did not persist the draft"
            )
        else:
            assert row is None, "Cancel persisted a preview"
        if mode == "position":
            assert position and json.loads(position[0])["position"] == "bottom-right"
        else:
            assert position is None, "Panel position did not inherit its conf default"
    print("Packaged menu startup, Cancel, Apply, restart and panel position passed.")
