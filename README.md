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

**The repository of record is this directory, on the Mac, and it is public:**
<https://github.com/richardbrandel/home-invasion-unreal>. `ubuntu-dev` holds a *build tree*
only — it is rsynced from here and has no commits of its own, so there is nothing to set up
on the Linux box and no reason to work there beyond compiling. Up to 2026-10-09 the Mac copy
was the only copy, because this repo had no remote at all.

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

## Assets this project GENERATES — the .uasset wall is gone

`tools/make-assets.py`, driven by `tools/make-assets.sh`, creates materials headlessly.
Their output is also in `Content/`, and also gitignored, for the same reason: it is derived
state and the script is the source of truth.

```sh
ssh ubuntu-dev '/data/projects/HIUnreal/tools/make-assets.sh'
```

**This section replaces a sentence that was in this file for the life of the port.** It said
a material *"is a `.uasset`, so a port that has no editor session cannot make one"* — and
every visual compromise in the project descends from that: every surface is
`BasicShapeMaterial` with a flat colour, the windows are opaque, and the characters carry the
engine's own materials because a skin or hair material was unmakeable.

Measured 2026-10-09: it is false. `PythonScriptPlugin` ships with the engine, and

```sh
UnrealEditor-Cmd <uproject> -unattended -NullRHI -ExecutePythonScript=...
```

creates a material, authors its expressions, connects them to its properties, sets its
shading model and blend mode, and **saves it to disk** — with no display and no human. The
no-Blueprints rule still holds (nothing is hand-authored in a GUI), but *"no editor session"*
no longer implies *"no assets"*. Two traps found on the way, both recorded in the porting
hazards below: a generated material needs its **usage flags** set by hand or the engine
swaps in the checkerboard Default Material, and **Unlit reads its colour from `MP_EMISSIVE_COLOR`,
not `MP_BASE_COLOR`**.

Current output: `M_HIGlass`, translucent, on the windows and the van's glazing.

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
| `-HIShotAfter=SEC` | how much round time first (default 1.5). Ignored when `-HIShotSeries` is given |
| `-HIShotSeries=t1,t2,...` | **capture at each of these SIM seconds, in ONE run** — `NAME_01.png`, `NAME_02.png`, … Then quit. See `tools/capture.sh` and bug 16 |
| `-HIShotPos=X,Y` | stand the homeowner at a sim coordinate. Also turns the autopilot OFF, so a posed frame stays posed |
| `-HIShotYaw=DEG`, `-HIShotPitch=DEG` | aim it |
| `-HIShotNoUI` | capture without the HUD |
| `-HICamBack=M` `-HICamUp=M` `-HICamSide=M` | move the chase camera, in metres. `-HICamSide=0.42` restores the Godot over-the-shoulder framing |
| `-HIWeapon=shotgun` | start the round holding it, through the same `SwitchWeapon` the [2] key uses -- a capture cannot press a key |
| `-HIWeaponAt=t:key,t:key` | **switch weapons at chosen SIM seconds inside ONE run** — `-HIWeaponAt=1.5:shotgun,3.5:pistol` covers both guns in one launch instead of two. Goes through the same `SwitchWeapon` the keys use |
| `-HIGunLoc=X,Y,Z` | nudge the weapon in the hand, in cm and in the **bone's** frame. See bug 11 |
| `-HIGunRot=P,Y,R` | turn the weapon in the hand |
| `-HIGunScale=S` | scale it -- `-HIGunScale=3` is how "is this even rendering" was settled in one frame |
| `-HIAuto` | drive the homeowner from the autopilot |
| `-HINoAuto` | force the autopilot off |
| `-HIProbe` | once a second (and on every weapon swap), log what the camera, the body and the gun are actually doing: barrel-vs-aim angle, gun roll, distances, visibility. See bug 15 |
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

## How to actually PLAY it

Everything above is a capture harness, and a capture harness is not a game you can hold. To
play it, the binary needs a display and no `-RenderOffscreen`:

**Four ways in, and they are all the same script.** `tools/play.sh` works from EITHER machine:
on the Mac it forwards to `ubuntu-dev` over ssh, and on the build box it launches directly. Extra
Unreal arguments pass through, so `-HIThieves=1` gives a round with one intruder.

| Where you are | Do this |
|---|---|
| Mac | double-click **`Play Home Invasion.command`** in the project folder |
| Mac | or, in Terminal: `./tools/play.sh` |
| At the Linux box | double-click the **Home Invasion** icon on the desktop, or find it in Activities |
| At the Linux box | or, in a terminal: `/data/projects/HIUnreal/tools/play.sh` |

The Mac copy used to FAIL with *"play: no editor at /home/richard/UnrealEngine/..."* -- true, and
useless, because it never said the game lives on the other machine. Now it says so and forwards.
The `.command` holds its Terminal window open afterwards, because the first desktop launcher
opened a terminal, printed, and closed before anyone could read it.

### Two ways this machine will waste your evening, both measured

**1. A game launched from a bare ssh session cannot see the screen.** SDL3 reports *"could not
initialize SDL: No available video device"* and the engine exits before drawing anything, because
the session variables are missing. `tools/play.sh` sets them (`XDG_RUNTIME_DIR`,
`WAYLAND_DISPLAY`, `DISPLAY`, and the Xwayland cookie -- whose FILENAME CHANGES ON EVERY BOOT, so
the script discovers it). The same binary that runs perfectly with them fails without them.

**2. A driver update without a reboot leaves the GPU unusable, and it looks like a game bug.**
On 2026-10-12 the NVIDIA userspace libraries were upgraded to 615.71.09 while the loaded kernel
module stayed at 610.57.04. UnrealEditor then HUNG inside RHI initialisation -- the log stopped at
`LogRHI: Skipping SF_VULKAN_SM5...` and never created a game window -- and what is left on screen
is the **splash**, a small window in the middle of the display. Reported from play as *"it
minimizes into something that looks like a mouse or a phone, just a small icon"*, and it is not
the game minimising at all. `nvidia-smi` answers *"Driver/library version mismatch"*, and the fix
is a reboot. `tools/play.sh` checks this and says so instead of launching into a hang.

| Key | |
|---|---|
| `W A S D` | move (camera-relative) |
| mouse | turn |
| left button | fire |
| `1` / `2` | pistol / shotgun — **each keeps its own magazine** |
| `R` | reload (walk slower while you do it) |
| `F` | shove — the only way to take loot back off a man without killing him |
| `C` | crouch — lowers the eye (but see the open item: it does not yet hide you) |
| `Shift` | sprint |
| `Q` | shout — frightens them, and tells them where you are |
| `P` | call the police |
| `L` | lights — off makes them shoot worse, and you see worse too |
| `Enter` | play again, once the round is decided |

**The port has never been played by a person.** A packaged build is deliberately absent: the
standing instruction is to tell Richard it is ready and let him test, not to ship builds
unasked. Packaging for Windows or macOS from here would need the engine's cross-compilation
toolchain, which is a separate job from making the game work.

