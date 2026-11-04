# SPDX-License-Identifier: AGPL-3.0-or-later
"""Optional off-thread account and metadata worker, one per interested player."""

import contextlib
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import sqlite3
import sys
import time

# mpv starts Python in isolated mode. Load siblings only from this installation.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from account import (
    AccountError,
    SessionExpired,
    Vault,
    authorize,
    graphql,
    read_entry,
    save_progress,
)
from providers import candidates, details, fingerprint


@contextlib.contextmanager
def serialized(directory):
    path = directory / "anilist-write.lock"
    with open(path, "a+b") as f:
        if os.name == "nt":
            import msvcrt

            if f.tell() == 0:
                f.write(b"0")
                f.flush()
            f.seek(0)
            msvcrt.locking(f.fileno(), msvcrt.LK_LOCK, 1)
        else:
            import fcntl

            fcntl.flock(f, fcntl.LOCK_EX)
        yield


class Worker:
    def __init__(self, db_path, window, settings):
        self.db = sqlite3.connect(db_path, isolation_level=None, timeout=4)
        self.directory = Path(db_path).parent
        self.window = window
        self.settings = settings
        self.vault = Vault()
        self.previous = None
        self.resolved = None
        self.expected = None
        self.data_generation = None

    def get(self, category, scope, default=None):
        row = self.db.execute(
            "SELECT value FROM records WHERE category=? AND scope=?", (category, scope)
        ).fetchone()
        return json.loads(row[0]) if row else ({} if default is None else default)

    def put(self, category, scope, value):
        self.db.execute(
            "INSERT OR REPLACE INTO records VALUES(?,?,?)",
            (category, scope, json.dumps(value)),
        )

    def delete(self, category, scope):
        self.db.execute(
            "DELETE FROM records WHERE category=? AND scope=?", (category, scope)
        )

    @contextlib.contextmanager
    def transaction(self):
        self.db.execute("BEGIN IMMEDIATE")
        try:
            yield
        except BaseException:
            self.db.rollback()
            raise
        else:
            self.db.commit()

    def reply(self, **fields):
        with self.transaction():
            account = self.get("account", "current")
            player = self.get("player", self.window)
            generation, session = account.get("generation"), player.get("session")
            if self.expected and self.expected != (generation, session):
                return
            response = self.get("response", self.window)
            if (response.get("generation"), response.get("context")) != (
                generation,
                session,
            ):
                response = {}

            if "message" in fields and fields["message"] != response.get("message"):
                fields["notice"] = secrets.token_hex(8)
            response.update(fields)
            response["context"] = self.get("player", self.window).get("session")
            response["generation"] = self.get("account", "current").get("generation")
            self.put("response", self.window, response)

    def context(self, job, media=False):
        account = self.get("account", "current")
        if job.get("account") != account.get("generation"):
            raise AccountError("Account changed. Review the action again.")
        player = self.get("player", self.window)
        if job.get("session") != player.get("session", 0) or (
            media
            and (job.get("media") != player.get("media") or not player.get("allowed"))
        ):
            raise AccountError("Playback or privacy settings changed.")
        return account, player

    def credential(self, account):
        if account.get("expired"):
            raise SessionExpired("Session expired. Reconnect to AniList.")
        token = self.vault.read(account.get("generation", ""))
        if not token:
            raise AccountError("Reconnect to AniList in this window.")
        return token

    def cycle(self, account, player):
        series = "%s/%s" % (account["id"], player["media"])
        return "%s/%s/%d" % (
            series,
            player["episode"],
            self.get("cycle", series).get("number", 0),
        )

    def write(self, job):
        with serialized(self.directory):
            account, p = self.context(job, True)
            token = self.credential(account)
            base = job["base"]
            if (
                base.get("viewer") != account["id"]
                or base.get("session") != p["session"]
                or base.get("media") != p["media"]
            ):
                raise AccountError("The draft no longer belongs to this entry.")
            if job.get("automatic") and (
                not p.get("watch")
                or p["tracking"].get("mode") != "auto"
                or not p.get("safe_episode")
            ):
                raise AccountError("Automatic tracking is off for this file.")

            def validate():
                current, player = self.context(job, True)
                if job.get("automatic") and (
                    player["tracking"].get("mode") != "auto"
                    or not player["active"]
                    or not self.get("pending", self.cycle(current, player))
                ):
                    raise AccountError("Automatic proposal expired.")

            result = save_progress(
                token,
                base,
                job["target"],
                p["total"],
                validate,
                status=job.get("status"),
            )
            self.context(job, True)
            key = self.cycle(account, p)
            with self.transaction():
                self.put(
                    "entry",
                    f"{account['id']}/{p['media']}",
                    dict(result, checked=time.time()),
                )
                self.put(
                    "history",
                    key,
                    {
                        "progress": result["progress"],
                        "time": time.time(),
                        "window": self.window,
                        "session": p["session"],
                    },
                )
                # A list edit also invalidates older proposals for this entry.
                prefix = f"{account['id']}/{p['media']}/%"
                self.db.execute(
                    "DELETE FROM records WHERE category='pending' AND scope LIKE ?",
                    (prefix,),
                )
            self.reply(
                entry=dict(result, session=p["session"]),
                proposal=None,
                read_state="ready",
                last_write=time.time(),
                message="Progress saved.",
            )

    def throttle(self, provider):
        with self.transaction():
            now = time.time()
            when = max(now, self.get("rate", provider).get("next", 0))
            self.put(
                "rate",
                provider,
                {"next": when + (1.1 if provider == "musicbrainz" else 0.5)},
            )
        time.sleep(max(0, when - now))

    def handle(self, job):
        op = job["op"]
        account, p = self.context(
            job, op in ("read", "write", "search", "select", "accept", "rearm")
        )
        if op == "connect":
            if not self.settings.get("broker") and not self.settings.get("personal"):
                raise AccountError(
                    "Public sign-in is not configured. App owners can enable personal sign-in in script options."
                )
            token = authorize(
                self.settings.get("client_id"),
                lambda: self.context(job),
                vault=self.vault,
                broker=self.settings.get("broker", ""),
                progress=lambda message: self.reply(message=message),
            )
            viewer = graphql("query{Viewer{id name avatar{medium}}}", {}, token)[
                "Viewer"
            ]
            with serialized(self.directory):
                self.context(job)
                generation = secrets.token_hex(16)
                persistent = self.vault.write(generation, token)
                if account.get("generation"):
                    self.vault.forget(account["generation"], account.get("persistent"))
                self.put(
                    "account",
                    "current",
                    dict(
                        id=viewer["id"],
                        name=viewer["name"],
                        avatar=viewer["avatar"],
                        generation=generation,
                        persistent=persistent,
                        window=self.window,
                    ),
                )
            self.reply(
                entry=None,
                proposal=None,
                message="Connected." if persistent else "Connected for this session.",
            )
        elif op in ("disconnect", "reset"):
            with serialized(self.directory):
                self.context(job)
                if account.get("generation"):
                    self.vault.forget(account["generation"], account.get("persistent"))
                with self.transaction():
                    self.delete("account", "current")
                    self.db.execute(
                        "DELETE FROM records WHERE category IN ('job','pending','response')"
                    )
                    if op == "reset":
                        self.db.execute("DELETE FROM records WHERE category!='player'")
                        self.put(
                            "privacy",
                            "global",
                            {"mode": "hidden", "block_lookup": True},
                        )
                        self.put("generation", "cache", {"id": secrets.token_hex(16)})
            self.reply(message="Disconnected.")
        elif op == "read":
            if not account.get("id") or not p.get("media"):
                raise AccountError(
                    "Choose a matched anime and connect an account first."
                )
            scope = f"{account['id']}/{p['media']}"
            previous = self.get("entry", scope)
            cached = dict(previous, session=p["session"]) if previous else None
            self.reply(read_state="loading", entry=cached, offline=False)
            try:
                entry = read_entry(self.credential(account), account["id"], p["media"])
            except AccountError:
                self.reply(read_state="offline", entry=cached, offline=True)
                raise
            with self.transaction():
                self.context(job, True)
                self.put("entry", scope, dict(entry, checked=time.time()))
            self.reply(
                entry=dict(entry, session=p["session"]),
                offline=False,
                read_state="ready",
                last_read=time.time(),
                message="Progress refreshed.",
            )
        elif op == "rearm":
            if (
                not p.get("tracking", {}).get("rewatch")
                or not account.get("id")
                or not p.get("media")
            ):
                raise AccountError("Enable rewatch tracking for a matched entry first.")
            with self.transaction():
                scope = str(account["id"]) + "/" + str(p["media"])
                self.put(
                    "cycle",
                    scope,
                    {"number": self.get("cycle", scope).get("number", 0) + 1},
                )
            self.previous = None
            self.reply(elapsed=0, proposal=None, message="New viewing cycle started.")
        elif op == "write":
            self.write(job)
        elif op in ("accept", "skip"):
            key = job["proposal"]
            prop = self.get("pending", key)
            if (
                not prop
                or not account.get("id")
                or not key.startswith(str(account["id"]) + "/")
                or prop["account"] != account["generation"]
            ):
                raise AccountError("This proposal has expired.")
            if op == "accept":
                if (
                    self.cycle(account, p) != key
                    or prop.get("offline")
                    or not p.get("safe_episode")
                ):
                    raise AccountError(
                        "Return to the matched episode and refresh its progress to apply."
                    )
                base = dict(prop["base"], session=p["session"])
                self.write(dict(job, base=base, target=prop["episode"]))
            else:
                with self.transaction():
                    self.put("skipped", key, {"skip": True})
                    self.delete("pending", key)
                self.reply(proposal=None, message="Skipped.")
        elif op == "clear":
            category = job["category"]
            with serialized(self.directory), self.transaction():
                if category in (
                    "appearance",
                    "privacy",
                    "exclude",
                    "tracking",
                    "match",
                    "provider",
                    "sources",
                ):
                    removed = self.get(category, job["scope"])
                    self.delete(category, job["scope"])
                    restricted = (
                        removed.get("blocked")
                        or removed.get("block_lookup")
                        or removed.get("mode") in ("hidden", "generic")
                    )
                    if category in ("privacy", "exclude") and restricted:
                        # Keep open files hidden even when removing their folder exclusion.
                        for (raw,) in self.db.execute(
                            "SELECT value FROM records WHERE category='player'"
                        ):
                            path = json.loads(raw).get("scope_path")
                            scope = job["scope"]
                            applies = (
                                scope == "global"
                                or scope == "file:" + str(path)
                                or (
                                    scope.startswith("folder:")
                                    and path
                                    and path.startswith(scope[7:].rstrip("/") + "/")
                                )
                            )
                            if path and applies:
                                self.put(
                                    "privacy",
                                    "file:" + path,
                                    {"mode": "hidden", "block_lookup": True},
                                )
                elif category in (
                    "watch",
                    "pending",
                    "history",
                    "skipped",
                    "entry",
                ) and account.get("id"):
                    prefix = str(account["id"]) + "/"
                    if job.get("scope"):
                        if not job["scope"].startswith(prefix):
                            raise AccountError("Account scope changed.")
                        self.delete(category, job["scope"])
                    else:
                        self.db.execute(
                            "DELETE FROM records WHERE category=? AND scope LIKE ?",
                            (category, prefix + "%"),
                        )
                elif category == "cache":
                    self.put("generation", "cache", {"id": secrets.token_hex(16)})
                    if job.get("scope"):
                        self.delete("cache", job["scope"])
                    else:
                        self.db.execute("DELETE FROM records WHERE category='cache'")
                else:
                    raise AccountError("Nothing to clear for this account.")
                self.put("generation", "data", {"id": secrets.token_hex(12)})
            self.previous = None
            self.reply(
                message="Local records cleared.", elapsed=0, proposal=None, results=[]
            )
        elif op == "search":
            provider = job["provider"]
            generation = self.get("generation", "data")
            query = job.get("query", "")
            key = (
                provider
                + "/"
                + hashlib.sha256(
                    (p["path"] if provider == "acoustid" else query).encode()
                ).hexdigest()
            )
            cached = self.get("cache", key)
            if cached.get("expires", 0) > time.time():
                hits = cached["results"]
            else:
                if provider == "acoustid":
                    hits = fingerprint(p["path"], self.settings, self.throttle)
                else:
                    hits = candidates(provider, query, self.settings, self.throttle)
                with self.transaction():
                    self.context(job, True)
                    if generation != self.get("generation", "data"):
                        return
                    self.put(
                        "cache",
                        key,
                        {
                            "expires": time.time() + (86400 if hits else 300),
                            "results": hits,
                        },
                    )
            self.reply(
                results=hits,
                provider=provider,
                search_session=p["session"],
                message="Search complete.",
            )
        elif op == "select":
            previous = self.get("response", self.window)
            if previous.get("search_session") != p["session"] or job[
                "candidate"
            ] not in previous.get("results", []):
                raise AccountError("Search again for this file.")
            se = re.search(r"\bS(\d+)E(\d+)\b", os.path.basename(p["path"]), re.I)
            hit = details(
                previous["provider"],
                job["candidate"],
                self.settings,
                self.throttle,
                se.groups() if se else None,
            )
            self.context(job, True)
            scope = {
                "file": "file:" + p["path"],
                "folder": "folder:" + os.path.dirname(p["path"]).replace("\\", "/"),
                "global": "global",
            }[job["scope"]]
            with self.transaction():
                self.context(job, True)
                self.put("provider", scope, hit)
                self.put("match", scope, {"not_anime": True})
            self.reply(message="Match saved.")
        else:
            raise AccountError("Unknown action.")

    def recognize(self, p):
        source = p.get("source", "original")
        key = (p.get("session"), source, p.get("title"), p.get("allowed"))
        if key == self.resolved:
            return
        self.resolved = key
        if source == "original" or not p.get("allowed"):
            return
        raw = p.get("title", "")
        generation = self.get("generation", "data")
        hit = None
        if source == "stream":
            hit = dict(
                provider=source,
                kind="stream",
                name=p.get("channel") or raw,
                link=p.get("page", ""),
            )
            if not hit["link"]:
                del hit["link"]
        elif (
            source == "musicbrainz"
            and not p["video"]
            and re.fullmatch(r"[a-fA-F0-9-]{36}", p.get("recording", ""))
        ):
            hit = dict(
                provider=source,
                kind="recording",
                id=p["recording"],
                name=raw,
                link="https://musicbrainz.org/recording/" + p["recording"],
            )
            if re.fullmatch(r"[0-9a-fA-F-]{36}", p.get("release", "")):
                hit["cover"] = (
                    "https://coverartarchive.org/release/" + p["release"] + "/front-250"
                )
        elif (source in ("tmdb-movie", "tmdb-tv", "tvmaze")) == p["video"]:
            # Search only a title. Never pass a path, URI or query string.
            if "://" in raw or "?" in raw or "/" in raw or chr(92) in raw:
                return
            season = re.search(r"\bS(\d+)E(\d+)\b", raw, re.I)
            title = (raw[: season.start()] if season else raw).strip(" ._-")
            if title.lower().endswith((".mkv", ".mp4", ".avi", ".flac", ".mp3")):
                title = title.rsplit(".", 1)[0]
            title = title.replace(".", " ").strip()
            if not title:
                return
            cache_key = (
                source
                + "/"
                + hashlib.sha256(
                    json.dumps(
                        [
                            title,
                            p.get("artist"),
                            p.get("album"),
                            p.get("tmdb"),
                            season.groups() if season else None,
                            p["path"] if source == "acoustid" else "",
                        ]
                    ).encode()
                ).hexdigest()
            )
            cache = self.get("cache", cache_key)
            if cache.get("expires", 0) > time.time():
                hit = cache.get("hit")
            else:
                if source.startswith("tmdb-") and p.get("tmdb", "").isdigit():
                    chosen = dict(
                        id=int(p["tmdb"]),
                        kind=source.split("-")[1],
                        name=title,
                        title=title,
                    )
                else:
                    if source == "acoustid":
                        rows = fingerprint(p["path"], self.settings, self.throttle)
                    else:
                        rows = candidates(source, title, self.settings, self.throttle)
                    exact = [
                        r for r in rows if r["name"].casefold() == title.casefold()
                    ]
                    chosen = exact[0] if len(exact) == 1 else None
                if chosen:
                    hit = details(
                        source,
                        chosen,
                        self.settings,
                        self.throttle,
                        season.groups() if season else None,
                    )
                with self.transaction():
                    if generation != self.get("generation", "data"):
                        return
                    self.put(
                        "cache",
                        cache_key,
                        {"hit": hit, "expires": time.time() + (86400 if hit else 300)},
                    )
        with self.transaction():
            current = self.get("player", self.window)
            if key != (
                current.get("session"),
                current.get("source"),
                current.get("title"),
                current.get("allowed"),
            ) or generation != self.get("generation", "data"):
                return
            self.put(
                "auto",
                self.window,
                dict(session=p["session"], source=source, title=raw, hit=hit or {}),
            )

    def sample(self):
        p = self.get("player", self.window)
        a = self.get("account", "current")
        self.expected = (a.get("generation"), p.get("session"))
        prev = self.previous
        self.previous = (p, a.get("generation"))
        if (
            not prev
            or not a.get("id")
            or not p.get("active")
            or not p.get("safe_episode")
        ):
            return
        before, generation = prev
        if (
            not before.get("active")
            or generation != a["generation"]
            or any(
                before.get(k) != p.get(k)
                for k in ("session", "media", "episode", "seek", "speed", "tracking")
            )
        ):
            return
        dt = p["time"] - before["time"]
        pos = p["pos"] - before["pos"]
        if not (
            0 < dt <= 2.5
            and pos > 0
            and abs(pos - dt * p["speed"]) < max(0.4, dt * p["speed"] * 0.3)
        ):
            return
        if abs(time.time() * 1000 - p["wall"]) > 3000 or not re.fullmatch(
            r"[1-9]\d*", p["episode"]
        ):
            return
        key = self.cycle(a, p)
        with self.transaction():
            watch = self.get("watch", key)
            if (
                watch.get("owner") not in (None, self.window)
                and 0 <= p["time"] - watch.get("owner_time", 0) < 2.75
            ):
                return
            if watch.get("owner") not in (None, self.window):
                watch["end"] = p["time"]
            # Intersect intervals with the last credited endpoint across all windows.
            end = watch.get("end", 0)
            if end > p["time"]:
                end = p["time"]
            start = max(before["time"], end)
            credit = min(dt, max(0, p["time"] - start))
            if not p["tracking"].get("resume", True) and (
                watch.get("owner") != self.window
                or watch.get("session") != p["session"]
            ):
                watch["elapsed"] = 0
            if self.get("history", key) or self.get("skipped", key):
                return
            elapsed = watch.get("elapsed", 0) + credit
            self.put(
                "watch",
                key,
                dict(
                    watch,
                    elapsed=elapsed,
                    end=p["time"],
                    owner=self.window,
                    owner_time=p["time"],
                    session=p["session"],
                ),
            )
        if not credit:
            return
        self.reply(elapsed=elapsed)
        mode = p["tracking"].get("mode", "off")
        if mode not in ("ask", "auto") or elapsed < 60 * p["tracking"].get(
            "threshold", 15
        ):
            return
        prop = self.get("pending", key)
        if prop and not prop.get("offline"):
            if (
                prop["account"] == a["generation"]
                and prop["base"].get("session") == p["session"]
            ):
                self.reply(proposal=prop)
            return
        if watch.get("retry", 0) > time.time():
            return
        job = dict(account=a["generation"], session=p["session"], media=p["media"])
        # Throttle reads after an offline failure or an unacknowledged write.
        with self.transaction():
            watch = self.get("watch", key)
            watch["retry"] = time.time() + 60
            self.put("watch", key, watch)
        epoch = self.get("generation", "data")
        try:
            entry = read_entry(self.credential(a), a["id"], p["media"])
        except SessionExpired:
            raise
        except AccountError:
            if p["tracking"].get("keep_pending"):
                with self.transaction():
                    self.context(job, True)
                    if epoch == self.get("generation", "data"):
                        self.put(
                            "pending",
                            key,
                            dict(
                                key=key,
                                window=self.window,
                                media=p["media"],
                                title=p.get("match_title", ""),
                                episode=int(p["episode"]),
                                offline=True,
                                account=a["generation"],
                                base={"session": p["session"]},
                            ),
                        )
            raise
        self.context(job, True)
        episode = int(p["episode"])
        if entry["progress"] >= episode:
            self.put("history", key, {"progress": entry["progress"]})
            self.delete("pending", key)
            return
        entry["session"] = p["session"]
        prop = dict(
            title=p.get("match_title", ""),
            media=p["media"],
            window=self.window,
            key=key,
            episode=episode,
            before=entry["progress"],
            base=entry,
            account=a["generation"],
        )
        with self.transaction():
            self.context(job, True)
            existing = self.get("pending", key)
            if (
                (existing and not existing.get("offline"))
                or self.get("history", key)
                or self.get("skipped", key)
                or epoch != self.get("generation", "data")
            ):
                return
            self.put("pending", key, prop)
        if (
            mode == "auto"
            and entry["id"]
            and entry["progress"] + 1 == episode
            and entry["status"] == "CURRENT"
        ):
            self.write(dict(job, base=entry, target=episode, automatic=True))
        else:
            self.reply(proposal=prop, message="Episode ready to review.")

    def run(self):
        while True:
            row = None
            self.expected = None
            try:
                epoch = self.get("generation", "data")
                if self.data_generation != epoch:
                    self.previous = None
                    self.data_generation = epoch
                cancellation = self.get("cancel", self.window)
                if cancellation:
                    with self.transaction():
                        self.db.execute(
                            "DELETE FROM records WHERE category='pending' AND json_extract(value,'$.window')=? AND json_extract(value,'$.base.session')=?",
                            (self.window, cancellation.get("session")),
                        )
                        self.db.execute(
                            "DELETE FROM records WHERE category='job' AND json_extract(value,'$.window')=? AND json_extract(value,'$.session')=?",
                            (self.window, cancellation.get("session")),
                        )
                        self.delete("cancel", self.window)
                    self.previous = None

                with self.transaction():
                    row = self.db.execute(
                        "SELECT scope,value FROM records WHERE category='job' AND json_extract(value,'$.window')=? ORDER BY rowid LIMIT 1",
                        (self.window,),
                    ).fetchone()
                    if row:
                        self.delete("job", row[0])
                if row:
                    job = json.loads(row[1])
                    if job["op"] not in ("connect", "disconnect", "reset"):
                        self.expected = (job.get("account"), job.get("session"))
                    self.handle(job)
                else:
                    p = self.get("player", self.window)
                    self.recognize(p)
                    if not self.previous or p != self.previous[0]:
                        self.sample()
            except AccountError as exc:
                if isinstance(exc, SessionExpired) and self.expected:
                    with self.transaction():
                        current = self.get("account", "current")
                        if current.get("generation") == self.expected[
                            0
                        ] and not current.get("expired"):
                            self.put("account", "current", dict(current, expired=True))
                if row and json.loads(row[1]).get("account") != self.get(
                    "account", "current"
                ).get("generation"):
                    continue
                self.reply(message=str(exc), offline=True)
            except Exception:
                self.reply(
                    message="Operation failed. Check the connection and settings."
                )
            time.sleep(0.25)


if __name__ == "__main__":
    settings = json.load(sys.stdin)
    config = Path(sys.argv[1]).parent.parent / "script-opts" / "rich-presence.conf"
    if config.is_file():
        for line in config.read_text(encoding="utf-8-sig").splitlines():
            key, sep, value = line.partition("=")
            if sep and key.strip() in ("tmdb_key", "acoustid_key"):
                settings[key.strip()] = value.strip().strip('"')
    Worker(sys.argv[1], sys.argv[2], settings).run()
