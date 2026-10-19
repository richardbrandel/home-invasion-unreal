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
  (floor-to-floor 3.0 = today's `WallHeight`). One room: `upstairs`, node (0, −12.2,
  floor 1), a SearchNode. The rear half stays single-storey under its existing roof —
  which is why the staircase lives in the front half, under the slab it climbs through;
  a first draft of this slice put the run in the study (rear half) and a man's head at
  the top tread (4.8 m) went through the 3.0 m rear roof. Recorded so the mistake is
  not repeated in the full house: **a stair top must arrive under a two-storey volume.**
- **Straight run in the kitchen's east strip** (the study version died above; the
  kitchen counters move instead): band x 6.60 … 7.875 — the FULL stairwell width, to
  the east wall's inner face — y −13.50 … −9.10, bottom at the SOUTH end (y = −9.10,
  h 0), top at the north (y = −13.50, h 3.0). Rise 3.0 = 16 × 0.1875; run 4.4 =
  15 × 0.293. The top tread opens west onto the header landing.
- **Slab hole** x 6.35 … 7.875, y −13.50 … −10.50 — computed from the 2 m headroom
  rule: h(y) = 3.0·(−9.1−y)/4.4 along the run; h + 1.8 clears slab-underside 2.75 from
  y ≤ −10.49, rounded to −10.5. The slab is therefore three rectangles: west
  (x −8…6.35, y −14…−8), south (x 6.35…8, y −10.5…−8), north strip (x 6.35…8,
  y −14…−13.5 — the header landing the top tread steps off west from).
- **Railings** (0.10 thick, span [3.0, 4.0] — they block second-floor walking, never
  block ground-floor sight (span reject), and second-floor eyes at 4.62 see over them
  (also rejected)): hole west edge x 6.25…6.35, y −13.1…−10.5 (the gap at the north
  end, y −13.5…−13.1, is the step-off from the header landing); hole south edge
  x 6.35…7.875, y −10.6…−10.5.
- **Stair side wall** (stringer): x 6.50…6.62, y −13.875…−9.5, span [0, **2.70**] —
  the wall table grew a per-row `TopZ` for it (full height by default). You enter the
  stairs from the kitchen at the open south end, not through the side. Both deviations
  from the drawing — the 2.70 top and the −9.5 foot — are measured as-built corrections,
  recorded below; the drawing's [0, 3.0] full-run version froze a climber at the top
  landing and pinched the foot corridor to 0.01 m.
