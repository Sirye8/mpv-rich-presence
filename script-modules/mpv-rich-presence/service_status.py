# SPDX-License-Identifier: AGPL-3.0-or-later
"""The last outcome of each hosted service, for Diagnostics. No titles or tokens are kept."""

import threading
import time
import urllib.error

_lock = threading.Lock()
states = {}
version = 0


def record(service, state, retry_after=None):
    global version
    entry = {"state": state, "time": round(time.time())}
    if retry_after:
        entry["retry_after"] = retry_after
    with _lock:
        if states.get(service, {}).get("state") != state or state != "OK" or \
                entry["time"] - states.get(service, {}).get("time", 0) >= 60:
            states[service] = entry
            version += 1


def classify(exc, message=""):
    """Map a failed call to a state a person can act on."""
    text = (message or str(exc)).lower()
    if isinstance(exc, urllib.error.HTTPError):
        if exc.code == 429:
            wait = (exc.headers or {}).get("Retry-After", "")
            return "Rate limited", int(wait) if wait.isdigit() else None
        if 300 <= exc.code < 400:
            return "Unexpected reply", None
        if "owner setup required" in text or (exc.headers or {}).get("X-Relay-Reason") == "not-configured":
            return "Not configured", None
        if exc.code >= 500:
            return "Unreachable", None
        if "ticket validation" in text or "invalid_grant" in text or exc.code == 410:
            return "Sign-in expired", None
        return "Rejected", None
    if isinstance(exc, (ValueError, KeyError, TypeError)):
        return "Unexpected reply", None
    if isinstance(exc, (urllib.error.URLError, TimeoutError, ConnectionError, OSError)):
        return "Unreachable", None
    return "Unexpected reply", None


def guarded(service, call, expected=(404,)):
    """Run one call and remember how it went, then return or re-raise its result."""
    try:
        result = call()
    except urllib.error.HTTPError as exc:
        # A missing entry is a normal answer, not a service problem.
        record(service, *(("OK", None) if exc.code in expected else classify(exc)))
        raise
    except Exception as exc:
        record(service, *classify(exc))
        raise
    record(service, "OK")
    return result


def snapshot():
    with _lock:
        return version, {key: dict(value) for key, value in states.items()}
