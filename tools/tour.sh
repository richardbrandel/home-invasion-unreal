#!/bin/bash
# THE SCRIPTED TOUR: eight known views of the house, captured in one command.
#
#   ssh ubuntu-dev '/data/projects/HIUnreal/tools/tour.sh'
#
# Then copy them back and look:
#
#   rsync -a ubuntu-dev:/data/projects/HIUnreal/Saved/Screenshots/LinuxEditor/tour_*.png shots/
#
# WHY A SCRIPT AND NOT ENGINE CODE. The first version of this was a `-HITour` switch inside
# the game mode, and it HUNG: it set a screenshot request that the harness only services when
# `-HIShot` is on the command line, so the game sat there forever and wrote nothing. The
# capture switches below already work, are already proven, and need no C++ at all -- so the
# tour is a loop over them. That is the whole trick, and it is why this file exists instead
# of a hundred lines of TourTick.
#
# WHAT IT IS FOR. An agent can read still frames, so it can catch a starburst minimap or a
# switch floating in a doorway. What it cannot do is play: it has no motion, no feel, and no
# way to happen across the thing a person notices in the first minute. This turns "go and
# play for an hour" into "look at eight pictures", which is the cheapest way to spend a
# person's eyes.
set -u

UE=/home/richard/UnrealEngine/Engine/Binaries/Linux/UnrealEditor
PROJ=/data/projects/HIUnreal/HIUnreal.uproject
SHOTS=/data/projects/HIUnreal/Saved/Screenshots/LinuxEditor
THIEVES=${THIEVES:-3}

shot() {
	local name="$1" pos="$2" yaw="$3" pitch="$4" after="$5"
	"$UE" "$PROJ" -game -RenderOffscreen -ResX=1280 -ResY=720 -unattended -nopause -nosplash \
		-log -abslog="/tmp/hi_tour_$name.log" \
		-HIShot="$name" -HIShotAfter="$after" -HIShotPos="$pos" \
		-HIShotYaw="$yaw" -HIShotPitch="$pitch" -HIThieves="$THIEVES" >/dev/null 2>&1
	if [ -f "$SHOTS/$name.png" ]; then
		echo "  ok    $name"
	else
		echo "  FAIL  $name (no PNG)" >&2
	fi
}

echo "tour: eight views, $THIEVES intruders"
#                    name              pos         yaw  pitch after
shot tour_1_street     "0,6.0"       180  -2   1.5   # the approach: house and drive ahead
shot tour_2_spawn      "0,-10.6"       0   2   2.0   # where a round starts: the front door
shot tour_3_hall       "0,-13.0"       0   0   2.0   # the hall, divider, fittings
shot tour_4_living     "-3.5,-11.0"  270   0   2.0   # sofa, coffee table, tv, lamp
shot tour_5_kitchen    "3.5,-11.0"    90   0   2.0   # counter, fridge, kettle
shot tour_6_bedroom    "-3.5,-17.0"  270   0   2.0   # bed, wardrobe, nightstand
shot tour_7_study      "3.5,-17.0"    90   0   2.0   # desk, bookshelf, cabinet
shot tour_8_arriving   "0,-10.6"       0   2   9.0   # nine seconds in: men coming, wounds

echo "--- frames ---"
ls "$SHOTS"/tour_*.png 2>/dev/null | sed 's|.*/|  |'
printf "  count: %s of 8\n" "$(ls "$SHOTS"/tour_*.png 2>/dev/null | wc -l | tr -d ' ')"
