#!/usr/bin/env python3
"""Runner daemon on the DevLog / AgentLog protocol.

WHO WRITES WHAT
    /home/richard/shots/DevLog.txt     WE write. Every action we take, and every request we
                                       make of the Master, as an `===Timestamp: ...===` block.
    /home/richard/shots/AgentLog.txt   THE MASTER writes. Its blocks are addressed
                                       `===To: Dev===` and carry a DIAGNOSIS and/or a FIX.
    /home/richard/shots/runner.log     operator log for a human (not part of the protocol).

GRAMMAR
    Every entry is one block, opening with an exact timestamp line:

        ===Timestamp: 2026-10-24 21:40:00===
        ACTION: capture kitchen -- 1280x720, 67s, READY

    A request to the Master adds a direction line and then the ask:

        ===Timestamp: 2026-10-24 21:40:00===
        ===To: Agent===
        ACTION: analyze kitchen.png

    Inbound, we only consider blocks whose body contains `===To: Dev===`:

        ===Timestamp: 2026-10-24 21:41:00===
        ===To: Dev===
        DIAGNOSIS: the counter top reads as wood
        FIX: { sed -i '1401s/DarkSteel/DarkSteel/' HIHouse.cpp }

PRUNING
    Both logs are pruned at the START OF EVERY ITERATION: blocks whose timestamp is older than
    PRUNE_DAYS (7) are dropped. A block with no parseable timestamp is KEPT — deleting text we
    cannot date is how a log quietly loses history.

WHY THE FIX HALF IS GATED (carried over from the mailbox version, still the point)
    A FIX is applied only if it can be VERIFIED against the file it names, and every applied
    fix is reverted if the build does not say "Result: Succeeded". Measured 2026-10-24, the
    instructions actually received were stale or destructive:

      * `sed -i '1452s/Wood/DarkSteel/'` -- line 1452 had moved; it is now an opacity setting.
      * "hedges to y = -12.5, height 1.83" -- y=-12.5 is INSIDE the house.
      * `sed -i '778,807d; r .../nicehouse.patch'` -- deletes 30 lines of PlaceSketchfabModel,
        and the patch file does not exist.

    So: line-targeted edits must find their text; range deletes, file includes and missing
    paths are refused; a failed build reverts the edit and reports it. A runner that says
    "I did not do that, and here is why" is worth more than one reporting success after
    corrupting a file.

    A block is processed ONCE, keyed by a hash persisted in `.runner_seen.json`, so a restart
    cannot re-apply yesterday's patch.
"""

import fcntl
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timedelta

SHOTS = "/home/richard/shots"
DEVLOG = os.path.join(SHOTS, "DevLog.txt")
AGENTLOG = os.path.join(SHOTS, "AgentLog.txt")
RUNLOG = os.path.join(SHOTS, "runner.log")
SEEN = os.path.join(SHOTS, ".runner_seen.json")
LOCK = "/tmp/hi_runner.lock"

PROJECT = "/data/projects/HIUnreal"
DEVS = os.path.join(PROJECT, "tools", "devloop-shots.py")
BUILD = "/data/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh"

POLL = 5.0
PRUNE_DAYS = 7
TS_FMT = "%Y-%m-%d %H:%M:%S"
TS_RE = re.compile(r"===Timestamp:\s*(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\s*===")
VIEWS = {"front", "neighbors", "kitchen", "living", "dining", "basement"}


def now():
    return datetime.now().strftime(TS_FMT)


def runlog(msg):
    line = f"{now()} {msg}"
    print(line, flush=True)
    with open(RUNLOG, "a") as f:
        f.write(line + "\n")


def devlog(body):
    """Append one timestamped block to DevLog.txt."""
    with open(DEVLOG, "a") as f:
        f.write(f"===Timestamp: {now()}===\n{body.rstrip()}\n\n")
    runlog("devlog: " + body.replace("\n", " | ")[:160])


def split_blocks(text):
    """[(timestamp or None, body), ...] -- the preamble comes back with timestamp None."""
    parts = TS_RE.split(text)
    blocks = []
    if parts[0].strip():
        blocks.append((None, parts[0]))
    for i in range(1, len(parts), 2):
        blocks.append((parts[i], parts[i + 1] if i + 1 < len(parts) else ""))
    return blocks


