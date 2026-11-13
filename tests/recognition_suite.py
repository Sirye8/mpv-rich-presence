# SPDX-License-Identifier: AGPL-3.0-or-later
"""Replay Automatic recognition against controlled provider data."""
import argparse
from collections import Counter
from contextlib import contextmanager
import csv
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "script-modules/mpv-rich-presence"))
import backend


def anime(ident, title, episodes, *, aliases=(), sequel=None, **fields):
    edges = [] if sequel is None else [
        {"relationType": "SEQUEL", "node": {"id": sequel, "type": "ANIME", "format": "TV"}}
    ]
    return dict(id=ident, title={"romaji": title}, synonyms=list(aliases), episodes=episodes,
                format=fields.pop("format", "TV"), duration=fields.pop("duration", 24),
                startDate={"year": fields.pop("year", None)}, relations={"edges": edges}, **fields)


def case(name, expected=None, *, page=(), entries=(), video=True, duration=1440, hits=None, **fields):
    return dict(filename=name, expected=expected, default_page=list(page), pages={},
                by_id={str(row["id"]): row for row in entries}, video=video,
                duration=duration, hits=hits or {}, **fields)


def fixture_cases():
    clover = anime(97940, "Black Clover", 170, sequel=195604)
    clover2 = anime(195604, "Black Clover 2nd Season", None)
    cases = []
    same_title = [anime(90001, "Identical Fixture", 12), anime(90002, "Identical Fixture", 12)]
    for rows in (same_title, list(reversed(same_title))):
        cases.append(case("[Group] Identical Fixture - 03 (1080p).mkv", ("local", "", ""),
                          page=rows, watch=False))
    cases.append(case("[Group] Identical Fixture - 03 (1080p).mkv", ("anilist", 90001, 3),
                      page=[same_title[0], same_title[0]], watch=True))
    for episode in (1, 169, 170, 171, 172, 174, 180, 200):
        second = episode > 170
        cases.append(case(
            f"[SubsPlease] Black Clover - {episode:03d} (1080p).mkv",
            ("anilist", 195604 if second else 97940, episode - 170 if second else episode),
            page=[clover, clover2], entries=[clover2], watch=True,
        ))
    cases.extend([
        case("[Judas] Black Clover - S02E02 (053).mkv", ("anilist", 97940, 53),
             page=[clover2, clover], entries=[clover2], watch=True),
        case("[Judas] Black Clover - OP 01 - Haruka Mirai.mkv", ("local", "", ""),
             page=[clover], duration=90),
        case("[SubsPlease] Black Clover - 174.5 (1080p).mkv", ("anilist", 195604, 4),
             page=[clover], entries=[clover2], watch=False),
        case("[SubsPlease] Black Clover - 174-175 (1080p).mkv", ("anilist", 195604, 4),
             page=[clover], entries=[clover2], watch=False),
        case("[SubsPlease] Black Clover - 172 (1080p).mkv", ("anilist", 97940, 172),
             page=[clover], watch=False),
        case("[SubsPlease] Black Clover S2 - 02 (1080p).mkv", ("anilist", 195604, 2),
             page=[clover2, clover], watch=True),
    ])
    first = anime(1, "Fixture Show", 12, sequel=2)
    second = anime(2, "Fixture Show II", 10, sequel=3)
    third = anime(3, "Fixture Show III", 5)
    for episode, ident, number in ((12, 1, 12), (13, 2, 1), (22, 2, 10), (23, 3, 1), (27, 3, 5), (28, 3, 6)):
        cases.append(case(f"[Group] Fixture Show - {episode}v2 (720p).mkv",
                          ("anilist", ident, number), page=[third, second, first],
                          entries=[second, third], watch=episode <= 27))
    first_cour = anime(101, "Another Series", 24, sequel=102)
    second_cour = anime(102, "Another Series Part 2", 12, sequel=103)
    final_cour = anime(103, "Another Series Part 3", 12)
    second_cour["relations"]["edges"].append(
        {"relationType": "PREQUEL", "node": {"id": 101, "type": "ANIME", "format": "TV"}}
    )
    cases.extend([
        case("[Group] Another Series (Part 2) - 25 (1080p).mkv", ("anilist", 102, 1),
             page=[final_cour, second_cour, first_cour], entries=[first_cour, final_cour], watch=True),
        case("[Group] Another Series (Part 2) - 09 (1080p).mkv", ("anilist", 102, 9),
             page=[final_cour, second_cour, first_cour], entries=[first_cour, final_cour], watch=True),
        case("[Group] Another Series (Part 2) - 36 (1080p).mkv", ("anilist", 102, 12),
             page=[final_cour, second_cour, first_cour], entries=[first_cour, final_cour], watch=True),
        case("Bakugan_ Mechtanium Surge Episode 44 (1-2).mp4", ("anilist", 10330, 44),
             page=[anime(10330, "Bakugan Battle Brawlers: Mechtanium Surge", 46)], watch=True),
        case("[Group] Fixture Show - 13 (OVA) - After the Finale [1080p].mkv",
             ("anilist", 104, 1), page=[first, anime(104, "Fixture Show: After the Finale", 1,
                                                       format="SPECIAL")], watch=False),
        case("Fixture Show S17E02.mkv", ("anilist", 105, 2), folder="Fixture Show Named Arc",
             page=[first, anime(105, "Fixture Show: Named Arc", 13, aliases=["Fixture Show Named Arc"])],
             watch=True),
        case("Fixture.Show.S17E02.The.Episode.Subtitle.1080p.mkv", ("anilist", 105, 2),
             folder="Fixture Show Named Arc",
             page=[first, anime(105, "Fixture Show: Named Arc", 13, aliases=["Fixture Show Named Arc"])],
             watch=True),
        case("Fixture Show S02E02 (014).mkv", ("anilist", 2, 2), page=[first, second],
             entries=[second], watch=True),
        case("03.mkv", ("anilist", 1, 3), page=[first], folder="Fixture Show", watch=True),
        case("S02E04.mkv", ("tmdb-tv", "Example Show", "S02E04"), folder="Example Show",
             hits={"tmdb-tv": [dict(id=1, name="Example Show", kind="tv", numbering="S02E04")]}),
        case("02.mkv", ("tmdb-tv", "Example Show", "S01E02"), folder="Example Show Season 1",
             hits={"tmdb-tv": [dict(id=1, name="Example Show", kind="tv", numbering="S01E02")]}),
        case("movie.mkv", ("tmdb-movie", "Example Film", ""), folder="Example Film (2024)",
             duration=6500, hits={"tmdb-movie": [dict(id=1, name="Example Film", kind="movie", year="2024")]}),
        case("movie.mkv", ("tmdb-movie", "Example Film", ""), folder="Example Film",
             duration=6500, hits={"tmdb-movie": [dict(id=1, name="Example Film", kind="movie", year="2024")]}),
        case("movie.mkv", ("local", "", ""), folder="Example Film", duration=6500,
             hits={"tmdb-movie": [dict(id=1, name="Example Film", kind="movie", year="2024"),
                                  dict(id=2, name="Example Film", kind="movie", year="1994")]}),
        case("movie.mkv", ("anilist", 106, 0), folder="Example Anime Film (2024)",
             duration=6500, page=[anime(106, "Example Anime Film", 1, format="MOVIE", year=2024)]),
        case("movie.mkv", ("local", "", ""), folder="Example Film (2024)", duration=75),
        case("movie.mkv", ("local", "", ""), folder="Movies", duration=6500),
        case("04.mkv", ("local", "", ""), folder="Season 2"),
        case("Screen Recording 2026-11-12.mp4", ("local", "", ""), folder="Example Film (2024)", duration=80),
        case("Fixture Show - 04.mkv", ("anilist", 1, 4), page=[first],
             folder="Unrelated Film (2024)", watch=True),
        case("movie.mkv", ("tmdb-movie", "Actual Film", ""), folder="Unrelated Film (2024)",
             duration=6500, tags={"title": "Actual Film (2020)"},
             hits={"tmdb-movie": [dict(id=1, name="Actual Film", kind="movie", year="2020")]}),
    ])
    cases.extend([
        case("[SubsPlease] Youjo Senki S2 - 08 (1080p).mkv", ("anilist", 135865, 8),
             page=[anime(21613, "Youjo Senki", 12), anime(135865, "Youjo Senki II", 12)], watch=True),
        case("Loki.S02E04.Heart.of.the.TVA.1080p.mkv", ("tmdb-tv", "Loki", "S02E04"),
             page=[anime(335, "Matantei Loki RAGNAROK", 26)], duration=2891,
             hits={"tmdb-tv": [dict(id=84958, name="Loki", kind="tv", genre="Drama",
                                    numbering="S02E04", episode_title="Heart of the TVA")]}),
        case("Arcane S02E04 Paint the Town Blue 1080p.mkv", ("tmdb-tv", "Arcane", "S02E04"),
             page=[anime(217391, "1999! Shenmi Xue Duice Bu", 12,
                         aliases=["1999! Arcane Incident Department"], duration=6, format="ONA")],
             duration=2346, hits={"tmdb-tv": [dict(id=94605, name="Arcane", kind="tv",
                                                  genre="Animation", numbering="S02E04")]}),
        case("Arcane S02E04 Paint the Town Blue 1080p.mkv", ("tvmaze", "Arcane: League of Legends", "S02E04"),
             page=[anime(217391, "1999! Shenmi Xue Duice Bu", 12,
                         aliases=["1999! Arcane Incident Department"], duration=6, format="ONA")],
             duration=2346, hits={"tvmaze": [dict(id=1, name="Arcane: League of Legends", kind="tv",
                                                 show_type="Animation", numbering="S02E04",
                                                 episode_title="Paint the Town Blue")]}),
        case("Screen Recording 2026-11-12.mp4", ("local", "", ""), duration=50,
             page=[anime(50, "Screen Recording", 1)]),
        case("Interstellar (2014) (2160p x265 10bit HDR Tigole).mkv", ("tmdb-movie", "Interstellar", ""),
             page=[anime(199, "Xingji Yi You", 12, aliases=["Interstellar Journey"], year=2022)],
             duration=10144, hits={"tmdb-movie": [dict(id=157336, name="Interstellar", year="2014",
                                                       kind="movie", genre="Adventure, Science Fiction")]}),
        case("Severance.S01E02.mkv", ("unavailable", "", ""), anime_offline=True),
        case("Daredevil.2003.Director.s.Cut.1080p.mkv", ("tmdb-movie", "Daredevil", ""),
             duration=7200, hits={"tmdb-movie": [dict(id=9480, name="Daredevil", year="2003", kind="movie")]}),
        case("Artist - Song.flac", ("musicbrainz", "Song", ""), video=False, duration=191,
             tags={"title": "Song", "artist": "Artist", "album": "Record"},
             hits={"musicbrainz": [dict(id="song", name="Song", artist="Artist", album="Record", duration_ms=191000)]}),
        case("Artist - Untagged.flac", ("acoustid", "Untagged", ""), video=False, duration=191,
             hits={"acoustid": [dict(id="track", name="Untagged", artist="Artist", score=0.99)]}),
    ])
    return cases


