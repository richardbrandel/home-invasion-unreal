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

## The bind-pose facing, and three more assumptions only she broke (2026-11-01)

**She is in the cast and the suite is 52/52.** Slot 0 is `TEXT("dancer")`; James's clips, physics
asset and travel row are untouched, so putting him back is that one string.

`HI.Unreal.Cast` used to fail on her with

    dancer's bind pose faces yaw 119.7 deg, so FHICharAnim::MeshFacingYawDeg() must say so (it says 90.0)

Four things had to change, and the first three are why one constant was defensible until now:

1. **Facing is per character.** `FHICharAnim::FacingYawDeg(Tag)`, a measured table beside
   `AuthoredCmPerSec`: louise 90.1, james 90.1, brute 90.0, martha 89.9, **dancer 119.7**. Measured
   to be a PURE yaw rather than a tipped rig — `lateral=(-0.87, -0.50, 0.00)` is exactly horizontal
   and `hips->head=(2.2, -3.9, 39.1)` points +Z — so rotating the component by her own number is the
   complete fix; no re-import, and nothing about how her clips are authored changes. `BodyRotation`
   takes the tag; the composition test loops every character, because a cast-wide constant no longer
   exists to be tested once. The test now logs those two vectors next to the angle, since a future
   character that fails may be TIPPED rather than twisted, and that needs the asset fixed instead.
2. **A retargeted clip is not a normalised clip.** The retargeter recomputes the pelvis from its
   retarget pose, so Hips travel came back at **6.12 cm** (walk), 5.69 (run), 2.99 (crouch_walk),
   10.04 (pistol_idle), 8.66 (shoot) against the test's 2.0 cm limit — motion the sim owns.
   `tools/retarget-character.py` now runs `HIAnimTool.normalize_character_clip` over its own output,
   with the same clip→mode table as `tools/import-assets.py` (the wrist raise is deliberately 0.0:
   the arms were already raised on the source, so re-raising would double it).
3. **`UHIAnimTool` refused `aim` outright.** Her retargeted `aim` has a Hips track of ONE key — the
   pelvis never moves — and the uniformity guard read that as "not frames+1" and declined to touch
   the clip, so it could never be normalised. A single key cannot vary; a constant track is the
   degenerate uniform case and is now accepted.
4. **The Cast test assumed the Hips' parent is unrotated.** It read the clip's Hips mean `Z` as the
   pelvis height, which is world-up only when the Hips sit directly under the skeleton root. Hers has
   `_rootJoint` above them (the `ZUpRoot` wrapper from `tools/rename-glb-bones.py` carries a -90 deg X
   rotation), so her 98 cm pelvis is stored on **-Y**. The check now projects the Hips translation
   onto the parent's up axis, which is the same assertion under either convention.

Two things left, neither blocking:

- **Her asset could still be made to face 90** by adding a yaw to that same `ZUpRoot` wrapper and
  re-importing, which would retire the per-tag table. Optional: the table is measured, the test
  recomputes it, and nothing else in the game cares.
- **The skeleton asset's stored reference pose disagrees with the mesh's.** Read from the skeleton
  asset, her `hips->head` is `(0.0, 39.1, 4.5)` — lying along +Y — while the mesh (what renders and
  what the test measures) is upright. Harmless for rendering, and it is why her clip Hips values look
  rotated; worth understanding before a second glTF character arrives.
- Cosmetic: normalising `aim` reports `MISSED` for 12 bones. They have no tracks because the batch
  retarget exported only animated bones, and a bone with no track cannot move — benign, but noisy.

`alina.glb` is staged in `SourceAssets/models` for the same pipeline; it has a `CC_Base_*` rig, so
expect the auto-characterizer to need more help than the Dancer Girl's Mixamo-family one did.

## Alina: the rig imports correctly IF Blender is fed the repaired glb (2026-11-03)

The auto-characterizer needed no help at all in the end -- `IK_alina` characterised to 21 chains
and mapped 21/21, exactly as the Dancer Girl did. The whole four-pass blocker was the input file:
Blender positions skinned bones from the skin's **inverse-bind matrices**, and `alina.glb` ships 21
of its 102 as zero-translation placeholders, so Blender put her hips on the floor and every importer
downstream inherited a 155 cm spine on a 176 cm body.

| input to `blender-reexport.py` | hips | head | `hips->head` |
|---|---|---|---|
| raw `alina.glb` | 0.000 m (0% of height) | 1.550 m | **155.0 cm** |
| `fix-glb-skin.py` output | 0.986 m (55.8%) | 1.550 m | **56.4 cm** |

Martha's is 55.1 cm, so the repaired path is in family. The engine agrees to 0.4 cm: `Cast` logs
`alina: 102 bones, bone[0] is '_rootJoint'`, `lateral=(-1.00, 0.00, 0.00)` and `hips->head` 56.4 --
so `FacingYawDeg` needs no row for her (she is a clean 90). `tools/blender-reexport.py` now refuses
to write a rig whose hips are outside 40-65% of height, which is the check that would have caught
this on day one. Remaining work is clip-side only; see `NEXT.md`.


