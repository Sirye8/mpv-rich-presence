# SPDX-License-Identifier: AGPL-3.0-or-later
"""Child process for the concurrent credit and absolute-write regression."""

from pathlib import Path
import sys
from unittest.mock import patch

sys.path.insert(
    0, str(Path(__file__).resolve().parents[1] / "script-modules/mpv-rich-presence")
)
from account import AccountError, save_progress
from backend import Worker


def contend(path, window, now):
    worker = Worker(path, window, {})
    account = worker.get("account", "current")
    sample = dict(
        session=1,
        media=2,
        episode="7",
        total=12,
        watch_episode=True,
        active=True,
        allowed=True,
        watch=True,
        speed=2,
        seek=0,
        tracking={"mode": "manual"},
    )
    for step in (0, 1):
        worker.put(
            "player",
            window,
            dict(sample, time=now + step, wall=1000 * (now + step), pos=2 * step),
        )
        with patch("backend.time.time", return_value=now + step):
            worker.sample()
    base = dict(
        id=9,
        viewer=1,
        media=2,
        progress=6,
        status="CURRENT",
        repeat=0,
        updatedAt=0,
        session=1,
    )

    def transport(query, args, token):
        row = worker.get("fake", "entry")
        if query.startswith("mutation"):
            row["progress"] = args["progress"]
            worker.put("fake", "entry", row)
            worker.db.execute("INSERT INTO writes DEFAULT VALUES")
            return {"SaveMediaListEntry": row}
        return {"Page": {"mediaList": [row]}}

    def save(token, entry, target, total, valid, **kw):
        return save_progress(token, entry, target, total, valid, transport, **kw)

    with (
        patch.object(worker, "credential", return_value="fixture"),
        patch("backend.save_progress", side_effect=save),
    ):
        try:
            worker.write(
                dict(
                    base=base,
                    target=7,
                    session=1,
                    media=2,
                    account=account["generation"],
                )
            )
        except AccountError as exc:
            if "changed" not in str(exc):
                raise
    worker.db.close()


if __name__ == "__main__":
    contend(sys.argv[1], sys.argv[2], float(sys.argv[3]))
