# PARITY — the Unreal port against the Godot original

**The Godot project is the design document.** Not a reference, not inspiration: the answer.
`../home-invasion-godot/` is a game that was in good shape, and this port exists to be the
same game in Unreal. So the rule this file is built on is:

> Where the Unreal port differs from Godot, **the port is wrong** unless there is a recorded
> reason. "I preferred it" is not a reason.

The recorded reasons are the two deliberate port decisions — which live in
`../home-invasion-godot/AGENTS.md` ("Two port decisions, made deliberately — do not
re-litigate"), **not** in this repository's README — plus the fix-list table at
`README.md` "the fix list, item by item".

## How to read a row

| Tag | Meaning | What to do |
|---|---|---|
| `faithful` | Matches Godot | Nothing |
| `deliberate` | A recorded decision, named | Nothing, provided the record still exists |
| `mine` | The port invented something Godot already answered | Revert to Godot's answer |
| `missing` | Godot has it; the port has nothing | Port it |
| `unknown` | Not establishable by reading | The settling test is named |

Five areas were read end to end with `file:line` evidence on both sides: world geometry,
camera/input/HUD, the simulation, audio/effects/marks, and characters/animation/minimap.

## The headline

**The simulation is a faithful port and the presentation is not.**

- **47 of 47 shared sim constants are byte-identical**, machine-diffed, not eyeballed. All 19
  furniture rows, both weapon specifications with both magazines and reserves, the 9-node nav
  graph, the spawn fan and the world bounds are identical. The intruder AI's state set,
  transitions and the round-end conditions match exactly.
- Outside the sim, the port diverges almost everywhere: **3 of 16 animation clips** are
  represented, **3 of 7 imported sound files** are used, the furniture is cubes, there are no
  windows visible from inside, no door leaf, no driveway, no street, and the minimap is in the
  wrong corner with the wrong legend.

That split is the useful finding. The hard part — the simulation — is done and correct. What
is left is the part a person actually sees.

---

## THE WORK LIST, in order of what a player notices

### 1. Twelve of Godot's sixteen animation clips are not played

Godot's `actor.gd:16-34` defines `idle, walk, run, crouch_idle, crouch_walk, aim, shoot,
reload, death, carry_walk, carry_idle, pickup, look_around, push, shove_react, hit_react`.
The port loads one asset — `BS_Idle_Walk_Run` — in `AnimationSingleNode` mode, and there is
exactly **one** `PlayOneShot` call site in the whole project (`HIIntruder.cpp:225`, the death).

So idle/walk/run are represented through a blend space, death is played, and **twelve clips
have no equivalent**: no aiming stance, no firing animation, no reload, no crouch, no carry
pose, no flinch, no shove. **A firefight in this port has no firing in it.**

Root cause is recorded and is the highest-value open item: an animation-only FBX imports to
nothing without a target skeleton, so this needs an IK Retargeter. This is the single change
that would most improve how the game reads.

### 2. Three intruders are three identical men

Godot tints each body four ways — `THIEF_TINTS` (`game.gd:34-39`), applied per intruder at
`game.gd:543`. The port loads one mesh for all of them and never assigns a body material;
`FHIMaterials::PlayerSkin` and `IntruderSkin` are **defined and referenced nowhere**.

The README claims the two meshes mean "three men through a door are tellable apart" — but
Manny-versus-Quinn separates intruders from the homeowner, not from each other. This is
exactly the question that was asked from play, and the answer is currently no.

### 3. No window is visible from inside the house

Godot's window bays **are** the wall panels (`wall_window_closed` / `wall_window_open`), with
sill and head spanning the full wall thickness plus 6 cm, on the wall centre line
(`house.gd:443-454`). The port centres each pane at `WallCoord + Facing*0.15` with 0.08 m
thickness, so the glass sits **10-11 cm clear of the wall**, outside it. From indoors there are
no windows at all. Godot also has 14 bays (9 closed with trim and curtains, 5 open holes,
including four in the interior divider); the port has 8 glazed panels at invented positions.

### 4. Furniture is nineteen cubes

Godot maps 17 furniture kinds to named KayKit glTF models and places them grounded at each
rect centre (`house.gd:498-516, 566-583`): sofa→`couch`, bed→`bed_double_A`, fridge→`fridge_A`,
desk→`table_medium_long`. The port uses one `/Engine/BasicShapes/Cube` per rect with a single
`Wood` tint. **glTF import has since been proven to work headlessly in this project**, so this
is now a content job rather than a research one.

### 5. The front door is a hole

Godot hangs `door_A` open at −74° on the west edge of the gap (`house.gd:597-607`). The port
has an opening and a lintel and no leaf. `FHIMaterials::Door` is declared and unused.

### 6. There is no driveway and no path

Godot has a 9 × 26 m drive at (0, 0.03, 6) with the `driveway` texture and a 1.70 × 2.60 m
path at (0, 0.025, −6.80) (`house.gd:639-647, 657-664`). The port has neither, so the front
door opens onto bare lawn and the van stands on grass. `FHIMaterials::Path` is unused.

### 7. The minimap is in the wrong corner, and its legend is inverted

| | Godot | Port |
|---|---|---|
| Position | bottom-right, `(1334, 674)` in 1600×900 | **top-right** |
| View rect | `Rect2(-22, -24, 44, 40)` | 38 × 32 at a different origin |
| Furniture | drawn dim, then lit inside the fan | **not drawn at all** |
| Free loot | amber `(0.961, 0.690, 0.165)` | cyan `(0.35, 0.85, 0.95)` |
| Taken loot | grey-blue `(0.294, 0.333, 0.388)` | orange `(0.95, 0.55, 0.15)` — the **brightest thing on the map** |
| Delivered loot | not drawn (`minimap.gd:152-153`) | drawn grey |
| Intruder shown within | 30 m (`minimap.gd:164`) | 26 m — the intruder's own sight constant |
| Player marker | blue circle + white ring + 31.5 px facing wedge | white square + 9 px nose |
| Background | opaque dark teal | translucent black |

Four of Godot's ten colours carry over exactly. Faithful: the axis flip, the 3-frame polygon
cache, and the point-in-polygon test (affine-equivalent in sim space).

### 8. Nothing tells you which direction you were shot from

Godot places a mark beside the crosshair at the shooter's bearing, fading over 1.1 s
(`game.gd:1142-1146, 2167-2186`) — added because "being shot gave you nothing but a falling
number". The port has an invented red screen edge with no bearing in it.

### 9. Three of the seven imported sound files are never used

`footstep.wav`, `door.wav` and `van_idle.wav` were imported and **have no code reference
outside the README's import table**.

- Godot plays footsteps on four separate timers and volumes (`game.gd:1764-1765, 1786`) and its
  own source says why it matters: *"the intruder crossed the house in silence and the only
  warning was the minimap dot — which is the one thing a defending homeowner does NOT have."*
- `door.wav` fires when an intruder crosses `Sim.HOUSE.end.y` (`game.gd:1791-1803`) — the cue
  that the house is no longer empty.
- `van_idle.wav` loops at the van for the whole round (`game.gd:1697-1724`).

Two further audio divergences: the siren is a single 3 s one-shot where Godot loops it for
10 s, and the port plays an impact for a kill and for a wall hit where Godot is silent.

### 10. The remaining geometry, in Godot's own words

Nothing in this list is subtle; each one is a `grep` for a missing model.

- **The roof is two flat slabs.** Godot has 1.6 m of pitch, a ridge cap, fascia, gutter and
  8-step gables (`house.gd:260-332`).
- **No plinth, no foundation slab** — the wall grows straight out of the grass
  (`house.gd:628-637, 136-143`).
- **No cornice, no ceiling emission, no lampshades** (`house.gd:198-233, 246-255`).
- **The kitchen floor is not tiled.** Godot tiles it on a 2 m grid with `floor_tile`
  (`house.gd:145-164`).
- **The `DECOR` list was never ported** — 28 placed non-solid models: the armchair, two
  shelves, the stove, the oval rugs, two cacti, three book sets, four picture frames, a table
  lamp, a kitchen cabinet, a chair, a crate of carrots (`sim.gd:87-137`).
- **Nothing exists outside the house.** Godot has a street at z 17 — road, seven buildings, a
  traffic light, a fire hydrant, a dumpster, a bench, two bins — plus six bushes and three
  street lights round the house (`house.gd:678-685, 708-746`).
- **Walls sample `ceiling_plaster` at tiling 40.** Godot uses the recoloured KayKit atlas
  `wall_texture_clean.png` (`house.gd:356-364`).
- **The van is boxes.** Godot uses KayKit `car_stationwagon` at `VAN_SCALE 2.5`.
- **The shotgun is the engine's rifle** (`SM_Rifle`), where Godot builds a distinct shotgun
  from primitives with six materials (`actor.gd:456-480`) — and the README's own plan was
  "build it in code".

### 11. The HUD readout was substituted, not ported

Godot shows a **live** intruder state line — `running` / `hurting` / `hit` / `unhurt`, with a
hit count (`game.gd:2142-2159`). The port replaced it with a cumulative `INTRUDERS DOWN n / 3`.
Also: HP is `"HP  %d"` against Godot's bare integer; the weapon is upper-cased where Godot uses
the spec's own `"Pistol"`; magazine **capacity is dropped** from the ammo line; the police clock
is its own line where Godot appends it to the loot line; and the round banner is drawn at the
bottom of the screen instead of Godot's y=150 band, with no dark backdrop and reworded text —
including collapsing every non-death loss into one sentence where Godot distinguishes
`"He broke off and ran — %d of 3 with him."`.

### 12. The aim-cone mode is absent entirely

Godot keeps **both** schemes live behind `[T]` (`game.gd:824-832`) with `CONE_YAW 0.61`,
`CONE_PITCH 0.38`, `CONE_RECENTRE 3.2`, and the comment says why both stay live: *"this is an
experiment, and Richard asked to fall back to plain first person if he does not like the cone."*
The port has no `EKeys::T` at all. Fixed mode is correctly the default; the toggle is missing.

### 13. Smaller numeric divergences, each with Godot's value

| Port | Godot | Where |
|---|---|---|
| `CAM_FADE 0.35` | `0.42` | `HIPlayerCharacter.cpp:207` vs `game.gd:110` |
| Recoil returns 100% of the kick | **65%** (`0.65` multiplier), leaving a residual | `HIGameMode.cpp:823-827` vs `game.gd:1456` |
| Reticle Y from the horizontal half-FOV | the **vertical** one, width derived from aspect | `HIHUD.cpp:499` vs `game.gd:2092-2096` |
| Thief shove does not kick the view | `pitch += 0.10`, `recoil_recover += 0.06` | `HIGameMode.cpp:1393-1397` vs `game.gd:1187-1188` |
| Camera pull-in: 0.12 m sphere at `Hit.Location` | ray plus a **0.14 m standoff** | `HIPlayerCharacter.cpp:174-178` vs `game.gd:1402-1407` |
| Mark pool 96, cleared each round | **48**, and Godot never clears them | `HIGameMode.cpp:62, 1118-1125` vs `game.gd:158` |
| Bullet hole 0.10 m, no shotgun case, flat dark tint | **0.16 m**, ×1.7 for a shotgun pellet, ×0.7 on flesh, `bullet_hole.png` | `HIGameMode.cpp:859-861, 73` vs `game.gd:2033, 2041-2043` |
| Muzzle flash 0.22 m, flat tint | **0.34 m**, `muzzle_flash.png`, additive, billboarded | `HIGameMode.cpp:985` vs `game.gd:1825-1837` |
| Tracer 20 mm square, two colours | **24 mm cylinder**, one emissive colour | `HIGameMode.cpp:881` vs `game.gd:1548-1555` |
| No footsteps, no casings, no muzzle smoke | four footstep timers, 24 casings with bounce, 10 smoke puffs | `game.gd:1764-1790, 1878-1929` |
| Loot floats at a fixed +0.42 m chest offset | rides the **hand bone** with per-label `CARRY_HANG` | `HILootProp.cpp:155-158` vs `game.gd:629-633, 730-734` |
| Loot is not emissive | `emission = col * 0.07`, so a valuable is findable in a dim corner | `HIMaterials.cpp:96` vs `game.gd:660-661` |
| Crouch moves the eye only | also squashes the model to 0.76 height | `HIPlayerCharacter.cpp:129` vs `game.gd:1260-1261` |
| No crouch clip, no aim clip, no shoot clip | all three | see item 1 |

### 14. Godot sim assertions with no Unreal equivalent

The port claims the same tests pass. **Four assertions are not represented**, and each one
guards a decision that was expensive to get right:

| Godot assertion | Why it matters |
|---|---|
| `test_sim.gd:125-127` | After `separate_bodies((0,0), (0,0.3))` the **homeowner** must end at `y < 0`. The direction of yield is the documented choice that caused 142 re-routes when it was wrong. The port asserts total distance but not which body moved |
| `test_sim.gd:131-132` | `steer_around` must change the heading by **> 0.1 rad**. The port asserts only `TestNotEqual` — `AVOID_TURN` could be 0.001 and it would pass |
| `test_sim.gd:137-140` | The swerve must turn **away** from the homeowner, not into him. Not ported at all |
| `test_sim.gd:568-570` | After a shove, the valuable returns to the world **where he stood**. The port asserts the flags only |

Everything else has an equivalent, and several port assertions are strictly stronger. The
furniture-containment pair is ported but lives in the **game** module (`HI.Unreal.Furniture`),
so a `HI.Sim.*` filter misses it.

---

## Fixed as a result of this audit

- **`thief_shove` was never called.** `FHISim::ThiefShove` was ported, correct and tested —
  and `StepThief` had no call to it, behind a stale comment claiming something else was "NOT
  YET PORTED". So the intruder never staggered or knocked back the homeowner at contact range,
  in a house where a man at arm's length should be the most dangerous thing in it. Three
  layers were wired for it and none of them fired: the function, the four constants, and a
  game-layer handler for a `thiefShove` event the sim never emitted. Now called, with a test
  that drives `StepIntruders` — **the group path the game actually uses**, because the old test
  called the function directly, which is why it passed while the game never ran it.
- **Bullet holes stood out of the wall as blades, and the corpse mark was a red sheet.** The
  slab is thin in Z and `SizeMeters` across X and Y; `FVector::Rotation()` aims the local **X**
  along the vector, so the size extent was landing on the wall normal. Wall holes were 10 cm
  blades, and the 0.9 m corpse mark stood **vertically** — which is almost certainly the
  "unidentified two-tone object, dark red above green, 1.2 m wide, 2.06 m tall" that this
  project had recorded in the README and could not name. Now `FRotationMatrix::MakeFromZ`, with
  `HI.Unreal.MarkOrientation` asserting the thin axis lies along the normal and the face lies
  in the wall.
- **The player's own shots had no muzzle flash and no tracer.** Godot flashes on every trigger
  pull and draws a tracer per pellet, both from `player_actor.muzzle()`. Both now exist, from
  the gun rather than the camera — a tracer that starts inside your own head is invisible.

## Stale records found, and corrected here rather than left to mislead

- `README.md` still describes "A RUG LIES ON THE FRONT LAWN … and it is the Godot source's bug"
  and says Godot has no furniture heights. The rug was moved to z **−10.60** in both projects
  and the port matches; the README itself retracts the height claim elsewhere.
- `README.md` says "walls have no texture of any kind", while `HIHouse.cpp:509` textures them
  with `ceiling_plaster`. The same repo states both.
- `README.md` lists fix-list item 3 as delivering "roof, eaves, plinth". There is **no plinth**
  in `HIHouse.cpp`, and the roof is flat.
- `HISim.h` still asserts "The Godot source carries no furniture heights", which the README
  retracts.
- The two deliberate port decisions are recorded in `home-invasion-godot/AGENTS.md`, not in
  this repository. A reader of this repo cannot find them, which is how the wall-texture and
  furniture divergences got called "fine" in the first place.

## Not settled by reading

- **Root motion.** Godot measures each clip's hip travel and removes it; the port never
  mentions root motion. If the engine clips carry it and the mesh applies it, bodies would
  drift from their sim positions. Read `bEnableRootMotion` on the clips and
  `AnimRootMotionMode` on the mesh, or compare a body's mesh offset against its sim position in
  a capture.
- **Godot's `AudioStreamPlayer3D.attenuation_model` default** — the port's is explicitly
  `Linear`, and Godot never sets one.
- **Whether Escape releases the mouse** in a packaged build, and **Unreal's camera near plane**
  (Godot sets 0.08).
- **Whether the `wall_window_closed` glTF already contains a mullion**, which decides whether
  the port's added central mullion is faithful.
