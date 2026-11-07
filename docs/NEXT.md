# Where to start next

Written at the end of a long session (2026-11-01). Read this, `AGENTS.md`, then
`docs/CHARACTERS.md`, and you are current.

## State

- Branch `main`, clean tree, 52/52 tests (`Automation RunTests HI.`), nothing pushed.
- Cast: **dancer, alina, brute** (`FHICharAnim::IntruderTagForId`). Louise is the player.
  **Martha and James are OUT of the rotation** (Richard, 2026-11-03) -- the two women are the point
  of the pair. They are still imported, still pass `HI.Unreal.Cast`, and putting either back is the
  one string. `EffectiveIntruderTag` falls back to **brute**, not to James, so a missing asset
  cannot quietly resurrect a retired character.
- The game plays; recent work is mostly scene/asset work, not mechanics.

## Where this stands: the Dancer Girl is in the cast (2026-11-01)

Branch `main`, **52/52 green with `TEXT("dancer")` in cast slot 0**, replacing James. His clips,
physics asset and travel row are untouched, so `TEXT("james")` in that one string puts him back.

Getting her there took the retargeter (`tools/retarget-character.sh` now builds the IK Rigs, the IK
Retargeter and its 21-chain mapping itself, and normalises its own output) plus three assumptions
only she broke — a cast-wide facing constant, "retargeted implies normalised", and a test that read
the Hips' local Z as world height. All four are documented with their measurements in
`docs/CHARACTERS.md`.

## The next design question: how many intruders, and which

A FOURTH entry in `Cast[]` is a one-line change and a **no-op for the player**: `Cast[Id % 3]` with
three thieves per round means the fourth name is never spawned. Options, cheapest first:

- leave her in slot 0 as she is now — every round has her, James does not appear
- `--thieves 4` for a round that shows all four (the count is a game-balance knob, not a cast one)
- a pool of four with the round deciding which three appear (`Cast[(Id + Round) % 4]`) — three
  thieves kept, everyone appears in three rounds out of four, but soaks stop being round-independent,
  so the "3x117.9 s bit-identical" measurements in AGENTS.md would need re-taking
- a fourth slot in the shipped default (four intruders per round) — a difficulty change, Richard's

## Ready to go when wanted

- `SourceAssets/models/alina.glb` — "Alina Ip, realistic Asian woman", CC_Base rig, to replace
  Martha; same one-command pipeline, harder rig family
- an asset-level fix for the Dancer Girl's 29.7 deg yaw (add it to the `ZUpRoot` wrapper in
  `tools/rename-glb-bones.py`, re-import, re-retarget) would retire the per-tag facing table — a
  want, not a need
- new skins over the existing three skeletons (Martha, James, Brute) — the cheap route to cast
  variety, no rigging involved

## Smaller open items

- **THE CAST IS IN THE REPO AS OF 2026-11-03**: `characters_dancer/` (18 MB), `characters_alina/`
  (57 MB) and `retarget/` (240 KB) are tracked, so a fresh clone has both women and the rig assets
  its own `retarget-character.sh` run would reuse. STILL UNTRACKED, all of it verified unreferenced
  (grep for the folder name across `Content/HIImported/**/*.uasset` returns 0 other assets):
  `characters_dancer_idle/` (3.1 MB), `characters_dancer_idle_Anim/` (57 MB) and
  `characters_martha_idle_Anim/` (57 MB) — stray copies from earlier import layouts, safe to delete;
  `models/college_girlfriend|everyday_jane|girl_with_clothes_worker_set|miss_galaxy|woman01/`
  (203 MB of art candidates, none of them in the game); and five dead rigs in `HIImported/mixamo/`
  (`IK_Female_Casual*`, `RTG_Female_Casual*`, `Female_Casual_Skeleton1`) from the GUI attempts.
- the earlier GUI attempts left dead rigs at `characters_dancer/idle/SkeletalMeshes/IK_idle`,
  `RTG_idle` and `HIImported/mixamo/IK_Female_Casual*` — safe to delete, nothing references them
- **AN UNREPRODUCED CRASH AT THE MAIN MENU (2026-11-01).** One interactive launch died 16 ms after
  `input claimed`, before `HI: menu -> play` -- i.e. ON the menu, not in a round. There is no
  evidence of *how*: no `Saved/Crashes/` entry, no core, no OOM or kernel message, and the log just
  stops (a clean exit always logs `LogExit: Exiting`). Five controlled runs afterwards, including
  the exact command line with a real window, all survived; the only coincidence worth noting is that
  the user's wireless HID dongle had re-enumerated two minutes before the launch. Two things came
  out of the investigation: **the menu no longer grabs the mouse** (it did, against the comment where
  that screen is set up -- so the one screen that needs a cursor had an invisible one), and
  `tools/play-guard.sh` now writes the game's exit status into its log, so a repeat reads
  `status=139` (segfault) / `137` (killed) / `143` (stopped) instead of "it crashed".
