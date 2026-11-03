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
import os
import struct
import sys

MAGIC = 0x46546C67      # 'glTF'
JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942


# CHARACTER CREATOR -> OUR NAMES (2026-11-01). `alina.glb` is a CC_Base rig, and the rename
# above only speaks `mixamorig`. Two things in the game address bones BY NAME -- `HandBoneFor`
# parents the pistol to `RightHand`, and the wound placement reads `Spine2`/`RightArm`/`LeftUpLeg`
# -- and the IK auto-characterizer recognises humanoid TEMPLATES, which is what makes a retarget
# need no hand-mapping. So the names have to become ours; leaving `CC_Base_R_Hand` would give a
# floating pistol, no wounds, and probably no retarget chains at all.
#
# Only the bones the game and the characterizer actually use are mapped. Twist bones, eyes, teeth,
# tongue, jaw, breasts and the toe-share bones keep their CC names: nothing reads them, and a
# wrong guess there is worse than an unchanged name.
CC_MAP = {
    "Hip": "Hips",
    "Waist": "Spine",
    "Spine01": "Spine1",
    "Spine02": "Spine2",
    "NeckTwist01": "Neck",
    "Head": "Head",
    "L_Clavicle": "LeftShoulder",
    "R_Clavicle": "RightShoulder",
    "L_Upperarm": "LeftArm",
    "R_Upperarm": "RightArm",
    "L_Forearm": "LeftForeArm",
    "R_Forearm": "RightForeArm",
    "L_Hand": "LeftHand",
    "R_Hand": "RightHand",
    "L_Thigh": "LeftUpLeg",
    "R_Thigh": "RightUpLeg",
    "L_Calf": "LeftLeg",
    "R_Calf": "RightLeg",
    "L_Foot": "LeftFoot",
    "R_Foot": "RightFoot",
    "L_ToeBase": "LeftToeBase",
    "R_ToeBase": "RightToeBase",
}
# Fingers, built rather than typed: <Side>_<CC finger><n> -> <Side>Hand<Mixamo finger><n>.
for _side, _ours in (("L", "Left"), ("R", "Right")):
    for _cc, _mix in (("Thumb", "Thumb"), ("Index", "Index"), ("Mid", "Middle"),
                      ("Ring", "Ring"), ("Pinky", "Pinky")):
        for _n in ("1", "2", "3"):
            CC_MAP["%s_%s%s" % (_side, _cc, _n)] = "%sHand%s%s" % (_ours, _mix, _n)


def new_name(name):
    if not isinstance(name, str):
        return name
    if name.startswith("CC_Base_"):
        rest = name[len("CC_Base_"):]
        # `CC_Base_L_Thigh_04` -> `L_Thigh`; one trailing _<digits> group only, so a bone that
        # genuinely ends in a digit keeps it.
        if "_" in rest:
            head, tail = rest.rsplit("_", 1)
            if tail.isdigit():
                rest = head
        return CC_MAP.get(rest, name)
    if ":" not in name:
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
        # UP-AXIS FIX (2026-11-01): add a wrapper root carrying +90 deg about X, turning glTF's
        # Y-up into Z-up. Without it the converted FBX lands in UE lying down -- measured on the
        # Dancer Girl: extent (71.6, 80.6, 18.3) against Martha's (83.5, 14.1, 88.5), i.e. a body
        # 18 cm tall and 80 cm deep, flat on the lawn where nothing obvious is wrong except that
        # she never appears upright.
        # GLB_WRAPPER=0 SKIPS THE Z-Up ROOT. It exists because assimp does not convert
        # glTF's Y-up; the engine's own glTF (Interchange) importer DOES, so adding the
        # wrapper on that path rotates the character twice -- measured on alina.glb:
        # extent (144, 173, 36) with the wrapper against (144, 36, 173)... i.e. lying down.
        import math
        scale = float(os.environ.get("GLB_SCALE", "1.0"))
        if os.environ.get("GLB_WRAPPER", "1") == "0" and abs(scale - 1.0) < 1e-6:
            print("RENAME: GLB_WRAPPER=0, no ZUpRoot (the engine converts Y-up itself)")
            blob = json.dumps(doc, separators=(",", ":")).encode("utf-8")
            blob += b" " * ((4 - len(blob) % 4) % 4)
            out_chunks.append((JSON_CHUNK, blob))
            continue
        half = math.radians(90.0) * 0.5
        # YAW CORRECTION (2026-11-01): every Mixamo character in this project faces +Y once
        # imported (MeshFacingYawDeg() == 90, measured per character). This one's bind pose faced
        # 119.7 deg, which the Cast test caught, so the wrapper also carries a -29.7 deg turn
        # about Z. Composed as qx * qz: the Z turn happens first in the model's own frame.
        yaw = math.radians(float(os.environ.get("GLB_YAW_DEG", "0.0"))) * 0.5
        qx = (math.sin(half), 0.0, 0.0, math.cos(half))
        qz = (0.0, 0.0, math.sin(yaw), math.cos(yaw))
        ax, ay, az, aw = qx; bx, by, bz, bw = qz
        rot = (aw*bx + ax*bw + ay*bz - az*by,
               aw*by - ax*bz + ay*bw + az*bx,
               aw*bz + ax*by - ay*bx + az*bw,
               aw*bw - ax*bx - ay*by - az*bz)
        # GLB_SCALE (2026-11-01): `alina.glb` imports at 345 cm tall against Martha's 177 --
        # exactly 2x -- so the wrapper carries the correction. The engine has no per-import
        # scale on the Interchange path we use, and a uniform scale on a parent node scales the
        # whole hierarchy (bones included), which is what the retarget then reads.
        doc.setdefault("nodes", []).append({
            "name": "ZUpRoot",
            "rotation": [rot[0], rot[1], rot[2], rot[3]] if os.environ.get("GLB_WRAPPER", "1") != "0" else [0.0, 0.0, 0.0, 1.0],
            "scale": [scale, scale, scale],
            "children": list(doc.get("scenes", [{}])[doc.get("scene", 0)].get("nodes", [])),
        })
        doc["nodes"][-1]["children"] = [i for i in doc["nodes"][-1]["children"]]
        for scene in doc.get("scenes", []):
            scene["nodes"] = [len(doc["nodes"]) - 1]
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
