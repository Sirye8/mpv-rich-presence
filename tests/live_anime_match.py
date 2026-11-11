# SPDX-License-Identifier: AGPL-3.0-or-later
"""Check the reported season match and provider priority in an isolated mpv config."""

import argparse
from contextlib import closing
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import tempfile
import threading
import time
import zipfile


def player_samples(config, media, mpv, environment, check, timeout):
    database = config / "scripts/mpv-rich-presence/data.sqlite3"
    with closing(sqlite3.connect(database)) as db, db:
        db.execute(
            "CREATE TABLE IF NOT EXISTS records("
            "category TEXT, scope TEXT, value TEXT, PRIMARY KEY(category, scope))"
        )
        db.execute(
            "INSERT OR REPLACE INTO records VALUES ('tracking','global',?)",
            (json.dumps({"mode": "manual", "without_sharing": True}),),
        )
    stop = config / "test-done"
    stop.unlink(missing_ok=True)
    script = config / "test-stop.lua"
    script.write_text(
        'local utils = require("mp.utils")\n'
        "mp.add_periodic_timer(.2, function()\n"
        '    if utils.file_info(os.getenv("MPVRP_TEST_DONE")) then mp.commandv("quit") end\n'
        "end)\n",
        encoding="utf-8",
    )
    process = subprocess.Popen(
        [
            mpv,
            "--config-dir=" + str(config),
            "--script=" + str(script),
            "--script-opts=rich-presence-on=no",
            "--no-terminal",
            "--pause",
            "--vo=null",
            "--ao=null",
            str(media),
        ],
        env=dict(environment, MPVRP_TEST_DONE=str(stop)),
    )
    try:
        deadline = time.monotonic() + timeout
        with closing(sqlite3.connect(database)) as db:
            while process.poll() is None and time.monotonic() < deadline:
                row = db.execute(
                    "SELECT value FROM records WHERE category='player'"
                ).fetchone()
                if row and check(json.loads(row[0])):
                    return
                time.sleep(0.2)
        raise AssertionError("Expected recognition result was not observed")
    finally:
        stop.touch()
        try:
            process.wait(5)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait()


def verify(package, mpv, ffmpeg):
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        config = root / "mpv"
        if package.is_dir():
            for part in ("scripts", "script-opts"):
                shutil.copytree(package / part, config / part)
        else:
            with zipfile.ZipFile(package) as archive:
                archive.extractall(config)
        anime = root / "[SubsPlease] Youjo Senki S2 - 08 (1080p).mkv"
        subprocess.run(
            [
                ffmpeg,
                "-loglevel",
                "error",
                "-f",
                "lavfi",
                "-i",
                "color=black:s=160x90:r=4",
                "-t",
                "1",
                "-c:v",
                "ffv1",
                str(anime),
            ],
            check=True,
        )

        def season_two(sample):
            if not sample.get("media"):
                return False
            assert sample["match_title"] == "Youjo Senki II", sample["match_title"]
            assert sample["episode"] == "8", sample["episode"]
            return True

        player_samples(config, anime, mpv, os.environ, season_two, 25)
        print("Youjo Senki II, episode 8")

        requests = []

        class OfflineAniList(BaseHTTPRequestHandler):
            def do_CONNECT(self):
                requests.append(self.path)
                self.send_error(503)

            def log_message(self, *args):
                pass

        tv = root / "Severance.S01E02.mkv"
        shutil.copyfile(anime, tv)
        with HTTPServer(("127.0.0.1", 0), OfflineAniList) as proxy:
            thread = threading.Thread(target=proxy.serve_forever)
            thread.start()
            try:
                url = f"http://127.0.0.1:{proxy.server_port}"
                environment = dict(
                    os.environ,
                    https_proxy=url,
                    HTTPS_PROXY=url,
                    NO_PROXY="",
                    no_proxy="",
                )
                start = time.monotonic()

                def still_local(sample):
                    if not sample.get("allowed"):
                        return False
                    assert sample["source"] == "original", (
                        "A failed AniList request enabled another provider"
                    )
                    return bool(requests) and time.monotonic() - start >= 4

                player_samples(config, tv, mpv, environment, still_local, 12)
                assert requests and set(requests) == {"graphql.anilist.co:443"}, (
                    requests
                )
                print(
                    "AniList outage keeps the local title without trying another provider"
                )
            finally:
                proxy.shutdown()
                thread.join()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("mpv")
    parser.add_argument("ffmpeg")
    args = parser.parse_args()
    verify(args.package, args.mpv, args.ffmpeg)
