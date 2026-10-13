#!/bin/sh
# PUT THE ICONS WHERE A DESKTOP CAN CLICK THEM. Idempotent, so it is re-run after a rebuild or
# on a fresh clone instead of anyone having to remember the copies and the gio incantation.
#
# THREE THINGS HAVE TO BE TRUE for a double-click to work on GNOME, and they fail differently:
#
#   1. the file is in ~/Desktop, or in ~/.local/share/applications for the Activities app grid;
#   2. it is EXECUTABLE -- GNOME silently refuses a .desktop that is not;
#   3. it is TRUSTED -- `metadata::trusted`, which is a per-file GNOME annotation and NOT a mode
#      bit, so it has to be re-applied after EVERY copy. Without it the icon is drawn with a
#      warning badge, or refuses to launch, and either way it says nothing useful.
#
# TWO ICONS, and the second one exists because the game's window has NO TITLE BAR: there is no X
# to click, so stopping it needs a path that is not the keyboard and not a terminal.
set -u

DIR=$(dirname "$0")
DESKTOP="$HOME/Desktop"
APPS="$HOME/.local/share/applications"

mkdir -p "$DESKTOP" "$APPS"

# $1 = source .desktop, $2 = filename on the Desktop, $3 = filename in the app grid
install_one() {
	SRC="$1"
	DESKNAME="$2"
	APPSNAME="$3"
	if [ ! -f "$SRC" ]; then
		echo "install-desktop-icon: no $SRC" >&2
		return 1
	fi
	install -m 755 "$SRC" "$DESKTOP/$DESKNAME" || return 1
	install -m 755 "$SRC" "$APPS/$APPSNAME" || return 1
	if command -v gio >/dev/null 2>&1; then
		if ! gio set "$DESKTOP/$DESKNAME" metadata::trusted true 2>/dev/null; then
			echo "install-desktop-icon: could not set metadata::trusted on $DESKNAME -- no session bus?" >&2
			echo "install-desktop-icon: it may need one 'Allow Launching' click on the desktop" >&2
		fi
	fi
	echo "installed: $DESKTOP/$DESKNAME"
	return 0
}

install_one "$DIR/home-invasion.desktop" "Home Invasion.desktop" "home-invasion.desktop" || exit 1
install_one "$DIR/stop-home-invasion.desktop" "Stop Home Invasion.desktop" "stop-home-invasion.desktop" || exit 1

if command -v update-desktop-database >/dev/null 2>&1; then
	update-desktop-database "$APPS" >/dev/null 2>&1
fi

echo "double-click either icon, or search 'Home Invasion' in Activities"
