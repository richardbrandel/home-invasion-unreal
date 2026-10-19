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

**Refreshed 2026-10-19, post phase 2.** The full three-floor house is now built and
committed (that work is HOUSE-DESIGN scope, not this list); **none of the realism items
below is implemented.** Items 6, 9–12, 16 and 17 carry a `[phase 2 status]` note where
the completed house changed their ground truth, and items 19–28 are new from that build.
Richard's instruction this round: *"realism is the next task — make a list of what you
think needs to be done."*

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

`[phase 2 status]` — now measured, and mostly moot. The solo sweep carries the
Laptop down the flight to the van at full stair pace with zero extra cost
(181.5 s round, HOUSE-DESIGN §8.7). A laptop is light, so 0.7× is arguably
correct for it; the TV and Safe both stay ground-floor in the current loot
layout, so "big loot on stairs" cannot happen today. Re-open only if loot moves
upstairs.

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

`[phase 2 status]` — the railing set is now THREE pieces, not one (basement-head
low railing, second-floor slab-hole south lip, and the stairwell screen's top
metre doubling as the f1 east-lip railing — HOUSE-DESIGN §8.2). All three need
the same baluster/cap-rail visual, and the "blocks sight" fix has to clear the
sight list for all three, not just the one.

### 10. Windows on the second floor

`WallFittings()` derives window holes from the ground-floor walls only (it
asserts flush-against-wall — the `HI.Unreal.WallFittings` test). The slice's
second floor has four blank exterior walls. Phase 2's full-house data
(HOUSE-DESIGN.md §4) already schedules bedrooms and bath with windows; this item
is the actor-layer work to put openings in floor-1 walls, plus the sight
consequence: a window is a sight tube between floors of the *outside* — the
player on the lawn can see into the upstairs rooms, and thieves should too.

`[phase 2 status]` — the second floor now EXISTS (br2, br3, bath2, office, linen,
HOUSE-DESIGN §8.1), so this is no longer "blank walls to come" but "five real
rooms with no windows." Unchanged otherwise.

### 11. Cinderblock basement

Richard's brief: *"the basement will be an open area with cinderblock walls, and
a water heater and a furnace."* Phase 2 adds the geometry; this item is the
material pass — block courses with mortar lines, painted or bare — plus the
appliances as identifiable objects (water heater: white cylinder, flue, pipes;
furnace: grey cabinet, ductwork rising into the ceiling). The two lally columns
in the design are a realism win here too: real basements have them.

`[phase 2 status]` — the basement geometry IS built now (baseOpen, utility, the
mech-room doorway, the B flight — HOUSE-DESIGN §8.1), so this item narrows to
the pure material + appliance pass, exactly as the brief stated. No longer
"geometry to come." The cinderblock WALLS are now real (2026-10-19): a CC0
concrete-block texture (Poly Haven) replaces the plaster stand-in; only the
appliances (water heater, furnace, lally columns) still need models.

### 12. Minimap floor awareness

The minimap and `VisibilityPolygon` run on the ground-floor projection
(`Solids()`), which is correct-but-blind once there are two floors: standing
upstairs, the minimap shows the floor *below* you. Options: (a) filter the
projection to the player's floor and hide furniture/walls of other floors,
(b) keep one map but mark the stairwell and grey out the other floor, (c) two
inset maps. Richard plays the minimap hard — his call.

`[phase 2 status]` — there are now THREE floors (f−1/f0/f1), so the minimap is
blind on two of them, not one. The decision below is now three-way (or a floor
switcher), and it is the item most likely to annoy Richard in actual play.

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

### 16. Stairwell lighting

The stair shaft has no light of its own. Measured from the 2026-10-15 slice
frames: tread luminance ~104 against ~190 for the divider seen through the open
risers — the flight reads as a dark wedge, not as stairs. The nearest bulb is
the kitchen's at 3.2 m west, shadowed by the stringer wall. Real houses light
their stairs (top, bottom, or both — often on a 3-way switch). One lamp entry at
the stair head fixes it; the second floor already proved the slab carries light
correctly.

`[phase 2 status]` — re-measure before trusting the old number: the 14-lamp pass
added a hall lamp "between the stairwell walls" (0.65, −14.25) and an
upstairs-hall lamp (0.45, −13.50). Whether the flight still reads as a dark wedge
is now a fresh capture, not a given.

### 17. The over-the-shoulder camera fights the railings

The camera rides ~1.2 m behind the player and its collision probe treats the
stairwell railings as walls. Standing at the second-floor hole's edge, the camera
was pulled to 1.24 m west of the player and framed a blank wall instead of the
stairwell (measured, 2026-10-15 landing capture). Options: let the camera probe
ignore railing solids (walking collision is the sim's job, the railings stay
drawn), or shorten the arm near ledges. This is game-feel, and it will bite every
player who goes upstairs.

`[phase 2 status]` — the railing set is now three pieces (see item 9), so the
camera probe has three places to fight instead of one: the basement head, the
second-floor slab hole, and the screen. The fix should exempt ALL railing solids
from the probe in one rule, not per-piece.

### 18. The pitch clamp hides the player's own feet on stairs

