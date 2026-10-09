#!/usr/bin/env bash
#
# Copy the engine's own art into this project's Content folder.
#
# WHY THIS IS A SCRIPT AND NOT A COMMIT. Every asset in `Content/` is COPIED from the
# Unreal install, not authored here. Committing ~130 MB of duplicated engine content would
# make the repository mostly a copy of something the machine already has, and the copy would
# silently rot against the engine it came from. `Content/` is gitignored for exactly that
# reason: it is derived state, and this is the derivation.
#
# WHY THE PATHS LOOK ODD. An asset refers to its neighbours by PACKAGE path, and the packages
# in the engine's template resources carry paths like `/Game/Characters/Mannequins/...` no
# matter which folder they physically sit in. So the copies have to land where those internal
# references point, or a mesh loses its skeleton, its materials lose their textures, and the
# only symptom is a character that renders in the default grey with no skin. The destination
# paths below are therefore dictated by `strings <asset> | grep '^/Game/'` and not chosen.
#
# Usage:  tools/fetch-engine-assets.sh [ENGINE_ROOT]
# Default ENGINE_ROOT is /home/richard/UnrealEngine.
set -euo pipefail

ENGINE="${1:-/home/richard/UnrealEngine}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$HERE/Content"

if [ ! -d "$ENGINE/Engine/Content" ]; then
  echo "No Unreal install at '$ENGINE'. Pass the engine root as the first argument." >&2
  exit 1
fi

TEMPLATES="$ENGINE/Templates/TemplateResources"

copy() {
  local src="$1" dst="$2" what="$3"
  if [ ! -d "$src" ]; then
    echo "MISSING: $what -- expected at $src" >&2
    return 1
  fi
  mkdir -p "$dst"
  cp -R "$src/." "$dst/"
  echo "  $what -> ${dst#"$HERE"/} ($(find "$dst" -type f | wc -l | tr -d ' ') files)"
}

echo "Copying engine art into $DEST"

# The UE5 Mannequin: two meshes (so the homeowner and the intruders are tellable apart), the
# skeleton, the physics asset, the materials and textures, every animation, and the
# locomotion BLEND SPACE that fix-list item 13 needs. The whole folder rather than a chosen
# subset: the assets reference each other by name and a partial copy fails in a way that only
# shows up as a missing texture on one material.
copy "$TEMPLATES/High/Characters/Content/Mannequins" \
     "$DEST/Characters/Mannequins" \
     "mannequin meshes, skeleton, materials and animations"

# The pistol, and the rifle standing in for the shotgun: this engine ships no shotgun mesh
# and a long gun in the hand reads as a long gun.
copy "$TEMPLATES/Standard/Weapons/Content/Pistol" \
     "$DEST/Weapons/Pistol" \
     "pistol mesh and materials"
copy "$TEMPLATES/Standard/Weapons/Content/Rifle" \
     "$DEST/Weapons/Rifle" \
     "rifle mesh (standing in for the shotgun)"

echo "Done. $(find "$DEST" -type f | wc -l | tr -d ' ') files, $(du -sh "$DEST" | cut -f1)."
