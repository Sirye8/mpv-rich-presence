# Build from source

## Prerequisites

- CMake 3.21 or newer for the configure preset
- A C++20 compiler and standard library with `std::format` support
- vcpkg
- Python 3.9 or newer for packaging and account tests
- [Discord Social SDK](https://discord.com/developers/social-sdk)

## Build and package

1. Clone the repository with its submodules.

   ```bash
   git clone --recursive https://github.com/Sirye8/mpv-rich-presence.git
   cd mpv-rich-presence
   ```

2. Extract the Discord Social SDK for your platform into `thirdparty/discord_social_sdk`. CMake expects the headers in `include/`. On Windows it uses the import library in `lib/release/` and the DLL in `bin/release/`. On Linux it uses the shared library in `lib/release/`.

3. Bootstrap vcpkg.

   ```batch
   .\thirdparty\vcpkg\bootstrap-vcpkg.bat
   ```
   *(or `./thirdparty/vcpkg/bootstrap-vcpkg.sh` on Linux)*

4. Configure with CMake. For a Windows x64 package, add `-DVCPKG_TARGET_TRIPLET=x64-windows-static-md` to the configure command.

   ```bash
   cmake --preset default -DCMAKE_BUILD_TYPE=Release
   ```

5. Build the Release configuration and run the tests.

   ```bash
   cmake --build build --config Release
   ctest --test-dir build -C Release --output-on-failure
   ```

### Package contents

Run `python tools/package.py dist/mpv-rich-presence-windows-x64.zip` on Windows. On Linux, use `python3 tools/package.py dist/mpv-rich-presence-linux-x64.zip` and name the output for your architecture. Packaging runs on the target platform and supports Windows x64 and Linux x86-64 or ARM64.

The script builds Release, runs CTest and archives a clean install. It writes a SHA-256 checksum next to the ZIP. The archive's `build-info.json` records the version, source commit, platform and architecture. A copy installs with the plugin so **Diagnostics → Release** can show it. Each ZIP includes the plugin, the shipped config, runtime files and license notices. Chromaprint comes with its corresponding source and licenses. Windows x64 also includes embeddable Python. Linux uses system Python.

The [README](../README.md), [user guide](user-guide.md) and screenshots are published in the repository. The packaged config links to the settings guide.

The Windows x64 ZIP is the prebuilt release asset. Linux users can build the same tag using the steps above. The live panel blur requires an mpv build with the `gpu-next` output.

Compiled binaries will be generated in `build/Release/rich-presence.dll` (or `build/rich-presence.so`).

## Test the package in CI

The **package** job builds the Windows and Linux packages and runs the packaged panel tests on pushes to `main` and on manual runs. It downloads a trimmed Discord Social SDK from the private repository `Sirye8/discord-social-sdk-ci` and checks it against the `DISCORD_SDK_ARCHIVE_SHA256` repository variable. The `DISCORD_SDK_TOKEN` secret must hold a fine-grained token with read-only access to that repository's contents. Pull requests and forks skip the job.

To replace the SDK, upload the new archive to the `sdk` release of that repository and update the variable with its SHA-256.

## Release a version

Every release, including one that only fixes bugs, increases the second version number: `v2.3`, then `v2.4`.

1. Update `project(... VERSION 2.3)` in `CMakeLists.txt`, `"version": "2.3.0"` in `vcpkg.json` and any version lines in the docs. Commit only these changes as `chore(release): set version to 2.3`.
2. Run `python tools/package.py dist/mpv-rich-presence-windows-x64.zip`. Copy the ZIP and its `.sha256` file to `dist/v2.3/`, and check that `build-info.json` names the release commit.
3. Push `main` and wait for the Regressions workflow to pass.
4. Run `gh release create v2.3 --target main --title v2.3 --notes-file <notes>` with the ZIP and its `.sha256` file. The notes list what changed and how to upgrade, and link the tag's user guide and build guide.

## License

See [LICENSE](../LICENSE) for the project's license and [NOTICE](../NOTICE) for upstream attribution and third-party terms.
