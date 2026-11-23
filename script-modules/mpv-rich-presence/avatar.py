# SPDX-License-Identifier: AGPL-3.0-or-later
"""Decode one avatar or card image to the raw format used by mpv overlays."""

import ctypes
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit
import urllib.request


def player_path(pid):
    if os.name != "nt":
        return os.readlink(f"/proc/{pid}/exe")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.restype = ctypes.c_void_p
    handle = kernel.OpenProcess(0x1000, False, pid)
    if not handle:
        raise OSError("Could not open mpv.")
    try:
        path = ctypes.create_unicode_buffer(32768)
        size = ctypes.c_ulong(len(path))
        if not kernel.QueryFullProcessImageNameW(ctypes.c_void_p(handle), 0, path, ctypes.byref(size)):
            raise OSError("Could not find mpv.")
        return path.value
    finally:
        kernel.CloseHandle(ctypes.c_void_p(handle))


HOSTS = {"s4.anilist.co", "lastfm-img.freetls.fastly.net"}


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args):
        return None


class HttpsRedirect(urllib.request.HTTPRedirectHandler):
    """Card artwork such as Cover Art Archive redirects to its storage. Only https targets are followed."""
    max_redirections = 3

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if urlsplit(newurl).scheme != "https":
            return None
        return super().redirect_request(req, fp, code, msg, headers, newurl)


# Decoded size, byte limit and mask for each kind of image.
MODES = {"avatar": (64, 512 * 1024, "circle"), "badge": (64, 2 * 1024 * 1024, "circle"),
         "cover": (128, 4 * 1024 * 1024, "rounded")}


def main(address, pid, mode="avatar"):
    size, limit, mask = MODES[mode]
    parsed = urlsplit(address)
    # Account avatars come only from the two account hosts. Card images are the public links sent to Discord.
    if parsed.scheme != "https" or (mode == "avatar" and parsed.netloc not in HOSTS) or not parsed.netloc:
        raise ValueError("Unexpected image host.")
    request = urllib.request.Request(address, headers={
        "User-Agent": "mpv-rich-presence (+https://github.com/Sirye8/mpv-rich-presence)",
    })
    opener = urllib.request.build_opener(NoRedirect() if mode == "avatar" else HttpsRedirect())
    with opener.open(request, timeout=6) as response:
        data = response.read(limit + 1)
    if len(data) > limit or not data.startswith((b"\x89PNG\r\n\x1a\n", b"\xff\xd8\xff", b"GIF8", b"RIFF")):
        raise ValueError("Unsupported image.")
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "image"
        source.write_bytes(data)
        output = source.with_suffix(".bgra")
        # Fill the square without stretching, as Discord crops artwork.
        scale = f"scale={size}:{size}:force_original_aspect_ratio=increase,crop={size}:{size},format=bgra"
        subprocess.run(
            [player_path(int(pid)), "--no-config", "--load-scripts=no", "--no-audio",
             "--no-terminal", "--frames=1", f"--vf={scale}", "--of=rawvideo",
             "--ovc=rawvideo", f"--o={output}", "--", str(source)],
            timeout=8, check=True,
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )
        pixels = bytearray(output.read_bytes())
    if len(pixels) != size * size * 4:
        raise ValueError("Unexpected image dimensions.")
    centre, radius = (size - 1) / 2, (size - 1) / 2
    corner = size * 0.1
    notch = size * 0.881
    for y in range(size):
        for x in range(size):
            if mask == "circle":
                outside = (x - centre) ** 2 + (y - centre) ** 2 > radius ** 2
            else:
                dx, dy = max(corner - x, x - (size - 1 - corner), 0), max(corner - y, y - (size - 1 - corner), 0)
                # The panel places the badge over the lower right corner. Cutting its ring here matches Discord,
                # because mpv draws images above the panel's own shapes.
                outside = dx * dx + dy * dy > corner * corner or                     (x - notch) ** 2 + (y - notch) ** 2 < (size * 0.2024) ** 2
            if outside:
                offset = (y * size + x) * 4
                pixels[offset:offset + 4] = b"\0" * 4
    sys.stdout.buffer.write(pixels)


if __name__ == "__main__":
    main(*sys.argv[1:])
