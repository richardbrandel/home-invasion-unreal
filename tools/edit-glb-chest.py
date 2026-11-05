#!/usr/bin/env python3
"""Bulge the chest of a character glb IN PLACE, by editing vertex positions only.

    python3 tools/edit-glb-chest.py <in.glb> <out.glb> [grow]

`grow` is the fraction of the measured apex depth added AT the apex: 1.0 doubles the protrusion,
0.85 is a ~1.4x. Vertices move along the model's own forward axis under a Gaussian centred on the
chest, so the falloff is smooth and the result is symmetric about the midline.

WHY THIS EXISTS (2026-11-04). Richard asked for the Dancer Girl's bust to be roughly doubled. The
obvious route -- import the FBX, edit it, re-export -- is closed twice over: Blender 5.0's FBX
exporter is broken (`AttributeError: 'ExportFBX' object has no attribute 'use_space_transform'`),
and a Blender glTF round-trip rewrites the rig's axis convention (her facing went 119.7 -> 56.7 deg
and the spine came back along -Z). Editing the POSITION accessor directly bypasses both: the
skeleton, inverse-bind matrices, weights, UVs and materials stay byte-identical, so nothing about
the rig can change. Only the vertices move.

Measured on woman01.glb (the Dancer's source): 9,509 verts, 66 joints, height 161.2 cm, chest apex
7.43 cm in front of the chest centre. With grow=1.0 the apex becomes 12.40 cm and 203 vertices move.

The output has the ORIGINAL bone names (`mixamorig:Hips_00`). Run it through
`rename-glb-bones.py` before importing:
    GLB_WRAPPER=1 GLB_SCALE=1 python3 tools/rename-glb-bones.py <out.glb> <named.glb>
"""
import json
import math
import struct
import sys

import numpy as np

SRC = "/data/projects/HIUnreal/SourceAssets/models/woman01.glb"
DST = "/tmp/woman01_edit.glb"
GROW = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0     # fraction of the apex depth added AT the apex

b = open(SRC, "rb").read(); off = 12; chunks = []
while off < len(b):
    clen, ctype = struct.unpack_from("<II", b, off); chunks.append([ctype, bytearray(b[off+8:off+8+clen])]); off += 8 + clen
j = json.loads(bytes(chunks[0][1]).decode())
nodes = j["nodes"]; parent = {}
for i, n in enumerate(nodes):
    for c in n.get("children", []): parent[c] = i
def local(n):
    if "matrix" in n: return np.array(n["matrix"], float).reshape(4,4).T
    M = np.eye(4); t = n.get("translation",[0,0,0]); r = n.get("rotation",[0,0,0,1]); s = n.get("scale",[1,1,1])
    x,y,z,w = r
    R = np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
    M[:3,:3] = R @ np.diag(s); M[:3,3] = t; return M
W = {}
def world(i):
    if i in W: return W[i]
    M = local(nodes[i]); W[i] = world(parent[i]) @ M if i in parent else M; return W[i]
nm = {}
for i, n in enumerate(nodes): nm.setdefault(n.get("name") or "", i)
def bpos(sub):
    for k, i in nm.items():
        if sub.lower() in k.lower(): return world(i)[:3,3].copy()
    raise SystemExit("missing " + sub)
sp1, neck = bpos("mixamorig:Spine1"), bpos("mixamorig:Neck")
foot, toe = bpos("mixamorig:LeftFoot"), bpos("mixamorig:LeftToeBase")
fwd = toe - foot; fwd[1] = 0; fwd /= np.linalg.norm(fwd)
lat = np.cross(np.array([0.0,1.0,0.0]), fwd); lat /= np.linalg.norm(lat)

prim = j["meshes"][0]["primitives"][0]; acc = j["accessors"][prim["attributes"]["POSITION"]]
bv = j["bufferViews"][acc["bufferView"]]; base = bv.get("byteOffset",0)+acc.get("byteOffset",0)
binchunk = chunks[1][1]; count = acc["count"]
pts = np.frombuffer(bytes(binchunk), dtype=np.float32, count=count*3, offset=base).reshape(count,3).copy()

C = (sp1 + neck) * 0.5
d = pts - C; f = d @ fwd; l = d @ lat; z = d[:,1]
H = float(pts[:,1].max() - pts[:,1].min())
# apex = the front-most vertex on the chest centreline
sel = (np.abs(l) < 0.05*H) & (np.abs(z) < 0.06*H) & (f > 0)
apex = float(f[sel].max()) if sel.sum() else 0.0
# ... and where it sits, so the Gaussian is centred on it
k = np.where(sel)[0][np.argmax(f[sel])]
lc, zc = 0.0, float(z[k])   # midline: a bust is symmetric, and a Gaussian peaking at the
                            # front-most vertex would have been lopsided by 6 cm
sl, sz = 0.045*H, 0.055*H

w_all = np.where(f > 0.0,
                 np.exp(-(((l - lc)**2)/(2*sl*sl) + ((z - zc)**2)/(2*sz*sz))), 0.0)
mask = w_all > 0.02
delta = fwd[None, :] * (GROW * apex * w_all[:, None])
pts2 = pts + delta * mask[:, None]

print("ED: H=%.1f apex=%.2f at (l=%.1f, z=%.1f) moved=%d  new apex=%.2f" % (
    H, apex, lc, zc, int(mask.sum()),
    float(np.max(((pts2 - C) @ fwd)[sel]))))

binchunk[base:base+count*12] = pts2.astype(np.float32).tobytes()
acc["min"] = [float(v) for v in pts2.min(axis=0)]
acc["max"] = [float(v) for v in pts2.max(axis=0)]
chunks[0][1] = bytearray(json.dumps(j, separators=(",", ":")).encode())
while len(chunks[0][1]) % 4: chunks[0][1].append(0x20)
out = bytearray(b"glTF"); out += struct.pack("<II", 2, 0)
for ctype, data in chunks: out += struct.pack("<II", len(data), ctype) + data
struct.pack_into("<I", out, 8, len(out))
open(DST, "wb").write(out)
print("ED: wrote %s" % DST)