## Layout

| Path | Contents |
|---|---|
| `Source/HISim/` | the ported simulation (`HISim.h`/`HISim.cpp`) and its automation tests (`HISimTest.cpp`) |
| `Source/HIUnreal/` | the game module: game mode, player, intruder, house, loot, marks, HUD, animation, palette |
| `Source/*.Target.cs` | editor and game build targets |
| `Config/` | `DefaultEngine.ini`, `DefaultInput.ini` |
| `tools/fetch-engine-assets.sh` | copies the engine's own art into `Content/` |
| `tools/make-assets.py` | AUTHORs materials headlessly — see "Assets this project GENERATES" |
| `tools/import-assets.py` | IMPORTs source files (wav, png, gltf, fbx) headlessly |
| `tools/make-voices.sh` | renders the bark lines on the Mac's own speech synthesiser (macOS `say`, the OLD take) |
| `tools/make-voices-neural.sh` | renders the same nine lines with **Kokoro**, an offline neural TTS on this Mac -- **this is what the game ships** |
| `tools/play.sh` | **launch the game for a person**, with the session environment and a driver check. See the hazards below |
| `tools/capture.sh` | **several frames from ONE launch, and it always ends**: `tools/capture.sh walk 1,2,3,4,5`. See bug 16 |
| `tools/session-env.sh` | the session environment, the editor path and the one-editor rule, **sourced by both scripts above** so they cannot drift |
| `tools/iterate.sh` | **sync + build + test in ONE command**, with the build's own timing and a non-zero exit on failure. `--filter HI.Unreal.Weapons` for a focused check, `--no-test` to build only |
| `tools/contact-sheet.py` | tile a capture series into ONE image, so a whole sweep is judged in one look instead of sampled three frames at a time |
| `tools/tour.sh` | the scripted tour: eight known views, one command |
| `docs/PARITY.md` | **the port measured against the Godot original, divergence by divergence** |

`HISim` is the game model, so it is a `Runtime` module with its tests guarded by
`#if WITH_DEV_AUTOMATION_TESTS` (zero in Shipping).

**`docs/PARITY.md` is the work list, and the Godot project is the design document.** Where
this port differs from `../home-invasion-godot/`, the port is wrong unless a reason is
recorded. Read it before deciding anything is "good enough" — the visual and behavioural
answers are almost always already written in the other repository.

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
- **`FScreenshotRequest::RequestScreenshot(..., bShowUI=false)` does NOT hide the HUD.** It
  suppresses SLATE. The HUD is a *canvas* drawn into the viewport, so it survives — measured:
  eleven "no UI" captures came back with the readout, the minimap and the damage bars on
  them, and `UGameViewportClient::ProcessScreenShots` is the code that says why. A capture
  with no HUD has to skip `AHIHUD::DrawHUD` itself.
- **`FCanvasTriangleItem` has no constructor that takes no texture**, so a filled shape needs
  `GWhiteTexture` — which is `RENDERCORE_API` and lives in `GlobalRenderResources.h`. A game
  module does not link RenderCore by default, and the compile succeeds: the failure is
  `ld.lld: undefined symbol: GWhiteTexture` at link time. Add `RenderCore` to
  `PrivateDependencyModuleNames`. `AHUD::DrawRect` reaches for the same global, which is where
  to look when working out how.
- **A model's bounding box is not its drawn size.** KayKit's glTF files carry identity node
  transforms, so a mesh's AABB is its model-space AABB — and `house.gd` scales every one of
  them by `SCALE` (0.5) at placement. Measuring the raw AABB gives numbers that look like
  furniture and are all half again too big. The port does not need them anyway: the heights
  are in `sim.gd`'s `FURNITURE` literal as `h`, which is what `game.gd` draws with.
- **A GENERATED material needs its usage flags set, or the engine silently discards it.**
  Putting a script-authored material on an `InstancedStaticMeshComponent` produced this, as a
  **Warning** and not an Error, and the pane rendered as the engine's grey-and-black
  **checkerboard** — which is what a "missing material" looks like, and which looks exactly
  like a material that was never applied:
  `LogMaterial: Warning: Material ... missing usage flag InstancedStaticMeshes! Default
  Material will be used in game.` In an editor those flags are set automatically when the
  material is assigned; a headless generator gets no such pass. `tools/make-assets.py` sets
  them explicitly now, and discovers the enum member names rather than hard-coding them.
- **On an Unlit material the colour goes in `MP_EMISSIVE_COLOR`, not `MP_BASE_COLOR`.** Both
  compile, and the wrong one is a black surface.
- **A lit translucent material dithers in a single offscreen frame.** The first working
  `M_HIGlass` was `DEFAULT_LIT` + `BLEND_TRANSLUCENT` and captured as a checkerboard, because
  Lumen's translucent lighting resolves over several frames of temporal accumulation and
  `-HIShot` renders one. It is `MSM_UNLIT` now — a plain alpha blend with nothing to
  accumulate — which is also what a window pane wants. Worth re-testing as a lit material if
  the game is ever captured as a running sequence rather than frame by frame.
- **A local named `Path` shadows `FHIMaterials::Path`** (the driveway colour) and the build
  fails on `-Wshadow` + `-Werror`. The same class as the `PI`/`I`/`J` macros: a short,
  ordinary-looking name that already means something in this codebase.

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

`HI.` passes **40/40** on ubuntu-dev. **All 97 assertions** in the Godot *sim* spec are
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
| **`HI.Unreal.Furniture`** | **the furniture heights against `sim.gd`'s own `h` field, and that a sofa is not a column** |
| **`HI.Unreal.WallFittings`** | **that every socket, switch and thermostat is flush against a wall -- with a negative control** |
| **`HI.Unreal.Wounds`** | **blood on the man: the wound count against health, five distinct bones, and a wound's SIZE in metres -- the assertion that used to be written in centimetres and so blessed a 7 m box** |
| **`HI.Unreal.Weapons`** | **both weapon meshes load, their materials are OPAQUE (a masked instance with a missing texture renders as nothing), the character mesh carries the `hand_r` grip bone, and the three pistol-pose clips resolve** |
| **`HI.Unreal.DeathFall`** | **everything a corpse needs in order to reach the floor: the clip is a playable AnimSequence, the mesh carries a physics asset, the lawn/floor/walls block `ECC_PhysicsBody` -- and bullets still pass through the floor while the walls still stop them** |

## Bugs the test suite could not have caught

Everything was green — 24/24, all 97 Godot sim assertions represented — while the game had
no reload, no weapon switch, no shove, intruders who could not hurt anybody, and **intruders
who could not move**. Each was found by launching the thing, and each is now pinned by a
test that would have failed.

