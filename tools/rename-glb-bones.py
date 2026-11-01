#!/usr/bin/env python3
"""Rename a glb's skeleton bones to this project's Mixamo convention, in place.

    python3 tools/rename-glb-bones.py <in.glb> <out.glb>

WHY (2026-11-01). The character pipeline wants `SourceAssets/characters/<tag>/idle.<ext>` whose
skeleton becomes the character's rig, with one FBX per clip bound to that same rig. Sketchfab
returns .glb, and its rigs are named differently from our clips:

    Sketchfab  : mixamorig:Hips_00, mixamorig:Spine_01, ...   (prefix + numeric suffix)
    our clips  : mixamorig11:Hips, mixamorig11:Spine, ...     (different prefix, no suffix)

so the clips cannot bind. Renaming HERE -- in the glTF's JSON, before import -- means the engine
creates a skeleton with the right names and the existing clips attach with no retargeter.

Blender was tried first and its FBX exporter is broken in 5.0
(`AttributeError: 'ExportFBX' object has no attribute 'use_space_transform'`), and Mixamo's web UI
needs the mesh uploading by hand. Editing the JSON needs neither.

The rename is deliberately conservative: only node names that look like `mixamorig:<Bone>_<digits>`
are touched, and a rename that would collide with an existing node is refused rather than merged.
"""
import json
import struct
import sys

MAGIC = 0x46546C67      # 'glTF'
JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942


def new_name(name):
    if not isinstance(name, str) or ":" not in name:
        return name
    prefix, rest = name.split(":", 1)
    if prefix not in ("mixamorig", "mixamorig11"):
        return name
    if "_" in rest:
        head, tail = rest.rsplit("_", 1)
        if tail.isdigit():
            rest = head
    # STRIP THE NAMESPACE ENTIRELY, which is what the engine's own FBX import does: a source
    # bone `mixamorig8:Hips` becomes `Hips` in the imported skeleton (recorded in
    # HITests.cpp's Cast test). Keeping a prefix here produced `mixamorig11:Hips`, so the mesh
    # imported with no `RightHand` bone and the weapon-grip assertion failed.
    return rest


def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    magic, version, _length = struct.unpack("<III", data[:12])
    if magic != MAGIC:
        print("RENAME: %s is not a glb" % src)
        raise SystemExit(1)

    chunks = []
    off = 12
    while off < len(data):
        clen, ctype = struct.unpack("<II", data[off:off + 8])
        chunks.append((ctype, data[off + 8:off + 8 + clen]))
        off += 8 + clen

    out_chunks = []
    renamed = 0
    for ctype, payload in chunks:
        if ctype != JSON_CHUNK:
            out_chunks.append((ctype, payload))
            continue
        doc = json.loads(payload)
        nodes = doc.get("nodes", [])
        existing = {n.get("name") for n in nodes}
        changes = {}
        for node in nodes:
            old = node.get("name")
            fresh = new_name(old)
            if fresh != old:
                if fresh in existing:
                    print("RENAME: COLLISION %s -> %s already exists; refusing" % (old, fresh))
                    raise SystemExit(1)
                changes[old] = fresh
        for node in nodes:
            old = node.get("name")
            if old in changes:
                node["name"] = changes[old]
                renamed += 1
        for anim in doc.get("animations", []):
            for ch in anim.get("channels", []):
                tgt = ch.get("target") or {}
                if "node" in tgt:
                    continue
        blob = json.dumps(doc, separators=(",", ":")).encode("utf-8")
        blob += b" " * ((4 - len(blob) % 4) % 4)
        out_chunks.append((JSON_CHUNK, blob))

    body = b""
    for ctype, payload in out_chunks:
        body += struct.pack("<II", len(payload), ctype) + payload
    total = 12 + len(body)
    with open(dst, "wb") as f:
        f.write(struct.pack("<III", MAGIC, version, total))
        f.write(body)
    print("RENAME: %d bones -> %s" % (renamed, dst))


main()