- **Second-floor walls**: the front half's own exterior runs at y = −8 (south), x = ±8
  (east/west, y −14…−8), and y = −14 (north, above the ground divider — the two-storey
  front section's back wall), all span [3.0, 5.75]. No openings in the slice.
- **Roofs**: rear half stays [3.0, 3.15]; front half rises to [5.75, 5.9] (second-floor
  ceiling 2.75). Head at the top tread: 3.0 + 1.8 = 4.8 < 5.75 ✓.
- **Kitchen data moves**: counter (6.6, −13.0, 1.15 × 4.00) → (4.90, −13.05, 1.15 ×
  3.40) and fridge (6.7, −13.85) → (5.00, −13.85); every other furniture row stands.
  Desk untouched (the study keeps its desk — the stair moved out of it). The counter's
  shortened length is a walkability fact, not taste: the kitchen→stairFootG nav edge is
  a straight line from (4.5, −9.6) to (7.24, −8.85) that dips to y = −9.49 at the
  counter's east edge, and the world-data test requires every edge to be clear on its
  floor — the 4.00-deep counter ending at y = −9.0 clipped that line by 0.49 m.
- **Graph**: `stairFootG` (7.24, −8.85, floor 0), `stairTopG` (7.24, −13.30, floor 1),
  `upstairs` (0, −12.2, floor 1); edges kitchen — stairFootG — stairTopG — upstairs,
  the two stair edges flagged (a stair edge IS an edge whose endpoints' floors differ —
  `IsStairEdge`, no separate table). SearchNodes gain `upstairs`. The `upstairs` node
  is NOT the plan-centre (0, −11): the stairTopG→upstairs walk line must pass the west
  railing through its step-off gap, and from (7.24, −13.30) it crosses x 6.25…6.35 at
  y −13.16…−13.15 (clear) instead of y −13.0 (inside the railing).
- **AI floor rules** (measured, from the MovingHeist trace of 2026-10-15): target
  choice uses `FloorDist` = plan distance + **12 m** per storey of difference — the
  slice detour hub→stairfoot→top→upstairs walks 3.1 + 4.4/0.7 + 3.3 ≈ 12.7 m, and a
  first penalty of 6 let `upstairs` (plan-near everything) beat unsearched ground rooms
  on float noise. Search ARRIVAL and item GRAB additionally require being on the
  target's floor / within 0.6 m of its height — plan proximity through a slab is not
  presence. And `OrbitBreakTime` = 2.5 s: a thief circling one waypoint that long
  (the tangential orbit around a camped homeowner is a stable attractor at 1.3–1.5 m —
  outside the 1.2 m occupied-pop, moving every frame so the stuck detector reads 0.00)
  walks straight at it and shoulders past; the separation rule already says the
  homeowner yields.
- **Player climb**: walk north into the band from the kitchen, Z rises along the run,
  `Floor` flips at the band midpoint (y = −11.30), step off west at the top onto the
  header landing. Pawn, camera and audio ride Z (audio already consumes it — HIGameMode.cpp:1253).
- **Under-stairs**: no solid. Inside the band you are ON the stairs (Z interpolates for
  anyone in the footprint) — a ground-floor walker crossing the band mid-kitchen climbs
  it and comes back down, odd but harmless, and the full house walls stairwells in
  anyway. The open risers do not block sight — realistic, recorded.

**As-built corrections, 2026-10-15** (found while writing the geometry, all recorded
above in place — listed together because each is a class of mistake the full house can
repeat): (1) the 1.0 m band left 0.275 m slab-free slivers beside the top tread where a
second-floor body would stand on nothing — the band spans the full stairwell, and the
railing's push-out radius makes the west sliver unreachable (a resolved body centre
cannot be west of x 6.77 there, and 6.77 is on the stairs); (2) UE's `FBox2D::IsInside`
is STRICT — `Contains` uses `IsInsideOrOn`, or the top landing is not part of the
flight (caught by SelfCheck v2's arrival assertion on its first run); (3) the stringer
stops at the divider, because a 0.375 m pocket between two walls is narrower than a
body (0.84) and any wall-slide can feed it; (4) `WorldSolids` headroom is sampled every
~21 cm along the flight — finer than a tread — so no hole hides between samples.

**As-built corrections, 2026-10-15 (slice bring-up)** — each of these was found by a
measured statue, teleport or orbit rather than by review; the code comments carry the
full traces:

1. **The stringer's top is 2.70, not 3.0 and not 2.95.** A climber stepping off west at
   the top (the step-off gap, y −13.5…−13.1, interpolated z 2.73…3.0) is within a body
   radius of the stringer's east face. With the top at 3.0 — then at 2.95 — the span
   [0, top] still strictly overlapped his cylinder, so the stringer pushed him back into
   the band on every frame and `ThiefUpstairs` froze at the top landing. Both values were
   tried; both froze (measured). At 2.70 the span ends below the step-off corridor: the
   stringer is transparent to anyone at z ≥ 2.70 and still solid to a ground-floor body
   (cylinder [0, 1.8]).
2. **The stringer's foot stops at y −9.5, not at the band's foot (−9.1).** With the foot
   at −9.1, the corridor between the stringer's foot corner (6.62, −9.1) and the south
   wall's inner face (−8.25) is 0.85 m; two body clearances (0.42 each) leave a 0.01 m
   passage — unthreadable by any slide rule, and `ThiefDescends` ping-ponged at the foot
   for 72 s (measured). Stopping at −9.5 leaves 1.25 m between foot and wall: 0.41 m of
   centre freedom, which the kitchen→stair line threads.
3. **Collision corners are rounded — but only past the end plane.** A body past a run's
   end plane on its long axis gets a diagonal push off the end corner instead of an
   antiparallel face push; that is the statue cure (item 1's freeze was this shape). A
   body BESIDE a long face gets the ordinary face push. The first, broader version — snap
   to the end corner for any body within a radius of the end, including beside the face —
   built a closed 16-frame orbit in `HI.Sim.Shove` (measured 2026-10-15): the corner snap
   replaced a genuine 0.079 m face overlap with a farther-corner "free", the body drifted
   off the push-out equilibrium into the band, the radial corner push and its slide
   tangent pumped it east along the corner circle, and the face push re-acquired at
   nearly full depth and launched it 0.3 m north back to the equilibrium — with the stuck
   detector silent, because Moved ≈ Want on every frame. Beside the face, the face push
   is the geometrically correct answer, and the plain clamp already rounds the end once
   the projection passes it.
4. **Deck beats band.** Where a slab deck covers a point — span [2.75, 3.0], top exactly
   `FloorHeight(1)` — a first-floor body's Z is the deck's top, not the flight's
   interpolation. Band-wins teleported an arriving body ~2 m (measured). The
   `DeckCovers` predicate is shared by `FloorZAt` and `ApplyElevation` so the two cannot
   disagree.
5. **The railing skip is Z-gated.** Inside the stair band a body ignores the first-floor
   railing solids only while Z < `FloorHeight(1)` − 0.02: a climber passes (the band is
   single file, nothing to pincer on), while a second-floor body at the hole's edge is
   still stopped by the railing.
6. **Graph: 14 nodes.** `stairLanding` (6.00, −13.35, f1) was added between `stairTopG`
   and `upstairs` — the straight top→upstairs line crossed the west railing outside the
   step-off gap — and `stairFootL` (7.24, −8.60, f0) pins the kitchen-side foot
   connections. `stairFootG`—`stairTopG` remains the only floor-changing edge.
7. **The 2-D visibility polygon is floor-aware at the actor edge.**
   `VisibilityPolygon(From, MaxRange, Floor)`: floor 0 keeps the legacy `Solids()`
   projection byte-identical; any other floor sees `FloorSolidRects(Floor)` — the same
   filter `WalkBlocked` uses — and the HUD passes `Round.Player.Floor`. The sim's own 3-D
   sight was already span-exact; this stops the minimap from drawing ground-floor
   furniture around an upstairs body.
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

**AS BUILT 2026-10-19: 26 nodes.** hallSouth, hallNorth and screenPass were added,
stairHeadB moved to the stair's NORTH head (not stairFootG's plan point), and eight
designed positions moved — every correction with its measured reason is in §8.1. The
v1 list below stays as designed.

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
  **As built 2026-10-19: the constant IS 180.0 and the measurement says "possible but
  not comfortable" was right to the second** — clean solo sweeps finish at 175.0 s (probe,
  homeowner parked on the lawn) and 181.5 s (probe AND game soak, homeowner alive and
  camping the living room). Ratification is Richard's: OPEN-QUESTIONS item 9 carries the
  full number set.

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
  **AS BUILT 2026-10-19: Louise spawns at (−2.0, −10.6), floor 0, yaw 0.45 — INSIDE the
  living room, 0.8 m from its doorway in the x −1.2 partition.** Richard's ruling, and the
  code comment at the spawn (HISim.cpp) carries the full rationale: "Defender by position,
  not by script." Measured consequences: the Drop sight line crosses the south wall at
  x −1.64 (buried — no driveway snipe), her own sight line to the entry hall crosses the
  partition at y −10.22, inside the 1.2 m doorway opening (y −11.2…−10.0), and she
  commands the living-room approach by body presence — see §8.7 for what a lone thief
  does about it. (OPEN-QUESTIONS 6.5's (−3, −15.5) is the v1-slice spawn, superseded
  twice; §8.7 has the lineage.)
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