def prune(path):
    """Drop blocks older than PRUNE_DAYS. Returns how many were dropped."""
    if not os.path.exists(path):
        return 0
    text = open(path).read()
    if not text.strip():
        return 0
    cutoff = datetime.now() - timedelta(days=PRUNE_DAYS)
    keep, dropped = [], 0
    for ts, body in split_blocks(text):
        if ts is None:
            keep.append((ts, body))
            continue
        try:
            when = datetime.strptime(ts, TS_FMT)
        except ValueError:
            keep.append((ts, body))       # undatable: keep rather than lose history
            continue
        if when >= cutoff:
            keep.append((ts, body))
        else:
            dropped += 1
    if dropped:
        with open(path, "w") as f:
            for ts, body in keep:
                f.write(body if ts is None else f"===Timestamp: {ts}===\n{body}")
        runlog(f"pruned {dropped} block(s) older than {PRUNE_DAYS} days from "
               f"{os.path.basename(path)}")
    return dropped


def seen_load():
    try:
        return set(json.load(open(SEEN)))
    except Exception:
        return set()


def seen_save(seen):
    # Bounded: only the recent past matters, and this file is not the record.
    json.dump(sorted(seen)[-200:], open(SEEN, "w"))


def capture(view):
    """Capture one view via the project's own harness. Returns True on success."""
    dst = os.path.join(SHOTS, f"{view}.png")
    before = os.path.getmtime(dst) if os.path.exists(dst) else 0
    t0 = time.time()
    try:
        r = subprocess.run([sys.executable, DEVS, "--no-build", "--views", view],
                           cwd=PROJECT, capture_output=True, text=True, timeout=600)
    except subprocess.TimeoutExpired:
        devlog(f"ACTION: capture {view} -- TIMEOUT after 600s")
        return False
    secs = time.time() - t0
    ok = r.returncode == 0 and os.path.exists(dst) and os.path.getmtime(dst) > before
    if ok:
        devlog(f"ACTION: capture {view} -- 1280x720, {secs:.0f}s, READY")
    else:
        devlog(f"ACTION: capture {view} -- FAILED rc={r.returncode} after {secs:.0f}s")
        runlog((r.stdout or "")[-300:] + (r.stderr or "")[-300:])
    return ok


def rebuild():
    """Build, serialised against any other builder. True only on 'Result: Succeeded'."""
    lock = open("/tmp/hi_devloop_build.lock", "w")
    t0 = time.time()
    try:
        fcntl.flock(lock, fcntl.LOCK_EX)
        r = subprocess.run([BUILD, "HIUnrealEditor", "Linux", "Development",
                            f"-project={PROJECT}/HIUnreal.uproject"],
                           capture_output=True, text=True, timeout=3600)
    finally:
        lock.close()
    out = (r.stdout or "") + (r.stderr or "")
    with open("/tmp/hi_runner_build.log", "w") as f:
        f.write(out)
    ok = "Result: Succeeded" in out
    devlog(f"ACTION: build -- {'Succeeded' if ok else 'FAILED'} in {time.time() - t0:.0f}s")
    return ok


def _line_of(path, n):
    with open(path) as f:
        lines = f.read().splitlines(keepends=True)
    if n < 1 or n > len(lines):
        return None, lines
    return lines[n - 1], lines


