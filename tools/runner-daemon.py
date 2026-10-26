#!/usr/bin/env python3
"""The Game Runner Agent (DeepSeek Flash) -- THE SLAVE. There is exactly one, and it is this.

    Session: "Game Runner Agent DeepSeek Flash". One slave, one role.

CHANNELS -- fixed by the Master session
    /home/richard/shots/DevToRunner.txt   the Master writes COMMANDS here. We only read.
    /home/richard/shots/RunnerToDev.txt    we write the RESULT of the last command.
    /home/richard/shots/AgentLog.txt       the SLAVE'S diary (audit only). We append to it.
    /home/richard/shots/DevLog.txt         the MASTER'S diary. We never write it; we only
                                           prune it.

    NOTE: an earlier instruction had these two diaries the other way round (DevLog = slave's
    actions, AgentLog = master's replies). The Master's mapping is the one in force. The result
    channel is a single file overwritten per command, so "what happened last" is never ambiguous.

    Do not start `tools/vl-loop.py`. It is an older script that ALSO watches DevToRunner.txt and
    ALSO captures, so running it alongside this one gives two processes fighting over the same
    GPU and killing each other's UnrealEditor by name. It is not the slave; this is.

TIMESTAMPS ARE UTC, and that is a fix rather than a preference. Measured 2026-10-25: vl-loop.py
writes UTC while this daemon wrote local time, so one log held both bases and the entries at
"07:50:38" (local) and "11:50:34" (UTC) were THE SAME MOMENT four hours apart in the file.
A shared log whose rows are not on one clock cannot be ordered, and a 7-day prune compares
timestamps. UTC on every line, matching the peer.

COMMANDS
    ACTION: take_screenshot <view>     views: front neighbors kitchen living dining basement
    FIX: <patch>                       applied only if it verifies (see below)
    STATUS: ✅                          stop the runner
    (bare `analyze <view>.png` is accepted too, as the Master's wording)

WHY A FIX IS GATED
    An unattended loop that applies another process's patches verbatim destroys a working tree
    the first time that process is wrong, and it was wrong on 2026-10-24: `1452s/Wood/DarkSteel/`
    pointed at a line that had moved, "hedges at y=-12.5" is INSIDE the house, and
    `778,807d; r .../nicehouse.patch` would have deleted 30 lines of PlaceSketchfabModel and the
    patch file did not exist. So: a line-targeted edit must find its text; range deletes, file
    includes and missing paths are refused; a failed build reverts the edit. Blocks are processed
    once, keyed by a persisted hash, so a restart cannot re-apply yesterday's patch.

A CAPTURE NEVER BUILDS. It warns when Source/ is newer than the built game module, because
otherwise the Master would analyse a frame of the PREVIOUS build and conclude the fix did nothing.
"""

import fcntl
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timedelta, timezone

SHOTS = "/home/richard/shots"
CMD = os.path.join(SHOTS, "DevToRunner.txt")        # Master -> us (read only)
RESULT = os.path.join(SHOTS, "RunnerToDev.txt")     # us -> Master (overwrite per command)
DIARY = os.path.join(SHOTS, "AgentLog.txt")         # our audit trail
MASTERLOG = os.path.join(SHOTS, "DevLog.txt")       # Master's diary: prune only, never write
RUNLOG = os.path.join(SHOTS, "runner.log")          # operator log
LASTCMD = os.path.join(SHOTS, ".runner_lastcmd")   # key of the last command generation
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
    """UTC, matching tools/vl-loop.py -- see the timestamp note in the module docstring."""
    return datetime.now(timezone.utc).strftime(TS_FMT)


def runlog(msg):
    line = f"{now()} {msg}"
    print(line, flush=True)
    with open(RUNLOG, "a") as f:
        f.write(line + "\n")


def diary(body):
    """Append one timestamped audit block to AgentLog.txt (the slave's diary)."""
    with open(DIARY, "a") as f:
        f.write(f"===Timestamp: {now()}===\n{body.rstrip()}\n\n")
    runlog("diary: " + body.replace("\n", " | ")[:160])


def result(text):
    """Overwrite RunnerToDev.txt: the answer to the command just handled."""
    with open(RESULT, "w") as f:
        f.write(text.rstrip() + "\n")
    runlog("result: " + text.replace("\n", " | ")[:160])


def split_blocks(text):
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
    cutoff = datetime.now(timezone.utc) - timedelta(days=PRUNE_DAYS)
    keep, dropped = [], 0
    for ts, body in split_blocks(text):
        if ts is None:
            keep.append((ts, body))
            continue
        try:
            when = datetime.strptime(ts, TS_FMT).replace(tzinfo=timezone.utc)
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
        runlog(f"pruned {dropped} block(s) older than {PRUNE_DAYS} d from "
               f"{os.path.basename(path)}")
    return dropped


def lastcmd_load():
    """Key of the last command generation already handled (survives a restart)."""
    try:
        return json.load(open(LASTCMD)).get("key", "")
    except Exception:
        return ""


def lastcmd_save(key):
    json.dump({"key": key, "at": now()}, open(LASTCMD, "w"))


def module_is_stale():
    """(stale, why) -- Source/ newer than the built game module means a frame of the OLD build."""
    so = os.path.join(PROJECT, "Binaries", "Linux", "libUnrealEditor-HIUnreal.so")
    if not os.path.exists(so):
        return True, "no game module has been built"
    so_t = os.path.getmtime(so)
    newest_t, newest_p = 0.0, ""
    for root, _dirs, files in os.walk(os.path.join(PROJECT, "Source")):
        for f in files:
            if f.endswith((".cpp", ".h")):
                p = os.path.join(root, f)
                try:
                    t = os.path.getmtime(p)
                except OSError:
                    continue
                if t > newest_t:
                    newest_t, newest_p = t, p
    if newest_t > so_t + 1.0:
        return True, f"{os.path.relpath(newest_p, PROJECT)} is newer than the built module"
    return False, ""


