# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build, test and archive one clean CMake install tree."""

import argparse
import hashlib
import io
import os
from pathlib import Path
import subprocess
import tempfile
import urllib.request
import zipfile


# python.org publishes the hash with the Windows x64 embeddable distribution.
PYTHON_VERSION = "3.13.17"
PYTHON_SHA256 = "9e35319bffedab8b36ad44d8f377cb4f1733bcec257a0b6d04d94f01486ddb38"


def bundle_python(build, root):
    name = f"python-{PYTHON_VERSION}-embed-amd64.zip"
    cached = build / name
    if cached.is_file():
        payload = cached.read_bytes()
    else:
        url = f"https://www.python.org/ftp/python/{PYTHON_VERSION}/{name}"
        with urllib.request.urlopen(url, timeout=30) as response:
            payload = response.read(16 * 1024 * 1024)
    if hashlib.sha256(payload).hexdigest() != PYTHON_SHA256:
        raise RuntimeError(f"Unexpected checksum for {name}")
    if not cached.is_file():
        cached.write_bytes(payload)
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        archive.extractall(root / "scripts" / "mpv-rich-presence" / "runtime")


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
        if (root / "scripts" / "rich-presence.dll").is_file():
            bundle_python(build, root)
        with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as f:
            temporary = Path(f.name)
        try:
            with zipfile.ZipFile(temporary, "w") as archive:
                for file in sorted(root.rglob("*")):
                    if file.is_file():
                        member = zipfile.ZipInfo(file.relative_to(root).as_posix())
                        member.compress_type = zipfile.ZIP_DEFLATED
                        archive.writestr(member, file.read_bytes())
            os.replace(temporary, output)
        finally:
            temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--build", type=Path, default=Path("build"))
    args = parser.parse_args()
    package(args.build.resolve(), args.output.resolve())
