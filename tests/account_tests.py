# SPDX-License-Identifier: AGPL-3.0-or-later
import json
from pathlib import Path
import subprocess
import sqlite3
import io
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import contextlib
import urllib.error
import urllib.parse
import urllib.request

sys.path.insert(
    0, str(Path(__file__).resolve().parents[1] / "script-modules/mpv-rich-presence")
)
from account import (
    AccountError,
    ClientSecretRequired,
    callback_server,
    read_entry,
    save_progress,
)
from backend import Worker
from providers import fingerprint_match, music_art, music_art_sources, musicbrainz_match


class CallbackTests(unittest.TestCase):
    def test_personal_form(self):
        def exchange(code, secret, remember):
            if not secret:
                raise ClientSecretRequired()
            if secret != "fixture":
                raise AccountError("denied")
            return "test-token"

        for supplied, origin_header, expected in [
            ("fixture", "null", 200),
            ("fixture", None, 200),
            ("bad", "same", 400),
            ("fixture", "https://example.org", 403),
        ]:
            with callback_server(
                "test-state", port=0, code_flow=True, exchange=exchange
            ) as server:
                origin = "http://127.0.0.1:%d" % server.server_port
                thread = threading.Thread(target=server.serve_forever)
                thread.start()
                try:
                    with urllib.request.urlopen(
                        origin + "/callback?code=x&state=test-state"
                    ) as response:
                        self.assertIn(b'type="password"', response.read())
                    # The foreign-origin check rejects the request before reading
                    # its body. An empty POST avoids a TCP reset on Windows when
                    # the server closes a socket with unread data.
                    payload = (
                        b""
                        if expected == 403
                        else urllib.parse.urlencode(
                            dict(state="test-state", secret=supplied)
                        ).encode()
                    )
                    req = urllib.request.Request(
                        origin + "/finish",
                        data=payload,
                        headers=(
                            {
                                "Origin": origin
                                if origin_header == "same"
                                else origin_header
                            }
                            if origin_header
                            else {}
                        ),
                    )
                    try:
                        with urllib.request.urlopen(req) as response:
                            status = response.status
                    except urllib.error.HTTPError as exc:
                        status = exc.code
                    self.assertEqual(status, expected)
                finally:
                    server.shutdown()
                    thread.join()

    def test_code_and_fragment(self):
        for code_flow in (True, False):
            with callback_server("test-state", port=0, code_flow=code_flow) as server:
                origin = "http://127.0.0.1:%d" % server.server_port
                thread = threading.Thread(target=server.serve_forever)
                thread.start()
                try:
                    with self.assertRaises(OSError):
                        callback_server("another-state", port=server.server_port)

                    def request(state):
                        params = urllib.parse.urlencode(
                            {"state": state, "code": "dummy", "access_token": "fixture"}
                        )
                        if code_flow:
                            return urllib.request.urlopen(
                                origin + "/callback?" + params
                            )
                        return urllib.request.urlopen(
                            urllib.request.Request(
                                origin + "/callback",
                                data=params.encode(),
                                headers={"Origin": origin},
                            )
                        )

                    with self.assertRaises(urllib.error.HTTPError):
                        request("wrong-state")
                    self.assertIsNone(server.result)
                    with request("test-state") as response:
                        self.assertEqual(response.status, 200)
                    self.assertEqual(server.result, "dummy" if code_flow else "fixture")
                finally:
                    server.shutdown()
                    thread.join()


class PublicSignInTests(unittest.TestCase):
    def test_wake_before_login(self):
        from account import wake_exchange

        base = "https://login.example.org"
        health = dict(
            service="mpv-anilist-login",
            client_id="55822",
            redirect_uri=base + "/callback",
        )
        attempts = []

        def opening(url, timeout):
            attempts.append(url.full_url)
            if len(attempts) == 1:
                raise urllib.error.HTTPError(url, 503, "Starting", None, None)
            return io.BytesIO(json.dumps(health).encode())

        wake_exchange(base, "55822", lambda: None, opening, sleep=lambda _: None)
        self.assertEqual(attempts, [base + "/health"] * 2)
        with self.assertRaises(AccountError):
            wake_exchange(base, "other", lambda: None, opening)


