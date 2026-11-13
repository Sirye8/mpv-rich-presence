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


def player_samples(
    config,
    media,
    mpv,
    environment,
    check,
    timeout,
    *,
    paused=True,
    tracking=None,
    extra_options=(),
    driver="",
):
    database = config / "scripts/mpv-rich-presence/data.sqlite3"
    with closing(sqlite3.connect(database)) as db, db:
        db.execute(
            "CREATE TABLE IF NOT EXISTS records("
            "category TEXT, scope TEXT, value TEXT, PRIMARY KEY(category, scope))"
        )
        db.execute(
            "INSERT OR REPLACE INTO records VALUES ('tracking','global',?)",
            (json.dumps(tracking or {"mode": "manual", "without_sharing": True}),),
        )
    stop = config / "test-done"
    stop.unlink(missing_ok=True)
    script = config / "test-stop.lua"
    script.write_text(
        'local utils = require("mp.utils")\n'
        "mp.add_periodic_timer(.2, function()\n"
        '    if utils.file_info(os.getenv("MPVRP_TEST_DONE")) then mp.commandv("quit") end\n'
        "end)\n" + driver,
        encoding="utf-8",
    )
    process = subprocess.Popen(
        [
            mpv,
            "--config-dir=" + str(config),
            "--script=" + str(script),
            "--script-opts=rich-presence-on=no",
            "--no-terminal",
            "--pause=yes" if paused else "--pause=no",
            "--vo=null",
            "--ao=null",
            *extra_options,
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
                "35",
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
            assert sample["watch_episode"], sample
            return True

        player_samples(config, anime, mpv, os.environ, season_two, 25)
        print("Youjo Senki II, episode 8 is eligible for watch time")

        for name, ident, episode in (
            ("[Judas] Black Clover - S04E16 (170).mkv", 97940, "170"),
            ("[SubsPlease] Black Clover - 172 (1080p).mkv", 195604, "2"),
        ):
            numbered = root / name
            shutil.copyfile(anime, numbered)

            def numbered_episode(sample):
                if not sample.get("media"):
                    return False
                assert sample["media"] == ident and sample["episode"] == episode, sample
                assert sample["watch_episode"], sample
                return True

            player_samples(config, numbered, mpv, os.environ, numbered_episode, 25)
        print("Distributor S04E16 (170) stays episode 170 and absolute episode 172 becomes Season 2, episode 2")

        folder = root / "Youjo Senki S2"
        folder.mkdir()
        numbered = folder / "08.mkv"
        shutil.copyfile(anime, numbered)
        player_samples(config, numbered, mpv, os.environ, season_two, 25)
        print("Automatic uses the folder title for a bare episode filename")

        black_clover = root / "[SubsPlease] Black Clover - 174 (1080p) [720FB31C].mkv"
        shutil.copyfile(anime, black_clover)
        database = config / "scripts/mpv-rich-presence/data.sqlite3"
        with closing(sqlite3.connect(database)) as db, db:
            db.execute(
                "INSERT INTO records VALUES ('account','current',?)",
                (json.dumps({"id": 1, "generation": "watch-test"}),),
            )

        enabled = False

        def watch_time(sample):
            nonlocal enabled
            if not enabled:
                assert not sample["watch"] and not sample["active"], sample
                with closing(sqlite3.connect(database)) as db, db:
                    db.execute(
                        "UPDATE records SET value=? WHERE category='tracking' AND scope='global'",
                        (json.dumps({"mode": "manual", "without_sharing": True}),),
                    )
                enabled = True
                return False
            if not sample.get("media"):
                return False
            assert sample["tracking"]["mode"] == "manual", sample
            assert sample["match_title"] == "Black Clover 2nd Season", sample
            assert sample["episode"] == "4" and sample["watch_episode"], sample
            with closing(sqlite3.connect(database)) as db:
                row = db.execute(
                    "SELECT value FROM records WHERE category='watch' AND scope='1/195604/4/0'"
                ).fetchone()
            return row is not None and json.loads(row[0])["elapsed"] >= 2

        player_samples(
            config,
            black_clover,
            mpv,
            os.environ,
            watch_time,
            25,
            paused=False,
            tracking={"mode": "off", "without_sharing": True},
        )
        print(
            "Switching Off to Manual records Black Clover episode 174 under Season 2, episode 4"
        )

        episode_range = root / "[SubsPlease] Black Clover - 174-175 (1080p).mkv"
        shutil.copyfile(anime, episode_range)

        def range_requires_review(sample):
            if not sample.get("media"):
                return False
            assert not sample["watch_episode"], sample
            return True

        player_samples(
            config, episode_range, mpv, os.environ, range_requires_review, 25
        )
        print("An episode range stays ineligible for watch time")

        correction = root / "[SubsPlease] Match Fixture - 06 (1080p).mkv"
        shutil.copyfile(anime, correction)
        with closing(sqlite3.connect(database)) as db, db:
            db.execute(
                "INSERT INTO records VALUES ('match','global',?)",
                (json.dumps({"id": 195604, "offset": -2}),),
            )

        def corrected_episode(sample):
            if not sample.get("media"):
                return False
            assert sample["media"] == 195604 and sample["episode"] == "4", sample
            assert sample["watch_episode"], sample
            return True

        player_samples(config, correction, mpv, os.environ, corrected_episode, 25)
        with closing(sqlite3.connect(database)) as db, db:
            db.execute("DELETE FROM records WHERE category='match' AND scope='global'")
        print(
            "A manual AniList match and episode offset also supply the tracked episode"
        )

        loki = (
            root / "Loki.S02E04.Heart.of.the.TVA.1080p.DSNP.WEB-DL.DDP5.1.H.264-NTb.mkv"
        )
        shutil.copyfile(anime, loki)

        def loki_episode(sample):
            with closing(sqlite3.connect(database)) as db:
                row = db.execute(
                    "SELECT value FROM records WHERE category='auto'"
                ).fetchone()
            if not row:
                return False
            hit = json.loads(row[0]).get("hit", {})
            if hit.get("name") != "Loki":
                return False
            assert hit["numbering"] == "S02E04", hit
            assert not sample.get("watch_episode"), sample
            return True

        player_samples(config, loki, mpv, os.environ, loki_episode, 30)
        print("Automatic recognition uses Loki S02E04 and leaves AniList tracking off")

        generic = root / "Screen Recording 2026-11-12.mkv"
        shutil.copyfile(anime, generic)
        player_samples(
            config,
            generic,
            mpv,
            os.environ,
            lambda sample: (
                sample["allowed"]
                and not sample.get("media")
                and sample["source"] == "auto"
            ),
            10,
        )
        print("A generic screen recording stays local")

        folder_sequence = r"""
local utils=require("mp.utils")
local function panel(fields)
    mp.commandv("script-message-to","rich_presence","panel",utils.format_json(fields))
end
mp.register_event("file-loaded",function()
    mp.add_timeout(.8,function() panel({action="folder_exception",mode="window"}) end)
    mp.add_timeout(1.8,function() panel({action="private",enabled=true}) end)
    mp.add_timeout(2.8,function() panel({action="private",enabled=false}) end)
    mp.add_timeout(3.8,function() panel({action="folder_filter",mode="disabled"}) end)
end)
"""
        folder_states = []

        def folder_filter(sample):
            value = sample["allowed"]
            if not folder_states or folder_states[-1] != value:
                folder_states.append(value)
            if folder_states != [False, True, False, True]:
                return False
            with closing(sqlite3.connect(database)) as db:
                return (
                    db.execute(
                        "SELECT 1 FROM records WHERE category='folder-filter'"
                    ).fetchone()
                    is not None
                )

        player_samples(
            config,
            generic,
            mpv,
            os.environ,
            folder_filter,
            12,
            extra_options=(
                "--script-opts-append=rich-presence-folder_filter_mode=whitelist",
            ),
            driver=folder_sequence,
        )
        with closing(sqlite3.connect(database)) as db, db:
            saved = db.execute(
                "SELECT value FROM records WHERE category='folder-filter' AND scope='global'"
            ).fetchone()
            assert saved and json.loads(saved[0])["mode"] == "disabled"
            db.execute("DELETE FROM records WHERE category='folder-filter'")
        print(
            "A window exception allows an empty whitelist and still honors private mode"
        )

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
