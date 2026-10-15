# REALISM BACKLOG

Written 2026-10-15 on Richard's instruction: *"You don't need to make the changes
yet, but make me a list of things you see that should be updated to make the game
more like real life."* **Nothing in this document is implemented.** Every item here
is a decision for Richard: build, defer, or reject. Where an item has a measured
fact behind it, the measurement and its date are in the entry.

Scope note: the house geometry itself is not on this list — that is
[HOUSE-DESIGN.md](HOUSE-DESIGN.md). The vertical slice deliberately excluded
falls, furniture-height sight lines and big-loot-on-stairs (HOUSE-DESIGN.md §6);
they land here as items with owners.

---

## Richard's three (verbatim from the 2026-10-15 instruction)

### 1. "The furniture looks like blocks"

True and structural: every furniture row in `FHISim::FurnitureRows()` is an
axis-aligned box, and `HIHouse` builds each as one `AddBox` instance. The sim's
collision will stay boxes forever (that is correct — hitboxes should be simple),
so the fix is **visual only**: real meshes placed at the data rows, boxes kept as
collision. Concretely, worst offenders first (size measured from the furniture
table, 2026-10-15):

- **Sofa** (living, 2.2×0.95×0.80): seat cushions, back cushions, arms, legs.
- **Beds** (master 1.6×2.0, second-floor rooms): frame, mattress, duvet, pillows.
- **TV** (1.2×0.1×0.7 on a stand): screen bezel, panel, stand — it is a named
  loot item the player stares at while stealing it.
- **Kitchen**: counter run with toe kick and countertop overhang, sink basin,
  stove with burners, fridge with doors and handles.
- **Chairs and tables**: legs and backs instead of slabs.
- **Loot props** (safe, laptop): the laptop is currently a box on a desk; a real
  closed-lid laptop model reads instantly.

Cost driver: none of this needs purchased assets — procedural meshes or simple
modeling pass. The data rows (position, size, height) stay authoritative for
collision; visuals get to be *smaller* than the box, never bigger.

### 2. "The outside doesn't look like a neighborhood"

Also true: beyond the house footprint there is a ground plane, the van, and sky.
What a neighborhood actually contains, roughly in order of what the player sees
during a round (he is looking at the van and the street for most of the intro):

- Street with curb, asphalt, lane markings; driveway apron to the house.
- Sidewalk from the street to the front steps (the walk the thieves come up).
- Neighbors: 2–4 houses down the street, simple but varied (mirror/rotate the
  existing house massing; they never enter gameplay, so they can be shells).
- Street lights with actual light fixtures — the round is at night, so the
  lighting *is* the neighborhood read.
- Mailbox, house number, porch light by the front door.
- Landscaping: grass material (currently plain), hedge or fence line, 2–3 trees,
  shrubs at the foundation.
- Parked cars on the street (static, unlootable — atmosphere).
- Distant backdrop: treeline or rooftops so the sky does not meet a flat plane.

Gameplay consequence to decide with any of these: **cover**. Hedges, parked
cars and trees occlude sight lines. Either they get added to `Solids()`/
`WorldSolids()` (the thieves' pathing then has to respect them — the graph does
not model the yard today) or they stay visual-only and slightly dishonest. My
recommendation: keep the yard walkable-and-visible as it is now, make landscaping
low (hedge under eye height 1.62 does not block sight anyway — the span rule
already handles that once heights are honest, see item 5).

### 3. "The van is just a box"

Measured from the sim constants (2026-10-15): the van is the rect
(−2.75, 7)..(2.75, 9.4) × wall-height box — 5.5 m long, 2.5 m wide, 3 m tall,
which is closer to a shipping container than a cargo van. Real proportions for a
Ford Transit-style van: ~5.5 m long ✓ (length is right), ~2.0 m wide, ~2.5 m
tall, with a cab slope, wheels, windows, mirrors, and rear doors that open.

- Cab: windshield, hood slope, two side windows, side mirrors.
- Wheels: four, with wheel wells — the box currently floats at ground level.
- Rear doors: the loot goes in the back; doors that visibly receive the TV are
  worth more than any other detail here.
