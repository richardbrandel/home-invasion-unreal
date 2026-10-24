#!/bin/sh
# Turn SOURCE files into .uassets, headlessly.
#
#   ssh ubuntu-dev '/data/projects/HIUnreal/tools/import-assets.sh'
#
# Put the files under /data/projects/HIUnreal/SourceAssets/ first -- `audio/` for .wav.
# SourceAssets/ is gitignored: the files are yours, and the point of this script is that
# receiving one needs no GUI.
#
# The value of this script is what it means for a shopping list. A .uasset can only be made
# by an editor; a .wav, a .fbx or a .png can be made by anyone with the right tool, and this
# turns them into assets unattended. So "I need X" can name the SOURCE format rather than
# "someone must open Unreal and import it".
set -eu

UE=/data/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd
PROJ=/data/projects/HIUnreal/HIUnreal.uproject
HERE=$(cd "$(dirname "$0")" && pwd)
LOG=/tmp/hi_import_assets.log

if [ ! -x "$UE" ]; then
	echo "import-assets: no editor at $UE" >&2
	exit 1
fi

"$UE" "$PROJ" -unattended -nosplash -nopause -NullRHI \
	-ExecutePythonScript="$HERE/import-assets.py" \
	-log -abslog="$LOG" || true

echo "--- what it reported ---"
grep -a "HI_IMPORT" "$LOG" || echo "  (nothing -- the script did not run; see $LOG)"

# Exit code is not a verdict (see the README). A MISSING or FAILED line is.
if grep -aqE "HI_IMPORT:.*(MISSING|FAILED)" "$LOG"; then
	echo "import-assets: FAILED -- at least one file did not import" >&2
	exit 1
fi
if ! grep -aq "HI_IMPORT: done" "$LOG"; then
	echo "import-assets: FAILED -- the script never reached the end" >&2
	exit 1
fi
echo "import-assets: ok"