def sample_for(test, native, index):
    name = test["filename"]
    tags = test.get("tags", {})
    return dict(
        session=index, allowed=True, source=native["source"], filename=name, video=test["video"],
        path=name,
        duration=test["duration"], title=tags.get("title") or name,
        track_title=tags.get("title") or "", artist=tags.get("artist") or "",
        album=tags.get("album") or "", album_artist=tags.get("album_artist") or "",
        release_date=tags.get("date") or "", recording=tags.get("musicbrainz_trackid") or "",
        release=tags.get("musicbrainz_albumid") or "", tmdb=tags.get("tmdb") or "",
        anime_runtime_conflict=native["runtime_conflict"],
        **{field: native[field] for field in ("folder_title", "folder_year", "folder_has_episode", "folder_episode")
           if field in native},
    )


def run_worker(worker, test, native, index):
    sample = sample_for(test, native, index)
    worker.put("player", worker.window, sample)
    worker.delete("auto", worker.window)
    searches = []

    def candidates(provider, title, *_args, **_kwargs):
        searches.append((provider, title))
        return test["hits"].get(provider, [])

    def fingerprint(*_args, **_kwargs):
        return test["hits"].get("acoustid", [])

    def details(provider, hit, _settings, _throttle, episode, _date=None):
        if episode and hit.get("numbering") != "S%02dE%02d" % tuple(map(int, episode)):
            raise backend.AccountError("The requested episode is missing from the fixture")
        return dict(hit, provider=provider)

    with (
        patch("backend.candidates", side_effect=candidates),
        patch("backend.details", side_effect=details),
        patch("backend.musicbrainz_album", return_value=None),
        patch("backend.music_art", return_value=None),
        patch("backend.fingerprint", side_effect=fingerprint),
        patch("backend.os.path.isfile", return_value=bool(test["hits"].get("acoustid"))),
        patch("backend.os.stat", return_value=SimpleNamespace(st_size=1, st_mtime_ns=1)),
        patch.object(worker, "cached_fetch", side_effect=lambda key, field, request, valid: request()),
    ):
        worker.recognize(sample)
    record = worker.get("auto", worker.window)
    hit = record.get("hit", {})
    if hit:
        result = (hit["provider"], hit["name"], hit.get("numbering", ""))
    elif native["anime_id"] and not native["hide_without_provider"] and not (
        record.get("unavailable") and native["hide_on_provider_failure"]
    ):
        result = ("anilist", native["anime_id"], native["episode"])
    elif not native["confirmed"] or record.get("unavailable"):
        result = ("unavailable", "", "")
    else:
        result = ("local", "", "")
    return result, searches