def apply_verified(text):
    """Apply a FIX block only if it verifies. Returns (status, detail)."""
    if re.search(r"\b\d+\s*,\s*\d+\s*d\b", text):
        return "blocked", ("line-range delete is not verifiable; refusing rather than "
                           "trusting a line number")
    for rfile in re.findall(r"\br\s+(/\S+)", text):
        if not os.path.exists(rfile):
            return "blocked", f"asked to include {rfile}, which does not exist"

    edits = []
    for m in re.finditer(r"sed\s+-i\s+['\"]?(\d+)s/([^/]*)/([^/]*)/['\"]?\s*(\S*)", text):
        edits.append((m.group(4) or None, int(m.group(1)), m.group(2), m.group(3)))
    for m in re.finditer(r"(\S+\.(?:cpp|h))[^\n]*?line\s+(\d+)\s*:?\s*\n?\s*"
                         r"([^\n→]+?)\s*(?:→|->)\s*([^\n]+)", text):
        edits.append((m.group(1), int(m.group(2)), m.group(3).strip(), m.group(4).strip()))

    if not edits:
        return "blocked", ("no verifiable edit found (expected `sed -i 'Ns/A/B/'` or "
                           "'file, line N: A -> B')")

    changed = []
    for target, n, old, new in edits:
        path = target if target and target.startswith("/") else os.path.join(
            PROJECT, "Source", "HIUnreal", os.path.basename(target or "HIHouse.cpp"))
        if not os.path.exists(path):
            return "blocked", f"{path} does not exist"
        line, lines = _line_of(path, n)
        if line is None:
            return "blocked", f"{os.path.basename(path)} has no line {n}"
        if old not in line:
            return "blocked", (f"line {n} of {os.path.basename(path)} does not contain "
                               f"{old!r} -- stale instruction; line {n} is {line.strip()[:80]!r}")
        lines[n - 1] = line.replace(old, new, 1)
        with open(path, "w") as f:
            f.writelines(lines)
        changed.append(os.path.relpath(path, PROJECT))
        devlog(f"ACTION: edit {os.path.relpath(path, PROJECT)}:{n} -- {old!r} -> {new!r}")

    if not rebuild():
        for rel in changed:
            subprocess.run(["git", "checkout", "--", rel], cwd=PROJECT, capture_output=True)
        return "blocked", f"build FAILED after editing {', '.join(changed)}; reverted"
    return "fixed", f"applied to {', '.join(changed)}; build succeeded"


def handle_block(ts, body):
    """Act on one inbound block. Returns False to stop the runner."""
    if "===To: Dev===" not in body:
        return True                                  # a note for somebody else
    if re.search(r"STATUS:\s*✅", body):
        devlog("ACTION: stop -- STATUS done received from the Master")
        return False

    m = re.search(r"ACTION:\s*take_screenshot\s+(\S+)", body)
    if m:
        view = m.group(1).strip()
        if view not in VIEWS:
            devlog(f"ACTION: capture {view} -- UNKNOWN VIEW (known: {sorted(VIEWS)})")
            return True
        if capture(view):
            devlog(f"===To: Agent===\nACTION: analyze {view}.png")
        return True

    if re.search(r"^\s*FIX:", body, re.M):
        status, detail = apply_verified(body)
        devlog(f"ACTION: fix -- {'FIXED: ' if status == 'fixed' else 'BLOCKED: '}{detail}")
        if status == "fixed":
            devlog("===To: Agent===\nACTION: analyze the rebuilt game")
        return True

    diag = re.search(r"DIAGNOSIS:\s*(.+)", body)
    if diag:
        devlog(f"ACTION: diagnosis noted -- {diag.group(1).strip()[:200]}")
    return True


def main():
    lock = open(LOCK, "w")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        print("runner: another runner holds the lock", file=sys.stderr)
        return 1

    os.makedirs(SHOTS, exist_ok=True)
    for p in (DEVLOG, AGENTLOG, RUNLOG):
        open(p, "a").close()
    runlog(f"runner started (pid {os.getpid()}); devlog={DEVLOG} agentlog={AGENTLOG}")
    devlog(f"ACTION: runner online -- polling AgentLog.txt every {POLL:.0f}s, "
           f"pruning blocks older than {PRUNE_DAYS} days")

    seen = seen_load()
    while True:
        # Prune at the start of every iteration, as specified.
        prune(DEVLOG)
        prune(AGENTLOG)

        try:
            text = open(AGENTLOG).read()
        except FileNotFoundError:
            text = ""

        for ts, body in split_blocks(text):
            if ts is None:
                continue
            digest = hashlib.sha256((ts + body).encode()).hexdigest()
            if digest in seen:
                continue
            seen.add(digest)
            seen_save(seen)
            if not handle_block(ts, body):
                lock.close()
                return 0
        time.sleep(POLL)


if __name__ == "__main__":
    sys.exit(main())
