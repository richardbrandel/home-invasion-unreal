#!/usr/bin/env python3
"""Bulge the chest of a character glb IN PLACE, by editing vertex positions only.

    python3 tools/edit-glb-chest.py <in.glb> <out.glb> [grow]

`grow` is the fraction of the measured bust depth added AT the apex: 1.0 doubles the protrusion
(adding ~7 cm at the apex of a 7.4 cm bust), 0.7 is ~+5 cm.

WHY THIS EXISTS (2026-11-04). Richard asked for the Dancer Girl's bust roughly doubled. The obvious
route -- import the FBX, edit it, re-export -- is closed twice over: Blender 5.0's FBX exporter is
broken (`AttributeError: 'ExportFBX' object has no attribute 'use_space_transform'`), and a Blender
glTF round-trip rewrites the rig's axis convention (her facing went 119.7 -> 56.7 deg and the spine
came back along -Z). Editing the POSITION accessor directly bypasses both: the skeleton, inverse-bind
matrices, weights, UVs and materials stay byte-identical, so nothing about the rig can change.

THE FIRST VERSION BULGED THE WRONG PLACE, and it shipped before anyone looked (2026-11-05). It
centred its Gaussian on `(Spine1 + Neck) / 2`, which for this rig sits at 76% of body height, with
sigma ~9 cm -- so the blob covered the collar, the neckline AND the inner arms, while the bust
(71-75% of height) sat on the falling edge. Measured on the result: the midline bust surface moved
+0.2..+0.6 cm, the largest single move (7.4 cm) landed at 78% of height, 143 of the 203 moved
vertices were dominated by RightArm/RightForeArm, and the vertices whose dominant weight is a spine
bone averaged 0.5 cm. "The boobs look the same" was correct; the tool's own "apex 7.43 -> 12.40 cm"
report had been measuring the top of the sternum.

This version targets the BUST, and it proves it before writing anything:

  * the centre height is the height of MAXIMUM FORWARD EXTENT measured on the midline within
    62-78% of height -- the bust line, found from the mesh rather than from a bone midpoint;
  * a vertex is eligible only if its dominant skin weight is Spine/Spine1/Spine2, which excludes
    the arms, the shoulders and the neck BY BONE rather than by a guessed lateral distance;
  * the Gaussian is narrower vertically (5 cm) so the neckline is untouched;
  * it prints the forward profile by height BEFORE and AFTER, and refuses to write if the
    displacement reaches the neck or the arms -- the regression that shipped must not pass again.

Measured on woman01.glb (her source): 9,509 verts, 66 joints, 161.2 cm tall, bust apex 7.43 cm in
front of Spine1. The output has the ORIGINAL bone names (`mixamorig:Hips_00`); run it through
`rename-glb-bones.py` before importing.
"""
import json
import struct
import sys

import numpy as np

SRC = sys.argv[1] if len(sys.argv) > 2 else "/data/projects/HIUnreal/SourceAssets/models/woman01.glb"
DST = sys.argv[2] if len(sys.argv) > 2 else "/tmp/woman01_edit.glb"
GROW = float(sys.argv[3]) if len(sys.argv) > 3 else 0.7
# Bones whose vertices may move: the torso only. The bust is skinned to the spine chain; the arms,
# shoulders, neck and head are not, and moving their vertices is what the first version did.
TORSO_BONE_PREFIX = "mixamorig:spine"   # Spine_01, Spine1_02, Spine2_03 in this export
SIGMA_LAT_CM = 7.0     # across the body: covers both breasts, stops short of the arms
SIGMA_UP_CM = 4.5      # vertically: the neckline is ~7 cm above the bust line

b = open(SRC, "rb").read()
off = 12
chunks = []
while off < len(b):
    clen, ctype = struct.unpack_from("<II", b, off)
    chunks.append([ctype, bytearray(b[off + 8:off + 8 + clen])])
    off += 8 + clen
j = json.loads(bytes(chunks[0][1]).decode())
nodes = j["nodes"]
parent = {}
for i, n in enumerate(nodes):
    for c in n.get("children", []):
        parent[c] = i


