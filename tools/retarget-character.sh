#!/bin/sh
# Retarget the ten clips onto another character's own skeleton. See retarget-character.py.
#   tools/retarget-character.sh <tag> <source-tag> <model-name>
# e.g. tools/retarget-character.sh dancer martha woman01
set -eu
UE=/data/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd
PROJ=/data/projects/HIUnreal/HIUnreal.uproject
HERE=$(cd "$(dirname "$0")" && pwd)
HI_TAG="${1:?tag}" HI_SRC_TAG="${2:?source tag}" HI_MODEL="${3:?model}" \
	"$UE" "$PROJ" -unattended -nosplash -nopause -NullRHI \
	-ExecutePythonScript="$HERE/retarget-character.py" \
	-log -abslog=/tmp/hi_retarget.log || true
grep -a "^\[.*LogPython: RT:" /tmp/hi_retarget.log | sed 's/.*LogPython: //'
