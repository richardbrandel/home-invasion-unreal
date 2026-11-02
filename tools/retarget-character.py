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

THE ROOT CAUSE, FOUND 2026-11-01 (this supersedes the "no IKRigFactory in this build" note in
docs/CHARACTERS.md). `duplicate_and_retarget` with `ik_retarget_asset=None` does not retarget
anything -- it logs "Batch retarget aborted. No IK Retargeter asset was specified.", copies the
clips and binds the copies to the SOURCE skeleton. Every log line around it looks like success, so
the failure only shows up as a wrong-skeleton clip at playback time. The factory was there all
along under a different name: **`unreal.IKRigDefinitionFactory`** (with a BlueprintCallable static
`create_new_ik_rig_asset`), not `IKRigFactory`. So the rigs, the retargeter and the chain mapping
are all creatable headlessly and this script now does them itself:

  IK_martha / IK_dancer        IK Rig per skeleton, chains from the auto-characterizer
  RT_dancer_from_martha        IK Retargeter, source/target rigs, chains auto-mapped (Exact)

Each asset is reused if it already exists, so re-running is cheap and idempotent.

The argument that costs an hour if you get it wrong: `assets_to_retarget` takes **FAssetData**, not
objects, lists or tuples -- objects fail with "Failed to convert parameter", and FAssetData is what
worked. The mesh pair is (source, target) as the signature reads.

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

# The rigs live in their own folder so a re-import of a character cannot take them with it.
IK_DIR = "/Game/HIImported/retarget"

PREFIX = "rt_"   # keeps the batch op from overwriting a working character

CLIPS = ["aim", "walk", "run", "crouch_idle", "crouch_walk", "idle_Anim",
         "pistol_idle", "reload", "shoot", "death"]

# KEEP IN STEP WITH tools/import-assets.py -- same modes, same failure tokens, and that file is
# the source of truth for why each clip gets the rule it gets. Duplicated here rather than
# imported because both scripts are run by the editor with only their own path on sys.path.
NORMALIZE_MODES = {
    "aim": "aim",
    "shoot": "shoot",
    "death": "none",
}
NORMALIZE_FAIL_TOKENS = ("FAILED", "MISSING", "NOT UNIFORM", "UNKNOWN MODE", "MISSED",
                         "nothing normalized")

lib = unreal.EditorAssetLibrary


def load(path):
    """`EditorAssetLibrary.load_asset` logs an error for every miss, and a miss is normal here.

    Every asset this script touches is optional: the rigs and the retargeter are created on first
    run and reused after that, so "not there yet" is the expected state, not a fault. Checking
    first keeps the log honest -- a real failure then stands out instead of hiding in the noise.
    """
    return lib.load_asset(path) if lib.does_asset_exist(path) else None


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


def ensure_ik_rig(tag, mesh):
    """Create (or reuse) the IK Rig for one character and auto-generate its retarget chains.

    `apply_auto_generated_retarget_definition` matches the skeleton against the engine's humanoid
    templates and writes the bone chains + pelvis. It returns False if nothing matched, which for
    a Mixamo-family rig means the bones were renamed beyond recognition -- worth a hard stop,
    because an empty chain list retargets nothing and looks like success.
    """
    name = "IK_%s" % tag
    path = "%s/%s" % (IK_DIR, name)
    rig = load("%s.%s" % (path, name))
    if rig is None:
        rig = unreal.IKRigDefinitionFactory.create_new_ik_rig_asset(IK_DIR, name)
        if rig is None:
            print("RT: FAILED -- could not create IK Rig %s" % path)
            return None
        print("RT: created IK Rig %s" % path)
    else:
        print("RT: reusing IK Rig %s" % path)

    controller = unreal.IKRigController.get_controller(rig)
    if controller is None:
        print("RT: FAILED -- no IKRigController for %s" % path)
        return None
    controller.set_skeletal_mesh(mesh)
    if not controller.apply_auto_generated_retarget_definition():
        print("RT: FAILED -- %s did not match a humanoid template (no retarget chains)" % name)
        return None
    lib.save_loaded_asset(rig)
    print("RT: %s has retarget chains" % name)
    return rig


