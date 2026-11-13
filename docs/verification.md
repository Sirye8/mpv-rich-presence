# Implementation and verification

The in-player interface and tracking build on [`v1.0.10`](https://github.com/Sirye8/mpv-rich-presence/tree/v1.0.10). Use these checks when testing a build or preparing a release.

## Implemented areas

| Guide area | Implementation |
| --- | --- |
| Player panel | Seven sections, nested pages and confirmations, mouse and keyboard navigation, text entry handoff, resizing, private previews and scoped Apply/Cancel. |
| AniList account | Public authorization-code exchange, verified Viewer identity, OS credential storage or session-only login, manual progress drafts, conflict checks, reconnect and disconnect. Disconnect can separately forget the current account's local history. |
| Watch tracking | Monotonic elapsed time, independent sampling during network requests, shared ownership across processes, Ask/Automatic policies, gap review, skips, pending proposals and explicit rewatch cycles. |
| Recognition | Automatic AniList anime matching, Anime, Movies & TV, Movies and TV folder rules, shared TMDB lookups, Wikidata and TVmaze fallback, tagged music and album artwork, manual corrections, and optional AcoustID. Requests are cached, deduplicated and checked against playback changes. |
| Appearance | Original SDK-call fixtures, optional presets, templates, language and numbering controls, per-media layouts, artwork, timers, buttons and supported links. |
| Local data | SQLite records by category and scope, individual match-field removal, account-scoped clears, cancellation of stale work and privacy protection after removing restrictions. |

## Repeatable checks

- `ctest --test-dir build -C Release --output-on-failure`: native regressions, account/tracking tests and Original SDK-call fixtures. Account tests cover two-process credit/write deduplication, stale responses, scoped deletion, disconnect races and sampling during a blocked account operation.
- `node --test tests/cloudflare.test.mjs`: encrypted ticket handoff, verifier checks, expiry, the registered public callback, and credential isolation and route restrictions for the TMDB relay.
- `python -B tests/package_tests.py <extracted-package> <mpv-executable>` checks the installed Lua modules, including panel navigation, tooltips, animations, OSC margins and settings persistence. Check that the binary ZIP includes the shipped config and license files. The documentation and screenshots live in the repository.
- `tests/panel_smoke.lua`: captures the seven panels from an isolated mpv install. Allow each page to reach the video output before taking its screenshot.
- `python -B tests/live_signin.py <installed-package> <mpv-executable>`: interactive, read-only public sign-in. It verifies Viewer, checks session-only storage, closes mpv and deletes the disposable config. It never edits the list.
- `python -B tests/live_anime_match.py <package-or-install> <mpv-executable> <ffmpeg-executable>`: verify that the season-two filename resolves to Youjo Senki II episode 8 and an AniList outage keeps movie and TV fallback disabled. The test generates its own footage and disables Discord sharing.
- `python -B tests/live_ask_prompt.py <package.zip> <mpv-executable> <ffmpeg-executable>`: exercise timed and untimed reviews, quit, window close, Next, and natural playlist advance with a saved local proposal. Add `--capture <file.jpg>` to check the graphical EOF prompt and capture it in mpv.
- Add `--uosc <uosc-script-directory>` to the Ask check to exercise Next and Previous through a local uosc install, including review at natural EOF after returning to an episode.

### Automatic recognition replay

The `automatic-recognition` CTest uses the same native anime resolver, provider routing, and Python matcher as mpv. Provider responses are controlled fixtures. It covers the Black Clover 170/171 boundary, explicit and absolute episode numbers, named cours, fractional and multi-episode files, alternate TV titles, movies, music, and offline fallback.

To replay a private inventory, prepare cases from an audit CSV and its provider journals, then pass them to the runner. The optional catalog JSON maps exact AniList search strings to their saved media rows. Keep inventory and result files under the ignored build directory.

```powershell
python -B tests/make_recognition_inventory.py build/recognition-audit/file-results.csv build/library-audit-v2 build/recognition-audit/inventory-suite.jsonl
python -B tests/recognition_suite.py build/Release/recognition-replay.exe --cases build/recognition-audit/inventory-suite.jsonl --output build/recognition-audit/inventory-results.csv
```

The CSV includes the expected result, evidence, actual match, and provider queries for each row. Rows without a reference result are marked unverified. Use the packaged live checks to verify provider availability and what mpv publishes.

The public sign-in check passed on Windows on 6 November 2026. Windows and Linux builds, regression suites and packaged menu checks have passed. Captures have been inspected over bright footage in wide and narrow windows. Tests use disposable data rather than the user's installed mpv configuration.

On 11 November 2026, the extracted Windows ZIP matched Interstellar and Severance S01E02 through the shared TMDB service, Sousou no Frieren through AniList, and a tagged Bohemian Rhapsody fixture through MusicBrainz. The test used the bundled Python with the system interpreter off PATH and Discord sharing disabled. A 720p capture showed the compact panel and its hover help above the visible OSC.

## Remaining release checks

- Check the visible Discord card from a second account on both Windows and Linux. Exact SDK-call parity is tested; Discord's current rendering, hover text and compact status still require that external check.
- Complete the manual visual matrix for dark footage, high DPI, fullscreen transitions, long multilingual titles and every nested control. Existing captures and layout tests cover part of this matrix.
- Check album selection for untagged recordings with multiple AcoustID matches on Linux and Windows.
