# User guide

- [Install or upgrade](#install-or-upgrade)
- [Use the panel](#use-the-panel)
- [Recognize media](#recognize-media)
- [Connect AniList](#connect-anilist)
- [Track episodes and rewatches](#track-episodes-and-rewatches)
- [Review Ask updates](#review-ask-updates)
- [Manage privacy and saved data](#manage-privacy-and-saved-data)
- [Configure startup defaults](#configure-startup-defaults)

## Install or upgrade

Copy the contents of the package into your mpv configuration directory. On Windows this is usually `%APPDATA%\mpv`. On Linux it is usually `~/.config/mpv`. A portable Windows install can use a `portable_config` folder next to mpv. If you start mpv with `--config-dir`, use that directory.

```text
mpv/
  scripts/
    rich-presence.dll       (rich-presence.so on Linux)
    mpv-rich-presence/
  script-opts/
    rich-presence.conf
```

Close all mpv windows before replacing these files. Keep your existing `rich-presence.conf` and `scripts/mpv-rich-presence/data.sqlite3` to keep your preferences and history. If upgrading from a version with `scripts/rich-presence-conf.lua`, remove that old script so only the bundled bridge loads. The plugin can migrate the old `rich-presence/data.sqlite3` the first time it opens the new database.

The Windows x64 ZIP includes portable Python and Chromaprint. The Linux ZIP includes Chromaprint and uses Python 3.9 or newer from your system. A source install can use `fpcalc` on PATH. The packaged Discord application ID, metadata services and public AniList sign-in are ready to use.

## Use the panel

Press **Alt+p**. Use Up and Down to move, Enter to select, Left and Right to change a choice, and Escape to go back. Hover over a row for help and the full value. Long values scroll on hover. Use the wheel over a tooltip if its text exceeds the window. Click a row marked ↗ or press Enter to open its link.

| Page | Use it to |
| --- | --- |
| Now playing | Inspect the current card, correct a match or choose which window shares to Discord. |
| AniList | Connect, see watch time, edit progress, change tracking settings or review a proposal. |
| Appearance | Preview a preset or edit artwork, language, numbering, timers, links and card layouts. **Apply** saves the preview. |
| Recognition & folders | Choose a provider scope, pin an AniList match, adjust episode numbering or fingerprint a local track. |
| Privacy & windows | Hide a card or folder, choose a publishing window or start a private session. |
| Saved data | Find saved profiles, matches, caches, timers and proposals. Review what a clear operation removes. |
| Diagnostics | See permission, active provider, rule source, tracking and Discord publishing status. |

**Appearance → Panel position** offers six anchors. **Config** uses `panel_position` from the conf file. Top anchors reserve room for the OSC title strip. Set `menu_scale` to resize the panel or `menu_animations=no` for instant transitions.

To open a page from the mpv console, use `script-message-to mpv_rich_presence menu Appearance`.

## Recognize media

**Automatic** uses the v1.0.10 AniList resolver for video, including its season and sequel rules. It tries movie or TV lookup on a completed anime miss. An exact movie title and year from TMDB can disambiguate a feature-length file that loosely matches a much shorter anime from a different year. If AniList is unavailable, the card uses the local title.

Movie and TV lookup prefers TMDB. Wikidata and TVmaze provide fallbacks. Air-date episodes use TVmaze. For music, the card starts with local tags. MusicBrainz checks title, credits, duration and release edition, and Cover Art Archive supplies public artwork when available. An embedded cover stays in mpv. If the music tags are incomplete or inconclusive, the plugin can send a Chromaprint fingerprint and the track duration to AcoustID. The audio file stays on your device.

To classify a directory, open a file inside it and choose **Recognition & folders → Save for → folder**. Then choose **Media recognition**. The rule also covers its subfolders.

| Choice | Lookup |
| --- | --- |
| Automatic | AniList first for video, with film and TV fallback. Music services for audio. |
| Anime | AniList, with the local title on a miss. |
| Movies & TV | TV for filenames with episode numbers or air dates. Otherwise movies first, then TV. |
| Movies | TMDB movies with Wikidata fallback. |
| TV | TMDB TV with TVmaze fallback. |
| Original | AniList for anime and local titles or tags for other media. |

The effective order is file, nearest matching folder, global panel setting, then `recognition_source` in the conf file. Choose **Inherit recognition source** to remove a rule. Saved match corrections still apply. **Fingerprint this file** runs an explicit AcoustID lookup on local audio.

You can override the bundled provider access with `tmdb_key` or `acoustid_key`. See [shared TMDB setup](tmdb-metadata.md) for deployment details.

## Connect AniList

Open **Alt+p → AniList → Connect account**, then approve in your browser. The bundled public sign-in needs only your AniList account. Use **Connect for this session** to keep the credential in memory until the account helper exits.

Windows uses Credential Manager for a saved sign-in. Linux can use Python keyring with Secret Service. If credential storage is unavailable, sign-in lasts for the worker session. **Disconnect** removes the local credential. Use AniList's connected app settings to revoke access remotely.

The project uses a Cloudflare relay and a Netlify function for the authorization-code exchange. After sign-in, mpv contacts AniList directly for account and list requests. See [public sign-in deployment](anilist-exchange.md).

To use a personal app, register a separate client in [AniList developer settings](https://anilist.co/settings/developer) with the redirect `http://127.0.0.1:47653/callback`. Set `anilist_personal_client=yes` and `anilist_client_id` to its public ID. Enter the secret on the local callback page. Keep secrets and access tokens out of the conf file. The bundled client ID uses the public callback.

## Track episodes and rewatches

| Tracking mode | Behavior |
| --- | --- |
| Off | Stop counting. Previously saved time stays available. |
| Manual | Count real playback time. Edit progress yourself in the panel. |
| Ask | Count time and open a review when a ready update reaches the configured timing. |
| Automatic | Advance consecutive episodes at the threshold. Episode gaps, new entries and required status changes go to review. |

The default threshold is 15 minutes of real playback. Pauses, buffering, seeks and long observation gaps add no time. A minute at 2× counts as one minute. The panel shows time spent in the current viewing cycle and the local times playback started and was last credited. Two windows share the same account and episode record. **Resume unfinished time** continues from a saved counter when you reopen the episode. **Keep offline proposals** remembers eligible updates locally so you can review them after reconnecting.

Use the minus and plus buttons around **Draft**, or Left and Right on that row, to adjust the proposed episode count. **Apply progress** opens a confirmation with the account, title and proposed change. The worker rechecks the entry before saving. Ordinary progress edits preserve status, score, notes and repeat count. **Status on Apply** lets you choose a status for a manual edit.

Watch tracking uses the matched AniList entry and its resolved season-relative episode number. It needs one identifiable episode. For an entry outside Watching or Rewatching, the first eligible update offers to switch it to Watching or retain the current status. If you keep the status, Automatic mode can advance consecutive episodes until the status changes. A new list entry needs approval.

An entry already marked Rewatching gets its own viewing cycle. Turn on **Rewatch tracking** to offer a new cycle after watching episode 1 of a completed anime. Starting the cycle needs approval because it restarts AniList progress. Reaching the known final episode completes the rewatch and adds one to the repeat count. An unknown total can continue in Rewatching. Use **Start new viewing cycle** to reset local counters and pending updates for the matched entry.

## Review Ask updates

| Ask timing | When it opens |
| --- | --- |
| At threshold | As soon as a proposal is ready. Playback continues. |
| End of episode | At natural playback completion, before the playlist advances. |
| Before leaving | At natural completion or when a standard quit or playlist binding would leave the episode. |

With **Before leaving**, the player stays responsive while a quit or playlist action waits for your decision. At natural completion, the last frame stays visible so mpv can still draw overlays and process input. After your choice, mpv resumes the requested action. **Later**, **×**, Escape, and a timed dismissal leave the proposal in **Pending updates**. **Skip this episode** clears the proposal for the current cycle.

**Ask prompt position** selects one of six anchors. **Ask prompt timeout** defaults to 30 seconds. Choose a preset, enter a custom 5 to 300 seconds, or select **Never** to wait for a choice.

![Ask prompt timeout menu showing Never, presets and a custom timeout](../screenshots/panel-timeout.jpg)

Direct commands from an OSC, another script or IPC can leave the file immediately. In that case the ready proposal stays in **Pending updates**. For a custom Next or Quit control, send the command through the review binding to make it wait in **Before leaving** mode. For example, in `input.conf`:

```ini
Ctrl+n script-message-to mpv_rich_presence review-before "playlist-next weak"
Ctrl+q script-message-to mpv_rich_presence review-before "quit-watch-later"
```

For uosc, replace the `next` item in `controls` in `script-opts/uosc.conf` with `command:arrow_forward_ios:script-message-to mpv_rich_presence review-before "script-binding uosc/next"?Next` to keep uosc's directory and playlist navigation.

## Manage privacy and saved data

**Privacy & windows** can hide only the card, switch to a generic card, or start a private session that stops both lookups and tracking. Folder exclusions and private sessions take precedence. **Track while hidden** lets AniList continue when Discord sharing is off and the file is otherwise eligible. A new or resumed file can claim Discord unless another player is pinned.

The database lives in `scripts/mpv-rich-presence/data.sqlite3` under your config directory. **Clear watch** resets the affected counters and removes their pending proposals and skips. If you keep watching the same episode, it can qualify again once it earns the threshold. Completed local history and saved AniList progress are kept. **Reset everything** clears the local profiles, matches, caches and history, disconnects the account and restores the conf defaults. It leaves your AniList list as it is.

Close all mpv instances before removing the plugin folder. The current account helper exits with the player, including when the player is forcibly stopped. If an older installation left a Python process behind, close the helper whose command line points to this plugin's `modules/backend.py`, then remove the folder.

## Configure startup defaults

Use `script-opts/rich-presence.conf` for startup defaults and restart mpv after editing. Save file, folder and global overrides in the panel.

| Option | Default and values |
| --- | --- |
| `application_id` | `1462038267183628308`. Override with your Discord application ID if needed. |
| `on` | `yes`. Set to `no` to start with Discord sharing off. |
| `folder_filter_mode` | `disabled`, `whitelist` or `blacklist`. |
| `folder_list` | Absolute paths separated by semicolons. Used with the allow or block list. |
| `recognition_source` | `auto`, `anime`, `movie-tv`, `movie`, `tv` or `original`. |
| `pause_timeout` | `1800` seconds. Use `0` to keep the card visible on pause. |
| `appearance_preset` | `original`, `clean`, `detailed` or `private`. |
| `menu_key` | `Alt+p`. Use an mpv key name. |
| `menu_scale` and `menu_animations` | `1` and `yes`. |
| `panel_position` | `top-left`. Choose `top` or `bottom` with `left`, `center` or `right`. |
| `python` | Empty uses the bundled Windows runtime or system Python. Set an executable path to override it. |

Appearance templates support `{title}`, `{artist}`, `{album}`, `{episode}`, `{original}`, `{group}`, `{channel}`, `{details}`, `{state}` and `{year}`. Separate optional pieces with a vertical bar, for example `{artist}|{title}`. A piece is skipped when its fields are missing.

To toggle the card with a key, add a line such as `D script-message-to rich_presence toggle` to `input.conf`. The same target also accepts `on`, `off`, `application_id <id>`, `folder_filter_mode <mode>`, `folder_list <paths>` and `pause_timeout <seconds>`.
