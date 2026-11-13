# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build private replay cases from the prior audit and independent assertions."""
import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path, PureWindowsPath
import re

from recognition_suite import anime, case


ANIME = "Anime series and extras"
FILM = "Anime films"
MUSIC = {"SpotiFLAC music", "Audio alongside video"}
TV = {"Live-action TV", "Western animated TV"}
KNOWN_MOVIES = {
    "Top.Gun.1986": "Top Gun", "The.Shawshank.Redemption.1994": "The Shawshank Redemption",
    "Jack.Reacher.2012": "Jack Reacher", "Interstellar.2014": "Interstellar",
    "Daredevil.2003": "Daredevil", "Ant-Man.2015": "Ant-Man",
}
# These relations are covered by the public boundary tests and checked against
# the entries seen in the original audit.
CHAINS = (
    (97940, 195604), (21459, 21856, 100166, 104276, 117193, 139630, 163139),
    (108465, 127720, 146065, 166873, 178789), (116674, 159322, 169755, 185874),
    (10087, 11741), (19603, 20792), (16498, 20958, 99147, 104578, 110277, 131681),
    (101348, 136430), (150672, 166531, 182587), (11757, 20594, 100182, 108759, 114308),
    (2156, 5337, 7334, 10330), (151807, 176496), (171018, 185660),
    (113415, 145064, 172463), (101280, 108511, 116742, 156822, 182205),
    (2251, 3901), (141391, 175914), (20464, 20992, 21698, 106625, 113538),
    (20920, 101167, 112124, 129196, 155211, 170732), (161645, 176301),
    (153288, 178754), (20755, 21170), (108632, 119661),
)
ADDITIONAL_ENTRIES = (
    (442, "Naruto the Movie: Ninja Clash in the Land of Snow", 1, "MOVIE"),
    (153288, "Kaijuu 8-gou", 12, "TV"),
    (108511, "Tensei Shitara Slime Datta Ken 2nd Season", 12, "TV"),
    (116742, "Tensei Shitara Slime Datta Ken 2nd Season Part 2", 12, "TV"),
    (101167, "Dungeon ni Deai wo Motomeru no wa Machigatteiru Darou ka II", 12, "TV"),
    (129196, "Dungeon ni Deai wo Motomeru no wa Machigatteiru Darou ka IV", 11, "TV"),
    (110277, "Shingeki no Kyojin: The Final Season", 16, "TV"),
    (20995, "Nekomonogatari (Shiro)", 4, "TV"),
)
ALIASES = {
    137667: ["Lord of Mysteries"], 20981: ["The Boy and The Beast"],
    9989: ["Anohana"], 176496: ["Solo Leveling Season 2"],
    151807: ["Solo Leveling"], 151970: ["ShanFro", "Shangri-La Frontier"],
    151514: ["Orb On the Movements of the Earth"], 30: ["Neon Genesis Evangelion"],
    153518: ["Delicious in Dungeon"], 175914: ["Call of the Night Season 2"],
    141391: ["Call of the Night"], 1210: ["Welcome to the N.H.K."],
    98659: ["Classroom of the Elite"], 108465: ["Mushoku Tensei Jobless Reincarnation"],
    161645: ["The Apothecary Diaries"], 176301: ["The Apothecary Diaries Season 2"],
    116674: ["Bleach Thousand Year Blood War", "Bleach Sennen Kessen-hen"],
    159322: ["Bleach Thousand Year Blood War Part 2", "Bleach Sennen Kessen-hen Part 2"],
    169755: ["Bleach Thousand Year Blood War Part 3", "Bleach Sennen Kessen-hen Part 3"],
    185874: ["Bleach Thousand Year Blood War Part 4", "Bleach Sennen Kessen-hen Part 4"],
    150672: ["Oshi no Ko"], 166531: ["Oshi no Ko Season 2"],
    182587: ["Oshi no Ko Season 3"], 2156: ["Bakugan Battle Brawlers"],
    5337: ["Bakugan New Vestroia"], 7334: ["Bakugan Gundalian Invaders"],
    10330: ["Bakugan Mechtanium Surge"], 18115: ["Magi The Kingdom of Magic"],
    14513: ["Magi The Labyrinth of Magic"], 21394: ["Magi Adventure of Sinbad"],
    20755: ["Assassination Classroom"], 21170: ["Assassination Classroom Season 2"],
    107663: ["Astra Lost in Space"], 136430: ["Vinland Saga Season 2"],
    172463: ["Jujutsu Kaisen Season 3"], 145064: ["Jujutsu Kaisen Season 2"],
    110277: ["Shingeki no Kyojin Final Season"], 131681: ["Shingeki no Kyojin The Final Season Part 2"],
    171627: ["Chainsaw Man The Movie Reze Arc"], 442: ["Naruto the Movie Ninja Clash in the Land of Snow"],
    21459: ["My Hero Academia", "Boku no Hero Academia"],
    21856: ["My Hero Academia Season 2"], 100166: ["My Hero Academia Season 3"],
    104276: ["My Hero Academia Season 4"], 117193: ["My Hero Academia Season 5"],
    139630: ["My Hero Academia Season 6"], 163139: ["My Hero Academia Season 7"],
    146065: ["Mushoku Tensei Season 2", "Mushoku Tensei 2nd Season"],
    166873: ["Mushoku Tensei Season 2 Part 2"],
    20920: ["DanMachi"], 112124: ["DanMachi Season 3"],
    129196: ["DanMachi Season 4"], 170732: ["DanMachi Season 5"],
    106625: ["Haikyuu!! Season 4"], 108632: ["Re Zero Starting Life in Another World Season 2"],
    10408: ["To the Forest of Firefly Lights"], 11577: ["Steins;Gate The Movie Load Region of Deja Vu"],
    13125: ["From the New World"],
    153288: ["Kaijuu 8 gou"], 10087: ["Fate Zero"],
    20995: ["Nekomonogatari"],
    20958: ["Attack on Titan Season 2"], 99147: ["Attack on Titan Season 3"],
    104578: ["Attack on Titan Season 3 Part 2"],
    19603: ["Fate Stay Night_ Unlimited Blade Works"],
    10863: ["Steins;Gate: Egoistic Poriomania"],
}
EPISODE = re.compile(r"(?i)(?:^|[ ._-])s(\d{1,2})[ ._-]*e(\d{1,3})(?!\d)")
ABSOLUTE = re.compile(r"(?i)(?:[-–]\s*|\bep(?:isode)?\s+)(\d{1,4})(?:v\d+)?(?=$|\D)")
EXTRA = re.compile(r"(?i)(?<![a-z])(?:nc\s*(?:op|ed)|opening|ending|creditless|trailer|pv|omake|sample)(?:\d+)?(?![a-z])|(?:^|[ _-])(?:op|ed)[ _-]*\d")
CORRECTIONS = {
    "Naruto the Movie (2004)": (442, 0),
    "Sword Art Online S2 - 01.mkv": (20594, 1),
    "My Hero Academia - 14 [720p]": (21856, 1),
    "Mushoku Tensei - 12v2": (127720, 1),
}
DIRECT_FILM_QUERIES = {
    "Chainsaw Man - The Movie Reze Arc": "Chainsaw Man Reze Arc",
    "Violet Evergarden The Movie": "Violet Evergarden The Movie",
}


