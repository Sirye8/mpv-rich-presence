# mpv-rich-presence

A modern, full-featured Discord Rich Presence plugin for mpv on Windows and Linux, powered by the Discord Social SDK. Displays rich media details, automated anime recognition via AniList, strict folder privacy filtering, speed-aware playback timing, and seamless multi-window coordination.

## Features

- **Rich Media Presence**:
  - Displays currently playing video or music with title, artist, and real-time playback progress.
  - Music and audio tracks automatically display as "Listening to [Artist] - [Title]" on Discord.
  - Non-anime videos display clean titles with duration timestamps and play or pause indicators.

- **Automated Anime Recognition (AniList)**:
  - Parses filenames using Anitomy to extract clean titles, season indicators (such as `S2`, `4th Season`, `S01E13`), part or cour notation, episode numbers, and release groups.
  - Queries the AniList GraphQL API with similarity confidence matching to fetch high-resolution cover art, Romaji and English titles, and total episode counts.
  - Traverses sequel and relation chains across multi-cour and continuous seasons without episode index wrapping issues.
  - Adds a clickable "View on AniList" button directly in Discord.

- **Speed-Aware Timing & Live Stream Support**:
  - Automatically calculates remaining playback time and seek detection according to current playback speed (such as 1.25x, 1.5x, or 2x).
  - Handles live streams and unknown-duration broadcasts gracefully with clean elapsed timers.
  - Displays network cache and buffering states ("Buffering...") when mpv is waiting on data.

- **Privacy & Folder Filtering**:
  - Whitelist or blacklist specific directories so private or sensitive media is never shared.
  - Privacy checks run before online metadata lookups, preventing sensitive file paths or titles from being queried online.
  - Fully OS-aware path normalization that handles symlinks, parent directory references, and case sensitivity properly across Windows and Linux.

- **Multi-Instance Coordination & Clean Handover**:
  - Coordinates presence across multiple concurrent mpv windows using an active lease system with automatic heartbeats.
  - Resuming or playing a window smoothly hands over Discord presence ownership to the active player.
  - Releases ownership immediately when an instance exits.

- **Managed Background Worker & Stability**:
  - Dedicated background worker queue with cancellation checks for rapid file skipping.
  - Eliminates detached thread hazards and safely cleans up memory and mpv property allocations on shutdown.

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
   - Place `rich-presence.conf` into your mpv `script-opts/` folder.
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

- CMake 3.18 or higher
- C++20 compliant compiler (MSVC 2019+, GCC 10+, or Clang 11+)
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
   ```

5. Build the Release configuration:
   ```bash
   cmake --build build --config Release
   ```

6. Run the regression test suite:
   ```bash
   ctest --test-dir build -C Release --output-on-failure
   ```

Compiled binaries will be generated in `build/Release/rich-presence.dll` (or `build/rich-presence.so`).

## License

This project is licensed under the GNU Affero General Public License v3.0 (AGPL-3.0). See [LICENSE](LICENSE) and [NOTICE](NOTICE) for full terms.

Based on [mpv-rich-presence](https://github.com/goodtrailer/mpv-rich-presence) by Alden Wu.

### Third-Party Notices
- **Anitomy**: Licensed under Mozilla Public License 2.0 (MPL-2.0) by Eren Okka. See [licenses/LICENSE-Anitomy](licenses/LICENSE-Anitomy).
- **Discord Social SDK**: The embedded runtime binary is proprietary software of Discord Inc. and governed by the [Discord Developer Terms of Service](https://discord.com/developers/docs/legal).
- **Boost**, **CMakeRC**, and **mpv client API**: See individual licenses in the [licenses/](licenses/) directory.