def rig_chain_names(rig):
    """The chain names the auto-characterizer wrote onto an IK Rig.

    Not from the asset's `retarget_definition` member -- that is a plain C++ field with no UPROPERTY,
    so Python cannot read it ("Failed to find property"). The controller's BlueprintCallable
    `get_retarget_chains()` is the way in, and it returns FBoneChain structs.
    """
    controller = unreal.IKRigController.get_controller(rig)
    return [str(chain.get_editor_property("chain_name"))
            for chain in controller.get_retarget_chains()]


def unmapped_chains(controller, target_chains):
    """A target chain with source chain None copies nothing -- that limb would stay in bind pose."""
    return [n for n in target_chains if str(controller.get_source_chain(n)) in ("None", "")]


def ensure_retargeter(source_rig, target_rig):
    """Create (or reuse) the IK Retargeter and map every target chain onto a source chain."""
    name = "RT_%s_from_%s" % (TAG, SRC_TAG)
    path = "%s/%s" % (IK_DIR, name)
    retargeter = load("%s.%s" % (path, name))
    if retargeter is None:
        factory = unreal.IKRetargetFactory()
        retargeter = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, IK_DIR, unreal.IKRetargeter, factory)
        if retargeter is None:
            print("RT: FAILED -- could not create IK Retargeter %s" % path)
            return None
        print("RT: created IK Retargeter %s" % path)
    else:
        print("RT: reusing IK Retargeter %s" % path)

    controller = unreal.IKRetargeterController.get_controller(retargeter)
    if controller is None:
        print("RT: FAILED -- no IKRetargeterController for %s" % path)
        return None

    # A FRESH IK RETARGETER IS AN EMPTY ASSET, and that is not obvious. UIKRetargetFactory::
    # FactoryCreateNew only does NewObject<UIKRetargeter>() -- the editor's toolkit adds the ops
    # when it opens the asset. With no op stack there is no chain-mapping op for AutoMapChains to
    # write into, so mapping silently comes back empty and the batch retarget degenerates to a
    # copy. Measured 2026-11-01: 21 target chains, 0 mapped, both with Exact and with Fuzzy.
    # GUARDED BY THE OP COUNT, because AddDefaultOps is NOT as idempotent as its comment claims:
    # on a retargeter that already had 5 ops it produced 9 (measured, re-run of the same command).
    # A growing stack would re-apply ops on every run, so only an EMPTY asset gets the defaults.
    if controller.get_num_retarget_ops() == 0:
        controller.add_default_ops()
    else:
        print("RT: retargeter already has an op stack, keeping it")
    controller.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, source_rig)
    controller.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target_rig)
    # ...and push the rigs into every op, which is what refreshes each op's own chain mapping.
    controller.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, source_rig)
    controller.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, target_rig)
    print("RT: %d retarget op(s) in the stack" % controller.get_num_retarget_ops())

    target_chains = rig_chain_names(target_rig)
    source_chains = rig_chain_names(source_rig)
    print("RT: chains -- source %d, target %d" % (len(source_chains), len(target_chains)))
    if not target_chains:
        print("RT: FAILED -- the target IK Rig has no chains")
        return None

    # EXACT first: both rigs come off the same humanoid template, so the chain names should agree
    # one-for-one. FUZZY is the fallback for a rig whose chains were named by hand, and it is only
    # used when something is left unmatched -- it rewrites every mapping, which would be a bad
    # trade if exact matching had already worked.
    controller.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    missing = unmapped_chains(controller, target_chains)
    if missing:
        print("RT: %d chain(s) unmatched exactly (%s), retrying FUZZY"
              % (len(missing), ", ".join(missing)))
        controller.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
        missing = unmapped_chains(controller, target_chains)
    mapped = len(target_chains) - len(missing)
    print("RT: %d/%d target chain(s) mapped" % (mapped, len(target_chains)))
    if mapped == 0:
        print("RT: FAILED -- no chain mapped, the retarget would copy and not retarget")
        return None
    if missing:
        print("RT: still unmapped (will stay in bind pose): %s" % ", ".join(missing))
    lib.save_loaded_asset(retargeter)
    return retargeter