def rows_from(directory):
    return [json.loads(line) for path in sorted(directory.glob("*/results.jsonl"))
            for line in path.read_text(encoding="utf-8").splitlines()]


def index_evidence(rows, catalog_pages):
    observations = defaultdict(list)
    entries = {}
    for row in rows:
        observations[row["path"]].append(row)
        ident = row.get("match", 0)
        if ident and row.get("title"):
            movie = not row.get("episode") and row.get("total") == 1
            entry = entries.setdefault(ident, anime(ident, row["title"], row.get("total") or None,
                                                   format="MOVIE" if movie else "TV"))
            if not movie:
                entry["format"] = "TV"
    for page in catalog_pages.values():
        for row in page:
            entries[row["id"]] = row
    for ident, title, episodes, format in ADDITIONAL_ENTRIES:
        entries.setdefault(ident, anime(ident, title, episodes, format=format))
    for ident, aliases in ALIASES.items():
        if ident in entries:
            entries[ident]["synonyms"].extend(aliases)
    if 10863 in entries:
        entries[10863]["format"] = "SPECIAL"
    for chain in CHAINS:
        for first, second in zip(chain, chain[1:]):
            if first in entries and second in entries and not any(
                edge["relationType"] == "SEQUEL" for edge in entries[first]["relations"]["edges"]
            ):
                entries[first]["relations"]["edges"].extend(anime(first, "", 0, sequel=second)["relations"]["edges"])
            if first in entries and second in entries and not any(
                edge["relationType"] == "PREQUEL" for edge in entries[second]["relations"]["edges"]
            ):
                entries[second]["relations"]["edges"].append(
                    {"relationType": "PREQUEL", "node": {"id": first, "type": "ANIME", "format": "TV"}}
                )
    return observations, entries


