#!/usr/bin/env python3
"""Capture the development-loop screenshot set into FIXED paths, for the Development session.

    tools/devloop-shots.py                 # build, then capture the default six views
    tools/devloop-shots.py --no-build      # capture against whatever is already built
    tools/devloop-shots.py --views front,kitchen
    tools/devloop-shots.py --out /home/richard/shots

WHAT THIS IS FOR
================
A second session (the Development session) edits `Source/HIUnreal/` and then needs to SEE the
result. This is the half that runs the game and hands back pictures at stable filenames:

    /home/richard/shots/front.png
    /home/richard/shots/neighbors.png
    /home/richard/shots/kitchen.png
    /home/richard/shots/living.png
    /home/richard/shots/dining.png
    /home/richard/shots/basement.png

Stable names, overwritten every run -- a loop that accumulates `shot_2026-10-23_0042.png` makes
the reader guess which one is current.

WHY IT BUILDS FIRST, AND WHY A FAILED BUILD IS A HARD STOP
==========================================================
`AGENTS.md` records the trap: **a FAILED build silently runs the OLD binary**. A screenshot loop
that does not read the build verdict will happily hand back a frame of yesterday's game, and the
Development session will conclude its fix did nothing -- or worse, that the fix worked. So this
parses `Result: Succeeded` out of the build output and exits WITHOUT capturing if it is absent.
`--no-build` skips the build, and says in its output that the frames are of the existing binary.

WHY ONE PROCESS PER VIEW
========================
`-HIShotPos` is parsed once, at BeginPlay. A pose therefore cannot change inside a run, so each
view is its own launch. `-RenderOffscreen` needs no display session and no GPU window; measured
on this box at roughly 25-40 s a launch including engine start.

WHAT THE POSES ARE
==================
Metres, sim coordinates. North is +Y (the street); the house spans x -6.35..6.35, y -17.39..-7.75;
the front door faces +Y. These are the same numbers as `tools/capture-grid.py`, so a frame from
this script and a cell from a grid sheet are the same camera.
"""

import argparse
import json
import fcntl
import os
import shutil
import subprocess
import sys
import time

# ONE ENGINE PATH: `/data/UnrealEngine`, and nothing else resolves to the engine.
#
# The history is kept because the failure it caused looked exactly like a source bug, and a
# future reader deserves to recognise it rather than re-diagnose it. Until 2026-10-24
# `/dev/sdb1` was ALSO bind-mounted at `/home/richard/UnrealEngine` -- the same device:inode,
# so the same bytes -- but a BUILD was not indifferent to the spelling: the engine's shared PCH
# records ABSOLUTE paths, so a build through the alias failed inside any translation unit that
# includes engine Core headers, with
#   `redefinition of 'CSameAsHelper' / 'CDecaysTo' / 'TAreTypesEqual_V'`
# and -Wshadow errors on engine enumerators nobody had edited (measured 2026-10-24, three
# separate attempts). A fully-cached no-op build through the alias still printed
# "Result: Succeeded", which is what made it look intermittent -- it only bit when something
# actually compiled.
#
# RESOLVED by removing the alias rather than working around it: unmounted, the `/etc/fstab`
# line commented out, the empty directory deleted, and every tool plus the README rewritten to
# `/data/UnrealEngine`. If `/home/richard/UnrealEngine` reappears in any file, it is stale text
# from before that removal -- fix the text, do not recreate the mount.
BIN = "/data/UnrealEngine/Engine/Binaries/Linux/UnrealEditor"
BUILD = "/data/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh"
PROJ = "/data/projects/HIUnreal/HIUnreal.uproject"
SHOTDIR = "/data/projects/HIUnreal/Saved/Screenshots/LinuxEditor"
DEFAULT_OUT = "/home/richard/shots"

