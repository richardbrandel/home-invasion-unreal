#!/usr/bin/env python3
"""Tile a capture series into ONE image, so a whole sweep can be judged in one look.

    tools/contact-sheet.py shots/sweep_sheet.png shots/sweep_*.png

WHY THIS IS NOT A LUXURY. A capture series is 4-12 PNGs, and looking at each one separately
costs both time and tokens -- which means the series gets SAMPLED, three frames read and the
rest guessed at. That is not a hypothetical: bug 15 in README.md is a gun that pointed 49
degrees off the crosshair while walking, and it survived two rounds of "verified" fixes because
every frame anyone looked at was a standing one. One sheet is the difference between looking at
every pose and looking at the convenient ones.

Needs Pillow. It runs on the BUILD BOX, where the frames already are, so the only thing that
crosses the network is the sheet.
"""

from __future__ import annotations

import os
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:  # pragma: no cover - the message is the point
    print(
        "contact-sheet: Pillow is not installed. On the build box it already is; "
        "on the Mac it is not, which is why this is run there.",
        file=sys.stderr,
    )
    raise SystemExit(2)

# Tile width and column count are environment-overridable so a 12-frame sweep does not have to
# become a 12-across strip nobody can see.
TILE_W = int(os.environ.get("SHEET_TILE_W", "640"))
COLS = int(os.environ.get("SHEET_COLS", "3"))
LABEL_H = 24
BG = (22, 22, 26)


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print("usage: contact-sheet.py OUT.png IN1.png [IN2.png ...]", file=sys.stderr)
        return 2
    out = argv[1]
    files = [f for f in argv[2:] if os.path.isfile(f)]
    if not files:
        print("contact-sheet: no input frames exist", file=sys.stderr)
        return 1

    tiles = []
    for path in files:
        with Image.open(path) as im:
            im = im.convert("RGB")
            w, h = im.size
            tiles.append((os.path.basename(path), im.resize((TILE_W, max(1, round(h * TILE_W / w))), Image.LANCZOS)))

    rows = (len(tiles) + COLS - 1) // COLS
    row_h = max(im.size[1] for _, im in tiles) + LABEL_H
    sheet = Image.new("RGB", (COLS * TILE_W, rows * row_h), BG)
    draw = ImageDraw.Draw(sheet)

    for idx, (name, im) in enumerate(tiles):
        x = (idx % COLS) * TILE_W
        y = (idx // COLS) * row_h
        # The filename is the label AND the frame's identity: it carries the sim second if the
        # capture series named it that way, which is what makes the sheet readable later.
        draw.text((x + 8, y + 6), name, fill=(232, 232, 232))
        sheet.paste(im, (x, y + LABEL_H))

    sheet.save(out)
    print(f"contact-sheet: {len(tiles)} frame(s), {COLS} across -> {out} ({sheet.size[0]}x{sheet.size[1]})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
