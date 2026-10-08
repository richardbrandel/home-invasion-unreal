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

## Status

The sim core builds and `HI.Sim` passes 4/4 (`WorldData`, `Geometry`, `Separation`,
`Navigation`). That is a small fraction of the ~100-assertion specification: combat,
theft, the AI state machine, the clock, the shove and search behaviour are all still
Godot-only.
