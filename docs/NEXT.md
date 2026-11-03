# Where to start next

Written at the end of a long session (2026-11-01). Read this, `AGENTS.md`, then
`docs/CHARACTERS.md`, and you are current.

## State

- Branch `main`, clean tree, 52/52 tests (`Automation RunTests HI.`), nothing pushed.
- Cast: **dancer, brute, martha** (`FHICharAnim::IntruderTagForId`). Louise is the player.
  James is imported and working; he is out of rotation, not gone.
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

- `Content/HIImported/characters_dancer/` (19 MB), `characters_dancer_idle/`,
  `characters_dancer_idle_Anim/` and `retarget/` (136 KB) are **untracked** — mesh, textures, the
  ten clips and the three rig assets. Nothing of hers was ever committed, and nothing is, because
  her slot is still `james`; `git add` those four folders is what ships her when the facing is
  fixed. Five untracked files in `HIImported/mixamo/` (`IK_Female_Casual*`, `RTG_Female_Casual*`,
  `Female_Casual_Skeleton1`) are dead leftovers from the GUI attempts.
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
- **ALINA: THE SCALE/ORIGIN BLOCKER IS DEAD (2026-11-03, fifth pass).** See the section below --
  her rig now imports at 1.76 m with `hips->head` **56.4 cm** (Martha is 55.1) and 102 bones with
  our names. What is left is clip work, not rig work: her retargeted `death` has its Hips going
  UP 104 cm instead of down, and her walk/run/crouch_walk retargets carry **0 cm of root travel**,
  so `AuthoredCmPerSec` has no row to give her. Everything below this line is the history that led
  there, kept because the diagnosis was wrong four times and the wrong turns are the useful part.
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

**What is still wrong with Alina, both clip-side and both measured** (cast is `dancer/brute/martha`,
52/52, her assets are on disk and out of the cast):

- `alina/death` **rises** 104 cm where it must fall (martha falls 80, brute 100). The retargeted
  Hips descend in the wrong direction for her rig -- the same class of mistake `Cast` already
  projects around for the crouch check (`Hips` up is not always `+Z`; hers is `(0.0, -1.7, 56.4)`).
- her `walk`/`run`/`crouch_walk` retargets carry **0.0 cm of travel**, so there is no authored
  cm/s to put in `AuthoredCmPerSec` and `Cast`'s stride checks are right to fail. Fix the retarget
  first (or author the rows from the engine's own measurement), not the table.

## Working notes that keep costing time

- `FParse::Value` treats a COMMA as a separator -- use `+` for lists
- metres vs UU has cost three separate bugs; check units at every placement
- a fix must be photographed by a camera that FACES it, and the object in frame must be the object
  you changed (the red trees were `tree2`, a crimson pine by design, for four rounds)