The **last three entries** were found by an adversarial pass over captures on 2026-10-09,
after the port was already building with everything green. The method was to stand the camera
at known sim coordinates, look at the frame, and ask what was wrong with it rather than
whether the tests passed. None of the three is a behaviour the suite asserts, so none of them
was reachable from it.

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
6. **EVERY PIECE OF FURNITURE WAS A FLOOR-TO-CEILING COLUMN — and the claim that excused it
   was wrong.** The port's furniture table had dropped the `h` field, so the builder had no
   height to draw with and reached for `SolidSpans` instead, which is uniform at a full storey
   *because it models sight*. A couch, a bed, a rug and a fridge all rendered as columns from
   floor to ceiling, and the house read as a maze of blocks. `sim.gd` carries the height on
   every row and `game.gd` draws with it — `_add_box(body, f["rect"], f["h"])` — so this was a
   dropped field, not a missing design decision. The claim on record ("the Godot source
   carries no furniture heights, and inventing them is a design decision") was **factually
   wrong** and is corrected below. Ported, and `HI.Unreal.Furniture` asserts all nineteen
   against the literal. Two of the nineteen rugs were not drawn at all, because `Solids()`
   excludes flat things; they are now drawn from `FurnitureRects`.
7. **The front door had no head, so it was a 3 m letterbox to the ceiling.** `Walls()` splits
   the south wall either side of `DoorGap` — a horizontal split, because the sim's wall data
   has one vertical span — and the port never filled the opening above the door. Godot does,
   in `build_exterior`, and says why: *"the doorway is a hole all the way to the ceiling,
   which a 3 m wall makes glaring."* It was the first thing visible, because the round starts
   facing it. Ported as a decoration (`DoorHeadInstances`), not as a wall, because in the
   original it is a plain `MeshInstance3D` with no collision — making it solid would block
   shots the original lets through.
8. **The light switch by the front door hung in the doorway, and five more fittings floated.**
   The switch sat at x = -0.60, which is inside `DoorGap` (x -1..1), so it had no wall behind
   it and read from the spawn as a small pale box floating over the lawn. Two sockets were
   1.1-1.4 m off any wall and four switches up to 0.5 m off, because all eight were hand-typed
   coordinate pairs. They are now data in `AHIHouse::WallFittings()` and
   `HI.Unreal.WallFittings` asserts each is within a centimetre of a wall face — **with a
   negative control**, because a check that cannot fail is not a check. The coat by the door
   had the same fault and is moved clear of the opening.

9. **EVERY WOUND WAS A SEVEN-METRE RED BOX, AND THE ASSERTION THAT GUARDED IT ASSERTED THE
   BUG.** Reported from play, 2026-10-12: *"when I fire my gun a giant dark red rectangular box
   appears. It kind of fills up the room."* `AHIIntruder::WoundPlacement` returned
   `FVector(Spot.Size, Spot.Size, 2.0f)` -- centimetres, straight out of the wound table -- and
   the caller handed it to `SetRelativeScale3D`, where **the unit is metres**, because the
   engine's cube is one metre across at unit scale. A 7 cm wound therefore rendered as a
   **7 m x 7 m x 2 m slab**, dark red. It appeared on the **first hit** and not before, because
   a full-health intruder shows no wounds at all, which is what made it look like a firing bug.
   The asymmetry is the whole trap: `SetRelativeLocation` on the same component *is*
   centimetre-denominated, so the offset above it is correctly in centimetres and only the
   scale was wrong.
   It survived a green suite because the assertion read
   `TEXT("wound %d is a sane size (%.1f cm)")` and then checked `1.0 < Size.X < 20.0` -- **the
   same numbers, in the wrong unit** -- so it accepted 7.0 with a clear conscience. An
   assertion in the wrong unit is worse than a weak one: it actively confirms the bug. It now
   asserts `0.01 < Size.X < 0.20` and labels the value in metres, and **the check was checked**:
   with the `/ HI_METERS_TO_UU` removed it fails, at
   `wound 0 is a sane size (7.000 m across, not 7.0)`, and 38/38 with it in place. Every other
   scale assignment in the project was swept for the same fault -- `HILootProp` and `HIMarks`
   both pass metres and `StampLine` already divides -- so the wound was the only one.

10. **THE DEAD DID NOT FALL. THEY HUNCHED, THEN THEY FROZE -- and it was three faults, not
   one.** Reported from play 2026-10-12: *"when the intruders get shot, they slouch over. They
   should fall to the ground."* Every part of that is a measurement now.

   (a) **The clip is a flinch, not a fall.** All six `MM_Death_*` sequences -- the only deaths
   the engine ships -- are about 1.1 s long and end with the man still upright. Measured in a
   driven soak: across `MM_Death_Back_01` the pelvis travels **96.6 cm -> 87.5 cm and then
   holds**. There is no full-collapse death clip anywhere in the mannequin content; all 97
   sequences under `Anims/` were listed to establish that. So the clip is used for what it is
   good at -- the moment of being hit -- and **the fall is physics**: `AHIIntruder::GoLimp`
   hands the body to the simulation 0.85 s in, which is inside every clip's length, so the two
   read as one motion. `SKM_Quinn_Simple` already carries `PA_Mannequin`, so no new asset was
   needed for this.

   (b) **There was nothing to land on.** The house blocks ONLY the Visibility channel
   (`SetCollisionResponseToAllChannels(ECR_Ignore)`, so that a picture frame can never stop a
   bullet) and the lawn and interior floor are decoration with collision switched off entirely.
   So the corpse fell through the world: measured, component Z **0 -> -1330 cm in 1.5 s and
   still accelerating**. Walls, furniture, lawn and floor now also block `ECC_PhysicsBody` --
   a channel nothing else in this project uses, which is why the fix costs the no-collision
   invariant exactly nothing. The test asserts that both ways: physics is stopped, and bullets
   still are not.

   (c) **The handover could not be on the frame loop.** Once the round is decided the game mode
   stops calling `SyncFromSim` at all -- *"a decided round is inert at the sim level too"* -- so
   a handover that counted frames would never fire for the **last man killed, who is precisely
   the one the player is watching.** It is a world timer instead.

   Verified end-to-end in the running game: all three corpses `sim=1`, pelvis at **16.4 cm**,
   against 96.6 cm standing.

   **And the first hypothesis, kept because it is the more useful half of the story.** The play
   RATE looked like the culprit -- single-node playback inherits the rate stride matching leaves
   behind, clamped to a 0.35 floor, so a man shot while standing would fall at a third of speed
   -- and a test was written for it. **The negative control killed it**: with the "fix" removed
   the test still passed, because `UAnimSingleNodeInstance::SetAnimation` already resets both
   the rate and the position. A fix that cannot be shown to matter is not a fix, and that test
   is gone rather than kept as decoration.

11. **THE PISTOLS WERE IN THE MEN'S HANDS AND NOBODY COULD SEE THEM.** Reported from play
   2026-10-12: *"I don't see the pistols."* Everything about the attachment was correct -- the
   `hand_r` socket resolved, `SM_Pistol` loaded, the component was visible, and its world
   position was **exactly** the hand bone's, to the centimetre. And there was no pistol on
   screen at any range, for anybody. Two faults stacked.

   (a) **THE POSE WAS THE UNARMED ONE, and that is the half that matters.** The engine's
   mannequin content contains exactly **one blend space**, `BS_Idle_Walk_Run`, and it is
   unarmed -- so every armed man walked with empty hands swinging, and a pistol attached to an
   empty hand is buried against the arm or the hip *wherever you put it*. There is no pistol
   blend space to switch to, so `DriveLocomotion` now picks the pistol-pose clip by hand:
   `MF_Pistol_Idle_ADS`, `_Walk_Fwd`, `_Jog_Fwd`. The `MF_` set because the `MM_` set has
   equip, fire, reload and dry-fire but no locomotion at all; both are authored on
   `SK_Mannequin`, which Quinn and Manny share. **The Godot original hit this same wall and
   answered it the same way** -- its own notes say the homeowner "now plays AIM, which holds
   it out".

   (b) **The grip was wrong even once the pose was right.** `SM_Pistol` is 26 cm long and its
   **pivot is at its centre**, so at zero offset it straddles the wrist, and the placement
   rotation was `FRotator(-90, 0, 0)` -- a **pitch**. Pitch rotates about Y, so the barrel
   stayed on the bone's Y, straight *across* the hand, and the gun lay along the forearm. It
   is now a **yaw** of -90, which takes the mesh's +Y (the barrel) onto the bone's +X (wrist to
   fingers), with a 6 cm offset along it to put the grip in the palm rather than the pivot at
   the joint.

   **How it was found, because the method is the reusable part.** Pose, axes and placement are
   all invisible to the test suite, so it took a measurement in the RUNNING game: a temporary
   log of the socket's world basis against the gun's, which showed `gunY == gripX` -- proving
   the rotation was doing exactly what was intended and that the *pose* was the problem, not
   the maths. And once the pose was suspected, scaling the pistol 3x and lifting it 30 cm
   sprang it into view instantly, clearing the mesh, the material and the attach in one frame.
   `-HIGunLoc=` / `-HIGunRot=` / `-HIGunScale=` exist because a socket offset is judged by
   LOOKING at it: rebuild-to-nudge was costing two minutes and a capture, four times over.
   `HI.Unreal.Weapons` asserts the four things that can break it silently -- both meshes load,
   their materials are OPAQUE, the `hand_r` bone is on the character mesh, and the three
   pistol-pose clips resolve. **And the long gun was checked in a frame too**, because it uses
   the same grip rule with a 78.6 cm mesh: `-HIWeapon=shotgun` starts a round holding it, and
   the rifle reads correctly in both hands. It is the dark one -- which is how the pistol's
   pale material was pinned down as a texture that is not reaching the shader.

12. **THE CROSSHAIR STOOD BESIDE THE DEFENDER INSTEAD OF IN FRONT OF HIM.** Reported from
   play 2026-10-12: *"in the middle of the screen there is a crosshair, but it is at the side of
   the defender; it should be in front."* Nothing was miscomputed -- the two halves were each
   doing what they were written to do, and they disagreed about where the middle of the screen
   is. The reticle is the AIM DIRECTION projected into the CAMERA's basis (`ReticleFraction`),
   and the shot is cast from the camera along its forward, so with the camera 0.42 m off to one
   side **the crosshair stayed at the centre and the MAN moved off it.** `FHISim::CamSide` is
   now **0**, which puts the camera on the defender's own line so he stands under his own
   crosshair. It is a deliberate divergence from `game.gd`'s 0.42, and the sim test now asserts
   the invariant that matters -- *no lateral offset* -- rather than the old one.
   The alternative was to move the RETICLE onto the defender's line, and it is wrong: the shot
   leaves the camera, so a reticle drawn anywhere else would lie about where the pellets go.
   The framing is tunable without a rebuild: `-HICamBack=`, `-HICamUp=`, `-HICamSide=`.

13. **THE PISTOL WAS HELD SIDEWAYS, SO THE GUN AND THE CROSSHAIR DISAGREED.** Reported from
   play: *"the crosshairs are pointing one way and the defender another."* Measured in the
   running game, at the aiming pose:

   ```
   camera forward yaw 90.0 | hand socket X 14.0 | socket Y 100.5 | socket Z 179.3
   ```

   The hand socket's **+Y is the pointing direction** and `SM_Pistol`'s barrel is its **+Y**, so
   the correct grip rotation is the **identity**. The code used a yaw of **-90**, which maps the
   barrel onto the socket's **+X** -- the sideways axis -- and that is exactly what a player
   sees: the pistol held out horizontally to the man's right while the crosshair points down the
   room. The barrel now tracks the aim to within the hand's natural angle (measured 99-106
   against a camera at 90; it was 60-90 out before, and in the standing aiming pose it is
   `barrel=(-0.06, 1.00, 0.00)` against a camera at `(0.00, 1.00, -0.03)`).
   `HI.Unreal.Weapons` also asserts the pistol is **longest along +Y**, because that is the
   assumption the grip rests on and another mesh would break it silently.

   **Then the OFFSET was wrong, and only a screenshot showed it.** The pistol is 26 cm long with
   its pivot at the centre, so the grip is 13 cm behind the pivot -- offsetting the pivot by 13 cm
   puts the grip at the WRIST and the whole gun out past the fingertips, floating in front of the
   fist. Reported as *"the defender has no pistol in his hand"*, which is a fair description of a
   pistol hovering 13 cm beyond his knuckles. The palm is about 11 cm along the socket from the
   wrist, so the answer is 13 - 11 = **2 cm**, chosen by looking at three offsets:
   `-HIGunLoc=0,13,0` floats it, `0,6,0` hangs it above the knuckles, `0,2,0` grips it.

   **The instrument was broken as well, and that cost most of the time.** `-HIGunRot=0,-90,0`
   parsed as `"0"`, because `FParse::Value` stops at the comma by default -- so four candidate
   rotations produced four *identical* measurements that looked like evidence about the rig.
   They were evidence about the parser. All three gun switches now pass
   `bShouldStopOnSeparator=false`.
   **This was the third broken instrument of the same day** -- a `strings` check that could never
   match the engine's wide-character literals, a window measurement taken of a hung splash
   screen, and now a command line that silently parsed a third of its argument. The rule that
   keeps earning its place: **when a sweep produces identical results, suspect the sweep.**

