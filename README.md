# mpv-rich-presence

Windows/Linux mpv plugin for Discord Rich Presence using the Discord Social SDK. Supports media details, automatic anime metadata via AniList, folder privacy filtering, and multi-instance coordination.

## Features

- **Media Presence**: Displays currently playing video or audio with title, artist, and playback progress. Audio files dynamically show as "Listening to <Artist> - <Title>" on Discord.
- **Anime Detection via AniList**:
  - Automatically parses anime filenames using Anitomy to extract title, season (e.g. `S4`, `4th Season`, `S01E13`), part/cour, episode, and release group.
  - Queries the AniList GraphQL API to fetch official cover art, Romaji/English titles, total episode count, and adds a clickable "View on AniList" button.
  - Sequentially walks anime relation chains for continuous episode numbering across multi-cour and sequel seasons.
- **Accurate Playback & Pause States**:
  - Shows an active progress bar while playing and an elapsed timer with timestamp details when paused.
  - Google Material Design play and pause status icons.
  - Instant response to pause, play, and seek events with rate-limit protection against Discord RPC throttling.
  - Configurable pause timeout to hide status after being paused for a specified duration.
- **Privacy & Folder Filtering**:
  - Whitelist or blacklist specific directories so private or sensitive media is never broadcast to Discord.
- **Multi-Instance Coordination**:
  - Coordinates active status between multiple mpv windows so only the active player reports to Discord.

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

1. Download the latest release from the [Releases](https://github.com/Sirye8/mpv-rich-presence/releases) page.
2. Copy the files into your mpv configuration directory (`%APPDATA%/mpv` on Windows, `~/.config/mpv` on Linux):
   - Place `rich-presence.dll` (or `rich-presence.so`) in your mpv `scripts/` folder.
   - Place `rich-presence-conf.lua` in your mpv `scripts/` folder.
   - Place `rich-presence.conf` in your mpv `script-opts/` folder.
3. Configure your settings in `script-opts/rich-presence.conf`.

## Configuration

Edit `script-opts/rich-presence.conf`:

```ini
# Discord Application ID (from https://discord.com/developers/applications)
application_id=

# Enable rich presence by default (yes/no)
on=yes

# Folder filtering mode: "disabled", "whitelist", or "blacklist"
folder_filter_mode=disabled

# Semicolon-separated list of folder paths (quotes optional)
# Example: D:\Anime;E:\Videos\Movies
folder_list=

# Pause timeout in seconds before presence is hidden (0 to never hide)
pause_timeout=1800
```

### Keybindings & Commands

You can bind a key in `input.conf` to toggle presence on and off:

```ini
D script-message-to rich_presence toggle
```

Available script messages:
- `script-message-to rich_presence toggle`
- `script-message-to rich_presence on`
- `script-message-to rich_presence off`
- `script-message-to rich_presence application_id <id>`
- `script-message-to rich_presence folder_filter_mode <disabled|whitelist|blacklist>`
- `script-message-to rich_presence folder_list <path1;path2>`
- `script-message-to rich_presence pause_timeout <seconds>`

## Building from Source

### Prerequisites

- CMake 3.18+
- C++20 compliant compiler (MSVC 2019+, GCC 10+, or Clang 11+)
- vcpkg
- [Discord Social SDK](https://discord.com/developers/social-sdk)

### Setup

1. Clone the repository recursively:
   ```bash
   git clone --recursive https://github.com/Sirye8/mpv-rich-presence.git
   cd mpv-rich-presence
   ```

2. Extract the Discord Social SDK into `thirdparty/discord_social_sdk` such that `thirdparty/discord_social_sdk/bin` and `lib` exist.

3. Bootstrap vcpkg:
   ```batch
   .\thirdparty\vcpkg\bootstrap-vcpkg.bat
   ```
   *(or `./thirdparty/vcpkg/bootstrap-vcpkg.sh` on Linux)*

4. Configure with CMake:
   ```bash
   cmake --preset default
   ```

5. Build the project:
   ```bash
   cmake --build build --config Release
   ```

Compiled binaries will be generated in `build/` (and can be packaged with `cmake --build build --target install`).

## License

This project is licensed under the GNU Affero General Public License v3.0 (AGPL-3.0). See [LICENSE](LICENSE) and [NOTICE](NOTICE) for details.

Based on [mpv-rich-presence](https://github.com/goodtrailer/mpv-rich-presence) by Alden Wu.
