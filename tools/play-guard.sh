#!/bin/sh
# Run the game, then RECORD HOW IT ENDED.
#
#   tools/play-guard.sh <UnrealEditor> <uproject> [game args...]
#
# WHY (2026-11-01). A report of "it crashed" cost an afternoon and could not be answered, because
# the launcher `setsid nohup`s the game and exits: the process's exit status -- the one piece of
# evidence that separates a segfault from a kill from a clean quit -- was thrown away. `play.sh`
# now launches this script, which runs the same command and writes the verdict into
# GAME LOG so the next report is a line of evidence instead of a memory.
#
# The statuses worth knowing:
#   0        clean exit
#   124      killed by a timeout wrapping the run
#   137      128+9, SIGKILL -- something killed it (OOM killer, pkill, the desktop environment)
#   139      128+11, SIGSEGV -- it segfaulted
#   143      128+15, SIGTERM -- asked to stop
# A crash the engine HANDLED still writes Saved/Crashes/crashinfo-*; this catches the ones it did
# not, which is exactly the class that leaves no other trace.
set -u

UE="$1"; PROJECT="$2"; shift 2
LOG="${HI_GAME_LOG:-/tmp/hi_play.log}"

"$UE" "$PROJECT" "$@"
STATUS=$?

if [ "$STATUS" -gt 128 ]; then
	printf '\n[hi-guard] game exited: status=%d (signal %d)%s\n' \
		"$STATUS" "$((STATUS - 128))" " -- see above for the last thing it logged" >>"$LOG"
else
	printf '\n[hi-guard] game exited: status=%d%s\n' "$STATUS" " -- clean, no signal" >>"$LOG"
fi
exit "$STATUS"