**Status 2026-10-19: every item above is IMPLEMENTED and asserted by the 51-test suite**
(§8 carries the as-built record; the graph grew to 26 nodes, §8.1). The police-clock
balance is measured and waits on Richard's ratification (§4.4 note, OPEN-QUESTIONS 9).
The furniture additions stand un-vetoed and are built as designed.

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

**Results, 2026-10-19, final build** (every soak `ended=round` — a resolved game, not a
budget expiry; zero assertions in any log):

- **Suite: 51/51 green** (52/52 with the temporary solo diagnostic in, removed before
  commit).
- **Single-thief variant with the upstairs target** — this plan's specific ask: sim probe
  S2, one thief, full three-floor sweep (TV f0, Safe f0, Laptop f1) in **175.0 s** with
  the homeowner parked on the lawn; **181.5 s** with her alive and camping the living
  room (§8.7). The game-mode solo soak matches the probe to the tenth: `loot_delivered=3
  loot_remaining=0 sim_seconds=181.5`. Before the §8.6 hysteresis fix this variant was
  INCONCLUSIVE — 0/3 at budget, two doorway traps deep.
- **3-loot/9-room soak, ×3 rounds, under the 180 s police**: **3 × 117.9 s**,
  bit-identical (7076 sim steps every round), all three thieves alive, `shots=0` — the
  police never armed because thief gunfire does not arm them (OPEN-QUESTIONS 10), which
  is the one soft spot in "under 180 s police" and Richard's to rule on.
