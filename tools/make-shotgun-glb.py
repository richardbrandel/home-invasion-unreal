#!/usr/bin/env python3
"""Generate the shotgun the README's asset list asked for -- from TEXT, headlessly.

    python3 tools/make-shotgun-glb.py [output.glb]
    (default output: SourceAssets/weapons/shotgun.glb -- then tools/import-assets.sh
     turns it into /Game/HIImported/weapons/shotgun, like every other dropped file)

WHY THIS EXISTS
---------------
The source-asset list came down to two items: the Mixamo characters (landed and wired
2026-10-13) and a shotgun mesh, "if the primitive one is not good enough". The list's own
verdict on the primitive one: *build it in code -- every prop in this port is already
composed from primitives; a shotgun from three boxes is free; only the last 10% needs an
artist.* Until this script the game shipped the ENGINE'S RIFLE standing in for the shotgun
(`SM_Rifle`), which was disclosed in code but still a rifle in the hand. This is eight
boxes and two cylinders instead, and it is a .glb for the same reason a .wav was the right
thing to ask a person for: source format in, .uasset out, no GUI anywhere.

THE CONTRACT THIS MESH HAS TO KEEP (asserted by HI.Unreal.Weapons, which is the check,
and measured after import, which is the proof):

  * The barrel is the mesh's LONGEST AXIS AND IT IS +Y in Unreal space. The importer's
    mapping was first guessed and the guess was WRONG -- guessed (x,y,z)_glTF ->
    (-z,x,y)_UE, modelled the gun along glTF +X, and HI.Unreal.Weapons failed with
    extent (50.2, 2.8, 9.6): the 100 cm barrel had landed on UE +X. The answer was then
    read out of the engine instead of guessed twice: InterchangeGltfTranslator.cpp
    builds its axis matrix as X->X, Y->Z, Z->Y, no sign flips. So the gun is modelled in
    natural coordinates (x toward the muzzle, y up, z across) and put through
    T(x,y,z) = (-z, y, x) before writing -- a ROTATION (det +1), which sends length to
    glTF +Z and therefore to UE +Y, and height to UE +Z. The other candidate swap,
    (z, y, x), maps the muzzle to +Y just as well and is a MIRROR (det -1): it would
    reverse every triangle's winding and the gun would render inside-out, i.e. invisible
    from outside, with no error anywhere. `WeaponAimRotation` puts the component's +Y on
    the aim; a mesh modelled along a different axis goes sideways in the hand, silently,
    which is what the test is for.
  * The PIVOT IS THE GRIP CENTRE. `FHICharAnim::AttachPistol` seats a Mixamo-hand
    weapon at zero relative offset ("the grip at the wrist"), so where the origin sits
    is where the hand closes. Everything is translated so the pistol-grip's centre is
    (0, 0, 0); the tuning knobs (-HIGunLoc/-HIGunRot) and captured frames are how the
    seat gets judged, per the same function's comment.
  * UNITS ARE METRES because glTF says so; the importer's x100 makes them centimetres.
    The gun is 1.0 m overall (a pump-action's real ballpark), so ~100 cm in UE -- a
    26 cm pistol beside it, and `HI.Unreal.Cast`-scale sanity either way.
  * Triangle winding is enforced against each face's own normal rather than trusted to
    a hand-derived rule: glTF is CCW-front, and a wound-backwards box renders
    inside-out, i.e. invisible from outside, with no error anywhere.

Regenerating is the whole instruction: run this, run tools/import-assets.sh. The .glb
is gitignored derived state like every SourceAssets file; THIS SCRIPT is the asset.
"""

import json
import os
import struct
import sys

# ---------------------------------------------------------------- materials

# Flat colours, the same visual language as every primitive prop in the port
# (HILootProp tints engine cubes; this tints its own faces at the source).
MATERIALS = [
    # name,      baseColor (linear-ish sRGB floats),        metallic, roughness
    ("gunmetal", (0.10, 0.10, 0.12, 1.0), 0.75, 0.38),
    ("wood",     (0.30, 0.17, 0.07, 1.0), 0.00, 0.62),
    ("synth",    (0.05, 0.05, 0.055, 1.0), 0.10, 0.70),
]
MAT_GUNMETAL, MAT_WOOD, MAT_SYNTH = 0, 1, 2

