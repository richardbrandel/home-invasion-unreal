# AGENTS.md — Home Invasion (Unreal), project-local memory

The Unreal 5.8.3 sequel of Home Invasion, all gameplay in C++ (no Blueprints), built and
tested headlessly. **Forked from the Godot original on 2026-10-15 — see `docs/PARITY.md`
"THE FORK": the parity obligation is ENDED, the three-floor house is Unreal's own, and new
divergences are recorded in the code + changelog, not re-litigated against Godot.**

## Build, test, soak (this machine)

Project at `/data/projects/HIUnreal`. The engine is the same files under two paths --
`/dev/sdb1` is mounted at both `/data/UnrealEngine` and `/home/richard/UnrealEngine`, and
`stat` gives the identical device:inode (`2065:56885249`). **Building is not indifferent to
which spelling is used; running is.**

```sh
# build (~25-60 s). ALWAYS check "Result: Succeeded" -- a FAILED build silently runs the OLD binary.
# USE /data/UnrealEngine FOR BUILDS. Same files, different spelling from /home, and the engine's
# shared PCH was regenerated under `/data` by the 2026-10-23 full rebuild: building through
# `/home` fails inside any translation unit that includes engine Core headers, with
# `redefinition of 'CSameAsHelper' / 'CDecaysTo' / 'TAreTypesEqual_V'` and -Wshadow errors on
# engine enumerators nobody edited (measured 2026-10-24, three times). `#pragma once` does not
# dedupe a header that arrives once from the PCH under one spelling and again under the other.
# A fully-cached no-op build still "succeeds" through /home, which is what makes it look
# intermittent -- it only bites when something actually compiles. To move back to /home the
# engine's `SharedPCH.*.h.gch` files must be deleted and the engine rebuilt under that spelling
# (hours on 4 cores), so it is a scheduled job, not a loop step.
/data/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh HIUnrealEditor Linux Development -project=/data/projects/HIUnreal/HIUnreal.uproject

# test. Exit 0 only when tests RAN and passed. The runner MASKS crashes -- after it, confirm
# "tests run : N" AND grep -c 'Assertion failed' on the newest /tmp/ue_auto_*.log.
# Filter is HI. (not HI.Sim): the game module carries its own tests, and a too-narrow filter is
# a "pass-shaped nothing".
~/tools/ue-run-tests.sh /data/projects/HIUnreal/HIUnreal.uproject HI.

