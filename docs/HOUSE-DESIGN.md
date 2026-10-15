# HOUSE-DESIGN — the three-floor house

Richard, 2026-10-15, commissioning it: *"Can you make the house a basement, the ground
level and a second floor? … I would like a real house with 3 bedrooms, 2 bathrooms, a
kitchen and a living room … the basement will be an open area with cinderblock walls, and
a water heater and a furnace. The first floor will have the kitchen, master bedroom,
master bathroom, a living room. The second floor, will have 2 bedrooms, a bathroom and an
office. The room dimensions don't matter too much to me, so you make it work. Try to be
realistic."*

Dimensions are delegated; realism is the instruction. Everything below is the v1 design,
measured — floor plans as coordinate tables, because the house in this codebase is data
the sim reads, not geometry someone models. Where I added a room or fixture beyond his
program (the dining room, the washer/dryer, the second basement column) it is marked
**ADDITION** so he can veto it in one pass.

Coordinate convention stays the sim's: `(x, y)` with +y toward the back of the lot
(Godot's Vector2 mapping; `FVector2D.Y` is world depth), heights `h` in metres above the
ground floor. The van, the drop point and the driveway do not move — the world constants
around the house stay put, so the round's outside half is untouched.

---

## 1. Architecture decisions (the part that is not negotiable by taste)

**The sim owns all movement, including the player's.** `FHIRound::Step` moves the
homeowner from input and mirrors him into the pawn (`SyncFromSim`); UE's
CharacterMovement is not in the loop. I told Richard on 2026-10-15 that "UE does the
human stairs for free" — **that was wrong**, and this document is where the wrong guess is
recorded: the player climbs stairs in the sim, like everything else he does. The
consequence is good rather than bad: one elevation function serves the player, the
thieves, line of sight and audio, and it stays deterministic and testable headlessly.

**Floors are explicit, height is derived.** Plan position alone cannot distinguish floors
where they stack (the same (x,y) is basement, ground and second), so:

- every entity carries `int8 Floor` (0 = ground, +1 up, −1 down) and `float Z` (already
  exists on `FHIPlayer`/`FHIThief`, unused until now);
- world data rows (walls, furniture, nodes, loot, rooms) carry a floor;
- `Z = FloorZAt(Pos, Floor)` — flat floors give the floor's height, stair bands
  interpolate along their run. One function, sim and actors read the same truth;
- a body's floor changes ONLY on a stair band (crossing its midpoint flips `Floor`), so
  floor state cannot drift from geometry.

**Slabs are `Solids()` entries with real Z-spans, and a stairwell is a literal hole.**
`LosBlocked3D` (HISim.cpp:1256) rejects a solid whose span the sight segment never
reaches, then does the 2-D crossing test. A second-floor slab spanning `[2.59, 2.74]`
therefore never blocks two pairs of ground-floor eyes (segment Z ≈ 1.6, rejected), and
compete for cross-floor sight only where the plan line crosses slab rather than hole.
The `SolidSpans` comment that says a floor slab "can carry its own span and block sight
between storeys" (HISim.h:703) anticipated exactly this.

One correction before it bites: the vertical reject as written compares the WHOLE
segment's Z range, then tests the WHOLE 2-D segment — conservative, and the false
positive lands precisely on the gameplay moment that matters. A homeowner at the stair
foot (6.0, −11, eyes 1.62) looking up at a man on the second floor (7.1, −12, eyes 4.62)
truly sees him: the sight line crosses slab height 2.75 at (6.41, −11.38), inside the
stairwell hole — but the conservative form rejects on span overlap and then finds the
segment touching the slab footprint, and calls it blocked. Defending the stairwell is a
core scenario, so the slice upgrades `LosBlocked3D` to the exact test: per solid, clip the
segment to the t-interval where z(t) lies inside the span, and run the 2-D crossing on
the clipped sub-segment. Flat spans make z(t) linear, so this is a few arithmetic ops per
solid on a function called dozens of times a frame — and exact ⊇ conservative, so every
existing elevation assertion still holds (truly-blocked stays blocked; only false blocks
clear).

Furniture spans stay uniform-at-a-full-storey for now. Real furniture heights already
exist in the data (`FurnitureHeights`) and seeing over a sofa but not a fridge is on the
realism backlog — but it changes every sight test and every thief behaviour at once, and
it is a feel decision for Richard, not a side effect of the floors.

**Thief sight must become 3-D.** Today `ThiefSees → CanSee → RayCastPx` iterates
`Solids()` with no spans at all — purely 2-D. Unchanged, a man in the basement sees
through the ceiling. The slice adds `CanSee3D`/`ThiefSees3D` on `LosBlocked3D` with real
eye heights (1.62 standing / 1.05 crouched — constants exist) and switches `StepThief` to
it. The 2-D functions stay for the minimap's visibility polygon.

**Stairs are elevation bands, not obstacles.** Inside a stair band rectangle, a body's Z
interpolates along the run; the band does not collide (walking into it IS walking up it).
Bodies on a stair band skip separation (a 1 m flight is single file) and skip the
avoid-orbit deflection (`SteerAround` cannot orbit in a 1 m corridor — the pincer fix of
2026-10-15 must not fire there). Stair traversal costs more: speed × 0.7 on stair edges,
which is also what makes cross-floor chases tense rather than instant.

**The minimap draws the player's floor only** (full-house phase; the slice keeps today's
ground-only minimap). Filtering `VisibilityPolygon` and the furniture dots by floor is
mechanical; showing all floors superimposed would be unreadable.

