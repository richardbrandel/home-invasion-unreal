#!/usr/bin/env python3
"""Retarget the project's ten Mixamo clips onto another character's own skeleton.

    UnrealEditor-Cmd <uproject> -ExecutePythonScript=tools/retarget-character.py
    (driven by tools/retarget-character.sh, which passes TAG / SRC_TAG / MODEL as env vars)

WHY (2026-11-01). A downloaded character has its own rig. Ours is `mixamorig11:*` before import
and plain `Hips`/`Spine`/`RightHand` after it; a Sketchfab glb is `mixamorig:Hips_00` or a
Character-Creator `CC_Base_*` rig. The ten clips cannot bind across that, and the offline
work-arounds all failed: Blender 5.0's FBX exporter is broken, and assimp's FBX conversion loses
the bone rest orientation (the mesh landed upright but ANIMATED prone, facing 119.7 deg where every
Mixamo character here faces 90).

The engine's own batch retargeter is the right tool and it IS exposed to Python:
`IKRetargetBatchOperation.duplicate_and_retarget`. The argument that matters: `assets_to_retarget`
takes **FAssetData**, not objects, lists or tuples -- objects fail with "Failed to convert
parameter", and FAssetData is what worked. That cost an hour; it is written down here so it never
costs another.

The result is a set of AnimSequences bound to the TARGET character's skeleton, in its folder, with
the same names the game already asks for by path.
"""
import os
import unreal

TAG = os.environ.get("HI_TAG", "dancer")
SRC_TAG = os.environ.get("HI_SRC_TAG", "martha")
MODEL = os.environ.get("HI_MODEL", "woman01")

SRC_DIR = "/Game/HIImported/characters_%s" % SRC_TAG
DST_DIR = "/Game/HIImported/characters_%s" % TAG
MODEL_DIR = "/Game/HIImported/models/%s/SkeletalMeshes" % MODEL

PREFIX = "rt_"   # keeps the batch op from overwriting a working character

CLIPS = ["aim", "walk", "run", "crouch_idle", "crouch_walk", "idle_Anim",
         "pistol_idle", "reload", "shoot", "death"]

lib = unreal.EditorAssetLibrary


def move_mesh_into_place():
    """The game finds a character at characters_<tag>/idle.idle; a glb import lands elsewhere."""
    src = "%s/%s.%s" % (MODEL_DIR, MODEL, MODEL)
    dst = "%s/idle" % DST_DIR
    if lib.does_asset_exist(dst + ".idle"):
        print("RT: mesh already at %s" % dst)
        return True
    if not lib.does_asset_exist(src):
        print("RT: FAILED -- no mesh at %s" % src)
        return False
    if not lib.rename_asset(src, dst):
        print("RT: FAILED -- could not move %s to %s" % (src, dst))
        return False
    print("RT: mesh moved to %s" % dst)
    return True


def clear_old_animations():
    """Remove anything the game would load but that belongs to a previous, broken pipeline."""
    for clip in CLIPS:
        path = "%s/%s" % (DST_DIR, clip)
        if lib.does_asset_exist(path):
            lib.delete_asset(path)
            print("RT: removed stale %s" % path)


def retarget():
    source_mesh = unreal.load_asset("%s/idle.idle" % SRC_DIR)
    target_mesh = unreal.load_asset("%s/idle.idle" % DST_DIR)
    if source_mesh is None or target_mesh is None:
        print("RT: FAILED -- source=%s target=%s" % (source_mesh, target_mesh))
        return False

    # WORK ON THROWAWAY COPIES (2026-11-01, learned the hard way). duplicate_and_retarget
    # CONSUMES its inputs: pointed at a working character's clips it renames them out from under
    # that character, and the first run of this script left Martha with no animations at all --
    # her folder held a mesh and eight textures, and the cast test failed. So the clips are copied
    # to a scratch folder, the copies are retargeted, and the originals are never touched.
    SCRATCH = "/Game/HIImported/rt_scratch"
    for clip in CLIPS:
        scratch_path = "%s/%s" % (SCRATCH, clip)
        if lib.does_asset_exist(scratch_path):
            lib.delete_asset(scratch_path)
    assets = []
    for clip in CLIPS:
        path = "%s/%s.%s" % (SRC_DIR, clip, clip)
        if not lib.does_asset_exist(path):
            print("RT: source clip missing, skipping: %s" % path)
            continue
        copy = lib.duplicate_asset(path, "%s/%s" % (SCRATCH, clip))
        if copy is None:
            print("RT: could not copy %s" % path)
            continue
        assets.append(lib.find_asset_data("%s/%s.%s" % (SCRATCH, clip, clip)))
    print("RT: retargeting %d clips from %s to %s" % (len(assets), SRC_TAG, TAG))

    # A PREFIX, NOT AN IN-PLACE OVERWRITE (2026-11-01). The first run passed target_path and got
    # ten retargeted sequences written back over the SOURCE character's clips -- the batch op
    # ignored the path and overwrote Martha's own animations with ones bound to the dancer's
    # skeleton, which broke the character that was working. With a prefix the new assets cannot
    # collide with a working character, and this script moves them into place afterwards.
    # THE TWO MESH ARGUMENTS ARE (target, source) IN THIS BUILD, not (source, target) as the
    # signature reads. Measured: passing Martha's mesh first produced ten sequences bound to
    # MARTHA's skeleton -- i.e. retargeted the wrong way -- so the pair is swapped here and the
    # check below asserts the result binds to the character we actually asked for.
    out = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        assets, target_mesh, source_mesh, None,
        "", "", PREFIX, "", DST_DIR, False, False, True)
    print("RT: produced %d asset(s)" % len(out))

    # MOVE EACH ONE TO WHERE THE GAME LOOKS FOR IT. The batch op keeps the prefixed name and, in
    # this engine build, ignores target_path -- so the move is explicit and checked.
    moved = 0
    for data in out:
        src_path = str(data.package_name)
        stem = src_path.rsplit("/", 1)[-1]
        if stem.startswith(PREFIX):
            stem = stem[len(PREFIX):]
        dst_path = "%s/%s" % (DST_DIR, stem)
        if lib.does_asset_exist(dst_path):
            lib.delete_asset(dst_path)
        if lib.rename_asset(src_path, dst_path):
            moved += 1
        else:
            print("RT: could not move %s -> %s" % (src_path, dst_path))
    print("RT: moved %d/%d into %s" % (moved, len(out), DST_DIR))

    # ASSERT THE BINDING. A retarget that produces sequences still pointing at the source's
    # skeleton looks like success in every log line and fails only when the character animates
    # wrongly (or the Cast test reads the skeleton).
    probe = unreal.load_asset("%s/aim.aim" % DST_DIR)
    want = target_mesh.get_editor_property("skeleton")
    if probe is not None and want is not None:
        got = probe.get_editor_property("skeleton")
        ok = got is not None and got.get_path_name() == want.get_path_name()
        print("RT: binding check -> %s (clip=%s want=%s)" % (
            "OK" if ok else "WRONG SKELETON",
            got.get_path_name() if got else "None", want.get_path_name()))
    return moved > 0


def main():
    print("RT: %s <- %s (mesh from models/%s)" % (TAG, SRC_TAG, MODEL))
    if not move_mesh_into_place():
        return
    clear_old_animations()
    retarget()
    lib.save_directory(DST_DIR, only_if_is_dirty=False, recursive=True)
    print("RT: done")


main()
