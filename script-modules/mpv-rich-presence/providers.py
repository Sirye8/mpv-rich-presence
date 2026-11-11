# SPDX-License-Identifier: AGPL-3.0-or-later
import json
import os
import re
import subprocess
import time
import unicodedata
import urllib.parse as url
import urllib.request
from account import AccountError, NoRedirect, graphql

TMDB_RELAY = "https://mpv-anilist-login.si-based.workers.dev/metadata"


def match_name(value):
    return " ".join(
        re.findall(r"[^\W_]+", unicodedata.normalize("NFKC", value).casefold())
    )


def video_query(label):
    """Extract a public title and common episode/year markers from a basename."""
    if not label or re.search(r"[/\\?&#\x00-\x1f]|://", label):
        return "", "", None, None
    title = re.sub(
        r"\.(mkv|mp4|avi|mov|webm|m4v|ts|mpg|mpeg|wmv)$", "", label, flags=re.I
    )
    title = re.sub(r"^\[[^]]+\]\s*", "", title).replace("_", " ")
    episode = re.search(
        r"(?<!\w)(?:s(\d{1,2})[ ._-]*e(\d{1,3})|(\d{1,2})x(\d{2,3}))(?!\d)", title, re.I
    )
    date = re.search(r"(?<!\d)(\d{4}-\d{2}-\d{2})(?!\d)", title)
    end = min((m.start() for m in (episode, date) if m), default=len(title))
    title = title[:end].replace(".", " ").replace("_", " ").strip(" -")
    years = list(re.finditer(r"(?<!\w)((?:18|19|20)\d{2})(?!\w)", title))
    year = ""
    if years and years[-1].start() > 0:
        match = years[-1]
        tail = title[match.end() :].strip(" )]-")
        if not tail or re.match(
            r"(?i)(?:\d{3,4}p|4k|uhd|bluray|web|hdr|remux|dvdrip)\b", tail
        ):
            title, year = title[: match.start()].strip(" ([{-"), match[1]
    title = re.split(
        r"(?i)\s+(?:\d{3,4}p|4k|uhd|bluray|web[- ]?dl|webrip|hdtv|remux)\b",
        title,
        maxsplit=1,
    )[0]
    numbers = (episode[1] or episode[3], episode[2] or episode[4]) if episode else None
    return (
        " ".join(title.split()),
        year,
        numbers,
        date[1] if date and not numbers else None,
    )


def wikidata_movies(query, throttle):
    api = "https://www.wikidata.org/w/api.php"
    data = fetch(
        api,
        dict(
            action="wbsearchentities",
            search=query,
            language="en",
            format="json",
            limit=12,
        ),
    )
    wanted = match_name(query)
    matches = {
        row["id"]: row
        for row in data["search"]
        if re.fullmatch(r"Q[1-9]\d*", row.get("id", ""))
        and (
            match_name(row.get("label", "")) == wanted
            or match_name(row.get("match", {}).get("text", "")) == wanted
        )
    }
    if not matches:
        return []
    throttle("wikidata")
    entities = fetch(
        api,
        dict(
            action="wbgetentities", ids="|".join(matches), props="claims", format="json"
        ),
    )["entities"]

    def values(claims, prop):
        return [
            statement.get("mainsnak", {}).get("datavalue", {}).get("value", {})
            for statement in claims.get(prop, [])
            if statement.get("rank") != "deprecated"
        ]

    result = []
    for entity_id, row in matches.items():
        claims = entities.get(entity_id, {}).get("claims", {})
        if not any(v.get("id") in {"Q11424", "Q24869"} for v in values(claims, "P31")):
            continue
        years = sorted(
            {
                date[1]
                for v in values(claims, "P577")
                if (date := re.match(r"\+(\d{4})-", v.get("time", "")))
            }
        )
        name = row["label"]
        result.append(
            dict(
                id=entity_id,
                kind="movie",
                name=name,
                match=query,
                title=name + (f" ({years[0]})" if years else ""),
                year=years[0] if years else "",
                years=years,
                link="https://www.wikidata.org/wiki/" + entity_id,
            )
        )
    return result


