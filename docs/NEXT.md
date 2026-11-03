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
- **ALINA: her SOURCE FILE IS FINE; THE IMPORT IS WHAT DISTORTS HER (2026-11-01, corrected).** Measured
  in the glb itself by composing her node transforms: feet at Y~0, hips **100.7**, head **158.3**, so
  `hips->head` is **57.6** against Martha's 55.1 and her height is 180 units -- a normal adult, rig
  and mesh both Y-up and consistent. An earlier note in this file claimed she was "authored at 2x and
  hip-centred"; that was inferred from engine bounds and is WRONG -- do not act on it.
  What the engine does with her is the problem: after an Interchange import the mesh measures ~345 cm
  (172 half-extent) and `HI.Unreal.Cast` reads `hips->head` as **151.5** -- about 2.6x the truth. The
  assimp->FBX route is no better and now for a different reason: with the merged single mesh it DOES
  carry the mesh, but writes it in metres into an FBX the engine reads as centimetres, so it lands
  1.8 cm tall.
  So the fix is an IMPORT-OPTION question, not a modelling one, and it is cheap to settle: import the
  merged glb with Interchange and vary (a) global/uniform scale 1 vs 100, (b) "use T0 as ref pose",
  (c) convert-scene on/off, reading `mesh bounds` and `hips->head` after each. Three or four runs.
  Everything else about her already works and is committed: `CC_Base_*` renamed to our names,
  `tools/merge-glb-meshes.py` merges her 19 parts into one mesh (stripping `primitive.targets`, which
  Interchange requires -- "Number of Primitive.Targets is not consistent across the Mesh"),
  `IK_alina` auto-characterizes to **21 chains**, 21/21 map, ten clips retarget and bind, and her
  facing is exactly **90.0 deg** so no code row is needed. `tools/scale-glb.py` exists for
  scale/ground surgery on a glb if a file ever really is wrong, but her file does not need it.
- hedges are leaf cards over a box: fine at distance, bright cards in shadow up close
- the getaway van's upper box panels still render red
- `docs/LOBBY.md` is DEFERRED (Steam/multiplayer parked by Richard)

## Working notes that keep costing time

- `FParse::Value` treats a COMMA as a separator -- use `+` for lists
- metres vs UU has cost three separate bugs; check units at every placement
- a fix must be photographed by a camera that FACES it, and the object in frame must be the object
  you changed (the red trees were `tree2`, a crimson pine by design, for four rounds)
