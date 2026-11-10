# Implementation and verification

The implementation follows the in-player interface and tracking guide based on v1.0.10 (`a739587`). This page separates automated checks from checks that need an external account or desktop.

## Implemented areas

| Guide area | Implementation |
| --- | --- |
| Player panel | Seven sections, nested pages and confirmations, mouse and keyboard navigation, text entry handoff, resizing, private previews and scoped Apply/Cancel. |
| AniList account | Public authorization-code exchange, verified Viewer identity, OS credential storage or session-only login, manual progress drafts, conflict checks, reconnect and disconnect. Disconnect can separately forget the current account's local history. |
| Watch tracking | Monotonic elapsed time, independent sampling during network requests, shared ownership across processes, Ask/Automatic policies, gap review, skips, pending proposals and explicit rewatch cycles. |
| Recognition | Original Anitomy/AniList behavior, manual corrections, folder profiles, optional TMDB, TVmaze, MusicBrainz, Cover Art Archive, AcoustID and stream metadata. Requests are cached, deduplicated and checked against playback changes. |
| Appearance | Original SDK-call fixtures, optional presets, templates, language and numbering controls, per-media layouts, artwork, timers, buttons and supported links. |
| Local data | SQLite records by category and scope, individual match-field removal, account-scoped clears, cancellation of stale work and privacy protection after removing restrictions. |

## Repeatable checks

- `ctest --test-dir build -C Release --output-on-failure`: native regressions, account/tracking tests and Original SDK-call fixtures. Account tests cover two-process credit/write deduplication, stale responses, scoped deletion, disconnect races and sampling during a blocked account operation.
- `node --test tests/cloudflare.test.mjs`: encrypted ticket handoff, verifier checks, expiry, relay route restrictions and the registered public callback.
- `python -B tests/package_tests.py <installed-package> <mpv-executable>`: installed Lua modules, stable focus, unchanged-frame suppression, text handoff, resized hit targets, cleanup, Cancel, Apply and restart.
- `tests/panel_smoke.lua`: captures the seven panels from an isolated mpv install. Allow each page to reach the video output before taking its screenshot.
- `python -B tests/live_signin.py <installed-package> <mpv-executable>`: interactive, read-only public sign-in. It verifies Viewer, checks session-only storage, closes mpv and deletes the disposable config. It never edits the list.

The public sign-in check passed on Windows on 6 November 2026. Windows and Linux builds, regression suites and packaged menu checks have passed. Captures have been inspected over bright footage in wide and narrow windows. Tests use disposable data rather than the user's installed mpv configuration.

## Remaining release checks

- Check the visible Discord card from a second account on both Windows and Linux. Exact SDK-call parity is tested; Discord's current rendering, hover text and compact status still require that external check.
- Complete the manual visual matrix for dark footage, high DPI, fullscreen transitions, long multilingual titles and every nested control. Existing captures and layout tests cover part of this matrix.
- Exercise optional services with real TMDB and AcoustID credentials before distributing a configuration that enables them. Public sign-in success does not verify those providers.

No release or tag has been created for this work. Passing the automated checks does not mark these manual release checks complete.
