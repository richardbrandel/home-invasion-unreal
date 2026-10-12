#!/bin/bash
# Double-click this in Finder to start the game.
#
# It runs tools/play.sh, which forwards to the build machine when it is run on the Mac and
# launches the game directly when it is run there -- so this file and the one inside tools/ are
# the same entry point from either side.
#
# The Terminal window is held open afterwards so a failure is readable rather than a window that
# flashes and vanishes. That was the original complaint about the desktop icon on the Linux box:
# it opened a terminal, printed, and closed.
cd "$(dirname "$0")" || exit 1
./tools/play.sh "$@"
STATUS=$?
echo
if [ "$STATUS" -ne 0 ]; then
	echo "play: FAILED (exit $STATUS) -- the message above says why."
fi
read -r -p "Press Return to close this window... " _
