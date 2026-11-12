# Build from source

## Prerequisites

- CMake 3.21 or newer for the configure preset
- A C++20 compiler and standard library with `std::format` support
- vcpkg
- [Discord Social SDK](https://discord.com/developers/social-sdk)

## Build and package

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

Run `python tools/package.py dist/mpv-rich-presence-windows-x64.zip` on Windows, or choose a Linux ZIP filename on Linux. The script builds Release, runs CTest, and adds the verified Chromaprint executable and its source and license files. The Windows x64 ZIP also includes embeddable Python. The Linux x86-64 and ARM64 ZIPs use system Python 3.

Compiled binaries will be generated in `build/Release/rich-presence.dll` (or `build/rich-presence.so`).

## License

This project is licensed under the GNU Affero General Public License v3.0 (AGPL-3.0). See [LICENSE](../LICENSE) and [NOTICE](../NOTICE) for full terms.

Based on [mpv-rich-presence](https://github.com/goodtrailer/mpv-rich-presence) by Alden Wu.

### Third-party notices
- **Anitomy**: Licensed under Mozilla Public License 2.0 (MPL-2.0) by Eren Okka. See [licenses/LICENSE-Anitomy](../licenses/LICENSE-Anitomy).
- **Discord Social SDK**: The embedded runtime binary is proprietary software of Discord Inc. and governed by the [Discord Developer Terms of Service](https://discord.com/developers/docs/legal).
- **cpr**: Licensed under the MIT License. See [licenses/LICENSE-cpr](../licenses/LICENSE-cpr).
- **curl**: Governed by the curl license (MIT/ISC-like). See [licenses/LICENSE-curl](../licenses/LICENSE-curl).
- **nlohmann-json**: Licensed under the MIT License. See [licenses/LICENSE-nlohmann-json](../licenses/LICENSE-nlohmann-json).
- **zlib**: Licensed under the zlib License. See [licenses/LICENSE-zlib](../licenses/LICENSE-zlib).
- **mpv client API header**: Licensed under the ISC License by the mpv developers. See [licenses/LICENSE-mpv](../licenses/LICENSE-mpv).
- **SQLite**: Public domain. See [its notice](../licenses/LICENSE-SQLite).
- **Boost** and **CMakeRC**: See individual licenses in the [licenses/](../licenses/) directory.
