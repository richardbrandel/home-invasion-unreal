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
- Outside the sim, the port diverged almost everywhere when this audit was written. Corrected
  2026-10-15, after a first pass of fixes: **9 of 16 animation clips** are played (the cast
  imports carry all ten of their clips), the intruders are **per-Id named characters** rather
  than one tint-less mesh, the minimap is in Godot's corner, and the directional hit marker is
  ported. Still open: **3 of 7 imported sound files** are used, the furniture is cubes, there
  are no windows visible from inside, no door leaf, no driveway, no street, and the minimap
  legend is still wrong.

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

### 2. Three intruders are three identical men — CLOSED 2026-10-15, by a different route

Godot tints each body four ways — `THIEF_TINTS` (`game.gd:34-39`), applied per intruder at
`game.gd:543`. When this audit was written the port loaded one mesh for all of them. The cast
import superseded it: `FHICharAnim::IntruderTagForId(ThiefId)` gives every intruder a distinct
named person, cycling (`HIIntruder.cpp BuildVisuals`), which answers the question the tints
answered — three men through a door are tellable apart — by a means the original did not use.
Recorded as `deliberate`: full-body characters rather than tints, because the assets exist.
`FHIMaterials::PlayerSkin` / `IntruderSkin` remain defined and unreferenced.

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

**Position fixed 2026-10-15**: the panel now sits bottom-right with Godot's 22 px margins.
The legend rows below are still open.

| | Godot | Port |
|---|---|---|
| Position | bottom-right, `(1334, 674)` in 1600×900 | ~~top-right~~ — **fixed 2026-10-15**, bottom-right at the same 22 px margins |
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

### 8. Nothing tells you which direction you were shot from — CLOSED 2026-10-15

Godot places a mark beside the crosshair at the shooter's bearing, fading over 1.1 s
(`game.gd:1142-1146, 2167-2186`) — added because "being shot gave you nothing but a falling
number". Ported in full: `AHIHUD::HitBearing` is game.gd's own atan2 formula (asserted in all
four quarters by `HI.Unreal.HitBearing`), `DrawHitMark` draws the 10×28 px bar at 62 px,
`(0.94, 0.25, 0.22)`, `alpha = clamp(t/1.1)·0.92`, rotation about the top-left corner like a
Godot Control. Set on a HIT roll only — a round that goes wide gives no direction, as in the
original. `-HIHoldHitMark` is the port of `--dbg-hit`, applied where the timer decays so the
mark genuinely holds for a camera. The invented red screen edge stays: it is additive, and the
PARITY rule sends invented *replacements* back to Godot's answer, not invented additions that
the original's own feedback rides alongside.

