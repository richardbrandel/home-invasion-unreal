# Open questions — what is waiting on a person

**This file exists so the list survives the session that produced it.** It was asked for on
2026-10-13: *"Don't lose this list and we will work on these issues one at a time."* Work them
one at a time; strike each through when it is decided, and record the DECISION rather than
deleting the item, because the reason is usually worth more than the outcome.

Ordered by how much each one unblocks. Status is as of 2026-10-13; items 9–13 were added
2026-10-19 when phase 2 (the full three-floor house) completed.

---

## 1. Camera framing — centre vs seeing your own gun — **DECIDED, and then MEASURED**

**The decision (Richard, 2026-10-13):** *"position the camera exactly behind the defender, but
move it back and up a little bit so that we can see the gun. We did something similar in the Godot
version."* He picked **back 1.60 / up 0.44** from a four-way rendered comparison.

**Then he asked for the minimum rather than another round trip:** *"why don't you vary the back and
up distances, run it and find what the minimum back and up values are that will still allow you to
see the gun."* The measurement overturned the premise and the default is now **back 1.30 / up
0.44**:

- **UP DOES NOTHING ON ITS OWN.** At back 0.90, rendered with up 0.22, 0.30, 0.38 and 0.44, the gun
  is invisible at **every** value. Raising the camera on his own axis does not move the sight line
  past his chest — it pushes the whole man, and the gun with him, down out of the bottom of the
  frame.
- **BACK IS THE LEVER.** Swept at fixed up 0.44: nothing at 1.10, the arm and pistol separating
  from his silhouette at **1.30**. That is the smallest value that works in the **worst** pose —
  standing, pistol tucked against the chest — so it holds in motion.
- The camera sits at 2.05 m against 3.0 m walls.

`CamSide` stays **0** — the camera remains on his own line, so the crosshair is in front of him.

**Re-measured with the Mixamo cast, 2026-10-13:** Louise's un-helmeted head and smaller build
occlude far less than the mannequin's. Swept at back 0.9/1.0/1.1/1.2/1.3 (posed worst case, up
0.44): the pistol and arm are separable from her silhouette at **every** value — `shots/camsweep-sheet.png`.
The default stays 1.30 because "visible on one static pose at sheet scale" is a floor, not a
preference; if Richard wants the tighter framing he originally preferred, `-HICamBack=1.0` tries it
live with no rebuild, and the constant is one line in `HISim.h`.

**AND THE CAMERA HEIGHT IS NOW LOAD-BEARING FOR BALLISTICS, not just framing** — measured the hard
way on 2026-10-13: the player's shot is a world trace from the camera (that is what makes the
crosshair honest), so the ray flies at `EyeHeight + CamUp` = **2.06 m**, and the intruder's hit
capsule tops out at exactly **2.0 m**. Level fire passes over every standing man's head. A human
never notices — putting the crosshair on a target means pitching down — but the driven soak's
autopilot aimed yaw only, missed 12 of 12, and the round ended LOST in 3.3 s on intruder fear.
The autopilot now aims pitch at the chest (see its comment); the constants stay as Richard chose
them. **If CamUp is ever raised again, remember a higher camera raises the shot ray with it.**

**A correction worth keeping, because it nearly ruled out the answer.** This file first warned that
`CamUp` was capped at about **+0.38 m** by 2.0 m walls — a number INHERITED FROM THE GODOT PROJECT.
Measured here: `HISim.h` has `WallHeight = 3.0f`. Every candidate was safe. A number from the other
project is not a measurement of this one.

**And the instrument built to answer this numerically does not work.** `-HIProbe`'s `gunSight`
traces camera → muzzle and asks whether the body is what the ray meets. It reported `muzzle=clear`
at **every** setting, including 0.90/0.22 where the gun is provably invisible, because the player's
body carries `Body->SetCollisionEnabled(NoCollision)` — **the trace physically cannot hit it**. It
is kept only for world occlusion and its log line now states what it cannot see. This was the
fourth check-that-cannot-fail in this project; the pictures are what settled it.

## 2. What "working" means — OPEN, the most valuable one

The README's *"What is still open"* has about ten items. "The game is playable" (2026-10-13) is
the first finish-line signal the project has had. Without a stated line, effort goes into polish
that may not matter. **Needs one sentence: working means X, Y and Z and nothing else matters.**

## 3. The shotgun mesh — the stand-in is RETIRED 2026-10-14; what remains is a keep-or-replace judgement

~~Upstream's `Game/Weapons/Rifle/.../SM_Rifle` stands in for it and the code says so. Either a real
`.fbx`/`.glb` arrives, or the stand-in is accepted and the item is struck off. The README calls
this the only asset genuinely still needed.~~

