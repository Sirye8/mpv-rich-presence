# SPDX-License-Identifier: AGPL-3.0-or-later
"""Read the .nfo files that Kodi, Jellyfin and Plex keep beside media."""

from pathlib import Path
import re
import xml.etree.ElementTree as ElementTree

LIMIT = 64 * 1024
ROOTS = {"movie": "movie", "episodedetails": "tv", "tvshow": "tv"}


def _number(text, low, high):
    text = (text or "").strip()
    return int(text) if text.isdigit() and low <= int(text) <= high else None


def _url_ids(text):
    """URL-only .nfo files name the entry by its page link."""
    ids = {}
    tmdb = re.search(r"themoviedb\.org/(movie|tv)/(\d{1,10})", text)
    if tmdb:
        ids["kind"], ids["tmdb"] = ("movie" if tmdb.group(1) == "movie" else "tv"), int(tmdb.group(2))
    imdb = re.search(r"imdb\.com/title/(tt\d{5,10})", text)
    if imdb:
        ids["imdb"] = imdb.group(1)
    return ids


def read_nfo(path):
    """Fields from one .nfo file, or an empty dict when it is missing or unsafe."""
    try:
        with open(path, "rb") as handle:
            data = handle.read(LIMIT + 1)
    except OSError:
        return {}
    if len(data) > LIMIT:
        return {}
    text = data.decode("utf-8", "replace")
    start = text.find("<")
    if start < 0:
        return _url_ids(text)
    # Declarations can expand entities. Real library files never need them.
    if re.search(r"<!(DOCTYPE|ENTITY)", text, re.IGNORECASE):
        return {}
    # Kodi allows a page link after the closing tag, so parse only the root element.
    name = re.match(r"<\?xml[^>]*>\s*<([A-Za-z]+)|<([A-Za-z]+)", text[start:])
    tag = name and (name.group(1) or name.group(2))
    end = text.rfind(f"</{tag}>") if tag else -1
    try:
        root = ElementTree.fromstring(text[start:end + len(tag) + 3] if end >= 0 else text[start:])
    except ElementTree.ParseError:
        return _url_ids(text)
    kind = ROOTS.get(root.tag)
    if not kind:
        return {}
    fields = {"kind": kind, "root": root.tag}
    # An episode file's own title names the episode, so only its show title is used.
    title = root.findtext("showtitle") if root.tag == "episodedetails" else root.findtext("title")
    if title and title.strip():
        fields["title"] = title.strip()[:256]
    year = _number(root.findtext("year") or (root.findtext("premiered") or "")[:4], 1870, 2200)
    if year:
        fields["year"] = str(year)
    season, episode = _number(root.findtext("season"), 0, 999), _number(root.findtext("episode"), 1, 9999)
    if root.tag == "episodedetails" and season is not None and episode:
        fields["season"], fields["episode"] = season, episode
    ids = {}
    for unique in root.findall("uniqueid"):
        ids[(unique.get("type") or "").lower()] = (unique.text or "").strip()
    for name in ("tmdb", "imdb", "tvdb"):
        legacy = root.findtext(name + "id")
        if legacy and name not in ids:
            ids[name] = legacy.strip()
    tmdb = _number(ids.get("tmdb"), 1, 2**31 - 1)
    if tmdb:
        fields["tmdb"] = tmdb
    if re.fullmatch(r"tt\d{5,10}", ids.get("imdb", "")):
        fields["imdb"] = ids["imdb"]
    return fields


def find(path):
    """Merge the .nfo files for a media file, nearest first, with each field's origin."""
    media = Path(path)
    candidates = [media.with_suffix(".nfo"), media.parent / "movie.nfo",
                  media.parent / "tvshow.nfo", media.parent.parent / "tvshow.nfo"]
    merged, origin = {}, {}
    for candidate in candidates:
        if not candidate.is_file():
            continue
        fields = read_nfo(candidate)
        # An episode's own file gives the season and episode. A show file gives the show IDs.
        for key, value in fields.items():
            if key == "root":
                continue
            # An episode's IDs and premiere year belong to the episode, not the show.
            if fields.get("root") == "episodedetails" and key in ("tmdb", "imdb", "year"):
                continue
            if key not in merged:
                merged[key] = value
                origin[key] = candidate.name
    return merged, origin
