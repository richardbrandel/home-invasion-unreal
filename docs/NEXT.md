# Where to start next

Written at the end of a long session (2026-11-01). Read this, `AGENTS.md`, then
`docs/CHARACTERS.md`, and you are current.

## State

- Branch `main`, clean tree, 52/52 tests (`Automation RunTests HI.`), nothing pushed.
- Cast: **james, brute, martha** (`FHICharAnim::IntruderTagForId`). Louise is the player.
- The game plays; recent work is mostly scene/asset work, not mechanics.

## The one open technical problem

**Replacing an intruder with a downloaded character.** Two attempts failed, both documented in
`docs/CHARACTERS.md`:

1. offline rig conversion (glb -> rename bones -> assimp -> FBX). The mesh imports upright after a
   Z-up wrapper, but it animates **prone and facing 119.7 deg**, and assimp flattens the node
   rotation that would fix it. Dead end.
2. `IKRetargetBatchOperation.duplicate_and_retarget` from Python. The call works with `FAssetData`
   (see the doc for the three forms that fail), but with no retargeter asset it **copies** --
   binding the result to the SOURCE skeleton -- and **consumes its inputs**, which cost Martha all
   ten of her clips once (restored; `HI.Unreal.Cast` caught it).

**The blocker:** a real IK Retargeter needs IK Rig assets, and this build exposes no
`IKRigFactory`. Creating source/target IK Rigs + auto-mapping chains **in the editor GUI** is a few
minutes of clicking; the ten-clip retarget against that asset is already scripted and working
(`tools/retarget-character.sh <tag> <source-tag> <model>`).

## Ready to go when that lands

- `SourceAssets/models/woman01.glb` -- "Dancer girl", Mixamo-family rig, chosen by Richard
- `SourceAssets/models/alina.glb` -- "Alina Ip, realistic Asian woman", CC_Base rig, to replace
  Martha
- both need the same retarget; whichever works first proves the pipeline

## Smaller open items

- hedges are leaf cards over a box: fine at distance, bright cards in shadow up close
- the getaway van's upper box panels still render red
- `docs/LOBBY.md` is DEFERRED (Steam/multiplayer parked by Richard)

## Working notes that keep costing time

- `FParse::Value` treats a COMMA as a separator -- use `+` for lists
- metres vs UU has cost three separate bugs; check units at every placement
- a fix must be photographed by a camera that FACES it, and the object in frame must be the object
  you changed (the red trees were `tree2`, a crimson pine by design, for four rounds)
