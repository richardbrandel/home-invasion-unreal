"""Import SOURCE files into .uassets, headlessly.

Run through `tools/import-assets.sh`. This is the other half of the finding in
`tools/make-assets.py`: that file proves the project can AUTHOR an asset without an editor
session, and this one proves it can IMPORT one. Together they are what makes a shopping list
of source files useful -- a WAV or an FBX can be dropped into `SourceAssets/` and turned into
a game asset by a script, so no one has to open a GUI to receive it.

SOURCE files are the thing worth asking a person for. A .uasset is an engine-specific binary
that only an editor can make; a .wav is a .wav.

EVERY FILE under SourceAssets/ is imported, into a destination folder named after the
subdirectory it sits in. Dropping a file in is the whole instruction.
"""

import os
import unreal

SRC = "/data/projects/HIUnreal/SourceAssets"
DEST = "/Game/HIImported"

# DO NOT set `task.skeleton` here, and this is a measured warning rather than a preference.
# Pointing an FBX task at `/Game/Characters/Mannequins/Meshes/SK_Mannequin` looks like the
# right way to land a Mixamo animation on the engine's skeleton, and it does make a rigged
# CHARACTER import richly -- mesh, its own skeleton, its own clothing parts and its own idle/
# walk/run/death clips. But it also makes the run stop part way through with no error and no
# "done" line, and animation-ONLY FBX still produced nothing. A silent half-finished import
# is the worst outcome available, so the override is gone.
#
# The open item is retargeting: a Mixamo animation needs an IK Retargeter or an Interchange
# user-defined pipeline, and that is a separate job. `SOURCE ASSETS` in the README says so.

# Subdirectories that are not assets in their own right (a .bin is a glTF's mesh data and is
# imported WITH its .gltf, not on its own).
SKIP_EXT = (".bin", ".import", ".uid", ".aiff", ".json")


def _import(rel_path, dest_folder):
	full = os.path.join(SRC, rel_path)
	task = unreal.AssetImportTask()
	task.set_editor_property("filename", full)
	task.set_editor_property("destination_path", "%s/%s" % (DEST, dest_folder))
	task.set_editor_property("automated", True)
	task.set_editor_property("replace_existing", True)
	task.set_editor_property("save", True)
	unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

	paths = task.get_editor_property("imported_object_paths")
	if not paths:
		print("HI_IMPORT: FAILED (no objects) for %s" % rel_path)
		return None
	print("HI_IMPORT: %s -> %s" % (rel_path, paths[0]))
	return paths[0]


def main():
	print("HI_IMPORT: importing source assets from %s" % SRC)
	if not os.path.isdir(SRC):
		print("HI_IMPORT: no %s -- nothing to import (that is not an error)" % SRC)
		print("HI_IMPORT: done")
		return

	ok = 0
	failed = 0
	for root, dirs, files in os.walk(SRC):
		dirs.sort()
		for name in sorted(files):
			if name.startswith(".") or name.lower().endswith(SKIP_EXT):
				continue
			rel = os.path.relpath(os.path.join(root, name), SRC)
			folder = os.path.dirname(rel).replace(os.sep, "_") or "Misc"
			if _import(rel, folder) is not None:
				ok += 1
			else:
				failed += 1

	print("HI_IMPORT: %d imported, %d failed" % (ok, failed))
	# A count of zero is a FAILURE, not a quiet success: the whole class of "pass-shaped
	# nothing" in this project comes from treating an empty run as a green one.
	if ok == 0:
		print("HI_IMPORT: FAILED -- nothing under %s to import" % SRC)
	print("HI_IMPORT: done")


main()
