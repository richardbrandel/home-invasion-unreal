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

## 3. The shotgun mesh — OPEN, a purchase or a keep

Upstream's `Game/Weapons/Rifle/.../SM_Rifle` stands in for it and the code says so. Either a real
`.fbx`/`.glb` arrives, or the stand-in is accepted and the item is struck off. The README calls
this the only asset genuinely still needed.

## 4. Mixamo retargeting — OPEN, scope decision

`assets/mixamo/` holds 28 clips including `crouch_idle` and `look_around`, which fix-list items 14
and 15 are both waiting on. Measured: an animation-only FBX imports to nothing without a target
skeleton, and setting `task.skeleton` stops the run part-way with no error — so this needs an IK
Retargeter or an Interchange pipeline, i.e. a job rather than a file drop. It also decides whether
the men in the game are Unreal's mannequins or the Mixamo characters.

## 5. The playtest's move/fire verdict — OPEN, small

`-HIPlaytest` reports FAIL on move and fire while its ESC step passes in the same run, and the
cause is believed to be the instrument: a synthesized key-down does not survive into
`IsInputKeyDown` across frames, though the press *edge* does. Fix it or delete it? Leaning fix — a
red light nobody believes is worse than no light.

## 6. Player impressions — OPEN, and only Richard can supply them

Camera distance in motion, whether the thief's 0.45 s aim delay feels right, whether the intruders
read as urgent or merely slow, whether the house is findable. He is still the only person who has
ever played this. Three sentences of "this annoyed me" are worth more than a day of captures.

## 7. The mouse — OPEN, and possibly already fixed

`[ESC]` now releases the cursor (the port had captured it with no way back; `game.gd` released on
Escape). Still to establish: did the stuck mouse happen while the game was **starting** or after it
**stopped**? The hatch covers the second; the first would be the game grabbing the cursor during
startup, which is a different fix.

## 8. Does the gun read as straight now — OPEN, and it is item 1 in disguise

The body now faces the aim (measured 0.5 deg, was exactly 90.0). The gun is aimed by construction.
Whether it *reads* as aimed is exactly the framing question in item 1.

---

## Not waiting on anybody

No sudo, no credentials, nothing to install. One wrinkle, recorded in `~/.dsh/AGENTS.md`: a GitHub
push only works when the session runs with full access — under the sandbox the credential silently
reads as a missing keychain item, which cost two attempts before it was spotted.
