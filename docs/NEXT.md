# Where to start next

Written at the end of a long session (2026-11-01). Read this, `AGENTS.md`, then
`docs/CHARACTERS.md`, and you are current.

## State

- Branch `main`, clean tree, 52/52 tests (`Automation RunTests HI.`), nothing pushed.
- Cast: **james, brute, martha** (`FHICharAnim::IntruderTagForId`). Louise is the player.
- The game plays; recent work is mostly scene/asset work, not mechanics.

## The one open technical problem

**Replacing an intruder with a downloaded character.** Both earlier attempts failed and are
documented in `docs/CHARACTERS.md`; the second one is now SOLVED (2026-11-01):

1. offline rig conversion (glb -> rename bones -> assimp -> FBX) — dead end, the bone rest
   orientation it carries animates prone.
2. ~~`IKRetargetBatchOperation.duplicate_and_retarget` from Python, no retargeter asset~~
   **DONE.** The factory the docs said did not exist is `unreal.IKRigDefinitionFactory` (wrong name,
   not a missing capability), and `tools/retarget-character.sh <tag> <src> <model>` now builds the
   two IK Rigs, builds the IK Retargeter, auto-maps its 21 chains and runs the batch itself. Her ten
   clips now bind to HER skeleton — verified per clip, and the keys are resampled rather than copied.
   The second trap was that a freshly created IK Retargeter has NO op stack, so chain mapping
   silently maps nothing; `AddDefaultOps()` first.

**What still blocks her: her bind-pose facing.** `HI.Unreal.Cast` measures 119.7 deg where
`FHICharAnim::MeshFacingYawDeg()` says 90, so she would draw with a 29.7 deg torso twist. Every
other fact about her assets passes with her in slot 0 (mesh, skeleton, physics asset, all ten clips
bound to her skeleton, lengths, rig bones, normalized keys, death fall): the suite is **51/52** with
her in the cast and **52/52** with `james` in it, which is where the line is left. Two routes, and
the reasoning for trying the first — her skeleton asset and her mesh appear to disagree about the
reference pose, which would explain the facing and the odd Hips values in her clips at once — are in
`docs/CHARACTERS.md`.

## Ready to go when that lands

- `SourceAssets/models/woman01.glb` — "Dancer Girl", Mixamo-family rig, chosen by Richard
- `SourceAssets/models/alina.glb` — "Alina Ip, realistic Asian woman", CC_Base rig, to replace
  Martha
- both use the same one-command pipeline now; `alina` is the harder one (foreign rig family)

## Smaller open items

- `Content/HIImported/characters_dancer/` (19 MB), `characters_dancer_idle/`,
  `characters_dancer_idle_Anim/` and `retarget/` (136 KB) are **untracked** — mesh, textures, the
  ten clips and the three rig assets. Nothing of hers was ever committed, and nothing is, because
  her slot is still `james`; `git add` those four folders is what ships her when the facing is
  fixed. Five untracked files in `HIImported/mixamo/` (`IK_Female_Casual*`, `RTG_Female_Casual*`,
  `Female_Casual_Skeleton1`) are dead leftovers from the GUI attempts.
- the earlier GUI attempts left dead rigs at `characters_dancer/idle/SkeletalMeshes/IK_idle`,
  `RTG_idle` and `HIImported/mixamo/IK_Female_Casual*` — safe to delete, nothing references them
- hedges are leaf cards over a box: fine at distance, bright cards in shadow up close
- the getaway van's upper box panels still render red
- `docs/LOBBY.md` is DEFERRED (Steam/multiplayer parked by Richard)

## Working notes that keep costing time

- `FParse::Value` treats a COMMA as a separator -- use `+` for lists
- metres vs UU has cost three separate bugs; check units at every placement
- a fix must be photographed by a camera that FACES it, and the object in frame must be the object
  you changed (the red trees were `tree2`, a crimson pine by design, for four rounds)
