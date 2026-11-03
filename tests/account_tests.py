# SPDX-License-Identifier: AGPL-3.0-or-later
import json
from pathlib import Path
import subprocess
import hashlib
import base64
from http.client import HTTPConnection
from http.server import ThreadingHTTPServer
from unittest.mock import patch
import io
import sys
import tempfile
import threading
import time
import unittest
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
                    req = urllib.request.Request(
                        origin + "/finish",
                        data=urllib.parse.urlencode(
                            dict(state="test-state", secret=supplied)
                        ).encode(),
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
    def test_one_time_handoff(self):
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
        from anilist_exchange import handler

        verifier = "v" * 64
        challenge = (
            base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest())
            .rstrip(b"=")
            .decode()
        )
        state = "s" * 32
        with ThreadingHTTPServer(
            ("127.0.0.1", 0), handler("https://example.org", "123", "fixture")
        ) as server:
            thread = threading.Thread(target=server.serve_forever)
            thread.start()
            client = HTTPConnection("127.0.0.1", server.server_port, timeout=5)

            def post(path, body):
                client.request(
                    "POST", path, json.dumps(body), {"Content-Type": "application/json"}
                )
                result = client.getresponse()
                return result.status, json.loads(result.read())

            try:
                code, started = post("/start", dict(state=state, challenge=challenge))
                self.assertEqual(code, 200)
                params = urllib.parse.parse_qs(
                    urllib.parse.urlsplit(started["authorize"]).query
                )
                with patch("anilist_exchange.urllib.request.build_opener") as opener:
                    opener.return_value.open.return_value = io.BytesIO(
                        b'{"access_token":"fixture-access"}'
                    )
                    client.request(
                        "GET",
                        "/callback?"
                        + urllib.parse.urlencode(
                            dict(code="code", state=params["state"][0])
                        ),
                    )
                    response = client.getresponse()
                    dest = response.getheader("Location")
                    response.read()
                self.assertTrue(dest.startswith("http://127.0.0.1:47653/callback?"))
                self.assertNotIn("fixture-access", dest)
                data = urllib.parse.parse_qs(urllib.parse.urlsplit(dest).query)
                self.assertEqual(data["state"], [state])
                req = dict(ticket=data["code"][0], verifier=verifier)
                self.assertEqual(
                    post("/redeem", req), (200, {"access_token": "fixture-access"})
                )
                self.assertEqual(post("/redeem", req)[0], 400)
            finally:
                client.close()
                server.shutdown()
                thread.join()


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


if __name__ == "__main__":
    unittest.main()