def fetch(base, params=None, headers=None):
    if params:
        base += "?" + url.urlencode(params)
    headers = dict(headers or {})
    headers["User-Agent"] = (
        "mpv-rich-presence/1.0 (https://github.com/Sirye8/mpv-rich-presence)"
    )
    with urllib.request.build_opener(NoRedirect).open(
        urllib.request.Request(base, headers=headers), timeout=8
    ) as r:
        payload = r.read(2_000_001)
    if len(payload) > 2_000_000:
        raise AccountError("Provider response was too large.")
    return json.loads(payload)


def tmdb_request(path, settings, params=None):
    token = settings.get("tmdb_key")
    try:
        if token:
            return fetch(
                "https://api.themoviedb.org/3" + path,
                params,
                {"Authorization": "Bearer " + token},
            )
        return fetch(TMDB_RELAY + path, params)
    except (OSError, ValueError):
        raise AccountError(
            "TMDB is unavailable. Check the connection or try again later."
        ) from None


def public_query(text):
    text = text.strip()
    if not text or len(text) > 180 or re.search(r"[/\\:?&#\x00-\x1f]", text):
        raise AccountError(
            "Enter a title or provider ID without paths or URL parameters."
        )
    return text


def musicbrainz_terms(**fields):
    terms = []
    for name, text in fields.items():
        if text:
            text = public_query(text).replace('"', '\\"')
            terms.append(f'{name}:"{text}"')
    return " AND ".join(terms)


def musicbrainz_artist(item):
    return "".join(
        a.get("name", "") + a.get("joinphrase", "")
        for a in item.get("artist-credit", [])
    )


def musicbrainz_album(title, artist, album, throttle):
    """Resolve release artwork when multiple recordings share the same tags."""
    if not artist or not album:
        return None
    throttle("musicbrainz")
    rows = fetch(
        "https://musicbrainz.org/ws/2/release-group/",
        {
            "query": musicbrainz_terms(releasegroup=album, artist=artist),
            "fmt": "json",
            "limit": 8,
        },
    )["release-groups"]
    matches = [
        r
        for r in rows
        if match_name(r["title"]) == match_name(album)
        and match_name(musicbrainz_artist(r)) == match_name(artist)
    ]
    if len(matches) != 1:
        return None
    group = matches[0]
    return dict(
        provider="musicbrainz",
        kind="recording",
        name=title,
        artist=artist,
        album=group["title"],
        release_group=group["id"],
        link="https://musicbrainz.org/release-group/" + group["id"],
        cover="https://coverartarchive.org/release-group/" + group["id"] + "/front-250",
    )


def candidates(provider, query, settings, throttle, *, artist="", album=""):
    query = public_query(query)
    throttle(provider)
    if provider == "wikidata":
        return wikidata_movies(query, throttle)
    if provider == "anilist":
        data = graphql(
            """query($q:String!){Page(perPage:10){media(search:$q,type:ANIME){
            id format seasonYear title{romaji english native} coverImage{large}}}}""",
            {"q": query},
        )
        return [
            dict(
                id=m["id"],
                name=m["title"]["romaji"],
                title="%s (%s, %s)"
                % (m["title"]["romaji"], m["seasonYear"] or "?", m["format"]),
                kind="anime",
            )
            for m in data["Page"]["media"]
        ]
    if provider in ("tmdb-movie", "tmdb-tv"):
        kind = provider.split("-")[1]
        params = {"query": query}
        year = re.search(r" \((\d{4})\)$", query)
        if year:
            params.update(query=query[: year.start()])
            params["year" if kind == "movie" else "first_air_date_year"] = year[1]
        data = tmdb_request("/search/" + kind, settings, params)
        return [
            dict(
                id=m["id"],
                kind=kind,
                name=m.get("title") or m["name"],
                title="%s (%s)"
                % (
                    m.get("title") or m["name"],
                    (m.get("release_date") or m.get("first_air_date") or "?")[:4],
                ),
            )
            for m in data["results"][:10]
        ]
    if provider == "tvmaze":
        rows = fetch("https://api.tvmaze.com/search/shows", {"q": query})
        return [
            dict(
                id=m["id"],
                kind="tv",
                name=m["name"],
                year=(m.get("premiered") or "")[:4],
                title="%s (%s, %s, %s)"
                % (
                    m["name"],
                    (m.get("premiered") or "?")[:4],
                    m.get("language") or "?",
                    (m.get("network") or m.get("webChannel") or {}).get("name", "?"),
                ),
                cover=(m.get("image") or {}).get("original", ""),
                link=m["url"],
            )
            for m in [r["show"] for r in rows[:10]]
        ]
    if provider == "musicbrainz":
        rows = fetch(
            "https://musicbrainz.org/ws/2/recording/",
            {
                "query": musicbrainz_terms(
                    recording=query, artist=artist, release=album
                ),
                "fmt": "json",
                "limit": 8,
            },
        )
        result = []
        for m in rows["recordings"]:
            for release in m.get("releases", [])[:3]:
                result.append(
                    dict(
                        id=m["id"],
                        kind="recording",
                        name=m["title"],
                        title=m["title"] + " · " + release["title"],
                        release=release["id"],
                        album=release["title"],
                        artist=musicbrainz_artist(m),
                        duration_ms=m.get("length"),
                        link="https://musicbrainz.org/recording/" + m["id"],
                        cover="https://coverartarchive.org/release/"
                        + release["id"]
                        + "/front-250",
                    )
                )
        return result
    raise AccountError("Unsupported provider.")


