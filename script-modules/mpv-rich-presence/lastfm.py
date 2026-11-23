# SPDX-License-Identifier: AGPL-3.0-or-later
"""Last.fm desktop authentication, scrobbling and track information.

Every write is signed with the application's shared secret, as Last.fm requires.
Recognition stays with MusicBrainz; this module only reports what was played.
"""

import hashlib
import json
import urllib.error
import urllib.parse
import urllib.request

API = "https://ws.audioscrobbler.com/2.0/"
API_KEY = "cded74edcc6264b63412b452550c7258"
SHARED_SECRET = "c1429730e04964e0c034f54ce2291706"
USER_AGENT = "mpv-rich-presence (https://github.com/Sirye8/mpv-rich-presence)"
# Error codes that will not succeed on retry. Everything else, including
# 11 (service offline), 16 (temporary error) and 29 (rate limit), is retried later.
INVALID_SESSION = {4, 9, 14, 15, 26}
REJECTED = {6, 7, 13}


class LastfmError(Exception):
    def __init__(self, message, code=0):
        super().__init__(message)
        self.code = code

    @property
    def retry(self):
        return self.code not in INVALID_SESSION | REJECTED


def signature(params, secret=SHARED_SECRET):
    """Last.fm's api_sig: md5 of the sorted name-value pairs plus the shared secret."""
    text = "".join(k + str(params[k]) for k in sorted(params) if k not in ("format", "callback"))
    return hashlib.md5((text + secret).encode("utf-8")).hexdigest()


def call(method, params=None, *, session=None, post=False, signed=False, opener=None, timeout=10):
    params = {k: str(v) for k, v in (params or {}).items() if v not in (None, "")}
    params.update(method=method, api_key=API_KEY)
    if session:
        params["sk"] = session
    if signed or session:
        params["api_sig"] = signature(params)
    params["format"] = "json"
    body = urllib.parse.urlencode(params).encode() if post else None
    url = API if post else API + "?" + urllib.parse.urlencode(params)
    request = urllib.request.Request(url, data=body, headers={"User-Agent": USER_AGENT})
    try:
        with (opener or urllib.request.urlopen)(request, timeout=timeout) as response:
            data = json.loads(response.read(1 << 20).decode("utf-8"))
    except urllib.error.HTTPError as exc:
        try:
            data = json.loads(exc.read(1 << 16).decode("utf-8"))
        except (ValueError, OSError):
            raise LastfmError(f"Last.fm returned HTTP {exc.code}.", 16) from None
    except (OSError, ValueError):
        raise LastfmError("Last.fm is unavailable. Check the connection.", 16) from None
    if isinstance(data, dict) and "error" in data:
        raise LastfmError(str(data.get("message") or "Last.fm rejected the request."), int(data["error"]))
    if not isinstance(data, dict):
        raise LastfmError("Last.fm returned an unexpected response.", 16)
    return data


def request_token(**kw):
    return call("auth.getToken", signed=True, **kw)["token"]


def approval_url(token):
    return "https://www.last.fm/api/auth/?" + urllib.parse.urlencode({"api_key": API_KEY, "token": token})


def session_for(token, **kw):
    """The permanent session key once the viewer approved the token. Raises code 14 until then."""
    data = call("auth.getSession", {"token": token}, signed=True, **kw)["session"]
    return {"name": data["name"], "key": data["key"]}


def profile(name, **kw):
    user = call("user.getInfo", {"user": name}, **kw)["user"]
    images = {i.get("size"): i.get("#text") for i in user.get("image", []) if isinstance(i, dict)}
    avatar = images.get("large") or images.get("medium") or ""
    return {"name": user.get("name", name), "url": user.get("url") or f"https://www.last.fm/user/{name}",
            "avatar": avatar if avatar.startswith("https://") else ""}


def track_fields(track, index=None):
    """Request fields for one track, numbered for a scrobble batch."""
    suffix = "" if index is None else f"[{index}]"
    fields = {"artist": track["artist"], "track": track["track"], "album": track.get("album"),
              "duration": int(track["duration"]) if track.get("duration") else None, "mbid": track.get("mbid")}
    if index is not None:
        fields["timestamp"] = int(track["timestamp"])
    return {k + suffix: v for k, v in fields.items() if v not in (None, "")}


def now_playing(session, track, **kw):
    call("track.updateNowPlaying", track_fields(track), session=session, post=True, **kw)


def scrobble(session, tracks, **kw):
    """Send up to 50 scrobbles. Returns the number Last.fm accepted and ignored."""
    params = {}
    for i, track in enumerate(tracks[:50]):
        params.update(track_fields(track, i))
    data = call("track.scrobble", params, session=session, post=True, **kw).get("scrobbles", {})
    attr = data.get("@attr", {})
    return int(attr.get("accepted", 0)), int(attr.get("ignored", 0))


def love(session, track, loved=True, **kw):
    call("track.love" if loved else "track.unlove", {"artist": track["artist"], "track": track["track"]},
         session=session, post=True, **kw)


def track_info(name, track, **kw):
    """The viewer's play count and loved state for a track."""
    info = call("track.getInfo", {"artist": track["artist"], "track": track["track"], "username": name,
                                  "autocorrect": 1}, **kw).get("track", {})
    return {"plays": str(int(info.get("userplaycount") or 0)), "loved": str(info.get("userloved", "0")) == "1"}


def track_page(track):
    quote = lambda text: urllib.parse.quote_plus(text, safe="")
    return f"https://www.last.fm/music/{quote(track['artist'])}/_/{quote(track['track'])}"


def scrobble_point(duration, percent=None):
    """Seconds of real listening before a track counts, or None when it is too short to scrobble."""
    if not duration or duration <= 30:
        return None
    if percent:
        return duration * max(10, min(100, percent)) / 100
    return min(duration / 2, 240)
