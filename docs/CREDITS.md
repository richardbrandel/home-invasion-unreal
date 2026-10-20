# Asset credits

The scanned furniture (sofa, armchair, dining set, coffee table, nightstands,
bookshelf) comes from **Poly Haven** (CC0, no attribution required) — see
`tools/fetch-models.py`.

The vehicles and trees below come from **Sketchfab** under **CC BY** (Creative
Commons Attribution). Attribution is required; commercial use is allowed.
Downloaded via `tools/fetch-sketchfab.py` (needs a Sketchfab account).

| File | Model | Author | License |
|---|---|---|---|
| `SourceAssets/models/van.glb` | 2003 Chevrolet Express GMC Savana 2500 Cargo Van | ddiaz-design | CC BY |
| `SourceAssets/models/car.glb` | Low Poly Car — Cadillac 75 Sedan 1953 | roh3d | CC BY |
| `SourceAssets/models/pickup.glb` | Low Poly Car — Chevrolet C10 Pickup 1963 | roh3d | CC BY |
| `SourceAssets/models/tree1.glb` | oak trees | bumstrum | CC BY |
| `SourceAssets/models/tree2.glb` | Pine Tree — Proto Series — Free | bitgem | CC BY |

Also downloaded (not yet wired into the scene):

| File | Model | Author | License |
|---|---|---|---|
| `SourceAssets/models/bed.glb` | Bed | rerwandi | CC BY |
| `SourceAssets/models/fridge.glb` | Not-too-modern fridge | anDDDres | CC BY |
| `SourceAssets/models/toilet.glb` | Fast Armored Toilet | destructful67 | CC BY |
| `SourceAssets/models/tub.glb` | Bathtub | roughdominator | CC BY |
| `SourceAssets/models/washer.glb` | Washing Machine | re1monsen | CC BY |

Model URLs are the Sketchfab pages reachable by searching the model name; the
`.glb` files carry the exact download. To refresh or re-download, run
`SKETCHFAB_TOKEN=… tools/fetch-sketchfab.py "<search>" <name> <max_faces>`.
