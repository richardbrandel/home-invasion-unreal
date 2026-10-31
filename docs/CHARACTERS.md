# Female intruders — candidates checked, and what the pipeline actually needs

Written 2026-10-31 after pulling five Sketchfab models and measuring each one. The point of this
file is that the next search starts from what has been ruled out.

## What the pipeline needs

`SourceAssets/characters/<tag>/` holding `idle.fbx` (the mesh-carrying export) **plus one FBX per
clip**: `aim`, `shoot`, `reload`, `walk`, `run`, `crouch_idle`, `crouch_walk`, `death`,
`pistol_idle`. Each character gets its OWN `USkeleton`, and clips bind only to the skeleton they
were exported against. A character with a different rig therefore has to be **retargeted** or
**re-rigged** before any of our animation plays on it.

## Candidates measured (all CC BY, downloaded through tools/fetch-sketchfab.py)

| Model | Author | Faces | Joints | Rig family | Verdict |
|---|---|---|---|---|---|
| Miss Galaxy | Loves_Art | 49,788 | 75 (65 `mixamorig:`) | **Mixamo** | Right rig, wrong subject — the atlas is a galaxy-print pageant gown |
| Every, Jane | pinckneyb | 99,045 | 25 | custom | Cartoon casual woman; only 25 joints, no fingers, so the pistol cannot parent to a hand |
| Your College Girlfriend | pinckneyb | 86,149 | 102 (`CC_Base_*`) | Character Creator | Good proportions, wrong rig for our clips; would need a retargeter |
| Girl with clothes (worker set) | cattleya | 2,469 | 68 (`girlBone_*`) | custom | Too low-poly, custom rig |
| Low Poly Female | Loves_Art | 4,384 | — | — | Not fetched; low-poly only |

**Only one of the five is Mixamo-rigged**, and it happens to be dressed as a galaxy. That is the
core problem: Mixamo-rigged CC-BY characters that look like modern women are rare on Sketchfab.

## The two routes that actually produce a playable character

1. **Mixamo auto-rigger** (free Adobe account). Upload any humanoid mesh — Sketchfab, a paid
   store, anywhere — let Mixamo rig it to `mixamorig`, then download it **with the ten clips**.
   That normalises any body to the skeleton our cast already uses. This is the shortest path and
   the one this project's pipeline was built for.
2. **UE IK Retargeter** (in-engine). Bring a `CC_Base_*` or custom rig and retarget the Mixamo
   clips onto it. More setup, and the project has a history of misdiagnosing this area — see
   `tools/import-assets.py`, which records that retargeting is NOT needed for Mixamo-origin
   characters, only for foreign rigs like these.

## Style decision that outranks the asset hunt

The existing cast (Louise, James, Brute, Martha) is **photogrammetry scans of real people**. Every
candidate above is **stylised or cartoon**. Dropping one in beside them will read as two different
games. Decide first: restyle the whole intruder cast as stylised, or find a realistic curvy model
(paid stores are the realistic route — Sketchfab's CC-BY pool is thin on realistic clothed women).

## Open

The in-engine art preview (`-HIModels=`, 2026-10-31) places and scales a candidate correctly but
does not draw it yet — fix that before the next round of candidates, so each one costs one frame
instead of one paragraph.