# ---------------------------------------------------------------- geometry
# Modelled in glTF metres: +X toward the muzzle, +Y up, +Z across. Natural
# layout first (butt at x=-0.525, muzzle at x=+0.48), then translated so the
# grip centre is the origin -- see the contract above.

GRIP_CENTRE = (-0.095, -0.080, 0.0)


def _tri(parts, mat, n, a, b, c):
    """One triangle, winding enforced CCW-about-n (see the contract)."""
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    cx, cy, cz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    if cx * n[0] + cy * n[1] + cz * n[2] < 0.0:
        b, c = c, b
    parts.setdefault(mat, []).extend((a, n, b, n, c, n))


def _quad(parts, mat, n, a, b, c, d):
    _tri(parts, mat, n, a, b, c)
    _tri(parts, mat, n, a, c, d)


def add_box(parts, mat, x0, x1, y0, y1, z0, z1):
    """An axis-aligned box; six outward faces, flat normals."""
    for n, quad in (
        ((+1, 0, 0), ((x1, y0, z0), (x1, y0, z1), (x1, y1, z1), (x1, y1, z0))),  # muzzle-end
        ((-1, 0, 0), ((x0, y0, z1), (x0, y0, z0), (x0, y1, z0), (x0, y1, z1))),  # butt-end
        ((0, +1, 0), ((x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1))),  # top
        ((0, -1, 0), ((x0, y0, z1), (x1, y0, z1), (x1, y0, z0), (x0, y0, z0))),  # bottom
        ((0, 0, +1), ((x0, y0, z1), (x0, y1, z1), (x1, y1, z1), (x1, y0, z1))),  # side
        ((0, 0, -1), ((x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z0))),  # side
    ):
        _quad(parts, mat, n, *quad)


def add_cyl_x(parts, mat, x0, x1, cy, cz, r, segs=20):
    """A cylinder along +X with capped ends; per-face vertices, flat shading."""
    import math
    for s in range(segs):
        a0 = 2.0 * math.pi * s / segs
        a1 = 2.0 * math.pi * (s + 1) / segs
        p0 = (math.cos(a0), math.sin(a0))
        p1 = (math.cos(a1), math.sin(a1))
        q00 = (x0, cy + r * p0[0], cz + r * p0[1])
        q01 = (x0, cy + r * p1[0], cz + r * p1[1])
        q10 = (x1, cy + r * p0[0], cz + r * p0[1])
        q11 = (x1, cy + r * p1[0], cz + r * p1[1])
        # side: the outward normal of the slab is the average of the two rim normals
        nm = (0.0, p0[0] + p1[0], p0[1] + p1[1])
        ln = (nm[0] ** 2 + nm[1] ** 2 + nm[2] ** 2) ** 0.5
        nm = (nm[0] / ln, nm[1] / ln, nm[2] / ln)
        _quad(parts, mat, nm, q00, q10, q11, q01)
        # caps
        c0 = (x0, cy, cz)
        c1 = (x1, cy, cz)
        _tri(parts, mat, (-1, 0, 0), c0, q01, q00)
        _tri(parts, mat, (+1, 0, 0), c1, q10, q11)


