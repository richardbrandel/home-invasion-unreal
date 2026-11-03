#!/usr/bin/env python3
"""Recompute a glb's skin inverse-bind matrices from its node transforms.

    python3 tools/fix-glb-skin.py <in.glb> <out.glb>

WHY (2026-11-01). `alina.glb` declares a 105-joint skin whose `inverseBindMatrices` are mostly
ZERO-TRANSLATION PLACEHOLDERS: `CC_Base_Hip_02`'s IBM translation is (0, 0, 0) while the joint's
actual bind position, composed from its node transforms, is (0, 100.7, 0). Measured across the
first joints: Hip (0,0,0), L_Thigh (0,0,0), L_Calf (0,0,0) against bind positions 100.7 and 98.2
and 53.0. Two of them are real; the rest are not.

An importer that trusts the IBMs therefore reconstructs a skeleton with every bone at the origin,
and the mesh gets skinned through those collapsed bones. That is what the engine did: the imported
skeleton reported `Head` local (0, 0, 375.9), `LeftFoot` (868.6, -237.5, 820.0) and `RightHand`
(-5973.6, -376.5, 11977.1) for a rig whose real bone offsets are 2-100 units, and the mesh measured
345 cm for a 180 cm character. The JSON itself is fine -- which is why nothing about this looked
wrong until the IBMs were compared with the node transforms.

The fix is arithmetic: an IBM is by definition the inverse of the joint's bind-pose world transform,
so recompute each one as inverse(composed node TRS). Then `jointMatrix * IBM == identity` at bind
pose by construction, and the skin renders exactly as authored. Verified after writing: every joint's
recomputed IBM inverts back to its node world position.

Only translation and rotation are handled; every node in this file has unit scale. A file with a
non-uniform scale in the skeleton should be fixed at the source, not here.
"""
import json
import math
import struct
import sys

MAGIC = 0x46546C67
JSON_CHUNK = 0x4E4F534A


def load(path):
    data = open(path, "rb").read()
    magic, version, _ = struct.unpack("<III", data[:12])
    if magic != MAGIC:
        raise SystemExit("FIXSKIN: %s is not a glb" % path)
    chunks, off = [], 12
    while off < len(data):
        clen, ctype = struct.unpack("<II", data[off:off + 8])
        chunks.append([ctype, bytearray(data[off + 8:off + 8 + clen])])
        off += 8 + clen
    return version, json.loads(bytes(chunks[0][1])), chunks


def save(path, version, doc, chunks):
    blob = json.dumps(doc, separators=(",", ":")).encode()
    blob += b" " * ((4 - len(blob) % 4) % 4)
    chunks[0][1] = bytearray(blob)
    body = b"".join(struct.pack("<II", len(c[1]), c[0]) + bytes(c[1]) for c in chunks)
    open(path, "wb").write(struct.pack("<III", MAGIC, version, 12 + len(body)) + body)


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qrot(q, v):
    x, y, z, w = q
    tx = 2 * (y * v[2] - z * v[1])
    ty = 2 * (z * v[0] - x * v[2])
    tz = 2 * (x * v[1] - y * v[0])
    return (v[0] + w * tx + (y * tz - z * ty),
            v[1] + w * ty + (z * tx - x * tz),
            v[2] + w * tz + (x * ty - y * tx))


def rot_matrix(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)],
            [2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)],
            [2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)]]


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    version, doc, chunks = load(src)
    bin_chunk = chunks[1][1]
    nodes = doc["nodes"]

    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get("children", []):
            parent[c] = i
    for n in nodes:
        s = n.get("scale")
        if s and any(abs(v - 1.0) > 1e-6 for v in s):
            raise SystemExit("FIXSKIN: node scale %s is not 1; refusing to guess" % s)

    def world(i):
        chain, j = [], i
        while j is not None:
            chain.append(j)
            j = parent.get(j)
        p, q = (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)
        for j in reversed(chain):
            n = nodes[j]
            t = n.get("translation") or (0.0, 0.0, 0.0)
            r = n.get("rotation") or (0.0, 0.0, 0.0, 1.0)
            moved = qrot(q, t)
            p = (p[0] + moved[0], p[1] + moved[1], p[2] + moved[2])
            q = qmul(q, r)
        return p, q

    skin = doc["skins"][0]
    acc = doc["accessors"][skin["inverseBindMatrices"]]
    view = doc["bufferViews"][acc["bufferView"]]
    base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = view.get("byteStride") or 64

    changed = 0
    for k, joint in enumerate(skin["joints"]):
        p, q = world(joint)
        # inverse of a rigid transform: R^T, and -R^T * p
        r = rot_matrix(q)
        rt = [[r[0][0], r[1][0], r[2][0]],
              [r[0][1], r[1][1], r[2][1]],
              [r[0][2], r[1][2], r[2][2]]]
        t = tuple(-(rt[i][0] * p[0] + rt[i][1] * p[1] + rt[i][2] * p[2]) for i in range(3))
        # column-major
        mat = [rt[0][0], rt[1][0], rt[2][0], 0.0,
               rt[0][1], rt[1][1], rt[2][1], 0.0,
               rt[0][2], rt[1][2], rt[2][2], 0.0,
               t[0], t[1], t[2], 1.0]
        at = base + k * stride
        old = struct.unpack_from("<16f", bin_chunk, at)
        if max(abs(old[i] - mat[i]) for i in range(16)) > 1e-3:
            changed += 1
        struct.pack_into("<16f", bin_chunk, at, *mat)

    print("FIXSKIN: %d/%d inverse-bind matrices rewritten" % (changed, len(skin["joints"])))

    # SELF-CHECK: the stored IBM must now invert back to the joint's world position.
    worst = 0.0
    for k, joint in enumerate(skin["joints"]):
        m = struct.unpack_from("<16f", bin_chunk, base + k * stride)
        p, _ = world(joint)
        back = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
                m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
                m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14])
        worst = max(worst, math.sqrt(sum(v * v for v in back)))
    print("FIXSKIN: self-check -- worst |IBM * bindPosition| = %.4f (0 is perfect)" % worst)
    save(dst, version, doc, chunks)
    print("FIXSKIN: wrote %s" % dst)


main()