def capture(view):
    dst = os.path.join(SHOTS, f"{view}.png")
    before = os.path.getmtime(dst) if os.path.exists(dst) else 0
    t0 = time.time()
    try:
        r = subprocess.run([sys.executable, DEVS, "--no-build", "--views", view],
                           cwd=PROJECT, capture_output=True, text=True, timeout=600)
    except subprocess.TimeoutExpired:
        diary(f"ACTION: capture {view} -- TIMEOUT after 600s")
        result(f"FAILED: capture {view} timed out")
        return False
    secs = time.time() - t0
    ok = r.returncode == 0 and os.path.exists(dst) and os.path.getmtime(dst) > before
    if ok:
        stale, why = module_is_stale()
        warn = f" WARNING: STALE BINARY -- {why}" if stale else ""
        diary(f"ACTION: capture {view} -- 1280x720, {secs:.0f}s, READY{warn}")
        result(f"READY: {view}.png" + (f"\nWARNING: STALE BINARY -- {why}" if stale else ""))
    else:
        diary(f"ACTION: capture {view} -- FAILED rc={r.returncode} after {secs:.0f}s")
        result(f"FAILED: could not capture {view}")
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
    diary(f"ACTION: build -- {'Succeeded' if ok else 'FAILED'} in {time.time() - t0:.0f}s")
    return ok


def _line_of(path, n):
    with open(path) as f:
        lines = f.read().splitlines(keepends=True)
    if n < 1 or n > len(lines):
        return None, lines
    return lines[n - 1], lines


def apply_verified(text):
    """Apply a FIX only if it verifies. Returns (status, detail)."""
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
    if not edits[0][2].strip() and not edits[0][3].strip():
        return "blocked", "empty FIX (no old/new text); nothing to apply"

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
        diary(f"ACTION: edit {os.path.relpath(path, PROJECT)}:{n} -- {old!r} -> {new!r}")

    if not rebuild():
        for rel in changed:
            subprocess.run(["git", "checkout", "--", rel], cwd=PROJECT, capture_output=True)
        return "blocked", f"build FAILED after editing {', '.join(changed)}; reverted"
    return "fixed", f"applied to {', '.join(changed)}; build succeeded"


def handle_command(text):
    """Handle one command from DevToRunner.txt. Returns False to stop the runner."""
    if re.search(r"STATUS:\s*✅", text):
        diary("ACTION: stop -- STATUS done received")
        result("DONE: runner stopped")
        return False

    m = re.search(r"(?:ACTION:\s*take_screenshot|analyze)\s+(\S+)", text)
    if m:
        view = m.group(1).strip().removesuffix(".png")
        if view not in VIEWS:
            diary(f"ACTION: capture {view} -- UNKNOWN VIEW (known: {sorted(VIEWS)})")
            result(f"FAILED: unknown view {view}")
            return True
        capture(view)
        return True

    if re.search(r"^\s*FIX:", text, re.M):
        status, detail = apply_verified(text)
        diary(f"ACTION: fix -- {'FIXED: ' if status == 'fixed' else 'BLOCKED: '}{detail}")
        result(("FIXED: " if status == "fixed" else "BLOCKED: ") + detail)
        return True

    return True


def main():
    lock = open(LOCK, "w")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        print("runner: another runner holds the lock", file=sys.stderr)
        return 1

    os.makedirs(SHOTS, exist_ok=True)
    for p in (CMD, RESULT, DIARY, MASTERLOG, RUNLOG):
        open(p, "a").close()
    runlog(f"runner started (pid {os.getpid()}); cmd={CMD} result={RESULT} diary={DIARY}")
    diary(f"ACTION: runner online -- polling DevToRunner.txt every {POLL:.0f}s (UTC timestamps)")
    # ONE SLAVE. The flock above already guarantees a single copy of THIS runner; the only
    # other process that could act as one is the old tools/vl-loop.py, which nothing starts.
    # (An earlier version of this file warned about it as a "competing slave", which was the
    # wrong model: there is one slave -- this session -- and vl-loop.py is a leftover script.)

    last_key = lastcmd_load()
    while True:
        prune(MASTERLOG)
        prune(DIARY)

        # THE COMMAND FILE IS A SINGLE SLOT, AND ITS CONTENT IS ALLOWED TO REPEAT.
        #
        # Deduping on the command TEXT was wrong and would have hung the loop on its most
        # common step: after a fix the Master re-requests the SAME view, and the rewritten
        # file is byte-identical to the previous one. Measured 2026-10-25 -- two identical
        # `ACTION: take_screenshot bogusview` writes produced ONE result, because the second
        # was silently deduped; the Master would then poll RunnerToDev.txt for a change that
        # never comes.
        #
        # The write is the signal, not the text. Key on the file's content AND its mtime, so a
        # rewrite counts as a new command while an idle poll does not. Persisted, so a restart
        # does not re-run the last command either.
        try:
            content = open(CMD, "rb").read()
            key = hashlib.sha256(content + str(os.stat(CMD).st_mtime_ns).encode()).hexdigest()
        except FileNotFoundError:
            content, key = b"", ""
        if key != last_key:
            last_key = key
            lastcmd_save(key)
            for line in content.decode(errors="replace").splitlines():
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                runlog(f"command: {line[:120]}")
                if not handle_command(line):
                    lock.close()
                    return 0
        time.sleep(POLL)


if __name__ == "__main__":
    sys.exit(main())