- Cargo area interior glimpse when doors are open.
- Livery: plain white work van, or something with character (Richard's call —
  this is the game's cover art object).
- Headlights/taillights: emissive at night; the van is the first thing on screen.

Collision stays the box (thieves path to `van` node at (0, 5) and the drop zone
is a rect — both unchanged by visuals).

---

## My additions — gameplay realism

### 4. Falls from height (there are none)

Today a body at the second-floor hole edge simply cannot leave the floor: the
railings are 1.0 m solid boxes (span [3.0, 4.0]) and the stairwell is the only
opening. Real houses: people fall off ladders, through railings, down stairs.
Options, cheapest first:

- **Stumble on shove near the edge**: a shove that lands a body in the hole
  footprint converts to fall damage (Z → 0 over ~0.5 s, damage on arrival). All
  the machinery exists: `Shove` already re-derives elevation; the hole is a
  known rect; only the "no deck underfoot → descend" rule and a damage number
  are new.
- **Slip on stairs during combat**: same machinery, along-band slide.
- Full physics: not worth it. The sim is kinematic by design (deterministic,
  testable) and must stay that way.

Decision needed: lethality. A 3 m fall is ~serious-injury, not death; a shove
off the second floor as a *tactic* is the interesting question.

### 5. Furniture-height sight lines

`FurnitureRows()` carries a real height per row (coffee table 0.45, counter
0.92, fridge 1.85, bookcase 2.2 — data measured 2026-10-15), and none of it is
used for sight: `SolidSpans()` gives every ground-floor solid the uniform span
[0, 3.0], so a 0.45 m coffee table blocks vision like a wall. The span machinery
is already exact (`SpannedRectBlocks` clips per-solid, built for the slabs) — the
only change is feeding real heights into furniture spans. Why it was deferred:
it flips a pile of sight-line pins in the test suite and changes thief behaviour
everywhere at once, which is too much to mix into the floor fork. This is the
single highest-value realism item for *feel*: crouching behind a kitchen counter
should work, and today it does not.

### 6. Carrying big loot on stairs

`CarrySpeed` slows a carrier on the flat; stairs additionally pace everyone to
0.7×. A TV down a flight in reality is a two-person, one-step-at-a-time affair.
Slice state: loot is all ground-floor, so this is untested territory. Phase 2
(the full house) moves the Laptop to the office upstairs, which makes this real:
proposal — carrying upstairs at 0.5×, downstairs at 0.35×, and the big items
(TV, safe) refuse stairs entirely solo (forces a decision: leave it, or the
second thief helps — co-op carry is a bigger feature).

### 7. Sound through floors

Audio already consumes `Who->Z` for position (HIGameMode.cpp, verified
2026-10-15), but loudness falls off on **plan** distance: a shotgun on the
second floor alerts a ground-floor thief at full yard-scale loudness. Real
houses: floors attenuate impact/gun noise substantially (and stairwells funnel
it). Proposal: when listener and source are on different floors and the segment
between them crosses a deck (the exact `SpannedRectBlocks` test again — the
machinery exists), scale loudness by ~0.4. Footsteps: nearly silent through a
deck; stairwell: full.

### 8. Interior doors

Every interior opening is a permanent gap in a wall (hall gap, room entries).
Real houses have doors, and doors are *gameplay*: a closed bedroom door hides
the thief's search progress, a slammed door is an audio event, a kicked-open
door is a moment. Cheapest honest version: door slabs in `WallFittings()`
(visual + swing animation), open by default, and one `bDoorClosed` flag per
opening that adds a sight-blocking span [0, 2.1] and muffles sound when shut.
Thief AI already routes by graph — closed doors just cost him a "open door"
beat at the waypoint (0.5 s, an audio event the player hears).

### 9. Railings as real models

The railings are 0.10 m solid boxes. Visually they should be balusters + cap
rail, and see-through (real railings do not block sight). The sim's span rule
already says the railing blocks *movement* [3.0, 4.0] but the sight question is
separate: today a railing box also blocks sight (it is a non-deck solid). Correct
model: keep the movement box, remove railings from the sight list (or give them
baluster gaps). Small, but visible every time the player looks down the
stairwell.

### 10. Windows on the second floor

`WallFittings()` derives window holes from the ground-floor walls only (it
asserts flush-against-wall — the `HI.Unreal.WallFittings` test). The slice's
second floor has four blank exterior walls. Phase 2's full-house data
(HOUSE-DESIGN.md §4) already schedules bedrooms and bath with windows; this item
is the actor-layer work to put openings in floor-1 walls, plus the sight
consequence: a window is a sight tube between floors of the *outside* — the
player on the lawn can see into the upstairs rooms, and thieves should too.

### 11. Cinderblock basement

Richard's brief: *"the basement will be an open area with cinderblock walls, and
a water heater and a furnace."* Phase 2 adds the geometry; this item is the
material pass — block courses with mortar lines, painted or bare — plus the
appliances as identifiable objects (water heater: white cylinder, flue, pipes;
furnace: grey cabinet, ductwork rising into the ceiling). The two lally columns
in the design are a realism win here too: real basements have them.

### 12. Minimap floor awareness

The minimap and `VisibilityPolygon` run on the ground-floor projection
(`Solids()`), which is correct-but-blind once there are two floors: standing
upstairs, the minimap shows the floor *below* you. Options: (a) filter the
projection to the player's floor and hide furniture/walls of other floors,
(b) keep one map but mark the stairwell and grey out the other floor, (c) two
inset maps. Richard plays the minimap hard — his call.

### 13. Stair treads as geometry

The actor layer will render the slice's flight as 16 tread boxes (data from
`Stairs()`). Realism details for later: nosing lip on each tread, closed risers
vs open (design says open — sight lines down the flight are a feature), stringer
skirt boards, and a handrail on the open side at 0.9 m above the tread line
(that is a *second* railing, distinct from the floor-edge one — code says 0.9 m
above nosing, continuous, graspable).

### 14. Night lighting inside the house

The round is at night. Currently interiors are lit evenly enough to play. Real
houses at night: dark corners, pools under fixtures, the TV's glow, flashlight
beams. Gameplay lever: thief visibility (and the player's) should *mean*
something in the dark — that interacts with sight spans, muzzle flash as a
light event, and the audio-alert system. Bigger than a backlog line; flagged
because item 2's street lights and this one are the same art direction decision.

### 15. Wrist and arm animation for shoot/reload

Older open question, still open: the shoot/reload clips play from the shoulder;
wrists stay rigid. Visible in first person every magazine. Animation-layer work
(HIAnimTool), independent of everything above.

---

## Suggested order (if Richard wants one)

1. **Item 5** (furniture-height sight) — cheapest, biggest gameplay-feel change,
   machinery already built.
2. **Item 1** (furniture models, sofa/bed/TV first) — the thing Richard named,
   and the living room is where players look.
3. **Item 3** (van) — named, cover-art object, self-contained.
4. **Item 2** (neighborhood shell) — named, mostly static scenery.
5. Item 4 (falls) — small mechanic with big tactical consequence, needs the
   lethality decision first.
6. Everything else in the order Richard's taste dictates.

None of this blocks the vertical slice or phase 2; none of it changes the sim's
determinism contract (every proposal above is either visual-only or a data rule
in the same style as the existing ones).