- **Driven battery ×3** (the aimbot, balance-silent): WON at **24.8 / 25.9 / 26.1 s**,
  three intruders down each round, Louise hp 64/64/71.
- **Cross-floor sight through the stairwell**: asserted in sim by the suite's floor/sight
  pins (green). The frame half of the battery ("AND in a frame") was captured 2026-10-19:
  `shots/xssight_01..03.png` (player chair (0.5, −12.5), yaw 220, pitch 25) and
  `shots/xs2_01..03.png` ((0.1, −12.9), yaw 212, pitch 30), trio round at sim seconds
  45/60/75 via `-HIShotSeries`, `-HINoThiefDamage` — reproducible with the README
  switches; `shots/` is gitignored by repo rule. The frames pass the not-blank sanity
  check (1280×720, real scene content); the LOOKED-at-by-a-human step is Richard's, per
  the house capture workflow.
- **PoliceTime**: 180.0 as shipped; the numbers above are the ratification set
  (OPEN-QUESTIONS 9).

---

## 8. Phase 2 as built (2026-10-19)

§3 recorded every number the v1 slice corrected. This section is the same record for the
full house: the graph as it actually stands, the walker mechanisms the build forced, and
the measurements that decided each one. Design intent lives in §4–§5; where this section
disagrees with them, this section is what the code does.

### 8.1 The graph: 26 nodes, not 24

Additions (all with measured reasons):

- **hallSouth (0.9, −12.9) f0** — the hall between kitchen doorway and stair foot. Without
  it the south hall is one 5 m jump and a thief stood in it is a *statue* (nearest-node
  routing has nowhere to walk him that does not cross furniture). Mode-3 doctrine: halls
  exist because standing in them must be legal.
- **hallNorth (0.4, −15.9) f0** — the north hall past the screen. Added, then its use as a
  search-loiter alternative was **rejected by measurement**: probe scenario F (a thief
  loitering there instead of the route design intended) delivered 1/3 in 400 s with 33
  reroutes. The node stays (connectivity), the alternative was not taken.
- **screenPass (−0.7, −15.8) f0, ADDITION 2026-10-18, veto-able** — the walk around the
  stairwell screen's north end. hallNorth↔screenPass↔stairHeadB. It exists because the
  hallNorth→stairHeadB edge passed 0.104 m from the screen's NW corner — inside the 0.42 m
  body — while every radius-0 probe stayed green. Fixed in DATA, per the house rule that
  geometry problems get geometry answers.

Corrections and moves (design → as built, with the margin that decided it):

