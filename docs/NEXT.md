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
- **ALINA: THE ASSET IS MALFORMED, AND THAT IS NOW PROVEN (2026-11-01, third correction).** Her
  glb declares a 105-joint skin whose `inverseBindMatrices` are partly ZERO-TRANSLATION
  PLACEHOLDERS: `CC_Base_Hip_02` carries IBM translation (0,0,0) while its bind position, composed
  from the node transforms, is (0, 100.7, 0) -- measured on Hip, L_Thigh, L_Calf. **21 of 102** are
  wrong; the rest are real. `tools/fix-glb-skin.py` recomputes them from the node transforms and
  self-checks (`|IBM * bindPosition|` comes back 0.0000).
  BUT THE UNITS DO NOT RECONCILE EITHER. After the IBM repair the engine reports the Hips at
  **10069 cm** (its unit conversion multiplying *joint translations* by 100) while the mesh stays
  **345 cm** (vertices passing through ~1.9x). Vertices and joints are being scaled by different
  factors, so no single pre-scale in the file can satisfy both -- and that is the state of this
  asset, not a knob I have not found yet. Earlier notes here said "she is 2x and hip-centred" and
  then "her file is sound"; both were wrong, this is the measured version.
  So: her asset needs a RE-EXPORT through a tool that recomputes skinning and units together. That
  is available -- `apt-cache policy blender` offers 5.0.1 and passwordless sudo works on this box.
  The project's recorded objection to Blender concerns its FBX **animation** exporter
  (`use_space_transform`), not a skinned mesh export, and its glTF exporter is the sane one; so:
  import her glb in Blender, export FBX (mesh + skeleton, no clips) for the engine's FBX importer,
  and fall back to a re-exported glb if the FBX path trips the same bug. Then the pipeline that
  already works takes over -- `IK_alina` auto-characterized to 21 chains on the first attempt, 21/21
  mapped, ten clips retargeted, facing exactly 90.0 deg -- and step 6 is the one-line cast flip.
  If the re-export disappoints too, the cheap fallback is her materials on the Dancer Girl's body.
- hedges are leaf cards over a box: fine at distance, bright cards in shadow up close
- the getaway van's upper box panels still render red
- `docs/LOBBY.md` is DEFERRED (Steam/multiplayer parked by Richard)

## Working notes that keep costing time

- `FParse::Value` treats a COMMA as a separator -- use `+` for lists
- metres vs UU has cost three separate bugs; check units at every placement
- a fix must be photographed by a camera that FACES it, and the object in frame must be the object
  you changed (the red trees were `tree2`, a crimson pine by design, for four rounds)