@contextmanager
def replay_worker():
    worker = backend.Worker(":memory:", "replay", {})
    try:
        worker.db.execute("CREATE TABLE records(category TEXT, scope TEXT, value TEXT, PRIMARY KEY(category, scope))")
        yield worker
    finally:
        worker.db.close()


def run(executable, cases, output=None):
    payload = "".join(json.dumps(test, ensure_ascii=True) + "\n" for test in cases)
    process = subprocess.run([executable], input=payload, encoding="utf-8", check=True,
                             capture_output=True, timeout=120)
    responses = [json.loads(line) for line in process.stdout.splitlines()]
    if len(responses) != len(cases):
        raise RuntimeError(f"Native runner returned {len(responses)} of {len(cases)} cases")
    counts = Counter()
    results = []
    with replay_worker() as worker:
        for index, (test, native) in enumerate(zip(cases, responses), 1):
            expected = test["expected"]
            error = native.get("error")
            try:
                actual, searches = run_worker(worker, test, native, index) if not error else ((), [])
            except Exception as exc:
                actual, searches, error = (), [], str(exc)
            if error:
                verdict = "error"
            elif expected is None:
                verdict = "unverified"
            elif tuple(expected) != actual or ("watch" in test and test["watch"] != native["watch_episode"]):
                verdict = "fail"
            else:
                verdict = "pass"
            counts[verdict] += 1
            row = dict(file=test.get("path", test["filename"]), category=test.get("category", "Public fixture"),
                       verdict=verdict, expected=expected, actual=actual, evidence=test.get("evidence", ""),
                       watch=native.get("watch_episode"), review=native.get("review", ""),
                       route=native.get("source"), anime_queries=native.get("queries"), provider_queries=searches, error=error)
            results.append(row)
            if verdict in ("fail", "error") and output is None:
                print(json.dumps(row, ensure_ascii=True), file=sys.stderr)
    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("w", encoding="utf-8-sig", newline="") as file:
            writer = csv.DictWriter(file, fieldnames=results[0].keys())
            writer.writeheader()
            for row in results:
                writer.writerow({key: "'" + value if isinstance(value, str) and value.startswith(("=", "+", "-", "@"))
                                 else value for key, value in row.items()})
    print(f"{len(cases)} cases, " + ", ".join(f"{count} {label}" for label, count in sorted(counts.items())))
    return 1 if counts["error"] or (counts["fail"] and output is None) else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native_runner", type=Path)
    parser.add_argument("--cases", type=Path, help="Private JSONL cases with provider fixtures")
    parser.add_argument("--output", type=Path, help="Write per-case CSV and report mismatches without stopping")
    args = parser.parse_args()
    cases = fixture_cases() if args.cases is None else [
        json.loads(line) for line in args.cases.read_text(encoding="utf-8").splitlines()
    ]
    if not cases:
        parser.error("No cases to replay")
    return run(args.native_runner.resolve(), cases, args.output)


if __name__ == "__main__":
    sys.exit(main())