def build():
    parts = {}  # material -> flat [vertex, normal, vertex, normal, ...]
    B = add_box
    C = add_cyl_x

    # Receiver: the action's body, between stock and barrel.
    B(parts, MAT_GUNMETAL, -0.14, 0.14, -0.030, 0.045, -0.022, 0.022)
    # Barrel: on top, out to the muzzle.
    C(parts, MAT_GUNMETAL, 0.14, 0.48, 0.028, 0.0, 0.0105)
    # Magazine tube: under the barrel, shorter.
    C(parts, MAT_GUNMETAL, 0.14, 0.38, -0.002, 0.0, 0.0085)
    # Front sight bead.
    B(parts, MAT_GUNMETAL, 0.455, 0.475, 0.038, 0.052, -0.004, 0.004)
    # Pump/forend: the sliding hand-grip around the magazine tube.
    B(parts, MAT_SYNTH, 0.17, 0.30, -0.028, 0.022, -0.028, 0.028)
    # Stock: back from the receiver, dropping to the butt.
    B(parts, MAT_WOOD, -0.50, -0.14, -0.075, 0.030, -0.021, 0.021)
    # Buttplate.
    B(parts, MAT_SYNTH, -0.525, -0.50, -0.085, 0.040, -0.024, 0.024)
    # Pistol grip: the origin sits at its centre, so the hand closes on it.
    gx, gy = GRIP_CENTRE[0], GRIP_CENTRE[1]
    B(parts, MAT_WOOD, gx - 0.035, gx + 0.035, gy - 0.060, gy + 0.060, -0.019, 0.019)

    # Translate the whole gun so the grip centre IS the pivot, then rotate into glTF
    # axes: T(x, y, z) = (-z, y, x), the det+1 rotation that lands the barrel (natural
    # +x) on glTF +z, which the Interchange translator sends to UE +Y -- see the
    # contract in the header for the wrong guess this replaces. Rotation about the
    # origin keeps the pivot exactly where the translation put it.
    tx, ty, tz = GRIP_CENTRE
    out = {}
    for mat, verts in parts.items():
        flat = []
        for i in range(0, len(verts), 2):
            v, n = verts[i], verts[i + 1]
            p = (v[0] - tx, v[1] - ty, v[2] - tz)
            flat.append((-p[2], p[1], p[0]))
            flat.append((-n[2], n[1], n[0]))
        out[mat] = flat
    return out


# ---------------------------------------------------------------- glb writer

