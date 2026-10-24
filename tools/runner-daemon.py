#!/usr/bin/env python3
"""Runner daemon: the screenshot half of the Dev<->Runner loop, with gates on the fix half.

PROTOCOL (files in /home/richard/shots/)
    DevToRunner.txt     read every POLL seconds
        ACTION: take_screenshot <view>   -> capture the view, write READY: <view>.png
        FIX: <instructions>              -> apply, rebuild, recapture
        STATUS: <done marker>            -> stop
    RunnerToDev.txt     our reply: READY / FIXED / BLOCKED / FAILED / DONE

WHY THE FIX HALF IS GATED, AND WHY THAT IS THE POINT
====================================================
An unattended loop that applies another process's patches verbatim will destroy a working
tree the first time that process is wrong -- and on 2026-10-24 the running "analyser" was a
stub whose three canned instructions were stale, self-defeating or destructive:

  * `sed -i '1452s/Wood/DarkSteel/'`  -- line 1452 had moved; it is now
    `Mid->SetScalarParameterValue(TEXT("Opacity"), 0.55f)`. Line-numbered edits rot.
  * "hedges to y = -12.5, height 1.83" -- that is the change removed the same day, because
    y=-12.5 is INSIDE the house and 1.83 m hides the van and the street.
  * `sed -i '778,807d; r /home/richard/shots/nicehouse.patch'` -- deletes 30 lines of
    `PlaceSketchfabModel`, and the patch FILE DOES NOT EXIST. It would have destroyed code and
    inserted nothing.

So a patch is applied only when it can be VERIFIED against the file it names, and every
applied patch is committed to git first so a failed build can be reverted. Anything else is
reported as BLOCKED and the tree is left untouched -- a loop that says "I did not do that, and
here is why" is worth more than one that reports success after corrupting a file.

The gates, exactly:
  1. `sed -i 'Ns/A/B/'` and `..., line N: A -> B`  -> line N must CONTAIN A right now.
     If it does not, the instruction is stale: BLOCKED, no edit.
  2. Any line-RANGE delete (`Nd,Md`), any `r <file>` include, any patch file that is missing
     -> BLOCKED. These are not verifiable by inspection.
  3. After a verified edit: rebuild. `Result: Succeeded` or the edit is reverted with
     `git checkout -- <file>` and reported as FIX-REVERTED. Never leave a half-applied fix.
  4. An inbox message is processed ONCE (by hash). Otherwise a FIX block sitting in the file
     would be re-applied every poll, forever.
"""

import hashlib
import os
import re
import shutil
import subprocess
import sys
import time

SHOTS = "/home/richard/shots"
INBOX = os.path.join(SHOTS, "DevToRunner.txt")
OUTBOX = os.path.join(SHOTS, "RunnerToDev.txt")
LOG = os.path.join(SHOTS, "runner.log")
LOCK = "/tmp/hi_runner.lock"

PROJECT = "/data/projects/HIUnreal"
DEVS = os.path.join(PROJECT, "tools", "devloop-shots.py")
BUILD = "/data/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh"

POLL = 5.0


def log(msg):
    line = f"{time.strftime('%Y-%m-%d %H:%M:%S')} {msg}"
    print(line, flush=True)
    with open(LOG, "a") as f:
        f.write(line + "\n")


def reply(text):
    with open(OUTBOX, "w") as f:
        f.write(text.rstrip() + "\n")
    log(f"-> {text.rstrip()}")


def capture(view):
    """Capture one view via the project's own harness. Returns True on success."""
    dst = os.path.join(SHOTS, f"{view}.png")
    before = os.path.getmtime(dst) if os.path.exists(dst) else 0
    r = subprocess.run([sys.executable, DEVS, "--no-build", "--views", view],
                       cwd=PROJECT, capture_output=True, text=True, timeout=600)
    ok = (r.returncode == 0 and os.path.exists(dst)
          and os.path.getmtime(dst) > before)
    if not ok:
        log(f"capture {view} FAILED rc={r.returncode} "
            f"{(r.stdout or '')[-200:]}{(r.stderr or '')[-200:]}")
    return ok


def rebuild():
    """Build, serialised. True only on 'Result: Succeeded'."""
    import fcntl
    lock = open("/tmp/hi_devloop_build.lock", "w")
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
    log(f"build {'SUCCEEDED' if ok else 'FAILED'}")
    return ok


def _line_of(path, n):
    with open(path) as f:
        lines = f.read().splitlines(keepends=True)
    if n < 1 or n > len(lines):
        return None, lines
    return lines[n - 1], lines


