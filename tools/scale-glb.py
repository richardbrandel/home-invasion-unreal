#!/usr/bin/env python3
"""Scale a glb's skinned character and/or drop it onto the ground plane, in the file itself.

    python3 tools/scale-glb.py <in.glb> <out.glb> --scale 0.5 --ground

WHY (2026-11-01). `alina.glb` is authored at 2x (345 cm tall against Martha's 177) and centred on
the HIPS, so after import the mesh measured 172 cm while her skeleton's `hips->head` was still
151.5 cm against Martha's 55.1 -- the mesh and the joints had been scaled by different things. A
scale on a node above the character is not a fix: glTF says a SKINNED mesh ignores its node's
transform, so the wrapper halved the geometry and left the joints alone, which is a character whose
skin is stretched over bones twice its size. The engine-side alternative (a per-tag actor scale) is
worse than that: `HI.Unreal.Cast` reads the CLIP's Hips height, so no amount of actor scaling makes
the data look right to it.

What this does, and why it is exactly this:

  * POSITION accessors are multiplied by `scale` -- the geometry.
  * every node `translation` is multiplied by `scale` -- the joints, so the hierarchy matches.
  * inverseBindMatrices are LEFT ALONE. They are the inverse of the bind pose, so when the bind
    pose scales by s and the vertices scale by s, `jointMatrix * IBM` stays a pure s-scale: the
    skin keeps its shape. Rewriting them as well would double the correction.
  * `--ground` then adds a parent node whose translation lifts the lowest vertex to Y=0, which is
    what the game assumes (every other character here has its origin at the feet, and the Cast test
    asserts the standing idle's Hips read ~96 cm, not 0).

Y is up in a glb; the engine's importer converts to Z-up afterwards, so grounding happens on Y here.
"""
import json
import os
import struct
import sys

MAGIC = 0x46546C67
JSON_CHUNK = 0x4E4F534A
FLOAT = 5126


def read_glb(path):
    data = open(path, "rb").read()
    magic, version, _len = struct.unpack("<III", data[:12])
    if magic != MAGIC:
        raise SystemExit("SCALE: %s is not a glb" % path)
    chunks, off = [], 12
    while off < len(data):
        clen, ctype = struct.unpack("<II", data[off:off + 8])
        chunks.append([ctype, bytearray(data[off + 8:off + 8 + clen])])
        off += 8 + clen
    doc = json.loads(bytes(chunks[0][1]))
    return version, doc, chunks


def write_glb(path, version, doc, chunks):
    blob = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    blob += b" " * ((4 - len(blob) % 4) % 4)
    chunks[0][1] = bytearray(blob)
    body = b"".join(struct.pack("<II", len(c[1]), c[0]) + bytes(c[1]) for c in chunks)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", MAGIC, version, 12 + len(body)))
        f.write(body)


def position_accessors(doc):
    """Every accessor used as a primitive POSITION, once, with the primitives that use it."""
    seen = set()
    for mesh in doc.get("meshes", []):
        for prim in mesh.get("primitives", []):
            idx = prim.get("attributes", {}).get("POSITION")
            if idx is not None:
                seen.add(idx)
    return seen


def scale_positions(doc, bin_chunk, accessors, factor):
    """Multiply VEC3 float positions in place; returns (min_y, max_y) across all of them."""
    views = doc["bufferViews"]
    lo, hi = float("inf"), float("-inf")
    for idx in accessors:
        acc = doc["accessors"][idx]
        if acc.get("componentType") != FLOAT or acc.get("type") != "VEC3":
            raise SystemExit("SCALE: accessor %d is not float VEC3; refusing" % idx)
        view = views[acc["bufferView"]]
        if view.get("buffer") not in (None, 0):
            raise SystemExit("SCALE: multi-buffer glb is not supported")
        stride = view.get("byteStride") or 12
        base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
        for i in range(acc["count"]):
            at = base + i * stride
            x, y, z = struct.unpack_from("<fff", bin_chunk, at)
            x, y, z = x * factor, y * factor, z * factor
            struct.pack_into("<fff", bin_chunk, at, x, y, z)
            lo, hi = min(lo, y), max(hi, y)
    return lo, hi


def main():
    argv = sys.argv[1:]
    src = dst = None
    factor = 1.0
    ground = False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--scale":
            factor = float(argv[i + 1])
            i += 2
        elif a == "--ground":
            ground = True
            i += 1
        elif src is None:
            src = a
            i += 1
        else:
            dst = a
            i += 1
    if src is None or dst is None:
        raise SystemExit(__doc__)

    version, doc, chunks = read_glb(src)
    bin_chunk = chunks[1][1] if len(chunks) > 1 else None
    if bin_chunk is None:
        raise SystemExit("SCALE: no binary chunk")

    accessors = position_accessors(doc)
    lo, hi = scale_positions(doc, bin_chunk, accessors, factor)
    print("SCALE: %d POSITION accessor(s) x%.3f; height %.1f -> %.1f"
          % (len(accessors), factor, hi - lo, (hi - lo)))

    # Node translations are the JOINTS (and a couple of wrappers). Uniform-scale a hierarchy by
    # scaling every translation: rotations are untouched, and a uniform scale commutes with them,
    # so each joint's global transform becomes S * global -- which is what keeps the skin intact.
    touched = 0
    for node in doc.get("nodes", []):
        t = node.get("translation")
        if t and any(abs(v) > 1e-9 for v in t):
            node["translation"] = [v * factor for v in t]
            touched += 1
    print("SCALE: %d node translation(s) scaled" % touched)

    if ground:
        # The lowest vertex becomes Y=0: the game puts a character's origin at its feet (every
        # other character here measures its standing Hips at ~96 cm, and the Cast test asserts it).
        lift = -lo
        scene_roots = []
        for scene in doc.get("scenes", []):
            scene_roots.extend(scene.get("nodes", []))
        doc.setdefault("nodes", []).append({
            "name": "GroundRoot",
            "translation": [0.0, lift, 0.0],
            "children": scene_roots,
        })
        for scene in doc.get("scenes", []):
            scene["nodes"] = [len(doc["nodes"]) - 1]
        print("SCALE: grounded -- lifted %.1f so the lowest vertex sits at Y=0" % lift)

    write_glb(dst, version, doc, chunks)
    print("SCALE: wrote %s" % dst)


main()
