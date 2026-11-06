# SPDX-License-Identifier: AGPL-3.0-or-later
"""Interactive read-only smoke test. Requires the viewer's approval in a browser."""

import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time


def verify(package, mpv):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for part in ("scripts", "script-modules", "script-opts"):
            shutil.copytree(package / part, root / part)
        # Use the interpreter running the test, even if the GUI has a different PATH.
        with (root / "script-opts" / "rich-presence.conf").open(
            "a", encoding="utf-8"
        ) as config:
            config.write("\npython=" + sys.executable.replace("\\", "/") + "\n")
        stop = root / "done"
        log = root / "mpv.log"
        driver = root / "signin-test.lua"
        driver.write_text(
            """
local utils=require("mp.utils")
local keys={"DOWN","DOWN","ENTER"}
mp.add_timeout(1,function()
    mp.commandv("script-message-to","rich_presence_conf","menu","AniList")
    local timer
    timer=mp.add_periodic_timer(.4,function()
        local key=table.remove(keys,1)
        if key then mp.commandv("keypress",key) else timer:kill() end
    end)
end)
mp.add_periodic_timer(.3,function()
    if utils.file_info(os.getenv("MPVRP_SIGNIN_DONE")) then mp.commandv("quit") end
end)
""",
            encoding="utf-8",
        )
        process = subprocess.Popen(
            [
                mpv,
                "--config-dir=" + str(root),
                "--script=" + str(driver),
                "--idle=yes",
                "--no-terminal",
                "--vo=null",
                "--ao=null",
                "--log-file=" + str(log),
            ],
            env=dict(os.environ, MPVRP_SIGNIN_DONE=str(stop)),
        )
        try:
            db_path = root / "rich-presence" / "data.sqlite3"
            deadline = time.monotonic() + 240
            while time.monotonic() < deadline and process.poll() is None:
                time.sleep(0.5)
                if log.is_file():
                    messages = log.read_text(encoding="utf-8", errors="replace")
                    if (
                        "stack traceback" in messages
                        or "Account helper unavailable" in messages
                    ):
                        errors = [
                            line
                            for line in messages.splitlines()
                            if "[f]" in line or "[w][rich_presence_conf]" in line
                        ]
                        raise RuntimeError(
                            "Player startup failed:\n" + "\n".join(errors)
                        )
                if not db_path.is_file():
                    continue
                db = sqlite3.connect(db_path)
                try:
                    row = db.execute(
                        "SELECT value FROM records WHERE category='account' AND scope='current'"
                    ).fetchone()
                    if row:
                        account = json.loads(row[0])
                        if (
                            not account.get("id")
                            or not account.get("name")
                            or account.get("persistent")
                        ):
                            raise RuntimeError(
                                "Expected verified viewer and session-only storage"
                            )
                        print(
                            "Verified public viewer sign-in from the installed mpv menu."
                        )
                        return
                    reply = db.execute(
                        "SELECT value FROM records WHERE category='response' LIMIT 1"
                    ).fetchone()
                    if reply and json.loads(reply[0]).get("offline"):
                        raise RuntimeError(
                            json.loads(reply[0]).get(
                                "message", "Account request failed"
                            )
                        )
                finally:
                    db.close()
            raise RuntimeError("No verified viewer before the test deadline")
        finally:
            stop.touch()
            try:
                process.wait(8)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait()


if __name__ == "__main__":
    verify(Path(sys.argv[1]).resolve(), sys.argv[2])