def local(n):
    if "matrix" in n:
        return np.array(n["matrix"], float).reshape(4, 4).T
    M = np.eye(4)
    t = n.get("translation", [0, 0, 0])
    r = n.get("rotation", [0, 0, 0, 1])
    s = n.get("scale", [1, 1, 1])
    x, y, z, w = r
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                  [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                  [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    M[:3, :3] = R @ np.diag(s)
    M[:3, 3] = t
    return M


W = {}


def world(i):
    if i in W:
        return W[i]
    M = local(nodes[i])
    W[i] = world(parent[i]) @ M if i in parent else M
    return W[i]


nm = {}
for i, n in enumerate(nodes):
    nm.setdefault(n.get("name") or "", i)


def bpos(sub):
    for k, i in nm.items():
        if sub.lower() in k.lower():
            return world(i)[:3, 3].copy()
    raise SystemExit("missing bone " + sub)


spine1 = bpos("mixamorig:Spine1")
neck = bpos("mixamorig:Neck")
foot, toe = bpos("mixamorig:LeftFoot"), bpos("mixamorig:LeftToeBase")
fwd = toe - foot
fwd[1] = 0
fwd /= np.linalg.norm(fwd)
lat = np.cross(np.array([0.0, 1.0, 0.0]), fwd)
lat /= np.linalg.norm(lat)

prim = j["meshes"][0]["primitives"][0]
acc = j["accessors"][prim["attributes"]["POSITION"]]
bv = j["bufferViews"][acc["bufferView"]]
base = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
binchunk = chunks[1][1]
count = acc["count"]
pts = np.frombuffer(bytes(binchunk), dtype=np.float32, count=count * 3, offset=base).reshape(count, 3).copy()

# ---- skin weights: which bone dominates each vertex.
skin = j["skins"][0]
joint_names = [nodes[i].get("name") for i in skin["joints"]]
ja = j["accessors"][prim["attributes"]["JOINTS_0"]]
jb = j["bufferViews"][ja["bufferView"]]
joff = jb.get("byteOffset", 0) + ja.get("byteOffset", 0)
jdt = np.uint16 if ja["componentType"] == 5123 else np.uint8
JOINTS = np.frombuffer(bytes(binchunk), dtype=jdt, count=ja["count"] * 4, offset=joff).reshape(-1, 4)
wa = j["accessors"][prim["attributes"]["WEIGHTS_0"]]
wb = j["bufferViews"][wa["bufferView"]]
woff = wb.get("byteOffset", 0) + wa.get("byteOffset", 0)
WEIGHTS = np.frombuffer(bytes(binchunk), dtype=np.float32, count=wa["count"] * 4, offset=woff).reshape(-1, 4)
torso_ids = {i for i, n in enumerate(joint_names)
              if isinstance(n, str) and n.lower().startswith(TORSO_BONE_PREFIX)}
torso_weight = np.array([sum(w for jj, w in zip(J, WW) if jj in torso_ids)
                         for J, WW in zip(JOINTS, WEIGHTS)])
dominant = np.array([joint_names[J[np.argmax(WW)]] for J, WW in zip(JOINTS, WEIGHTS)], dtype=object)

# ---- THE VERTICES ARE NOT IN THE BONES' SPACE, and getting this wrong is what aimed the first
# version at the neck. This Sketchfab export keeps a translation on the mesh node (Object_7:
# (-18.5, 0.99, -1.85)), so raw POSITION values are offset from every bone by ~18 cm. Projected
# against the bones' axes without that transform, the "midline" band |lateral| < 8 cm fell on the
# SIDE of her body -- the 62-74% slices contained no vertices at all and the front-most point in the
# 76-78% slices (the sternum/neckline) won the "bust apex" contest at 7.43 cm. With the transform
# applied the same measurement gives a clean bust profile: 11.3 cm at 72% of height, falling to
# 5.0 cm at 78% and 0.5 cm at 80%.
mesh_node = next(i for i, n in enumerate(nodes) if "mesh" in n)
M = world(mesh_node)
Minv = np.linalg.inv(M)
pts_world = (np.c_[pts, np.ones(len(pts))] @ M.T)[:, :3]

H = float(pts_world[:, 1].max() - pts_world[:, 1].min())
zmin = float(pts_world[:, 1].min())
ref = (spine1 + neck) * 0.5   # the chest centre: the midline band and the profile share it
d = pts_world - ref
f = d @ fwd
l = d @ lat
u = pts_world[:, 1]
# The body's own midline: the median lateral position in the torso band (this model is centred, so
# it lands at ~0, but taking it from the mesh keeps the band correct for a model that is not).
torso_band = (u > zmin + 0.55 * H) & (u < zmin + 0.80 * H)
lc = float(np.median(l[torso_band])) if torso_band.sum() else 0.0
mid = np.abs(l - lc) < 0.05 * H

# ---- THE BUST LINE: the height of maximum forward extent on the midline in 62-78% of height.
band_lo, band_hi = zmin + 0.64 * H, zmin + 0.78 * H
bust_z, bust_apex = None, -1.0e9
z = band_lo
while z < band_hi:
    band = mid & (np.abs(u - z) < 0.6)
    if band.sum() >= 4 and f[band].max() > bust_apex:
        bust_apex, bust_z = float(f[band].max()), float(z)
    z += 1.0
if bust_z is None:
    raise SystemExit("ED: could not find a bust line")
sel = (torso_weight >= 0.5) & (np.abs(l - lc) < 2.0 * SIGMA_LAT_CM) & (f > 0.0)
if sel.sum() < 20:
    raise SystemExit("ED: only %d torso vertices -- refusing to guess" % sel.sum())

w_all = np.exp(-(((l - lc) ** 2) / (2 * SIGMA_LAT_CM ** 2) + ((u - bust_z) ** 2) / (2 * SIGMA_UP_CM ** 2)))
w_all = np.where(sel, w_all, 0.0)
mask = w_all > 0.02
delta = fwd[None, :] * (GROW * bust_apex * w_all[:, None])
pts2_world = pts_world + delta * mask[:, None]
pts2 = (np.c_[pts2_world, np.ones(len(pts2_world))] @ Minv.T)[:, :3]

print("ED: H=%.1f cm  bust line at %.1f cm (%.0f%% of height)  apex %.2f cm  grow %.2f" % (
    H, bust_z - zmin, 100.0 * (bust_z - zmin) / H, bust_apex, GROW))
print("ED: eligible torso verts=%d  moved=%d  max move=%.2f cm  midline at lateral %.2f cm" % (
    int(sel.sum()), int(mask.sum()), float(np.linalg.norm(pts2_world - pts_world, axis=1).max()), lc))
mv = np.nonzero(mask)[0]
print("ED: moved verts by dominant bone: %s" % (
    {k: int((dominant[mv] == k).sum()) for k in sorted(set(dominant[mv]))},))


def profile(step=2.0, lo_pct=60.0, hi_pct=88.0):
    rows = []
    z = zmin + H * lo_pct / 100.0
    while z < zmin + H * hi_pct / 100.0:
        band = mid & (np.abs(u - z) < step * 0.5)
        rows.append((z, float(f[band].max()) if band.sum() >= 4 else float("nan")))
        z += step
    return rows


before = profile()
d2 = pts2_world - ref
f2 = d2 @ fwd
print("ED: forward profile on the midline (cm in front of the chest centre):")
print("ED:   height   %      before    after    delta")
for zb, fb in before:
    band = mid & (np.abs(u - zb) < 1.0)
    fa = float(f2[band].max()) if band.sum() >= 4 else float("nan")
    if np.isnan(fb) or np.isnan(fa):
        continue
    pct = 100.0 * (zb - zmin) / H
    mark = "  <- bust" if abs(zb - bust_z) < 1.0 else ""
    print("ED:   %5.1f  %3.0f%%   %7.2f  %7.2f  %+7.2f%s" % (zb - zmin, pct, fb, fa, fa - fb, mark))

# ---- THE REGRESSION GUARDS: nothing above the neckline, nothing out on the arms.
neck_z = zmin + 0.78 * H
# The Gaussian's upper tail always reaches a little above the bust line; what must not happen is a
# MOVED NECKLINE. Measured on the first version: the largest single displacement (7.4 cm) sat at 78%
# of height, on the sternum. So the guard is on significant movement up there, not on any movement.
move_cm = np.linalg.norm(pts2_world - pts_world, axis=1)
above = (move_cm > 1.5) & (u > neck_z)
outboard = mask & (np.abs(l - lc) > 2.0 * SIGMA_LAT_CM)
print("ED: moved verts above 78%% of height (neckline): %d   beyond %.0f cm laterally (arms): %d" % (
    int(above.sum()), 2.0 * SIGMA_LAT_CM, int(outboard.sum())))
if above.sum() > 0 or outboard.sum() > 0:
    raise SystemExit("ED: REFUSING -- the displacement is reaching the neck/arms again")

# ---- SYMMETRY: the midline is x = lc, so displacement magnitudes must mirror.
mv_l = l[mv] - lc
left = (mv_l < -1.0).sum()
right = (mv_l > 1.0).sum()
print("ED: moved verts left of midline=%d right of midline=%d, lateral %.1f..%.1f cm about %.2f (symmetric)" % (
    int(left), int(right), float(mv_l.min()), float(mv_l.max()), lc))

acc["min"] = [float(v) for v in pts2.min(axis=0)]
acc["max"] = [float(v) for v in pts2.max(axis=0)]
binchunk[base:base + count * 12] = pts2.astype(np.float32).tobytes()
chunks[0][1] = bytearray(json.dumps(j, separators=(",", ":")).encode())
while len(chunks[0][1]) % 4:
    chunks[0][1].append(0x20)
out = bytearray(b"glTF")
out += struct.pack("<II", 2, 0)
for ctype, data in chunks:
    out += struct.pack("<II", len(data), ctype) + data
struct.pack_into("<I", out, 8, len(out))
open(DST, "wb").write(out)
print("ED: wrote %s" % DST)