class RecognitionTests(unittest.TestCase):
    def test_episode_verification_needs_exact_live_action_episode(self):
        with tempfile.TemporaryDirectory() as directory:
            worker = Worker(Path(directory) / "db", "window", {})
            try:
                worker.db.execute(
                    "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
                )
                filename = "Loki.S02E04.Heart.of.the.TVA.1080p.DSNP.WEB-DL.mkv"
                loki = dict(id=84958, kind="tv", name="Loki", year="2021")
                cases = (
                    (loki, "Drama, Sci-Fi & Fantasy", "S02E04", True),
                    (loki, "Animation", "S02E04", False),
                    (loki, "Drama", "", False),
                    (
                        dict(loki, name="Matantei Loki RAGNAROK"),
                        "Drama",
                        "S02E04",
                        False,
                    ),
                )
                for session, (row, genre, numbering, expected) in enumerate(cases, 1):
                    player = dict(
                        source="verify-tv",
                        session=session,
                        title=filename,
                        filename=filename,
                        video=True,
                        allowed=True,
                        duration=2891,
                    )
                    worker.put("player", "window", player)
                    worker.put("refresh", "window", {"id": session})
                    with (
                        patch("backend.candidates", return_value=[row]) as search,
                        patch(
                            "backend.details",
                            return_value=dict(row, genre=genre, numbering=numbering),
                        ) as details,
                    ):
                        worker.recognize(player)
                    self.assertEqual(
                        search.call_args_list[0].args[:2], ("tmdb-tv", "Loki")
                    )
                    self.assertEqual(
                        bool(worker.get("auto", "window")["hit"]), expected
                    )
                    if row == loki:
                        self.assertEqual(
                            details.call_args_list[0].args[4], ("02", "04")
                        )
            finally:
                worker.db.close()

    def test_automatic_short_video_does_not_search_a_bare_movie_title(self):
        with tempfile.TemporaryDirectory() as directory:
            worker = Worker(Path(directory) / "db", "window", {})
            try:
                worker.db.execute(
                    "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
                )
                player = dict(
                    source="auto",
                    session=1,
                    title="Interstellar",
                    filename="Interstellar.mp4",
                    video=True,
                    allowed=True,
                    duration=45,
                )
                worker.put("player", "window", player)
                with patch("backend.candidates") as search:
                    worker.recognize(player)
                search.assert_not_called()
            finally:
                worker.db.close()

    def test_fingerprint_fallback_keeps_tied_recordings_unresolved(self):
        rows = [
            dict(id="first", name="Track", artist="One", score=0.99, groups=[]),
            dict(id="second", name="Track", artist="Two", score=0.99, groups=[]),
        ]
        self.assertIsNone(fingerprint_match(rows, "", "", ""))
        self.assertEqual(fingerprint_match(rows, "Track", "Two", "")["id"], "second")
        self.assertIsNone(
            fingerprint_match([dict(rows[0], score=0.5)], "Track", "One", "")
        )

    def test_feature_verification_requires_title_year_and_live_action_genre(self):
        with tempfile.TemporaryDirectory() as directory:
            worker = Worker(Path(directory) / "db", "window", {})
            try:
                worker.db.execute(
                    "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
                )
                filename = "Interstellar (2014) (2160p x265 10bit HDR Tigole).mkv"
                for session, genre in enumerate(
                    ("Adventure, Science Fiction", "Animation", ""), 1
                ):
                    sample = dict(
                        source="verify-movie",
                        session=session,
                        title=filename,
                        filename=filename,
                        video=True,
                        allowed=True,
                        duration=10140,
                    )
                    worker.put("player", "window", sample)
                    worker.put("refresh", "window", {"id": session})
                    candidate = dict(
                        id=157336, kind="movie", name="Interstellar", year="2014"
                    )
                    with (
                        patch("backend.candidates", return_value=[candidate]) as search,
                        patch(
                            "backend.details",
                            return_value=dict(
                                candidate, genre=genre, provider="tmdb-movie"
                            ),
                        ),
                    ):
                        worker.recognize(sample)
                    self.assertEqual(
                        search.call_args.args[:2], ("tmdb-movie", "Interstellar (2014)")
                    )
                    self.assertEqual(
                        bool(worker.get("auto", "window")["hit"]), session == 1
                    )
            finally:
                worker.db.close()

    def test_music_matches_feature_credits_and_remaster_titles(self):
        with tempfile.TemporaryDirectory() as directory:
            worker = Worker(Path(directory) / "db", "window", {})
            try:
                worker.db.execute(
                    "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
                )
                cases = (
                    (
                        "Blame (feat. John Newman)",
                        "Calvin Harris;John Newman",
                        "Motion",
                        214,
                        "Blame",
                        "Calvin Harris feat. John Newman",
                        ["Calvin Harris", "John Newman"],
                        210253,
                    ),
                    (
                        "Hotel California - 2013 Remaster",
                        "Eagles",
                        "Hotel California (2013 Remaster)",
                        391,
                        "Hotel California",
                        "Eagles",
                        ["Eagles"],
                        390000,
                    ),
                )
                for session, (
                    title,
                    artist,
                    album,
                    duration,
                    name,
                    credit,
                    artists,
                    length,
                ) in enumerate(cases, 1):
                    sample = dict(
                        source="auto",
                        session=session,
                        title=title,
                        artist=artist,
                        album=album,
                        duration=duration,
                        video=False,
                        allowed=True,
                    )
                    worker.put("player", "window", sample)
                    hit = dict(
                        id=session,
                        kind="recording",
                        name=name,
                        artist=credit,
                        artist_names=artists,
                        album=album,
                        duration_ms=length,
                        cover="cover",
                    )
                    with (
                        patch("backend.candidates", return_value=[hit]) as search,
                        patch("backend.fingerprint") as acoustid,
                    ):
                        worker.recognize(sample)
                    self.assertEqual(search.call_args.args[:2], ("musicbrainz", name))
                    acoustid.assert_not_called()
                    self.assertEqual(
                        worker.get("auto", "window")["hit"]["provider"], "musicbrainz"
                    )
            finally:
                worker.db.close()

    def test_music_edition_matching_with_bilingual_titles(self):
        album = "よふかしのうた - Yofukashino Uta"
        recording = dict(
            id="recording",
            name="よふかしのうた",
            artist="Creepy Nuts",
            album="よふかしのうた",
            duration_ms=237000,
        )
        single = "20c3d5a5-297a-4b9c-8f7c-abb5ff8786ef"
        single_group = "7fa49ca2-4fd9-423f-bda9-7673ced09eb9"
        editions = [
            dict(
                recording,
                release="ep",
                release_group="ep-group",
                release_date="2019-08-07",
            ),
            dict(
                recording,
                release=single,
                release_group=single_group,
                release_date="2018-12-12",
            ),
        ]
        hit = musicbrainz_match(
            editions, album, "Creepy Nuts", album, 240, "2018-12-12"
        )
        self.assertEqual(hit["id"], "recording")
        self.assertEqual(
            music_art_sources(hit),
            (("release", single), ("release-group", single_group)),
        )
        self.assertIsNone(
            musicbrainz_match(editions, album, "Other artist", album, 240)
        )
        self.assertIsNone(musicbrainz_match(editions, album, "Creepy Nuts", album, 99))

    def test_music_album_spellings_and_available_cover(self):
        release = "78742576-dba3-4fd5-99f0-bda8b27f79bf"
        group = "8a24ae4f-704d-4065-912a-beba2659a386"
        song = dict(
            id="song",
            name="Магниты",
            artist="Aspen",
            release=release,
            release_group=group,
        )
        hit = musicbrainz_match(
            [dict(song, album="lll")], "Магниты", "Aspen", "III", 218
        )
        self.assertEqual(hit["album"], "III")
        cover = "https://archive.org/path/cover.jpg"
        with patch(
            "providers.cover_art_image",
            side_effect=lambda source, _: cover if source[0] == "release-group" else "",
        ):
            self.assertEqual(music_art(music_art_sources(hit)), cover)

    def test_music_publishes_before_artwork_and_retries_a_temporary_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            worker = Worker(Path(directory) / "db", "window", {})
            try:
                worker.db.execute(
                    "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
                )
                player = dict(
                    source="auto",
                    session=1,
                    title="Artist - Track",
                    track_title="Track",
                    artist="Artist",
                    album="Album",
                    video=False,
                    allowed=True,
                )
                worker.put("player", "window", player)
                row = dict(
                    id="track",
                    kind="recording",
                    name="Track",
                    artist="Artist",
                    album="Album",
                    release="78742576-dba3-4fd5-99f0-bda8b27f79bf",
                )
                cover = "https://archive.org/path/cover.jpg"

                def artwork(_):
                    hit = worker.get("auto", "window")["hit"]
                    self.assertEqual(hit["name"], "Track")
                    self.assertNotIn("cover", hit)
                    if art.call_count <= 2:
                        raise TimeoutError()
                    return cover

                with (
                    patch("backend.candidates", return_value=[row]) as search,
                    patch("backend.music_art", side_effect=artwork) as art,
                ):
                    for _ in range(2):
                        worker.recognize(player)
                        self.assertGreater(worker.music_retry, time.monotonic())
                        worker.music_retry = time.monotonic() - 1
                    worker.recognize(player)
                    self.assertEqual(
                        worker.get("auto", "window")["hit"]["cover"], cover
                    )
                    player.update(session=2, path="different.flac")
                    worker.put("player", "window", player)
                    worker.recognize(player)
                    self.assertEqual(
                        worker.get("auto", "window")["hit"]["cover"], cover
                    )
                    self.assertEqual(search.call_count, 1)
                    self.assertEqual(art.call_count, 3)
            finally:
                worker.db.close()

    def test_scoped_video_sources(self):
        cases = [
            ("movie", "Film.2024.mkv", True, ["tmdb-movie", "wikidata"], "wikidata"),
            ("movie", "Missing Film.mkv", True, ["tmdb-movie", "wikidata"], None),
            ("tv", "Show.mkv", True, ["tmdb-tv"], "tmdb-tv"),
            ("tv", "Daily.2024-01-02.mkv", True, ["tvmaze"], "tvmaze"),
            ("movie-tv", "Show.S01E02.mkv", True, ["tmdb-tv"], "tmdb-tv"),
            ("movie-tv", "Film.mkv", True, ["tmdb-movie", "wikidata"], "wikidata"),
            (
                "movie-tv",
                "Show.mkv",
                True,
                ["tmdb-movie", "wikidata", "tmdb-tv"],
                "tmdb-tv",
            ),
            ("movie-tv", "Track.flac", False, [], None),
        ]
        with tempfile.TemporaryDirectory() as directory:
            worker = Worker(Path(directory) / "db", "window", {})
            try:
                worker.db.execute(
                    "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
                )
                for session, (source, title, video, calls, expected) in enumerate(
                    cases, 1
                ):
                    with self.subTest(source=source, title=title):
                        player = dict(
                            source=source,
                            session=session,
                            title=title,
                            allowed=True,
                            video=video,
                        )
                        worker.put("player", "window", player)

                        def lookup(provider, query, *_args, **_kwargs):
                            if provider != expected:
                                return []
                            name = query.split(" (")[0]
                            kind = "movie" if provider == "wikidata" else "tv"
                            return [dict(id=7, name=name, kind=kind, year="2024")]

                        with (
                            patch("backend.candidates", side_effect=lookup) as search,
                            patch(
                                "backend.details",
                                side_effect=lambda provider, hit, *_: dict(
                                    hit, provider=provider
                                ),
                            ),
                        ):
                            worker.recognize(player)
                        self.assertEqual(
                            [call.args[0] for call in search.call_args_list], calls
                        )
                        if video:
                            self.assertEqual(
                                worker.get("auto", "window")["hit"].get("provider"),
                                expected,
                            )
            finally:
                worker.db.close()

    def test_default_dispatch_and_ambiguous_remakes(self):
        with tempfile.TemporaryDirectory() as directory:
            worker = Worker(Path(directory) / "db", "window", {})
            try:
                worker.db.execute(
                    "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
                )
                cases = [
                    (
                        "Interstellar.2014.1080p.mkv",
                        True,
                        {},
                        "wikidata",
                        "Interstellar",
                        [
                            dict(
                                id="Q1", kind="movie", name="Interstellar", year="2014"
                            ),
                            dict(
                                id="Q2", kind="movie", name="Interstellar", year="2024"
                            ),
                        ],
                        "Q1",
                    ),
                    (
                        "Severance_S01E02.mkv",
                        True,
                        {},
                        "tvmaze",
                        "Severance",
                        [
                            dict(id=7, kind="tv", name="Severance"),
                        ],
                        7,
                    ),
                    (
                        "Dune.Part.Two.2024.mkv",
                        True,
                        {},
                        "tmdb-movie",
                        "Dune Part Two (2024)",
                        [dict(id=9, kind="movie", name="Dune: Part Two", year="2024")],
                        9,
                    ),
                    (
                        "Track Title",
                        False,
                        {"artist": "Artist", "album": "Album"},
                        "musicbrainz",
                        "Track Title",
                        [
                            dict(
                                id="track",
                                kind="recording",
                                name="Track Title",
                                artist="Artist",
                                album="Album",
                            ),
                        ],
                        "track",
                    ),
                    (
                        "Interstellar.mkv",
                        True,
                        {},
                        "wikidata",
                        "Interstellar",
                        [
                            dict(
                                id="Q1", kind="movie", name="Interstellar", year="2014"
                            ),
                            dict(
                                id="Q2", kind="movie", name="Interstellar", year="2024"
                            ),
                        ],
                        None,
                    ),
                ]
                for session, (
                    label,
                    video,
                    tags,
                    source,
                    title,
                    hits,
                    expected,
                ) in enumerate(cases, 1):
                    player = dict(
                        source="auto",
                        session=session,
                        title=label,
                        allowed=True,
                        video=video,
                        **tags,
                    )
                    worker.put("player", "window", player)

                    def lookup(provider, *_args, **_kwargs):
                        if provider.startswith("tmdb-") and provider != source:
                            raise AccountError("Shared TMDB token unavailable")
                        return hits

                    with (
                        patch("backend.candidates", side_effect=lookup) as search,
                        patch(
                            "backend.details",
                            side_effect=lambda provider, hit, *_: dict(
                                hit, provider=provider
                            ),
                        ) as detail,
                    ):
                        worker.recognize(player)
                    self.assertEqual(search.call_args.args[:2], (source, title))
                    if source in ("wikidata", "tvmaze"):
                        preferred = "tmdb-movie" if source == "wikidata" else "tmdb-tv"
                        self.assertEqual(search.call_args_list[0].args[0], preferred)
                    self.assertEqual(
                        worker.get("auto", "window")["hit"].get("id"), expected
                    )
                    if source == "tvmaze":
                        self.assertEqual(detail.call_args.args[4], ("01", "02"))

                for session, player in enumerate(
                    [
                        dict(
                            title="https://host.example/signed?id=token",
                            remote=True,
                            allowed=True,
                        ),
                        dict(title="Interstellar", allowed=False),
                    ],
                    6,
                ):
                    player.update(source="auto", session=session, video=True)
                    worker.put("player", "window", player)
                    with patch("backend.candidates") as search:
                        worker.recognize(player)
                        search.assert_not_called()
            finally:
                worker.db.close()


