# AGENTS.md — Home Invasion (Unreal), project-local memory

The Unreal 5.8.3 sequel of Home Invasion, all gameplay in C++ (no Blueprints), built and
tested headlessly. **Forked from the Godot original on 2026-10-15 — see `docs/PARITY.md`
"THE FORK": the parity obligation is ENDED, the three-floor house is Unreal's own, and new
divergences are recorded in the code + changelog, not re-litigated against Godot.**

## Build, test, soak (this machine)

Project at `/data/projects/HIUnreal`; engine at **`/data/UnrealEngine`**, and that is the only
path. There was a second one -- `/dev/sdb1` was also bind-mounted at
`/home/richard/UnrealEngine`, the same device:inode, and `nofail`-mounted from `/etc/fstab`.
**Building is not indifferent to which spelling is used:** the shared PCH records absolute
paths, so a build through the alias failed inside any translation unit that includes engine
Core headers (`redefinition of 'CSameAsHelper' / 'CDecaysTo'`, -Wshadow errors on engine
enumerators nobody edited, measured 2026-10-24). A fully-cached no-op through the alias still
"succeeded", which is what made it look intermittent -- it only bit when something compiled.

**The alias is gone (2026-10-24):** unmounted, the `fstab` line commented out, the empty
directory removed, and every tool in `tools/` plus this file and the README rewritten to
`/data/UnrealEngine`. If you find `/home/richard/UnrealEngine` in anything, it is stale --
fix it rather than recreating the mount.

