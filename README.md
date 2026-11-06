# mpv-rich-presence

Share what you are watching or listening to in mpv with Discord. The plugin runs on Windows and Linux. Anime filenames are parsed with Anitomy and matched against AniList. Music uses the artist and track title from mpv. No AniList account is needed for either feature.

Press **Alt+p** to open the player panel. The Original preset keeps the v1.0.10 Discord card. Changes in Appearance are private until you choose **Apply**. Account details, watch time and list progress stay in the player.

## In the player

| Section | What you can do |
| --- | --- |
| Now playing | Check the outgoing card, see why sharing is hidden, correct a match or claim Discord for this window. |
| AniList | Connect an account, refresh a list entry, adjust the progress draft or review an episode that qualified for an update. |
| Appearance | Preview Original, Clean, Detailed or Private. Change title language, numbering, artwork, buttons and timers. Choose a file, folder or global scope. |
| Recognition & folders | Search for a match, pin an AniList ID, adjust episode numbering or select another provider for a folder. |
| Privacy & windows | Hide a file, hide Discord for an hour, block a folder, pin a player or start a private session. |
| Saved data | Filter local records and confirm what to remove. Clearing local progress leaves your AniList list alone. |
| Diagnostics | Check the active provider, ownership, permission and most recent SDK or account result. |

Use Up and Down to move, Enter to select, Left and Right to change a choice, and Escape to go back. Click or scroll to navigate with a mouse. Search and ID entry use mpv's text input when available. You can also open a page from the mpv console, for example `script-message-to rich_presence_conf menu Appearance`.

### Sharing and privacy

The native plugin waits for the Lua configuration bridge before searching or publishing. A blank Discord application ID disables ordinary Discord sharing and its anime lookups. Explicitly enabling **Track while hidden** gives AniList separate permission. Folder exclusions and a private session take precedence. A new file or a resumed window can claim Discord unless a different player is pinned.

Matched anime can show episode numbers, release group, cover art and an AniList link. Other videos use mpv's title. Audio uses a Listening activity. Playback timestamps account for speed, and buffering suspends the display timer. Paused presence is hidden after 30 minutes by default.

### Connecting AniList

The optional account and provider worker requires Python 3.9 or newer. Set `python` to its executable if mpv cannot find it. The native plugin and player menu work independently of Python.

Open **Alt+p → AniList → Connect account** and approve access on AniList. The default client and sign-in service are included. You do not need to register an application, enter a client secret or create a hosting account. **Connect for this session** keeps the credential in memory until the account helper exits.

The public service uses a Cloudflare relay and a Netlify function for the authorization-code exchange. The application secret stays on Netlify. Once connected, mpv talks directly to AniList for account and list requests. See [the deployment notes](docs/anilist-exchange.md) for the service design, limits and owner setup.

