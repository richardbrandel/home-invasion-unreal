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

# THE `task.skeleton` OVERRIDE IS USED, AND ONLY FOR ANIMATION-ONLY CLIPS, POINTED AT THEIR OWN
# CHARACTER'S RIG. The distinction is the whole of this note, and it was got wrong for a day:
#
#   * Pointing a Mixamo clip at `/Game/Characters/Mannequins/Meshes/SK_Mannequin` is RETARGETING,
#     and retargeting is the thing that does not work this way. That attempt also made the run
#     stop part way through with no error and no "done" line. Measured 2026-10-09.
#   * Binding a Mixamo clip to the skeleton that its OWN mesh-carrying export just created is not
#     retargeting at all -- same rig, same joint names, nothing to translate. This is what
#     `_skeleton_for` does, and it is why the port does not need an IK Retargeter.
#
# Measured 2026-10-13 on `characters/louise/`: `idle.fbx` (the mesh-carrying export) imported
# richly with NO override -- mesh, skeleton, physics asset, eight textures and the idle clip. The
# other eight FBX in that folder are animation-only and each produced NOTHING, reported as
# "FAILED (no objects)", which is exactly what an animation with nowhere to bind does.
MESH_CARRYING = "idle.fbx"

# Subdirectories that are not assets in their own right (a .bin is a glTF's mesh data and is
# imported WITH its .gltf, not on its own).
SKIP_EXT = (".bin", ".import", ".uid", ".aiff", ".json")


def _skeleton_for(rel_path):
	"""The skeleton an animation-only character clip must bind to, or None.

	`SourceAssets/characters/<name>/idle.fbx` is the mesh-carrying export and creates the rig;
	every other FBX in that folder is animation-only and has to be pointed at it. Derived from
	the folder layout rather than from a list, so adding a character is a copy and no edit.
	"""
	parts = rel_path.replace(os.sep, "/").split("/")
	if len(parts) == 3 and parts[0] == "characters" and parts[2].lower() != MESH_CARRYING:
		return unreal.load_asset("%s/%s_%s/idle_Skeleton" % (DEST, parts[0], parts[1]))
	return None


def _import(rel_path, dest_folder):
	full = os.path.join(SRC, rel_path)
	task = unreal.AssetImportTask()
	task.set_editor_property("filename", full)
	task.set_editor_property("destination_path", "%s/%s" % (DEST, dest_folder))
	task.set_editor_property("automated", True)
	task.set_editor_property("replace_existing", True)
	task.set_editor_property("save", True)
	skel = _skeleton_for(rel_path)
	if skel is not None:
		# `AssetImportTask` HAS NO `skeleton` PROPERTY. Measured 2026-10-13 by asking the engine
		# with an introspection probe rather than by guessing a second time: the task's only
		# relevant property is `options`, and the skeleton lives on `FbxImportUI`.
		#
		# Setting it on the task raises
		#   "Failed to find property 'skeleton' for attribute 'skeleton' on 'AssetImportTask'"
		# -- which is what the note here used to record as a "silent half-finished import".
		# It was never silent; it was a thrown exception, and nobody was reading the Python
		# error out of the log. The old conclusion ("this needs an IK Retargeter") was built on
		# that misreading, and the port does not need one.
		ui = unreal.FbxImportUI()
		ui.set_editor_property("skeleton", skel)
		ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
		ui.set_editor_property("import_animations", True)
		ui.set_editor_property("import_mesh", False)
		ui.set_editor_property("import_materials", False)
		ui.set_editor_property("import_textures", False)
		task.set_editor_property("options", ui)
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
