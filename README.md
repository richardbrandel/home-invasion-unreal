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

Sync source to the build tree before building:

```sh
rsync -a --exclude='Intermediate/' --exclude='Binaries/' --exclude='DerivedDataCache/' \
  --exclude='Saved/' --exclude='.vscode/' --exclude='Makefile' \
  ./ ubuntu-dev:/data/projects/HIUnreal/
```

## Build and test

```sh
# Build (incremental is ~30 s; a -Clean rebuild recompiles the shared PCH, many minutes)
ssh ubuntu-dev '/home/richard/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh \
  HIUnrealEditor Linux Development -project=/data/projects/HIUnreal/HIUnreal.uproject'

# Test. Exits 0 only when tests ran AND all passed — zero tests is a failure by design,
# because `-TestExit` hardcodes exit status 1 and the editor's exit code is not a verdict.
ssh ubuntu-dev '~/tools/ue-run-tests.sh /data/projects/HIUnreal/HIUnreal.uproject HI.Sim'
```

## Layout

| Path | Contents |
|---|---|
| `Source/HISim/` | the ported simulation (`HISim.h`/`HISim.cpp`) and its automation tests (`HISimTest.cpp`) |
| `Source/HIUnreal/` | the game module; currently a shell that depends on `HISim` |
| `Source/*.Target.cs` | editor and game build targets |
| `Config/` | `DefaultEngine.ini`, `DefaultInput.ini` |

`HISim` is the game model, so it is a `Runtime` module with its tests guarded by
`#if WITH_DEV_AUTOMATION_TESTS` (zero in Shipping).

## Porting hazards — read before editing

Unreal's headers define macros that collide with ordinary identifiers, and the failure
modes are misleading rather than obvious. Accumulated, measured, in
`../home-invasion-godot/docs/BUILD.md`:

- **`I`, `J` and `PI` are macros.** `PI` is `UnrealMathUtility.h`'s constant, so a
  variable named `PI` expands into `3.14159f` and the errors read
  `expected unqualified-id` / `invalid operands to binary expression ('float' and 'TVector2<double>')`.
  Use `Idx`/`Jdx` for indices and `PosI`/`PJ` for points.
- **A module absent from `HIUnreal.uproject`'s `Modules` array never LOADS**, so its
  tests never register. UBT still compiles and links it. Symptom:
  `No automation tests matched 'HI.Sim'` against thousands of available tests. Check
  `grep "InternalLoadLibrary: 'HISim'"` in the test log first.
- **`tar x` refuses to overwrite**, so a re-transferred fix can silently not apply and the
  rebuild reproduces the old error byte-for-byte. Use `rsync` or `tar xzmf`.
- **Coordinate convention.** `FVector2D.X` is world X and `.Y` is world **Z** — the sim is
  a top-down XZ plane, matching Godot's `Vector2`. Height is a renderer concept.
- **`FMath` has no `MoveToward`** — neither spelling (`MoveToward` nor `MoveTowards`) exists,
  so Godot's `move_toward` is written out by hand. Expect other small Godot math helpers to
  be missing rather than assume an engine equivalent.
- **`FMath::FindDeltaAngleRadians(A1, A2)` returns `A2 - A1`**, wrapped into `[-PI, PI]` — the
  delta *from* A1 *to* A2, not the subtraction the argument order suggests. Getting it
  backwards is silent: it compiles, runs, and looks plausible. Measured — `StepThief` had both
  calls reversed, so the thief turned *away* from his heading; that pinned the turn-alignment
  factor at its 0.55 floor and halved his walking pace, turning a 137 s heist into a timeout.
  To wrap an angle into range, pass `0.0f` **first**: `FindDeltaAngleRadians(0.0f, X)`. With the
  angle first it returns the *negation*, flipping a facing every frame.
  (Guard: where the result is wrapped in `Abs`, as in `ThiefSees`, the sign cancels and the
  argument order does not matter — which is exactly why one correct call next to one broken
  call is easy to miss.)
- **Proving a test assertion is actually live, not silently absent.** `TestTrue` only logs on
  *failure*, so a passing assertion leaves no trace in the run log. And UE stores `TEXT()`
  literals as **UTF-16** on Linux, so `grep -a "assertion text" libUnrealEditor-HISim.so`
  returns 0 and looks exactly like the test was never compiled in. Measured — that false
  negative cost a detour. Use:

  ```sh
  strings -e l /data/projects/HIUnreal/Binaries/Linux/libUnrealEditor-HISim.so | grep -F "assertion text"
  ```

  Prefer an assertion that can only hold for real output (e.g. `Poly.Num() > 20`) over one
  that is true by construction.

## Status

`HI.Sim` passes **21/21** on ubuntu-dev. **All 97 assertions** in the Godot *sim* spec are
represented; `HI.Sim.Round` and `HI.Sim.Elevation` add coverage the Godot project has never
had on either side.

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

## Elevation, and the one decision left in it

Fix-list item 18 said the sim was 2-D and that this blocked a multi-level house. The
mechanism is now in: `FHISolidSpan` gives every solid a vertical extent, `LosBlocked3D`
respects it, and the entities carry a `Z`.

**The spans are deliberately uniform at a full storey**, and that is the honest part. The
Godot source carries no furniture heights — only a `flat` flag — so inventing them would
silently change which obstacles the AI can see over, with no test able to say whether the
invented numbers are right. Giving a sofa its real height is a **design decision, not a
porting one**, and it is left open on purpose.

What that buys without inventing anything: a **floor slab** is new structure rather than a
re-description of old, so it carries its own span and blocks sight between storeys. That is
the bit that actually blocked a multi-level house, and it is tested — including that a slab
does *not* block two people on the same storey, because a ceiling that hid the room it
roofs would be worse than no ceiling.

`HI.Sim.Elevation` also carries the **regression harness for this work**: across every pair
of nav nodes, the 2-D test and the height-aware one at eye height must agree. When someone
gives furniture real heights, that is the assertion that will notice.

## The game layer, and where it stands

`FHISim::FHIRound` is the round model lifted out of `game.gd` (2211 lines of SceneTree node
with the camera, HUD, Mixamo loading and minimap attached, which is why it was never
testable). It owns the homeowner, the intruders, the loot and the rules that end a round.

**The split that matters:** `FHIRound` holds everything a trace cannot decide — movement
and momentum, ammunition and cooldown, the distance-scaled alert, and the win/lose rules.
It deliberately does **not** resolve hits: casting pellets at a body needs Unreal's
collision, so the actor layer does the trace and calls `DamageIntruder` with the result.

**Next, and not started:** `Source/HIUnreal/` is still an empty shell. It needs the player
pawn and controller (input, camera, movement driven by `FHIRound`), one actor per intruder
mirroring `FHIThief`, a game mode owning the `FHIRound` and ticking it, and the hit
resolution that calls `DamageIntruder`. None of that exists yet.

`HI.Sim.ThiefAI` reports its own timing, and it is worth watching: the port clears three
items in **137.0 s** — identical to the Godot original — with 0 re-routes. That agreement
is not decoration. It read 144.8 s until a pathfinding divergence was fixed, and the
timing is what exposed it.

## What is NOT yet true

Passing the sim spec is not the same as having the game. Everything here is the **model**:
there is no rendering, no input, no HUD and no house geometry. `Source/HIUnreal/` is still
an empty shell that depends on `HISim`. The port also carries the sim only — the Godot
project's other suite (`test_actor.gd`, 41 tests over animation and character loading) and
its 7 probes have no Unreal equivalent, and the fix-list work from `AGENTS.md` (the 3-D
house, animation blending, MetaHuman, lighting) has not started.
