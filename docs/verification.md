# Implementation and verification

The implementation follows the in-player interface and tracking guide based on [`v1.0.10`](https://github.com/Sirye8/mpv-rich-presence/tree/v1.0.10). This page separates automated checks from checks that need an external account or desktop.

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
- `python -B tests/package_tests.py <installed-package> <mpv-executable>`: installed Lua modules, link actions, long row scrolling, full tooltip text, stable focus, animation cleanup, text handoff, panel positions, OSC margins, Cancel, Apply and restart.
- `tests/panel_smoke.lua`: captures the seven panels from an isolated mpv install. Allow each page to reach the video output before taking its screenshot.
- `python -B tests/live_signin.py <installed-package> <mpv-executable>`: interactive, read-only public sign-in. It verifies Viewer, checks session-only storage, closes mpv and deletes the disposable config. It never edits the list.
- `python -B tests/live_anime_match.py <package-or-install> <mpv-executable> <ffmpeg-executable>`: verify that the season-two filename resolves to Youjo Senki II episode 8 and an AniList outage keeps movie and TV fallback disabled. The test generates its own footage and disables Discord sharing.

The public sign-in check passed on Windows on 6 November 2026. Windows and Linux builds, regression suites and packaged menu checks have passed. Captures have been inspected over bright footage in wide and narrow windows. Tests use disposable data rather than the user's installed mpv configuration.

On 11 November 2026, the extracted Windows ZIP matched Interstellar and Severance S01E02 through the shared TMDB service, Sousou no Frieren through AniList, and a tagged Bohemian Rhapsody fixture through MusicBrainz. The test used the bundled Python with the system interpreter off PATH and Discord sharing disabled. A 720p capture showed the compact panel and its hover help above the visible OSC.

## Remaining release checks

- Check the visible Discord card from a second account on both Windows and Linux. Exact SDK-call parity is tested; Discord's current rendering, hover text and compact status still require that external check.
- Complete the manual visual matrix for dark footage, high DPI, fullscreen transitions, long multilingual titles and every nested control. Existing captures and layout tests cover part of this matrix.
- Check album selection for untagged recordings with multiple AcoustID matches on Linux and Windows.

No release or tag has been created for this work. Passing the automated checks does not mark these manual release checks complete.
