#!/bin/sh
# PUT THE ICON WHERE A DESKTOP CAN CLICK IT. Idempotent, so it is re-run after a rebuild or on a
# fresh clone instead of anyone having to remember two copies and a gio incantation.
#
# THREE THINGS HAVE TO BE TRUE for a double-click to work on GNOME, and they fail differently:
#
#   1. the file is in ~/Desktop, or in ~/.local/share/applications for the Activities app grid;
#   2. it is EXECUTABLE -- GNOME silently refuses a .desktop that is not;
#   3. it is TRUSTED -- `metadata::trusted`, which is a per-file GNOME annotation and NOT a mode
#      bit, so it has to be re-applied after EVERY copy. Without it the icon is drawn with a
#      warning badge, or refuses to launch, and either way it says nothing useful.
set -u

SRC=$(dirname "$0")/home-invasion.desktop
DESK="$HOME/Desktop/Home Invasion.desktop"
APPS="$HOME/.local/share/applications/home-invasion.desktop"

if [ ! -f "$SRC" ]; then
	echo "install-desktop-icon: no $SRC" >&2
	exit 1
fi

mkdir -p "$HOME/Desktop" "$HOME/.local/share/applications"
install -m 755 "$SRC" "$DESK" || exit 1
install -m 755 "$SRC" "$APPS" || exit 1

if command -v gio >/dev/null 2>&1; then
	if ! gio set "$DESK" metadata::trusted true 2>/dev/null; then
		echo "install-desktop-icon: could not set metadata::trusted -- no session bus?" >&2
		echo "install-desktop-icon: the icon may need one 'Allow Launching' click on the desktop" >&2
	fi
fi

if command -v update-desktop-database >/dev/null 2>&1; then
	update-desktop-database "$HOME/.local/share/applications" >/dev/null 2>&1
fi

echo "installed: $DESK"
echo "installed: $APPS"
echo "either double-click the desktop icon, or search 'Home Invasion' in Activities"