14. **W WALKED BACKWARDS, S WALKED FORWARDS, AND EVERY TEST AGREED IT WAS FINE.** Reported from
   play: *"switch the W and S button, right now they work counter intuitive."* The port's axis is
   **"+1 is forward"** -- the game layer builds `(W?1:0) - (S?1:0)`, and the sim's own comment
   says *"+1 forward is what `Step` already expects"* -- but the movement line still carried
   `game.gd`'s negation:

   ```cpp
   Wish = (Fwd * -Input.Axis.Y + Right * Input.Axis.X) ...
   ```

   Godot's `_move_axis` gives W `v.y -= 1`, **minus** one, and then negates it here. The port
   flipped the input to +1 and kept the negation, so two flips that used to cancel became a
   double negative. It is now `Fwd * Input.Axis.Y`.

   **The suite could not see it, and the reason is the useful part.** `HI.Sim.Momentum` drove
   `Axis = (0, 1)` labelled *"walk forward"* and asserted how FAR he moved, and that he reached
   full pace a second later -- and every one of those numbers is identical whichever way he
   walks. It now asserts **which way**: at yaw 0 a step on `(0, 1)` must increase Y and leave X
   alone. Negative-controlled: with the old negation restored it fails with
   `and he walks FORWARD, not backwards (dY=-0.0056, dX=0.0000)`.

