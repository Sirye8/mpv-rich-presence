# SPDX-License-Identifier: AGPL-3.0-or-later
"""Check packaged Ask reviews and playlist transitions with a saved local proposal."""

import argparse
from contextlib import closing
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import tempfile
import time
import zipfile


DRIVER = r"""
local action=os.getenv("MPVRP_ASK_ACTION")
local auto_dismiss=os.getenv("MPVRP_ASK_DISMISS")=="yes"
local signal=os.getenv("MPVRP_ASK_SIGNAL")
local trace=os.getenv("MPVRP_ASK_TRACE")
local capture=os.getenv("MPVRP_ASK_CAPTURE")
local never=os.getenv("MPVRP_ASK_NEVER")=="yes"
local function log(event)
    local f=assert(io.open(trace,"a"))
    f:write(event," ",mp.get_time(),"\n")
    f:close()
end
local prompt_seen=false
local function review_seen()
    if prompt_seen then return end
    prompt_seen=true
    log("prompt")
    if auto_dismiss then mp.add_timeout(never and 5.5 or .3,function()
        log("dismiss")
        if action=="next" then
            mp.commandv("keypress","END")
            mp.commandv("keypress","ENTER")
        else mp.commandv("keypress","ESC") end
        if action=="wait" then mp.add_timeout(.2,function() mp.commandv("quit") end) end
    end) end
end
mp.observe_property("term-status-msg","string",function(_,text)
    if text and text:find("AniList update",1,true) then review_seen() end
end)
mp.register_event("end-file",function() log("end") end)
local loaded=0
mp.register_event("file-loaded",function()
    loaded=loaded+1
    if loaded==2 then log("next"); mp.commandv("quit") end
end)
local utils=require("mp.utils")
local timer
timer=mp.add_periodic_timer(.05,function()
    if action=="wait" or not utils.file_info(signal) then return end
    timer:kill()
    log("action")
    if action=="eof" then mp.set_property_bool("pause",false)
    elseif action=="next" then mp.commandv("keypress",">")
    else mp.commandv("keypress",action) end
    if capture then
        mp.add_timeout(1.4,function()
            mp.commandv("keypress","]")
            mp.commandv("script-message-to","mpv_rich_presence","menu","Now playing")
        end)
        mp.add_timeout(2,function()
            if mp.get_property_number("speed",1)>1 then log("player-input") end
            mp.commandv("screenshot-to-file",capture,"window")
            log("captured")
            local w,h=mp.get_osd_size()
            local scale=math.min(math.max(.76,math.min(h/740,2.4)),w/300,h/380)
            mp.commandv("mouse",math.floor(w-34*scale),math.floor(68*scale))
            mp.commandv("keydown","MBTN_LEFT")
            mp.add_timeout(.1,function()
                mp.commandv("keyup","MBTN_LEFT")
                log("dismiss")
            end)
        end)
        mp.add_timeout(8,function()
            log("fallback")
            mp.commandv("quit")
        end)
    end
end)
"""