If you want to use your own **personal application**, register a separate client in [AniList developer settings](https://anilist.co/settings/developer) with the redirect URL `http://127.0.0.1:47653/callback`. Set `anilist_personal_client=yes` and `anilist_client_id` to its public ID. Enter its secret only on the local callback page. Do not put a secret or access token in the config. Client `55822` uses the project's public callback and cannot also use a personal localhost callback.

Windows uses Credential Manager. Linux can use the Python keyring package with Secret Service. If credential storage is unavailable, sign-in lasts for the worker session. Disconnect removes the local account credential. Use AniList's connected app settings to revoke remote access.

### Watch time and list edits

Tracking starts off. Manual mode lets you edit an entry without waiting. Ask and Automatic use a fixed threshold, initially 15 minutes of real playback. Watching at 2× earns the same time per minute as watching at 1×. Pauses, seeks, buffering and long observation gaps earn no extra time. Two windows share one watch record for an account and episode.

The minus and plus controls edit a draft. Apply shows the account and proposed change. The worker re-reads the entry and sends an absolute progress value. Ordinary progress edits preserve score, notes, repeat count and status. Automatic changes require a suitable episode, a current list entry and the next consecutive progress value. Gaps and missing entries require review. A manual correction or skip suppresses a later automatic credit for that episode. Sampling runs independently of account and metadata requests, so a slow network response does not stop the eligible watch counter.

### Recognition and templates

AniList remains the default for video. Optional sources include TMDB movies and TV, TVmaze, MusicBrainz, Cover Art Archive, stream metadata and AcoustID. Select a source under **Recognition & folders**. TMDB needs a read access token. AcoustID needs a client key and `fpcalc` on PATH. Fingerprinting runs only when selected. Configure provider keys in your own mpv config and keep that file private.

Templates support `{title}`, `{artist}`, `{album}`, `{episode}`, `{original}`, `{group}`, `{channel}`, `{details}`, `{state}` and `{year}`. Separate optional pieces with a vertical bar, for example `{artist}|{title}`. Pieces with missing fields are skipped. Appearance settings inherit from global to folder to file. User choices live in `rich-presence/data.sqlite3` under the mpv config directory.

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
| ![Music Playing](screenshots/music%201.png) | ![Discord Sidebar Music](screenshots/music%202.png) |

### General Video Playback

| Active Playback | Paused State | Discord Status Indicator |
| :---: | :---: | :---: |
| ![Video Playing](screenshots/non-anime%20video%202.png) | ![Video Paused](screenshots/non-anime%20video%203.png) | ![Discord Sidebar Video](screenshots/non-anime%20video%201.png) |

## Installation

1. Download the latest release package from the [Releases](https://github.com/Sirye8/mpv-rich-presence/releases) page.
2. Copy the files into your mpv configuration directory (`%APPDATA%/mpv` on Windows, or `~/.config/mpv` on Linux):
   - Place `rich-presence.dll` (or `rich-presence.so` on Linux) into your mpv `scripts/` folder.
   - Place `rich-presence-conf.lua` into your mpv `scripts/` folder.
   - Copy the entire `script-modules/mpv-rich-presence/` directory into the corresponding `script-modules/` directory in your config.
   - On a new installation, place `rich-presence.conf` in `script-opts/`. On an upgrade, keep your existing config.
3. Open `script-opts/rich-presence.conf` in a text editor to configure your Discord Application ID and optional settings.

## Configuration

Edit `script-opts/rich-presence.conf`:

```ini
# Discord Application ID (from https://discord.com/developers/applications)
application_id=

# Enable rich presence by default (yes/no)
on=yes

# Folder filtering mode: "disabled", "whitelist", or "blacklist"
folder_filter_mode=disabled

# Semicolon-separated list of folder paths (quotes are optional)
# Example: D:\Anime;E:\Videos\Movies
folder_list=

# Pause timeout in seconds before presence is hidden (set to 0 to never hide)
pause_timeout=1800
```

### Keybindings & Commands

You can bind a key in your `input.conf` to toggle rich presence on and off on demand:

```ini
D script-message-to rich_presence toggle
```

Available script messages:
- `script-message-to rich_presence toggle`: Toggle presence on or off.
- `script-message-to rich_presence on`: Explicitly enable presence.
- `script-message-to rich_presence off`: Explicitly disable presence.
- `script-message-to rich_presence application_id <id>`: Set Discord application ID.
- `script-message-to rich_presence folder_filter_mode <disabled|whitelist|blacklist>`: Switch filter mode.
- `script-message-to rich_presence folder_list <path1;path2>`: Update filtered directories.
- `script-message-to rich_presence pause_timeout <seconds>`: Change pause timeout duration.

## Building from Source

### Prerequisites

- CMake 3.21 or newer for the configure preset
- A C++20 compiler and standard library with `std::format` support
- vcpkg
- [Discord Social SDK](https://discord.com/developers/social-sdk)

### Build Instructions

1. Clone the repository recursively:
   ```bash
   git clone --recursive https://github.com/Sirye8/mpv-rich-presence.git
   cd mpv-rich-presence
   ```

2. Extract the Discord Social SDK into `thirdparty/discord_social_sdk` such that `thirdparty/discord_social_sdk/bin` and `lib` exist.

3. Bootstrap vcpkg dependencies:
   ```batch
   .\thirdparty\vcpkg\bootstrap-vcpkg.bat
   ```
   *(or `./thirdparty/vcpkg/bootstrap-vcpkg.sh` on Linux)*

4. Configure with CMake:
   ```bash
   cmake --preset default
   # Windows x64 package builds: add -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
   ```

5. Build the Release configuration:
   ```bash
   cmake --build build --config Release
   ```

6. Run the regression test suite:
   ```bash
   ctest --test-dir build -C Release --output-on-failure
   ```

To create a local Windows archive from a clean install tree, run `python tools/package.py dist/mpv-rich-presence-windows-x64.zip`. This rebuilds Release, runs CTest and packages the installed DLL, scripts, documentation and licenses. It does not publish to GitHub.

Compiled binaries will be generated in `build/Release/rich-presence.dll` (or `build/rich-presence.so`).

## License

This project is licensed under the GNU Affero General Public License v3.0 (AGPL-3.0). See [LICENSE](LICENSE) and [NOTICE](NOTICE) for full terms.

Based on [mpv-rich-presence](https://github.com/goodtrailer/mpv-rich-presence) by Alden Wu.

### Third-Party Notices
- **Anitomy**: Licensed under Mozilla Public License 2.0 (MPL-2.0) by Eren Okka. See [licenses/LICENSE-Anitomy](licenses/LICENSE-Anitomy).
- **Discord Social SDK**: The embedded runtime binary is proprietary software of Discord Inc. and governed by the [Discord Developer Terms of Service](https://discord.com/developers/docs/legal).
- **cpr**: Licensed under the MIT License. See [licenses/LICENSE-cpr](licenses/LICENSE-cpr).
- **curl**: Governed by the curl license (MIT/ISC-like). See [licenses/LICENSE-curl](licenses/LICENSE-curl).
- **nlohmann-json**: Licensed under the MIT License. See [licenses/LICENSE-nlohmann-json](licenses/LICENSE-nlohmann-json).
- **zlib**: Licensed under the zlib License. See [licenses/LICENSE-zlib](licenses/LICENSE-zlib).
- **mpv client API header**: Licensed under the ISC License by the mpv developers. See [licenses/LICENSE-mpv](licenses/LICENSE-mpv).
- **SQLite**: Public domain. See [its notice](licenses/LICENSE-SQLite).
- **Boost** and **CMakeRC**: See individual licenses in the [licenses/](licenses/) directory.