15. **THE GUN WAS AIMED BY THE ARM, AND THE ARM WAS WALKING.** Reported from play 2026-10-12:
   *"The cross hairs still point to the side and sometimes there is a gun in the defender's hand
   and sometimes not ... Also, hitting the 1 and 2 buttons doesn't switch guns."* All three had
   survived the previous round, and **the previous round's instruments could not have caught
   them**, which is why this one is written down at this length.

   **The [1]/[2] keys are not broken, and this is now proven rather than argued.** The live
   play log carries

   ```
   LogStaticMesh: Waiting on static mesh /Game/Weapons/Rifle/Meshes/SM_Rifle.SM_Rifle being ready
   ```

   at 23:20:52, exactly when the keys were pressed, and `SM_Rifle` is the shotgun's stand-in.
   Nothing else in the port can put that mesh on the weapon component: `SetWeaponVisual` is
   driven from `Round.Player.Weapon`, and the only writers of that are `SwitchWeapon` (a key)
   and `-HIWeapon` (not passed on that launch). So the sim switched, the mesh swapped, and the
   HUD would have read `SHOTGUN 6/18`. **What the player could not see was the change**, because
   of the pose below -- a gun that stays pointing at the wall looks the same whichever one it is.

   **The measurement.** `-HIProbe` was added for this: it logs, once a second, the angle between
   the weapon's barrel and the aim, the gun's roll off the world's up, the body's and camera's
   forward, distances and the visibility flags. Same build, same player, only the pose differing:

   ```
   standing (MF_Pistol_Idle_ADS):  barrel  3.3 deg off the aim,  rolled  8.2 deg
   walking  (MF_Pistol_Walk_Fwd):  barrel 49.4 then 26.2 deg off, rolled 73.5 then 26.7 deg
   ```

   **The pistol-pose LOCOMOTION clips are walk cycles.** The arm swings because the man is
   walking, not because he is aiming, so the barrel wanders up to fifty degrees off the crosshair
   and the pistol rolls up to seventy-four degrees onto its side. That is both reports at once: a
   gun carried forty degrees off *is* the crosshair pointing one way and the gun another, and a
   pistol rolled seventy degrees and swung behind the forearm is what *"sometimes there is a gun
   in his hand and sometimes not"* looks like from over his shoulder. **The old instrument was a
   still frame, and every still was taken standing still**, where the pose is the one pose that
   happens to be right.

   **The fix is `AHIPlayerCharacter::WeaponAimRotation`, and it aims the WEAPON, not the arm.**
   The props hang off the hand socket, so the pose decides where the gun points; the correction
   therefore drives the weapon component's **world rotation** from the aim every frame, and only
   its rotation -- its position still comes from the hand, so the grip stays in the palm however
   the arm swings, and the arm is left alone to animate a walk. A static mesh's local axes are
   its component's axes, so it is the component's **+Y** that must be put on the aim (the barrel),
   not its +X; the roll is pinned separately, because a pistol on the aim but lying on its side is
   still wrong. `HI.Unreal.Weapons` now asserts both as a pure function -- the +Y lands on the aim
   to 1e-5 and the +Z keeps its up -- plus the straight-up degenerate case, which has no meaningful
   "upright" and would otherwise put a NaN into a component transform.

   **After the fix, the same walking pose reads `barrelVsAim=0.0deg`, `gunRollFromUp=0.5-2.0deg`.**

   The honest fix is an upper-body aim offset, and that needs an animation BLUEPRINT, which this
   port deliberately does not have. So the arm still swings wide of the aim while walking and the
   gun is corrected on top of it. It reads correctly from behind, which is the only view the game
   has, but it is a compensation and it should be deleted if an AnimBP ever arrives.

   **Two instrument notes worth keeping.** (1) `-HIShot` together with `-HIAuto` did not capture
   or quit in two runs of up to 75 s; `-HIShot` with `-HIShotPos` (autopilot off) captures and
   exits in about 40 s, so the probe was read from the log instead. (2) The probe's own first
   version would have been useless in the same way the still frames were: it logged the barrel
   against the **camera** forward, which is the aim, so it can distinguish "the gun is off the
   aim" -- but not "the gun is behind the man", which is the other half of the report.