def fingerprint(path, settings, throttle, valid=lambda: None):
    if not settings.get("acoustid_key") or not os.path.isfile(path):
        raise AccountError(
            "Fingerprinting needs a local file, fpcalc, and acoustid_key."
        )
    valid()
    with subprocess.Popen(
        ["fpcalc", "-json", "-length", "120", os.path.abspath(path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    ) as process:
        deadline = time.monotonic() + 35
        try:
            while True:
                valid()
                if time.monotonic() >= deadline:
                    raise AccountError("Fingerprinting timed out.")
                try:
                    output, _ = process.communicate(timeout=0.25)
                    break
                except subprocess.TimeoutExpired:
                    continue
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
        if process.returncode:
            raise AccountError("fpcalc could not read this file.")
    fp = json.loads(output)
    throttle("acoustid")
    valid()
    data = fetch(
        "https://api.acoustid.org/v2/lookup",
        {
            "client": settings["acoustid_key"],
            "meta": "recordings releases",
            "duration": int(fp["duration"]),
            "fingerprint": fp["fingerprint"],
        },
    )
    if data.get("status") != "ok":
        raise AccountError(
            "AcoustID rejected the lookup. Check your key and try again."
        )
    return [
        dict(
            id=r["id"],
            kind="recording",
            name=r["title"],
            title=r["title"],
            link="https://musicbrainz.org/recording/" + r["id"],
        )
        for hit in data.get("results", [])
        for r in hit.get("recordings", [])
        if r.get("title")
    ][:10]


def details(provider, hit, settings, throttle, episode, airdate=None):
    item = dict(hit, provider=provider)
    if provider.startswith("tmdb-"):
        throttle(provider)
        kind = hit["kind"]
        data = tmdb_request("/%s/%d" % (kind, hit["id"]), settings)
        item["name"] = data.get("title") or data.get("name") or hit["name"]
        item["year"] = (data.get("release_date") or data.get("first_air_date") or "")[
            :4
        ]
        item["genre"] = ", ".join(
            g["name"] for g in data.get("genres", []) if g.get("name")
        )
        if kind == "tv" and episode:
            throttle(provider)
            season, number = map(int, episode)
            ep = tmdb_request(
                "/tv/%d/season/%d/episode/%d" % (hit["id"], season, number), settings
            )
            item["episode_title"] = ep.get("name", "")
            item["numbering"] = "S%02dE%02d" % (season, number)
        if data.get("poster_path"):
            item["cover"] = "https://image.tmdb.org/t/p/w500" + data["poster_path"]
        item["link"] = "https://www.themoviedb.org/%s/%d" % (kind, hit["id"])
    if provider == "tvmaze" and episode:
        throttle(provider)
        data = fetch(
            "https://api.tvmaze.com/shows/%d/episodebynumber" % hit["id"],
            {"season": episode[0], "number": episode[1]},
        )
        item["episode_title"] = data.get("name", "")
        item["numbering"] = "S%02dE%02d" % tuple(map(int, episode))
    elif provider == "tvmaze" and airdate:
        throttle(provider)
        matches = fetch(
            "https://api.tvmaze.com/shows/%d/episodesbydate" % hit["id"],
            {"date": airdate},
        )
        if len(matches) == 1:
            item["episode_title"] = matches[0].get("name", "")
            item["numbering"] = airdate
    return item
