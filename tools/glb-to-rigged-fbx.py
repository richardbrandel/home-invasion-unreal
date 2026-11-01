#!/usr/bin/env python3
"""Convert a Sketchfab .glb character into an FBX this project's character pipeline can import.

    blender --background --python tools/glb-to-rigged-fbx.py -- <in.glb> <out.fbx>

WHY THIS EXISTS (2026-11-01). The pipeline wants `SourceAssets/characters/<tag>/idle.fbx` -- a
mesh-carrying FBX whose skeleton becomes the character's rig -- plus one FBX per clip, each bound
to that same rig. Sketchfab hands back .glb, and the rigs do not match ours:

    Sketchfab/Mixamo export :  mixamorig:Hips_00, mixamorig:Spine_01, ...
    our clips (this project):  mixamorig11:Hips, mixamorig11:Spine, ...

Different prefix AND a numeric suffix per bone, so the clips cannot bind. This renames every bone
to our convention -- `mixamorig:` -> `mixamorig11:`, trailing `_<digits>` stripped -- which is what
makes a downloaded character usable with the ten clips we already have, with no retargeter and no
Mixamo round trip.

The suffix stripping is deliberately conservative: it removes ONE trailing underscore-plus-digits
group, and only that. A bone genuinely named `Hand_02` (a finger phalanx) would collide with
`Hand_2`, so the script reports any collision and refuses rather than silently merging bones.
"""
import sys

import bpy


def target_name(name):
    """`mixamorig:Hips_00` -> `mixamorig11:Hips`; leaves anything else alone."""
    base = name
    if ":" in base:
        prefix, rest = base.split(":", 1)
        if prefix in ("mixamorig", "mixamorig11"):
            # strip ONE trailing _NN group
            if "_" in rest:
                head, tail = rest.rsplit("_", 1)
                if tail.isdigit():
                    rest = head
            return "mixamorig11:" + rest
    return base


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    src, dst = argv[0], argv[1]

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=src)

    armatures = [o for o in bpy.data.objects if o.type == 'ARMATURE']
    if not armatures:
        print("GLB2FBX: FAILED -- no armature in %s" % src)
        raise SystemExit(1)
    arm = armatures[0]
    bones = list(arm.data.bones)
    print("GLB2FBX: '%s' has %d bones" % (arm.name, len(bones)))

    renamed, collisions = 0, []
    for bone in bones:
        new = target_name(bone.name)
        if new == bone.name:
            continue
        if new in arm.data.bones:
            collisions.append("%s -> %s (already exists)" % (bone.name, new))
            continue
        bone.name = new
        renamed += 1
    print("GLB2FBX: renamed %d bones, %d collisions" % (renamed, len(collisions)))
    for c in collisions:
        print("GLB2FBX: COLLISION %s" % c)
    if collisions:
        print("GLB2FBX: FAILED -- refusing to merge bones")
        raise SystemExit(1)

    sample = [b.name for b in list(arm.data.bones)[:5]]
    print("GLB2FBX: first bones now: %s" % ", ".join(sample))

    # BLENDER 5.0 EXPOSES ALMOST NOTHING ON THIS OPERATOR (2026-11-01): only filepath,
    # check_existing, axis_forward and axis_up. Passing the 4.x keywords raises
    # "keyword 'use_selection' unrecognized", which is how this was found. The scene contains
    # exactly one armature and its mesh, so the defaults do the right thing and the axis options
    # are the only ones worth stating: UE wants Z-up, and Blender is already Z-up.
    bpy.ops.export_scene.fbx(filepath=dst, axis_forward='-Y', axis_up='Z')
    print("GLB2FBX: wrote %s" % dst)


main()
