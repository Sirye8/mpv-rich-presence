# SPDX-License-Identifier: AGPL-3.0-or-later
"""Last.fm client and scrobble queue checks with a mocked Last.fm."""

import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import urllib.error
import urllib.parse

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "script-modules/mpv-rich-presence"))
import lastfm  # noqa: E402
from backend import Worker  # noqa: E402


class Response(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False


class ClientTests(unittest.TestCase):
    def test_signature_sorts_parameters_and_skips_format(self):
        params = {"method": "auth.getSession", "token": "t", "api_key": "k", "format": "json"}
        expected = __import__("hashlib").md5("api_keykmethodauth.getSessiontokentSECRET".encode()).hexdigest()
        self.assertEqual(lastfm.signature(params, "SECRET"), expected)

    def test_signed_post_and_error_codes(self):
        sent = []

        def opener(request, timeout):
            sent.append(request)
            return Response(json.dumps({"error": 9, "message": "Invalid session key"}).encode())

        with self.assertRaises(lastfm.LastfmError) as caught:
            lastfm.now_playing("sk", dict(artist="A", track="T", album="", duration=200), opener=opener)
        self.assertEqual(caught.exception.code, 9)
        self.assertFalse(caught.exception.retry)
        body = urllib.parse.parse_qs(sent[0].data.decode())
        self.assertEqual(body["method"], ["track.updateNowPlaying"])
        self.assertEqual(body["sk"], ["sk"])
        self.assertNotIn("album", body)
        self.assertEqual(len(body["api_sig"][0]), 32)
        self.assertTrue(lastfm.LastfmError("x", 16).retry and lastfm.LastfmError("x", 29).retry)

    def test_http_failures_are_retried(self):
        def opener(request, timeout):
            raise urllib.error.URLError("offline")

        with self.assertRaises(lastfm.LastfmError) as caught:
            lastfm.call("user.getInfo", {"user": "x"}, opener=opener)
        self.assertTrue(caught.exception.retry)

    def test_scrobble_batches_number_each_track(self):
        tracks = [dict(artist="A", track=f"T{i}", timestamp=1000 + i, duration=180) for i in range(60)]
        captured = {}

        def fake(method, params, **kw):
            captured.update(params)
            return {"scrobbles": {"@attr": {"accepted": "50", "ignored": "0"}}}

        with patch("lastfm.call", side_effect=fake):
            self.assertEqual(lastfm.scrobble("sk", tracks), (50, 0))
        self.assertEqual(captured["track[49]"], "T49")
        self.assertNotIn("track[50]", captured)
        self.assertEqual(captured["timestamp[0]"], 1000)

    def test_scrobble_point_follows_lastfm_rules(self):
        self.assertIsNone(lastfm.scrobble_point(30))
        self.assertIsNone(lastfm.scrobble_point(0))
        self.assertEqual(lastfm.scrobble_point(200), 100)
        self.assertEqual(lastfm.scrobble_point(1200), 240)
        self.assertEqual(lastfm.scrobble_point(1200, 90), 1080)
        self.assertEqual(lastfm.scrobble_point(200, 5), 20)

    def test_track_page_and_info(self):
        self.assertEqual(lastfm.track_page(dict(artist="AC/DC", track="T.N.T.")),
                         "https://www.last.fm/music/AC%2FDC/_/T.N.T.")
        with patch("lastfm.call", return_value={"track": {"userplaycount": "12", "userloved": "1"}}):
            self.assertEqual(lastfm.track_info("me", dict(artist="A", track="T")), {"plays": "12", "loved": True})


class ScrobbleQueueTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.worker = Worker(str(Path(directory.name) / "db"), "window", {})
        self.addCleanup(self.worker.db.close)
        self.worker.db.execute("CREATE TABLE records(category TEXT,scope TEXT,value TEXT,PRIMARY KEY(category,scope))")
        self.worker.vault.write("lastfm-gen", "session-key", remember=False)
        self.worker.put("lastfm-account", "current", dict(name="listener", generation="gen", persistent=False))
        self.base = dict(session=5, time=100.0, wall=1_700_000_000_000, active=True, scrobble=True,
                         scrobbling={"enabled": True}, duration=200.0, artist="Evanescence",
                         title="Bring Me to Life", track_title="Bring Me to Life", album="Fallen")

    def play(self, seconds, **changes):
        for _ in range(seconds):
            self.base = dict(self.base, time=self.base["time"] + 1, wall=self.base["wall"] + 1000, **changes)
            self.worker.put("player", "window", self.base)
            self.worker.listen()

    def test_real_listening_queues_one_scrobble_at_the_point(self):
        self.play(1)
        self.play(98)
        self.assertFalse(self.worker.get("listen", "window")["scrobbled"])
        self.play(2)
        self.play(30)
        queued = self.worker.db.execute("SELECT value FROM records WHERE category='lastfm-queue'").fetchall()
        self.assertEqual(len(queued), 1)
        entry = json.loads(queued[0][0])
        self.assertEqual((entry["artist"], entry["track"], entry["timestamp"]), ("Evanescence", "Bring Me to Life", 1_700_000_001))

    def test_pauses_and_disabled_scrobbling_add_no_time(self):
        self.play(1)
        self.play(50, active=False)
        self.play(50, active=True, scrobble=False)
        self.assertLess(self.worker.get("listen", "window").get("credited", 0), 2)

    def test_recognized_names_replace_tags_without_losing_time(self):
        self.play(1)
        self.play(20)
        self.worker.put("auto", "window", dict(session=5, hit=dict(kind="recording", provider="musicbrainz",
            name="Bring Me to Life", artist="Evanescence feat. Paul McCoy",
            album="Fallen", id="00000000-0000-0000-0000-000000000001")))
        self.play(10)
        state = self.worker.get("listen", "window")
        self.assertGreaterEqual(state["credited"], 29)
        self.assertEqual(state["track"]["artist"], "Evanescence feat. Paul McCoy")
        self.assertEqual(state["track"]["mbid"], "00000000-0000-0000-0000-000000000001")
        self.assertFalse(state["now_playing"])

    def test_flush_sends_once_and_keeps_rows_on_retryable_errors(self):
        self.play(1)
        self.play(110)
        account = self.worker.get("lastfm-account", "current")
        with patch("lastfm.scrobble", side_effect=lastfm.LastfmError("offline", 16)):
            with self.assertRaises(lastfm.LastfmError):
                self.worker.lastfm_flush(account)
        self.assertEqual(self.count(), 1)
        with patch("lastfm.scrobble", return_value=(1, 0)) as send:
            self.worker.lastfm_flush(account)
            self.worker.lastfm_flush(account)
        self.assertEqual(send.call_count, 1)
        self.assertEqual(self.count(), 0)
        self.assertEqual(self.worker.get("lastfm-history", "global")["tracks"][0]["status"], "sent")

    def test_invalid_session_marks_the_account_and_keeps_the_queue(self):
        self.play(1)
        self.play(110)
        account = self.worker.get("lastfm-account", "current")
        self.worker.put("player", "window", dict(self.base, active=False))
        with patch("lastfm.scrobble", side_effect=lastfm.LastfmError("Invalid session key", 9)):
            self.worker.lastfm_tick(self.worker.get("player", "window"))
        self.assertTrue(self.worker.get("lastfm-account", "current")["expired"])
        self.assertEqual(self.count(), 1)

    def test_now_playing_and_track_info_are_sent_once(self):
        self.base["lastfm_info"] = True
        self.play(2)
        with patch("lastfm.now_playing") as playing, patch("lastfm.track_info", return_value={"plays": "3", "loved": False}) as info, \
                patch("lastfm.scrobble", return_value=(0, 0)):
            self.worker.lastfm_tick(self.base)
            self.worker.lastfm_tick(self.base)
        self.assertEqual(playing.call_count, 1)
        self.assertEqual(info.call_count, 1)
        self.assertEqual(self.worker.get("lastfm-track", "window"), {"plays": "3", "loved": False, "session": 5})

    def test_sign_in_stores_the_session_and_disconnect_forgets_it(self):
        self.worker.delete("lastfm-account", "current")
        approvals = iter([lastfm.LastfmError("Unauthorized token", 14), {"name": "listener", "key": "new-key"}])

        def approve(token):
            result = next(approvals)
            if isinstance(result, Exception):
                raise result
            return result

        with patch("lastfm.request_token", return_value="tok"), patch("lastfm.session_for", side_effect=approve), \
                patch("lastfm.profile", return_value={"name": "listener", "url": "https://www.last.fm/user/listener", "avatar": ""}), \
                patch("webbrowser.open", return_value=True), patch("time.sleep"):
            self.worker.lastfm_connect({"session_only": True})
        account = self.worker.get("lastfm-account", "current")
        self.assertEqual(account["name"], "listener")
        self.assertEqual(self.worker.vault.read("lastfm-" + account["generation"]), "new-key")
        self.worker.handle(dict(op="lastfm-disconnect", forget_queue=True, account=None, session=0))
        self.assertEqual(self.worker.get("lastfm-account", "current"), {})
        self.assertIsNone(self.worker.vault.session.get("lastfm-" + account["generation"]))

    def test_saved_data_clears_queue_history_and_profiles(self):
        self.worker.put("lastfm-queue", "gen/1/window", dict(artist="A", track="T", timestamp=1))
        self.worker.put("lastfm-queue", "gen/2/window", dict(artist="B", track="U", timestamp=2))
        self.worker.put("lastfm-history", "global", dict(tracks=[dict(artist="A")]))
        self.worker.put("scrobbling", "folder:/music", dict(enabled=True))
        clear = dict(op="clear", account=None, session=0)
        self.worker.handle(dict(clear, category="lastfm-queue", scope="gen/1/window"))
        self.assertEqual(self.count(), 1)
        self.worker.handle(dict(clear, category="lastfm-queue"))
        self.worker.handle(dict(clear, category="lastfm-history", scope="global"))
        self.worker.handle(dict(clear, category="scrobbling", scope="folder:/music"))
        self.assertEqual(self.count(), 0)
        self.assertEqual(self.worker.get("lastfm-history", "global"), {})
        self.assertEqual(self.worker.get("scrobbling", "folder:/music"), {})

    def count(self):
        return self.worker.db.execute("SELECT COUNT(*) FROM records WHERE category='lastfm-queue'").fetchone()[0]


if __name__ == "__main__":
    unittest.main()
