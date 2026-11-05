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

## The Dancer Girl's chest edit: mesh done, asset pipeline blocked (2026-11-04)

Richard asked for her bust roughly doubled. **The mesh edit itself is finished and verified**;
what fails is getting the edited mesh back into a playable character. Two attempts, both rolled
back, both documented here so a third does not repeat them.

**The edit is done.** `SourceAssets/models/woman01_edited.glb` is her source glb with the chest
bulged by `tools/edit-glb-chest.py` — vertices only, so skeleton, inverse-bind matrices, weights,
UVs and materials are byte-identical. Measured: chest apex **7.43 cm -> 12.40 cm** in front of the
chest centre (x1.67 protrusion, about double the volume), 203 vertices, symmetric about the midline.
`SourceAssets/models/woman01_original.glb` is the untouched source, kept for rollback.
`SourceAssets/models/woman01_edited_renamed.glb` is the same edit after `rename-glb-bones.py`
(`GLB_WRAPPER=1 GLB_SCALE=1`), i.e. the file that is ready to import.

**What works: the rig comes back exactly right.** Importing the renamed glb and letting the retarget
pipeline flatten it reproduces her original rig to the digit:
`dancer: facing 119.7 deg; hips->head=(2.2, -3.9, 39.1) |39.3|` -- identical to the values recorded
on 2026-11-01. So the axis-convention problem that killed the first attempt (a Blender glTF
round-trip gave facing 56.7 deg and the spine along -Z) is solved by using the project's own wrapper.

**The two blockers, in the order they bite:**

1. **`retarget-character.sh` cannot retarget a mesh that was MOVED out of `models/`.** Its
   `move_mesh_into_place` path fails with `Unable to retrieve bone index for track: Hips/Spine`
   even though the target skeleton has those bones and the chain mapping reports 21/21. The same
   script SUCCEEDS when the mesh is imported directly into `characters_dancer` as `idle.glb` and
   flattened (mesh -> idle_tmp, skeleton -> idle_Skeleton, physics -> idle_PhysicsAsset, move
   Textures/Materials up, delete the nested folder, idle_tmp -> idle). That path produced ten bound
   clips.
2. **On that working path the character is still broken in two ways:** her retargeted clips carry
   NO pelvis motion (`HI.Unreal.Cast`: "the standing idle's Hips mean -0 cm", should be ~96) and the
   flattened mesh has NO physics asset ("dancer carries a physics asset ... must be not null"). So
   she would sink into the floor and never fall. Both must be fixed before this ships.

**Two tooling traps that cost most of the time, both measured:**

- **Renaming a skeletal mesh and its skeleton as separate assets can sever the mesh's skeleton
  link**, and `skeleton` is read-only from Python. The only reliable way is to let the importer
  create the pair, which means the FILE must be named `idle.glb` before import.
- **Blender's FBX exporter is unusable in 5.0** (same `use_space_transform` failure the project
  recorded months ago), which is why `tools/edit-glb-chest.py` edits the glb directly.

**Rollback, used twice and proven:** `git checkout -- Content/HIImported/characters_dancer`
(her content is TRACKED, 27 files, and restores mesh + skeleton + ten clips), then re-import
`SourceAssets/models/woman01_original.glb` into `/Game/HIImported/models/woman01/SkeletalMeshes`
as `woman01.glb` if the model folder was touched, then run the suite: expect 52/52.

## Cleanup queue (for after the Dancer chest attempt)

Collected 2026-11-04, not started. Ordered by how much it cleans for how little risk.

1. **962 tracked files under `Content/` show as modified.** 951 of them date from 2026-10-31 (an
   import run), the rest from the 2026-11-03/04 imports. The current on-disk state is what passes
   52/52, so this is a decision, not a chore: either commit them (a large binary commit) or
   `git checkout -- Content/` and re-verify. Do not "tidy" it without re-running the suite.
2. **Untracked and referenced by nothing** (verified by grepping the folder name across every
   `.uasset`): `characters_dancer_idle/`, `characters_dancer_idle_Anim/`,
   `characters_martha_idle_Anim/` — 117 MB of strays from earlier import layouts. Safe to delete.
3. **Untracked art candidates**: `models/college_girlfriend`, `everyday_jane`,
   `girl_with_clothes_worker_set`, `miss_galaxy`, `woman01` (203 MB, none of them in the game).
4. **Dead rigs** in `HIImported/mixamo/` (`IK_Female_Casual*`, `RTG_Female_Casual*`,
   `Female_Casual_Skeleton1`) and `characters_dancer/idle/SkeletalMeshes/{IK_idle,RTG_idle}` — from
   the GUI attempts, nothing references them. Note these are TRACKED, so deleting them is a commit.
5. **The brute's axe** — baked into his mesh (`Axe_GEO`, material slot 9 `phong4`). Hiding the slot
   with a zero-opacity material rendered it white instead of removing it. The fix is a model edit,
   the same in-place-glb trick that worked for the Dancer's chest.
6. **Daylight through the new window openings** — the openings are real and the glass is clear, but
   nothing lights the rooms through them yet; two lighting attempts were reverted (see
   REALISM-BACKLOG item 8).