class AccountTests(unittest.TestCase):
    def test_helper_releases_database_when_owner_is_killed(self):
        with tempfile.TemporaryDirectory() as directory:
            db_path = Path(directory) / "data.sqlite3"
            with contextlib.closing(sqlite3.connect(db_path)) as db, db:
                db.execute(
                    "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
                )
                db.executemany(
                    "INSERT INTO records VALUES(?,?,?)",
                    [
                        ("player", "test", '{"session":1}'),
                        (
                            "job",
                            "ready",
                            '{"window":"test","session":1,"op":"clear","category":"cache"}',
                        ),
                    ],
                )
            backend = (
                Path(__file__).resolve().parents[1]
                / "script-modules/mpv-rich-presence/backend.py"
            )
            supervisor = (
                "import os, subprocess, sys, time\n"
                "subprocess.Popen([sys.executable, '-B', sys.argv[1], sys.argv[2], "
                "'test', '{}', sys.argv[2]+'.conf', str(os.getpid())], "
                "stdout=sys.stdout, stderr=sys.stdout)\n"
                "time.sleep(60)\n"
            )
            owner = subprocess.Popen(
                [sys.executable, "-c", supervisor, str(backend), str(db_path)],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            try:
                deadline = time.monotonic() + 8
                with contextlib.closing(sqlite3.connect(db_path)) as db:
                    while time.monotonic() < deadline:
                        if db.execute(
                            "SELECT 1 FROM records WHERE category='response'"
                        ).fetchone():
                            break
                        time.sleep(0.05)
                    else:
                        self.fail("Account helper never opened the test database")
            finally:
                owner.kill()
                # The worker also inherited the pipe. EOF proves that it exited.
                owner.communicate(timeout=8)
            deadline = time.monotonic() + 4
            while True:
                try:
                    db_path.rename(db_path.with_name("closed.sqlite3"))
                    break
                except PermissionError as exc:
                    if (
                        getattr(exc, "winerror", None) != 32
                        or time.monotonic() >= deadline
                    ):
                        raise
                    time.sleep(0.05)

    def test_quiet_and_manual_list_refresh(self):
        with tempfile.TemporaryDirectory() as directory:
            worker = Worker(Path(directory) / "db", "window", {})
            try:
                worker.db.execute(
                    "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
                )
                worker.put("account", "current", {"id": 5, "generation": "test"})
                worker.put(
                    "player", "window", {"session": 7, "media": 18, "allowed": True}
                )
                job = {"op": "read", "account": "test", "session": 7, "media": 18}
                worker.notice_page = "AniList"
                with (
                    patch.object(worker, "credential", return_value="test"),
                    patch(
                        "backend.read_entry",
                        return_value=dict(id=3, progress=3, status="CURRENT", repeat=0),
                    ),
                ):
                    worker.handle(dict(job, quiet=True))
                    response = worker.get("response", "window")
                    self.assertEqual(response["entry"]["progress"], 3)
                    self.assertNotIn("notice", response)
                    worker.handle(job)
                    response = worker.get("response", "window")
                    self.assertEqual(response["message"], "Progress refreshed.")
                    self.assertEqual(response["notice_page"], "AniList")
                    worker.handle(job)
                    self.assertNotEqual(
                        worker.get("response", "window")["notice"], response["notice"]
                    )
            finally:
                worker.db.close()

    def test_conflict_and_lost_response(self):
        row = dict(
            id=10,
            userId=1,
            mediaId=5,
            progress=6,
            status="CURRENT",
            repeat=0,
            updatedAt=1,
        )
        calls = []

        def transport(query, args, token):
            calls.append(args)
            if query.startswith("mutation"):
                self.assertEqual(args, {"id": 10, "progress": 7})
                row["progress"] = 7
                raise AccountError("timeout")
            return {"Page": {"mediaList": [dict(row)]}}

        base = dict(
            id=10,
            viewer=1,
            media=5,
            progress=6,
            status="CURRENT",
            repeat=0,
            updatedAt=1,
        )
        self.assertEqual(
            save_progress("dummy", base, 7, 12, lambda: None, transport)["progress"], 7
        )
        with self.assertRaises(AccountError):
            save_progress("dummy", base, 7, 12, lambda: None, transport)
        self.assertEqual(sum("progress" in c for c in calls), 1)

    def test_scoped_clear_and_stale_response(self):
        with tempfile.TemporaryDirectory() as temp:
            worker = Worker(Path(temp) / "db", "left", {})
            try:
                worker.db.execute(
                    "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
                )
                worker.put(
                    "player", "left", {"session": 1, "scope_path": "/private/show.mkv"}
                )
                worker.put(
                    "player",
                    "right",
                    {"session": 2, "scope_path": "/private-other/show.mkv"},
                )
                worker.put("exclude", "folder:/private", {"blocked": True})
                worker.put("cache", "art", {"url": "fixture"})
                worker.handle(
                    {
                        "op": "clear",
                        "category": "exclude",
                        "scope": "folder:/private",
                        "session": 1,
                    }
                )
                self.assertTrue(
                    worker.get("privacy", "file:/private/show.mkv")["block_lookup"]
                )
                self.assertEqual(
                    worker.get("privacy", "file:/private-other/show.mkv"), {}
                )
                self.assertEqual(worker.get("cache", "art"), {"url": "fixture"})
                worker.put("account", "current", {"id": 2, "generation": "new"})
                worker.expected = ("old", 1)
                worker.reply(entry={"viewer": 1, "progress": 7})
                self.assertNotIn("entry", worker.get("response", "left"))
            finally:
                worker.db.close()

    def test_disconnect_history_is_account_scoped(self):
        with tempfile.TemporaryDirectory() as directory:
            worker = Worker(Path(directory) / "db", "window", {})
            try:
                worker.db.execute(
                    "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
                )
                worker.put("player", "window", {"session": 1})
                for forget in (False, True):
                    worker.put("account", "current", {"id": 1, "generation": "test"})
                    for category in (
                        "watch",
                        "history",
                        "skipped",
                        "entry",
                        "cycle",
                        "pending",
                    ):
                        for viewer in (1, 10):
                            worker.put(category, f"{viewer}/2", {"saved": True})
                    with patch.object(worker.vault, "forget"):
                        worker.handle(
                            {
                                "op": "disconnect",
                                "account": "test",
                                "session": 1,
                                "forget_history": forget,
                            }
                        )
                    self.assertEqual(worker.get("account", "current"), {})
                    for category in (
                        "watch",
                        "history",
                        "skipped",
                        "entry",
                        "cycle",
                        "pending",
                    ):
                        self.assertEqual(
                            bool(worker.get(category, "1/2")),
                            not forget and category != "pending",
                        )
                        self.assertEqual(worker.get(category, "10/2"), {"saved": True})
            finally:
                worker.db.close()

    def test_disconnect_during_sample_does_not_restore_watch_data(self):
        with tempfile.TemporaryDirectory() as directory:
            worker = Worker(Path(directory) / "db", "window", {})
            try:
                worker.db.execute(
                    "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
                )
                worker.put("account", "current", {"id": 1, "generation": "test"})
                sample = dict(
                    session=1,
                    media=2,
                    episode="7",
                    watch_episode=True,
                    active=True,
                    speed=1,
                    seek=0,
                    tracking={"mode": "manual"},
                    wall=time.time() * 1000,
                )
                worker.put("player", "window", dict(sample, time=100, pos=100))
                worker.sample(credit_only=True)
                worker.put("player", "window", dict(sample, time=101, pos=101))
                transaction = worker.transaction

                @contextlib.contextmanager
                def disconnect_first():
                    worker.delete("account", "current")
                    with transaction():
                        yield

                with patch.object(worker, "transaction", disconnect_first):
                    worker.sample(credit_only=True)
                self.assertEqual(worker.get("watch", "1/2/7/0"), {})
            finally:
                worker.db.close()

    def test_two_processes(self):
        with tempfile.TemporaryDirectory() as directory:
            db = Path(directory) / "db"
            w = Worker(db, "parent", {})
            w.db.execute(
                "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
            )
            w.db.execute("CREATE TABLE writes(id INTEGER PRIMARY KEY)")
            w.put("account", "current", {"id": 1, "generation": "generation"})
            w.put(
                "fake",
                "entry",
                dict(
                    id=9,
                    userId=1,
                    mediaId=2,
                    progress=6,
                    status="CURRENT",
                    repeat=0,
                    updatedAt=0,
                ),
            )
            now = str(time.time())
            children = [
                subprocess.Popen(
                    [
                        sys.executable,
                        "-B",
                        str(Path(__file__).with_name("worker_processes.py")),
                        str(db),
                        window,
                        now,
                    ]
                )
                for window in ("a", "b")
            ]
            try:
                for child in children:
                    self.assertEqual(child.wait(timeout=12), 0)
                self.assertEqual(
                    w.db.execute("SELECT count(*) FROM writes").fetchone()[0], 1
                )
                self.assertAlmostEqual(w.get("watch", "1/2/7/0")["elapsed"], 1)
            finally:
                for child in children:
                    if child.poll() is None:
                        child.kill()
                        child.wait()
                w.db.close()

    def test_elapsed(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "db"
            workers = [Worker(path, k, {}) for k in ["a", "b"]]
            a, b = workers
            a.db.execute(
                "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
            )
            try:
                a.put("account", "current", dict(id=1, generation="test"))
                now = time.time()
                for t, pos, active, seek in [
                    (0, 0, True, 0),
                    (1, 2, True, 0),
                    (2, 1260, True, 1),
                    (3, 1262, False, 1),
                    (4, 1262, True, 1),
                    (5, 1264, True, 1),
                ]:
                    if t == 2:
                        a.put("history", "1/2/7/0", {"progress": 7})
                    for w in workers:
                        w.put(
                            "player",
                            w.window,
                            dict(
                                session=1,
                                media=2,
                                episode="7",
                                watch_episode=True,
                                active=active,
                                time=now + t,
                                wall=(now + t) * 1000,
                                pos=pos,
                                speed=2,
                                seek=seek,
                                tracking={"mode": "manual"},
                            ),
                        )
                        from unittest.mock import patch

                        with patch("backend.time.time", return_value=now + t):
                            w.sample()
                watch = a.get("watch", "1/2/7/0")
                self.assertAlmostEqual(watch["elapsed"], 2)
                self.assertAlmostEqual(watch["first_watched"], now * 1000)
                self.assertAlmostEqual(watch["last_watched"], (now + 5) * 1000)
            finally:
                for w in workers:
                    w.db.close()

    def test_watch_time_during_blocked_account_operation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "db"
            control = Worker(path, "window", {})
            control.db.execute(
                "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
            )
            control.put("account", "current", dict(id=1, generation="test"))
            sample = dict(
                session=1,
                media=2,
                episode="7",
                watch_episode=True,
                active=True,
                speed=2,
                seek=0,
                tracking={"mode": "manual"},
            )
            control.put(
                "player",
                "window",
                dict(sample, time=1000, pos=0, wall=time.time() * 1000),
            )
            stop = threading.Event()
            entered = threading.Event()

            def run():
                worker = Worker(path, "window", {})

                def blocked_request():
                    entered.set()
                    stop.wait(8)

                worker.operations = blocked_request
                worker.run()

            thread = threading.Thread(target=run)
            thread.start()
            try:
                self.assertTrue(entered.wait(2))
                time.sleep(0.3)
                for step in range(1, 4):
                    control.put(
                        "player",
                        "window",
                        dict(
                            sample,
                            time=1000 + step,
                            pos=step * 2,
                            wall=time.time() * 1000,
                        ),
                    )
                    deadline = time.monotonic() + 2
                    while (
                        time.monotonic() < deadline
                        and control.get("watch", "1/2/7/0").get("end", 0) < 1000 + step
                    ):
                        time.sleep(0.02)
                self.assertAlmostEqual(control.get("watch", "1/2/7/0")["elapsed"], 3)
            finally:
                stop.set()
                thread.join(3)
                control.db.close()


class TrackingTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.worker = Worker(Path(directory.name) / "db", "window", {})
        self.addCleanup(self.worker.db.close)
        self.worker.db.execute(
            "CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))"
        )
        self.account = dict(id=1, generation="fixture")
        self.worker.put("account", "current", self.account)
        self.player = dict(
            session=1,
            media=195604,
            episode="4",
            total=12,
            watch_episode=True,
            watch=True,
            allowed=True,
            active=True,
            tracking={"mode": "auto", "threshold": 15},
        )
        self.row = dict(
            id=9,
            userId=1,
            mediaId=195604,
            progress=3,
            status="CURRENT",
            repeat=0,
            updatedAt=1,
        )
        self.mutations = []
        self.before_final_read = None
        self.lose_response = False
        patches = (
            patch.object(self.worker, "credential", return_value="fixture"),
            patch("backend.read_entry", side_effect=self.read),
            patch("backend.save_progress", side_effect=self.save),
        )
        for mock in patches:
            mock.start()
            self.addCleanup(mock.stop)

    def transport(self, query, args, _token):
        if query.startswith("mutation"):
            self.mutations.append(args)
            if self.row["id"] is None:
                self.row["id"] = 10
            self.row = dict(
                self.row,
                **{
                    k: v
                    for k, v in args.items()
                    if k in ("progress", "status", "repeat")
                },
            )
            self.row["updatedAt"] += 1
            if self.lose_response:
                raise AccountError("timeout")
            return {"SaveMediaListEntry": self.row}
        return {"Page": {"mediaList": [self.row] if self.row["id"] else []}}

    def read(self, token, viewer, media):
        return read_entry(token, viewer, media, self.transport)

    def save(self, token, base, target, total, valid, **options):
        if self.before_final_read:
            self.before_final_read()
        return save_progress(
            token, base, target, total, valid, self.transport, **options
        )

    def publish(self):
        self.player["wall"] = time.time() * 1000
        self.worker.put("player", "window", self.player)

    def propose(self, elapsed=900):
        self.publish()
        key = self.worker.cycle(self.account, self.player)
        self.worker.put("watch", key, dict(elapsed=elapsed))
        self.worker.propose(self.account, self.player)
        return self.worker.get("pending", self.worker.cycle(self.account, self.player))

    def accept(self, proposal, keep_status=False):
        self.worker.handle(
            dict(
                op="accept",
                account=self.account["generation"],
                session=self.player["session"],
                media=self.player["media"],
                proposal=proposal["key"],
                keep_status=keep_status,
            )
        )

    def test_clear_watch_rearms_ask_for_same_episode_and_session(self):
        self.player["tracking"]["mode"] = "ask"
        first = self.propose()
        key = first["key"]
        self.worker.put("skipped", key, {"skip": True})
        self.worker.put("watch", "2/195604/4/0", {"elapsed": 900})
        self.worker.handle(
            dict(op="clear", category="watch", scope=key, session=1, account="fixture")
        )
        for category in ("watch", "pending", "skipped"):
            self.assertEqual(self.worker.get(category, key), {})
        self.assertEqual(self.worker.get("watch", "2/195604/4/0")["elapsed"], 900)
        self.assertFalse(self.propose(elapsed=899))
        second = self.propose()
        self.assertEqual(second["key"], first["key"])
        self.assertNotEqual(second["revision"], first["revision"])
        with self.assertRaisesRegex(AccountError, "expired"):
            self.worker.handle(
                dict(
                    op="skip",
                    account="fixture",
                    session=1,
                    proposal=key,
                    revision=first["revision"],
                )
            )
        self.assertEqual(self.worker.get("pending", key), second)

    def test_reset_restores_defaults_and_leaves_player_active(self):
        self.publish()
        self.worker.put("privacy", "global", {"mode": "hidden", "block_lookup": True})
        self.worker.put("sources", "global", {"source": "movie"})
        self.worker.handle(dict(op="reset", session=1, account="fixture"))
        for category, scope in (
            ("account", "current"),
            ("privacy", "global"),
            ("sources", "global"),
        ):
            self.assertEqual(self.worker.get(category, scope), {})
        self.assertEqual(self.worker.get("player", "window")["session"], 1)
        self.assertTrue(self.worker.get("generation", "cache"))
        self.assertTrue(self.worker.get("generation", "data"))

    def test_auto_uses_resolved_episode_and_rejects_saved_time_for_unclear_numbering(
        self,
    ):
        self.player["watch_episode"] = False
        self.assertFalse(self.propose())
        self.assertEqual(self.mutations, [])
        self.player["watch_episode"] = True
        self.assertFalse(self.propose(899))
        self.assertEqual(self.mutations, [])
        self.assertFalse(self.propose())
        self.assertEqual(self.mutations, [{"id": 9, "progress": 4}])
        self.assertFalse(self.propose())
        self.assertEqual(len(self.mutations), 1)

    def test_auto_rechecks_resolution_after_remote_read(self):
        def invalidate():
            self.player["watch_episode"] = False
            self.publish()

        self.before_final_read = invalidate
        with self.assertRaisesRegex(AccountError, "matched episode"):
            self.propose()
        self.assertEqual(self.mutations, [])

    def test_resume_off_requires_time_from_the_current_window_and_session(self):
        self.player["tracking"]["resume"] = False
        self.publish()
        key = self.worker.cycle(self.account, self.player)
        for owner, session in (("old-window", 1), ("window", 0)):
            self.worker.put(
                "watch", key, dict(elapsed=900, owner=owner, session=session)
            )
            self.worker.propose(self.account, self.player)
            self.assertEqual(self.mutations, [])
        self.worker.put("watch", key, dict(elapsed=900, owner="window", session=1))
        self.worker.propose(self.account, self.player)
        self.assertEqual(self.mutations, [{"id": 9, "progress": 4}])

    def test_auto_rejects_an_episode_offset_change_within_the_same_file(self):
        def change_episode():
            self.player["episode"] = "5"
            self.publish()

        self.before_final_read = change_episode
        with self.assertRaisesRegex(AccountError, "matched episode"):
            self.propose()
        self.assertEqual(self.mutations, [])

    def test_ask_mode_reviews_a_consecutive_episode(self):
        self.player["tracking"]["mode"] = "ask"
        proposal = self.propose()
        self.assertEqual(proposal["episode"], 4)
        self.assertIsNone(proposal["status"])
        self.assertTrue(proposal["needs_review"])
        self.assertEqual(self.mutations, [])
        self.player["active"] = False
        self.publish()
        self.accept(proposal)
        self.assertEqual(self.mutations, [{"id": 9, "progress": 4}])

    def test_other_statuses_offer_watching_and_allow_keeping_the_status(self):
        for status in ("PLANNING", "PAUSED", "DROPPED", "COMPLETED", None):
            with self.subTest(status=status):
                self.worker.db.execute(
                    "DELETE FROM records WHERE category NOT IN ('account','player')"
                )
                self.worker.tracking_probe = None
                self.mutations.clear()
                self.row.update(id=9 if status else None, progress=3, status=status)
                proposal = self.propose()
                self.assertEqual(proposal["status"], "CURRENT")
                self.assertEqual(self.mutations, [])
                keep = status == "DROPPED"
                self.accept(proposal, keep_status=keep)
                expected = {"id": 9 if status else 195604, "progress": 4}
                if not keep:
                    expected["status"] = "CURRENT"
                self.assertEqual(self.mutations, [expected])

    def test_progress_gap_and_remote_change_both_require_review(self):
        self.row["progress"] = 1
        proposal = self.propose()
        self.assertEqual(proposal["before"], 1)
        self.assertEqual(self.mutations, [])
        self.row.update(progress=2, updatedAt=2)
        with self.assertRaisesRegex(AccountError, "progress changed"):
            self.accept(proposal)
        self.assertEqual(self.mutations, [])
        self.assertFalse(self.worker.get("pending", proposal["key"]))

    def test_keeping_a_status_allows_later_auto_updates_until_status_changes(self):
        self.row["status"] = "PAUSED"
        self.accept(self.propose(), keep_status=True)
        self.player.update(session=2, episode="5")
        self.assertFalse(self.propose())
        self.assertEqual(
            self.mutations, [{"id": 9, "progress": 4}, {"id": 9, "progress": 5}]
        )
        self.assertEqual(self.row["status"], "PAUSED")
        self.row.update(status="DROPPED", updatedAt=8)
        self.player.update(session=3, episode="6")
        self.assertEqual(self.propose()["status"], "CURRENT")
        self.assertEqual(len(self.mutations), 2)

    def test_completed_entry_starts_rewatch_with_fresh_time_and_explicit_approval(self):
        self.row.update(status="COMPLETED", progress=12, repeat=2)
        self.player["episode"] = "1"
        self.player["tracking"]["rewatch"] = True
        self.worker.put("history", "1/195604/1/0", {"progress": 1})
        self.assertFalse(self.propose())
        self.assertEqual(self.worker.cycle(self.account, self.player), "1/195604/1/1")
        self.assertFalse(self.propose(899))
        proposal = self.propose()
        self.assertTrue(proposal["start_rewatch"])
        self.assertEqual(proposal["status"], "REPEATING")
        self.assertEqual(self.mutations, [])
        self.accept(proposal)
        self.assertEqual(
            self.mutations, [{"id": 9, "progress": 1, "status": "REPEATING"}]
        )
        self.assertEqual(self.row["repeat"], 2)
        self.assertEqual(self.worker.cycle(self.account, self.player), "1/195604/1/1")

    def test_remote_rewatch_advances_and_finishes_once_after_a_lost_response(self):
        self.row.update(status="REPEATING", progress=3, repeat=2)
        self.worker.put("history", "1/195604/4/0", {"progress": 4})
        self.assertFalse(self.propose())
        self.assertEqual(self.worker.cycle(self.account, self.player), "1/195604/4/1")
        self.assertFalse(self.propose())
        self.assertEqual(self.mutations, [{"id": 9, "progress": 4}])
        self.assertEqual(self.row["status"], "REPEATING")
        self.row.update(progress=11, updatedAt=5)
        self.player.update(session=2, episode="12")
        self.lose_response = True
        self.assertFalse(self.propose())
        self.assertEqual(
            self.mutations[-1],
            {"id": 9, "progress": 12, "status": "COMPLETED", "repeat": 3},
        )
        self.assertFalse(self.propose())
        self.assertEqual(len(self.mutations), 2)

    def test_paused_rewatch_resumes_without_completing_an_unknown_total(self):
        self.row.update(status="REPEATING", progress=3, repeat=2)
        self.player["total"] = 0
        self.assertFalse(self.propose())
        self.row.update(status="PAUSED", updatedAt=2)
        self.player["session"] = 2
        proposal = self.propose()
        self.assertEqual(proposal["status"], "REPEATING")
        self.assertIsNone(proposal["repeat_count"])
        self.accept(proposal)
        self.assertEqual(
            self.mutations, [{"id": 9, "progress": 4, "status": "REPEATING"}]
        )
        self.assertEqual(self.row["repeat"], 2)


if __name__ == "__main__":
    unittest.main()
