#!/bin/sh
# STOP the game from the desktop, because its window has no title bar and therefore no X.
#
# WHY THIS IS NEEDED AT ALL: the game runs `-game -windowed`, but the window it creates carries
# NO decorations -- so there is no close button to click, and "where is the X?" is the right
# question to ask. The window manager's own bindings still work (GNOME binds Alt+F4 to close and
# Alt+Space to the window menu), and so does `pkill`. This is the CLICK path.
#
# THE ESCALATION IS NOT SUPERSTITION. This project already measured that Unreal ignores SIGTERM
# mid-round, so a polite `pkill` can leave the old build on screen while the next launch starts
# beside it -- which reads as "the fix did not work". So: ask, wait, then insist.
set -u

LOG=/tmp/hi_launch.log

notify() {
	command -v notify-send >/dev/null 2>&1 || return 0
	notify-send -a "Home Invasion" "$1"
}

notify_error() {
	command -v notify-send >/dev/null 2>&1 || return 0
	notify-send -a "Home Invasion" -u critical "$1"
}

if ! pgrep -x UnrealEditor >/dev/null 2>&1; then
	notify "Home Invasion is not running."
	echo "stop: $(date '+%F %T') nothing to stop" >>"$LOG"
	exit 0
fi

notify "Stopping Home Invasion..."
echo "stop: $(date '+%F %T') signalling pid $(pgrep -x UnrealEditor | head -1)" >>"$LOG"

pkill -x UnrealEditor 2>/dev/null
n=0
while [ "$n" -lt 10 ]; do
	sleep 1
	n=$((n + 1))
	if ! pgrep -x UnrealEditor >/dev/null 2>&1; then
		break
	fi
done

if pgrep -x UnrealEditor >/dev/null 2>&1; then
	# Mid-round, or wedged in RHI init. Ask harder.
	echo "stop: still up after ${n}s, escalating to SIGKILL" >>"$LOG"
	pkill -9 -x UnrealEditor 2>/dev/null
	sleep 1
fi

if pgrep -x UnrealEditor >/dev/null 2>&1; then
	notify_error "Home Invasion would not stop. See $LOG"
	exit 1
fi

notify "Home Invasion stopped."
echo "stop: $(date '+%F %T') stopped after ${n}s" >>"$LOG"
exit 0
