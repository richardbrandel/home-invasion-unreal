# Open questions — what is waiting on a person

**This file exists so the list survives the session that produced it.** It was asked for on
2026-10-13: *"Don't lose this list and we will work on these issues one at a time."* Work them
one at a time; strike each through when it is decided, and record the DECISION rather than
deleting the item, because the reason is usually worth more than the outcome.

Ordered by how much each one unblocks. Status is as of 2026-10-13.

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

## Not waiting on anybody

No sudo, no credentials, nothing to install. One wrinkle, recorded in `~/.dsh/AGENTS.md`: a GitHub
push only works when the session runs with full access — under the sandbox the credential silently
reads as a missing keychain item, which cost two attempts before it was spotted.
