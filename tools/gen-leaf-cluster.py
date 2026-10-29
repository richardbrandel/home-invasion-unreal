#!/usr/bin/env python3
"""Generate SourceAssets/textures/leaf_cluster.png -- the foliage sprite the hedge cards use.

   python3 tools/gen-leaf-cluster.py [out.png]

There is no foliage art in either asset pack this project draws from (KayKit has buildings and
one potted plant; Poly Haven's shrubs are prostrate or bare), so the hedge card texture is drawn:
58 pointed leaves in varied greens on transparent, overlapping from a centre so the card reads as
a clump rather than as one leaf. Seeded, so a rebuild gives the same sprite and a capture diff
does not move.

Tune `LEAVES`, the green range, and the size range here rather than in an image editor -- the
point of generating it is that it can be regenerated.
"""
import math
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

SIZE = 512
LEAVES = 58
SEED = 7


def _leaf(draw, cx, cy, length, width, angle, colour, edge):
    """A pointed leaf: a lens profile rotated about its base, filled and outlined."""
    pts = []
    n = 14
    for i in range(n + 1):
        t = i / n
        w = math.sin(math.pi * t) ** 0.65
        pts.append((t * length, w * width * 0.5))
    poly = pts + [(-length + x, -y) for (x, y) in reversed(pts[:-1])]
    ca, sa = math.cos(angle), math.sin(angle)
    draw.polygon([(cx + px * ca - py * sa, cy + px * sa + py * ca) for (px, py) in poly],
                 fill=colour, outline=edge)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "SourceAssets/textures/leaf_cluster.png"
    random.seed(SEED)
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    for _ in range(LEAVES):
        ang = random.uniform(0, math.tau)
        rad = random.uniform(0, SIZE * 0.30)
        cx = SIZE * 0.5 + math.cos(ang) * rad
        cy = SIZE * 0.5 + math.sin(ang) * rad
        length = random.uniform(SIZE * 0.16, SIZE * 0.30)
        width = length * random.uniform(0.42, 0.62)
        g = random.randint(86, 150)
        r = int(g * random.uniform(0.45, 0.72))
        b = int(g * random.uniform(0.35, 0.55))
        _leaf(draw, cx, cy, length, width, random.uniform(0, math.tau),
              (r, g, b, 255), (max(0, r - 35), max(0, g - 45), max(0, b - 20), 255))
    # Soften the outer silhouette so a card edge is not a razor line.
    img.putalpha(img.split()[3].filter(ImageFilter.GaussianBlur(0.8)))
    img.save(out)
    opaque = sum(1 for p in img.getdata() if p[3] > 128) / (SIZE * SIZE)
    print("wrote %s (%dx%d, %.0f%% opaque)" % (out, SIZE, SIZE, opaque * 100))


if __name__ == "__main__":
    main()