- **ALINA IS IN THE CAST AND PLAYABLE (2026-11-03, fifth pass).** `Cast[]` slot 0 is `alina`, she
  renders textured (camisole, denim, red sneakers, pistol in hand), the suite is **52/52** and
  `HI.Unreal.Cast` reads `hips->head` **56.4 cm** over 102 bones with our names, facing 90. Two
  things had to be fixed after the rig, and neither was her asset:
  1. her `death` was reported as *rising* 104 cm. It falls **81 cm** -- the assertion was reading
     raw Hips Z, and her parent's up is **(0, 1, 0)**, not (0,0,1). Fixed by projecting onto the
     parent's up axis, which is the correction the crouch check already carried; both numbers are
     now logged.
  2. her `walk`/`run`/`crouch_walk` carry **no Hips translation** (the retarget drops the pelvis
     track -- the Dancer Girl's do the same), so `AuthoredCmPerSec` gets her Martha's row, which is
     where her clips were retargeted from.
  **Cast composition, corrected the same day**: Alina was briefly put in slot 0 *in place of* the
  Dancer Girl, which was not what was asked for -- Richard's rotation is **dancer, alina, brute**,
  with Martha and James out. Slot 0 is the Dancer Girl again. The four-way pool at the top of this
  section is still an open design question, not a licence to reshuffle the three. Everything below
  is the history, kept because the diagnosis was wrong four times and the wrong turns are the
  useful part.
- **ALINA: her MESH now imports correctly; her SKELETON still does not (2026-11-01, fourth pass).**
  Blender is installed (5.0.1 from apt; `python3-numpy` was needed for its glTF importer, and the
  importer rejects the merged file with `'NoneType' object has no attribute 'id_type'` but accepts
  her ORIGINAL glb). `tools/blender-reexport.py` imports her, joins her 20 parts into one mesh,
  scales her to a chosen height and grounds her feet, then exports glb -- and that file is right:
  1 mesh, 102 joints, **1.765 m tall, feet at 0, bones named Hips/RightHand/Spine2/LeftToeBase**.
  The engine imports THAT to a **176.6 cm** mesh (Martha is 177) -- so the mesh finally matches the
  cast.
  But the skeleton does not: with her in the cast the suite is 50/52 and `HI.Unreal.Cast` reads
  `hips->head` as **155.0 cm** for a 176.6 cm body, the same bone-vs-mesh split as every earlier
  attempt, plus her clip Hips reads 0 cm (so `death` shows no fall) and her walk/run strides read
  0.0. Two of those are mine to fix -- the `AuthoredCmPerSec` row for alina was removed on revert
  and the physics-asset reference is lost when the mesh is renamed/moved -- but `hips->head` is not:
  the bones are ~2.7x the mesh in every path tried (raw glb, IBM-repaired glb, assimp FBX, Blender
  re-export), which points at her rig's bone space rather than at any importer.
  RECOMMENDATION, and it is judgement not measurement: stop spending sessions on this asset. The
  reliable routes are (a) her MATERIALS on the Dancer Girl's body -- no rig, works today, different
  body shape; (b) a re-rigged character (Mixamo upload, or a model that ships with a standard rig),
  which the pipeline handles in one command; (c) a deliberate investigation of her glb's joint
  space with a fresh session's full attention. Do not start (c) expecting a quick win.
- hedges are leaf cards over a box: fine at distance, bright cards in shadow up close
- the getaway van's upper box panels still render red
- `docs/LOBBY.md` is DEFERRED (Steam/multiplayer parked by Richard)

## Alina, 2026-11-03: the rig was never broken, the INPUT to Blender was

Four passes blamed her rig: the wrapper rotation, the 2x/hip-centred authoring, the zero
inverse-bind matrices, then "her bone space is incomprehensible". The measurement that settles it
takes one command and no engine:

```sh
python3 tools/fix-glb-skin.py SourceAssets/models/alina.glb /tmp/alina_fixed.glb
blender --background --python tools/blender-reexport.py -- /tmp/alina_fixed.glb /tmp/alina_bl.glb 1.75
```

- **Fed the RAW glb, Blender puts `CC_Base_Hip_02` on the floor.** Measured on the raw file at
  import: hip local `(0.000, 0.000, 0.000)` while the head bone is at `158.295` -- so the rig comes
  out with a **155 cm `hips->head` on a 176 cm body**, which is EXACTLY the number
  `HI.Unreal.Cast` logged for four passes. Blender positions skinned bones from the skin's
  inverse-bind matrices, and `alina.glb` ships 21 of them as zero placeholders.
- **Fed the REPAIRED glb, the same command gives hip `z = 0.986 m` (55.8% of height), head
  `1.550`, `hips->head` 56.4 cm, feet at 0, mesh 1.765 m** -- and the engine imports it unchanged:
  `alina: 102 bones, bone[0] is '_rootJoint'`, `lateral=(-1.00, 0.00, 0.00)`, **`hips->head` 56.4**.
  No wrapper, no per-tag scale, no code change: she faces 90 deg like the rest of the cast.
- **The gate is now in the tool**, not in a doc: `tools/blender-reexport.py` refuses to write a rig
  whose hips are outside 40-65% of height and names `fix-glb-skin.py` in the error. Verified both
  ways -- raw file: "hips at -0.1% of height -- refusing"; repaired file: writes normally.

**Working pipeline, in order** (the `idle.glb` route, not FBX):

```sh
python3 tools/fix-glb-skin.py SourceAssets/models/alina.glb /tmp/a_fixed.glb
blender --background --python tools/blender-reexport.py -- /tmp/a_fixed.glb /tmp/a_bl.glb 1.75
GLB_WRAPPER=0 GLB_SCALE=1 python3 tools/rename-glb-bones.py /tmp/a_bl.glb /tmp/a_named.glb
python3 tools/merge-glb-meshes.py /tmp/a_named.glb SourceAssets/characters/alina/idle.glb
tools/import-assets.sh
tools/retarget-character.sh alina martha woman01
```

Three traps on that route, all hit today:

1. **A stale `SourceAssets/models/alina.glb` poisons the retarget.** `import-assets.sh` imports
   everything, so the old 2x model re-created `models/alina/SkeletalMeshes/alina_Skeleton`, and
   `retarget-character.sh` bound her ten clips to THAT instead of to her character rig. The binding
   check is what named it -- read the `-> OK, all 10 clips bound to ...` line, do not assume.
2. **A glTF character imports NESTED** (`characters_alina/idle/SkeletalMeshes/idle`), and
   `rename-glb-bones`/`retarget` cannot rename it to the flat `characters_alina/idle` while the
   folder of that name still holds the textures. Flatten first (rename mesh to `idle_tmp`, rename
   the `Textures`/`Materials` folders up one level, delete the empty folder, then rename the mesh),
   or the game loads nothing and `Cast` reports a bone list from a stale asset.
3. **`import-assets.sh` OVERWRITES retargeted clips with the raw FBX in that character's folder.**
   Running it after `retarget-character.sh` silently reverted the Dancer Girl to clips whose Hips
   sit at 0 cm, and `Cast` went red. Re-run `retarget-character.sh dancer martha woman01` to restore
   her; do not run `import-assets.sh` casually on a character that has been retargeted.

**Both of the clip faults turned out to be bookkeeping, not motion** (recorded here because the
first reading of each was wrong):

- `alina/death` was reported as **rising** 104 cm. It **falls 81 cm**. The clip was always right;
  the assertion read raw Hips `Z`, and her Hips' parent up is `(0, 1, 0)`, not `(0,0,1)` -- the
  same trap the crouch check documented on 2026-11-01 and the death check had not. Her
  `hips->head` being `(0.0, -1.7, 56.4)` says nothing about the PARENT's frame, which is the
  distinction that cost the reading.
- her `walk`/`run`/`crouch_walk` carry **0.0 cm of Hips translation**, and so do the Dancer Girl's:
  the batch retarget drops the pelvis track, and the values in `AuthoredCmPerSec` were always
  measured from the SOURCE clips before pinning. So the row for a retargeted character is the row
  it was retargeted from (Martha's). That is provenance, not a guess -- the same reasoning the
  dancer's row already carried.

**Materials.** She renders textured only if her 19 materials and 37 textures survive the import.
They do NOT survive a re-import of the mesh into an existing flat package (Interchange replaces the
mesh alone and reports one object), and the nested glTF layout has to be flattened AROUND them --
move `Textures/` and `Materials/` up first, then the mesh. Importing her while
`SourceAssets/characters/dancer` exists re-breaks the dancer (see trap 3 above); hold that folder
out of the tree for the run.

## Working notes that keep costing time

- `FParse::Value` treats a COMMA as a separator -- use `+` for lists
- metres vs UU has cost three separate bugs; check units at every placement
- a fix must be photographed by a camera that FACES it, and the object in frame must be the object
  you changed (the red trees were `tree2`, a crimson pine by design, for four rounds)

- **A `TObjectPtr` WITHOUT `UPROPERTY` IS A RAW POINTER, and it cost three crashed runs on
  2026-11-04.** `AHIMark` (the pooled blood decal) declared `PlaneMesh`, `CylinderMesh` and
  `DecalMat` as `TObjectPtr` with no `UPROPERTY`, so the garbage collector could not see them and
  was free to collect the material instance while the actor lived. The game then stamped through a
  dangling pointer: a SIGSEGV three rounds into a run, in `AHIMark::Stamp`. The smart-pointer
  spelling buys nothing for lifetime; only `UPROPERTY` does. Four house ISM components had the same
  omission and were fixed with it. `HI.Unreal.Cast` cannot see this class of bug -- it needs a run
  long enough for a GC pass, which is what `-HISoak=200 -HIRounds=6 -HINoAuto` gives (passive
  rounds lose, so it exercises the restart path too).

## The Dancer Girl's chest edit: SHIPPED (2026-11-05)

Richard asked for her bust roughly doubled. Two attempts were rolled back on 2026-11-04 and the
third shipped. **Both of the blockers recorded in the previous version of this section were
symptoms of the same thing: the import.** The retarget ran fine once the import was right -- but it
then shipped a 79 m character, because it does not carry the import's scale (see "The giant" below).

**Result, measured:** suite **52/52**, `HI.Unreal.Cast` green.
`dancer: Hips mean 98 cm standing, 48 cm crouching (parent up)`; `facing 119.7 deg;
hips->head=(2.2, -3.9, 39.1) |39.3|` — unchanged from before the edit; the mesh carries a physics
asset; all ten clips are bound to `characters_dancer/idle_Skeleton`. The shipped mesh really is the
edited geometry: exported and compared vertex-cloud-to-vertex-cloud, the 203 moved vertices sit
**0.000 cm** from it and up to **6.25 cm** from her original (source glb: apex 7.43 -> 12.40 cm,
largest moved vertex 7.41 cm, 9509 verts before the engine's split).

**What was actually wrong, in the order it bit:**

1. **The bone-renaming wrapper was written for the OLD glTF importer.** This engine's Interchange
   importer converts glTF's Y-up to Z-up itself AND scales glTF metres to centimetres, so
   `GLB_WRAPPER=1 GLB_SCALE=1` stacked a second conversion on top: the mesh came in **100x too big
   and lying on its back** (bounds `(6278, 8059, 3706)` against her real `(62.79, 37.06, 80.59)`).
   A retarget pose built from that mesh is flat, which is exactly what "the standing idle's Hips
   mean -0 cm" was. The variant that imports correctly here is
   **`GLB_WRAPPER=0 GLB_SCALE=0.01`** (identity wrapper, 1/100 scale).
2. **The glTF importer REUSES an existing skeleton asset of the same name at the destination.** The
   tracked nested `characters_dancer/idle/SkeletalMeshes/idle_Skeleton` was a pre-fix import whose
   bones are named `mixamorig11_Hips`; importing over it rebuilt the mesh with those names, and every
   bone lookup after that failed — the `Unable to retrieve bone index for track: Hips` that cost
   attempt one. Empty the destination folder first; then the import creates a clean pair.
3. **The mesh's bone space cannot be swapped under her old clips.** Her old `_rootJoint` carried
   -90 deg X (so the Hips' parent space had UP on -Y); the new import leaves `_rootJoint` at
   identity (UP on +Z) for the SAME rendered geometry, because the axis conversion is now baked into
   the vertex data instead of the root bone. Pointing the new mesh back at her old skeleton read
   `Hips mean -7 cm`. Mesh and skeleton must come from ONE import, and the ten clips are retargeted
   onto that pair — `tools/retarget-character.sh dancer martha woman01` then produced **98 cm**, with
   21/21 chains mapped and `aim`'s known single-key `MISSED` report unchanged.
4. **The import skipped the physics asset and replaced her material.** Importing `idle.glb` into her
   own folder reused `Scene_-_Root` by name (good: her approved look, and the edit is vertex-only so
   the UVs still map) but created no physics asset, which failed `Cast` and `DeathFall` with
   "dancer has no physics asset". `SkeletalMeshEditorSubsystem.create_physics_asset(mesh, True, 0)`
   builds and assigns one.
5. **Cleanup that came with it:** the nested pre-fix folder is gone (10 tracked assets, including the
   dead `IK_idle`/`RTG_idle` rigs). Her folder is now flat: `idle`, `idle_Skeleton`,
   `idle_PhysicsAsset`, the ten clips, and the material + two textures.

**Reproducing it, exactly** (the order matters; each step was measured):

```sh
GLB_WRAPPER=0 GLB_SCALE=0.01 python3 tools/rename-glb-bones.py \
    SourceAssets/models/woman01_edited.glb /tmp/idle.glb      # FILE must be named idle.glb
# 1. delete /Game/HIImported/characters_dancer/{idle,idle_Skeleton,idle_PhysicsAsset}
#    and the whole characters_dancer/idle/ folder; exit the editor.
# 2. import /tmp/idle.glb into /Game/HIImported/characters_dancer (AssetImportTask,
#    automated, replace_existing, save). It lands FLAT: idle + idle_Skeleton.
#    The mesh is her height (bounds z=80.59) and has bones Hips/Spine/... with no prefix.
python3 - <<'EOF'  # in the editor's Python: physics asset + save
import unreal
m = unreal.EditorAssetLibrary.load_asset("/Game/HIImported/characters_dancer/idle")
unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem).create_physics_asset(m, True, 0)
unreal.EditorAssetLibrary.save_loaded_asset(m)
EOF
tools/retarget-character.sh dancer martha woman01   # rebuilds the ten clips onto the new pair
~/tools/ue-run-tests.sh /data/projects/HIUnreal/HIUnreal.uproject HI.   # 52/52
```

### The giant, same day: retargeted clips and the import scale (2026-11-05)

The chest shipped and the Dancer came back **~100x too large** -- a 79 m figure, "three times the size
of the house" -- while `HI.Unreal.Cast` stayed green and her pelvis read a perfect 98 cm. She is an
INTRUDER (the player is louise), so this was visible from the yard on the first run after the ship.

**What it was, measured.** The Interchange glTF import keeps the wrapper's scale in the RIG rather
than baking it into the geometry: `_rootJoint` carries **scale 0.01** and every other bone's local
translation is **100x** (Hips `(0.2, 603.2, 9808.1) cm`, a finger joint 14183 cm). The two cancel, so
the component-space pose and the bind pose are exactly right -- which is why the mesh measured
161 cm, the facing 119.7 deg and `hips->head` 39.1 cm all along. IK Retargeting copies those offsets
into the clips and writes **scale 1.0**, dropping the cancellation: the bones then sit at 0.95 m
(hips) but 14 m (Spine1) and 40 m (Head), and the component's bounds were 36 m. Nothing in the suite
read scale, and the pelvis -- the one track the retarget authors in CENTIMETRES rather than copying --
was the single number that looked right.

**How it was found** -- recorded because a screenshot cannot tell "giant" from "close to the camera":
`-HIDebugBodies` (a temporary switch, since removed) walked every skeletal body in the world and
printed its component extent plus the world positions of Hips/Spine1/Head/LeftFoot. It named
`HIIntruder_0` (the Dancer) with extent `(6264, 5263, 7344)` against alina's `(40, 25, 87)`, and the
bone dump showed the pelvis sane at 0.948 m with the chain exploding above it. `HIAnimTool`'s report
now also prints `scaleDev=` (the largest `|scale-1|` across a clip's keys) and `maxBoneT=` (the
largest non-pelvis translation) -- those two numbers are the early warning, and both were 0/fine
before the retarget and 14183 cm after it.

**The fix, in the pipeline.** `UHIAnimTool::BakeClipRootScale(clip, mesh, factor)` converts the ONE
track the retarget writes in centimetres (Hips) into the rig's space (x 1/factor), sets the root
bone's scale keys to `factor` and every other bone's to 1, and saves. `tools/retarget-character.py`
reads `factor` from the target skeleton's `_rootJoint` and runs it over all ten clips, so a re-run of
`retarget-character.sh dancer martha woman01` reproduces this from scratch (verified: "baked the rig
scale into 10/10 clip(s)"). For an FBX rig the factor is 1 and the step is a no-op.

**And the test now measures in centimetres.** The Hips track lives in its parent's space, so with a
scaled rig its numbers are in that scale; `HI.Unreal.Cast` applies the Hips' parent accumulated scale
once, at the read (`HICastHipsParentScale`), instead of special-casing each assertion. Measured after:
`Hips mean 98 cm standing, 48 cm crouching`, death drops 81 cm, and the suite is **52/52**.

### The fourth pass: the bulge was aimed at her NECK (2026-11-05)

The first in-game evaluation after the pipeline worked was Richard's: *"she is the right height, but
the boobs seem like the same size as when we started."* Correct, and measurable.

**What the tool actually moved.** Its Gaussian was centred on `(Spine1 + Neck) / 2` -- **76% of body
height**, with sigma 9 cm -- so the blob covered the collar, the neckline and the inner arms while
the bust (71-75%) sat on its falling edge. A height-by-height profile of the surface said it plainly:
the midline bust moved **+0.2..+0.6 cm**, the largest single displacement (**7.4 cm**) sat at 78% of
height, and of 203 moved vertices **143 were dominated by RightArm/RightForeArm**; the ones whose
dominant weight is a spine bone averaged **0.5 cm**. The tool's own "apex 7.43 -> 12.40 cm" line had
been measuring the top of the sternum -- a real number about the wrong landmark.

**Two independent bugs, both in the targeting:**

1. **The vertices were never in the bones' space.** This export keeps a translation on the mesh node
   (`Object_7`, (-18.5, 0.99, -1.85)), so raw POSITION values are offset from every bone by ~18 cm.
   Projected against the bones' axes without that transform, the "midline" band fell on the SIDE of
   her body: the 62-74% slices held no vertices at all, and the front-most point in the 76-78%
   slices won the apex contest.
2. **The region was chosen by a bone midpoint and a lateral radius**, not by what the surface is
   skinned to, which is how the arms and the neckline got pulled forward.

**The fix** (`tools/edit-glb-chest.py`): work in skeleton space (apply the mesh node's matrix), find
the bust line as the height of **maximum forward extent on the midline within 64-78% of height**
(measured: 118.2 cm, 73%, apex 11.34 cm), and gate the eligible vertices on **dominant skin weight in
Spine/Spine1/Spine2** -- which excludes the arms, shoulders and neck by construction. Narrower
vertical sigma (4.5 cm), and the tool now **refuses to write** if any displacement above 78% of
height exceeds 1.5 cm or any vertex beyond 14 cm laterally moves at all. It prints the forward
profile before and after, so the failure that shipped cannot pass unnoticed again.

**Result, measured on the glb:** bust apex **11.34 -> 17.54 cm (+6.20 cm, +55%)**; profile deltas
+0.35 at 66%, +3.44 at 70%, **+6.21 at 73%**, +3.83 at 75%, +0.29 at 80%, **0.00 above 82%**; 393
vertices moved, symmetric (centroid 0.03 cm off the midline, 182 left / 161 right, lateral -11.1 ..
+10.1 cm); **zero** movement on the arms. Rendered A/B (same camera, same pose, original vs edited,
front and profile): `shots/chest-ab.png` -- the difference is plain in both views. In-game front view
of her: `shots/dancer-ingame-front.png`.

Through the pipeline (which needed no changes): import `GLB_WRAPPER=0 GLB_SCALE=0.01`,
`create_physics_asset`, `retarget-character.sh dancer martha woman01` (which bakes the rig scale),
**suite 52/52**, `Hips mean 98 cm standing, 48 cm crouching`.

Source assets refreshed to match: `SourceAssets/models/woman01_edited.glb` (raw edit) and
`woman01_edited_renamed.glb` (renamed, `GLB_WRAPPER=0 GLB_SCALE=0.01`).

**Two things worth knowing before touching this again:**

- `-HIModels=` art previews render THESE glb imports about 100x too large: the preview code derives
  its scale from `Bounds.BoxExtent.Z`, and for an Interchange glTF import that bound is not the
  rendered size. Candidate art from `import-assets.py` does not have this problem, so it is specific
  to a character glb whose scale lives on the wrapper node rather than in the vertices. A visual A/B
  of the chest was attempted this way, failed, and the numeric check above replaced it — the game's
  camera only ever looks at her back, so there is no in-game angle that shows the chest anyway.
- The screenshots that go with this: `shots/dancerdrive_01.png` (her on the driveway, in the game),
  `shots/chestab5_02.png`. `shots/` is gitignored.

## The tranquilizer, and what happens to the men it takes (2026-11-05)

Third weapon, key **3**: a dart gun. One in the chamber, 24 darts in reserve, **no damage** -- a dart
does not wound, it puts a man down. The round counts a captured intruder as **neutralised** without him
being dead, so the tranquillizer is a non-lethal route to exactly the same win (the HUD's "intruders
down" counter includes him), and the AI skips him like a corpse.

**The sequence, as specified:** he falls where the dart lands (the same ragdoll a death uses), vanishes
2.2 s later, and is found in the basement hanging from the ceiling by both wrists.

Implementation notes that matter later:

* **`FHIThief::bCaptured`** is a new state: alive, out of the fight, still in `Intruders`. Every place
  that used to read `bAlive` alone for "still a threat" now reads both -- `AnyIntruderStanding` and the
  AI step loop -- and `DamageIntruder`'s drop-the-loot logic is mirrored in `Tranquilise`.
* **The capture branch runs BEFORE the actor is positioned from the sim.** The sim keeps the thief at
  the spot where the dart landed, so applying that position after the hang teleported the hanging man
  back up to the ground floor every frame -- the basement looked empty and the body was upstairs.
* **The hanging is posed, not simulated.** The first design was a real suspension: two
  `UPhysicsConstraintComponent`s pinning the hand BODIES to ceiling points, which is by far the best
  answer because the body then hangs off its own arms. It works on Alina and fails on the Dancer, whose
  rig carries the glTF import's 100x encoding: `RecreatePhysicsState` brings her bodies back ~90 m from
  the actor (measured), so the constraints towed her across the map. She now hangs *posed*: physics
  off, and the reference pose -- arms straight out, which is a man shackled by both wrists -- with
  instanced-cube **chains** (alternating-roll links) from the ceiling to each wrist and a **cuff** box
  at each wrist, redrawn every frame. He is lifted 0.23 m so his feet dangle, and the lean is 7 deg so
  he does not read as a mannequin.
* **This actor does not tick**, so a clip set on it is never evaluated and the bones stay in the world
  space the ragdoll left them: the pose is driven by hand in `UpdateHangVisuals` (`TickPose` +
  `TickAnimation`), which is also why the chain positions are taken from the drawn pose's wrist offsets
  rather than from `GetSocketLocation` (those disagreed by a frame).
* Three **stations** in the basement, along the clear floor at x +-0.55 and y -11.4 / -12.55.
* `-HITranqDemo=N` darts the first N intruders a second into the round. It exists because a manual
  capture drives no input, so without it there is no way to photograph the basement.

**RESOLVED (2026-11-05), and the fix was structural.** Richard: *"I am happy with the 3 intruders and
how they look, so if you can use the dedicated captive actor, without losing the characters appearance,
do it."* So there is now **`AHICaptive`**: its own actor, an ordinary tick, and the same body —
`SetSkeletalMeshAsset` + `FHICharAnim::SetupCharacter`, word for word what the intruder does, so the
mesh, materials, face and the brute's hidden axe are identical. The fallen intruder hands off to it on
the third second, then hides itself for good.

Why that worked when nothing else did: the intruder is an actor the game mode *steps* and never ticks,
so its skeletal mesh never evaluates an animation into the render state. `TickPose`/`TickAnimation`
by hand updated the sockets and nothing else. A captive has nothing to do but hang, so it gets the
tick, and the clip renders.

**The pose itself took three wrong answers, all of them now written down:**
1. *Arm bones' local rotation is not their direction.* `CompRot[Arm].GetAxisX()` reports a lateral axis
   on a Mixamo arm, so "swing X to up" rolls the arm instead of lifting it. The direction that cannot
   lie is between the bone and its child (`LeftArm` -> `LeftForeArm`), and with that the swing
   (`FindBetweenNormals(dir, up)`) raises both arms first time.
2. Two intermediate attempts were signed rotations about the forward axis -- one raised Alina's left
   arm and left the other three arms out, which is what made "the pose is wrong" look true when the
   formula was not the problem.
3. The chain anchors were stored once, in the captive's frame, which drew them converging on a point in
   the middle of the room; they are recomputed from each wrist every frame now.

**Taller basement, bigger cell, and a fall for the Dancer (2026-11-05).** *"Make the basement higher add one
meter to the height. When the Dancer gets tranquilized, she doesn't fall down and disappears in a couple of
seconds... I want the Dancer to get 2 seconds or whatever it is for the other intruders. Also, make the
cell a meter wider and 1/2 meter deeper."*

* **The basement is 3.29 m** (was 2.29) and the slab is at -3.44. The deck did NOT move --
  `BasementZ + BasementCeiling` is still -0.39 -- so anything about the basement ceiling reads
  `BasementCeiling` now, not a literal. The stair band needed 18 risers instead of 14 (0.19 m each), the
  lally columns reach the new deck, the jail bars reach the new ceiling, and the prisoners hang 0.80 m
  off the floor with a visible run of chain (no more tiptoe). `FloorHeight(-1)` and the storey-height
  bound in `HI.Furniture` were updated with it.
* **The Dancer's fall**: her ragdoll has never worked -- `SetAllBodiesSimulatePhysics` throws the body
  **18 m** (Alina: 20 m, the brute: fine) because of the glTF 100x rig encoding, so she vanished the
  instant the dart landed. The capture branch now measures the body against where he stood and, if the
  ragdoll has thrown it further than 3 m, abandons physics and falls by hand: 0.9 s ease onto his side,
  then the same 2.2 s disappearance as everyone else. Both women take that path today; the log says so
  in as many words ("ragdoll threw the body 18 m -- falling by hand instead").
* **The cell** is x -5.66..-2.86 (2.80 m wide, +1.04) and y -12.82..-16.50 (3.68 m deep, +0.52). Growing
  it east put the **lally columns inside the cell** (they are at x -3.125..-2.875), which is the only way
  to widen without moving a column; the walls route around them, and the north wall moved to -12.82
  because the `baseOpen`->`baseDoor` walk line's envelope fouled the old corner.

**The stick, and squirming (2026-11-05).** *"When the homeowner enters the cage, the weapons will change. 1)
will be the pistol 2) will be the shotgun and 3) will be a stick. If the prisoner gets hit with a stick, they
will squirm. Try your best at squirm. I think we will have some random movements with each stick hit. One of
them could be running in place for 2 seconds. If we have 3 types of reactions, you randomly mix them up."*

**Slot 3 is zone-dependent, and the ZONE does the swapping, not the key.** `FHISim::JailCell()` is the cell's
interior plan (x -5.66..-2.86, y -12.82..-16.50, floor -1) and `FHIRound::Step` swaps `tranq <-> stick` on the
boundary: walking in turns the dart gun into a stick, walking out turns it back. That means there is no way to
be standing in front of a prisoner holding the wrong one. The 3 key reads the same predicate so the HUD and the
swap agree.

`FHIWeapon` grew **`bMelee`**. A stick spends no magazine (the fire gate asks `bMelee || Mag > 0`) and makes no
gunshot noise (the "they hear it" alert loop is skipped), but it still raises the same `playerShot` event so the
actor layer traces it -- the shortest path through code that already works. In the game mode a stick shot has
no flash, no tracer, no recoil, and a **1.8 m reach** instead of 60.

**Three reactions, picked per hit and never the same twice in a row** (`AHICaptive::Squirm`):
1. **RUNNING IN PLACE, 2.0 s** -- the character's own WALK clip at 1.4x on a body that is not going anywhere,
   plus a small pitch into each stride. The legs are the whole point, so this is the one reaction that changes
   the clip.
2. **THRASHING, 1.4 s** -- the hang pose held; the actor shakes fast (10 deg yaw at 6 Hz, 5 roll, 3 pitch) with
   a 3 cm bounce. The chains are drawn from the wrists every frame, so they follow him.
3. **WRITHING, 1.6 s** -- the same shake, slower and bigger (7/5/4 deg at 3 Hz, 4 cm bounce).

Kinds 2 and 3 keep the HANG clip on purpose: a full-body clip drops the arms to the character's sides, and a man
cuffed to the ceiling with his arms suddenly down looks released rather than struggling. That was measured --
reaction 2 first played the idle clip and the brute stood there perfectly calm with his hands at his sides.

`-HISquirmDemo=seconds` (capture-only, like `-HITranqDemo`) makes each captive do a different reaction at that
moment, which is how `shots/squirm3_*.png` shows all three at once. Between frames 0.15 s and 0.9 s into the
reactions, **194,270 pixels** change inside the cell.

Suite 52/52 with new `HI.Sim.Round` assertions: the stick is melee with zero damage, an empty-handed swing still
fires and spends nothing, and stepping in and out of the cell turns slot 3 into the stick and back.

**The brute had no cuffs, and the reason was the cuff's shape (2026-11-05).** *"The brute doesn't have cuffs
around his wrists."* The cuff was a box of **8.5 x 4.7 x 4.7 cm** parked on the HAND bone. 4.7 cm across is
narrower than a forearm, so on the brute it was entirely inside his arm (and on the two women it was only
just proud of the skin). It was not missing -- it was swallowed.

It is now a **band**: square in section, 3.5 cm along the arm, sized from the man's own forearm
(`0.55 x forearm`, floor 12 cm), and placed at the **wrist crease** -- 22 % of the way from the hand to the
elbow rather than at the palm. Measured in the cellar:

| | forearm | cuff across | wrist offset |
|---|---|---|---|
| dancer | 23.2 cm | 12.7 cm | 5.1 cm |
| Alina | 22.7 cm | 12.5 cm | 5.0 cm |
| brute | 29.0 cm | **15.9 cm** | 6.4 cm |

A wrist is 6-8 cm across, so the steel is now visibly outside the skin on all three.

This one is verified **by measurement, not by a frame**: the third-person shot camera trails the player by
1.27 m, the corridor beside the cell is only 1.58 m wide, and the stairwell (a metre deeper since the
basement grew) swallows the camera at every angle that would look into the cell -- four attempts produced
clips of the stair wall, a belt buckle and a bare chest. A `-HIShotCam=x,y,z` flag to place the camera
independently of the player would fix that class of problem for good, and is worth doing before the next art
check.

**Lowered, turned to the door, and the arms finally overhead (2026-11-05).** *"Lower the Brute by enough so
that he can hang by his arms... about 2 feet... Also turn the prisoners 90 degrees to the left so that
they are facing the entrance to the cell."*

* The brute hangs at **0.19 m** instead of 0.80 (2 ft lower). He is 2.2 m of man in a 3.29 m cellar;
  at the women's height his head and shoulders were jammed against the deck and his arms had nowhere to
  go. The women keep 0.80.
* The prisoners face the doorway in the east face of the cell: **`FRotator(7, 270, 3)`**. The first
  attempt used 90 and Richard, looking at the capture, said it exactly right -- "you turned the
  prisoners the wrong way. Now if you turn them 180 degrees, I think they will be OK." At 90 they had
  their shoulder blades to the door. **The lesson: 90 was reasoned from the cell's axes instead of read
  off a frame, and these meshes' bind facing is not the actor's forward** -- the same trap as the
  walking yaw and the hang pose.
* **The brute's arms are overhead at last**, and the answer was the thing flagged as unexplained last
  time: swinging only the UPPER ARM was enough for the women and never for him, because his bind has the
  arms forward and his clavicle keeps them there. The generator now swings **every link** -- clavicle,
  upper arm, forearm -- to point up, the same construction the legs use downwards. A rig whose bind
  already has the arms up is unchanged by it (the two women look the same), and one whose bind has them
  forward ends up vertical (the brute does).

**The brute's legs, fixed by not trusting a bind pose (2026-11-05).** His hang was written as "4 degrees of
thigh, 10 of knee", on the assumption that every rig's reference pose stands with its legs down. **The
brute's does not** -- his bind has the knees folded up, so the clip faithfully hung him with his knees
ABOVE his hips. Measured in the cellar before the fix: hips z -143, knee -103, foot -58 (i.e. legs
pointing up); after: hips -143, knee -199, foot -247.

The fix is the same construction the arms already used: swing the bone's DIRECTION to an absolute target
instead of adding a fixed angle. Thigh: swing (thigh -> knee) to straight down, relative to the rig's own
up. Knee: swing (knee -> ankle) to straight down plus 0.10 of the body's forward. Any bind pose now
produces the same hanging legs, and the women are unchanged.

**Still wrong, and known:** the brute's arms are not overhead -- they hang forward, in his bind's
"zombie" position. The swing IS being computed and written (measured: both arms ~80 degrees, symmetric),
and it does not show on screen, which is not explained. The next thing to try is swinging the CLAVICLE
(`LeftShoulder`/`RightShoulder`) rather than the arm bone, so the whole chain follows.

**Old note (superseded by the above):** the brute's hang pose is horizontal -- his own rig's frame disagrees with the
hips->head/foot->toe derivation somewhere. Richard deferred it ("we will adjust the Brute's chain next
time"), so it is untouched here.

**The cell, and legs that hang (2026-11-05).** *"The right side of the basement has the furnace, and on the
left side, put a jail with bars in it... big enough to hold 3 prisoners. The door can just be an opening,
not a real door yet. Then hang the intruders in there... Also, have gravity pull their legs down."*

The jail is **five thin furniture rows** -- north, south, west, and two east strips with a 1.00 m doorway
between them -- so the bars ARE the collision and the doorway is simply the part with no row in it. The
`jail` branch of `BuildFurniture` draws vertical bars (0.018 m steel, 0.20 m apart) plus a floor and a
ceiling rail. As built: x -5.66..-3.90, y -12.44..-15.60.

Three things the house's own `SelfCheck` caught, each a real error rather than a formality:
1. The first cell started at y -11.48 and `baseOpen` (-3.80,-11.80) reached into the east wall with its
   0.42 m body disc -- i.e. a man unable to stand at his own nav node.
2. `Z` is the NEAR edge and `D` runs +Y, so two of the walls were laid out backwards and ran into the
   house.
3. `ReallyOverlaps` treats TOUCHING strips as overlapping, so the cell's own corners were rejected. Every
   junction now has 2 cm of clearance.

The stations moved inside the cell (x -4.80, y -13.35 / -14.30 / -15.25), and the legs stopped being
folded: they were bent 38/68 degrees to keep the feet off a 2.05 m ceiling, which read as a man squatting
in mid-air. Now 4 degrees of thigh and 10 of knee -- a chained man on tiptoe -- with the body hung 0.10 m
up. Bars at 0.13 m apart read as a solid grid (the near and far walls line up behind each other); 0.20 m
reads as a barred wall.

Furniture row count guards updated: 53 rows (48 + the 5 jail strips), 13 basement, and the same in
`HI.Furniture`. Suite 52/52.

**Three more from play, same day.** *"Fix the brute... there is a dead brute in the basement when the game
starts... can we put some life into her? A little bit of wiggling to know she is alive."*

* **The brute hung at 40 degrees** because the pose swung his arms to WORLD up, and his FBX import
  carries its own rest orientation. Up is now measured from the rig itself -- hips to head -- and
  forward from his own foot to toe, with the lateral axis from the cross product. Every rig is treated
  the same way and none needs a special case; he hangs upright.
* **The dead brute on a new round** was a captive from the previous round: those actors belong to the
  round that took them and nothing destroyed them. `AHIGameMode` now clears every `AHICaptive` when it
  builds a round, and says how many it removed.
* **A little life**: the hang clip is no longer one key. It is 48 keys at 30 fps (1.6 s, one unhurried
  breath) with the arms drifting, the legs kicking gently out of phase (left lags right by 1.4 rad),
  the head turning, and the spine twisting 1.5 degrees. Played LOOPED (`PlayAnimation(..., true)`),
  because one breath has to repeat. Measured between two captures 0.5 s apart: 24,381 changed pixels
  in the body region -- she is visibly awake, and it is nowhere near a struggle.

**Two more from play, same day.** *"I tranquilized all 3, but the message said that they got away with the
loot and the game ended... One of the problems is if they get tranquilized while carrying loot, it is
counted that they got away with the loot."*

The cause was a missing guard, not a bookkeeping slip: **`StepThief` only refused the dead**
(`if (!Thief.bAlive) return false`), so a man darted on the drop point went on walking to the van and
DELIVERING from the basement. `HI.Sim.Round` reproduces it exactly -- the new test darts a carrier
standing on the drop point and asserts his loot stays in the house -- and the fix is the second line of
that function: `if (Thief.bCaptured) return false`. (The earlier "AI skips captured" edit had landed on
a different loop, the perception one, which is why captures looked inert in playtests that never had a
carrier.)

And the loss itself is now deferred: **`allStolen` no longer ends the round while anybody is alive.**
It sets `bAllLootDelivered` and the round runs on, which is what lets the player walk down and look at
the men they took; when the last of them is killed the round resolves to **Lost** if the loot was gone
(a win with an empty house is not a win) and **Won** if it was not. An intruder who ESCAPES and the
player going down still end it on the spot -- neither can be caught in the basement.

`-HITranqDemo=N@seconds` darts the first N that far into the round, which is how the carrier case is
photographed: `shots/tranqrules_02.png` shows **LOOT STOLEN 0/3**, **INTRUDERS DOWN 0/3**,
**CHAINED 3** and the round still running with all three hanging.

**The round no longer ends on a capture.** Richard: *"once you tranquilize all 3 intruders, the game
ends. The game shouldn't end if there is an intruder in the basement chained up. The game will end when
all chained up intruders are killed."* `AnyIntruderStanding` reads `bAlive` again -- a chained man is
alive, so the round goes on -- and capturing is counted separately as `ThievesCaptured`, shown on the
HUD as **CHAINED n**. `DamageIntruder` still works on a captive: the same sim path, and a lethal hit
cuts the chains and drops the body to the slab (`AHICaptive::GoLimp`). `HI.Sim.Round` asserts the whole
sequence: capture one -> not standing -> capture all three -> **still playing** -> kill them -> won.

**Later the same day (2026-11-05), from play:** *"the restraint, they should be hanging by their wrists
meaning that their wrists are above their heads"* and *"when I shoot the dart gun, nothing is fired."*

The firing bug is fixed: `CreatePlayer` banked pistol and shotgun magazines in `mags` but not the
tranquilizer's, and `SwitchWeapon` takes `Mag` from `mags[key]` -- so the dart came up 0 and the gun
could not fire at all. One dart is banked now, and `HI.Sim.Round` asserts it.

**The wrists-over-head pose is authored and NOT working.** `HIAnimTool::MakeHangClip` writes a one-key
clip per character from that mesh's own reference pose (both arms swung to overhead, thighs forward,
knees bent, elbows softened), the clip is a duplicate of the character's own `idle_Anim` so it is bound
to the right skeleton, and it is loaded and played by the hang. Verified at runtime: the clip's
`LeftArm` key IS the raised rotation (`q=(0.022,-0.019,-0.865,0.502)`) and the hand sockets report the
arm overhead. **The rendered mesh stays in the reference pose.** Ruled out, one at a time: the clip's
contents, the load path (no fallback warning), the actor's tick (enabled and its tick functions
registered after spawn), driving the pose by hand (`TickPose` + `TickAnimation` + `RefreshBoneTransforms`),
`SetAnimationMode(AnimationSingleNode)` ordering, and the component's own rotation.

The suspicion worth following: this actor is stepped by the game mode and never ticks, and the player's
pawn -- which renders its clips correctly -- does tick. The clean next step is a **dedicated captive
actor** with a normal tick and its own skeletal mesh (built like the pawn's), instead of reusing the
intruder body and hand-feeding its animation. That is a small class, and it removes the whole class of
"the data is right, the renderer disagrees" from this feature.

**Known compromise:** a *limp* hang with the arms overhead would look better, and cannot be done by
posing the reference skeleton -- it wants an authored `hang` AnimSequence (one key: ref pose with both
arms rotated up), which the asset pipeline can make. That is the next step if the pose matters; the
restraint reads correctly as it is (`shots/hung9_01.png`).

Suite **52/52** with the new `HI.Sim.Round` assertions: a dart captures, he stays alive, he counts as
down, darting twice does nothing, capturing every intruder wins the round, and a captured man drops what
he was carrying.

## Three from play: the carried load, the near-side street, the van (2026-11-05)

**1. Loot slid along at the ankles instead of riding in a hand.** The carried item's position comes
from the carrier's HAND BONE -- the original's comment is the specification: *"the hand bone moves with
the animation, so taking the position from it is what makes the object look carried rather than glued
to his middle."* The port asked for `hand_r`, which is the engine MANNEQUIN's bone. The cast is Mixamo:
`RightHand`. So the lookup returned zero for every thief, `bHaveHand` stayed false, and every load took
the chest fallback -- a fixed offset from the carrier's centre at 0.88 m, which from the player's eye
line reads as something sliding along the floor between his legs.

`FHICharAnim::HandBoneFor(Body)` already knew both rigs (it is what the pistol attaches with), so the
fix is to ask the mesh which hand it has instead of naming one, and to LOG when a carrier has no hand
bone at all -- the silent fallback is what hid this. Measured during a 45 s soak, the hand is found and
the load rides at the hand: `hand=RightHand at (…, 160-163 cm)` for the brute mid-walk.

**2. The near side of the street sat in a field.** Our house fronts at **y -7.75** and the far-side
neighbours are 5 m behind their pavement, so the street looked right from the road -- but the four
plots on OUR side were centred at **y -30**, i.e. their fronts 20 m further back than ours, and every
one of them was rotated the same way as the far-side plots, which on our side means **facing away from
the street**. `NiceHouse` now takes a facing, and our side uses **y -11.75** (fronts exactly on our own
elevation) and **facing 0**, with the plot's hedge U computed from the facing rather than from the sign
of `CY` -- otherwise moving the houses forward inverts the U (its back run would fall south of its
front run), which is what the old `CY > 0` branch would have done.

**3. The getaway van is white.** It was tinted `(0.19, 0.25, 0.20)` -- khaki. Unlike the street cars it
cannot simply keep its imported materials: several of its slots import with no base colour, which is
why it carries an opaque texture override at all. So it takes the white the note offered:
`(0.90, 0.91, 0.92)`, with the existing per-slot overrides still giving the glass, tyres, hubs and rims
their own colours so it does not flatten into a slab.

Suite **52/52** throughout. Frames: `shots/van_01.png` (white van, far-side row intact),
`shots/near_01.png` (our side, houses up to the street), `shots/near2_01.png` (looking down the street
with both sides on it).

## The dining chairs faced the wrong way (2026-11-05)

*"when entering the kitchen, the two kitchen chairs closer to the front are facing away from the table.
They need to be turned 180 degrees."* Correct, and the cause was structural rather than a slip: the four
dining chairs are one furniture row each, and the port drew every one of them the same way. Two pairs
on opposite sides of a table cannot both be right if they share a facing -- so one pair was always
going to be backwards, and it was the pair on the front (street) side.

Nothing in the row data could express a facing: `FFurnRow` was `{X, Z, W, D, H, bFlat, Label, Floor}`
and the builder placed every piece axis-aligned. The fix adds a **trailing** `float YawDeg = 0.0f`, so
every row written before it keeps its orientation and only the rows that name one change:

* `FHISim::FurnitureYaws()` -- index-aligned with `FurnitureRects()`, like the heights, labels, flat
  flags and floors the builder already walks;
* `AHIHouse::BuildFurniture(..., YawDeg)` adds the row's yaw to the LABEL's model correction
  (`ModelYaw`, which exists to fix a mesh authored along the wrong axis) when the piece is placed as a
  real mesh -- the dining chairs are `dining_chair_02`, so that path, not the box composition, is what
  draws them;
* the two front rows (`y -10.30`) carry `180.0f`.

The **footprint is untouched** -- a multiple of 90 degrees on a symmetric piece is the same rectangle --
so the sim's collision rects, the furniture-envelope assertions and every nav clearance stay exactly as
they were. This is decoration, and the one thing it must not do is move a chair into a wall.

Verified where it counts: the chair model's own geometry says its back is on -Z (measured: the top 6 cm
of the mesh, 1,216 vertices, sits at z = -0.27 against a mesh centre of -0.13), and the rendered set now
shows the front pair's backs out and their seats across the table. Suite **52/52** (the envelope checks
are unaffected, as intended). `shots/chairs7_01.png` is the top-down check; `shots/chairs3_01.png` is
the same set from the room before.

## The chest, third shape: a cup instead of a cone (2026-11-05)

*"The Dancers boobs look like 2 tents. I would like a fuller cup, like a DD size cup. Alina has nice
boobs."* Right on both counts, and the measurement says why: a bra size is not a number this file can
compute, but Alina's own mesh can supply the targets. Measured off `SourceAssets/characters/alina/idle.glb`
with the same code that measures hers: at the bust line her **cup is 7.2% of her height** and the
front face at the apex is **15.2% of her height wide**. The Dancer's tent was *deeper* than Alina's
(8.5%) and **half as broad** (7.2%): deep and narrow is what a cone looks like. The job was breadth at
a slightly shallower depth, not more amplitude.

**What changed in `tools/edit-glb-chest.py`** -- the displacement, not the pipeline:

* **Two centres, one per breast**, 5 cm either side of the midline and 2 cm below the bust line, at the
  **mid-depth of the ribcage** (measured: back -6.62, underbust surface +10.36, so the centres sit at
  +1.87). The first attempt put them just inside the underbust *surface*, which is nearly where the
  bust surface already is: every direction then pointed sideways, the patch flattened instead of
  rounding, and the amplitude solver ran away to 3.3 million cm.
* **A radial push from each centre** instead of one axis, so a vertex at the apex moves forward and
  one at the edge moves sideways -- inflation rather than a poke.
* **A super-Gaussian falloff** (`exp(-q^2.5)`, radii 10.5 cm across and 7.5 cm up) so the top is a
  plateau with a soft edge rather than a peak.
* **Two Laplacian smoothing passes** over the moved patch, using the mesh's actual triangles, so the
  underbust crease and the inter-breast fold blend instead of creasing.
* The amplitude is DERIVED: the field's own forward component at the apex says how much displacement
  one unit buys, so the solver lands the cup depth on the target in one step (the first version
  corrected in proportion to the miss, which is positive feedback the moment the field pushes the
  wrong way).
* The neckline guard moved from 78% to **80% of height**: the bust line is at 73% and the upper
  breast is *supposed* to gain a little. What must not move is the collar, which is what the first
  version did.

**Shipped depth: `grow` 0.55** -- cup **0.98 -> 6.19 cm**, front face **16.3 -> 14.7 cm** (a dome
scores *narrower* than a slab on that measure, which is why it is reported and not gated). Rendered at
0.55 / 0.75 / 1.0 against Alina and the previous asset: **0.55** is fuller and rounded without the
shelf that 1.0 gives. `shots/chest-cup-compare.png` is that sheet: Alina, the old tent, then the three
depths, front and profile, one camera derived from the mesh (`tools/render-chest.py`).

Through the pipeline unchanged: import, pair into the flat layout, `create_physics_asset`, retarget
(21/21 chains, 10/10 clips, rig scale baked), suite **52/52**, `dancer bind: OK (longest segment 42 cm
= 26% of 161 cm)`, facing **119.7 deg**, `hips->head` **39.3 cm**, `Hips mean 98 cm standing / 48 cm
crouching`. In game: `shots/cup-ingame.png`.

**Rollback points, deliberately kept:** git tag **`dancer-chest-good-2026-11-05`** (commit 14a8a80) and
a tarball of the exact assets, `Saved/snapshots/dancer-chest-good-2026-11-05.tgz` -- her mesh,
skeleton, physics asset, all ten clips and both source glbs. Restore with
`git checkout dancer-chest-good-2026-11-05 -- Content/HIImported/characters_dancer` or untar the
snapshot over the folder. **One word moves the size:** `tools/edit-glb-chest.py <src> <dst> 0.75`
(or 1.0) and re-run the pipeline -- the shape stays, the cup deepens.

## Her hands turned into long swords (2026-11-05)

Richard: *"the dancer when she walks, sometimes her hands turn into what looks like long swords.
Sometimes they are normal."*

**What it was not.** Not the weights (no vertex is more than 41 cm from the joint that drives it, and
658 of 9,509 hand-and-leg vertices are under 25 cm of theirs). Not a bone scale (only the root's
0.01 import scale exists, and the mesh bounds stayed at 1-1.2 m while it happened). Not the translation
curves (every bone's clip translation equals its bind to 0.00% -- `HIAnimTool::CheckBindTranslations`
is what proves that, and it was written to test this theory, which failed).

**What it was.** Thirteen joints -- the whole thumb and every fingertip, in both hands -- had an
`inverseBindMatrix` that placed the bone **1.37-1.42 m from its parent**, while the model's node
hierarchy placed it 1-3 cm away. Unreal's glTF importer builds the bind from those matrices, so the
skeleton carried a fingertip a metre and a half from the hand. That is invisible in the bind pose,
because `inverseBind * bindPose == identity` for every bone no matter how wrong it is -- the mesh
renders exactly as authored. It appears the instant one of those bones **rotates**: its vertices
swing on a 1.4 m radius instead of a 2 cm one, and a handful of hand vertices drawn as a thin sliver
is the sword. "Sometimes" is whichever frames turned a thumb or a fingertip.

**The fix** is `tools/fix-glb-skin.py` -- which already existed for exactly this defect (Alina,
2026-11-01) and had two things that made it refuse her file:

1. **A uniform scale on the rig.** Her `_rootJoint` carries 0.01 and everything beneath it is 100x;
   the old guard rejected any node scale that was not 1. Uniform scales compose, so they are handled
   now; a non-uniform scale is still refused, because the scale/rotation order is a question the
   file has to answer.
2. **A transform on the mesh node.** Her vertices live under `Object_7`, 18.5 cm off the skeleton's
   origin, and the skinning matrix is `jointWorld * IBM` *in mesh space* -- so the bind is
   `inverse(jointWorld) * meshNodeWorld`. For Alina's model (mesh node at the origin) that is the old
   behaviour exactly, which is why the change is safe for her.

Measured after the rewrite (self-check: `jointWorld * IBM` reproduces `meshWorld` to 0.00018 on all 66
joints): the imported bind reads `LeftHandIndex4` **2.4 cm** (was 141.7), `LeftHandThumb1` **3.2 cm**
(was 137.2), `HeadTop_End` **23 cm** (was 137). The mesh is byte-identical to the one that shipped
apart from those 20 matrices: 9,595 verts, bounds `(62.79, 37.06, 80.59)`, facing **119.7 deg**,
`hips->head` **39.3 cm**, `Hips mean 98 cm standing / 48 cm crouching`, suite **52/52**.

Verified in game over twelve phases of the walk (5.6-10.0 s), including the four timestamps where the
blades had appeared: `shots/swords-sweep-after.png`, all clean.

**The check that would have caught it** is now in `HI.Unreal.Cast`: `UHIAnimTool::CheckBindProportions`
reports any bone segment longer than 30% of the character's own height (the pelvis and the root's
children exempt, because the hip offset is legitimately ~1 m). The cast's longest real segment is a
shin at 26%; the broken fingertip was **88%**. Every other assertion in the Cast test read hips,
height or facing and all three were correct while the hands were a metre and a half wrong -- which is
the pattern worth remembering: a bind error is invisible until something rotates.

## The cast walked 90 degrees out (2026-11-05)

Richard, from play: *"when they walk, they don't face where they are going, they are turned 90 degrees
to the right. I suppose you wouldn't see that from just screen shots."* Both halves were right: the
frames I had showed them walking the one heading where the error happened to be near zero, and no
test could see it at all.

**Two yaw conventions, one formula.** `FHISim::AimDir` is `(sin yaw, cos yaw)` -- yaw 0 faces +Y, +90
faces +X -- and that is what the player's yaw, the camera, the player's movement and
`FHICharAnim::BodyRotationFor` all speak. A thief's yaw is not that: `StepThief` steers by
`Atan2(WP.Y - Pos.Y, WP.X - Pos.X)` and moves along `(cos A, sin A)`, so it is a BEARING, 0 along +X.
`AHIIntruder` fed that bearing straight into the body-rotation formula, so every man was turned by
`90 - 2 * bearing`, not by a constant: 90 degrees out at the cardinals, correct only near 45 and 135,
which is why it looked fine walking one way and absurd walking another.

**Measured, on the same frames, by evaluating both paths in one build:** the angle between a walking
thief's chest and his direction of travel was **78-102 degrees** on the old path (90 on the nose when
his yaw had settled) and **0-12 degrees** after -- the residual being the turn-rate lag while he
rounds a corner, which is correct behaviour.

**The fix** is one function, `FHISim::YawFromBearing(Bearing) = 90 - Bearing`, applied at the three
seams where a thief's yaw becomes a FACING rather than an AI input:

- `AHIIntruder::SyncFromSim` -- the body rotation;
- `AHIIntruder::SyncFromSim` -- the pistol's `AimDir` (it was 90 degrees out too, same cause);
- `HIGameMode` -- the carried item's carrier yaw, which `HILootProp` uses both for its facing and
  for the hand offset.

The AI is untouched: its movement, turning and vision cones are self-consistent in bearings, and
converting inside the AI would have changed behaviour for nothing.

**The test that was missing** is in `HI.Sim.View`: `AimDir(YawFromBearing(b), 0)` must equal
`(cos b, sin b)` all round the compass, plus an assertion that walking south is bearing -90 and sim
yaw 180 so the bridge can never be quietly dropped. Before this there was no test that tied a
thief's yaw to a direction at all.

**Two traps worth keeping.**

1. **The Cast test cannot see this class of bug.** It measures each mesh's facing from
   `Cross(RightShoulder - LeftShoulder, Up)`, and the body-rotation formula was derived to agree with
   that same cross product -- so the measurement and the bug share a convention and stay consistent
   while the render is a quarter turn out. The tiebreaker is the SKELETON IN WORLD SPACE (chest axis
   from two shoulder bones vs `Pos - Prev`), or the frame itself: `shots/brute-facing-ab.png` shows
   the brute side-on before and facing his travel after.
2. **A forwarded hypothesis was close but not the cause.** Another session proposed that the cast
   animates out because the clips are Martha's (bind 89.9) while the Dancer's bind faces 119.7, and
   that Alina has no facing row. The error here was heading-dependent in a way a per-tag bind
   mismatch cannot produce (it would be a constant offset for a given tag), and Alina does have a
   number: she uses the 90.0 fallback, and `HI.Unreal.Cast` recomputes every tag's bind facing and
   asserts it, which passes. Worth noting because the wrong fix -- bending the per-tag constants --
   would have masked this on one heading and made it worse on another.

## Cleanup queue — DONE 2026-11-05

Collected 2026-11-04 after the chest attempt; worked through on 2026-11-05. The repo is clean:
`git status` reports nothing except the two Config files below, and the suite is **52/52** before
and after every step.

1. **964 tracked files under `Content/` showed as modified** — and it was almost all noise. 947 of
   them were BYTE-SIZE-IDENTICAL re-saves (an import run re-serialised the packages without changing
   what is in them); reverting those changed nothing: **suite 52/52**. The other 17 were the two
   Config files below and 15 content files with real size differences (martha's `idle_Anim` 20 KB
   smaller, shrub/tree3 textures +93/+418 bytes, six clips +/-1-2 bytes); those were reverted too and
   the suite is still **52/52**, so HEAD is the passing state and the churn was a working-tree
   artefact rather than uncommitted work. A tar of the
   reverted versions was taken first (`/tmp/hi_modified_backup.tgz`, 325 MB) -- `git checkout --
   Content/` was the whole operation once that existed.
2. **117 MB of untracked strays deleted**: `characters_dancer_idle/` (3.1 MB),
   `characters_dancer_idle_Anim/` (57 MB), `characters_martha_idle_Anim/` (57 MB). Re-verified by
   grepping the folder names across every `.uasset`: the only references were between the first two,
   and the game loads none of it.
3. **Untracked art candidates**: `models/woman01/` (14 MB) deleted -- its source glb is in
   `SourceAssets/models`. `college_girlfriend/`, `everyday_jane/`, `girl_with_clothes_worker_set/`,
   `miss_galaxy/` (196 MB) are KEPT: their source models are not in `SourceAssets` any more, so these
   `.uassets` are the only copy, and deleting the only copy of an art candidate is not cleanup. They
   are now in `.gitignore`, which is what the untracked noise was for.
4. **Dead rigs deleted**: `mixamo/{IK,RTG}_Female_Casual*` and `Female_Casual_Skeleton1` (they were
   untracked, not tracked as this note said) plus the Dancer's `idle/SkeletalMeshes/{IK,RTG}_idle`,
   which went with her nested folder when the chest shipped.
5. **The brute's axe** — done; see REALISM-BACKLOG (the axe was material slot 0 `blinn7`, not slot 9).
6. **Daylight through the window openings** is still open — it is a lighting task, not cleanup.

**The one thing that will keep showing up:** `Config/DefaultEngine.ini` and `Config/DefaultInput.ini`
are REWRITTEN BY THE ENGINE on a run. `DefaultEngine.ini` gains an `AndroidFileServer` block with a
`SecurityToken`; `DefaultInput.ini` is re-serialised in the engine's canonical form, which drops the
comment block explaining the deliberate legacy-input choice AND switches
`DefaultPlayerInputClass`/`DefaultInputComponentClass` to the Enhanced Input classes. **Revert both
with `git checkout -- Config/`; do not commit the rewrite** -- it discards the rationale and flips an
architectural decision nobody made. The comment is preserved in HEAD, and the suite passes either way,
which is exactly why it would slip through unnoticed.
