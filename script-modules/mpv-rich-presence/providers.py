# SPDX-License-Identifier: AGPL-3.0-or-later
import json
import os
import re
import subprocess
import urllib.parse as url
import urllib.request
from account import AccountError, graphql


def fetch(base, params=None, headers=None):
    if params:
        base += "?" + url.urlencode(params)
    headers = dict(headers or {})
    headers["User-Agent"] = (
        "mpv-rich-presence/1.0 (https://github.com/Sirye8/mpv-rich-presence)"
    )
    with urllib.request.urlopen(
        urllib.request.Request(base, headers=headers), timeout=8
    ) as r:
        payload = r.read(2_000_001)
    if len(payload) > 2_000_000:
        raise AccountError("Provider response was too large.")
    return json.loads(payload)


def public_query(text):
    text = text.strip()
    if not text or len(text) > 180 or re.search(r"[/\\:?&#\x00-\x1f]", text):
        raise AccountError(
            "Enter a title or provider ID without paths or URL parameters."
        )
    return text


def candidates(provider, query, settings, throttle):
    query = public_query(query)
    throttle(provider)
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
        if not settings.get("tmdb_key"):
            raise AccountError("Set tmdb_key to your TMDB read access token.")
        kind = provider.split("-")[1]
        params = {"query": query}
        year = re.search(r" \((\d{4})\)$", query)
        if year:
            params.update(query=query[: year.start()])
            params["year" if kind == "movie" else "first_air_date_year"] = year[1]
        data = fetch(
            "https://api.themoviedb.org/3/search/" + kind,
            params,
            {"Authorization": "Bearer " + settings["tmdb_key"]},
        )
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
                title="%s (%s, %s)"
                % (
                    m["name"],
                    (m.get("premiered") or "?")[:4],
                    m.get("language") or "?",
                ),
                cover=(m.get("image") or {}).get("original", ""),
                link=m["url"],
            )
            for m in [r["show"] for r in rows[:10]]
        ]
    if provider == "musicbrainz":
        rows = fetch(
            "https://musicbrainz.org/ws/2/recording/",
            {"query": query, "fmt": "json", "limit": 8},
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
                        link="https://musicbrainz.org/recording/" + m["id"],
                        cover="https://coverartarchive.org/release/"
                        + release["id"]
                        + "/front-250",
                    )
                )
        return result
    raise AccountError("Unsupported provider.")


def fingerprint(path, settings, throttle):
    if not settings.get("acoustid_key") or not os.path.isfile(path):
        raise AccountError(
            "Fingerprinting needs a local file, fpcalc, and acoustid_key."
        )
    raw = subprocess.run(
        ["fpcalc", "-json", "-length", "120", os.path.abspath(path)],
        capture_output=True,
        timeout=35,
        check=True,
    )
    fp = json.loads(raw.stdout)
    throttle("acoustid")
    data = fetch(
        "https://api.acoustid.org/v2/lookup",
        {
            "client": settings["acoustid_key"],
            "meta": "recordings releases",
            "duration": int(fp["duration"]),
            "fingerprint": fp["fingerprint"],
        },
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


def details(provider, hit, settings, throttle, episode):
    item = dict(hit, provider=provider)
    if provider.startswith("tmdb-"):
        throttle(provider)
        kind = hit["kind"]
        data = fetch(
            "https://api.themoviedb.org/3/%s/%d" % (kind, hit["id"]),
            headers={"Authorization": "Bearer " + settings["tmdb_key"]},
        )
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
    return item
