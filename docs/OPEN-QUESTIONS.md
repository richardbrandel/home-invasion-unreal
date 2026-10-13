# Open questions — what is waiting on a person

**This file exists so the list survives the session that produced it.** It was asked for on
2026-10-13: *"Don't lose this list and we will work on these issues one at a time."* Work them
one at a time; strike each through when it is decided, and record the DECISION rather than
deleting the item, because the reason is usually worth more than the outcome.

Ordered by how much each one unblocks. Status is as of 2026-10-13.

---

## 1. Camera framing — centre vs seeing your own gun — **ANSWERED, in progress**

**The decision (Richard, 2026-10-13):** *"position the camera exactly behind the defender, but
move it back and up a little bit so that we can see the gun. We did something similar in the
Godot version."*

So: keep `CamSide = 0` (the camera stays on his own line, crosshair in front of him), and gain
the view by moving **back and up** instead of sideways. `-HICamBack` / `-HICamUp` already exist
as dev switches, and a sweep of them is being captured for him to choose from.

**Why "up" is the dimension that matters:** moving straight back along his own axis does not
change the occlusion — his torso is between the camera and anything held in front of his chest.
Raising the camera does, because the sight line then passes over his head and shoulder.

**The cap to respect:** the walls are 2.0 m and the eye is 1.62 m, so `CamUp` above roughly
**+0.38 m** puts the camera *over* the walls and the house becomes a dollhouse — you see the
intruder over cover the minimap correctly hides. This was measured in the Godot original by
rendering candidates, and two of them showed open sky above the walls. Any chosen value must be
checked against that, not assumed safe.

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