Pitch is clamped to −0.3 rad (−17°), inherited from the Godot parity: at the top
of the flight the whole staircase projects BELOW the frame — you cannot look down
at the treads you are standing on (measured, 2026-10-15 startop capture). With
stairs now in the game, looking down matters. Richard's call: widen the clamp
(−45° is typical for this camera style), or keep it and accept blind stair tops.

---

## My phase-2 additions (2026-10-19)

These came out of building and watching the full three-floor house. Same style:
a measured fact, a proposal, a decision for Richard.

### 19. Footsteps — nobody has any

There is no footstep audio at all; the only movement-adjacent sounds are the shot
and the `Impact` thud (item 20). The player's own footsteps are the single
biggest "am I in a real house" cue, and a thief's footsteps on the stairs are a
gameplay cue — you hear him coming up. Proposal: footstep samples per surface
(wood, carpet, tile, concrete), tied to the walk/run cycle; the per-room floor
finish (item 24) is the surface map. The sim already knows every body's speed and
floor; only the sound event and the surface mapping are new.

### 20. The impact/flesh sounds are placeholders

`FHIAudio::Impact` (HIAudio.cpp:305-308) plays the Godot `impact.wav` — the
comment calls it "the old stand-in, and audibly a placeholder once you have the
real one." It already splits `bFlesh` (bullet-on-body vs bullet-on-wall, different
pitch/volume); it just needs two real samples. Cheap and self-contained.

### 21. The night house is silent between shots

Between gunshots the house is dead silent — no exterior bed (distant traffic,
wind), no interior bed (house settling, appliance hum). One looping exterior bed
plus one interior bed, with the interior bed attenuated when the listener is
across a deck (reuses item 7's through-floor model), is the whole thing.

### 22. Searching thieves read as pacing, not searching

The `look_around` clip was never imported (OPEN-QUESTIONS item 4), so a thief in
the search phase plays the `walk` clip — he looks like he is pacing a hallway,
not hunting. Importing `look_around` (or a head-scan) makes the search phase
legible: "he is looking for me." The clip set is otherwise complete — aim, walk,
run, crouch_idle/walk, idle, pistol_idle, reload, shoot, death (HICharAnim.cpp).

### 23. Loot is carried, not held

Loot "rides the hand bone" (README); there is no carry pose or held prop. The
carrier is the round's centre of attention once the grab happens — measured ~78 s
of the 181.5 s solo round (43%) is spent carrying (TV 17.6 s, Safe 36.5 s, Laptop
23.5 s). A two-handed carry pose for the TV, one-handed for the laptop, and the
prop actually in-hand rather than teleported to the hand bone, is the fix.

### 24. Interior finishes

Beyond the basement's cinderblock (item 11), the house reads as flat colour.
Drywall/wallpaper walls, wood floor (living/master), carpet (bedrooms), tile
(baths/kitchen), baseboards and door casings. The per-room floor finish is also
the surface map item 19's footsteps key off, so these two items ship together.

`[2026-10-19 status]` — the big surfaces are now photoreal (CC0 from Poly Haven):
plaster walls, oak wood floor, tile floor, grass lawn, grey roof tiles. Still
flat: carpet (bedrooms), baseboards, door casings, and the ceiling.

### 25. The house exterior

Item 2 is the NEIGHBORHOOD; the house itself is still a mass with cut-out
openings. Siding or brick cladding, roof shingles and the gable, gutters/fascia,
a real front door and porch light. The player stands on this threshold every
round, and the thieves' drop view frames it.

`[2026-10-19 status]` — the roof and the lawn are now photoreal (CC0); siding,
gutters and a real front door are still pending.

### 26. Muzzle flash and shell casings

The shot is a discrete event with a position, but produces no light and no brass.
At night the player's own muzzle flash should be a light event (ties to item 14's
"muzzle flash as a light event" note), and ejected casings a few static meshes
that despawn after seconds. One emissive flash mesh + one casing prop + a spawn
rule.

### 27. Blood where the round actually hit

OPEN-QUESTIONS item 6.3 already owns this: five blood decals, bone offsets
provisional. Listed here so the realism pass does not forget it — blood should
appear where the bullet lands, not at a stock offset. Small, but it is the only
feedback the player gets that a hit connected.

### 28. A non-lethal hit produces no reaction

Death is handled — `death` clip + death latch (HICharAnim.cpp:278,
HIGameMode.cpp:1419). But there is no hit/flinch clip in the set (verified), so a
shot that does NOT kill leaves the intruder walking in his current pose until the
killing blow. A flinch clip is the gap; whether Richard wants it is a feel call
(a flinch on every graze can read as stutter).

---

## Suggested order (if Richard wants one)

1. **Item 5** (furniture-height sight) — cheapest, biggest gameplay-feel change,
   machinery already built.
2. **Items 19 + 20 + 21** (footsteps, impact sounds, ambient bed) — the audio trio,
   all no-sim-risk, and audio is most of "feels real" for the least effort.
3. **Item 1** (furniture models, sofa/bed/TV first) — the thing Richard named,
   and the living room is where players look.
4. **Item 3** (van) — named, cover-art object, self-contained.
5. **Item 2** (neighborhood shell) — named, mostly static scenery.
6. **Item 4** (falls) — small mechanic with big tactical consequence, needs the
   lethality decision first.
7. Everything else in the order Richard's taste dictates.

None of this changes the sim's determinism contract — every proposal above is
either visual-only or a data rule in the same style as the existing ones.