# name, x, y, yaw_deg, pitch_deg, floor
VIEWS = {
    # Standing on the drive, looking at the house front: the frame the round opens on.
    # FRONT IS THE ELEVATION, NOT THE FACADE SEEN EDGE-ON (2026-10-28). The old pose stood at
    # y -4 facing WEST (yaw -90), i.e. down the front wall, and never showed the house to a
    # camera -- which is how review after review "missed the front of the house". Stand in the
    # street and face -Y (yaw 180) to see the elevation across the yard.
    "front":     (0.0, 4.0, 180.0, -6.0, 0),   # in the yard, inside the trees and the van
    # The front lawn looking north across the street at the neighbours, the cars and the van.
    # Across the street, facing +Y (yaw 0) -- the neighbour houses, not the road surface.
    "neighbors": (12.0,  6.0,   3.0, -2.0, 0),   # east of the van, at the houses
    # Kitchen: counters, island, fridge.
    "kitchen":   (3.5, -14.5, 180.0, 0.0, 0),
    # Living room: sofa/armchair, the picture window wall.
    "living":    (-4.0, -10.5, 90.0, 0.0, 0),
    # Dining, with the stair flight behind it.
    "dining":    (2.6, -10.0, 180.0, 0.0, 0),
    # Basement: cinderblock, washer/dryer, workbench.
    "basement":  (0.0, -14.0, 90.0, 0.0, -1),
    # ---- the four the Master could ask for that this script did not originally shoot. The
    # project's grid harness (tools/capture-grid.py) already defines all ten poses, so a Slave
    # that can only answer six of them is a Slave that replies "unknown view" to a fair
    # request. Same numbers as the grid, so a frame from either is the same camera.
    # Back yard, looking at the rear elevation.
    "back":      (0.0, -21.0, 90.0, 0.0, 0),
    # Ground-floor master bedroom and its bathroom door.
    "master":    (-4.0, -15.5, 90.0, 0.0, 0),
    # Second floor (floor 1): bedroom 2.
    # Second floor (floor 1): bedroom 2. RE-AIMED 2026-10-28, for a measured reason: Yaw is
    # Godot's, AimDir = (sin yaw, cos yaw, -sin pitch), so yaw 180 faces -Y (SOUTH) and west is
    # yaw -90; and NEGATIVE pitch looks UP. Two "the room is empty" reports were a camera aimed at
    # a wall with the bed behind it -- the bed was at (-507,-1070,274) cm the whole time.
    "bedroom2":  (-3.2, -10.7, -90.0, 8.0, 1),
    # Second floor: office, over the desk.
    "office":    (4.0, -10.0, 180.0, 0.0, 1),
}
DEFAULT_VIEWS = ["front", "neighbors", "kitchen", "living", "dining", "basement"]

# SECONDS BEFORE THE SHUTTER FIRES, and 3 was too early -- measured, not cautious.
#
# At `-HIShotAfter=3` the player body is photographed BEFORE its textures finish streaming, and
# she renders as a featureless grey mannequin -- no navy shirt, no brown hair. That frame was
# twice reported as a game defect ("the character is white / untextured"), and it is not: the
# same build, same camera, same pose at `-HIShotAfter=12` renders her correctly. Verified
# 2026-10-24 by capturing the kitchen both ways and comparing the two frames.
#
# This is the stale-artifact trap in its purest form: nothing in the game is wrong, the
# screenshot is simply too early, and the cost of not knowing that is an entire debugging
# round spent on a texture pipeline that works. 12 s costs nine extra seconds per view.
SHUTTER_SECONDS = 12


def fail(msg):
    print(f"devloop: {msg}", file=sys.stderr)
    return 1


def editor_running():
    r = subprocess.run(["pgrep", "-x", "UnrealEditor"], capture_output=True)
    return r.returncode == 0


def build():
    """Run the game-module build. Returns True only on 'Result: Succeeded'.

    SERIALISED THROUGH A LOCK, and that is not belt-and-braces. Measured 2026-10-24: two
    builds of this project overlapped by about a minute and the second failed with a storm of
    `redefinition of 'TAreTypesEqual_V'` / `declaration shadows` errors inside
    `Core/Public/HAL/Platform.h` -- engine headers nobody had touched, in a file whose only
    fault was being compiled while the other build rewrote the same objects. The errors look
    exactly like a source bug, so a loop that hits this will "fix" code that was never
    broken. One build at a time is the whole of the fix: a second caller waits.
    """
    lock = open("/tmp/hi_devloop_build.lock", "w")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        print("devloop: another build holds the lock -- waiting ...")
        fcntl.flock(lock, fcntl.LOCK_EX)      # blocks until the other build finishes
    print("devloop: building HIUnrealEditor ...")
    t0 = time.time()
    r = subprocess.run(
        [BUILD, "HIUnrealEditor", "Linux", "Development", f"-project={PROJ}"],
        capture_output=True, text=True)
    out = (r.stdout or "") + (r.stderr or "")
    ok = "Result: Succeeded" in out
    # The full log is written for the other session to read: when a build fails, the question
    # is always "which translation unit, and with what command", and a piped summary cannot
    # answer it. (This is also how the 2026-10-24 redefinition storm was traced.)
    with open("/tmp/hi_devloop_build.log", "w") as f:
        f.write(out)
    print(f"devloop: build {'SUCCEEDED' if ok else 'FAILED'} in {time.time() - t0:.0f}s "
          f"(log: /tmp/hi_devloop_build.log)")
    if not ok:
        # The errors matter to the other session, so print them rather than swallowing them.
        lines = [ln for ln in out.splitlines() if "error" in ln.lower() or "Result:" in ln]
        for ln in lines[-15:]:
            print("  " + ln, file=sys.stderr)
    lock.close()
    return ok


