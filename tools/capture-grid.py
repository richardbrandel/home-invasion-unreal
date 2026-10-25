#!/usr/bin/env python3
"""Render a numbered grid of fixed camera views of Home Invasion.

One -game launch per view (the shot harness takes a single pose per run), then the
frames are assembled into one labelled sheet so Richard can point at "cell 3" instead
of hunting through shots/.

Usage:  tools/capture-grid.py [--prefix NAME] [--cell WxH] [--out PATH]
Edit VIEWS below to change the camera set.
"""
import argparse
import math
import os
import subprocess
import sys

BIN = "/data/UnrealEngine/Engine/Binaries/Linux/UnrealEditor"
PROJ = "/data/projects/HIUnreal/HIUnreal.uproject"
SHOTDIR = "/data/projects/HIUnreal/Saved/Screenshots/LinuxEditor"

# Seconds before the shutter fires. 3 was too early: the player's textures are still streaming
# at that point and she photographs as a grey mannequin, which was twice reported as a game
# defect. Same build and camera at 12 s renders her correctly (measured 2026-10-24).
SHUTTER_SECONDS = 12

# name, x, y, yaw, pitch, floor. North is +Y (the street); the house spans
# x -6.35..6.35, y -17.39..-7.75. Front door faces +Y.
VIEWS = [
    ("1_front",     0.0,  -4.0, -90, 0,  0),   # looking south at the house front
    ("2_street",    0.0,  -1.0,  90, 0,  0),   # looking north at the van/cars/houses
    ("3_back",      0.0, -21.0,  90, 0,  0),   # looking north at the back yard
    ("4_living",   -4.0, -10.5,  90, 0,  0),   # ground, living room (sofa/armchair)
    ("5_kitchen",   3.5, -14.5, 180, 0,  0),   # ground, kitchen (fridge/island)
    ("6_master",   -4.0, -15.5,  90, 0,  0),   # ground, master bedroom + bath
    ("7_dining",    2.6, -10.0, 180, 0,  0),   # ground, dining set
    ("8_bedroom2", -4.0, -11.5,  90, 0,  1),   # second, bedroom 2
    ("9_office",    4.0, -10.0, 180, 0,  1),   # second, office/bookshelf
    ("10_basement", 0.0, -14.0,  90, 0, -1),   # basement (washer/dryer/workbench)
]


def capture(prefix, views):
    for name, x, y, yaw, pitch, floor in views:
        shot = f"{prefix}_{name}" if prefix else name
        png = os.path.join(SHOTDIR, f"{shot}.png")
        if os.path.exists(png):
            os.remove(png)
        cmd = [BIN, PROJ, "-game", "-RenderOffscreen", "-ResX=1280", "-ResY=720",
               "-unattended", "-nopause", "-nosplash", "-log",
               f"-abslog=/tmp/hi_grid_{name}.log",
               "-HIThieves=1", "-HINoThiefDamage", "-HIShotNoUI",
               f"-HIShot={shot}", f"-HIShotAfter={SHUTTER_SECONDS}",
               f"-HIShotPos={x},{y}", f"-HIShotYaw={yaw}", f"-HIShotPitch={pitch}",
               f"-HIShotFloor={floor}"]
        r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if not os.path.exists(png):
            print(f"  MISSING {shot}.png (exit {r.returncode})", file=sys.stderr)
        else:
            print(f"  {shot}.png")


def assemble(prefix, cell, out, views):
    from PIL import Image, ImageDraw, ImageFont
    frames = []
    for name, *_ in views:
        shot = f"{prefix}_{name}" if prefix else name
        p = os.path.join(SHOTDIR, f"{shot}.png")
        if os.path.exists(p):
            frames.append((name, p))
    if not frames:
        print("no frames to assemble", file=sys.stderr)
        return 1
    cols = math.ceil(math.sqrt(len(frames)))
    rows = math.ceil(len(frames) / cols)
    cw, ch = cell
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 22)
    except Exception:
        font = ImageFont.load_default()
    sheet = Image.new("RGB", (cols * cw, rows * ch), (18, 18, 20))
    draw = ImageDraw.Draw(sheet)
    for i, (name, p) in enumerate(frames):
        im = Image.open(p).convert("RGB").resize((cw, ch))
        r, c = divmod(i, cols)
        sheet.paste(im, (c * cw, r * ch))
        label = f"{i + 1}  {name.split('_', 1)[-1]}"
        bbox = draw.textbbox((0, 0), label, font=font)
        tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
        pad = 6
        x0, y0 = c * cw + pad, r * ch + pad
        draw.rectangle([x0 - 2, y0 - 2, x0 + tw + 4, y0 + th + 4], fill=(0, 0, 0, 190))
        draw.text((x0, y0), label, fill=(255, 255, 0), font=font)
    sheet.save(out)
    print(f"grid -> {out}")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prefix", default="grid")
    ap.add_argument("--cell", default="512x288")
    ap.add_argument("--out", default="/data/projects/HIUnreal/shots/grid.png")
    # --only recaptures a subset, matched as a substring of the view name ("living"), so an
    # iteration that changed one room does not pay ten engine launches to look at it.
    ap.add_argument("--only", default="", help="comma-separated view-name substrings")
    args = ap.parse_args()
    w, h = (int(v) for v in args.cell.split("x"))
    views = VIEWS
    if args.only:
        keys = [k.strip() for k in args.only.split(",") if k.strip()]
        views = [v for v in VIEWS if any(k in v[0] for k in keys)]
        if not views:
            print(f"--only {args.only!r} matched no view in {[v[0] for v in VIEWS]}",
                  file=sys.stderr)
            sys.exit(2)
    print(f"capturing {len(views)} views ...")
    capture(args.prefix, views)
    print("assembling ...")
    sys.exit(assemble(args.prefix, (w, h), args.out, views))


if __name__ == "__main__":
    main()