## Re-importing a character glb: three things that cost days (2026-11-05)

Written down after the Dancer Girl's chest edit shipped, because every one of them presented as a
different bug -- a broken retarget, a flat pelvis, a missing physics asset -- and none of them was.

1. **The wrapper in `tools/rename-glb-bones.py` is for the LEGACY glTF importer.** The Interchange
   importer in this build does the Y-up -> Z-up conversion itself and scales glTF metres to
   centimetres, so `GLB_WRAPPER=1 GLB_SCALE=1` applies both twice: the mesh arrives 100x too large
   and lying down (bounds `(6278, 8059, 3706)` where her real half-extents are
   `(62.79, 37.06, 80.59)`). Anything built from that -- a retarget pose especially -- is flat, which
   reads as "the pelvis has no motion". **For a glb imported by this engine use
   `GLB_WRAPPER=0 GLB_SCALE=0.01`.** The correct import is verified by the mesh's Z half-extent
   (80.59 cm for her, 1.612 m tall).
2. **The importer REUSES an existing skeleton of the same name at the destination.** A folder left
   over from an older import layout (`characters_dancer/idle/SkeletalMeshes/idle_Skeleton`, bones
   named `mixamorig11_Hips`) silently became the new mesh's skeleton. Check the bone names of the
   mesh you just imported (`Hips`, not `mixamorig11_Hips`); empty the destination first if they are
   wrong. `unreal.SkeletalMeshEditorSubsystem.get_bone_children` walks them from `_rootJoint`.
3. **A skeletal mesh's bone space is part of its content, and clips do not survive changing it.** Her
   old `_rootJoint` carried -90 deg X, so the Hips' parent space had UP on -Y; an Interchange import
   bakes the same conversion into the vertex data and leaves `_rootJoint` at identity, UP on +Z.
   Rendered geometry, facing (119.7 deg) and `hips->head` (39.1 cm) are IDENTICAL either way -- only
   the clip's Hips track tells them apart, reading -7 cm against +98. So a mesh cannot be swapped
   under an existing clip set: import the mesh and skeleton as ONE pair, then retarget the clips onto
   them (`tools/retarget-character.sh <tag> martha woman01`). Confirmed working 2026-11-05: 21/21
   chains, `Hips mean 98 cm standing, 48 cm crouching`, all ten clips bound to the new skeleton.

4. **A rig whose inverse bind matrices disagree with its node hierarchy draws as SPIKES, and only
   while animating.** `tools/fix-glb-skin.py` is for it, and the Dancer's glb needed it for thirteen
   joints -- the whole thumb and every fingertip, both hands -- whose matrix put the bone 1.37 m from
   its parent where the nodes put it 1.5 cm. The bind pose is unaffected (a bone's inverse cancels
   its own transform, however wrong that transform is), so the mesh looks perfect until a bone
   ROTATES and flings its vertices a metre and a half: *"her hands turn into what looks like long
   swords, sometimes."* Two things made the tool refuse her file, and both are now handled: a
   UNIFORM scale on the rig (`_rootJoint` 0.01 with 100x offsets below it) and a transform on the
   MESH node (`Object_7`, 18.5 cm off the skeleton's origin -- the bind must be
   `inverse(jointWorld) * meshNodeWorld`, in mesh space, or the mesh binds away from its own bones).
   Its self-check now asserts `jointWorld * IBM == meshWorld` for every joint; a run that cannot
   satisfy it refuses to write. `HI.Unreal.Cast` asserts `CheckBindProportions` for every character,
   which is the cheap half: no bone segment may exceed 30% of the body's height.

5. **A rig that carries an import SCALE makes retargeted clips 100x.** The Interchange glTF import
   of a wrapped glb keeps the wrapper's scale in the rig (`_rootJoint` scale 0.01, every other bone's
   local translation 100x, cancelling in component space). IK Retargeting copies those offsets and
   writes scale 1.0, so the cancellation is lost and the animated figure is ~100x -- measured: a
   79 m Dancer with a perfect 98 cm pelvis and every position/rotation assertion green. Fix:
   `UHIAnimTool::BakeClipRootScale(clip, mesh, factor)` (already called by
   `tools/retarget-character.py`, factor read from the skeleton's `_rootJoint`), and the Cast test
   applies the Hips' parent scale so its numbers stay centimetres. If a character ever looks giant,
   read the component extent and the bone world positions before touching anything else:
   `HIAnimTool` reports `scaleDev=`/`maxBoneT=` per clip, and those are where it shows.

Also, on Python: `mesh.set_editor_property("skeleton", ...)` refuses (VisibleAnywhere), but plain
attribute assignment `mesh.skeleton = <USkeleton>` goes through the BlueprintSetter and works.
`physics_asset` is settable either way, and `SkeletalMeshEditorSubsystem.create_physics_asset(mesh,
True, 0)` is the way to get the asset the glTF import may skip. And `EditorAssetLibrary.delete_asset`
followed by a same-session rename/import is unreliable -- the deleted asset stays in the live
registry -- so delete in one editor run and import in the next.