A real `.glb` arrived — written, not bought. `tools/make-shotgun-glb.py` composes eight boxes and
two cylinders into a plain glTF binary (stdlib Python, deterministic, self-verifying) and the
ordinary import turnstile turns it into `/Game/HIImported/weapons/shotgun`: 100.5 cm overall,
grip-centre pivot, flat gunmetal/wood/synthetic colours in the same visual language as every
primitive prop in the port. Measured at import: bounds origin (0.0, 7.2, 3.6), extent
(2.8, 50.2, 9.6) cm — longest along +Y, muzzle positive, exactly the `HI.Unreal.Weapons`
contract, 41/41 green. Frame review 2026-10-14 (`shots/shotgun-sheet.png`): grip seated in the
palm, barrel along the aim, proportions believable, no defects — and the pistol aim clip's raised
left hand lands on the forend, so it reads as a proper two-handed hold by accident.

The first version was wrong and the contract caught it: the importer's axis mapping was guessed
(barrel along glTF +X) and the mesh landed longest along UE **+X**; the answer was then read out
of the engine's own `InterchangeGltfTranslator.cpp` (X→X, Y→Z, Z→Y, no sign flips) instead of
guessed twice, and the generator carries the rotation — and the reason the det+1 rotation was
used rather than the mirror that maps just as well (winding, i.e. an inside-out invisible gun).

**What remains is Richard's call:** keep the primitive, or replace it with an artist's mesh. The
upgrade path needs no GUI and has not changed: a better `.fbx`/`.glb` dropped into
`SourceAssets/weapons/`, one run of `tools/import-assets.sh`, one path in `SetWeaponVisual`. See
it with `-HIWeapon=shotgun` (or [2] in-game) before deciding.

## 4. Mixamo retargeting — **RESOLVED 2026-10-13: there was nothing to retarget**

~~`assets/mixamo/` holds 28 clips including `crouch_idle` and `look_around`, which fix-list items 14
and 15 are both waiting on. Measured: an animation-only FBX imports to nothing without a target
skeleton, and setting `task.skeleton` stops the run part-way with no error — so this needs an IK
Retargeter or an Interchange pipeline, i.e. a job rather than a file drop. It also decides whether
the men in the game are Unreal's mannequins or the Mixamo characters.~~

**The decision:** the retargeting "wall" was a misread error — mesh-carrying FBX exports import
directly, and each character export carries its own skeleton, so clips bind per character with no
IK Retargeter at all. All four characters (louise the homeowner; james, brute, martha the
intruders) are imported, wired, normalized at import time by `UHIAnimTool` (root motion pinned out
per clip mode — the sim owns positions), and asserted by `HI.Unreal.Cast`. The mannequins are out
of the game. What the clip set still does NOT include: `look_around` was never imported, and the
search behaviour plays `walk` — if Richard wants a distinct searching pose, that is one more FBX
in `SourceAssets/characters/<tag>/` and one import run, not a job.

## 5. The playtest's move/fire verdict — OPEN, small

`-HIPlaytest` reports FAIL on move and fire while its ESC step passes in the same run, and the
cause is believed to be the instrument: a synthesized key-down does not survive into
`IsInputKeyDown` across frames, though the press *edge* does. Fix it or delete it? Leaning fix — a
red light nobody believes is worse than no light.

## 6. Player impressions — OPEN, and only Richard can supply them

Camera distance in motion, whether the thief's 0.45 s aim delay feels right, whether the intruders
read as urgent or merely slow, whether the house is findable. He is still the only person who has
ever played this. Three sentences of "this annoyed me" are worth more than a day of captures.

**The 2026-10-13 cast swap added a short, concrete list:**
1. **Camera distance**: the gun is visible down to back 0.9 with Louise — does he want the tighter
   framing now (`-HICamBack=1.0` tries it with no rebuild; the constant is one line)?
2. **The crouch**: `crouch_idle`/`crouch_walk` play on [C], but no capture switch can press a key —
   does the lowered walk read correctly at 2 m/s?
3. **Wound placement**: the five blood decals' bone offsets are provisional (placed from measured
   bone child-directions, not from looking at each body). Do any sit inside the body or float off it?
4. **The intruders' gait**: stride-matching is per character (Brute's clips travel 25% further —
   measured), but only a human can say whether all three walk believably at ThiefSpeed 1.3 m/s.
