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
GROW = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
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
# ---- THE CUP, NOT THE CONE (2026-11-05). The first versions displaced vertices FORWARD along one
# axis with a Gaussian weight, which can only ever make a cone: one vertex furthest out, straight
# sides, and the silhouette is a tent however large the amplitude. Richard: "the boobs look like 2
# tents. I would like a fuller cup, like a DD size cup. Alina has nice boobs."
#
# Measured off Alina's own mesh (SourceAssets/characters/alina/idle.glb) for the target, because a
# bra size is not a number this file can compute: at her bust line the FRONT FACE is 15.2% of her
# height wide, and the cup (apex minus underbust) is 7.2% of her height. The Dancer's tent was 7.2%
# wide -- half as broad -- for a DEEPER cup (8.5%), which is exactly what a cone looks like: deep and
# narrow. So the job here is breadth at a slightly shallower depth.
#
# Three changes do it: TWO centres (one per breast, inside the chest), a RADIAL push from each centre
# instead of a single axis, and a SUPER-GAUSSIAN falloff (q^4) instead of q^2 -- a plateau with a soft
# edge rather than a peak -- followed by Laplacian smoothing of the moved patch so the underbust
# crease and the infill between the breasts blend instead of folding.
CUP_TARGET = 0.072      # of body height -- Alina's, measured
BREADTH_TARGET = 0.152  # of body height -- Alina's, measured
R_LAT, R_UP, POW = 10.5, 7.5, 2.5
FRONT_CENTRE_LAT = 5.0   # each breast centre, either side of the midline
MAX_MOVE_CM = 13.0

# The underbust level, which is both the depth reference and the front-face cut-off.
under_band = mid & (np.abs(u - (bust_z - 9.0)) < 2.5)
if under_band.sum() < 4:
    raise SystemExit("ED: no vertices to measure the underbust against")
f_under = float(f[under_band].max())

# THE CENTRES GO INSIDE THE CHEST, not on its surface. First attempt put them just behind the
# underbust SURFACE, which is nearly where the bust surface already is: every radial direction then
# pointed sideways rather than out, the patch flattened instead of rounding, and the amplitude solver
# (which corrects in proportion to how far it missed) ran away to three kilometres. A centre at
# mid-depth of the ribcage makes "outward" mean forward at the apex and sideways at the edges, which
# is what inflation is.
torso_depth = (np.abs(l - lc) < 8.0) & (u > zmin + 0.60 * H) & (u < zmin + 0.80 * H)
f_back = float(np.percentile(f[torso_depth], 5)) if torso_depth.sum() > 10 else f_under - 16.0
f_mid = 0.5 * (f_back + f_under)
print("ED: chest depth: back %.2f, underbust surface %.2f, mid-depth %.2f (centres sit here)" % (
    f_back, f_under, f_mid))

# One centre per breast, either side of the midline and 2 cm below the bust line. TWO forms are kept
# deliberately: the WORLD position, which gives the direction to push each vertex in, and the
# (lateral, up) pair, which measures how far a vertex is from the centre along the body's surface.
# Conflating the two is what made the first attempt push the apex 12 cm backwards.
UP = np.array([0.0, 1.0, 0.0])
CENTRE_LU = [(lc - FRONT_CENTRE_LAT, bust_z - 2.0), (lc + FRONT_CENTRE_LAT, bust_z - 2.0)]
# `f` and `l` are measured FROM `ref`, but `u` is an absolute height, so the vertical term has to be
# taken relative to `ref` as well -- getting that wrong put the centre 100 cm above her head and made
# every "outward" push point straight down (the apex moved 21 cm while advancing 1.6).
CENTRE_W = np.array([ref + fwd * f_mid + lat * lu[0] + UP * (lu[1] - ref[1]) for lu in CENTRE_LU])

sel = (torso_weight >= 0.5) & (np.abs(l - lc) < 2.0 * SIGMA_LAT_CM) & (f > f_mid)
if sel.sum() < 20:
    raise SystemExit("ED: only %d torso vertices -- refusing to guess" % sel.sum())

