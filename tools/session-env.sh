#!/bin/sh
# The session environment a game needs on this box, discovered rather than written down.
#
# SOURCED, not executed. `play.sh` and `capture.sh` both need every line of this, and two
# copies of "the cookie is named per boot" would drift the moment one of them was fixed --
# which is the same failure as the stale build that made a fixed collision bug look unfixed.
#
# The two hazards below were each measured, and each cost hours:
#
#  1. A GAME LAUNCHED FROM A PLAIN SSH SESSION CANNOT SEE THE SCREEN. SDL3 reports "could not
#     initialize SDL: No available video device" and the engine exits before drawing. The SAME
#     binary runs perfectly with XDG_RUNTIME_DIR and WAYLAND_DISPLAY set. The Xwayland auth
#     cookie's FILENAME CHANGES ON EVERY BOOT, so it is discovered here and never hard-coded.
#
#  2. A DRIVER UPDATE WITHOUT A REBOOT LEAVES THE GPU UNUSABLE, and it looks like a game bug.
#     nvidia-smi answers "Driver/library version mismatch"; every GPU client then HANGS inside
#     RHI init and what is left on screen is the SPLASH window, which is easy to mistake for
#     the game having minimised itself. Measured 2026-10-12: a window-sizing workaround was
#     written for a phantom whose measurements were all taken of a hung splash screen.
#
# Sets and exports: DISPLAY, XAUTHORITY, XDG_RUNTIME_DIR, WAYLAND_DISPLAY.
# Sets: UE, PROJ, PROJDIR, SHOTDIR, RUNTIME.
# Provides: hi_require_editor, hi_require_gpu, hi_kill_stale_editor.

hi_require_editor() {
	UE=/data/UnrealEngine/Engine/Binaries/Linux/UnrealEditor
	PROJ=/data/projects/HIUnreal/HIUnreal.uproject
	PROJDIR=/data/projects/HIUnreal
	# Where FScreenshotRequest lands: the PROJECT's Saved, not the engine's, because -game runs
	# the project. Printed by the game too, so the two can be compared rather than trusted.
	SHOTDIR="$PROJDIR/Saved/Screenshots/LinuxEditor"
	if [ ! -x "$UE" ]; then
		echo "env: no editor at $UE" >&2
		return 1
	fi
	return 0
}

hi_require_gpu() {
	if command -v nvidia-smi >/dev/null 2>&1; then
		if ! timeout 20 nvidia-smi >/dev/null 2>&1; then
			echo "env: THE GPU DRIVER IS BROKEN -- nvidia-smi fails." >&2
			echo "env: an update was probably installed without a reboot (kernel module vs" >&2
			echo "env: userspace mismatch). REBOOT the machine; no amount of relaunching helps." >&2
			return 1
		fi
	fi
	return 0
}

hi_kill_stale_editor() {
	# ONE AT A TIME. A second editor doubles the load and neither one is the one you are
	# looking at. Unreal ignores SIGTERM mid-round, so escalate -- otherwise the old build
	# keeps the screen and the new one starts beside it, which reads as "the fix did not work".
	pkill -x UnrealEditor 2>/dev/null
	sleep 3
	if [ "$(pgrep -c -x UnrealEditor 2>/dev/null || echo 0)" != "0" ]; then
		pkill -9 -x UnrealEditor 2>/dev/null
		sleep 2
	fi
}

hi_detect_session() {
	RUNTIME=/run/user/$(id -u)
	XA=""
	for f in "$RUNTIME"/.mutter-Xwaylandauth.*; do
		[ -f "$f" ] && XA="$f"
	done
	DISPLAY="${DISPLAY:-:0}"
	export DISPLAY XAUTHORITY="${XA:-$HOME/.Xauthority}"
	export XDG_RUNTIME_DIR="$RUNTIME"
	export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
	echo "env: display=$DISPLAY auth=$XAUTHORITY runtime=$XDG_RUNTIME_DIR"
}