5. **The spawn kill zone — RESOLVED 2026-10-15 (Richard: too brutal, move her inside).**
   Measured 2026-10-14 while chasing bug 21: a homeowner who does NOTHING is at **HP 57 by
   t=3.0 s** and dead between **8 and 12.5 s** with three intruders. The cause was geometry,
   not tuning — Louise spawned at (0, -10.6), the far end of a straight sight channel from the
   intruders' Drop (0, 4.5) through the door gap (x -1..1) and the hall gap (x -2..2), so the
   crew could put rounds on her from the driveway the instant their cooldowns lapsed. Richard's
   ruling: "the beginning is so brutal ... the fender ... is standing in the door way while the
   intruders are coming at her. I think she should be more inside the building." Two changes
   landed, both deliberate divergences from Godot parity recorded in PARITY.md §13:
   - **Opening hold-fire** — `ThiefOpeningCd 4.0 s` + `ThiefOpeningStagger 0.7 s`, so first
     shots come at 4.0 / 4.7 / 5.4 s instead of Godot's flat 1.2 s. At the OLD spawn this moved
     the passive idle-death from 8.1 s to 10.87 s (soak, `shots=0 alive=3`).
   - **Deeper, off-axis spawn** — Louise now starts at **(-3, -15.5)** in the back zone, facing
     the hall gap. The InteriorZ divider stops every Drop lane's sight line (they cross y=-8 at
     x=-1.6 to -2.2, west of the door gap), so the crew must come inside and through the hall to
     reach her — the driveway snipe is gone by geometry, not by a damage number. The
     three-intruder heist still clears the house (72.8 s in `HI.Sim.Round`); the passive
     idle-death soak from the new spawn measured **36.4 s** (up from 10.87 s at the old spawn,
     and 8.1 s before the hold-fire), all three intruders still alive, no loot delivered,
     `shots=0` — the crew now walks in and comes through the hall before it gets a line on her.
   - **Update 2026-10-19 — every number above is doubly stale: the spawn moved AGAIN for
     phase 2.** The full three-floor house put Louise at **(−2.0, −10.6), inside the living
     room, 0.8 m from its doorway** — Richard's ruling, "defender by position, not by
     script" (rationale in HOUSE-DESIGN §4.6/§8.7). The kill-zone finding was re-measured
     at the as-built point and inverted: the Drop sight line now crosses the south wall at
     x −1.64 (buried — no driveway snipe), while her own line to the entry hall runs
     through the 1.2 m living doorway (y −10.22). The hold-fire and ThiefShove-no-damage
     rulings survive unchanged. What a lone thief does about a camping Louise — shoves
     past, fires eight shots, completes the full 3/3 sweep at 181.5 s with her hp untouched
     under the `-HINoThiefDamage` diagnostic — is measured in §8.7 and raises item 13.

## 7. The mouse — OPEN, and possibly already fixed

`[ESC]` now releases the cursor (the port had captured it with no way back; `game.gd` released on
Escape). Still to establish: did the stuck mouse happen while the game was **starting** or after it
**stopped**? The hatch covers the second; the first would be the game grabbing the cursor during
startup, which is a different fix.

## 8. Does the gun read as straight now — OPEN, and it is item 1 in disguise

The body now faces the aim (measured 0.5 deg, was exactly 90.0). The gun is aimed by construction.
Whether it *reads* as aimed is exactly the framing question in item 1.

**Re-measured with the Mixamo cast, 2026-10-13:** `-HIProbe` reports the body **2.0–3.3 deg off the
crosshair** (the residual is the aim clip's torso sway, not a systematic offset — the bind-pose
facing constant itself is measured at 90° from the shoulders and asserted by `HI.Unreal.Cast`),
barrel-vs-aim **0.0 deg**, gun roll **2.0 deg** from upright. A frame review of the posed capture
found the pistol gripped in the right hand in a natural aiming stance. The instruments say it is
straight; whether it LOOKS straight in motion is still only Richard's call, in item 6.

---

## 9. PoliceTime 240 → 180 — MEASURED, Richard's to ratify

Added 2026-10-19. HOUSE-DESIGN §4.4 proposed raising the police clock from the v1 value
to 180 s for the nine-room house; **the code already ships `PoliceTime = 180.0f`**
(HISim.h) so the phase-2 numbers below were all measured against it. The final-build
measurements, every one `ended=round` (a resolved game, not a budget expiry):

| round | time | note |
|---|---|---|
| solo clean sweep, Louise parked on the lawn | **175.0 s** | sim probe, no interference |
| solo clean sweep, Louise alive and camping the living room | **181.5 s** | probe AND game soak, bit-matching; the shove detour costs ~6.5 s |
| trio sweep, passive Louise | **117.9 s × 3** | bit-identical (7076 sim steps every round) |
| driven (autopilot Louise) | WON at 24.8 / 25.9 / 26.1 s | the aimbot, balance-silent by design |