def retarget():
    source_mesh = load("%s/idle.idle" % SRC_DIR)
    target_mesh = load("%s/idle.idle" % DST_DIR)
    if source_mesh is None or target_mesh is None:
        print("RT: FAILED -- source=%s target=%s" % (source_mesh, target_mesh))
        return False

    source_rig = ensure_ik_rig(SRC_TAG, source_mesh)
    target_rig = ensure_ik_rig(TAG, target_mesh)
    if source_rig is None or target_rig is None:
        return False
    retargeter = ensure_retargeter(source_rig, target_rig)
    if retargeter is None:
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
    # ignored the path and overwrote Martha's own animations. With a prefix the new assets cannot
    # collide with a working character, and this script moves them into place afterwards.
    # The mesh pair is (source, target), i.e. copy FROM Martha TO the dancer, as the C++ signature
    # reads. Verified by the checks below rather than by trusting the header.
    out = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        assets, source_mesh, target_mesh, retargeter,
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

    # NORMALIZE, because a retargeted clip is NOT automatically a normalized one (2026-11-01).
    # The retargeter recomputes the pelvis from its retarget pose, so a clip that was pinned flat
    # on Martha comes back with Hips travel: measured on the first complete run, walk 6.12 cm,
    # run 5.69, crouch_walk 2.99, pistol_idle 10.04, shoot 8.66 against HI.Unreal.Cast's 2.0 cm
    # limit. The sim owns position, so these have to be pinned again after the retarget -- exactly
    # as tools/import-assets.py pins an imported clip. The wrist raise is deliberately 0.0: this
    # copies ARMS from the source, which was already raised at ITS import, so re-raising would
    # double it.
    tool = getattr(unreal, "HIAnimTool", None)
    if tool is None:
        print("RT: FAILED -- unreal.HIAnimTool is missing (the C++ build is older than this "
              "script); the clips would stay unnormalized and skate")
        return False
    normalized = 0
    for clip in CLIPS:
        pkg = "%s/%s.%s" % (DST_DIR, clip, clip)
        if not lib.does_asset_exist(pkg):
            continue
        report = tool.normalize_character_clip(pkg, NORMALIZE_MODES.get(clip, "locomotion"), 0.0)
        print("RT: normalize %s: %s" % (clip, report))
        if not any(tok in report for tok in NORMALIZE_FAIL_TOKENS):
            normalized += 1
    print("RT: normalized %d/%d clip(s)" % (normalized, len(CLIPS)))
    if normalized < len(CLIPS):
        print("RT: FAILED -- some clips did not normalize; the game would skate")

    # ASSERT THE BINDING, ON EVERY CLIP. A retarget that produces sequences still pointing at the
    # source's skeleton looks like success in every log line and fails only when the character
    # animates wrongly (or the Cast test reads the skeleton). Checking one clip was not enough:
    # the failure mode here is a class of assets, not a single one.
    want = target_mesh.get_editor_property("skeleton")
    want_path = want.get_path_name() if want else "None"
    wrong = []
    for clip in CLIPS:
        seq = load("%s/%s.%s" % (DST_DIR, clip, clip))
        if seq is None:
            wrong.append("%s (missing)" % clip)
            continue
        got = seq.get_editor_property("skeleton")
        got_path = got.get_path_name() if got else "None"
        if got_path != want_path:
            wrong.append("%s (got %s)" % (clip, got_path))
    if wrong:
        print("RT: binding check -> WRONG SKELETON, want %s" % want_path)
        for item in wrong:
            print("RT:   %s" % item)
        return False
    print("RT: binding check -> OK, all %d clips bound to %s" % (len(CLIPS), want_path))
    return moved > 0


def main():
    print("RT: %s <- %s (mesh from models/%s)" % (TAG, SRC_TAG, MODEL))
    if not move_mesh_into_place():
        return
    clear_old_animations()
    if not retarget():
        # Loud, and a non-zero exit is not available from inside the editor's Python, so the
        # failure has to be legible in the log the shell script prints.
        print("RT: FAILED -- see the lines above")
    lib.save_directory(DST_DIR, only_if_is_dirty=False, recursive=True)
    print("RT: done")


main()
