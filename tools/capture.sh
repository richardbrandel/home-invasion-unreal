#!/bin/sh
# CAPTURE: several frames from ONE launch, and it always ends.
#
#   tools/capture.sh walk 1,2,3,4,5                 # five frames as the autopilot plays
#   tools/capture.sh gun 2 -- -HIWeapon=shotgun     # one frame holding the long gun
#   tools/capture.sh posed 5 --offscreen -- -HIShotPos=0,-10.6 -HIShotYaw=0 -HIShotPitch=2
#
# Run it from the project root; the frames land in `shots/`.
#
# WHY THIS EXISTS, and both reasons were measured today rather than imagined:
#
#  1. `-HIShot` fired ONCE and then quit, so any sweep of poses was N launches at ~40 s each.
#     `-HIShotSeries=t1,t2,...` takes a list of SIM seconds in a single run.
#
#  2. **A CAPTURE WHOSE TARGET TIME WAS PAST THE ROUND'S LAST SECOND NEVER FIRED.** The round
#     clock is sim time and stops when the round is decided, so a later target was not late --
#     it was UNREACHABLE -- and the process waited for ever. That is why a capture of a MOVING
#     player always hung while a capture of a standing one worked: the autopilot clears the
#     house in about five sim seconds, and a standing homeowner obligingly outlives the timer.
#     The game no longer hangs on this (see the gate in `TickHarness`), and this script carries
#     its own wall-clock ceiling on top, because a harness that CAN hang costs an afternoon.
#
# WINDOWED IS THE DEFAULT, AND THE REASON IS RESOLUTION, NOT SPEED -- both were measured.
#
#   * SPEED IS A WASH. Offscreen and windowed advance the sim at almost exactly 1x realtime:
#     one sim second cost 1.83-1.90 s of wall clock in the first interval and 0.99-1.01 s in the
#     second, in BOTH modes. The "~2 fps, a quarter of real speed" figure this script was
#     originally written around is STALE and has been corrected at its source in
#     `AHIGameMode::Tick`; it briefly had this script's whole design built on a ten-times speed
#     win that does not exist.
#   * RESOLUTION IS NOT A WASH, and this is why windowed wins. A windowed run HONOURS
#     `-ResX=1600 -ResY=900` and writes a 1600x900 PNG. `-RenderOffscreen` IGNORES it and writes
#     1280x720. Comparing a capture against Richard's own 1600x900 screen print needs the same
#     frame, or an offscreen capture quietly answers a different question.
#
# `--offscreen` is kept because it does not steal the build box's screen and needs no display
# session at all. Use it when nobody is sitting at that desktop.
set -u

case "$(uname -s)" in
Darwin)
	ARGS=""
	for a in "$@"; do
		ARGS="$ARGS $a"
	done
	echo "capture: Mac copy -- the game runs on ubuntu-dev, forwarding there"
	ssh ubuntu-dev "/data/projects/HIUnreal/tools/capture.sh$ARGS" || exit $?
	# Bring the frames back, because a picture that stays on the build box cannot be LOOKED at,
	# and looking is the only thing a capture is for.
	rsync -a ubuntu-dev:/data/projects/HIUnreal/shots/ ./shots/ 2>/dev/null
	echo "capture: frames synced to ./shots/"
	exit 0
	;;
esac

NAME=""
TIMES=""
MODE=windowed
THIEVES=3
MAXW=300
PASSTHRU=""

while [ $# -gt 0 ]; do
	case "$1" in
		--offscreen) MODE=offscreen; shift ;;
		--windowed)  MODE=windowed; shift ;;
		--thieves)   THIEVES="${2:-3}"; shift 2 ;;
		--thieves=*) THIEVES="${1#*=}"; shift ;;
		--timeout)   MAXW="${2:-300}"; shift 2 ;;
		--timeout=*) MAXW="${1#*=}"; shift ;;
		--)
			shift
			while [ $# -gt 0 ]; do PASSTHRU="$PASSTHRU $1"; shift; done
			;;
		-*)
			# Anything else that looks like a switch goes to the GAME, so a new dev switch
			# needs no change to this script.
			PASSTHRU="$PASSTHRU $1"; shift
			;;
		*)
			if [ -z "$NAME" ]; then NAME="$1"
			elif [ -z "$TIMES" ]; then TIMES="$1"
			else echo "capture: unexpected argument '$1'" >&2; exit 2
			fi
			shift
			;;
	esac
done

if [ -z "$NAME" ]; then
	echo "usage: capture.sh NAME [t1,t2,...] [--offscreen] [--thieves N] [--timeout SEC] [-- game args]" >&2
	exit 2
fi
[ -n "$TIMES" ] || TIMES=2

. "$(dirname "$0")/session-env.sh" || exit 1
hi_require_editor || exit 1
hi_require_gpu || exit 1
hi_detect_session
hi_kill_stale_editor

LOG=/tmp/hi_capture_$NAME.log
: > "$LOG" 2>/dev/null || true

if [ "$MODE" = offscreen ]; then
	VIDEO=-RenderOffscreen
	echo "capture: offscreen -- about 2 fps, so a sim-second target costs roughly four wall seconds"
else
	VIDEO=-windowed
	echo "capture: windowed on the build box's screen; it opens and closes itself"
fi

# shellcheck disable=SC2086
setsid nohup "$UE" "$PROJ" -game $VIDEO -ResX=1600 -ResY=900 -unattended -nopause -nosplash \
	-HIThieves="$THIEVES" -HIShot="$NAME" -HIShotSeries="$TIMES" \
	-log -abslog="$LOG" $PASSTHRU >/tmp/hi_capture.out 2>&1 < /dev/null &
disown 2>/dev/null || true

n=0
while [ "$n" -lt "$MAXW" ]; do
	sleep 2
	n=$((n + 2))
	if ! pgrep -x UnrealEditor >/dev/null 2>&1; then
		break
	fi
done

KILLED=0
if pgrep -x UnrealEditor >/dev/null 2>&1; then
	KILLED=1
	echo "capture: STILL RUNNING after ${MAXW}s -- killing it. See $LOG" >&2
	pkill -9 -x UnrealEditor 2>/dev/null
	sleep 2
fi

mkdir -p "$PROJDIR/shots"
COPIED=0
for f in "$SHOTDIR"/${NAME}_*.png "$SHOTDIR"/${NAME}.png; do
	[ -f "$f" ] || continue
	cp -f "$f" "$PROJDIR/shots/" && COPIED=$((COPIED + 1))
done

echo "--- what the game said ---"
grep -h "HI: shot \|round decided at\|HI: scripted run\|screenshot" "$LOG" 2>/dev/null | cut -c1-220

echo "--- frames ---"
if [ "$COPIED" = "0" ]; then
	echo "capture: NO FRAME WAS WRITTEN. Look at $LOG." >&2
	[ "$KILLED" = "1" ] && echo "capture: and it had to be killed, which is the hang itself." >&2
	exit 1
fi
echo "capture: $COPIED frame(s) copied into $PROJDIR/shots/"
ls -1 "$PROJDIR/shots/${NAME}"_*.png "$PROJDIR/shots/${NAME}.png" 2>/dev/null
exit 0