The design's promise — "keeps a full clear possible but not comfortable" — measured true
to the second: 5 s of margin on the clean solo clear, and the camping variant finishes
1.5 s past the horn. One interaction softens the horn's bite and is itself item 10: with
thief-only gunfire the police never arm at all, so 180 s only becomes real when the
player fires. **Decision wanted: ratify 180 (one line keeps it), or name a number.**
Until then the constant stays 180 and every soak above documents it.

## 10. Thief gunfire never calls the police — measured; since the fork, a pure design question

Added 2026-10-19. The alarm clock (`Alarm`, starts at −1) arms on the PLAYER's first shot
or the [P] key only. Measured: probe S ran 181.5 s with eight `thiefShot` events and
`alarm=−1.0` in every sample; the pinned pre-fix variant fired 126 shots over 240 s with
the same reading. Under the 2026-10-15 fork (PARITY.md) whatever Godot did is no longer
normative — this is simply what the game does, and whether it is what the game SHOULD do
is Richard's call: a house full of gunfire the neighbours cannot hear is a design
statement. Arming on thief shots too would make the 180 s horn bind the solo sweep (see
item 9's numbers: the camping clear finishes at 181.5 s). Reported, not patched.

## 11. The player's hp IS ThiefMaxHp (78) — measured; since the fork, a pure design question

Added 2026-10-19. `CreatePlayer` sets `P.Hp = ThiefMaxHp` (78.0f): Louise and the
intruders share one health constant. This corrected a wrong inference mid-session — the
passive soaks' `hp=78` is FULL health (they took zero damage: suppressed or never hit),
not "22 damage survived". The driven soaks show real damage working (hp 64/64/71 = 14/14/7
taken in ~25 s against three shooters). Whether Godot gave the player separate hp is
unverifiable from this box and, post-fork, beside the point: if Richard wants the defender
tougher or frailer than a thief, it is one constant — but say so deliberately, because
today the equality is silent.

**RESOLVED 2026-10-19 (Richard: "double the life of Louise, she dies too early").** The
item's premise was wrong on one more count: the constants were NAMED backwards — the player
used `ThiefMaxHp` (78) while a thief uses `ThiefHealth` (100), so Louise was actually
WEAKER than a thief. The constant is renamed `PlayerMaxHp` and doubled to **156**, and the
HUD now scales off it (it had also been hardcoding /100, so the bar never showed full).
Louise now carries 1.56x a thief's health instead of 0.78x.

## 12. Ten of 33 door edges are narrower than a thief's body — fixed in the walker, data-level fix is Richard's call

Added 2026-10-19. The phase-2 diagnostic dump measured **10 of the 33 flat nav edges
body-radius-blocked node-to-node** (all ten clear at radius 0; list in HOUSE-DESIGN §8.6):
doors are 0.9–1.2 m, the body is 0.84 m across, so node-to-node lines cross the effective
0.06–0.36 m gaps off-centre. Traversal works — the corner slide centres the body — but the
routing oracle disagrees with the physics, and that mismatch produced the prepend
ping-pong trap (§8.6, fixed with hysteresis, whole suite green). The honest data-level
alternatives, if Richard wants the graph to mean what it says: widen the doorways to
≥ 1.3 m, or add door-centre waypoints to the ten edges. Both change the house or the
graph's shape, so neither was taken unilaterally. As built, the walker carries the fix.

## 13. A camping homeowner is terrain, not a defender, against a lone thief — measured

Added 2026-10-19. The phase-2 spawn (§8.7, item 6.5's update) puts Louise 0.8 m from the
living doorway — she commands the most-used entrance on the ground floor. Measured against
a passive lone thief: he shoves past (two shoves, t=28.7/31.3; shoves stagger and push but
never damage, per the standing ruling), fires eight shots that never connect in the sim
probe, and completes the full 3/3 sweep — **181.5 s vs 175.0 s with her parked on the
lawn: her defence of the doorway costs him ~6.5 s and does not stop him**. In the sim
probe thief shots carry no damage at all (damage is applied in the game module on events);
in the suppressed game soak her hp stayed 78 by switch. **The damage-live solo variant
has since been measured: `player_hp=29` at the end — eight shots for 49 damage, she
survives — and the sweep timing is bit-identical to the suppressed run (181.5 s, 10890
sim steps): being shot at does not change his path.** The round only
becomes a fight when the player shoots back. Is "defender by position" meant to read this
way with an AI-only Louise? If not, the lever is hers (she never fires — hold-fire covers
only the opening 4 s of the thieves, and she has no trigger logic of her own), not the
spawn. Reported, not patched.

---

## Not waiting on anybody

No sudo, no credentials, nothing to install. One wrinkle, recorded in `~/.dsh/AGENTS.md`: a GitHub
push only works when the session runs with full access — under the sandbox the credential silently
reads as a missing keychain item, which cost two attempts before it was spotted.
