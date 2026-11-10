# SPDX-License-Identifier: AGPL-3.0-or-later
import json
from pathlib import Path
import subprocess
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
from account import AccountError, ClientSecretRequired, callback_server, save_progress
from backend import Worker


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


class AccountTests(unittest.TestCase):
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
                    safe_episode=True,
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
                    for w in workers:
                        w.put(
                            "player",
                            w.window,
                            dict(
                                session=1,
                                media=2,
                                episode="7",
                                safe_episode=True,
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
                self.assertAlmostEqual(a.get("watch", "1/2/7/0")["elapsed"], 2)
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
                safe_episode=True,
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


if __name__ == "__main__":
    unittest.main()
