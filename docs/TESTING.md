# TESTING — what actually finds bugs in this port

Asked directly: *"can you find a testing tool/agent that will save my time in finding obvious
bugs."* This is the answer, and it is mostly a record of one idea that does not work.

## The short version

| Approach | Verdict |
|---|---|
| Local vision-language model as an automated reviewer | **Tried, does not work.** Proven by a positive control it fails |
| Golden-image diff of the scripted tour | **Works.** Verified with a positive control |
| Deterministic invariants in the test suite | **Works**, and has found every real bug so far |
| The `-HIPlaytest` input harness | Built; does not yet inject keys successfully |
| A person looking at eight frames | Still the only thing that catches "this looks wrong" |

## 1. The vision model, and the control that kills it

`mlx_vlm` is installed on this Mac, so the obvious idea was available immediately: a local
vision-language model that looks at a captured frame and reports what is broken. Free,
offline, no API cost. It was set up and run over all eight tour frames.

**It answered "Nothing wrong" for all eight.** Including a frame with a known visual
artefact. That is exactly the "check that cannot fail" shape this project keeps having to
guard against, so the next step was a **positive control** rather than a conclusion:

```
                      Qwen2.5-VL-3B   Qwen2.5-VL-7B
solid magenta frame   NOTHING WRONG   NOTHING WRONG     <- not a house at all
frame + giant black
box drawn over it     NOTHING WRONG   "Clipping through walls."
```

The magenta frame is one flat colour. **No model that answers "NOTHING WRONG" to that is
evaluating the image**, whatever it says elsewhere. The 7B model is more sensitive — it does
notice the injected defect — but it still fails the control, so its answers cannot be
trusted unconditionally.

**It is not blind.** Asked to describe rather than to judge, the 3B model read a tour frame
back accurately — "a character in a first-person perspective with a pistol and a helmet,
facing two enemies in a room with a green door", and it read the HUD's numbers correctly. So
the plumbing works and the vision works. What does not work is the open-ended
*"what is wrong here?"* question: the models default to the reassuring answer.

**Rule if it is ever used again: put a control frame in front of it every single time.**
An agent whose failures are indistinguishable from its successes is worse than no agent, and
the only defence is a frame with a known, planted fault.

## 2. What does work: a diff needs no judgement

`tools/tour-diff.py`. Capture the tour, compare against an approved baseline, print only what
moved, and write a red overlay of the changed region. It cannot say what is *correct* — but
"the thing the intruder carried got huge" is a **change**, and a change is exactly what a
diff detects without needing to understand the picture.

Verified with a positive control, which is the whole point:

```
baseline vs itself                     0 of 8 frames differ     exit 0
one frame replaced with a planted bug  1 of 8, 33% of pixels, correct bounding box   exit 1
```

It exits non-zero when anything differs, so it can gate a build. Run `--update` once to
accept a tour as the reference, then re-run after each change to the world.

Use it as: change the house, re-capture, and look only at the frames that moved.

## 3. Deterministic checks, which have a better record here

Every real bug in this port has been found by a *check*, not by looking:

- 47 of 47 sim constants identical to Godot — because they were machine-diffed, not eyeballed
- `thief_shove` was never called — found by grepping for call sites of a tested function
- the attenuation, the sound class and the submix were not GC-rooted — found by a test that
  asserts `IsRooted()`, whose negative control is *removing the fix and watching it fail*
- the mark orientation bug — found by reading the geometry against Godot's

The lesson generalises: **when something is noticed from play, the useful follow-up is not a
better observer, it is an invariant.** "He was carrying something enormous" becomes "every
prop's world bounding box is within X of Godot's dimensions", which is a test.

## 4. The input harness, and why it still matters

`-HIPlaytest` exists because `-HIAuto` calls `ReadInput()` and then **overwrites it**, so no
test in this project's history had ever executed the code a keyboard goes through — which is
how a dead input path shipped twice.

The harness injects real key events with `FInputKeyEventArgs::CreateSimulated` and asserts
that the body moved and the magazine dropped. **It does not work yet:** the injected keys do
not reach `IsInputKeyDown`, so it reports FAIL and proves nothing either way. That is recorded
rather than hidden — an instrument that cannot observe is not evidence.

## 5. The honest ranking

1. **Deterministic invariants**, especially parity assertions against the Godot original. Best
   signal per unit of effort, and the only category with a clean record here.
2. **Golden-image diff** for catching unintended visual change. Verified working.
3. **A person, on eight frames rather than an hour of play.** `tools/tour.sh` exists to make
   that cheap.
4. **A local VLM**, only ever behind a planted-control frame. Useful as a narrator.
