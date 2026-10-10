#!/bin/sh
# Generate this project's .uassets from text, with no display and no human in an editor.
#
#   ssh ubuntu-dev '/data/projects/HIUnreal/tools/make-assets.sh'
#
# Run it on the BUILD MACHINE, after `rsync`ing this directory there -- it drives the editor
# that is already built at /data/UnrealEngine, and it writes into Content/HIGenerated/.
#
# Why this is a script and not a saved asset: see tools/make-assets.py. The short version is
# that the port's README asserted a .uasset could not be made without an editor session, and
# measured on 2026-10-09 that is false -- so the assets are derived state, exactly like the
# engine art that tools/fetch-engine-assets.sh copies in, and Content/ is gitignored for both.
set -eu

UE=/home/richard/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd
PROJ=/data/projects/HIUnreal/HIUnreal.uproject
HERE=$(cd "$(dirname "$0")" && pwd)
LOG=/tmp/hi_make_assets.log

if [ ! -x "$UE" ]; then
	echo "make-assets: no editor at $UE" >&2
	exit 1
fi

echo "make-assets: generating into Content/HIGenerated/ ..."
# -NullRHI: no display, no GPU context needed to author and save an asset.
# -unattended -nosplash -nopause: nothing is allowed to wait for a human.
"$UE" "$PROJ" -unattended -nosplash -nopause -NullRHI \
	-ExecutePythonScript="$HERE/make-assets.py" \
	-log -abslog="$LOG" || true

# The editor's exit code is NOT a verdict -- the same lesson as -TestExit. Parse the log.
echo "--- what it reported ---"
grep -a "HI_ASSET" "$LOG" || echo "  (nothing -- the script did not run; see $LOG)"
FAILED=$(grep -ac "HI_ASSET:.*exists=False\|FAILED" "$LOG" || true)
if [ "$FAILED" != "0" ]; then
	echo "make-assets: FAILED -- an asset was not written" >&2
	exit 1
fi
if ! grep -aq "HI_ASSET: done" "$LOG"; then
	echo "make-assets: FAILED -- the script never reached the end" >&2
	exit 1
fi
echo "make-assets: ok"
