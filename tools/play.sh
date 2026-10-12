#!/bin/sh
# PLAY IT: launch the game on the build machine, with the environment it actually needs.
#
#   ssh ubuntu-dev '/data/projects/HIUnreal/tools/play.sh'
#
# WHY THIS IS A SCRIPT AND NOT ONE COMMAND LINE. Two things cost hours on 2026-10-12 and both
# belong in a file rather than in a shell history:
#
#  1. THE SESSION ENVIRONMENT IS NOT OPTIONAL. A game launched from a plain ssh session gets
#     no XDG_RUNTIME_DIR and no WAYLAND_DISPLAY, and SDL3 then reports "could not initialize
#     SDL: No available video device" and the engine exits before it draws anything. This was
#     measured: the SAME binary that runs fine with those two variables set fails without them.
#     DISPLAY and the Xwayland auth cookie are needed as well, and the cookie FILENAME CHANGES
#     ON EVERY BOOT, so it is discovered here rather than written down.
#
#  2. A DRIVER UPDATE WITHOUT A REBOOT LEAVES THE GPU UNUSABLE. On 2026-10-12 the NVIDIA
#     userspace libraries went to 615.71.09 while the loaded kernel module stayed at
#     610.57.04, and every GPU client hung inside RHI initialisation -- including UnrealEditor,
#     which stopped at `LogRHI: Skipping SF_VULKAN_SM5...` and never created a game window.
#     What a person sees in that state is the SPLASH, a small window in the middle of the
#     screen, which is easy to mistake for the game having minimised itself. `nvidia-smi`
#     saying "Driver/library version mismatch" is the check, and the fix is a REBOOT.
#
# Optional: any extra Unreal arguments are passed through, e.g.
#     tools/play.sh -HIThieves=1
set -u

# ---- ON THE MAC, FORWARD TO THE BUILD MACHINE. The game runs on `ubuntu-dev`, but the project
# is edited on the Mac and that is where Richard usually is, so the SAME script has to work from
# either side. Running the Mac's copy used to fail on `[ ! -x "$UE" ]` with
# "play: no editor at /home/richard/UnrealEngine/..." -- which is true, and useless: it does not
# say that the game lives on the other machine or how to start it there.
case "$(uname -s)" in
Darwin)
	ARGS=""
	for a in "$@"; do
		ARGS="$ARGS $a"
	done
	echo "play: Mac copy -- the game runs on ubuntu-dev, forwarding there"
	exec ssh ubuntu-dev "/data/projects/HIUnreal/tools/play.sh$ARGS"
	;;
esac

UE=/home/richard/UnrealEngine/Engine/Binaries/Linux/UnrealEditor
PROJ=/data/projects/HIUnreal/HIUnreal.uproject
RUNTIME=/run/user/$(id -u)

if [ ! -x "$UE" ]; then
	echo "play: no editor at $UE" >&2
	exit 1
fi

# The graphics driver must match its kernel module, or nothing renders. Checked here because
# the failure is otherwise a silent hang that looks like a game bug.
if command -v nvidia-smi >/dev/null 2>&1; then
	if ! timeout 20 nvidia-smi >/dev/null 2>&1; then
		echo "play: THE GPU DRIVER IS BROKEN -- nvidia-smi fails." >&2
		echo "play: an update was probably installed without a reboot (kernel module vs" >&2
		echo "play: userspace mismatch). REBOOT the machine; no amount of relaunching helps." >&2
		exit 1
	fi
fi

# The Xwayland cookie is named per boot, so find it rather than guess it.
XA=""
for f in "$RUNTIME"/.mutter-Xwaylandauth.*; do
	[ -f "$f" ] && XA="$f"
done

DISPLAY="${DISPLAY:-:0}"
export DISPLAY XAUTHORITY="${XA:-$HOME/.Xauthority}"
export XDG_RUNTIME_DIR="$RUNTIME"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"

echo "play: display=$DISPLAY auth=$XAUTHORITY runtime=$XDG_RUNTIME_DIR"

# One at a time: a second editor doubles the load and neither one is the one you are looking at.
pkill -x UnrealEditor 2>/dev/null
sleep 3
if [ "$(pgrep -c -x UnrealEditor 2>/dev/null || echo 0)" != "0" ]; then
	# Unreal ignores SIGTERM mid-round; without this the old build keeps the screen.
	pkill -9 -x UnrealEditor 2>/dev/null
	sleep 2
fi

LOG=/tmp/hi_play.log
setsid nohup "$UE" "$PROJ" -game -windowed -ResX=1600 -ResY=900 -HIThieves=3 "$@" \
	-log -abslog="$LOG" >/tmp/hi_play.out 2>&1 < /dev/null &
disown 2>/dev/null || true

# Wait for it to actually claim input, rather than assuming 20 seconds is enough. "The game did
# not start" and "the game is starting slowly" look identical otherwise.
n=0
while [ "$n" -lt 60 ]; do
	sleep 2
	n=$((n + 2))
	if grep -aq "input claimed" "$LOG" 2>/dev/null; then
		echo "play: running (input claimed after ${n}s), pid $(pgrep -x UnrealEditor | head -1)"
		exit 0
	fi
	if ! pgrep -x UnrealEditor >/dev/null 2>&1; then
		echo "play: THE GAME EXITED DURING STARTUP -- see $LOG" >&2
		tail -5 "$LOG" >&2
		exit 1
	fi
done
echo "play: still starting after 120s -- see $LOG" >&2
exit 1