| node | design | as built | why |
|---|---|---|---|
| stairHeadB | (−0.7, −11.35), same plan as stairFootG | **(−0.7, −14.6)**, the stair's NORTH head | the design point put the basement edge's head at the foot; the run starts in the wrong room and separation drags thieves across the flight |
| stairTopG | (−0.7, −14.8) | **(−0.7, −15.5)** | the upHallN walking line cleared the slab-hole corner by 0.086 m at −14.8 |
| baseOpen | (−3.0, −12.6) | **(−3.8, −11.8)** | furniture-envelope margin 0.025 → 0.345 m (moved 0.40 north) |
| baseDoor | — (not in design) | **(3.5, −12.7) f−1** | the mech-room doorway; envelope margin 0.137 m |
| living | (−3.6, −10.5) | (−3.2, −10.8) | off the console/TV envelope and the Louise spawn lane |
| dining | (4.0, −10.0) | (5.0, −9.0) | room centre after the table moved |
| kitchen | (4.0, −14.6) | (4.6, −13.9) | off the counter envelope |
| master | (−3.6, −14.2) | (−3.4, −14.1) | off the bed envelope (the Safe's grab side) |
| masterBath | (−2.5, −16.0) | (−2.6, −15.7) | inside the room as built |
| br2 | (−3.6, −10.2) | (−3.4, −10.2) | off the bed envelope |
| linen | (2.8, −14.5) | (3.0, −14.05) | upHallN↔linen line clearance 0.323 m |

Full table as built (verified against `NodePositions()`/`NodeFloors()`, 14/4/8 by floor):

- **f0:** van (0, 5), doorOut (0, −5), doorIn (0, −8.6), entryHall (0.3, −9.5),
  living (−3.2, −10.8), dining (5.0, −9.0), kitchen (4.6, −13.9), hallSouth (0.9, −12.9),
  hallNorth (0.4, −15.9), master (−3.4, −14.1), masterBath (−2.6, −15.7),
  stairFootG (−0.7, −11.35), stairHeadB (−0.7, −14.6), screenPass (−0.7, −15.8)
- **f−1:** stairFootB (−0.7, −11.35), baseOpen (−3.8, −11.8), baseDoor (3.5, −12.7),
  utility (4.5, −15.0)
- **f1:** stairTopG (−0.7, −15.5), upHallN (0.5, −15.6), upHall (0.4, −11.5),
  br2 (−3.4, −10.2), br3 (−3.6, −14.8), bath2 (4.8, −15.0), office (4.0, −10.5),
  linen (3.0, −14.05)

### 8.2 Structure: decks, holes, railings

- Ground deck [−0.15, 0], hole x −1.25…−0.15, y −11.05…−14.80. Second-floor slab
  [2.59, 2.74], hole x −1.25…−0.15, y −15.03…−12.20. Roof [5.18, 5.33].
- The stairwell **screen** is one solid: x −0.20…−0.15, y −15.03…−12.20, span [0, 3.74],
  tagged floor 0. Its top metre IS the second floor's east-lip railing — a deliberate
  lie in the floor tag that the physics honours and floor-filtered queries cannot see
  (§8.3).
- Railings, three-piece: the f0 low railing (span [0, 1.0], y −12.20…−11.05) around the
  basement head; the f1 slab-hole south lip (span [2.74, 3.79], same y) — open above
  3.79 so a standing body on f1 has the cross-floor sight line §7 asks for; and the
  screen above.
- Flights G and B share the plan footprint x −1.20…−0.20: G runs (−0.70, −11.10) h 0 →
  (−0.70, −15.03) h 2.74; B descends from the same head to the basement. The x −1.2
  partition's living-room doorway measures y −11.2…−10.0 (1.2 m).

### 8.3 WalkBlockedBody — the routing oracle's twin, and why floor tags lie

`WalkBlocked` stays radius-0 by pinned contract (edge validation and NearestClearNode
speak line clearance). The prepend guard needs a different question — *can the 0.84 m
body sweep this leg* — so `WalkBlockedBody` (file-local, HISim.cpp) inflates by
`ThiefRadius` and gates solids by SPAN OVERLAP ONLY, mirroring `ResolveCircleZ`, with
**no floor-tag filter**. Measured why: the searcher pinned at (0.27, −14.85) f1 was held
by the screen — tagged floor 0 — whose top metre is the f1 east-lip railing.
`WalkBlocked` at floor 1 cannot see the solid that the physics can and does. The lesson
this project keeps relearning, wearing a new face: radius-0 clearance is not body-radius
walkability.

### 8.4 The walker: corner-rounding commitment (runs 15–21 arc)

Head-on contact with a wall used to mean standing (tangential projection under the 0.0025
deadzone): the f1 rim pin measured 220 windowed reroutes in 400 s with the laptop
undelivered while every probe stayed green. The fix is a **commitment**, and its lifetime
rules are measured, not styled — do not simplify them without re-running the battery:

- Side is chosen ONCE, on the first deadzone frame, from the waypoint's side; `CornerDir`
  is FROZEN at pick time (run 19: recomputing Perp from the live push normal rotates
  toward the face inside a corner's push-out and the commitment flipped at both ends — a
  full-pace 2.8 m shuttle, stuck=0.00, E/F0 dead at 400 s).
- Every contact frame steps along it, INCLUDING frames where the projection becomes
  tangential again (run 17: without the commitment the two branches fight into a harmonic
  orbit about br3's y, ±0.19 m, ~196 reroutes in 400 s, zero net travel).
- Probes run 5 cm off the face (at push-out equilibrium the centre sits exactly ON the
  inflated boundary and SegRect is inclusive).
- A walled-ahead/open-behind probe pair flips side AND direction; both-walled stands
  (a real pocket — the windowed detector owns it).
- The commitment survives free frames (run 18: clearing on the corner's one push-less
  frame let the tangential branch march him back up the face), and ends at waypoint
  change or 3 s staleness (~3.9 m of stepping, longer than any face in the house).
- The windowed stuck detector is EXEMPT while the commitment is live (F0: 220 reroutes
  without the exemption).

### 8.5 Search and hunt state

- **Search commits** to its target room until searched; the standing override fires at
  the room node's POP RADIUS (0.6 m), not SearchRange — attribution corrected 2026-10-18:
  three override parameter sets produced byte-identical probe runs (22–24), proving the
  override inert in the stalls it was blamed for. The mark itself fires at SearchRange
  1.2 m + floor match ("plan proximity is not presence").
- **HuntRouteKey v3** (2026-10-18) keeps an at-home item on its birth node when the
  trimmed line (GrabRange − 0.15) from node to item is body-clear, and re-keys to
  `NearestClearNode` otherwise. v1 (distance rule) and v2 (whole-line rule) are recorded
  wrong guesses in the function's comment: ITEMS LIVE ON FURNITURE, so the untrimmed
  living→TV line ends inside the console solid and SegRect's inclusive end test re-keyed
  the TV to "master" — wrong side of a wall — for 330 s and 85 reroutes (run 23). v3
  restored run-21 byte-identity across all eight probe scenarios (run 25).
- **Hunt self-heal**: an empty route mid-hunt rebuilds it (the Shove office round trip
  measured the stall). **PPos re-sync** after a shove clobber (2026-10-18): the walk's
  occupied-waypoint rule reads the player position sampled at step start.

### 8.6 Prepend hysteresis — the doorway ping-pong (2026-10-19)

The last walker fix of the phase, and the one the final soak battery forced out.

**Measurement first (SoloProbe S3 dump): 10 of the 33 flat nav edges are blocked at body
radius, node-to-node** (all ten clear at radius 0 — the pins are honest, the doors are
just narrow): entryHall→living, entryHall→stairFootG, master→stairHeadB,
master→masterBath (f0); baseOpen→baseDoor, baseDoor→utility (f−1); upHallN→br3,
upHallN→linen, upHall→br2, bath2→office (f1). Doors are 0.9–1.2 m, the body is 0.84 m:
the effective gap is 0.06–0.36 m and a node-to-node line crosses it off-centre. Physical
traversal still works — the corner slide centres the body in the gap — but the ROUTING
ORACLE says blocked, and that drove a trap:

blocked leg → prepend guard adds the From node → walker pops it up to 0.6 m short →
re-aim is still jamb-blocked → guard prepends the same node again. Each flip changes
Route[0], which resets WpT and clears the corner commitment — so neither the slide nor
the orbit breaker ever finishes. Two rounds measured it: the br2 door (control probe S2:
112+ s of route=3↔4 with the Laptop never even known, at 240 s) and the living doorway
(probe S: 170 s oscillating in a 0.4 m band, stuck=0.00 throughout).

The first hysteresis cut (2.0 s) fixed S2 and NOT S, and the instrumented rerun showed
why — S's pin is a **three-way race**: SteerAround's orbit deflection (Louise stands on
the living line) aims him north into the jamb while WpT < 2.5; the tangential slide
returns him south; net motion ~0 (yaw 2.93, corner=0 in every sample — a tangential
branch, never the deadzone); and the 1.2 s windowed reroute therefore fires BEFORE the
2.5 s orbit break, at LastPoppedAgo ~2.3–3.0 — past a 2 s window, so the prepend went
through. **The window is 8.0 s**: orbit break (2.5) + slide through the door (~1) with
margin. Scope stays narrow — only re-prepending the From node he JUST popped is
suppressed; the screen-face storm the prepend was written for is unaffected (its pin
forms on a different From, and even suppressed, the documented slide-out covers it).

Results, same build: S2 = 3/3 delivered at **175.0 s**; S = 3/3 at **181.5 s** with
Louise alive and camping; the game-mode solo soak resolves (§7 results) where it was
INCONCLUSIVE before; suite 52/52 with the probe in, 51/51 after its removal.

### 8.7 Louise's spawn as built, and what a lone thief does about it

The spawn is (−2.0, −10.6) f0, yaw 0.45 — the §4.6 note carries Richard's ruling and the
lineage (design (0.3, −9.8); v1 slice (−3, −15.5); as built here). Lineage matters
because OPEN-QUESTIONS 6.5's spawn-kill-zone measurements were all taken at the v1
point.

Measured behaviour of the as-built spawn against a passive lone thief (SoloProbe S, then
the game soak with `-HINoThiefDamage`): he walks to the living doorway, she is in it,
ThiefShove staggers and pushes her 0.54 m west (no damage — shoves never damage), he
rounds the door, marks the room, takes the TV, and runs the full sweep — **3/3 delivered
at 181.5 s, eight shots fired at her along the way, her hp untouched under the
diagnostic**. The damage-live variant was measured too: **hp 78 → 29** (49 damage from
those eight shots, she survives) and the sweep timing bit-identical — 181.5 s, 10890 sim
steps — being shot at does not change his path. Her defence of the doorway costs him
~50 s against the lawn-control's 175.0 s and does not stop him. She is terrain, and
terrain slows without a weapon in her hands — the round only becomes a fight when the
player (or the autopilot) shoots back.
Design question for Richard, reported not patched: OPEN-QUESTIONS item 13.

Also measured here, and since the 2026-10-15 fork pure design questions rather than
parity debts (OPEN-QUESTIONS items 10 and 11): **thief shots do not arm the police
clock** — `Alarm` stayed −1.0 through all eight shots (and 126 in the pinned variant);
only the player's first shot or [P] arms it. And **the player's hp IS `ThiefMaxHp` = 78**
(CreatePlayer): the passive soaks' "hp=78" is untouched full health, not damage.

### 8.8 Recorded, not fixed

- **The screen/B-band funnel**: the stairwell screen plus the B flight make a pocket at
  the north head where a fleeing thief can shuttle. It has never lost a round in any
  measured battery; the prepend-guard doorway ping-pong family (run 24's B2 stall-1 at
  this same br2 door) is now mitigated by hysteresis but the class — a conservative
  oracle against 0.84 m bodies in 0.9 m doors — remains. The data-level answer, if
  Richard ever wants it, is wider doorways or door-centre waypoints; both change the
  house, so neither was taken unilaterally.
- **br2's south side is a dead end**: the y −12.4 partition is solid (no door), so a
  corner commitment picking SOUTH at the br2 doorway slides into a pocket. It does not
  trap (staleness + hysteresis let the north pick eventually win — S2 measured), but the
  asymmetry is real and recorded.
- Furniture-height sight, fall damage, TV-through-the-stair-well: §6, unchanged.
