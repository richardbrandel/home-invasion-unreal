#!/bin/sh
# The desktop icon runs THIS, not play.sh, and the difference is entirely about what a PERSON
# sees when they double-click.
#
# TWO FAILURES THIS EXISTS TO PREVENT, both of which make a working launcher look dead:
#
#  1. NOTHING HAPPENS FOR TWENTY SECONDS, SO YOU CLICK AGAIN. The game takes about 20 s from
#     launch to "input claimed", and `play.sh` begins by KILLING any running UnrealEditor (by
#     design -- one editor at a time). So a second click does not queue, it kills the first
#     attempt and starts over. Click, wait, see nothing, click, wait, see nothing: a launcher
#     that works perfectly and never once gets to finish. The fix is feedback plus a refusal to
#     restart what is already up.
#
#  2. NO TERMINAL ON THIS BOX TO PRINT AN ERROR INTO. `gnome-terminal`, `kgx` and `xterm` are all
#     absent, so a `.desktop` with `Terminal=true` has nothing to open, and a failed launch says
#     nothing anywhere. Errors here go to a NOTIFICATION and a zenity window instead.
#
# `notify-send` and `zenity` are both present on this machine; both calls are guarded anyway, so
# a missing one degrades to silence rather than to a crash.
set -u

TOOLS=$(dirname "$0")
LOG=/tmp/hi_launch.log

notify() {
	command -v notify-send >/dev/null 2>&1 || return 0
	notify-send -a "Home Invasion" "$1"
}

notify_error() {
	command -v notify-send >/dev/null 2>&1 || return 0
	notify-send -a "Home Invasion" -u critical "$1"
}

RUNNING=$(pgrep -x UnrealEditor 2>/dev/null | head -1)
if [ -n "$RUNNING" ]; then
	# REFUSE, rather than restart. The alternative is the double-click trap described above, and
	# "already running" is also the honest answer: the window is somewhere on screen already.
	notify "Home Invasion is already running."
	echo "launch: $(date '+%F %T') already running, pid $RUNNING -- not restarting" >>"$LOG"
	exit 0
fi

notify "Starting Home Invasion - about 20 seconds to a window."
echo "launch: $(date '+%F %T') starting" >>"$LOG"

if "$TOOLS/play.sh" >>"$LOG" 2>&1; then
	notify "Home Invasion is up."
	exit 0
fi

# A FAILURE MUST NOT BE SILENT, because there is no terminal to have printed it into.
TAIL=$(tail -3 "$LOG" 2>/dev/null | tr '\n' ' ')
notify_error "Home Invasion did NOT start. See $LOG"
if command -v zenity >/dev/null 2>&1; then
	zenity --error --width=560 --title="Home Invasion did not start" \
		--text="The launcher failed.\n\n$TAIL\n\nFull log: $LOG"
fi
exit 1