**SelfCheck changes meaning.** Today it asserts the world data matches the Godot literal
("seven walls…"). After the fork its job is self-consistency of the NEW house: no
furniture overlaps a wall or another item on its floor, every node reachable from
`doorIn` on its floor graph, stair bands connect exactly two floors, slab holes contain
their stair tops, every loot node exists. A parity validator cannot validate a house the
original never had.

---

## 2. Global constants (v1)

| | value | note |
|---|---|---|
| Footprint | x −6.10 … 6.10, y −17.14 … −8.00 | 40 × 30 ft, 111.5 m² per floor |
| Basement floor h | −2.44 | 8 ft; clear ceiling 2.29 under the deck |
| Ground floor h | 0.00 | ceiling 2.59 (8 ft 6 in) |
| Second floor h | +2.74 | 9 ft floor-to-floor; ceiling 2.44 |
| Roof deck h | 5.18 … 5.33 | gable above, visual |
| Slab solids | 0.15 thick | deck [−0.15, 0]; second-floor [2.59, 2.74]; roof [5.18, 5.33] |
| Stair width | 1.00 m | straight runs, stacked in one well |
| Riser / tread | ≈ 0.18 / ≈ 0.28 m | within code limits (7¼ in / 11 in) |
| Wall thickness | 0.25 exterior, 0.12 interior | existing constants |
| Headroom rule | ≥ 2.0 m vertical | the slab-hole extents below are computed from it |

**ADDITION** flagged once for all: the house grows from 176 m²/floor to 111.5 m²/floor but
gains two floors — total conditioned area ~300 m² (~3 200 ft²) with the basement. That is a
big but real American house, which is what "3 bedrooms, 2 baths, office, master on main"
describes.

---

## 3. Phase 1 — the vertical slice (existing footprint)

Prove the machinery before the house data is touched wholesale: one staircase, a partial
second floor, sight through the stairwell, one thief that uses it. Everything here is
data the full house replaces; everything in section 1 is code that survives.

On today's 16 × 11 footprint (`House` = x −8…8, y −19…−8, divider at y = −14), +y is
toward the back of the lot:

- **Second floor over the FRONT half only** (y −14 … −8), slab span [2.75, 3.0]
  (floor-to-floor 3.0 = today's `WallHeight`). One room: `upstairs`, node (0, −11,
  floor 1), a SearchNode. The rear half stays single-storey under its existing roof —
  which is why the staircase lives in the front half, under the slab it climbs through;
  a first draft of this slice put the run in the study (rear half) and a man's head at
  the top tread (4.8 m) went through the 3.0 m rear roof. Recorded so the mistake is
  not repeated in the full house: **a stair top must arrive under a two-storey volume.**
- **Straight run in the kitchen's east strip** (the study version died above; the
  kitchen counters move instead): band x 6.60 … 7.60, y −13.60 … −9.10, bottom at the
  SOUTH end (y = −9.10, h 0), top at the north (y = −13.60, h 3.0). Rise 3.0 = 16 ×
  0.1875; run 4.5 = 15 × 0.30. The top tread opens west onto the deck.