def capture(name, pose, timeout):
    x, y, yaw, pitch, floor = pose
    png = os.path.join(SHOTDIR, f"devloop_{name}.png")
    if os.path.exists(png):
        os.remove(png)          # never let a previous run's frame pass for this one
    cmd = [
        BIN, PROJ, "-game", "-RenderOffscreen", "-ResX=1280", "-ResY=720",
        "-unattended", "-nopause", "-nosplash", "-log",
        f"-abslog=/tmp/hi_devloop_{name}.log",
        "-HIThieves=1", "-HINoThiefDamage", "-HIShotNoUI",
        f"-HIShot=devloop_{name}", f"-HIShotAfter={SHUTTER_SECONDS}",
        f"-HIShotPos={x},{y}", f"-HIShotYaw={yaw}", f"-HIShotPitch={pitch}",
        f"-HIShotFloor={floor}",
    ]
    t0 = time.time()
    try:
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=timeout)
    except subprocess.TimeoutExpired:
        # A run that outlives the ceiling is killed, not waited on: the harness exists to
        # answer the other session, and a hung launch answers nothing.
        subprocess.run(["pkill", "-9", "-x", "UnrealEditor"], capture_output=True)
        print(f"devloop: {name}: TIMED OUT after {timeout}s", file=sys.stderr)
        return None
    dt = time.time() - t0
    if not os.path.exists(png):
        print(f"devloop: {name}: NO FRAME written (see /tmp/hi_devloop_{name}.log)",
              file=sys.stderr)
        return None
    return png, dt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--views", default=",".join(DEFAULT_VIEWS))
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--build", dest="do_build", action="store_true", default=True)
    ap.add_argument("--no-build", dest="do_build", action="store_false")
    ap.add_argument("--take-over", action="store_true",
                    help="kill a running game; without this a capture is refused")
    ap.add_argument("--timeout", type=int, default=180,
                    help="per-launch wall-clock ceiling")
    args = ap.parse_args()

    names = [n.strip() for n in args.views.split(",") if n.strip()]
    unknown = [n for n in names if n not in VIEWS]
    if unknown:
        return fail(f"unknown view(s) {unknown}; known: {sorted(VIEWS)}")

    # A CAPTURE KILLS A RUNNING GAME. The project learned this the hard way (a sweep ended
    # somebody's round and read as a crash), so it is refused unless asked for explicitly.
    if editor_running() and not args.take_over:
        return fail("an UnrealEditor is already running -- refusing to kill it "
                    "(pass --take-over if it is yours to take)")

    if args.do_build:
        if not build():
            return fail("BUILD FAILED -- not capturing. A frame here would be the OLD binary.")
    else:
        print("devloop: --no-build, so these frames come from whatever binary is on disk now")

    os.makedirs(args.out, exist_ok=True)
    os.makedirs(SHOTDIR, exist_ok=True)

    manifest = {"started": time.strftime("%Y-%m-%dT%H:%M:%S"),
                "binary": BIN, "views": {}}
    built_all = True
    for name in names:
        print(f"devloop: capturing {name} ...")
        got = capture(name, VIEWS[name], args.timeout)
        if got is None:
            built_all = False
            manifest["views"][name] = {"ok": False}
            continue
        png, dt = got
        dest = os.path.join(args.out, f"{name}.png")
        # INVALIDATE THE CONSUMER'S "ALREADY ANALYSED" MARKER BEFORE OVERWRITING.
        #
        # The watcher on the other side marks each file it has read as `<name>.png.done` and
        # then skips it. Measured 2026-10-24: after a second capture wrote front.png at 03:58,
        # front.png.done was still dated 03:45 -- the new frame would NEVER have been looked
        # at, and the loop would have gone quiet while appearing to run. Fixed filenames are
        # right (a consumer should not have to guess which frame is current), so the marker
        # has to be cleared by the writer. Deleting it is safe: the worst case is one extra
        # analysis of a frame, never a missed one.
        marker = dest + ".done"
        try:
            os.remove(marker)
        except FileNotFoundError:
            pass
        shutil.copyfile(png, dest)
        size = os.path.getsize(dest)
        print(f"  -> {dest}  ({size // 1024} KiB, {dt:.0f}s)")
        manifest["views"][name] = {"ok": True, "path": dest, "bytes": size,
                                   "seconds": round(dt, 1),
                                   "pose": VIEWS[name]}

    # A manifest is what lets the other session tell a fresh capture from a stale one without
    # trusting a timestamp it cannot see.
    with open(os.path.join(args.out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)

    print(f"devloop: {sum(1 for v in manifest['views'].values() if v['ok'])}/{len(names)} "
          f"frames in {args.out}")
    return 0 if built_all else 1


if __name__ == "__main__":
    sys.exit(main())
