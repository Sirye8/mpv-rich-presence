# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build, test and archive one clean CMake install tree."""

import argparse
import hashlib
import io
import os
from pathlib import Path
import platform
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile


# python.org publishes the hash with the Windows x64 embeddable distribution.
PYTHON_VERSION = "3.13.17"
PYTHON_SHA256 = "9e35319bffedab8b36ad44d8f377cb4f1733bcec257a0b6d04d94f01486ddb38"
CHROMAPRINT_RELEASE = (
    "https://github.com/acoustid/chromaprint/releases/download/v1.6.1/"
)
FPCALC = {
    "windows-x86_64.zip": "735d6182b38e9f364b84ce6f4ccd682c75e2851de89735711d6b762d12b92a4e",
    "linux-x86_64.tar.gz": "fc16cd37a70168040bc9ceb45f1d4d1216f5a75bc4c9cf8564bea70ac6a45733",
    "linux-arm64.tar.gz": "7eaf5d655c4aa172ab28e3c870b8bb61dd2c327ac94de145676f88842cf6215a",
}


def verified_download(build, name, url, digest):
    cached = build / name
    if cached.is_file():
        payload = cached.read_bytes()
    else:
        with urllib.request.urlopen(url, timeout=30) as response:
            payload = response.read(32 * 1024 * 1024)
    if hashlib.sha256(payload).hexdigest() != digest:
        raise RuntimeError(f"Unexpected checksum for {name}")
    if not cached.is_file():
        cached.write_bytes(payload)
    return payload


def bundle_python(build, root):
    name = f"python-{PYTHON_VERSION}-embed-amd64.zip"
    payload = verified_download(
        build,
        name,
        f"https://www.python.org/ftp/python/{PYTHON_VERSION}/{name}",
        PYTHON_SHA256,
    )
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        archive.extractall(root / "scripts" / "mpv-rich-presence" / "runtime")


def bundle_fpcalc(build, root, windows):
    architecture = {"AMD64": "x86_64", "aarch64": "arm64"}.get(
        platform.machine(), platform.machine()
    )
    suffix = "windows-x86_64.zip" if windows else f"linux-{architecture}.tar.gz"
    if suffix not in FPCALC:
        raise RuntimeError(f"No packaged fpcalc for {suffix}")
    name = f"chromaprint-fpcalc-1.6.1-{suffix}"
    payload = verified_download(build, name, CHROMAPRINT_RELEASE + name, FPCALC[suffix])
    executable = "fpcalc.exe" if windows else "fpcalc"
    runtime = root / "scripts" / "mpv-rich-presence" / "runtime"
    runtime.mkdir(parents=True, exist_ok=True)
    binary = runtime / executable
    if windows:
        with zipfile.ZipFile(io.BytesIO(payload)) as archive:
            binary.write_bytes(
                archive.read(f"{name.removesuffix('.zip')}/{executable}")
            )
    else:
        with tarfile.open(fileobj=io.BytesIO(payload), mode="r:gz") as archive:
            (member,) = [
                m
                for m in archive.getmembers()
                if m.isfile() and Path(m.name).name == executable
            ]
            binary.write_bytes(archive.extractfile(member).read())
        binary.chmod(0o755)

    sources = root / "licenses" / "fpcalc"
    sources.mkdir(parents=True, exist_ok=True)
    source_archives = (
        (
            "chromaprint-1.6.1.tar.gz",
            CHROMAPRINT_RELEASE + "chromaprint-1.6.1.tar.gz",
            "3368805af0ee47b9df74df10b5001a44569e01df2844dab520031720dde9ad23",
            "chromaprint-1.6.1/LICENSE.md",
        ),
        (
            "ffmpeg-8.0.tar.xz",
            "https://ffmpeg.org/releases/ffmpeg-8.0.tar.xz",
            "b2751fccb6cc4c77708113cd78b561059b6fa904b24162fa0be2d60273d27b8e",
            "ffmpeg-8.0/COPYING.LGPLv2.1",
        ),
        (
            "ffmpeg-build-8.0-1.tar.gz",
            "https://codeload.github.com/acoustid/ffmpeg-build/tar.gz/refs/tags/v8.0-1",
            "2533f000cc9d4ea72e5108c95bf2cc59770fa076ad8d4147e66ff5c16e42de14",
            "ffmpeg-build-8.0-1/LICENSE",
        ),
    )
    for name, url, digest, license_path in source_archives:
        payload = verified_download(build, name, url, digest)
        (sources / name).write_bytes(payload)
        with tarfile.open(fileobj=io.BytesIO(payload), mode="r:*") as archive:
            (sources / (license_path.split("/")[0] + "-LICENSE.txt")).write_bytes(
                archive.extractfile(license_path).read()
            )


def package(build, output):
    subprocess.run(["cmake", "--build", str(build), "--config", "Release"], check=True)
    subprocess.run(
        ["ctest", "--test-dir", str(build), "-C", "Release", "--output-on-failure"],
        check=True,
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=build) as stage:
        subprocess.run(
            [
                "cmake",
                "--install",
                str(build),
                "--config",
                "Release",
                "--prefix",
                stage,
            ],
            check=True,
        )
        root = Path(stage) / "mpv-rich-presence"
        windows = (root / "scripts" / "rich-presence.dll").is_file()
        if windows:
            bundle_python(build, root)
        bundle_fpcalc(build, root, windows)
        with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as f:
            temporary = Path(f.name)
        try:
            with zipfile.ZipFile(temporary, "w") as archive:
                for file in sorted(root.rglob("*")):
                    if file.is_file():
                        member = zipfile.ZipInfo(file.relative_to(root).as_posix())
                        member.compress_type = zipfile.ZIP_DEFLATED
                        if os.name != "nt":
                            member.external_attr = file.stat().st_mode << 16
                        archive.writestr(member, file.read_bytes())
            os.replace(temporary, output)
        finally:
            temporary.unlink(missing_ok=True)
    with output.open("rb") as archive:
        digest = hashlib.sha256()
        for chunk in iter(lambda: archive.read(1024 * 1024), b""):
            digest.update(chunk)
    checksum = digest.hexdigest()
    output.with_suffix(output.suffix + ".sha256").write_text(f"{checksum}  {output.name}\n", encoding="ascii")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--build", type=Path, default=Path("build"))
    args = parser.parse_args()
    package(args.build.resolve(), args.output.resolve())