16. **THE CAPTURE HARNESS COULD ONLY PHOTOGRAPH A ROUND THAT OUTLIVED ITS TIMER.** Not a game
   bug, and it is written down here because it is the reason bug 15 survived two rounds of
   "verified" fixes. The harness was as much a part of the defect as the code was.

   The gate was `RoundSeconds < ShotAfterSeconds → return`, and **`RoundSeconds` is SIM time that
   only advances while the round is LIVE**. So a capture scheduled later than the round's last
   second was not *late*, it was **UNREACHABLE**: the process sat in `-game` until something
   killed it. Measured twice, at 75 s and at 120 s, and it reads exactly like a hang because it
   is one.

   The consequence is worse than the wasted time. The autopilot clears the house in about five
   sim seconds, so **every capture that DID succeed was one where the round ran long** — and with
   the autopilot off and the homeowner standing still, the round ran long because he was being
   shot. The harness could photograph a man standing still and dying, and nothing else. Every
   still in bug 15's investigation was one of those, and the sweep that would have caught it
   needed a round that did *not* outlive its timer.

   The fix is in the gate: when the round is decided and the clock has stopped short of the
   target, it captures where the clock actually stopped and says what that cost —

   ```
   HI: round decided at 3.45 s, before the 4.00 s capture -- taking it at 3.45 s instead;
       1 further frame(s) unreachable
   ```

   — rather than waiting for a time that can never arrive. Skipping would be a lie about what was
   photographed; waiting is a hang. `tools/capture.sh` carries its own wall-clock ceiling on top,
   because a harness that *can* hang costs an afternoon and this one already had.

   **`-HIShotSeries=t1,t2,...` now takes many frames from ONE launch**, named `NAME_01.png` and
   so on, quitting only after the last. Measured: a five-view sweep of a *moving* player went
   from five launches (~200 s, and the moving ones hung) to **one 28 s run that produced four
   frames and reported the fifth as unreachable**. That is the speed the loop needed, and it is
   the difference between seeing a walk cycle and guessing at one.

   **Two corrections to the record, both measured rather than reasoned.** (1) This README's
   `~2 fps / a quarter of real speed` claim about offscreen rendering is **stale** and it had a
   whole design built on it: offscreen and windowed both advance the sim at about **1x realtime**
   (one sim second cost 1.83-1.90 s of wall clock in the first interval, 0.99-1.01 s in the
   second, in both modes). The reason to prefer windowed is not speed — it is that a windowed run
   **honours `-ResX=1600 -ResY=900` and writes a 1600x900 PNG, while `-RenderOffscreen` ignores it
   and writes 1280x720**, which quietly answers a different question from the one a 1600x900
   screen print asks. (2) `-HIShotSeries` was first written with `FParse::Value`'s default, which
   **stops at the first comma**: `-HIShotSeries=1,2,3,4,5` parsed as `1` and the run took one
   frame and reported success. That is the **third** time this project has been bitten by the same
   default — after `-HIGunRot=0,-90,0` and `-HIShotPos=0,16` — and it is now the reason
   `bShouldStopOnSeparator=false` is called out at every comma-parsing site.

   `tools/session-env.sh` holds the editor path, the session environment and the one-editor rule
   that `play.sh` and `capture.sh` both need, so the cookie discovery and the driver check exist
   once instead of twice.

   **Then the sweep itself got the same treatment, and the same class of bug twice more.**

   * `-HIWeaponAt=t:key,...` switches weapons at chosen SIM seconds inside one run, so a
     pistol+shotgun sweep is ONE launch instead of two and does not pay ~18 s of engine startup
     twice. Verified in one 5-frame series: `PISTOL 7/36` -> `SHOTGUN 5/18` -> `SHOTGUN 2/18` ->
     `PISTOL 4/36`, with the long gun visibly in hand in the middle frames.
   * `tools/capture.sh` now emits `NAME-sheet.png`, an image of the whole series, and
     `tools/iterate.sh` makes sync+build+test one command with one verdict.

   **It did not work the first time, and both failures are worth keeping because they are the
   same failure this file keeps recording — a signal that fired while the thing it described
   did NOT happen.**

   1. **The cue was written into the input struct, which is rebuilt every frame.** `ReadInput()`
      returns a fresh `FHIInput` each Tick, so a cue arriving on a frame where the accumulator did
      not reach a whole sim step was dropped — about one frame in six here. It was SILENT,
      because the cue's own log line (`-HIWeaponAt fired 'shotgun'`) had already printed. The fix
      is a latch: the cue sits in `PendingWeaponCue` until the step loop actually delivers it.
      **The first version reported success over a sweep that covered one weapon**, which is
      precisely the shape of bug 15 and of the `-HIGunRot` sweep before it.
   2. **Two directories accumulate frames, and the sheet read the wrong one.** A three-frame
      series reported four frames copied (the build box's screenshot directory still held
      `walk_04.png` from the run before), and then the sheet — globbing the *destination*
      directory — tiled that older run in as a fourth tile. Both are the stale-artifact failure
      that once made a fixed collision bug look unfixed. The box's directory is now cleared for
      the name before the run, and the sheet is built from the list of files actually copied
      rather than from a glob.
   3. And a keeper: a switch the sim REFUSES now logs `the weapon switch to 'x' was REFUSED`,
      because `FHIRound::SwitchWeapon` declines an empty key, a repeated key and an unknown
      weapon — all silently, which is right for a keypress and wrong for a scripted sweep that
      then reports success over frames it never covered.