Pixel-verified in combat captures 2026-10-15: 263-286 mark pixels a frame once a hit lands,
bearing and radius matching the shooter, and a 15° mark's bounding box measuring 17 px =
10·cos15° + 28·sin15° to the pixel (the rotation is YAW — `FRotationMatrix` makes Roll an
out-of-plane spin). One trap recorded for the next person: the code's colour is LINEAR; the
sRGB framebuffer reads it as (248, 137, 129), and a scanner thresholded on the code's own
numbers finds nothing and calls a working feature invisible — README bug 26.

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
| ~~`CAM_FADE 0.35`~~ — **fixed 2026-10-15**, now 0.42 | `0.42` | `HIPlayerCharacter.cpp` vs `game.gd:110` |
| ~~Recoil returns 100% of the kick~~ — **fixed 2026-10-15**, now 65% | **65%** (`0.65` multiplier), leaving a residual | `HIGameMode.cpp` vs `game.gd:1456` |
| ~~Reticle Y from the horizontal half-FOV~~ — **fixed 2026-10-15, and it was worse than this row says**: the port multiplied X by the aspect and left Y bare while feeding UE's HORIZONTAL tangent, so the aspect sat on the wrong axis under either reading — an aim at the true frustum edge projected to x=1.389 on a 1.0-wide screen. It hid because the test's `Expected` was computed the same wrong way (a check that could not fail); first-principles edge-ray assertions now guard it | the **vertical** one, width derived from aspect | `HIHUD.cpp` vs `game.gd:2092-2096` |
| ~~Thief shove does not kick the view~~ — **fixed 2026-10-15**: `pitch +0.10`, `recoil_recover +0.06`, impact sound at the player, Godot's own words ("He shoved you off!") and 1.4 s | `pitch += 0.10`, `recoil_recover += 0.06` | `HIGameMode.cpp` vs `game.gd:1187-1189` |
| ~~Camera pull-in: 0.12 m sphere at `Hit.Location`~~ — **standoff ported 2026-10-15**: `Hit.Location + (Pivot−Ideal)·0.14 m`, game.gd:1407's own vector. The sphere STAYS as the recorded port decision (it keeps the near plane out of walls a zero-thickness ray passes beside); Godot's ray has no such guard | ray plus a **0.14 m standoff** | `HIPlayerCharacter.cpp` vs `game.gd:1402-1407` |
| Mark pool 96, cleared each round | **48**, and Godot never clears them | `HIGameMode.cpp:62, 1118-1125` vs `game.gd:158` |
| ~~Bullet hole 0.10 m, no shotgun case~~ — **sizes fixed 2026-10-15**: 0.16 m base, ×1.7 shotgun, ×0.7 flesh, on the player's pellets AND the thief's wall mark. The `bullet_hole.png` texture and flat-tint question stay open | **0.16 m**, ×1.7 for a shotgun pellet, ×0.7 on flesh, `bullet_hole.png` | `HIGameMode.cpp` vs `game.gd:2033, 2041-2043` |
| ~~Muzzle flash 0.22 m~~ — **stale, corrected 2026-10-15**: the code flashes 0.34 m in the original's warm white; still open are the texture, additivity and billboarding | **0.34 m**, `muzzle_flash.png`, additive, billboarded | `HIGameMode.cpp` vs `game.gd:1825-1837` |
| ~~Tracer 20 mm square~~ — **stale, corrected 2026-10-15**: tracers are 24 mm (0.024); still open are the cylinder cross-section and the single emissive colour | **24 mm cylinder**, one emissive colour | `HIGameMode.cpp` vs `game.gd:1548-1555` |
| No footsteps, no casings, no muzzle smoke | four footstep timers, 24 casings with bounce, 10 smoke puffs | `game.gd:1764-1790, 1878-1929` |
| ~~Loot floats at a fixed +0.42 m chest offset~~ — **stale, corrected 2026-10-15**: carried loot rides the **hand bone** (`bHaveHand` path), with the tuned chest offset only as the no-body fallback, exactly game.gd:730-734's specification | rides the **hand bone** with per-label `CARRY_HANG` | `HILootProp.cpp:155-200` vs `game.gd:629-633, 730-734` |
| Loot is not emissive | `emission = col * 0.07`, so a valuable is findable in a dim corner | `HIMaterials.cpp:96` vs `game.gd:660-661` |
| Crouch moves the eye only | also squashes the model to 0.76 height | `HIPlayerCharacter.cpp:129` vs `game.gd:1260-1261` — **re-scoped 2026-10-15**: the port now plays authored `crouch_idle`/`crouch_walk` clips per capsule height (`HICharAnim.cpp:202`), which is what the squash was standing in for; what remains unported is the extra 0.76 mesh squash on top of the clip, visible only while crouched. Minor, open |
| ~~No crouch clip, no aim clip, no shoot clip~~ — **stale, corrected 2026-10-15**: the Mixamo cast carries all ten clips per character, imported and asserted by `HI.Unreal.Cast` | all three (and seven more) | `assets/mixamo/*` vs `SourceAssets/characters/*` |
| The armed standing pose is **`pistol_idle`** (Mixamo "Pistol Idle", 8.30 s, locomotion-pinned: pistol gripped up near the chest); the unarmed `idle` is reserved for an intruder mid-carry, and `aim` is imported, normalized and tested but deliberately never played | `aim` plays ONCE non-looping and holds its last key — Godot's FBX import defaults to no loop and `actor.gd play()` restarts nothing | `HICharAnim.cpp DriveCharacter` (`bArmed ? pistol_idle : idle`) vs `actor.gd:311-320`; Richard 2026-10-15: the port LOOPED the 2.4 s raise-hold-lower `aim`, so Louise "keeps drawing and holstering her gun"; the calm `idle` that replaced it left the gun hanging low behind her torso from the rear camera (measured `-HIProbe` gunDist 1.61-1.81 m vs body 1.38 m — rendered but occluded), so he chose a real pistol-ready clip. Then, on the delivered pose, Richard 2026-10-15: "Move the gun up a little bit, about 6 inches" — done as an import-time ARM RAISE (`HIAnimTool` pre-multiplies measured local-space shoulder deltas, RightArm +24.6° / LeftArm +23.8°, onto `pistol_idle` for all four characters, derived by `tools/gunraise.py` from an in-pose `-HIProbe` bone dump), so the hands carry the gun: gunLoc Z 1.09 → 1.19-1.25 m, RightHand bone 1.097 → 1.249 m, gunSight clear, suite 41/41, vision read "CONTACT … all five fingers wrapped around the grip". The gun-only attach offset was built and measured first (an exact 15.24 cm rise) and rejected on frames: hands stayed at the waist, pistol visibly floating above them |
| Opening hold-fire: first shots at 4.0 / 4.7 / 5.4 s (`ThiefOpeningCd` + stagger) | every intruder opens at cd **1.2 s**, no stagger — an idle homeowner measured HP 57 by t=3.0 s, dead at 8-12.5 s | `HISim.h ThiefOpeningCd` vs `sim.gd:748 create_thief()`; Richard 2026-10-15: "the first few seconds are too brutal. Make them easier." |
| Louise spawns in the **back zone at (−3, −15.5)**, off-axis and facing the hall gap; the InteriorZ divider stops the straight Drop → door-gap → hall-gap snipe channel (every Drop lane's sight line now crosses y=−8 at x=−1.6 to −2.2, west of the door gap) | spawns at **(0, −10.6)**, front zone, dead-centre on the door axis — the near end of an unobstructed sight line from the intruders' Drop (0, 4.5), so a man who has not crossed the driveway can shoot her the instant his cooldown lapses | `HISim.cpp CreatePlayer` vs `sim.gd create_player()`; Richard 2026-10-15: "the fender ... is standing in the door way while the intruders are coming at her. I think she should be more inside the building." |

### 14. Godot sim assertions with no Unreal equivalent — CLOSED 2026-10-15, with one correction

All four assertions are now ported. **The fourth row below mis-described its own source**:
`test_sim.gd:568-570` asserts only the flags (`not taken and not delivered`), which the port
always matched — Godot's *message* says "where he stood" but its assertion never checked
position, in either suite. The port now asserts the position too (`Loot[1].Pos ≈ Thief.Pos`),
strictly stronger than Godot, because loot that "returns to the world" somewhere else reads
as teleporting loot in play and passes both original suites.

| Godot assertion | Status |
|---|---|
| `test_sim.gd:125-127` — homeowner is the body that yields (`y < 0`) | **ported** into `HI.Sim.Separation` |
| `test_sim.gd:131-132` — `steer_around` turns **> 0.1 rad** | **ported** into `HI.Sim.Steer`, on Godot's own inputs |
| `test_sim.gd:137-140` — the swerve turns **away** from the homeowner | **ported**, same inputs (`north`, `(-0.4,-1.5)`, `> north`) |
| `test_sim.gd:568-570` — after a shove the valuable is back in the world | **was already matched** (flags); position now asserted on top |

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

### The 2026-10-15 adversarial pass

Found by a soak battery run under corrected tooling (`-HIThieves` was being silently ignored
before that day — see below), and fixed the same day:

- **THE FRONTHUB FREEZE: a single thief stops the heist forever, and the round can then never
  end.** Measured across two byte-identical 400 s soaks (`intruders=1`, `sim_steps=24004`
  both): one of three valuables delivered, then `mode=search target=bedroom route=<none>
  stuck=0.00` for the remaining 327 s, parked 0.59 m from frontHub with the bedroom 5.66 m
  away. THREE defects compounded, and **all three exist identically in Godot's `sim.gd`** —
  this audit found an original-game bug through its port:
  1. A reroute resolved `Thief.Target` only as a loot LABEL; a search target is a ROOM NAME,
     so `Back` fell through to the `frontHub` default — the stuck searcher was rerouted away
     from the room he was searching (`HISim.cpp` reroute block; Godot `sim.gd`'s reroute has
     the same lookup).
  2. The search branch rebuilt a route only when the target CHANGED. He arrives at frontHub,
     the last waypoint pops, the target stays `bedroom` — no route is ever rebuilt. Flee,
     carry and hunt all self-heal an empty route; search did not (`sim.gd:978-980` same).
  3. The stuck detector could not see him: it measures travel against `Thief.Step`, and a
     thief with no route steps nowhere — `Want=0` skips the detector entirely, `stuck=0.00`
     forever (`sim.gd`'s detector has the same shape).
  Why three thieves hide it: `bKnown` is global, so somebody else searches the room and the
  hunt branch takes over — the frozen man walks again. **The gameplay-reachable form is the
  survivor**: drop two of three intruders and the last one can freeze the round into a state
  that can never be won or lost. Fixed in the port by self-healing the search route (with a
  nearest-OTHER-node fallback, because `NearestNode` is Voronoi and can name a room across a
  wall) and by teaching the reroute room names. `HI.Sim.Searching` now reproduces the frozen
  state itself and asserts he walks out. **Godot is NOT patched** — Richard's decision,
  recorded 2026-10-15: leave Godot alone for now; the original ships with the bug, and the
  sim.gd line numbers in this entry are the map if that ever changes.
- **THE PINCER: the avoid-orbit and a wall corner held a searcher to ±5 mm.** With the freeze
  fixed, the regression still did not arrive: frame-level tracing caught him at (−1.58, −13.81),
  exactly 2.21 m from the idle homeowner — `AVOID_RANGE` is 2.2. The raw bearing to the
  bedroom node clips the hall gap's corner by 6 cm, so every frame alternated: step
  southwest, wall contact, slide south; player inside 2.2 m, `SteerAround` deflects west,
  head-on into the same corner, no slide tangent, `ResolveCircle` hands the southward gain
  straight back. `SteerAround` now probes both deflection sides one metre ahead and refuses
  the side that ends inside something solid — "do not steer into a wall", one probe, no new
  state. Deliberate divergence: Godot's `steer_around` has the same pincer (a homeowner
  camping a doorway can freeze a searcher there too).
- **WALL SLIDE on contact** (same pass): the move loop projects the desired step onto the
  contact tangent when a body hits something solid, instead of grinding in place. What a
  physical body does; Godot has no equivalent. Head-on contact still stops him — that is
  the stuck detector's job.
- **The reticle projected off-screen at the frustum edges.** The port fed UE's HORIZONTAL
  `FieldOfView` tangent into Godot's VERTICAL-fov formula and then put the aspect factor on
  the wrong axis for either reading: an aim at the true horizontal edge landed at x=1.389 on
  a 1.0-wide screen. Invisible in play because fixed mode aims along the camera forward, and
  invisible to the suite because the test's `Expected` mirrored the code's own formula — a
  check that could not fail, whose comment ("a quarter of the way") even contradicted its own
  asserted number. Fixed as the tanH translation of game.gd:2092-2096 (X bare, Y × aspect);
  the suite now carries first-principles edge-ray assertions (frustum edge ⇒ screen edge)
  that fail on any axis flip, plus an exact 15° pitch magnitude — 15°, because `PitchMin`
  clamps at 0.30 rad and a 20° test measures the clamp, not the formula.
- **The directional hit marker is ported** (item 8) with `-HIHoldHitMark` as the `--dbg-hit`
  equivalent, held where the timer decays so a camera can actually catch it.
- **The shove is a fight again** (item 13 rows): view kick +0.10, 0.06 riding the recovery,
  Godot's words "He shoved you off!" for 1.4 s, impact sound at the player. The port had a
  message only, differently worded.
- **Numeric parity batch**: recoil returns 65% (was 100%), CAM_FADE 0.42 (was 0.35), camera
  standoff 0.14 m off the wall (was flush), bullet holes 0.16 m with the shotgun ×1.7 and
  flesh ×0.7 (was 0.10 flat), the thief's wall chip 0.16 (was 0.10), minimap bottom-right at
  Godot's margins (was top-right).
- **`-HIThieves=N` was silently ignored by the tooling.** `play.sh` appended its own
  `-HIThieves=3` BEFORE `"$@"`, and `FParse::Value` takes the FIRST occurrence — every
  "single-thief" run before 2026-10-15 actually ran three, which is how the earlier
  "1/3 in 180 s stall" measurement got invalidated and the freeze stayed hidden. `play.sh`
  and `capture.sh` now pass caller arguments first, with the trap written down at both sites.
- **Determinism, qualified** (corrects a blanket claim on record): passive no-combat runs are
  exactly reproducible — two 400 s single-thief soaks matched to the sim step (24004) and to
  the centimetre. Combat runs are NOT: actor-layer hit rolls use `FMath::FRand`, the port of
  Godot's `randf()` — identical lethal runs ended at 34.2 s, 36.4 s and 37.8 s. The sim core
  is deterministic; the fight above it is deliberately dice.

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
- **Whether Escape releases the mouse** in a packaged build. (The near-plane half of this row
  is settled 2026-10-15: the camera keeps the documented 12 cm sphere AND now applies Godot's
  0.14 m standoff off the hit, so the pull-in cannot plaster the near plane into a wall. What
  remains unmeasured is the packaged-build Escape behaviour only.)
- **Whether the `wall_window_closed` glTF already contains a mullion**, which decides whether
  the port's added central mullion is faithful.
