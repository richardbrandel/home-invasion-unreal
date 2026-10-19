#!/usr/bin/env python3
"""Search Sketchfab and download the best CC0/CC-BY model as a .glb.

Requires SKETCHFAB_TOKEN in the environment. The token is Richard's, used only to
download freely-licensed (CC0 / CC Attribution) models for this Unreal project.
Usage:  SKETCHFAB_TOKEN=... tools/fetch-sketchfab.py "<query>" <outname> [max_faces]
"""
import json
import os
import subprocess
import sys
import urllib.parse
import urllib.request

API = "https://api.sketchfab.com/v3"
DEST = "/data/projects/HIUnreal/SourceAssets/models"


def api(path):
    req = urllib.request.Request(API + path, headers={"Authorization": "Token " + os.environ["SKETCHFAB_TOKEN"]})
    with urllib.request.urlopen(req, timeout=40) as r:
        return json.load(r)


def best(query, max_faces):
    q = urllib.parse.quote(query)
    d = api(f"/search?type=models&q={q}&downloadable=true&sort_by=-likeCount")
    for m in d.get("results", []):
        uid = m["uid"]
        try:
            detail = api(f"/models/{uid}")
        except Exception:
            continue
        lic = detail.get("license", {}).get("slug", "") if isinstance(detail.get("license"), dict) else ""
        faces = detail.get("faceCount") or 0
        if lic in ("cc0", "by") and 0 < faces <= max_faces:
            return uid, detail
    return None, None


def main():
    query, outname = sys.argv[1], sys.argv[2]
    max_faces = int(sys.argv[3]) if len(sys.argv) > 3 else 80000
    uid, detail = best(query, max_faces)
    if uid is None:
        print(f"{outname}: no CC0/CC-BY downloadable model under {max_faces} faces", file=sys.stderr)
        sys.exit(1)
    d = api(f"/models/{uid}/download")
    url = (d.get("glb") or {}).get("url") or (d.get("gltf") or {}).get("url")
    if not url:
        print(f"{outname}: no glb/gltf download", file=sys.stderr)
        sys.exit(1)
    dest = os.path.join(DEST, f"{outname}.glb")
    subprocess.run(["curl", "-fsSL", "--retry", "4", "--retry-all-errors",
                    "--max-time", "300", url, "-o", dest], check=True)
    author = (detail.get("user") or {}).get("username", "?")
    print(f"{outname}: {detail.get('name','')} by {author} "
          f"[{detail.get('license',{}).get('slug','')}] {detail.get('faceCount')} faces -> {dest}")


if __name__ == "__main__":
    main()