```sh
# build (~25-60 s). ALWAYS check "Result: Succeeded" -- a FAILED build silently runs the OLD binary.
# Use /data/UnrealEngine. A cached no-op build proves nothing about the path: it "succeeds"
# even when a real compile would fail (see the alias note above).
/data/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh HIUnrealEditor Linux Development -project=/data/projects/HIUnreal/HIUnreal.uproject

# test. Exit 0 only when tests RAN and passed. The runner MASKS crashes -- after it, confirm
# "tests run : N" AND grep -c 'Assertion failed' on the newest /tmp/ue_auto_*.log.
# Filter is HI. (not HI.Sim): the game module carries its own tests, and a too-narrow filter is
# a "pass-shaped nothing".
~/tools/ue-run-tests.sh /data/projects/HIUnreal/HIUnreal.uproject HI.

# soak (one headless round, or rounds). Switches: -HISoak=SEC -HIThieves=N -HINoAuto -HIAuto -HINoThiefDamage.
# "HISOAK RESULT=" prints per round; "HISOAK THIEF" lines print only at round END.
# RUNNING works through either spelling; the project's own scripts use /data/UnrealEngine.
/data/UnrealEngine/Engine/Binaries/Linux/UnrealEditor /data/projects/HIUnreal/HIUnreal.uproject -game -RenderOffscreen -ResX=1280 -ResY=720 -unattended -nopause -nosplash -log -abslog=/tmp/hi.log <switches>
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
- **Packaging command (deployment session)**, and it must use the `/data` spelling for the same
  PCH reason as builds above -- `RunUAT.sh` compiles the Game target, so a cached no-op will not
  save it:

  ```sh
  /data/UnrealEngine/Engine/Build/BatchFiles/RunUAT.sh BuildCookRun \
      -project=/data/projects/HIUnreal/HIUnreal.uproject -noP4 -platform=Linux \
      -clientconfig=Development -build -cook -stage -pak -archive \
      -archivedirectory=/data/projects/HIUnreal/PackagedLinux -utf8output
  ```

  Then verify by running it, not by trusting `BUILD SUCCESSFUL`: a 15 s soak
  (`-HISoak=15 -unattended -RenderOffscreen -log -abslog=...`) must print `HISOAK RESULT=` and
  `grep -c 'Failed to find object'` must be **0**. That single check is what caught the cook
  shipping no content while the build reported success.

## The dev loop: one agent, plus mechanical gates

Richard runs **two sessions** against this box, but only **one of them is needed for the loop**.
Measured 2026-10-28, over a full day:

- **Brain (the Development session) contributed nothing to the loop.** It never wrote a single
  command to `DevToRunner.txt` (its diary's last entry was reading a test line), and the one time
  it edited `Source/` directly it appended the same comment twenty times to two lines and
  **changed no value** -- a wasted round, cleaned up by hand.
- **Hands (this session) did the whole job**: found the defects by looking at the frames, wrote
  the fixes, built, re-shot, verified. Two verified rounds in twenty minutes (ceiling fitting,
  framed picture).
- **The Eyes are the same model as Hands** (`qwen3-vl-plus`, per the session log), so
  `tools/vl-ask.py` is a second opinion, not a separate capability.

So the working shape is **one agent + gates in code**:

| part | does | why it is not an agent |
|---|---|---|
| **the agent** (Hands) | looks at the frame, decides, writes the fix, builds, re-verifies | it is the only part that needs judgement |
| **the gates** (in `runner-daemon.py`) | patch must match the named line; build must print `Result: Succeeded` or the edit is reverted; 20-round cap; timestamps UTC | these are checks, not decisions -- and a check an agent enforces on itself is the one that gets forgotten |
| **the keys** (`tools/vl-ask.py`) | optional second opinion on a frame | same model, so same blind spots; useful for a fresh read, not for authority |

**Brain earns a slot only if it does something the agent structurally cannot.** The one candidate
is an independent review of the *patch* (a different model reading the diff), not of the picture.
Until that exists, a third participant is a third thing that can stall, and it stalled for a day.

`/home/richard/shots/BRAIN-BRIEF.md` is kept as the record of the Brain role that was tried
and is not currently part of the loop.

- **THE BRAIN MUST NOT EDIT `Source/` DIRECTLY** (measured 2026-10-27). Left to edit files, it
  appended the same comment fragment twenty times to two lines and de-indented a statement --
  **no value changed at all**, and the round was wasted. Edits go through `FIX:` so the runner
  replaces verified text in one place. A `FIX` whose old text is not on the named line is refused.
- **There is exactly ONE Hands**: `tools/runner-daemon.py`. Never start
  `tools/vl-loop.py.disabled` or `~/.dsh/tools/vl-proxy.py` -- both watch the same command file,
  and two runners kill each other's `UnrealEditor` by process name.
- **All timestamps are UTC**, because a peer tool writes UTC and two clock bases in one log made
  `07:50` and `11:50` the same moment four hours apart.
- **A repeated identical command is a NEW command.** The command file is a single slot whose text
  repeats by design (re-shoot the same view after a fix), so the runner keys on content **plus
  mtime**, not text. Text-hash dedupe hangs the loop on its commonest step.
- **A capture does not build.** It warns when `Source/` is newer than the built module, and
  `ACTION: build` exists so hand-edited source is never photographed stale.
- **The round cap lives in Hands** (20, persisted in `.runner_rounds`, `STATUS: clear` resets it).
  A limit an agent enforces on itself is the one that gets forgotten.
- **Log the session, not just the actions**: `python3 tools/diary.py "…"` stamps UTC into
  `AgentLog.txt`.
- **The eyes are a detector, not a patch generator.** Asked for patches they invented four files
  that do not exist, including Blueprints (this project has none).
- **The repository is plain git, no LFS** (decided 2026-10-29, before the first push). Measured
  then: `.git` 1.4 GB, working `Content/` 954 MB, largest single blob 26.4 MB -- under GitHub's
  100 MB per-file limit. LFS was rejected rather than deferred: its free tier is 1 GB storage and
  1 GB/month bandwidth, and the asset set alone is ~954 MB, so a single clone would exhaust the
  month and the account would start paying. Revisit only if the repo outgrows ~2-3 GB, and note
  that migrating a *published* history means a force-push. Windows release assets go on GitHub
  Releases (v0.1, v0.2), never into the repo.
