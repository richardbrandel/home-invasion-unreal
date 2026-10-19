#!/usr/bin/env python3
"""Download Poly Haven CC0 models (glTF + bin + 1K textures) into SourceAssets/models/.

No account, CC0, direct download. Used by the asset pass (2026-10-19): the procedural
furniture read as boxes, so the hero pieces are swapped for real scanned models. Each model
lands in SourceAssets/models/<name>/ as <name>_1k.gltf + <name>.bin + its 1K jpg maps, and
tools/import-assets.sh turns them into /Game/HIImported/models/<name>/... uassets.
"""
import json
import os
import sys
import urllib.request

MODELS = [
    "Sofa_01", "ArmChair_01", "dining_table", "dining_chair_02",
    "modern_coffee_table_01", "ClassicNightstand_01", "modern_wooden_cabinet",
    "wooden_bookshelf_worn", "Television_01",
]

DEST = "/data/projects/HIUnreal/SourceAssets/models"


def files(model):
    import subprocess
    raw = subprocess.run(
        ["curl", "-fsSL", "--max-time", "30",
         f"https://api.polyhaven.com/files/{model}"],
        capture_output=True, text=True, check=True).stdout
    return json.loads(raw)


def urls(model):
    """The .gltf (1k), its .bin, and the 1K jpg maps the gltf references."""
    data = files(model)
    found = {}
    def walk(o):
        if isinstance(o, dict):
            for k, v in o.items():
                if k == "url" and isinstance(v, str):
                    s = v
                    if s.endswith("_1k.gltf"):
                        found["gltf"] = s
                    elif s.endswith(f"{model}.bin"):
                        found.setdefault("bin", s)
                    elif "/jpg/1k/" in s and any(t in s for t in
                            ("_diff_1k.jpg", "_nor_gl_1k.jpg", "_roughness_1k.jpg", "_arm_1k.jpg")):
                        found.setdefault("tex", []).append(s)
                else:
                    walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)
    walk(data)
    return found


def get(url, dest):
    # dl.polyhaven.org 403s urllib's user-agent but serves curl fine; go through curl.
    import subprocess
    subprocess.run(["curl", "-fsSL", "--max-time", "120", url, "-o", dest], check=True)


def main():
    for model in MODELS:
        u = urls(model)
        if "gltf" not in u:
            print(f"{model}: NO GLTF", file=sys.stderr)
            continue
        # Flat, not a subfolder: import-assets.py flattens subfolders anyway
        # (models/Sofa_01/... -> models_Sofa_01), and the flat couch.gltf imports cleanly.
        # The gltf is renamed to drop the "_1k" so the mesh is <model>.<model>.
        get(u["gltf"], os.path.join(DEST, f"{model}.gltf"))
        if "bin" in u:
            get(u["bin"], os.path.join(DEST, f"{model}.bin"))
        for t in u.get("tex", []):
            get(t, os.path.join(DEST, os.path.basename(t)))
        print(f"{model}: ok ({len(u.get('tex', []))} maps)")


if __name__ == "__main__":
    main()
