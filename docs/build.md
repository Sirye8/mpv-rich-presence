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

The script builds Release, runs CTest and archives a clean install. It writes a SHA-256 checksum next to the ZIP. The archive's `build-info.json` records the source commit, platform and architecture. Each ZIP includes the plugin, the shipped config, runtime files and license notices. Chromaprint comes with its corresponding source and licenses. Windows x64 also includes embeddable Python. Linux uses system Python.

The [README](../README.md), [user guide](user-guide.md) and screenshots are published in the repository. The packaged config links to the settings guide.

Compiled binaries will be generated in `build/Release/rich-presence.dll` (or `build/rich-presence.so`).

## License

See [LICENSE](../LICENSE) for the project's license and [NOTICE](../NOTICE) for upstream attribution and third-party terms.
