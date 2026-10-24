#!/bin/sh
# ITERATE: sync, build and test in ONE command, so a stale build cannot masquerade as a fix.
#
#   tools/iterate.sh                              # sync + build + the whole HI. suite
#   tools/iterate.sh --filter HI.Unreal.Weapons   # a focused check while iterating
#   tools/iterate.sh --no-test                    # build only
#
# Run it from the project root. On the Mac it syncs first; on the build box it does not need to.
#
# WHY THIS EXISTS, and the reason is not the seconds. This project has TWICE been bitten by an
# artifact that was not the build the code described: once when the workspace-root `.app` was a
# stale copy so a FIXED collision bug looked unfixed, and once when a second UnrealEditor started
# beside a stale one and the new build was never the one on screen. "I ran a build" and "the
# thing I ran is what I just built" are different claims, and this makes them one command.
#
# THE EXIT STATUS IS THE VERDICT and it is deliberately not piped anywhere: `ue-run-tests.sh`
# exits 0 only when tests RAN and all passed (zero tests is a failure by design), so piping it
# into `tail` would report success by way of tail's own status. The tail happens after.
set -u

FILTER=HI.
NOTEST=0
while [ $# -gt 0 ]; do
	case "$1" in
		--filter) FILTER="${2:-HI.}"; shift 2 ;;
		--filter=*) FILTER="${1#*=}"; shift ;;
		--no-test) NOTEST=1; shift ;;
		-h|--help) sed -n '2,6p' "$0"; exit 0 ;;
		*) echo "iterate: unknown argument '$1'" >&2; exit 2 ;;
	esac
done

case "$(uname -s)" in
Darwin)
	RUN="ssh ubuntu-dev"
	echo "iterate: syncing source to ubuntu-dev"
	rsync -a --exclude='Intermediate/' --exclude='Binaries/' --exclude='DerivedDataCache/' \
		--exclude='Saved/' --exclude='.vscode/' --exclude='Makefile' --exclude='Content/' \
		./ ubuntu-dev:/data/projects/HIUnreal/ || { echo "iterate: rsync FAILED" >&2; exit 1; }
	;;
*)
	RUN=""
	;;
esac

# One place that knows whether the work happens here or over there, because `$RUN "$CMD"` with an
# empty RUN runs the whole command string as a single word and fails in a way that looks like a
# missing tool.
run_box() {
	if [ -n "$RUN" ]; then
		$RUN "$1"
	else
		sh -c "$1"
	fi
}

BUILD='/data/UnrealEngine/Engine/Build/BatchFiles/Linux/Build.sh HIUnrealEditor Linux Development -project=/data/projects/HIUnreal/HIUnreal.uproject'

echo "iterate: building"
if ! run_box "$BUILD" >/tmp/hi_iterate_build.log 2>&1; then
	echo "iterate: BUILD FAILED" >&2
	grep -iE "error" /tmp/hi_iterate_build.log | head -25 >&2
	tail -15 /tmp/hi_iterate_build.log >&2
	exit 1
fi
# The build's own timing, quoted rather than summarised, so "it felt slow" has a number.
echo "iterate: build OK $(grep -o 'Total execution time: [0-9.]* seconds' /tmp/hi_iterate_build.log | tail -1)"

if [ "$NOTEST" = "1" ]; then
	echo "iterate: skipping tests (--no-test)"
	exit 0
fi

TESTS="/home/richard/tools/ue-run-tests.sh /data/projects/HIUnreal/HIUnreal.uproject $FILTER"
echo "iterate: testing, filter '$FILTER'"
if ! run_box "$TESTS" >/tmp/hi_iterate_test.log 2>&1; then
	echo "iterate: TESTS FAILED" >&2
	tail -30 /tmp/hi_iterate_test.log >&2
	exit 1
fi
tail -5 /tmp/hi_iterate_test.log
echo "iterate: OK -- synced, built, green"
exit 0