- **Slab hole** x 6.35 … 7.875, y −13.90 … −10.50 — computed from the 2 m headroom rule:
  h(y) = 3.0·(−9.1−y)/4.5 along the run; h + 1.8 clears slab-underside 2.75 from
  y ≤ −10.44, rounded to −10.5. The slab is therefore three rectangles: west
  (x −8…6.35, y −14…−8), south (x 6.35…8, y −10.5…−8), north strip (x 6.35…8,
  y −14…−13.9).
- **Railings** (0.10 thick, span [3.0, 4.0] — they block second-floor walking, never
  block ground-floor sight (span reject), and second-floor eyes at 4.62 see over them
  (also rejected)): hole west edge x 6.25…6.35, y −13.3…−10.5 (the gap at the north end
  is the step-off from the top tread); hole south edge x 6.35…7.875, y −10.6…−10.5.
- **Stair side wall** (stringer): x 6.50…6.62, y −13.9…−9.1, span [0, 3.0] — you enter
  the stairs from the kitchen at the open south end, not through the side.
- **Second-floor walls**: the front half's own exterior runs at y = −8 (south), x = ±8
  (east/west, y −14…−8), and y = −14 (north, above the ground divider — the two-storey
  front section's back wall), all span [3.0, 5.75]. No openings in the slice.
- **Roofs**: rear half stays [3.0, 3.15]; front half rises to [5.75, 5.9] (second-floor
  ceiling 2.75). Head at the top tread: 3.0 + 1.8 = 4.8 < 5.75 ✓.
- **Kitchen data moves**: counter (6.6, −13.0) → x 5.10, fridge (6.7, −13.85) → x 5.20;
  every other furniture row stands. Desk untouched (the study keeps its desk — the stair
  moved out of it).
- **Graph**: `stairFootG` (7.1, −8.9, floor 0) and `stairTopG` (7.1, −13.75, floor 1);
  edges kitchen — stairFootG — stairTopG — upstairs, the two stair edges flagged.
  SearchNodes gain `upstairs`.
- **Player climb**: walk north into the band from the kitchen, Z rises along the run,
  `Floor` flips at the band midpoint (y = −11.35), step off west at the top onto the
  deck. Pawn, camera and audio ride Z (audio already consumes it — HIGameMode.cpp:1253).
- **Under-stairs**: no solid. Inside the band you are ON the stairs (Z interpolates for
  anyone in the footprint) — a ground-floor walker crossing the band mid-kitchen climbs
  it and comes back down, odd but harmless, and the full house walls stairwells in
  anyway. The open risers do not block sight — realistic, recorded.
- **No falls, no basement** in the slice; both are phase 2 with the stacked run.

**Slice tests (new):** FloorZ interpolation ends + midpoint; the sight matrix — same
floor clear, cross-floor blocked by slab, clear through the hole (both walkers inside the
well), blocked past the hole's north edge, clear over the railing, blocked beside it;
exact-clip regression for the false-block case computed in § 1 (homeowner at stair foot
sees the man on the deck); NearestNode floor filtering; the headline regression — a
thief spawned at the door, target `upstairs`, arrives and finds the loot within a bounded
sim time (the frontHub-freeze lesson generalised to floors, asserted BEFORE it can rot);
player climbing asserts Z monotone and Floor flipped; no separation inside a stair band;
3-D hit chance with a ΔZ term; SelfCheck v2 passes on the slice data. Existing suite:
the kitchen-geometry pins update for the counter and fridge; the rest should stand — any
that do not are findings, not chores.

## 4. Phase 2 — the full house

### 4.1 Ground floor (h = 0), x −6.10 … 6.10, y −17.14 … −8.00

Front door stays at DoorGap x −1 … 1, south wall. Rooms (rectangles are clear space, walls
centre-line):

| room | rect (x, y) | m² | notes |
|---|---|---|---|
| entry hall | −1.2 … 1.83, −11.0 … −8.0 | 9.2 | front door, coat closet |
| stair hall | −1.2 … 1.83, −17.14 … −11.0 | ~13 | stair band + basement head; passage east of the run 2.03 wide |
| living | −6.1 … −1.2, −13.0 … −8.0 | 24.5 | picture window south |
| master | −6.1 … −1.2, −17.14 … −13.0 | 20.3 | door from stair hall |
| master bath | −3.9 … −1.2, −17.14 … −14.9 | 6.0 | off the master; tub, toilet, double vanity |
| kitchen | 1.83 … 6.1, −17.14 … −12.2 | 21.1 | L-counter, island, fridge |
| dining | 1.83 … 6.1, −12.2 … −8.0 | 17.9 | **ADDITION** — the programme leaves the front-east quarter unassigned; a dining room off the kitchen is what a real house does here. Veto-able (alternative: fold into an eat-in kitchen). |

**Stair band (ground → second):** x −1.2 … −0.2, y −11.1 … −15.03, bottom south (h 0), top
north (h 2.74). Rise 2.74 = 15 × 0.183; run 3.93 = 14 × 0.281.
**Slab hole (second-floor slab [2.59, 2.74]):** x −1.25 … −0.15, y −12.2 … −15.1 (headroom
rule: h(y) = 2.74·(−11.1−y)/3.93; h + 1.8 > 2.59 from y < −12.23).

### 4.2 Basement (h = −2.44), full footprint

- **Open area** west/centre: x −6.1 … 3.0 — Richard's "open area with cinderblock walls":
  all basement walls and the mechanical-room partition render in a cinderblock material
  (actor-layer material work, not sim data). Two lally columns 0.25 × 0.25 at (−3.0,
  −12.6) and (−3.0, −15.4) (**ADDITION**: the second column; one is structurally
  plausible, two is what 40 ft of joists actually do).
- **Mechanical room** x 3.0 … 6.1, y −17.14 … −13.0, cinderblock partition with a 0.9 m
  door at x 3.0 … 3.9: **furnace** 0.75 × 0.75 h 1.40 at (4.7 … 5.45, −16.6 … −15.85),
  **water heater** 0.60 × 0.60 h 1.50 at (3.5 … 4.1, −16.6 … −16.0), washer + dryer
  0.70 × 0.70 h 1.00 side by side at y −14.2 … −13.5 (**ADDITION**: washer/dryer — a
  basement with a furnace and no laundry is a stage set; veto-able).
- Storage shelving 2.4 × 0.4 h 1.8 along the west wall; workbench 1.8 × 0.6 h 0.9 north.
- **Stair band (basement → ground):** x −1.2 … −0.2, y −11.1 … −14.74, bottom south
  (h −2.44), top north (h 0). Rise 2.44 = 14 × 0.174; run 3.64 = 13 × 0.28. It stacks
  under the ground→second run — headroom checked: 2.34 m at the top tread, above code.
- **Deck hole ([−0.15, 0]):** x −1.25 … −0.15, y −11.05 … −14.8 — the ground floor walks
  over it at h 0; the hole exists for SIGHT (a homeowner at the basement stairhead can see
  down; a man in the open basement cannot see the kitchen through the deck). The hole is a
  fall hazard with no fall model — recorded in § 6, gated with a railing solid span
  [0, 1.0] that sight at 1.62 sees over.

### 4.3 Second floor (h = +2.74), full footprint

| room | rect (x, y) | m² |
|---|---|---|
| up hall | −1.2 … 2.0, −17.14 … −8.0 | ~29 incl. stair hole |
| bedroom 2 | −6.1 … −1.2, −12.4 … −8.0 | 21.6 |
| bedroom 3 | −6.1 … −1.2, −17.14 … −12.4 | 23.2 |
| bathroom | 3.6 … 6.1, −17.14 … −13.4 | 9.4 |
| linen | 2.0 … 3.6, −17.14 … −13.4 | 6.0 (**ADDITION**, veto-able) |
| office | 2.0 … 6.1, −13.4 … −8.0 | 22.1 |

Stair hole per § 4.1. Bedrooms 2/3 are generous (230–250 ft²) — real for a house this
size; closet insets can trim them later without moving walls.

### 4.4 Navigation graph (v1, 24 nodes)

Every node carries a floor. `NearestNode(pos, floor)` filters by floor — without that,
the stacked stair nodes (one plan position, two/three floors) make Voronoi meaningless.

- outside: van, doorOut, doorIn (floor 0, as today)
- ground: entryHall (0.3, −9.5), stairFootG (−0.7, −11.35), living (−3.6, −10.5),
  dining (4.0, −10.0), kitchen (4.0, −14.6), master (−3.6, −14.2), masterBath (−2.5,
  −16.0), stairHeadB (−0.7, −11.35 → same plan point as stairFootG, floor 0, distinct
  node so the basement edge has its own head)
- basement: stairFootB (−0.7, −11.35, floor −1), baseOpen (−3.0, −12.6), utility (4.5,
  −15.0)
- second: stairTopG (−0.7, −14.8, floor 1), upHall (0.4, −11.5), br2 (−3.6, −10.2),
  br3 (−3.6, −14.8), bath2 (4.8, −15.0), office (4.0, −10.5), linen (2.8, −14.5)

Stair edges (flagged, ×0.7 speed, no separation, no avoid-orbit):
stairFootG—stairTopG, stairHeadB—stairFootB. Everything else walks flat.
Adjacency otherwise follows doors; the edge table is written with the data in
implementation, and SelfCheck v2 asserts full connectivity from doorIn.

**SearchRooms (9):** living, dining, kitchen, master, baseOpen, utility, br2, br3, office.
Baths, linen and halls are not worth a thief's minute — realistic, and it bounds the
round. This is ~2.2× today's four rooms across two floor changes, so:

- **PoliceTime rises from its current value to 180 s (proposal, flagged for Richard).**
  The HUD clock is the round's whole tension budget; nine rooms over three floors cannot
  be searched in a 116 s round. 180 s keeps a full clear possible but not comfortable.
  This is the one balance number in the design that needs his eyes after a few rounds.

### 4.5 Loot (3 items, unchanged count, redistributed)

| label | room | floor |
|---|---|---|
| TV | living, on the stand | 0 |
| Safe | master, closet wall | 0 |
| Laptop | office desk | +1 |

Two ground, one second — the search MUST cross floors, which is the whole point of the
commission, and the delivery leg down the stairs with the laptop is the round's best new
moment of tension. Basement starts loot-free (utility is searched for nothing, which is
realistic: nobody keeps the good silver next to the furnace).

### 4.6 Spawns, windows, roof

- Louise spawns in the entry hall (0.3, −9.8, floor 0). Intruders fan on the driveway as
  today; the van and Drop do not move.
- Window schedule per floor (ground: living picture south, master west + north, kitchen
  north + east, dining south + east; second: both bedrooms south/north as their walls
  allow, office south, small high bath windows 0.6 × 0.6 at h+1.4). Windows stay
  actor-layer fittings generated from wall runs — the mechanism exists (`AddWindow`).
- Gable roof over the second floor, ridge east–west at y = −12.57, ~1.9 m rise — visual;
  the roof slab keeps its span [5.18, 5.33] for sight.
- `Bounds` recomputed from the new footprint (yard grows); ClampToWorld follows.

---

## 5. What the slice does NOT prove (and phase 2 must)

Basement stair (second stacked run, third floor state), falls and the basement-head
railing, the full 24-node graph and its police-clock balance, per-floor minimap, window
schedule, cinderblock material, dining/laundry/linen additions pending Richard's veto.

## 6. Deliberately out of scope (both phases)

- **Fall damage.** No fall model exists; stairs cannot injure. Adding one is a design
  question for the realism backlog, not a side effect of floors.
- **Furniture-height sight** (see over the sofa, not the fridge): data exists, behaviour
  change is Richard's call — realism backlog.
- **Carrying large loot down stairs** (the TV through a 1 m well): no animation or
  clearance model; loot rides the hand bone as today. Recorded, not solved.
- Everything visual — furniture that isn't cubes, a van that isn't a box, a neighborhood
  outside the windows — lives in `docs/REALISM-BACKLOG.md`, per Richard's instruction that
  the list comes before the work.

## 7. Verification plan

Slice: suite green (42 existing, migrated where the desk moved, + ~15 new floor tests) →
`-HISoak` single-thief variant with an upstairs target → capture pass: player-chair frames
climbing (Z visible against the hole edge), thief following, cross-floor sight through the
stairwell (he sees you at the hole, not through the slab — asserted in sim AND in a frame).
Full house: same battery, plus the 3-loot/9-room soak under 180 s police, plus rounds×3.
Every claim in the final report carries its measurement, per house style.