def season_number(name):
    match = EPISODE.search(name)
    return tuple(map(int, match.groups())) if match else None


def absolute_number(name):
    match = ABSOLUTE.search(name)
    return int(match[1]) if match else None


def dated_title(name):
    match = re.match(r"^(.*?)[ .([]((?:19|20)\d{2})(?=$|[^\w])", name)
    if not match:
        return "", ""
    title = re.sub(r"^\[[^]]+\][. _-]*", "", match[1]).replace(".", " ").strip()
    return title, match[2]


def catalog_for_anime(row, path, by_family, by_folder, entries, pages):
    name = path.name
    category = row["category"]
    source = "saved audit observation"
    expected = None
    ids = list(by_family[(path.drive, path.parts[2])])
    observed = int(row["anilist_id"] or 0)
    if category == FILM and path.parts[2] == "#Standalone Movies":
        ids = list(by_folder[path.parent])
    if observed:
        old_episode = row["matched_episode"]
        expected = ("anilist", observed, int(old_episode) if old_episode.isdigit() else 0)
        ids = [observed, *(ident for ident in ids if ident != observed)]
    if EXTRA.search(name):
        return [], ("local", "", ""), "Opening, ending, trailer or sample marker"

    number = absolute_number(name)
    season = season_number(name)
    family = path.parts[2]
    if family == "Black Clover" and season and re.search(r"\(\d{3}\)", name):
        original = int(re.search(r"\((\d{3})\)", name)[1])
        if original <= 170:
            expected = ("anilist", 97940, original)
            source = "Original series absolute number explicitly in filename"
    elif family == "Naruto" and number and category == ANIME and not EXTRA.search(name):
        expected = ("anilist", 1735 if "shippuden" in name.casefold() else 20, number)
        source = "Named Naruto series and absolute filename episode"
    elif family == "My Hero Academia" and number and category == ANIME and "special" not in name.casefold():
        remaining = number
        for ident in (21459, 21856, 100166, 104276, 117193, 139630, 163139):
            if ident not in entries:
                break
            total = entries[ident]["episodes"]
            if remaining <= total:
                expected = ("anilist", ident, remaining)
                source = "Absolute filename episode and verified sequel boundaries"
                break
            remaining -= total
    elif family == "My Hero Academia" and season and 1 <= season[0] <= 7:
        expected = ("anilist", (21459, 21856, 100166, 104276, 117193, 139630, 163139)[season[0] - 1], season[1])
        source = "Explicit season and episode in the filename"
    elif family == "Bleach" and category == ANIME:
        if "kessen-hen" in path.parent.name.casefold() and (number or season):
            absolute = season[1] if season else number
            if season and season[0] in (2, 3, 4) and absolute <= 13:
                absolute += (13, 26, 40)[season[0] - 2]
            for ident, first, last in ((116674, 1, 13), (159322, 14, 26), (169755, 27, 40), (185874, 41, 50)):
                if first <= absolute <= last:
                    expected = ("anilist", ident, absolute - first + 1)
                    source = "Thousand-Year Blood War folder, filename episode and cour boundaries"
        elif number and number <= 366 and "bleach -" in name.casefold():
            expected = ("anilist", 269, number)
            source = "Original BLEACH absolute episode number"
    elif family == "Vinland Saga" and number:
        expected = ("anilist", 136430 if " s2 " in name.casefold() else 101348, number)
        source = "Explicit season and filename episode"
    elif family == "Oshi no Ko" and number:
        second = " S2 " in path.parent.name
        third = " S3 " in path.parent.name
        expected = ("anilist", 182587 if third else 166531 if second else 150672,
                    number - 11 if second else number)
        source = "Season folder and series-wide filename episode"
    elif family == "Bakugan" and number:
        season_folder = re.fullmatch(r"Season ([1-4])", path.parent.name)
        if season_folder:
            ident = (2156, 5337, 7334, 10330)[int(season_folder[1]) - 1]
            expected = ("anilist", ident, number)
            source = "Release folder and episode in the filename"
    elif family == "Magi" and number and category == ANIME:
        for title, ident in (("Kingdom of Magic", 18115), ("Labyrinth of Magic", 14513), ("Adventure of Sinbad", 21394)):
            if title.casefold() in name.casefold():
                expected = ("anilist", ident, number)
                source = "Distinct Magi series title and filename episode"
                break
    elif family == "Tensei Shitara Slime Datta Ken" and season and season[0] == 2 and 13 <= season[1] <= 24:
        expected = ("anilist", 116742, season[1] - 12)
        source = "Season 2 has two 12-episode cours"
    elif family == "Shingeki no Kyojin" and number and "Final Season Part 2" in name and 76 <= number <= 87:
        expected = ("anilist", 131681, number - 75)
        source = "Explicit Final Season Part 2 title and series-wide episode"
        ids = [131681, 110277, 104578, 99147, 20958, 16498]
    elif family == "Monogatari" and "Nekomonogatari " in name:
        expected = ("anilist", 20995, int(re.search(r"Nekomonogatari (\d+)", name)[1]))
        source = "Nekomonogatari chapter title and local chapter number"
    for marker, (ident, episode) in CORRECTIONS.items():
        if marker in name:
            expected = ("anilist", ident, episode)
            source = "Independently verified audit correction"
    if expected and expected[1] not in entries:
        return [entries[i] for i in ids if i in entries], None, "Catalog entry missing from replay"
    if expected and expected[1] not in ids:
        ids.append(expected[1])
    if family == "Kaijuu 8-gou":
        ids = [153288, 178754, *[ident for ident in ids if ident not in (153288, 178754)]]
    related = []
    for ident in ids:
        if ident not in related and ident in entries:
            related.append(ident)
    for page in pages.values():
        known = {entry["id"] for entry in page}
        if known.intersection(related):
            related.extend(entry["id"] for entry in page if entry["id"] not in related)
    return [entries[ident] for ident in related if ident in entries], expected, source


