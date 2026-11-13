# mpv-rich-presence

Share what you watch or listen to in mpv on Discord. The plugin recognizes anime, movies, TV and music on Windows and Linux. It can also track your anime progress on AniList.

[User guide](docs/user-guide.md) · [Installation](#installation) · [Screenshots](#screenshots) · [Build from source](#building-from-source)

## In the player

Press **Alt+p** to open the panel. Use the mouse or the arrow keys, Enter and Escape. Hover over a setting for its options and over a long value to read the full text.

| Section | What you can do |
| --- | --- |
| Now playing | Preview the Discord card, correct a match or choose which window shares. |
| AniList | Connect an account, see watch time and review or edit progress. |
| Appearance | Preview presets and edit card text, artwork, links and timers. |
| Recognition & folders | Pick a provider, pin a match or adjust episode numbering. |
| Privacy & windows | Hide a file or folder, start a private session or pin a window. |
| Saved data | Find and clear local matches, caches and watch records. |
| Diagnostics | Check lookup permissions, the active provider and publishing status. |

Use **Appearance → Panel position** to move the panel. **Apply** saves card edits. See the [panel controls](docs/user-guide.md#use-the-panel) for keyboard shortcuts and appearance settings.

### Sharing and privacy

- Anime cards can show the episode, release group, cover art and AniList link. Movie and TV cards use the matched title and available artwork. Music uses a Listening activity.
- Paused presence hides after 30 minutes by default. Folder exclusions and private sessions also block metadata lookups and tracking.
- **Track while hidden** can keep AniList tracking active while Discord sharing is off. See [privacy and saved data](docs/user-guide.md#manage-privacy-and-saved-data).

### Connecting AniList

Open **Alt+p → AniList → Connect account** and approve access in your browser. The default sign-in is included. Choose **Connect for this session** if you want the credential cleared when the helper exits.

For personal clients and credential storage, see [Connect AniList](docs/user-guide.md#connect-anilist). The hosted exchange is described in the [deployment notes](docs/anilist-exchange.md).

### Watch time and list edits

- Tracking starts **Off**. **Manual** counts real playback time. **Ask** offers a review. **Automatic** advances confirmed consecutive episodes and asks before leaving when it needs a decision.
- In **Ask**, choose **At threshold**, **End of episode** or **Before leaving**. Before leaving and Auto reviews default to the center. Other Ask prompts default to the upper right. Select **Never** to keep the prompt open until you make a choice.
- Use **−** and **+** around the progress draft, then choose **Apply progress** to review the change. **Later** keeps a proposed update in **Pending updates**.
- Enable **Rewatch tracking** to get a prompt when starting a completed anime again. See the [tracking guide](docs/user-guide.md#track-episodes-and-rewatches) for timing, gaps and rewatch rules.

### Recognition and templates

To classify a folder and its subfolders, open a file inside it and choose **Recognition & folders → Save for → folder**, then set **Media recognition**.

| Choice | What gets checked |
| --- | --- |
| **Automatic** | AniList first for video, with movie and TV lookup after a completed miss. Music services for audio. |
| **Anime** | AniList, with the local title on a miss. |
| **Movies & TV** | TV for episode numbers or air dates. Otherwise, movies first, then TV. |
| **Movies** | TMDB movies, with Wikidata as a fallback. |
| **TV** | TMDB TV, with TVmaze as a fallback and for air-date episodes. |
| **Original** | AniList for anime and local titles or tags for other media. |
| **Local only** | Use mpv's local title or tags. Saved manual matches can still apply. |

- **Automatic**, **Anime** and **Original** use the v1.0.10 anime resolver. A saved file rule takes priority over the nearest folder rule, global panel settings and the conf default.
- Music starts with the file's tags. MusicBrainz and Cover Art Archive can fill in metadata and artwork. The Windows and Linux packages include `fpcalc` for AcoustID fingerprinting.
- Use a template such as `{artist}|{title}` to include optional fields. The [recognition and configuration guide](docs/user-guide.md#recognize-media) covers provider overrides, and [startup defaults](docs/user-guide.md#configure-startup-defaults) lists the template fields.

## Screenshots

### Anime Detection & Cover Art (AniList)

| Active Playback | Paused State |
| :---: | :---: |
| ![Anime Playing](screenshots/anime%203.png) | ![Anime Paused](screenshots/anime%201.png) |

| Pause Tooltip | Relation Chain & Multi-Season |
| :---: | :---: |
| ![Anime Pause Tooltip](screenshots/anime%202.png) | ![Anime Series](screenshots/anime%204.png) |

| Long Title & Airing Sequences | Discord Status Indicator |
| :---: | :---: |
| ![Bleach TYBW](screenshots/anime%206.png) | ![Discord Sidebar Anime](screenshots/anime%205.png) |

### Music & Audio Playback

| Listening Activity | Discord Status Indicator |
| :---: | :---: |
| ![Discord card for Bring Me to Life by Evanescence](screenshots/music%201.png) | ![Bring Me to Life in the Discord sidebar](screenshots/music%202.png) |

### Movies & TV

| Movie · Interstellar | TV · Loki, season 2 episode 4 |
| :---: | :---: |
| ![Interstellar Discord card with artwork and release year](screenshots/movie-card.png) | ![Loki Discord card with artwork and episode number](screenshots/tv-card.png) |
| ![Interstellar in the Discord sidebar](screenshots/movie-status.png) | ![Loki in the Discord sidebar](screenshots/tv-status.png) |

### General Video Playback

| Active Playback | Paused State | Discord Status Indicator |
| :---: | :---: | :---: |
| ![Video Playing](screenshots/non-anime%20video%202.png) | ![Video Paused](screenshots/non-anime%20video%203.png) | ![Discord Sidebar Video](screenshots/non-anime%20video%201.png) |

### In-Player Panel

| Now Playing | AniList Progress |
| :---: | :---: |
| ![Now playing panel](screenshots/panel-now-playing.jpg) | ![AniList Episodes watched control](screenshots/panel-anilist.jpg) |

| Ask Prompt | Timeout Options |
| :---: | :---: |
| ![AniList review before playlist advance](screenshots/panel-ask.jpg) | ![Ask prompt timeout options](screenshots/panel-timeout.jpg) |

## Installation

1. Download a compatible package from [Releases](https://github.com/Sirye8/mpv-rich-presence/releases), or [build from source](#building-from-source) for the features shown here. Close mpv before copying files.
2. Copy the package's `scripts` folder into your mpv config directory. The usual paths are `%APPDATA%/mpv` on Windows and `~/.config/mpv` on Linux.
3. On a new install, copy `script-opts` as well. On upgrade, keep your existing `rich-presence.conf` and `data.sqlite3`. If present, remove the old `scripts/rich-presence-conf.lua` before restarting.
4. Open a file with the Discord desktop app running. Press **Alt+p** to open the panel in builds that include it. See [install or upgrade](docs/user-guide.md#install-or-upgrade) for portable installs, Python requirements and database migration.

## Configuration

The bundled config is ready to use. Use the panel for file and folder overrides. For startup defaults, edit `script-opts/rich-presence.conf` in your mpv config directory and restart mpv. See the [full settings reference](docs/user-guide.md#configure-startup-defaults) for every option and its default.

- Use [Recognition & folders](docs/user-guide.md#recognize-media) for conditional TV rules and local metadata files. Both are off by default.
- [Connect AniList](docs/user-guide.md#connect-anilist) in your browser and confirm the displayed account before mpv saves it.

### Keybindings & Commands

Add this to `input.conf` to toggle Discord presence with **D**.

```ini
D script-message-to rich_presence toggle
```

The `rich_presence` script also accepts `on`, `off`, `application_id`, `folder_filter_mode`, `folder_list` and `pause_timeout`. See the [user guide](docs/user-guide.md#configure-startup-defaults) for arguments.

## Building from Source

### Prerequisites

- CMake 3.21 or newer with the CMake CLI on PATH
- A C++20 compiler with `std::format` support
- vcpkg, Python 3.9 or newer for packaging and account tests, and the [Discord Social SDK](https://discord.com/developers/social-sdk)

### Build Instructions

Clone recursively, extract the SDK, and bootstrap vcpkg as shown in the [build guide](docs/build.md).

```sh
cmake --preset default -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For the Windows x64 ZIP, configure with `-DVCPKG_TARGET_TRIPLET=x64-windows-static-md`, then run `python tools/package.py dist/mpv-rich-presence-windows-x64.zip`. The [build guide](docs/build.md) covers Linux packages. See [verification](docs/verification.md) for additional tests.

## License

Licensed under the GNU Affero General Public License v3.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE). Based on [mpv-rich-presence](https://github.com/goodtrailer/mpv-rich-presence) by Alden Wu.

### Third-Party Notices

- [Anitomy](licenses/LICENSE-Anitomy) uses MPL-2.0.
- [cpr](licenses/LICENSE-cpr), [nlohmann-json](licenses/LICENSE-nlohmann-json) and [zlib](licenses/LICENSE-zlib) include their own license notices.
- [curl](licenses/LICENSE-curl), the [mpv client API header](licenses/LICENSE-mpv), [SQLite](licenses/LICENSE-SQLite), [Boost](licenses/LICENSE-Boost) and [CMakeRC](licenses/LICENSE-CMakeRC) have separate notices.
- Discord's proprietary SDK is governed by the [Discord Developer Terms of Service](https://discord.com/developers/docs/legal).
- See [provider credits](docs/tmdb-metadata.md#credits) for TMDB attribution and [NOTICE](NOTICE) for the packaged Python and Chromaprint components.
