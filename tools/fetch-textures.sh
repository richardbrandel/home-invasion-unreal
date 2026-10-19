#!/usr/bin/env bash
#
# Download the photoreal surface textures into SourceAssets/textures/, then import them.
#
# WHY THIS IS A SCRIPT AND NOT A COMMIT. `SourceAssets/` and `Content/` are gitignored for
# the same reason as the engine art: they are derived/owned content, and a commit would be a
# copy that rots. The SOURCE here is Poly Haven (polyhaven.com), all CC0 -- public domain, no
# attribution, commercial use OK, no account needed to download. This is the derivation.
#
# The names are load-bearing: `FHIMaterials::Textured` loads /Game/HIImported/textures/<name>,
# and the importer names each asset after its file stem, so a texture dropped here as
# `floor_wood.png` becomes the `floor_wood` the house code already references.
#
# Usage:  tools/fetch-textures.sh [--import]
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$HERE/SourceAssets/textures"
mkdir -p "$DEST"

# Poly Haven asset name -> the game's texture stem. `_diff_2k.jpg` is the 2K albedo map.
# Chosen 2026-10-19 for "closest to real life" (no-account CC0 source, first on the list).
PAIRS=(
	"oak_wood_planks       floor_wood"
	"floor_tiles_06        floor_tile"
	"grey_plaster          ceiling_plaster"
	"grass_ground          lawn"
	"grey_roof_tiles       roof_shingle"
	"concrete_block_wall   cinderblock"
)

command -v python3 >/dev/null || { echo "python3 required" >&2; exit 1; }
python3 -c "import PIL" 2>/dev/null || { echo "Pillow required (pip install Pillow)" >&2; exit 1; }

for pair in "${PAIRS[@]}"; do
	read -r src dst <<< "$pair"
	url="https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/${src}/${src}_diff_2k.jpg"
	echo "fetching $src -> $dst.png"
	curl -fsSL --max-time 120 "$url" -o "/tmp/${dst}.jpg"
	python3 -c "
from PIL import Image
Image.open('/tmp/${dst}.jpg').convert('RGB').save('${DEST}/${dst}.png')
"
	rm -f "/tmp/${dst}.jpg"
done
echo "textures written to $DEST -- run tools/import-assets.sh to import them"

if [[ "${1:-}" == "--import" ]]; then
	"$HERE/tools/import-assets.sh"
fi
