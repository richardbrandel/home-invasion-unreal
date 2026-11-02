# Female intruders — candidates checked, and what the pipeline actually needs

Written 2026-10-31 after pulling five Sketchfab models and measuring each one. The point of this
file is that the next search starts from what has been ruled out.

## What the pipeline needs

`SourceAssets/characters/<tag>/` holding `idle.fbx` (the mesh-carrying export) **plus one FBX per
clip**: `aim`, `shoot`, `reload`, `walk`, `run`, `crouch_idle`, `crouch_walk`, `death`,
`pistol_idle`. Each character gets its OWN `USkeleton`, and clips bind only to the skeleton they
were exported against. A character with a different rig therefore has to be **retargeted** or
**re-rigged** before any of our animation plays on it.

## Candidates measured (all CC BY, downloaded through tools/fetch-sketchfab.py)

| Model | Author | Faces | Joints | Rig family | Verdict |
|---|---|---|---|---|---|
| Miss Galaxy | Loves_Art | 49,788 | 75 (65 `mixamorig:`) | **Mixamo** | Right rig, wrong subject — the atlas is a galaxy-print pageant gown |
| Every, Jane | pinckneyb | 99,045 | 25 | custom | Cartoon casual woman; only 25 joints, no fingers, so the pistol cannot parent to a hand |
| Your College Girlfriend | pinckneyb | 86,149 | 102 (`CC_Base_*`) | Character Creator | Good proportions, wrong rig for our clips; would need a retargeter |
| Girl with clothes (worker set) | cattleya | 2,469 | 68 (`girlBone_*`) | custom | Too low-poly, custom rig |
| Low Poly Female | Loves_Art | 4,384 | — | — | Not fetched; low-poly only |

**Only one of the five is Mixamo-rigged**, and it happens to be dressed as a galaxy. That is the
core problem: Mixamo-rigged CC-BY characters that look like modern women are rare on Sketchfab.

## The two routes that actually produce a playable character

1. **Mixamo auto-rigger** (free Adobe account). Upload any humanoid mesh — Sketchfab, a paid
   store, anywhere — let Mixamo rig it to `mixamorig`, then download it **with the ten clips**.
   That normalises any body to the skeleton our cast already uses. This is the shortest path and
   the one this project's pipeline was built for.
2. **UE IK Retargeter** (in-engine). Bring a `CC_Base_*` or custom rig and retarget the Mixamo
   clips onto it. More setup, and the project has a history of misdiagnosing this area — see
   `tools/import-assets.py`, which records that retargeting is NOT needed for Mixamo-origin
   characters, only for foreign rigs like these.

## Style decision that outranks the asset hunt

