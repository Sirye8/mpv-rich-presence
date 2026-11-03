# SPDX-License-Identifier: AGPL-3.0-or-later
import base64
import hashlib
import json
import os
import secrets
import socket
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, HTTPServer


class AccountError(Exception):
    pass


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args):
        return None


def graphql(query, variables, token=None):
    headers = {"User-Agent":"mpv-rich-presence (https://github.com/Sirye8/mpv-rich-presence)", "Content-Type": "application/json", "Accept": "application/json"}
    if token:
        headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request("https://graphql.anilist.co", headers=headers,
                                 data=json.dumps({"query": query, "variables": variables}).encode())
    try:
        with urllib.request.build_opener(NoRedirect).open(req, timeout=8) as response:
            result = json.load(response)
    except urllib.error.HTTPError as exc:
        if exc.code == 401:
            raise AccountError("Session expired. Reconnect to AniList.") from None
        if exc.code == 429:
            raise AccountError("AniList rate limit reached. Try again later.") from None
        raise AccountError("AniList request failed (HTTP %d)." % exc.code) from None
    except (ValueError, OSError):
        raise AccountError("AniList is offline or timed out.") from None
    if result.get("errors") or not isinstance(result.get("data"), dict):
        raise AccountError("AniList rejected the request.")
    return result["data"]


def read_entry(token, viewer, media, call=graphql):
    # Page returns an empty list for a missing entry. Query errors stay errors.
    data = call("""query($viewer:Int!,$media:Int!){Page(perPage:1){
        mediaList(userId:$viewer,mediaId:$media){id userId mediaId progress status repeat updatedAt}}}""",
                {"viewer": viewer, "media": media}, token)
    entries = data["Page"]["mediaList"]
    if not isinstance(entries, list):
        raise AccountError("AniList returned an incomplete list.")
    if not entries:
        return dict(id=None, viewer=viewer, media=media, progress=0, status=None, repeat=0, updatedAt=None)
    entry = dict(entries[0])
    if entry.pop("userId") != viewer or entry.pop("mediaId") != media:
        raise AccountError("The entry belongs to a different viewer or media.")
    return dict(entry, viewer=viewer, media=media)


def save_progress(token, base, target, total, valid, call=graphql):
    if type(target) is not int or target < 0 or (total and target > total):
        raise AccountError("Progress is out of range.")
    fresh = read_entry(token, base["viewer"], base["media"], call)
    if any(fresh[k] != base.get(k) for k in fresh):
        raise AccountError("AniList progress changed. Refresh and review the edit.")
    valid()
    if base["id"] and fresh["progress"] == target:
        return fresh
    field, value = ("id", base["id"]) if base["id"] else ("mediaId", base["media"])
    query = ("mutation($id:Int!,$progress:Int!){SaveMediaListEntry(" + field +
             ":$id,progress:$progress){id userId mediaId progress status repeat updatedAt}}")
    try:
        row = dict(call(query, {"id": value, "progress": target}, token)["SaveMediaListEntry"])
    except AccountError:
        # Never repeat a mutation whose acknowledgement was lost.
        row = read_entry(token, base["viewer"], base["media"], call)
        if row["progress"] != target:
            raise AccountError("Update unconfirmed. Refresh before trying again.") from None
        return row
    if row.pop("userId") != base["viewer"] or row.pop("mediaId") != base["media"]:
        raise AccountError("Unexpected mutation result. Refresh the entry.")
    return dict(row, viewer=base["viewer"], media=base["media"])


def callback_server(state, port=47653, code_flow=False, exchange=None):
    class Callback(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def send_body(self, code, body):
            self.send_response(code)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Referrer-Policy", "no-referrer")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Security-Policy", "default-src 'none'; script-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; frame-ancestors 'none'")
            self.end_headers()
            self.wfile.write(body.encode())

        def do_GET(self):
            request = urllib.parse.urlsplit(self.path)
            if self.headers.get("Host") != "127.0.0.1:%d" % self.server.server_port:
                return self.send_error(403)
            if request.path != "/callback":
                return self.send_error(404)
            if code_flow:
                params = urllib.parse.parse_qs(request.query)
                if not secrets.compare_digest(params.get("state", [""])[0], state) or not params.get("code"):
                    return self.send_error(400)
                code = params["code"][0]
                if exchange:
                    self.server.code = code
                    try:
                        self.server.result = exchange(code, None, False)
                    except ClientSecretRequired:
                        self.send_body(200, f"""<title>mpv Rich Presence</title>
                        <script>history.replaceState(null,"","/callback")</script>
                        <h2>Connect a personal AniList application</h2>
                        <p>AniList requires the secret for the application you registered. This form runs on your computer.
                        Only the owner of that application should enter its secret.</p>
                        <form action="/finish" method="post"><input type="hidden" name="state" value="{state}">
                        <label>Personal client secret <input type="password" name="secret" required autocomplete="off"></label>
                        <p><label><input type="checkbox" name="remember"> Save in my OS credential store</label></p>
                        <button>Finish sign-in</button></form>""")
                        return
                    except (AccountError, OSError) as exc:
                        self.server.error = "Sign-in failed during the code exchange."
                        self.send_body(502, self.server.error)
                        return
                else:
                    self.server.result = code
                self.send_body(200, "<title>mpv Rich Presence</title>Return to mpv to see the sign-in result.")
                return
            self.send_body(200, """<title>mpv Rich Presence</title><p id="message">Finishing sign-in...</p>
<script>const fragment=location.hash.slice(1);history.replaceState(null,"","/callback");
fetch("/callback",{method:"POST",body:fragment}).then(r=>document.querySelector("p").textContent=
r.ok?"Return to mpv. You can close this tab.":"Sign-in failed. Try connecting again in mpv.");</script>""")

        def do_POST(self):
            origin = "http://127.0.0.1:%d" % self.server.server_port
            path = "/finish" if code_flow else "/callback"
            # Browsers may send a null Origin for a same-site form under a
            # no-referrer policy. The form still carries the nonce from the
            # state-verified callback. Reject foreign hosts and origins.
            allowed_origins = (origin, "null", None) if code_flow else (origin,)
            if (self.path != path or self.headers.get("Host") != origin.removeprefix("http://")
                    or self.headers.get("Origin") not in allowed_origins):
                return self.send_error(403)
            try:
                size = int(self.headers.get("Content-Length", "0"))
                if not 0 < size <= 16384:
                    raise ValueError()
                result = urllib.parse.parse_qs(self.rfile.read(size).decode("ascii"))
                if not secrets.compare_digest(result["state"][0], state):
                    raise ValueError()
                if code_flow:
                    if not exchange or not getattr(self.server, "code", None):
                        return self.send_error(400)
                    try:
                        self.server.result = exchange(self.server.code, result["secret"][0], "remember" in result)
                    except (AccountError, OSError):
                        self.server.error = "AniList declined the code exchange. Reconnect with your current credentials."
                        self.send_body(400, self.server.error)
                        return
                    self.send_body(200, "Connected. Return to mpv.")
                    return
                token = result["access_token"][0]
                if not token.isascii() or any(c.isspace() for c in token):
                    raise ValueError()
                self.server.result = token
            except (KeyError, ValueError):
                return self.send_error(400)
            self.send_body(200, "Connected")

    class LoopbackServer(HTTPServer):
        allow_reuse_address = False

        def server_bind(self):
            if os.name == "nt":
                self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            super().server_bind()

    server = LoopbackServer(("127.0.0.1", port), Callback)
    server.timeout = 1
    server.error = None
    server.result = None
    return server