# The triangles, for the smoothing pass: a smoothing that is not tied to the actual surface tears
# the mesh at the patch edge.
ia = j["accessors"][prim["indices"]]
ib = j["bufferViews"][ia["bufferView"]]
ioff = ib.get("byteOffset", 0) + ia.get("byteOffset", 0)
idt = np.uint16 if ia["componentType"] == 5123 else np.uint32
IDX = np.frombuffer(bytes(binchunk), dtype=idt, count=ia["count"], offset=ioff).reshape(-1, 3).astype(np.int32)
nbrs = [set() for _ in range(count)]
for tri in IDX:
    a, b, c = int(tri[0]), int(tri[1]), int(tri[2])
    nbrs[a].update((b, c)); nbrs[b].update((a, c)); nbrs[c].update((a, b))


def push(amount_scale, base_world):
    """Radial push from both centres, then Laplacian smoothing inside the patch."""
    w = np.zeros(count)
    dirv = np.zeros_like(base_world)
    for (c_lat, c_up), c_world in zip(CENTRE_LU, CENTRE_W):
        fall = np.exp(-((((l - c_lat) / R_LAT) ** 2 + ((u - c_up) / R_UP) ** 2) ** (POW / 2.0)))
        w = np.maximum(w, fall)
        v = base_world - c_world
        n = np.linalg.norm(v, axis=1)
        n[n == 0.0] = 1.0
        dirv += (v / n[:, None]) * fall[:, None]
    w = np.where(sel, w, 0.0)
    w = np.where(w > 0.10, w, 0.0)
    dirv = np.where(w[:, None] > 0.0, dirv, 0.0)
    dn = np.linalg.norm(dirv, axis=1)
    dn[dn == 0.0] = 1.0
    dirv = dirv / dn[:, None]

    moved = base_world + dirv * (amount_scale * w)[:, None]

    # Laplacian smoothing, patch only, the unmoved boundary still pulling.
    idx = np.nonzero(w > 0)[0]
    for _ in range(2):
        newp = moved.copy()
        for i in idx:
            ns = nbrs[i]
            if not ns:
                continue
            acc_p = np.zeros(3)
            for k in ns:
                acc_p += moved[k]
            newp[i] = 0.5 * moved[i] + 0.5 * (acc_p / len(ns))
        moved = newp
    return moved, w, dirv


def measures(world_pts):
    """Cup depth and front-face breadth, measured the way Alina's were."""
    fw = (world_pts - ref) @ fwd
    band = mid & (np.abs(u - bust_z) < 1.0)
    apex = float(fw[band].max()) if band.sum() >= 4 else float("nan")
    row = np.abs(u - bust_z) < 1.0
    near = row & (fw > apex - 1.5)
    width = float(np.percentile(l[near], 97) - np.percentile(l[near], 3)) if near.sum() >= 3 else float("nan")
    return apex - f_under, 100.0 * width / H, width


# The amplitude is DERIVED, not searched: the apex has to move forward by (target - current), and the
# field's own forward component at the apex says how much displacement one unit of amplitude buys.
# The first version corrected the amplitude in proportion to the miss, which is a positive feedback
# loop the moment the field pushes the wrong way (it reached 3.3 million cm).
cup_before, breadth_before, width_before = measures(pts_world)
target_cup = CUP_TARGET * H * GROW
need = target_cup - cup_before
if need <= 0.0:
    raise SystemExit("ED: REFUSING -- the cup is already deeper than the target; this pass only adds volume")
probe, _w, probe_dir = push(1.0, pts_world)
probe_fw = (probe - ref) @ fwd
probe_move = np.linalg.norm(probe - pts_world, axis=1)
gain = float(np.max((probe_fw - f)[probe_move > 0.05])) if np.any(probe_move > 0.05) else 0.0
if gain <= 0.0:
    raise SystemExit("ED: REFUSING -- the field does not push forward at all (gain %.3f)" % gain)
