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

EXTENDED 2026-11-05 for the Dancer, whose `woman01_edited.glb` has the same defect but needed two
things this tool refused at first:

  * **A uniform SCALE on the rig.** Her `_rootJoint` carries 0.01 (the glTF -> cm import scale), and
    the old guard rejected any node whose scale was not 1. Uniform scales compose fine, so they are
    handled now; a NON-uniform scale is still refused, because the order of a scale against a
    rotation is a question the file has to answer, not this tool.
  * **A transform on the mesh node.** Her vertices live under `Object_7`, which sits 18.5 cm off the
    skeleton's origin, so an IBM of `inverse(jointWorld)` alone binds the mesh 18.5 cm away from its
    own bones. The skinning matrix is `jointWorld * IBM` *in mesh space*, so the bind is
    `IBM = inverse(jointWorld) * meshNodeWorld`. For a model whose mesh node is at the origin --
    Alina's is -- this is exactly the old behaviour, which is why adding it changed nothing there.

Her defect is partial and the symptom is worth writing down, because it will be met again: twenty of
sixty-six joints had an IBM that put the bone ~1.4 m from its parent (the whole thumb and every
fingertip, both hands), while their node transforms put it 1-3 cm away. Both are invisible in the
bind pose -- `IBM * bindPose == identity` either way -- so the mesh renders perfectly until one of
those bones ROTATES, and then the vertices it drives swing on a 1.4 m radius. That is Richard's
"her hands turn into what looks like long swords, sometimes": a few hand vertices flung a metre and a
half along a thin sliver, only in the frames where a clip happens to turn a thumb or a fingertip.
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
        sc = n.get("scale")
        if sc and (abs(sc[0] - sc[1]) > 1e-6 or abs(sc[1] - sc[2]) > 1e-6):
            raise SystemExit("FIXSKIN: node scale %s is not uniform; refusing to guess" % (sc,))

    def world(i):
        chain, j = [], i
        while j is not None:
            chain.append(j)
            j = parent.get(j)
        p, q, s = (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0), 1.0
        for j in reversed(chain):
            n = nodes[j]
            t = n.get("translation") or (0.0, 0.0, 0.0)
            r = n.get("rotation") or (0.0, 0.0, 0.0, 1.0)
            sc = n.get("scale") or (1.0, 1.0, 1.0)
            moved = qrot(q, (t[0] * s, t[1] * s, t[2] * s))
            p = (p[0] + moved[0], p[1] + moved[1], p[2] + moved[2])
            q = qmul(q, r)
            s = s * sc[0]
        return p, q, s

    skin = doc["skins"][0]
    acc = doc["accessors"][skin["inverseBindMatrices"]]
    view = doc["bufferViews"][acc["bufferView"]]
    base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = view.get("byteStride") or 64

    def mat_from_trs(p, q, s):
        r = rot_matrix(q)
        m = [[r[i][j] * s for j in range(3)] + [p[i]] for i in range(3)]
        m.append([0.0, 0.0, 0.0, 1.0])
        return m

    def mat_inv_trs(p, q, s):
        """inverse of T(p) R(q) S(s):  S(1/s) R(q)^T T(-p)."""
        r = rot_matrix(q)
        inv = [[r[j][i] / s for j in range(3)] + [0.0] for i in range(3)]
        for i in range(3):
            inv[i][3] = -sum(inv[i][k] * p[k] for k in range(3))
        inv.append([0.0, 0.0, 0.0, 1.0])
        return inv

    def mul(a, b):
        return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]

    # The skinning matrix is `jointWorld * IBM` in MESH space, so the bind this file must store is
    # `inverse(jointWorld) * meshWorld`. Without that factor a mesh whose node is offset from the
    # skeleton's origin binds away from its own bones: identity for Alina, 18.5 cm for the Dancer.
    mesh_node = next(i for i, n in enumerate(nodes) if "mesh" in n)
    mesh_world = mat_from_trs(*world(mesh_node))

    changed = 0
    for k, joint in enumerate(skin["joints"]):
        ibm = mul(mat_inv_trs(*world(joint)), mesh_world)
        mat = [ibm[0][0], ibm[1][0], ibm[2][0], ibm[3][0],
               ibm[0][1], ibm[1][1], ibm[2][1], ibm[3][1],
               ibm[0][2], ibm[1][2], ibm[2][2], ibm[3][2],
               ibm[0][3], ibm[1][3], ibm[2][3], ibm[3][3]]
        at = base + k * stride
        old = struct.unpack_from("<16f", bin_chunk, at)
        if max(abs(old[i] - mat[i]) for i in range(16)) > 1e-3:
            changed += 1
        struct.pack_into("<16f", bin_chunk, at, *mat)

    print("FIXSKIN: %d/%d inverse-bind matrices rewritten" % (changed, len(skin["joints"])))

    # SELF-CHECK: `jointWorld * IBM` must reproduce the mesh node's transform for EVERY joint. That
    # is the definition of the bind, and it is the identity that makes the bind pose render as
    # authored -- which is why a bad IBM is invisible until a bone rotates.
    worst, worst_bone = 0.0, ""
    for k, joint in enumerate(skin["joints"]):
        m = struct.unpack_from("<16f", bin_chunk, base + k * stride)
        jibm = [[m[0], m[4], m[8], m[12]],
                [m[1], m[5], m[9], m[13]],
                [m[2], m[6], m[10], m[14]],
                [m[3], m[7], m[11], m[15]]]
        got = mul(mat_from_trs(*world(joint)), jibm)
        d = max(abs(got[i][j] - mesh_world[i][j]) for i in range(4) for j in range(4))
        if d > worst:
            worst, worst_bone = d, nodes[joint].get("name", "?")
    print("FIXSKIN: self-check -- worst |jointWorld*IBM - meshWorld| = %.5f (%s)" % (worst, worst_bone))
    if worst > 1e-2:
        raise SystemExit("FIXSKIN: self-check FAILED (%.3f at %s) -- refusing to write" % (worst, worst_bone))
    save(dst, version, doc, chunks)
    print("FIXSKIN: wrote %s" % dst)


main()