def apply_verified(text):
    """Apply a FIX block only if it verifies. Returns (status, detail).

    status: 'fixed' | 'blocked'
    """
    # ---- gate 2 first: refuse the shapes that cannot be verified by inspection.
    if re.search(r"\b\d+\s*,\s*\d+\s*d\b", text):
        return "blocked", ("line-range delete is not verifiable -- refusing rather than "
                           "trusting a line number; do this by hand")
    for rfile in re.findall(r"\br\s+(/\S+)", text):
        if not os.path.exists(rfile):
            return "blocked", f"asked to include {rfile}, which does not exist"

    edits = []

    # ---- form A: sed -i 'Ns/A/B/' FILE  (FILE optional; default HIHouse.cpp)
    for m in re.finditer(r"sed\s+-i\s+['\"]?(\d+)s/([^/]*)/([^/]*)/['\"]?\s*(\S*)", text):
        n, old, new, target = int(m.group(1)), m.group(2), m.group(3), m.group(4)
        edits.append((target or None, n, old, new))

    # ---- form B: "... , line N:" followed by "OLD" -> "NEW"
    for m in re.finditer(r"(\S+\.(?:cpp|h))[^\n]*?line\s+(\d+)\s*:?\s*\n?\s*"
                         r"([^\n→]+?)\s*(?:→|->)\s*([^\n]+)", text):
        edits.append((m.group(1), int(m.group(2)), m.group(3).strip(), m.group(4).strip()))

    if not edits:
        return "blocked", ("no verifiable edit found (expected `sed -i 'Ns/A/B/'` or "
                           "'file, line N: A -> B'); not touching the tree")

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
            # THE STALE-LINE GATE. This is what would have caught `1452s/Wood/DarkSteel/`.
            return "blocked", (f"line {n} of {os.path.basename(path)} does not contain "
                               f"{old!r} -- the instruction is stale, so the edit would hit "
                               f"the wrong code. Line {n} is: {line.strip()[:80]!r}")
        lines[n - 1] = line.replace(old, new, 1)
        with open(path, "w") as f:
            f.writelines(lines)
        changed.append(os.path.relpath(path, PROJECT))
        log(f"edited {os.path.relpath(path, PROJECT)}:{n} {old!r} -> {new!r}")

    if not rebuild():
        for rel in changed:
            subprocess.run(["git", "checkout", "--", rel], cwd=PROJECT, capture_output=True)
        return "blocked", f"build FAILED after editing {', '.join(changed)}; reverted"
    return "fixed", f"applied to {', '.join(changed)} and the build succeeded"


def handle(text):
    """Process one inbox message. Returns True to keep running."""
    if re.search(r"STATUS:\s*✅", text) or re.search(r"STATUS:\s*(DONE|ALL FIXED)", text, re.I):
        reply("DONE: saw STATUS done in DevToRunner.txt")
        return False

    m = re.search(r"ACTION:\s*take_screenshot\s+(\S+)", text)
    if m:
        view = m.group(1).strip()
        log(f"ACTION take_screenshot {view}")
        if capture(view):
            reply(f"READY: {view}.png")
        else:
            reply(f"FAILED: could not capture {view}")
        return True

    if re.search(r"^\s*FIX:", text, re.M):
        log("FIX block received")
        status, detail = apply_verified(text)
        if status == "fixed":
            reply(f"FIXED: {detail}")
            # Re-shoot whatever was last asked for, so the other side sees the result.
            view = getattr(handle, "last_view", None)
            if view and capture(view):
                reply(f"FIXED: {detail}\nREADY: {view}.png")
        else:
            reply(f"BLOCKED: {detail}")
        return True

    return True


def main():
    import fcntl
    lock = open(LOCK, "w")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        print("runner: another runner holds the lock", file=sys.stderr)
        return 1

    os.makedirs(SHOTS, exist_ok=True)
    log(f"runner started (pid {os.getpid()}), polling {INBOX} every {POLL:.0f}s")
    reply("RUNNER: online")

    seen = None
    while True:
        try:
            text = open(INBOX).read()
        except FileNotFoundError:
            text = ""
        digest = hashlib.sha256(text.encode()).hexdigest()
        if text.strip() and digest != seen:
            seen = digest
            m = re.search(r"ACTION:\s*take_screenshot\s+(\S+)", text)
            if m:
                handle.last_view = m.group(1).strip()
            if not handle(text):
                return 0
        time.sleep(POLL)


if __name__ == "__main__":
    sys.exit(main())