The existing cast (Louise, James, Brute, Martha) is **photogrammetry scans of real people**. Every
candidate above is **stylised or cartoon**. Dropping one in beside them will read as two different
games. Decide first: restyle the whole intruder cast as stylised, or find a realistic curvy model
(paid stores are the realistic route — Sketchfab's CC-BY pool is thin on realistic clothed women).

## Open

The in-engine art preview (`-HIModels=`, 2026-10-31) places and scales a candidate correctly but
does not draw it yet — fix that before the next round of candidates, so each one costs one frame
instead of one paragraph.

## The retarget route: SOLVED (2026-11-01)

The engine's own retargeter is exposed to Python and is the right tool. `tools/retarget-character.sh
<tag> <source-tag> <model>` now does the whole job — rigs, retargeter, chain mapping and the batch
— so nothing here needs the editor GUI. Findings, all measured:

- **`IKRetargetBatchOperation.duplicate_and_retarget` takes `FAssetData`, not objects.** Passing
  `[clip]`, a tuple, or `unreal.Array(...)` all fail with "Failed to convert parameter
  'assets_to_retarget'". `unreal.EditorAssetLibrary.find_asset_data(path)` works.
- **It CONSUMES its inputs.** Pointed at a working character's clips it renames them out from
  under that character: Martha lost all ten animations in one run (mesh and textures survived,
  and `HI.Unreal.Cast` caught it immediately). Any future use MUST copy the clips to a scratch
  folder first -- `tools/retarget-character.sh` now does.
- **With no retargeter asset it does not retarget at all.** `ik_retarget_asset=None` logs "Batch
  retarget aborted. No IK Retargeter asset was specified." and then COPIES: the ten sequences come
  out bound to `characters_martha/idle_Skeleton`. Every log line around it reads like success, which
  is why this looked like a binding bug for a day. Swapping the two mesh arguments changes nothing.
- **The factory exists — the earlier note had the wrong class name.** It is
  `unreal.IKRigDefinitionFactory` (BlueprintCallable static `create_new_ik_rig_asset(path, name)`),
  not `IKRigFactory`, which does not exist in any build. `unreal.IKRetargetFactory` is exposed too,
  and `AssetTools.create_asset(name, dir, unreal.IKRetargeter, factory)` makes the retargeter.
- **A FRESH IK RETARGETER IS AN EMPTY ASSET, and that is the second silent failure.** Its factory
  only does `NewObject<UIKRetargeter>()`; the editor's toolkit adds the op stack when it opens one.
  With no ops there is nothing for `AutoMapChains` to write into, so `SetIKRig` + `AutoMapChains`
  reports **0 of 21 chains mapped** and the retarget again degrades to a copy. `AddDefaultOps()`
  first — **but only on an empty asset.** Its comment claims already-present ops are not re-added;
  measured, a second call on a 5-op stack produced **9**, so `tools/retarget-character.py` gates it
  on `GetNumRetargetOps() == 0` and a re-run now leaves the stack at 5.
- **The auto-characterizer does the chains on both sides.** `IKRigController
  .apply_auto_generated_retarget_definition()` returned true for Martha's 65-bone and the Dancer
  Girl's 66-bone skeleton, 21 chains each, and `AutoMapChains(Exact)` then matched **21/21** — no
  fuzzy pass needed. Both are Mixamo-family rigs, so the chain names agree one-for-one.
- **Read chains from the controller, not the asset.** `UIKRigDefinition::RetargetDefinition` is a
  plain C++ member with no `UPROPERTY`, so `get_editor_property("retarget_definition")` fails with
  "Failed to find property". `IKRigController.get_retarget_chains()` is the scripting API.
- The mesh pair is **(source, target)**, as the C++ signature reads. The earlier "the arguments are
  swapped in this build" note was a misreading of the copy-with-no-retargeter behaviour; with a real
  retargeter, source-first produces clips bound to the target skeleton.

**Verified, not assumed:** all ten retargeted clips in `characters_dancer/` report
`skeleton == characters_dancer/idle_Skeleton`; the saved retargeter reports source `IK_martha`,
target `IK_dancer`, 5 ops, and `Spine/Head/LeftArm/LeftLeg` mapped; and the retargeted keys are NOT
Martha's numbers (Hips at frame 0 is (13.7, -97.5, -13.9) against Martha's (0.3, -7.1, 97.2)), which
is what separates a real resample from a copy.

## What still blocks putting her in the cast: her bind-pose facing

`HI.Unreal.Cast` fails on her with

    Expected 'dancer's bind pose faces yaw 119.7 deg, so FHICharAnim::MeshFacingYawDeg() must say so
    (it says 90.0)'

so every other fact about her passes with her in slot 0 — mesh, skeleton, physics asset, all ten
clips bound to HER skeleton, their lengths, the 22 rig bone names, the normalized Hips keys and the
death fall — and the suite is **51/52**, the one failure being that constant. Her compile-time slot
is left at `james` until it is resolved; flipping `FHICharAnim::IntruderTagForId` to `TEXT("dancer")`
is the whole swap.

A related measurement worth checking first, because the two may be one bug. From the **skeleton
asset's** reference pose, composed into component space in Python: her `hips -> head` is
(0.0, 39.1, 4.5) — i.e. along **+Y**, lying down — while Martha's is (0.0, 0.2, 55.1), upright along
+Z; her lateral (LeftShoulder -> RightShoulder) is (-9.0, 0, 0.03), giving a facing of **90.0**. So
the skeleton asset says "upright facing 90", the mesh (which is what `HI.Unreal.Cast` measures and
what skins the body) says "119.7", and the retargeted clips express her Hips in that same rotated
space. A mesh/skeleton reference-pose disagreement would explain all three at once, and it is also
why `tools/retarget-character.py` prints her Hips as (13.7, -97.5, -13.9).

Two routes, and the first is the one to try because it fixes the cause rather than the symptom:

1. **Reconcile the reference poses** — re-import her mesh so the mesh's ref skeleton and the
   skeleton asset agree (the `ZUpRoot` wrapper in `tools/rename-glb-bones.py` is where the up-axis
   fix lives, and a yaw belongs beside it), then delete `IK_dancer`/`RT_dancer_from_martha` and
   re-run `tools/retarget-character.sh` — it recreates both and re-retargets in one command.
2. **Carry her number in code** — `MeshFacingYawDeg()` becomes a per-tag table, exactly like
   `AuthoredCmPerSec` two functions below it (which already has a measured `dancer` row), with 119.7
   for her and `HI.Unreal.Cast` asserting the per-tag value. Cheap, but it papers over a suspect
   asset and cannot be right if the 29.7 deg is not a pure yaw.

`alina.glb` is staged in `SourceAssets/models` for the same pipeline; it has a `CC_Base_*` rig, so
expect the auto-characterizer to need more help than the Dancer Girl's Mixamo-family one did.
