#!/usr/bin/env python3
"""Golden-image diff for the scripted tour.

    python3 tools/tour-diff.py [--shots DIR] [--baseline DIR] [--update]

WHY THIS AND NOT A VISION MODEL. The obvious idea for "find obvious bugs automatically"
is a local vision-language model that looks at a frame and says what is wrong. It was
tried, and it does not work: `mlx-community/Qwen2.5-VL-3B-Instruct-4bit` was shown a
SOLID MAGENTA FRAME -- not a house, not a screenshot, one flat colour -- and answered
"Nothing Wrong". It is a rubber stamp on an open-ended "what is wrong here?" question,
which is the same "check that cannot fail" shape this project keeps having to guard
against. It can DESCRIBE a frame accurately (it read the HUD back correctly), so it is
useful as a narrator; it is not usable as a judge.

A diff needs no judgement. It cannot tell you what is CORRECT, but it can tell you what
CHANGED -- and "the thing the intruder carried got huge" is exactly a change. Capture the
tour, compare against an approved set, and look only at what moved. That is the part of a
playtester's eye that can be mechanised cheaply and honestly, and it is why this file
exists instead of a prompt.

WHAT IT PRINTS. Per frame: unchanged, or changed with the bounding box of the change and
the fraction of pixels affected. With --update it (re)writes the baseline.

Exit code is 1 if anything differs, so it can gate a build.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image, ImageChops
except ImportError:  # pragma: no cover
    sys.exit("tour-diff: Pillow is required (it is present in ~/.dsh/tools/.embed-venv)")

# A pixel counts as changed only if it moved by more than this in any channel. Antialiasing
# and TAA jitter make a strict equality test useless: every frame differs from every other
# by a few units per pixel.
PIXEL_THRESHOLD = 12
# And a frame counts as changed only if more than this fraction of its pixels moved, so a
# single dithered pixel does not raise a flag.
AREA_THRESHOLD = 0.002


def load(path: Path) -> Image.Image:
    return Image.open(path).convert("RGB")


def diff_region(a: Image.Image, b: Image.Image, out: Path) -> tuple[float, tuple[int, int, int, int] | None]:
    """Return (fraction changed, bounding box of the change) and write a visual diff."""
    delta = ImageChops.difference(a, b).convert("L")
    mask = delta.point(lambda v: 255 if v > PIXEL_THRESHOLD else 0)
    bbox = mask.getbbox()
    changed = sum(mask.histogram()[255:]) / float(a.width * a.height)

    # Red where it changed, over the new frame: a person can see WHAT moved without a
    # side-by-side. Written next to the frames rather than into the repo.
    overlay = b.copy()
    red = Image.new("RGB", b.size, (255, 0, 0))
    overlay.paste(red, (0, 0), mask)
    overlay.save(out)
    return changed, bbox


def main() -> int:
    here = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser()
    ap.add_argument("--shots", default=str(here.parent / "shots"))
    ap.add_argument("--baseline", default=str(here.parent / "shots" / "baseline"))
    ap.add_argument("--update", action="store_true",
                    help="accept the current frames as the new baseline")
    args = ap.parse_args()

    shots = Path(args.shots)
    base = Path(args.baseline)
    frames = sorted(shots.glob("tour_*.png"))
    if not frames:
        print(f"tour-diff: no tour_*.png in {shots}", file=sys.stderr)
        return 2

    if args.update:
        base.mkdir(parents=True, exist_ok=True)
        for f in frames:
            (base / f.name).write_bytes(f.read_bytes())
        print(f"tour-diff: baseline updated, {len(frames)} frames -> {base}")
        return 0

    if not base.exists():
        print(f"tour-diff: no baseline at {base}. Run with --update once to accept the "
              f"current tour as the reference, then re-run after each change.", file=sys.stderr)
        return 2

    changed_frames = 0
    for f in frames:
        ref = base / f.name
        if not ref.exists():
            print(f"  NEW      {f.name}  (not in the baseline)")
            changed_frames += 1
            continue
        a, b = load(ref), load(f)
        if a.size != b.size:
            print(f"  RESIZED  {f.name}  {a.size} -> {b.size}")
            changed_frames += 1
            continue
        changed, bbox = diff_region(a, b, shots / f"diff_{f.name}")
        if changed > AREA_THRESHOLD:
            print(f"  CHANGED  {f.name}  {changed * 100:.2f}% of pixels, region {bbox}"
                  f"  -> {shots / f'diff_{f.name}'}")
            changed_frames += 1
        else:
            print(f"  same     {f.name}  ({changed * 100:.3f}%)")

    print(f"tour-diff: {changed_frames} of {len(frames)} frames differ from the baseline")
    return 1 if changed_frames else 0


if __name__ == "__main__":
    raise SystemExit(main())