class ClientSecretRequired(AccountError):
    pass


def exchange_code(client_id, code, verifier, secret=None):
    fields = dict(grant_type="authorization_code",client_id=client_id,
                  redirect_uri="http://127.0.0.1:47653/callback",code=code,code_verifier=verifier)
    if secret:
        fields["client_secret"] = secret
    request = urllib.request.Request("https://anilist.co/api/v2/oauth/token",
        data=urllib.parse.urlencode(fields).encode(), headers={"Accept":"application/json","User-Agent":"mpv-rich-presence (https://github.com/Sirye8/mpv-rich-presence)"})
    try:
        with urllib.request.build_opener(NoRedirect).open(request,timeout=10) as response:
            return json.load(response)["access_token"]
    except urllib.error.HTTPError as exc:
        if exc.code == 401: raise ClientSecretRequired("AniList requires client authentication.") from None
        raise AccountError("AniList declined the authorization code.") from None


def authorize(client_id, valid, flow="pkce", vault=None):
    if not str(client_id).isdigit():
        raise AccountError("Set anilist_client_id in rich-presence.conf first.")
    state = secrets.token_urlsafe(32)
    verifier = secrets.token_urlsafe(48)
    code_flow = flow == "pkce"
    redirect = "http://127.0.0.1:47653/callback"
    def exchange(code, secret, remember):
        valid()
        key = "client-" + str(client_id)
        credential = secret or (vault.read(key) if vault else None)
        if not credential:
            raise ClientSecretRequired("Personal application secret required.")
        token = exchange_code(client_id, code, verifier, credential)
        if secret and remember and vault:
            vault.write(key, secret)
        return token
    with callback_server(state, code_flow=code_flow, exchange=exchange) as server:
        params = dict(client_id=client_id, response_type="code" if code_flow else "token",
                      state=state, redirect_uri=redirect)
        if code_flow:
            params["code_challenge_method"] = "S256"
            params["code_challenge"] = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
        query = urllib.parse.urlencode(params)
        if not webbrowser.open("https://anilist.co/api/v2/oauth/authorize?" + query):
            raise AccountError("Could not open your browser.")
        limit = time.monotonic() + 180
        while time.monotonic() < limit and not server.result and not server.error:
            valid()
            server.handle_request()
        if server.error: raise AccountError(server.error)
        if not server.result:
            raise AccountError("Sign-in timed out.")
        return server.result


class Vault:
    """Keep a session copy when no OS credential service is available."""
    def __init__(self):
        self.session = {}
        self.keyring = None
        try:
            if os.name == "nt":
                from windows_vault import WindowsVault
                self.keyring = WindowsVault()
            else:
                from keyring.backends.SecretService import Keyring
                self.keyring = Keyring()
        except (ImportError, RuntimeError):
            pass

    def write(self, key, value):
        self.session[key] = value
        if self.keyring:
            try:
                self.keyring.set_password("mpv-rich-presence", key, value)
                return True
            except Exception:
                pass
        return False

    def read(self, key):
        if key in self.session:
            return self.session[key]
        if self.keyring:
            try:
                return self.keyring.get_password("mpv-rich-presence", key)
            except Exception:
                pass
        return None

    def forget(self, key, persistent):
        if persistent:
            if not self.keyring:
                raise AccountError("Unlock the OS credential store to disconnect.")
            if self.keyring.get_password("mpv-rich-presence", key) is not None:
                self.keyring.delete_password("mpv-rich-presence", key)
        self.session.pop(key, None)