def write_glb(parts, path):
    positions_b = bytearray()
    json_parts = []
    accessors = []
    buffer_views = []

    def add_view(data, target):
        # bufferViews must start 4-byte aligned; pad before, not after.
        while len(positions_b) % 4:
            positions_b.append(0)
        view_index = len(buffer_views)
        buffer_views.append({
            "buffer": 0, "byteOffset": len(positions_b),
            "byteLength": len(data), "target": target,
        })
        positions_b.extend(data)
        return view_index

    # One mesh primitive per (part, material), indices sequential: every
    # triangle was emitted independently, so there is nothing to share.
    primitives = []
    for mat in sorted(parts):
        verts = parts[mat]
        count = len(verts) // 2
        pos = bytearray()
        nrm = bytearray()
        mn = [float("inf")] * 3
        mx = [float("-inf")] * 3
        for i in range(0, len(verts), 2):
            v, n = verts[i], verts[i + 1]
            pos.extend(struct.pack("<fff", *v))
            nrm.extend(struct.pack("<fff", *n))
            for k in range(3):
                mn[k] = min(mn[k], v[k])
                mx[k] = max(mx[k], v[k])
        idx = struct.pack("<%dH" % count, *range(count))

        pos_view = add_view(bytes(pos), 34962)   # ARRAY_BUFFER
        nrm_view = add_view(bytes(nrm), 34962)
        idx_view = add_view(bytes(idx), 34963)   # ELEMENT_ARRAY_BUFFER

        pos_acc = len(accessors)
        accessors.append({"bufferView": pos_view, "componentType": 5126,
                          "count": count, "type": "VEC3", "min": mn, "max": mx})
        nrm_acc = len(accessors)
        accessors.append({"bufferView": nrm_view, "componentType": 5126,
                          "count": count, "type": "VEC3"})
        idx_acc = len(accessors)
        accessors.append({"bufferView": idx_view, "componentType": 5123,
                          "count": count, "type": "SCALAR"})
        primitives.append({"attributes": {"POSITION": pos_acc, "NORMAL": nrm_acc},
                           "indices": idx_acc, "material": mat})

    materials = [{
        "name": name,
        "pbrMetallicRoughness": {
            "baseColorFactor": list(rgba),
            "metallicFactor": metallic,
            "roughnessFactor": roughness,
        },
    } for (name, rgba, metallic, roughness) in MATERIALS]

    gltf = {
        "asset": {"version": "2.0",
                  "generator": "home-invasion-unreal tools/make-shotgun-glb.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "shotgun"}],
        "meshes": [{"name": "shotgun", "primitives": primitives}],
        "materials": materials,
        "accessors": accessors,
        "bufferViews": buffer_views,
        "buffers": [{"byteLength": len(positions_b)}],
    }
    json_bytes = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    while len(json_bytes) % 4:
        json_bytes += b" "
    bin_bytes = bytes(positions_b)
    while len(bin_bytes) % 4:
        bin_bytes += b"\x00"

    total = 12 + 8 + len(json_bytes) + 8 + len(bin_bytes)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(struct.pack("<4sII", b"glTF", 2, total))
        f.write(struct.pack("<II", len(json_bytes), 0x4E4F534A))
        f.write(json_bytes)
        f.write(struct.pack("<II", len(bin_bytes), 0x004E4942))
        f.write(bin_bytes)

    # ---- verify the file just written, by reading it back: a GLB the writer
    # cannot re-parse is not a deliverable, whatever the exit code said.
    with open(path, "rb") as f:
        blob = f.read()
    magic, version, length = struct.unpack("<4sII", blob[:12])
    assert magic == b"glTF" and version == 2 and length == len(blob), "container corrupt"
    jlen, jtype = struct.unpack("<II", blob[12:20])
    assert jtype == 0x4E4F534A, "first chunk is not JSON"
    rejson = json.loads(blob[20:20 + jlen].decode("utf-8"))
    blen, btype = struct.unpack("<II", blob[20 + jlen:28 + jlen])
    assert btype == 0x004E4942 and blen == rejson["buffers"][0]["byteLength"], "bin chunk mismatch"

    verts = sum(a["count"] for a in rejson["accessors"] if a["type"] == "VEC3") // 2
    allmn = [min(a["min"][k] for a in rejson["accessors"] if "min" in a) for k in range(3)]
    allmx = [max(a["max"][k] for a in rejson["accessors"] if "min" in a) for k in range(3)]
    # Predicted UE bounds under the TRANSLATOR'S OWN mapping (X->X, Y->Z, Z->Y, no sign
    # flips -- InterchangeGltfTranslator.cpp), which is what the probe after import is
    # checked against rather than against a second guess.
    ue_min = [allmn[0] * 100.0, allmn[2] * 100.0, allmn[1] * 100.0]
    ue_max = [allmx[0] * 100.0, allmx[2] * 100.0, allmx[1] * 100.0]
    print("make-shotgun-glb: %s" % path)
    print("  %d primitives, %d triangles, %d bytes" % (len(primitives), verts // 3, len(blob)))
    print("  glTF bounds min=(%.3f, %.3f, %.3f) max=(%.3f, %.3f, %.3f) m"
          % (allmn[0], allmn[1], allmn[2], allmx[0], allmx[1], allmx[2]))
    print("  predicted UE bounds min=(%.1f, %.1f, %.1f) max=(%.1f, %.1f, %.1f) cm"
          % tuple(ue_min + ue_max))
    longest_ue = max(range(3), key=lambda k: ue_max[k] - ue_min[k])
    print("  checks: longest axis is %s (must be Y); origin.Y=%.1f (must be >0: the muzzle "
          "outruns the stock past the grip-centre pivot)"
          % ("XYZ"[longest_ue], (ue_min[1] + ue_max[1]) / 2.0))
    return 0


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_out = os.path.join(here, os.pardir, "SourceAssets", "weapons", "shotgun.glb")
    out = os.path.normpath(sys.argv[1] if len(sys.argv) > 1 else default_out)
    return write_glb(build(), out)


if __name__ == "__main__":
    sys.exit(main())
