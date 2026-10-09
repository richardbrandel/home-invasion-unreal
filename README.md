# Home Invasion — Unreal port

The Unreal Engine 5.8.3 port of **Home Invasion**. The Godot game in
`../home-invasion-godot/` is the working version and the source of truth for
behaviour; this project exists to recreate it in Unreal without rewriting the design.

## The specification, and why this port is testable

`../home-invasion-godot/test/test_sim.gd` is a **~100-assertion specification** over
`scripts/sim.gd`, which is pure logic with no rendering dependencies. The strategy is
therefore: port the sim to **C++**, and make those tests pass here. A port that passes
the same tests is correct by construction; one judged by eye is a guess.

**Do not build gameplay in Blueprints.** Blueprints cannot be exercised headlessly, and
the headless automation loop is the entire reason the Unreal route is viable.

## Where this runs

The Mac has no Unreal. Build and test happen on **ubuntu-dev**, and the build tree there
is `/data/projects/HIUnreal`:

| | |
|---|---|
| Engine | UE 5.8.3 at `/data/UnrealEngine`, bind-mounted as `/home/richard/UnrealEngine` |
| Project (build tree) | `ubuntu-dev:/data/projects/HIUnreal` |
| Project (source of record) | this directory |
| Compiler | clang 20.1.8 (Epic's pinned toolchain) |
| GPU | RTX 5060 Ti 16 GB, driver 610.57.04, **headless** — no display, no Xvfb |

Sync source to the build tree before building:

```sh
rsync -a --exclude='Intermediate/' --exclude='Binaries/' --exclude='DerivedDataCache/' \
  --exclude='Saved/' --exclude='.vscode/' --exclude='Makefile' --exclude='Content/' \
  ./ ubuntu-dev:/data/projects/HIUnreal/
```

## Engine art — fetched, not committed

`Content/` is **gitignored**. Everything in it is copied out of the Unreal install by
`tools/fetch-engine-assets.sh`, because committing ~135 MB of duplicated engine content
would make this repository mostly a copy of something the machine already has.

```sh
ssh ubuntu-dev '/data/projects/HIUnreal/tools/fetch-engine-assets.sh'
```

It brings in the UE5 Mannequin (two meshes, skeleton, materials, every animation, and the
locomotion **blend space**), plus the pistol and rifle meshes. **Read that script before
changing it**: the destination paths are dictated by the assets' OWN internal `/Game/...`
package references, not chosen, and a copy that lands anywhere else silently loses
skeletons and textures.

## Build and test

```sh
# Build (incremental is ~25 s; a -Clean rebuild recompiles the shared PCH, many minutes)
ssh ubuntu-dev '/home/richard/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh \
  HIUnrealEditor Linux Development -project=/data/projects/HIUnreal/HIUnreal.uproject'

# Test. Exits 0 only when tests ran AND all passed -- zero tests is a failure by design,
# because `-TestExit` hardcodes exit status 1 and the editor's exit code is not a verdict.
# Filter is `HI.` and not `HI.Sim`: the game module carries its own tests now, and a filter
# that quietly matches less than you think is the "pass-shaped nothing" this project keeps
# having to guard against.
ssh ubuntu-dev '~/tools/ue-run-tests.sh /data/projects/HIUnreal/HIUnreal.uproject HI.'
```

## Running it — and this is new

**Nothing here had ever been launched until 2026-10-09.** It now has been, offscreen, and
the harness below is what did it. There is no display on the build machine, so "launch"
means `-RenderOffscreen` plus a screenshot; the frame is then copied to the Mac and
LOOKED at.

```sh
# One captured frame, from a chosen spot, after N seconds of round
ssh ubuntu-dev 'cd /data/projects/HIUnreal && \
  /home/richard/UnrealEngine/Engine/Binaries/Linux/UnrealEditor \
    /data/projects/HIUnreal/HIUnreal.uproject -game -RenderOffscreen \
    -ResX=1280 -ResY=720 -unattended -nopause -nosplash -log -abslog=/tmp/hi.log \
    -HIShot=NAME -HIShotAfter=4 -HIShotPos=0,-10.6 -HIShotYaw=0 -HIShotPitch=2 -HIThieves=1'
# the PNG lands in  Saved/Screenshots/LinuxEditor/NAME.png
```

| Switch | Meaning |
|---|---|
| `-HIShot=NAME` | capture one frame, then quit |
| `-HIShotAfter=SEC` | how much round time first (default 1.5) |
| `-HIShotPos=X,Y` | stand the homeowner at a sim coordinate. Also turns the autopilot OFF, so a posed frame stays posed |
| `-HIShotYaw=DEG`, `-HIShotPitch=DEG` | aim it |
| `-HIShotNoUI` | capture without the HUD |
| `-HIAuto` | drive the homeowner from the autopilot |
| `-HINoAuto` | force the autopilot off |
| `-HINoThiefDamage` | **diagnostic**: withhold the intruders' damage (see below) |
| `-HISoak=SEC` | run a round and print `HISOAK RESULT=…`, then quit |
| `-HIThieves=N` | how many intruders (default 3) |

**Screenshot arguments are read from `FCommandLine::Get()` by hand, not by `FParse::Value`,
and there is a reason** — see the porting hazards. The screenshot path is printed to the log
on every scripted run, because "the file was never written" and "the game did not run" look
identical otherwise.

### The two soaks, and what each one is allowed to conclude

`-HISoak` prints one line:

```
HISOAK RESULT=WON ended=round seconds=5.0 intruders=3 alive=0 down=3
       loot_delivered=0 loot_remaining=3 player_hp=50 shots=17
```

`ended=round` and `ended=budget` are different findings: the first is a game that resolved,
the second a game that ran out of time without resolving — the failure the Godot probes
exist to catch.

- **Driven (`-HISoak -HIAuto`)**: the autopilot turns onto the nearest standing intruder,
  closes, fires and shoves. Measured: **WON in 5.0 s, three intruders down, 17 shots.** This
  says the whole loop works — movement, aiming, firing, hit resolution, death, the win
  condition — and it says **nothing about balance**, because the autopilot is an aimbot.
- **Passive (`-HISoak -HINoAuto`)**: the homeowner stands still. Measured: **LOST in 6.6 s,
  0 shots fired, all three intruders alive, homeowner at 0 HP.** Three men shoot an idle
  homeowner dead through his own front door in under seven seconds. That is a real
  measurement and a real design question; it is reported, not patched, because "port it,
  don't rewrite the design" is the standing instruction. The Godot project already recorded
  the same phenomenon in the opposite direction — `probe_heist.gd` withholds the intruder's
  damage for exactly this reason, or every scenario ends with the homeowner dead in ~22-32 s.
- **Heist probe (`-HINoAuto -HINoThiefDamage`)**: withholds only the wound, so the trace, the
  tracer and the hit roll all still run. This is the Unreal answer to `probe_heist.gd`: it
  asks whether a thief can FIND, GRAB and DELIVER loot, which is unobservable otherwise
  because the round is over before anyone reaches the house. The report warns loudly that it
  says nothing about lethality.

## Layout

| Path | Contents |
|---|---|
| `Source/HISim/` | the ported simulation (`HISim.h`/`HISim.cpp`) and its automation tests (`HISimTest.cpp`) |
| `Source/HIUnreal/` | the game module: game mode, player, intruder, house, loot, marks, HUD, animation, palette |
| `Source/*.Target.cs` | editor and game build targets |
| `Config/` | `DefaultEngine.ini`, `DefaultInput.ini` |
| `tools/fetch-engine-assets.sh` | copies the engine's own art into `Content/` |

`HISim` is the game model, so it is a `Runtime` module with its tests guarded by
`#if WITH_DEV_AUTOMATION_TESTS` (zero in Shipping).

## Porting hazards — read before editing

### Unreal's macros and the arguments that lie

- **`I`, `J` and `PI` are macros.** `PI` is `UnrealMathUtility.h`'s constant, so a variable
  named `PI` expands into `3.14159f` and the errors read `expected unqualified-id` /
  `invalid operands to binary expression ('float' and 'TVector2<double>')`. Use `Idx`/`Jdx`
  for indices and `PosI`/`PJ` for points.
- **A module absent from `HIUnreal.uproject`'s `Modules` array never LOADS**, so its tests
  never register. Symptom: `No automation tests matched 'HI.Sim'` against thousands of
  available tests.
- **`tar x` refuses to overwrite** — use `rsync` or `tar xzmf`.
- **Coordinate convention.** `FVector2D.X` is world X and `.Y` is world **Z**.
- **`FMath` has no `MoveToward`**, in either spelling.
- **`FMath::FindDeltaAngleRadians(A1, A2)` returns `A2 - A1`**, the delta *from* A1 *to* A2.
  To wrap an angle into range pass `0.0f` **first**.
- **`FParse::Value` treats a COMMA as a separator.** Measured 2026-10-09:
  `-HIShotPos=0,16` came back as `"0"`, the parse was rejected as malformed, and the capture
  was silently taken from the spawn point — a perfectly plausible screenshot that was of the
  wrong place. Scripted arguments are therefore read by hand in `RawCommandValue`.
- **`UCanvas::DrawTile`'s last parameter is an `EBlendMode`, not a colour.** Passing a
  `FLinearColor` is a compile error (the good kind). Coloured rectangles want
  `AHUD::DrawRect(Colour, X, Y, W, H)`.
- **`CreateDefaultSubobject` is protected and only valid inside a constructor.** A free
  helper that builds components cannot call it; it has to be a member function invoked from
  the constructor.
- **Proving an assertion is live.** `TestTrue` only logs on failure, and UE stores `TEXT()`
  literals as UTF-16 on Linux, so `grep -a "text" libUnrealEditor-HISim.so` returns 0 and
  looks exactly like the test was never compiled in. Use
  `strings -e l .../libUnrealEditor-HISim.so | grep -F "assertion text"`.

### The engine has to be built for the game to run at all

- **`ShaderCompileWorker` is a SEPARATE build target and the editor build does not make it.**
  Without it `UnrealEditor -game` dies during startup with
  `FUnixPlatformProcess::CreateProc: File does not exist .../ShaderCompileWorker` and
  `FUnixPlatformMisc::RequestExit(1)`. This is why the project had "never been launched": the
  binary was missing, not the code. Build it once:

  ```sh
  ssh ubuntu-dev '/home/richard/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh \
    ShaderCompileWorker Linux Development'   # ~8 minutes, 196 actions
  ```

- **First-run shader compilation is the real cost, and TSR dominates it.** Measured: a first
  run blocked for **over 25 minutes without reaching a frame**, every line a Temporal Super
  Resolution permutation (`FTSRRejectShadingCS`, 130+ of them, 30-70 s each, three workers).
  `Config/DefaultEngine.ini` sets `r.AntiAliasingMethod=2` (TAA) and
  `r.Shadow.Virtual.Enable=0` for that reason, with the cost stated at the setting. Set it
  back to `4` to restore TSR.
- **`-RenderOffscreen` works on this headless box.** Vulkan RHI initialises, finds the RTX
  5060 Ti, and renders — verified. No Xvfb and no `DISPLAY` are needed.
- **Screenshots are written to `Saved/Screenshots/<Platform>Editor/`** — note the `Editor`
  suffix even in `-game` mode. The path is logged at startup.

### Exposure, measured the hard way

The first captures pinned exposure (`r.DefaultFeature.AutoExposure=False`) so that one frame
would be comparable with the next. That was **reversed**, because the arithmetic does not
work: a sunlit lawn is on the order of 100,000 lux and a 2000-lumen lamp two metres away is
on the order of 20. No single exposure shows both, and pinning it blew the interior to pure
white and flattened the exterior to a mid-green. Auto-exposure stays on; motion blur and lens
flare stay off, which costs nothing.

## Status

`HI.` passes **33/33** on ubuntu-dev. **All 97 assertions** in the Godot *sim* spec are
represented; the port adds coverage the Godot project has never had on either side.

| Test | Covers |
|---|---|
| `HI.Sim.WorldData` | the wall/nav/loot constants against `sim.gd` |
| `HI.Sim.Geometry` | `seg_rect`, `rayCast`, `canSee`, thief FOV, `visibilityPolygon` |
| `HI.Sim.Separation` | N-body separation, the no-overlap contract, steer-around |
| `HI.Sim.SolidBodies` | neither body can walk through the other, both directions |
| `HI.Sim.Navigation` | routes, nearest-node snapping, degenerate routes |
| `HI.Sim.LosInvariant` | he never fires through a wall, and never leaves the world |
| `HI.Sim.Entities` | the player/thief model and the ammunition rules |
| `HI.Sim.Momentum` | acceleration builds speed; coasting decays it |
| `HI.Sim.ThiefAI` | a full heist against an idle homeowner, and re-route thrash |
| `HI.Sim.MovingHeist` | the heist still completes against a chasing/blocking homeowner |
| `HI.Sim.Shove` | the homeowner's shove: range, arc, cooldown, knocking loot loose |
| `HI.Sim.Hands` | the intruder's grab, and that he cannot grab while laden |
| `HI.Sim.Hit` | a wound staggers him and costs his sight picture; lethal damage |
| `HI.Sim.Fear` | gunfire builds fear until he breaks off, then he runs for the van |
| `HI.Sim.HandsFull` | he cannot fire while carrying loot — asserted as a difference |
| `HI.Sim.Aiming` | the aim settle before the first shot |
| `HI.Sim.Searching` | he must look for loot; an empty room finds nothing |
| `HI.Sim.PoliceClock` | one gunshot starts the clock; the sirens arrive and drive him off |
| `HI.Sim.MultipleIntruders` | 3-body separation contract, id attribution, unbanked push |
| `HI.Sim.Round` | the round model ported out of `game.gd`: momentum, the shot every intruder hears, and what actually ends a round |
| `HI.Sim.Elevation` | the height axis: a floor slab separates storeys, a low obstacle is seen over, and 2-D and height-aware sight still agree everywhere |
| `HI.Sim.View` | the aim and chase-camera maths |
| **`HI.Sim.Verbs`** | **reload, weapon switch and shove, driven through `FHIRound::Step`** |
| **`HI.Sim.GroupHeist`** | **a full heist through `StepIntruders`, the ONLY path the game calls** |
| **`HI.Sim.NonLethal`** | **the shout and the police call: they frighten, and the shout does NOT call the police** |
| **`HI.Unreal.Audio`** | **that the sound class, the reverb submix and the copied cue all resolve, plus the indoor/outdoor test** |
| **`HI.Unreal.Minimap`** | **the map's coordinate mapping, its framing, and the flip that would mirror it** |
| **`HI.Sim.StartView`** | **where a round starts looking (0.03 rad, not 0.24)** |
| `HI.Unreal.Sizing` | the metre/centimetre split, the body-rotation mapping |
| `HI.Unreal.Hud` | what the readout says |
| **`HI.Unreal.Reticle`** | **the reticle projection, with an exact expected value** |
| **`HI.Unreal.Materials`** | **that the palette's material really has its `Color` parameter, with a control** |
| **`HI.Unreal.ThiefAim`** | **the intruder's range falloff, guarding the 0.55 floor that made range meaningless** |

## Six bugs the test suite could not have caught

Everything was green — 24/24, all 97 Godot sim assertions represented — while the game had
no reload, no weapon switch, no shove, intruders who could not hurt anybody, and **intruders
who could not move**. Each was found by launching the thing, and each is now pinned by a
test that would have failed.

0. **THE INTRUDERS COULD NOT MOVE AT ALL, and the Godot original has the same bug.**
   `StepIntruders` — the only path the game calls — called `StepThief(..., bSeparate=false)`,
   and `StepThief` wrote `Thief.Pos` only *inside* its `if (bSeparate)` block, so every step
   was discarded. It then separated the frame-**start** positions and wrote those back,
   teleporting each body to where it began. Measured with a new probe: **0.000 m travelled in
   180 simulated seconds**, ending at exactly the spawn point, with a valid route and target.
   It survived every test because they all drive `StepThief` alone for one thief —
   `HI.Sim.ThiefAI` clears 3/3 in 137 s that way — and nothing ran a heist through the group
   path. Ported `sim.gd` has the identical shape (`thief["pos"] = tpos` inside
   `if separate:`, plus `separate_all(before, …)`), and a probe run against the **unmodified
   Godot source** reports the same 0.000 m. **The Godot original is fixed too**, with two new
   tests in its spec; both sides now clear the house in ~74 s. This is the bug that explains
   why nobody had watched three men come through the door.
1. **The game was ONE ROUND ONLY.** `ReadInput` is reached only while the round is *live*, and
   the Enter check lived inside it — so it had never executed. The outcome banner promised
   "[ENTER] to play again" and the key did nothing; the only way to play a second round was to
   restart the process. Now polled from the decided-round branch, and verified by
   `-HIRounds=3`, which plays three rounds through the same `RestartRound` the player's key
   calls.
2. **`bCrouching` and `bSprinting` were never written** after spawn, so the HUD stance read
   STANDING forever, and **`CrouchEyeHeight` was declared and referenced nowhere** — [C]
   slowed the homeowner down and left his viewpoint at standing height. The whole point of a
   crouch in a home-defence game is not being seen over the furniture.
3. **The player could not reload, switch weapons or shove.** `Sim::Shove`, `LoadMagazine`
   and the per-weapon `mags` map were ported *and tested* — as functions. `FHIRound::Step`
   had no input path to any of them, so the shotgun was unreachable and a dry pistol stayed
   dry forever. Fixed in `FHIRound::Step`, asserted through `Step` by `HI.Sim.Verbs`, with
   one weapon keeping its own magazine across a swap.
4. **Twelve of the sim's fifteen event types were dropped.** Only `playerShot` and
   `thiefDown` were handled. The sim *deliberately* leaves the intruder's hit/miss roll to
   the game layer, so with `thiefShot` unread **nothing damaged the homeowner and a round
   could not be lost by gunfire.** Now resolved in `ResolveThiefShot`, ported from `game.gd`:
   the round is a RAY traced against the world, so a wall stops it, with range falloff
   `clamp(1 - 0.035 r, 0.05, 1)`.
5. **A round started pitched 0.24 rad down.** Godot keeps two pitches and the camera reads
   the game layer's `reset()` value (0.03); the port merged the fields. The symptom was the
   first captured frame being the top of the homeowner's head, and it is nearly fourteen
   degrees of view. `HI.Sim.StartView` pins it.

## The fix list, item by item

Godot item numbers are from `../home-invasion-godot/ROADMAP-2.md`. **This is the Unreal
column of that list**; the Godot column is unchanged.

| # | Item | In this port |
|---|---|---|
| 2 | Characters are shiny mannequins | **DONE, differently.** The engine's own Mannequin, and **two different meshes** — Manny for the homeowner, Quinn for the intruders — so three men through a door are tellable apart. No MetaHuman: it needs an editor session and a download |
| 3 | Walls | **DONE from text.** Skirting and picture rail generated from the sim's own wall list; roof, eaves, plinth |
| 5 | Windows | **DONE.** Frames, sills, mullions and glazing on all four exterior walls. The glazing is still an OPAQUE panel — a translucent material is a `.uasset` — so it is deliberately dark, which is the only thing that reads as glass when it is lit from behind |
| 8 | Sockets, switches, thermostats, light fittings | **DONE.** Sockets, switches, a thermostat, a smoke alarm and a ceiling rose under each lamp |
| 10 / 11 | Valuables and props | **DONE.** TV, laptop and safe modelled from primitives (each with its BASE at floor level so one placement rule serves floor, hand and van), plus a kettle, toaster, bread bin, coat and hook, keys, a phone, a picture and a desk mat |
| 13 | Animation blending | **DONE, and NOT the way this port's own notes assumed.** "Blending is built in" is true of a normal Unreal project and false of one with no editor session: an AnimBlueprint is a `.uasset`. So it uses `EAnimationMode::AnimationSingleNode` on the engine's 2-D **blend space** over speed and direction — a genuine continuous cross-fade, with the metre-to-centimetre conversion named in the one place it happens |
| 14 | Foot IK | **HALF DONE.** The visible half — the feet skating — is fixed by matching the clip's play rate to the body's actual speed. Foot PLANTING on an edge needs an anim graph, and the engine's `CR_Mannequin_FootIK` is available and unused |
| 15 | Idle variation | **HALF DONE.** Every body's idle loop starts at a random phase, so three men no longer breathe in unison. Genuinely different IDLE POSES need more recorded clips; the engine ships one |
| 16 | Voice / barks | **HOOK ONLY.** The bark system exists and fires at the right moments (a man goes down, you shout), but the cue is a pitched sine and sounds like equipment, not a person. Voices need recordings |
| 17 | More non-lethal verbs | **Mostly done.** Shove, reload and weapon switch were already wired; now **[Q] shout** frightens every man who hears it without spending a round, and **[P] calls the police** without a gunshot — both tested. Barricading and lights are not started: a barricade is a new SOLID, and the sim's rectangle list is the Godot spec and is index-aligned with its spans, so adding one is a world-data change rather than a verb |
| 18 | The simulation is 2-D | **DONE** — `FHISolidSpan`, `LosBlocked3D` and a `Z` on both entities, with `HI.Sim.Elevation` as the regression harness. The spans are deliberately uniform at a full storey: the Godot source carries no furniture heights, and inventing them is a design decision, not a porting one |
| 19 | Blood / wounds | **PARTLY.** Pooled bullet holes where pellets land, a blood mark where a man falls, a flash on a hit and a red edge when the homeowner is hit. Nothing ON the person |
| 20 | Audio buses | **DONE for the buses, placeholder content.** Every cue is routed through the engine's `SFX` sound class rather than Master, and carries a submix send to `MasterReverbSubmixDefault` — whose effect chain was inspected before it was relied on — set from whether the cue is inside the house, so a shot in the kitchen rings and a shot on the lawn does not. The gunshots are the engine's template cue; impacts and barks are that cue and a sine at shifted pitch. **Foley needs recordings** |
| 21 | Reflections / GI | **DONE.** Lumen for GI and reflections, virtual shadow maps off, distance fields on, stated in `DefaultEngine.ini` |

## What is still open, and it is not a green tick

- **The intruders are still lethal, but no longer absurdly so.** The 6.6 s figure above was
  measured while the intruders could not move — they stood at the van with a clear line
  through the front door and shot an idle homeowner at leisure. **Re-measured after the fix:
  74.2 s**, ending with all three valuables delivered and the homeowner dead. That is a much
  more reasonable "if you do nothing, you lose", and it is reported rather than retuned
  because the numbers are the spec's.
- **The sound is placeholder.** Gunshots are the engine's template cue and everything else is
  that cue or a sine tone at a shifted pitch. The mixing and the timing are real; the foley is
  not, and barks in particular sound like equipment rather than men.
- **The van cannot be walked into, because it cannot be walked INTO.** The sim's world data
  does not make the van solid — it is scenery in both projects — so a body driven by the sim
  passes through it. This cannot be fixed from the actor layer: the player's movement is
  sim-driven and ignores physics entirely, so adding collision to the van would stop nothing.
  The fix is to add the van rectangle to the sim's solids, which changes AI routing and the
  Godot spec's world-data assertions. Reported rather than done.
- **Crouching does not make you harder to SEE**, only lower and slower. Every solid's vertical
  span is a full storey by design (see item 18), so no furniture is short enough to hide
  behind. That is the same open decision, and the same one a person has to make.
- **Nothing has been played by a person.** Everything above is compile, test, and captured
  frames looked at by an agent. No human has held the controls.
- **Mouse-look sensitivity is flagged TUNING** in `HIGameMode.h` and needs eyes.
- The house's door is an opening, not a leaf; the van's glazing is only on its flanks and
  windscreen; walls have no texture of any kind. All three need art, not code.
