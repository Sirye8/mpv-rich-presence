# SPDX-License-Identifier: AGPL-3.0-or-later
"""Single-process AniList OAuth exchange. Bind behind an HTTPS reverse proxy."""

import base64
import hashlib
import hmac
import json
import os
import re
import secrets
import threading
import time
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Sessions:
    def __init__(self):
        self.items = {}
        self.lock = threading.Lock()

    def create(self, data, lifetime):
        with self.lock:
            now = time.monotonic()
            self.items = {k: v for k, v in self.items.items() if v[0] > now}
            if len(self.items) >= 512:
                raise ValueError("Busy")
            key = secrets.token_urlsafe(32)
            self.items[key] = (now + lifetime, data)
            return key

    def take(self, key, kind):
        with self.lock:
            entry = self.items.pop(key, None)
            if not entry or entry[0] <= time.monotonic() or entry[1]["kind"] != kind:
                raise ValueError("Expired request")
            return entry[1]


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args):
        return None


def handler(public, client_id, secret):
    sessions = Sessions()
    callback = public + "/callback"

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def reply(self, status, payload):
            raw = json.dumps(payload).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Referrer-Policy", "no-referrer")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

        def do_POST(self):
            try:
                size = int(self.headers.get("Content-Length", 0))
                if not 0 < size <= 2048:
                    raise ValueError()
                body = json.loads(self.rfile.read(size))
                if self.path == "/start":
                    challenge = body["challenge"]
                    state = body["state"]
                    if not re.fullmatch(
                        r"[A-Za-z0-9_-]{43}", challenge
                    ) or not re.fullmatch(r"[A-Za-z0-9_-]{32,96}", state):
                        raise ValueError()
                    key = sessions.create(
                        dict(kind="start", challenge=challenge, state=state), 180
                    )
                    query = urllib.parse.urlencode(
                        dict(
                            response_type="code",
                            client_id=client_id,
                            redirect_uri=callback,
                            state=key,
                        )
                    )
                    self.reply(
                        200,
                        {
                            "authorize": "https://anilist.co/api/v2/oauth/authorize?"
                            + query
                        },
                    )
                elif self.path == "/redeem":
                    data = sessions.take(body["ticket"], "ticket")
                    challenge = (
                        base64.urlsafe_b64encode(
                            hashlib.sha256(body["verifier"].encode()).digest()
                        )
                        .rstrip(b"=")
                        .decode()
                    )
                    if not hmac.compare_digest(challenge, data["challenge"]):
                        raise ValueError()
                    self.reply(200, {"access_token": data["token"]})
                else:
                    self.reply(404, {})
            except (ValueError, KeyError, TypeError):
                self.reply(400, {"error": "Request expired or invalid."})

        def do_GET(self):
            try:
                path = urllib.parse.urlsplit(self.path)
                if path.path != "/callback":
                    return self.reply(404, {})
                query = urllib.parse.parse_qs(path.query)
                data = sessions.take(query["state"][0], "start")
                fields = dict(
                    grant_type="authorization_code",
                    client_id=client_id,
                    client_secret=secret,
                    redirect_uri=callback,
                    code=query["code"][0],
                )
                req = urllib.request.Request(
                    "https://anilist.co/api/v2/oauth/token",
                    data=urllib.parse.urlencode(fields).encode(),
                    headers={
                        "Accept": "application/json",
                        "User-Agent": "mpv-rich-presence",
                    },
                )
                with urllib.request.build_opener(NoRedirect).open(req, timeout=10) as r:
                    token = json.load(r)["access_token"]
                ticket = sessions.create(
                    dict(kind="ticket", challenge=data["challenge"], token=token), 60
                )
                local = urllib.parse.urlencode(dict(state=data["state"], code=ticket))
                self.send_response(303)
                self.send_header("Location", "http://127.0.0.1:47653/callback?" + local)
                self.send_header("Referrer-Policy", "no-referrer")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
            except (ValueError, KeyError, OSError):
                self.reply(
                    400,
                    {"error": "Authorization expired or declined. Reconnect from mpv."},
                )

    return Handler


if __name__ == "__main__":
    origin = os.environ["ANILIST_PUBLIC_ORIGIN"].rstrip("/")
    if not re.fullmatch(r"https://[a-zA-Z0-9.-]+", origin):
        raise SystemExit("ANILIST_PUBLIC_ORIGIN must be an HTTPS origin.")
    app = handler(
        origin, os.environ["ANILIST_CLIENT_ID"], os.environ["ANILIST_CLIENT_SECRET"]
    )
    ThreadingHTTPServer(
        ("127.0.0.1", int(os.environ.get("PORT", "8787"))), app
    ).serve_forever()
