#!/usr/bin/env python3
"""Merge every glTF mesh in a glb into ONE mesh, so the importer produces one character.

    python3 tools/merge-glb-meshes.py <in.glb> <out.glb>

WHY (2026-11-01). `alina.glb` carries her body as 19 separate glTF meshes (one per material), all
sharing skin 0 and all on identity nodes. The engine's FBX importer has no "combine skeletal
meshes" option any more (5.8 removed it -- measured: the property does not exist), so 19 meshes
become 19 skeletal mesh assets, and the game -- and the retargeter -- address ONE mesh at
`characters_<tag>/idle`. Merging the primitives into a single mesh is JSON-only: no vertex data is
touched, because each primitive already points at its own accessors.

Two things it must also do, both measured:
 - **drop `primitive.targets`** (morph targets). Some of her parts have facial morphs and some do
   not, and Interchange refuses the merged mesh outright: "Number of Primitive.Targets is not
   consistent across the Mesh". This game animates bones, never morphs.
 - **keep exactly ONE node carrying the mesh.** The other 18 nodes stay in the hierarchy (they are
   skeleton children) but lose their mesh reference, or the character draws 19 times.
"""
import json
import struct
import sys
MAGIC, JSON_CHUNK = 0x46546C67, 0x4E4F534A
src, dst = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()
_magic, version, _len = struct.unpack("<III", data[:12])
chunks, off = [], 12
while off < len(data):
    clen, ctype = struct.unpack("<II", data[off:off + 8])
    chunks.append((ctype, data[off + 8:off + 8 + clen]))
    off += 8 + clen
out, dropped = [], 0
for ctype, payload in chunks:
    if ctype != JSON_CHUNK:
        out.append((ctype, payload)); continue
    doc = json.loads(payload)
    prims = []
    for m in doc["meshes"]:
        m.pop("weights", None)
        for p in m.get("primitives", []):
            if "targets" in p:
                del p["targets"]; dropped += 1
            prims.append(p)
    doc["meshes"] = [{"name": "alina_body", "primitives": prims}]
    kept = 0
    for node in doc["nodes"]:
        if "mesh" not in node: continue
        if kept == 0:
            node["mesh"], node["name"] = 0, "alina_body"
            node.setdefault("skin", 0); kept += 1
        else:
            del node["mesh"]; node.pop("skin", None)
    blob = json.dumps(doc, separators=(",", ":")).encode()
    blob += b" " * ((4 - len(blob) % 4) % 4)
    out.append((JSON_CHUNK, blob))
    print("MERGE: %d primitives, %d target sets dropped, %d mesh node(s)" % (len(prims), dropped, kept))
body = b"".join(struct.pack("<II", len(p), t) + p for t, p in out)
open(dst, "wb").write(struct.pack("<III", MAGIC, version, 12 + len(body)) + body)