def verify(package, mpv, ffmpeg, capture=None):
    cases = (
        (
            ("threshold", "wait", True, False, True, False),
            ("end", "eof", True, True, True, False),
            ("end", "q", False, False, False, False),
            ("leave", "q", True, False, True, False),
            ("leave", "CLOSE_WIN", True, False, True, False),
            ("leave", "next", True, True, True, False),
            ("leave", "eof", False, True, True, False),
            ("leave", "eof", True, True, True, True),
        )
        if not capture
        else (("leave", "eof", True, True, True, True),)
    )
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        driver = root / "ask-driver.lua"
        driver.write_text(DRIVER, encoding="utf-8")
        clip = root / "[SubsPlease] Black Clover - 174 (1080p) [720FB31C].mkv"
        subprocess.run(
            [
                ffmpeg,
                "-v",
                "error",
                "-f",
                "lavfi",
                "-i",
                "gradients=s=1280x720:r=24:c0=0x102133:c1=0x3b6574:c2=0x101820:n=3:speed=0.005"
                if capture
                else "color=s=160x90:r=4",
                "-t",
                "1",
                "-c:v",
                "ffv1",
                str(clip),
            ],
            check=True,
        )
        next_clip = root / "Next clip.mkv"
        shutil.copyfile(clip, next_clip)
        for index, (timing, action, dismiss, playlist, expected, never) in enumerate(
            cases
        ):
            config = root / f"mpv-{index}"
            with zipfile.ZipFile(package) as z:
                z.extractall(config)
            database = config / "scripts/mpv-rich-presence/data.sqlite3"
            signal, trace = config / "go", config / "trace"
            key = "1/195604/4/0"
            records = [
                ("account", "current", {"id": 1, "generation": "ask-test"}),
                (
                    "tracking",
                    "global",
                    {
                        "mode": "ask",
                        "ask_timing": timing,
                        "ask_timeout": 0 if never else 5,
                        "without_sharing": True,
                    },
                ),
                (
                    "pending",
                    key,
                    {
                        "key": key,
                        "account": "ask-test",
                        "title": "Black Clover 2nd Season",
                        "media": 195604,
                        "episode": 4,
                        "before": 3,
                        "status": None,
                        "base": {"session": 1, "id": 2, "status": "CURRENT"},
                    },
                ),
            ]
            with closing(sqlite3.connect(database)) as db, db:
                db.execute(
                    "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
                )
                db.executemany(
                    "INSERT INTO records VALUES(?,?,?)",
                    (
                        (category, scope, json.dumps(value))
                        for category, scope, value in records
                    ),
                )
            args = [
                mpv,
                f"--config-dir={config}",
                "--script-opts=rich-presence-on=no",
                "--pause",
                "--vo=gpu-next" if capture else "--vo=null",
                "--ao=null",
                f"--script={driver}",
                str(clip),
            ]
            if playlist:
                args.append(str(next_clip))
            env = dict(
                os.environ,
                MPVRP_ASK_ACTION=action,
                MPVRP_ASK_DISMISS="yes" if dismiss else "no",
                MPVRP_ASK_SIGNAL=str(signal),
                MPVRP_ASK_TRACE=str(trace),
                MPVRP_ASK_NEVER="yes" if never else "no",
            )
            if capture:
                env["MPVRP_ASK_CAPTURE"] = str(capture.resolve())
            process = subprocess.Popen(args, env=env)
            try:
                deadline = time.monotonic() + 20
                with closing(sqlite3.connect(database)) as db:
                    while time.monotonic() < deadline and process.poll() is None:
                        row = db.execute(
                            "SELECT value FROM records WHERE category='player'"
                        ).fetchone()
                        if row and json.loads(row[0]).get("watch_episode"):
                            break
                        time.sleep(0.05)
                    else:
                        raise AssertionError(
                            f"{timing}/{action}: recognition timed out"
                        )
                if action != "wait":
                    assert not trace.exists(), (
                        f"{timing}/{action}: popup appeared early"
                    )
                    # Let the Lua bridge receive and arm the ready proposal.
                    time.sleep(0.5)
                    signal.touch()
                assert process.wait(15) == 0
                events = [
                    (name, float(when))
                    for name, when in (
                        line.split()
                        for line in trace.read_text(encoding="utf-8").splitlines()
                    )
                ]
                names = [name for name, _ in events]
                if capture:
                    assert "fallback" not in names, events
                    assert names.index("player-input") < names.index("captured"), events
                    assert names.index("captured") < names.index("end"), events
                    assert capture.is_file(), (
                        "mpv failed to capture the graphical prompt"
                    )
                else:
                    assert ("prompt" in names) == expected, (timing, action, events)
                if expected and not capture:
                    assert names.index("prompt") < names.index("end"), events
                    if dismiss:
                        assert names.index("dismiss") < names.index("end"), events
                        if never:
                            assert (
                                dict(events)["dismiss"] - dict(events)["prompt"] >= 5.2
                            ), events
                    else:
                        prompt_time = next(
                            when for name, when in events if name == "prompt"
                        )
                        end_time = next(when for name, when in events if name == "end")
                        assert end_time - prompt_time >= 4.6, events
                if playlist:
                    assert names.index("end") < names.index("next"), events
                with closing(sqlite3.connect(database)) as db:
                    pending = db.execute(
                        "SELECT 1 FROM records WHERE category='pending' AND scope=?",
                        (key,),
                    ).fetchone()
                    if action == "next":
                        assert pending is None, events
                        assert db.execute(
                            "SELECT 1 FROM records WHERE category='skipped' AND scope=?",
                            (key,),
                        ).fetchone()
                    else:
                        assert pending, events
                print(
                    f"{timing} / {action}: {'held through review' if expected and action != 'wait' else 'passed'}",
                    flush=True,
                )
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("mpv")
    parser.add_argument("ffmpeg")
    parser.add_argument("--capture", type=Path, help="capture the graphical EOF review")
    options = parser.parse_args()
    verify(options.package, options.mpv, options.ffmpeg, options.capture)