Also fixed in the same pass, and smaller: **`-HIShotNoUI` did nothing.** It was read and
handed to `FScreenshotRequest::RequestScreenshot(bShowUI=...)`, and that flag suppresses
SLATE, not the canvas the HUD is drawn on — the engine's own `UGameViewportClient::
ProcessScreenShots` is what says so. Eleven "no UI" captures came back with the full readout
on them. And the minimap's sight fan was drawn as a **line to every vertex of the visibility
polygon**, which is not a fan: the rays are a fan's *edges*, so the map rendered as a
starburst of spokes and — because the polygon reaches 28 m and the panel shows 38 x 32 — the
spokes painted over the game outside the panel as well. It is now the filled triangle fan the
Godot original draws, clipped to the panel.

## The fix list, item by item

Godot item numbers are from `../home-invasion-godot/ROADMAP-2.md`. **This is the Unreal
column of that list**; the Godot column is unchanged.

| # | Item | In this port |
|---|---|---|
| 2 | Characters are shiny mannequins | **DONE, differently.** The engine's own Mannequin, and **two different meshes** — Manny for the homeowner, Quinn for the intruders — so three men through a door are tellable apart. No MetaHuman: it needs an editor session and a download |
| 3 | Walls | **DONE from text.** Skirting and picture rail generated from the sim's own wall list; roof, eaves, plinth |
| 5 | Windows | **DONE.** Frames, sills, mullions and glazing on all four exterior walls, and the glazing is now **real translucent glass** (`M_HIGlass`, generated) rather than the pale opaque panel this row used to describe. The pane is in its own component -- `AddWindow` put the pane, the frame, the mullion and the sill in one component with one material, so making the pane glass would have made the frame glass too |
| 8 | Sockets, switches, thermostats, light fittings | **DONE.** Sockets, switches, a thermostat, a smoke alarm and a ceiling rose under each lamp |
| 10 / 11 | Valuables and props | **DONE.** TV, laptop and safe modelled from primitives (each with its BASE at floor level so one placement rule serves floor, hand and van), plus a kettle, toaster, bread bin, coat and hook, keys, a phone, a picture and a desk mat |
| 13 | Animation blending | **DONE, and NOT the way this port's own notes assumed.** "Blending is built in" is true of a normal Unreal project and false of one with no editor session: an AnimBlueprint is a `.uasset`. So it uses `EAnimationMode::AnimationSingleNode` on the engine's 2-D **blend space** over speed and direction — a genuine continuous cross-fade, with the metre-to-centimetre conversion named in the one place it happens |
| 14 | Foot IK | **HALF DONE.** The visible half — the feet skating — is fixed by matching the clip's play rate to the body's actual speed. Foot PLANTING on an edge needs an anim graph, and the engine's `CR_Mannequin_FootIK` is available and unused |
| 15 | Idle variation | **HALF DONE.** Every body's idle loop starts at a random phase, so three men no longer breathe in unison. Genuinely different IDLE POSES need more recorded clips; the engine ships one |
| 16 | Voice / barks | **DONE, with no recordings.** The bark system fires at the right moments (a man goes down, you shout, he spots you, he is shoved, he flees) and all nine lines are real speech: **Kokoro**, an 82M-parameter neural TTS run offline on this Mac by `tools/make-voices-neural.sh`, resampled to 48 kHz mono. The item assumed voices needed a human in a booth; what it actually needed was a speech model that fits on a laptop. Two voices (`bm_george` for the intruders, `am_michael` for the homeowner) so the two sides are audibly different people. `HI.Unreal.Audio` asserts all nine resolve, because a missing line plays NOTHING and that looks exactly like a game with the volume down |
| 17 | More non-lethal verbs | **Done except barricading.** Shove, reload and weapon switch were already wired; **[Q] shout** frightens every man who hears it without spending a round; **[P] calls the police** without a gunshot; and **[L] kills the lights**, which cuts the intruders' accuracy to 55% — applied in the GAME layer's `ThiefHitChance`, where the port's own design rule puts the shot and not the decision. Barricading is not started: it is a new SOLID, and the sim's rectangle list is this repo's spec and is index-aligned with its spans, so adding one is a world-data change rather than a verb |
| 18 | The simulation is 2-D | **DONE** — `FHISolidSpan`, `LosBlocked3D` and a `Z` on both entities, with `HI.Sim.Elevation` as the regression harness. The spans are uniform at a full storey **on purpose, because they model SIGHT and Godot's own `los_blocked` is 2-D and ignores height** — so a sofa you can see the top of still blocks your view, in both projects. This paragraph used to justify the uniform spans by saying *"the Godot source carries no furniture heights"*. **That was wrong**: `sim.gd`'s `FURNITURE` literal carries `h` on every row and `game.gd` draws with it. The spans are the sight model and stay as they are; the *geometry* now draws at `h`, which is bug 6 above. The remaining mismatch — tall sight, short furniture — is the original's, not this port's |
| 19 | Blood / wounds | **DONE.** Pooled bullet holes where pellets land, a blood mark where a man falls, a flash on a hit, a red edge when the homeowner is hit -- and now **wounds on the BODY**: five blood marks parented to bones, appearing one per 20 HP lost, so the body says what the HUD number says. Parented to bones rather than to the mesh, because a component-space offset slides across the skin as he walks. `HI.Unreal.Wounds` asserts the count against health and that all five land on distinct bones. The marks are still flat cubes rather than decals, which is the same primitive limitation as everything else |
| 20 | Audio buses | **DONE for the buses, placeholder content.** Every cue is routed through the engine's `SFX` sound class rather than Master, and carries a submix send to `MasterReverbSubmixDefault` — whose effect chain was inspected before it was relied on — set from whether the cue is inside the house, so a shot in the kitchen rings and a shot on the lawn does not. The gunshots are the engine's template cue; impacts and barks are that cue and a sine at shifted pitch. **Foley needs recordings** |
| 21 | Reflections / GI | **DONE.** Lumen for GI and reflections, virtual shadow maps off, distance fields on, stated in `DefaultEngine.ini` |

## SOURCE ASSETS THIS PROJECT NEEDS

**Ask for source formats, never for `.uasset` files.** Since 2026-10-09 the project can both
AUTHOR and IMPORT assets headlessly (`tools/make-assets.py`, `tools/import-assets.py`), so a
`.wav`, a `.fbx`, a `.glb` or a `.png` is something a script turns into a game asset. A
`.uasset` is an engine binary that only an editor can make, and asking for one means asking
someone to sit in a GUI.

**This list was ten items. It is two.** The first pass asked for things that were already on
disk in the Godot repository, or that the engine already ships, or that this machine can
synthesise -- so before asking anyone for anything, check the three places below. Measured
2026-10-10: **28 source files imported, 0 failed**, all of them free.

### Already in hand

| Was asked for | Where it actually was | State |
|---|---|---|
| Gunshots (pistol, shotgun) | `../home-invasion-godot/assets/audio/gunshot_pistol.wav`, `gunshot_shotgun.wav` | **Imported** |
| Foley: door, footstep, impact, van idle | The same repository's `assets/audio/` -- `door.wav`, `footstep.wav`, `impact.wav`, `van_idle.wav` | **Imported** |
| Sirens | `assets/audio/siren.wav` | **Imported** |
| Intruder and homeowner voices | **This Mac, and the second answer was the right one.** `tools/make-voices.sh` renders nine lines with macOS `say` -- intelligible, and unmistakably a formant synthesiser. `tools/make-voices-neural.sh` renders the same nine with **Kokoro** through `mlx_audio`: free, offline, no API key, a few hundred MB of model. The old claim that "synthesised audio cannot do voices" was wrong twice over -- once for the OS synthesiser, and again for the neural one | **Generated and imported (Kokoro)** |
| Wall, floor and door textures | `assets/textures/` -- `ceiling_plaster`, `floor_tile`, `floor_wood`, `driveway`, `lawn`, `roof_shingle`, `bullet_hole`, `muzzle_flash`, `smoke_puff` | **Imported.** There is no wall texture in either project, but `ceiling_plaster` is plaster and serves |
| Furniture meshes | `assets/kaykit/furniture/*.gltf` -- **the exact KayKit models the Godot original places**, 40 of them | **Proven**: `couch.gltf` imported headlessly to `StaticMeshes/couch` with its material and texture. glTF needs no editor |
| A clothed character mesh | `assets/characters/*.fbx` -- eight rigged Mixamo characters | **Reachable**: `Female_Casual.fbx` imports as a SkeletalMesh. See the open item below |
| A shotgun mesh | Nowhere -- KayKit has no weapons and the engine ships only a pistol and a rifle | **Build it in code.** Every prop in this port is already composed from primitives, and `HILootProp` is the pattern. A shotgun from three boxes is free; only the last 10% needs an artist |

### The one thing still genuinely needed

**A shotgun mesh**, if the primitive one is not good enough. `.fbx` or `.glb`.

**Voice recordings came off this list on 2026-10-12.** It asked for a human in a booth; the
answer was `tools/make-voices-neural.sh` and Kokoro, which Richard listened to and approved
before the nine lines were re-rendered and imported. Real actors would still be better, and
they are no longer the difference between a game that talks and one that beeps.

### Open work items, not assets

- **Retargeting Mixamo animations.** `assets/mixamo/` has 28 clips across `player/` and
  `thief/` -- including `crouch_idle` and `look_around`, which are exactly the *idle variation*
  item 15 asks for. Measured: an animation-only FBX imports to **nothing** without a target
  skeleton, and setting `task.skeleton` on the import task makes the run stop part-way through
  with no error. So this needs an IK Retargeter or an Interchange user-defined pipeline, and it
  is a job rather than a file. **This is the highest-value open item**, because item 15 and
  half of item 14 are waiting behind it.
- **The Mixamo characters are on the Mixamo skeleton**, so the engine's own animations do not
  apply to them without the same retargeting work.

### How to add a source file

```sh
# put it under SourceAssets/ (audio/, textures/, models/, voice/), then:
ssh ubuntu-dev '/data/projects/HIUnreal/tools/import-assets.sh'
```

The importer walks the whole tree and puts each file in a folder named after its
subdirectory. `SourceAssets/` is gitignored -- the files are yours, and the point is that
receiving one needs no GUI.

## What is still open, and it is not a green tick
- **The intruders are still lethal, but no longer absurdly so.** The 6.6 s figure above was
  measured while the intruders could not move — they stood at the van with a clear line
  through the front door and shot an idle homeowner at leisure. **Re-measured after the fix:
  74.2 s**, ending with all three valuables delivered and the homeowner dead. That is a much
  more reasonable "if you do nothing, you lose", and it is reported rather than retuned
  because the numbers are the spec's.
- **The FOLEY and the VOICES are both real now.** This item has been wrong twice in two
  days, which is why it is kept rather than deleted. It first said the gunshots were the
  engine's template cue and everything else a sine at a shifted pitch; `FHIAudio::Imported`
  loads `/Game/HIImported/audio/` -- the Godot original's own gunshot, impact and siren wavs --
  and the template cue is gone. It then said the nine VOICE lines were still macOS `say`, which
  was true until 2026-10-12, when all nine were re-rendered with **Kokoro** and imported:
  `voice-samples/` holds the A/B that decided it, and Richard's verdict was that the new ones
  are "really good". `HI.Unreal.Audio` asserts all nine resolve. A real actor would still beat
  either, and that is now a polish item rather than a gap.
- **The van cannot be walked into, because it cannot be walked INTO.** The sim's world data
  does not make the van solid — it is scenery in both projects — so a body driven by the sim
  passes through it. This cannot be fixed from the actor layer: the player's movement is
  sim-driven and ignores physics entirely, so adding collision to the van would stop nothing.
  The fix is to add the van rectangle to the sim's solids, which changes AI routing and the
  Godot spec's world-data assertions. Reported rather than done.
- **The light switch is a weak lever against a passive player, measured.** Two passive soaks,
  lit and dark, over the same 74.2 s: **13 hits / 8 misses lit against 12 hits / 16 misses
  dark.** The 55 % accuracy cut works exactly as the unit test says (0.27 against 0.48 at
  14.7 m) and the intruders miss twice as often — but they keep firing, so an idle homeowner
  still takes twelve hits and still dies. It should matter much more to a player who moves
  and takes cover, and that is not measurable without one. Reported as measured rather than
  as designed.
- **Crouching does not make you harder to SEE**, only lower and slower. Every solid's vertical
  span is a full storey by design (see item 18), so no furniture is short enough to hide
  behind. That is the same open decision, and the same one a person has to make.
- **A person IS playing it, and this list was behind that.** ~~Nothing has been played by a
  person.~~ Corrected 2026-10-12: the Unreal port has been in Richard's hands, and bugs 11
  through 15 below are all *"reported from play"* — so the old line was false for as long as
  those bugs have existed. What is still true, and worth keeping: every change here is verified
  by compile, headless test and captured frames looked at by an **agent**, so "it builds and the
  captures look right" is the strongest claim the automation can make on its own. Feel
  judgements — camera distance in motion, the thief's aim delay, whether a walk reads with
  urgency — still need a person, and bug 15 is the standing proof of how far a still frame can
  mislead.
- ~~**A RUG LIES ON THE FRONT LAWN**~~ — **FIXED 2026-10-09, in both projects.** The
  living-room rug was `Rect2(-7.60, -8.60, 4.40, 1.80)` against a house of `Rect2(-8, -19, 16,
  11)`, so 1.2 m of it lay on the drive. It went unnoticed for the life of both projects
  because a rug is `flat`, so `Solids()` excludes it and nothing ever *drew* it; the port only
  found it by starting to draw rugs. The intended value was not a guess after all — `-10.60`
  puts it in front of the sofa, and that is what both projects now carry
  (`Source/HISim/HISim.cpp:927`, Godot commit `fd14d77`). `HI.Unreal.Furniture` asserts every
  furniture rect is inside the house, **with the old bad rectangle kept as a negative
  control**, so the bug cannot return quietly.
- **`sim.gd`'s `DECOR` list was never ported.** The original has a second, separate list of
  drawn-but-not-solid items — picture frames on specific walls, two cacti, two book sets, a
  stove, a pot and a table lamp — added precisely because a picture frame must never block a
  route. The port has its own hand-written props instead (kettle, toaster, bread bin, coat,
  keys, phone, a picture, a desk mat), so the house is dressed, but not with the original's
  objects or in the original's places.
- **The props share the wall's colour.** `PropInstances` is tinted `FHIMaterials::Counter`
  (0.72, 0.73, 0.71) and `Wall` is (0.74, 0.70, 0.62) — which is why the coat by the front
  door reads as a small pale cabinet rather than as a coat. The palette already has an unused
  `Sofa` colour. It is one component and one line, and it wants a person's eye rather than a
  measurement.
- **The pistols render LIGHT GREY rather than dark metal.** The material is opaque and lights
  correctly -- it is simply pale, and against the mannequins' own grey it reads as a toy. The
  imported texture does not show, and which of the four `T_Pistol_*` maps it wants is a
  question for an eye rather than a measurement. Cosmetic, and now that the guns are visible it
  is the next thing a player notices.
- **The damage flash is four hard-edged rectangles.** It is drawn as four edge bars so it does
  not hide what is shooting you, which is right, but they meet at the corners with no falloff,
  so at high alpha the frame reads as a picture frame rather than as being hurt. Cosmetic.
- **UNIDENTIFIED: a two-tone object stands against a wall in driven captures.** Reproducible
  with `-HIAuto -HIThieves=1 -HIShotAfter=0.9 -HIShotNoUI`: roughly 1.2 m wide and 2.06 m
  tall, dark red above green, sitting on the floor hard against a wall, with the skirting
  running unbroken in front of it. Its colours match `FHIMaterials::Blood` over
  `Grass`/`Van` and nothing else in the palette. It does **not** appear in posed captures at
  the same coordinates, so it is produced by the fight rather than by the house. It was *not*
  identified -- the session ran out of budget on it -- and it is recorded here rather than
  guessed at. Start from `HILootProp` and `AHIGameMode::FlashPoint`, which are the two things
  that appear only once shooting starts.
- **The captures show the engine's shader-preparation overlay.** "Preparing Shaders ... Sky
  Light waiting on Shaders for final capture" is drawn into the frame when a capture is
  requested before shaders finish, and it is visible in `shots/h_in.png`. It pollutes the
  picture, not the game. A longer `-HIShotAfter`, or suppressing the notification, would
  clear it.
- **Mouse-look sensitivity is flagged TUNING** in `HIGameMode.h` and needs eyes.
- The house's door is an opening, not a leaf; the van's glazing is only on its flanks and
  windscreen; walls have no texture of any kind. All three need art, not code.