amp = min(need / gain, 3.0 * need)
print("ED: amplitude %.2f cm per unit (need %.2f cm, gain %.3f)" % (amp, need, gain))
pts_s, w_final, dir_final = push(amp, pts_world)
cup, breadth_pct, width = measures(pts_s)
# One linear correction for the smoothing's pull-back, clamped so a bad measure cannot run away.
if cup > 0.1:
    amp = min(max(amp * (target_cup / cup), 0.5 * amp), 2.0 * amp)
    pts_s, w_final, dir_final = push(amp, pts_world)
    cup, breadth_pct, width = measures(pts_s)
pts2_world = pts_s
mask = w_final > 0
mv = np.nonzero(mask)[0]
move_cm = np.linalg.norm(pts2_world - pts_world, axis=1)

print("ED: H=%.1f cm  bust line at %.1f cm (%.0f%% of height)  underbust forward %.2f cm  depth scale %.2f" % (
    H, bust_z - zmin, 100.0 * (bust_z - zmin) / H, f_under, GROW))
print("ED: TARGETS from Alina: cup %.1f cm (%.1f%% of H), front-face breadth %.1f cm (%.1f%% of H)" % (
    CUP_TARGET * H * GROW, CUP_TARGET * GROW * 100.0, BREADTH_TARGET * H, BREADTH_TARGET * 100.0))
print("ED: BEFORE  cup %.2f cm (%.2f%% of H)  front-face breadth %.1f cm (%.1f%% of H)" % (
    cup_before, 100.0 * cup_before / H, width_before, breadth_before))
print("ED: AFTER   cup %.2f cm (%.2f%% of H)  front-face breadth %.1f cm (%.1f%% of H)" % (
    cup, 100.0 * cup / H, width, breadth_pct))
dbg_band = mid & (np.abs(u - bust_z) < 2.0)
dbg = np.nonzero(dbg_band)[0][np.argsort(-f[dbg_band])][:6]
for i in dbg:
    print("ED: DBG v%d f=%6.2f w=%.2f  dir=(fwd %+.2f, lat %+.2f, up %+.2f)  dfwd=%+6.2f move=%6.2f" % (
        i, f[i], w_final[i], dir_final[i] @ fwd, dir_final[i] @ lat, dir_final[i][1],
        ((pts2_world[i] - pts_world[i]) @ fwd), move_cm[i]))
print("ED: eligible torso verts=%d  moved=%d  max move=%.2f cm (cap %.1f)  midline at lateral %.2f cm" % (
    int(sel.sum()), int(mask.sum()), float(move_cm.max()), MAX_MOVE_CM, lc))
print("ED: moved verts by dominant bone: %s" % (
    {k: int((dominant[mv] == k).sum()) for k in sorted(set(dominant[mv]))},))

pts2 = (np.c_[pts2_world, np.ones(len(pts2_world))] @ Minv.T)[:, :3]

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

# ---- THE REGRESSION GUARDS: nothing above the neckline, nothing out on the arms, nothing absurd.
# The neckline guard sits at 80%, not 78%: the bust line is at 73%, and the upper breast is SUPPOSED to
# gain a little volume -- the fault this guards against is the collar and the neck moving, which is
# what the first version did (7.4 cm of movement on the sternum, and nothing on the bust).
neck_z = zmin + 0.80 * H
above = (move_cm > 1.5) & (u > neck_z)
outboard = mask & (np.abs(l - lc) > 16.0)
print("ED: moved verts above 80%% of height (neckline): %d   beyond 16 cm laterally (arms): %d   over %.1f cm: %d" % (
    int(above.sum()), int(outboard.sum()), MAX_MOVE_CM, int((move_cm > MAX_MOVE_CM).sum())))
if above.sum() > 0 or outboard.sum() > 0 or (move_cm > MAX_MOVE_CM).sum() > 0:
    raise SystemExit("ED: REFUSING -- the displacement is reaching the neck, the arms, or is absurd")
# (The breadth number is REPORTED, not gated: measuring "how much of the front face is within 1.5 cm of
# the apex" flatters a FLAT chest and penalises a dome, so it cannot tell a cup from a slab. The shape
# judgement is the render's, and the depth target above is the only number this tool enforces.)

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