def make_cases(report, journal, catalog, replay_root=None):
    with report.open(encoding="utf-8-sig", newline="") as file:
        inventory = list(csv.DictReader(file))
    original_rows = rows_from(journal)
    followup_by_name = defaultdict(list)
    if replay_root:
        for recorded in rows_from(replay_root):
            followup_by_name[PureWindowsPath(recorded["path"]).name].append(recorded)
    observations, entries = index_evidence(original_rows, catalog)
    by_family = defaultdict(Counter)
    by_folder = defaultdict(Counter)
    for row in inventory:
        path = PureWindowsPath(row["path"])
        ident = int(row["anilist_id"] or 0)
        if ident:
            by_family[(path.drive, path.parts[2])][ident] += 1
            by_folder[path.parent][ident] += 1
    for row in inventory:
        path = PureWindowsPath(row["path"])
        name = path.name
        category = row["category"]
        observed = sorted(observations[row["path"]], key=lambda r: r["outcome"] == "metadata", reverse=True)
        followup = [r for r in followup_by_name[name] if r["outcome"] == "metadata"]
        best = next((r for r in observed if r.get("duration")), {})
        tags = {k.casefold(): v for k, v in best.get("tags", {}).items() if isinstance(v, str)}
        hits = {}
        page = []
        expected = None
        evidence = ""
        if category in (ANIME, FILM):
            page, expected, evidence = catalog_for_anime(row, path, by_family, by_folder, entries, catalog)
            if category == FILM and not expected:
                checked = next((r for r in followup if r.get("match")), None)
                if checked:
                    ident = checked["match"]
                    if ident not in entries:
                        entries[ident] = anime(ident, checked["title"], 1, format="MOVIE")
                    if all(entry["id"] != ident for entry in page):
                        page.append(entries[ident])
                    expected = ("anilist", ident, 0)
                    evidence = "Confirmed in the targeted mpv replay"
                for marker, query in DIRECT_FILM_QUERIES.items():
                    if marker in name and catalog.get(query):
                        result = catalog[query][0]
                        if result["format"] == "MOVIE":
                            page = [result]
                            expected = ("anilist", result["id"], 0)
                            evidence = "Verified named film in AniList"
        if category in TV:
            marker = EPISODE.search(name)
            if marker:
                title = name[:marker.start()].replace(".", " ").strip()
                number = "S%02dE%02d" % tuple(map(int, marker.groups()))
                provider = "tmdb-tv"
                expected = (provider, title, number)
                evidence = "Series title and season/episode in filename"
                hits[provider] = [dict(id=1, name=title, kind="tv", genre="Drama",
                                       numbering=number, year="2021")]
                if title == "Arcane":
                    page = [anime(217391, "1999! Shenmi Xue Duice Bu", 12,
                                  aliases=["1999! Arcane Incident Department"], duration=6, format="ONA")]
                    hits[provider][0]["genre"] = "Animation"
                if title == "Loki":
                    page = [anime(335, "Matantei Loki RAGNAROK", 26)]
        elif category == "Non-anime films and videos":
            title, year = dated_title(name)
            title = row["recognized_title"] or next((v for k, v in KNOWN_MOVIES.items() if k in name), title)
            if title and year:
                hits["tmdb-movie"] = [dict(id=1, name=title, kind="movie", year=year)]
                expected = ("tmdb-movie", title, "")
                evidence = "Filename year and the catalog title from the audit or targeted replay"
        elif category in MUSIC:
            known = next((r for r in observed if r.get("provider_match")), {})
            if known.get("provider") == "musicbrainz":
                hit = dict(known["provider_match"])
                hit.setdefault("id", hit["link"])
                hits["musicbrainz"] = [hit]
                expected = ("musicbrainz", hit["name"], "")
                evidence = "Observed recording and captured tags"
            elif known.get("provider") == "acoustid":
                hit = known["provider_match"]
                hit = dict(hit, score=1)
                hits["acoustid"] = [hit]
                expected = ("acoustid", hit["name"], "")
                evidence = "Observed fingerprint result and captured tags"
            elif tags.get("title") and tags.get("artist"):
                evidence = "Captured tags, no validated recording fixture"
        if not page and expected and expected[0] == "anilist":
            page = [entries[expected[1]]]
        # Use the same regional search results for that exact query where available.
        pages = {query: rows for query, rows in catalog.items()
                 if {entry["id"] for entry in rows}.intersection(entry["id"] for entry in page)}
        data = case(name, expected, page=page, entries=(),
                    video=category not in MUSIC, duration=best.get("duration", 0),
                    tags=tags, hits=hits, category=category, path=row["path"], evidence=evidence,
                    folder=path.parent.name)
        data["pages"] = pages
        relevant = {item["id"] for item in page}
        for values in pages.values():
            relevant.update(item["id"] for item in values)
        while True:
            additions = {edge["node"]["id"] for ident in relevant if ident in entries
                         for edge in entries[ident]["relations"]["edges"]
                         if edge["relationType"] in ("SEQUEL", "PREQUEL")} - relevant
            if not additions:
                break
            relevant.update(additions)
        data["by_id"] = {str(ident): entries[ident] for ident in sorted(relevant) if ident in entries}
        yield data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inventory_csv", type=Path)
    parser.add_argument("journal_directory", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--catalog", type=Path, help="Optional saved AniList query results keyed by search string")
    parser.add_argument("--replays", type=Path, help="Optional targeted mpv replay journals")
    args = parser.parse_args()
    catalog = json.loads(args.catalog.read_text(encoding="utf-8")) if args.catalog else {}
    cases = list(make_cases(args.inventory_csv, args.journal_directory, catalog, args.replays))
    if len({test["path"] for test in cases}) != len(cases):
        parser.error("Duplicate paths in the inventory")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("".join(json.dumps(test, ensure_ascii=False) + "\n" for test in cases), encoding="utf-8")
    print(f"Wrote {len(cases)} unique cases, {sum(test['expected'] is not None for test in cases)} with an expected result")


if __name__ == "__main__":
    main()
