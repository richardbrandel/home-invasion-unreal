# Handover notes for the deployment session (2026-11-05)

Everything below is committed. `git status` is clean and the suite is 52/52.

## What is new since the last deployable state, and must be packaged

All of it is under `Content/`, and all of it is committed -- a fresh clone has it:

| folder | what |
|---|---|
| `Content/HIImported/characters_dancer/`, `_alina/`, `_brute/` | new clips per character: **`flinch`** (head snap), **`hang`** (arms overhead, legs down, 48 keys of sway), **`hang_walk`** (hang above the hips, the character's own walk legs below) |
| `Content/HIImported/voice/` | **17 new sound assets**: `owner_getout`, `owner_cell_1..3`, `brute_1..3`, `alina_1..5`, `dancer_1..5`. The nine older `owner_*` lines are still there and still used (`owner_police` in particular) |

No new C++ modules, no new engine plugins, no new runtime dependencies. `SourceAssets/` is gitignored and holds
nothing the game needs at run time.

## Voices: how they were made, if a line must change

`tools/make-voices.sh` (Piper, local neural TTS, free) renders every line and imports it through
`tools/import-voices.py`. The WAVs and the ~380 MB of voice models land in the gitignored `SourceAssets/voices`,
so **they are not in a fresh clone** -- only the imported `.uasset`s are. Re-run the script only if a line
changes; it downloads what it needs.

Casting, so nobody re-litigates it by accident: homeowner `en_GB-alba` (British woman) · Alina `ru_RU-irina`
(Russian accent) · Dancer `en_US-lessac` **resampled up 12%** (`tools/pitch-up.py`) · brute `en_US-mike`,
slowed 12% and pitched down 6%. Those two processing steps are the `--length-scale` flags and `pitch-up.py`
calls inside the script; nothing at run time depends on them.

**Verification trick worth keeping:** the renders are transcribed back with Whisper
(`faster-whisper`, CPU) so a line that the accent mangles is caught before it ships. That is how
"lawyer" (heard as "liar" at the default pace) got its `--length-scale 1.45`.

## Capture-only command-line flags

These exist for the screenshot harness and do nothing in play; they are safe to leave in a build:
`-HIShotPos`, `-HIShotFloor`, `-HIShotYaw`, `-HIShotPitch`, `-HIShotSeries`, `-HISnap`, `-HIShot`,
`-HISOAK`/`-HISoak`, `-HINoAuto`, `-HIAuto`, `-HINoThiefDamage`, `-HIThieves`, `-HITranqDemo`,
`-HISquirmDemo`, `-HISquirmSpam`, `-HIFireAt`, `-HIShoutAt`, `-HIWeaponAt`, `-HIDark`, `-HIModels`.

## Known cosmetic limitations (not bugs, deliberately not fixed)

* The cast has **no face rig and no blendshapes** (measured: 0 morph targets), so facial expressions are
  impossible without new models or a painted face texture. Fear is sold with the neck snap (`flinch`).
* The Dancer's rig carries the glTF import's **100x encoding**, which breaks physics for her: her capture
  falls by hand (`HI: dancer's ragdoll threw the body 96 m -- falling by hand instead`) rather than by ragdoll.
  Anything else physics-shaped will hit the same wall.
* The stick has no impact sound of its own; it plays the prisoner's response line.

## Before packaging

1. `~/tools/ue-run-tests.sh /data/projects/HIUnreal/HIUnreal.uproject HI.` -> expect **52/52**.
2. One soak with darts to prove the whole capture path:
   `UnrealEditor HIUnreal.uproject -game -HISoak=60 -HITranqDemo=3@3 -HISquirmSpam=6 -log`
   The log should show `HI: STICK ...` breadcrumbs, `HI: voice ...` lines, and no assert.
3. `git status` clean; `Config/` must be checked out (engine runs rewrite `DefaultEngine.ini`/`DefaultInput.ini`).