# soak (one headless round, or rounds). Switches: -HISoak=SEC -HIThieves=N -HINoAuto -HIAuto -HINoThiefDamage.
# "HISOAK RESULT=" prints per round; "HISOAK THIEF" lines print only at round END.
# RUNNING works through either spelling; the project's own scripts use /home/richard/UnrealEngine.
/home/richard/UnrealEngine/Engine/Binaries/Linux/UnrealEditor /data/projects/HIUnreal/HIUnreal.uproject -game -RenderOffscreen -ResX=1280 -ResY=720 -unattended -nopause -nosplash -log -abslog=/tmp/hi.log <switches>
```

**Two sessions run on this machine and they SHARE the build tree, the engine and the GPU.**
Richard, 2026-10-24. Consequences that are not theoretical -- both were measured the same day:

- **Two builds at once corrupts the build, not the source.** Overlapping about a minute of
  two builds produced the Core-header `redefinition` storm above in a tree nobody had edited.
  `tools/devloop-shots.py` takes an `flock` (`/tmp/hi_devloop_build.lock`) around its build;
  any other caller should do the same rather than trusting a quiet machine.
- **A capture kills a running game** (`capture.sh` pkills `UnrealEditor` by name). If the other
  session is playing, that reads to them as a crash. `capture.sh` and `tools/devloop-shots.py`
  both refuse unless `--take-over` is passed.

**`-game` launches mangle `Config/*.ini`** (AndroidFileServer junk in DefaultEngine.ini, a
comment-stripped rewrite of DefaultInput.ini). Re-check `git status` and `git checkout --
Config/` immediately before any commit.

## House style

- Measurements over guesses; label each claim verified vs inferred, and run the cheap check
  instead of theorizing.
- Every deliberate change carries a DATED comment (`2026-10-XX`) and the measurement that
  decided it. `WRONG-GUESS-RECORDED` for honest history — record what failed, not only what shipped.
- "Report, don't patch" design questions go to `docs/OPEN-QUESTIONS.md`, never a silent change.
- Public repo: never write private names into it (mechanically enforced by
  `.git/hooks/pre-commit`, but the hook is PER-CLONE — a fresh clone must reinstall it).

## Docs map

- `README.md` — build/run/capture/soak harness and its history.
- `docs/HOUSE-DESIGN.md` — §1–7 the design, §8 "as built" (the measured record of phase 2).
- `docs/OPEN-QUESTIONS.md` — what waits on Richard (items 9–13 added on phase 2).
- `docs/PARITY.md` — the port audit, frozen as of the 2026-10-15 fork.
- `docs/TESTING.md` — what actually finds bugs in this port.
- `docs/REALISM-BACKLOG.md` — Richard's realism decisions.

## Never change without re-measuring (the code comments carry the measurements)

- `WalkBlocked` stays radius-0 (line clearance); body-radius lives only in the file-local
  `WalkBlockedBody` helper (span-only gate, no floor-tag filter).
- `ResolveCircleZ` itself; the `stairTopG` node (−0.70, −15.50); `screenPass` and its edges.
- `StepThief` mode order (flee→carry→hunt→search) and the step/event-ordering contracts.
- The corner-rounding commitment lifetime: pick side once, freeze `CornerDir`, 3 s staleness,
  windowed-detector corner exemption — do not "simplify" without re-running the battery.
- Hysteresis window **8.0 s** + 0.3 m node match (`HOUSE-DESIGN §8.6`: 2.0 s lost the race, measured).
- Player spawn (−2.0, −10.6) — Richard's ruling, "defender by position, not by script".
- `PoliceTime = 180.0` — ships 180, ratification pending (`OPEN-QUESTIONS` item 9).

## Measured facts (final phase-2 build, 2026-10-19)

- Suite **51/51** green. Soaks, every one `ended=round`: trio **3×117.9 s bit-identical**;
  solo **181.5 s** with Louise camping (damage-live ends hp 29) / **175.0 s** lawn; driven
  **3× WON at 24.8–26.1 s**.
- Player hp = `ThiefMaxHp` = **78** (passive soaks' hp=78 is FULL health). Thief shots do NOT
  arm the alarm (measured 126 shots, `Alarm` stayed −1.0).
- 10 of 33 flat nav edges are body-radius-blocked at door jambs (`HOUSE-DESIGN §8.6`); the
  walker's hysteresis carries the fix, the data-level answer is Richard's call (item 12).
- Push uses SSH (`git@github.com:richardbrandel/home-invasion-unreal.git`); the HTTPS remote
  has no credentials on this box.

## Release builds (first packaged build, 2026-10-23)

- **DEVELOPMENT IS LINUX-ONLY ON THIS BOX** (Richard, 2026-10-24). Windows is not a
  development target and this machine does not build it. When the game is promoted to Steam,
  the **deployment session** handles Windows; until then nothing here should carry a
  Windows-specific constraint, and "it will not build on Windows" is not a reason to defer a
  change. The earlier plan -- building Windows on Plane1, the Windows machine, with a 5.8.3
  engine at `C:\UnrealEngine` and the project at `C:\HIUnreal` -- is HISTORY, kept only
  because the first packaged build came from there.
- Every asset is loaded by path, so **cooking needs `bCookAll=True`** (`Config/DefaultGame.ini`).
  Without it the package ships engine content only: invisible people, no refrigerator, cube
  furniture, and `Failed to find object` lines in the log.
- **`bUseZenStore=False`** for the same class of reason: the cook reaches Zen at the hardcoded
  IPv6 loopback `[::1]:8558`, and this host has no IPv6 loopback at all, so the cook dies with
  `Error_UnknownCookFailure`.
- Strip before shipping: `*.debug`, `*.sym`, `*.pdb`, `VkLayer_*validation|gfxreconstruct|api_dump`
  and `d3d12SDKLayers.dll`. That is 3.4 GB down to 937 MB, none of it runnable.
- **Next release: build Shipping, not Development**, so the debug info inside the binaries goes
  too, not just the sidecar symbol files (Richard, 2026-10-23).
- The Linux package built here is the one development verifies against
  (`Dist/Linux/HIUnreal.sh`). It goes **stale** the moment `Source/` changes -- it is a cook,
  not a build -- so a screenshot loop must use the editor's `-game` path, which picks up a
  rebuilt module in seconds, and only re-cook when the package itself is the thing being
  checked.
