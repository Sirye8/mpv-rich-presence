# SPDX-License-Identifier: AGPL-3.0-or-later
"""Build, test and archive one clean CMake install tree."""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile


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
