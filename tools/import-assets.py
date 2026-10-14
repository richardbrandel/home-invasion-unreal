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

# THE CLIPS ARE NOT LEFT AS MIXAMO SHIPPED THEM. The raw exports carry root motion baked
# into the Hips track -- measured from these same files by the Godot project's anim probe:
# walk travels +1.748 m, run +3.552 m, aim travels 0.735 m AND turns 143.2 deg, shoot
# swivels the Hips +/-70 deg -- but the SIM owns position, so a clip that moves the mesh
# moves it away from the body the sim is stepping. Godot flattened its copies at load
# time; this port flattens the ASSETS, at import, through `UHIAnimTool` (see its header
# for what each mode pins and why). Every imported character clip goes through it, and
# `HI.Unreal.Cast` asserts the result from the saved key data -- so a skipped or failed
# normalization is a failed import, not a surprise in a screenshot.
#
# Mode per clip: `aim` is stripped to its upper body (Hips + spine + legs frozen at their
# first keys -- the single-node equivalent of Godot's `_strip_to_upper_body`), `shoot`
# loses only the Hips swivel (the arm's recoil IS the shot), `death` is left exactly as
# authored and merely measured (the fall IS the root motion), and everything locomotive
# keeps its vertical bob -- which in the crouch clips IS the crouch -- with only the
# horizontal travel pinned.
NORMALIZE_MODES = {
	"aim": "aim",
	"shoot": "shoot",
	"death": "none",
}
NORMALIZE_DEFAULT_MODE = "locomotion"

# Richard, 2026-10-15, on the pistol-ready pose: "Move the gun up a little bit,
# about 6 inches." The gun is parented to the hand bone, so raising the gun means
# raising the HANDS: for the clips below, the tool pre-multiplies measured
# local-space shoulder deltas (derived by tools/gunraise.py from the in-pose
# -HIProbe bone dump of 2026-10-15) onto the LeftArm/RightArm tracks. The gun-only
# alternative -- an attach offset -- was built, measured (an exact 15.24 cm rise)
# and rejected on the frames: a pistol floating above hands that hold nothing.
# Only pistol_idle for now; whether shoot/reload need the same raise to keep the
# firing transitions continuous is a look-at-the-frames question, not a guess.
RAISE_WRISTS_CM = {
	"pistol_idle": 15.24,
}
# A report is a failure when it says so, in the tool's own words. Mode "none" succeeds
# with "report only, SAVED nothing"; every failure path says "(nothing normalized)",
# "MISSED", "FAILED" or "NOT UNIFORM".
NORMALIZE_FAIL_TOKENS = ("FAILED", "MISSING", "NOT UNIFORM", "UNKNOWN MODE", "MISSED",
	"nothing normalized")

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


def _normalize(rel_path, imported_path, folder):
	"""Pin a character clip's root motion through UHIAnimTool. True when normalized.

	Only `characters/` content is normalized -- weapons and audio have no Hips. The
	mesh-carrying export needs special handling: importing `idle.fbx` yields the mesh
	`idle` AND its own take as a separate asset `idle_Anim`, and the import task reports
	the first of its objects, so the anim's package path is derived from the folder
	instead of trusted from the report.
	"""
	rel = rel_path.replace(os.sep, "/")
	parts = rel.split("/")
	# ONLY the animation files in `characters/<tag>/` are clips -- NOT the loose
	# textures in the `idle.fbm/` folders beside them, which are three parts deep
	# under characters/ and are not AnimSequences at all. Measured 2026-10-13 on the
	# first normalization run: counting a texture's "MISSING" report as a failure
	# turned a fully successful import into "28 imported, 74 failed". The shape check
	# mirrors `_skeleton_for`'s, which is the same question -- is this one of a
	# character's animation files?
	if len(parts) != 3 or parts[0] != "characters" or not parts[2].lower().endswith(".fbx"):
		return True
	base = os.path.splitext(parts[2])[0].lower()
	mode = NORMALIZE_MODES.get(base, NORMALIZE_DEFAULT_MODE)
	pkg = imported_path
	if base == os.path.splitext(MESH_CARRYING)[0]:
		pkg = "%s/%s/idle_Anim.idle_Anim" % (DEST, folder)

	tool = getattr(unreal, "HIAnimTool", None)
	if tool is None:
		# NOT SILENT: without the tool the clips stay raw, and raw clips mean bodies
		# that skate, aim sideways and moonwalk -- the game still runs, which is
		# exactly why a missing tool has to fail the import instead of warning.
		print("HI_IMPORT: FAILED to normalize %s -- unreal.HIAnimTool is missing "
			"(the C++ build is older than this script)" % pkg)
		return False
	report = tool.normalize_character_clip(pkg, mode, RAISE_WRISTS_CM.get(base, 0.0))
	print("HI_IMPORT: normalize: %s" % report)
	return not any(tok in report for tok in NORMALIZE_FAIL_TOKENS)


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
		# THE MESH-CARRYING EXPORT MUST BE IMPORTED FIRST, or that folder's own clips cannot
		# bind to anything. Measured 2026-10-13, importing three new characters in name order:
		# `aim`, `crouch_idle`, `crouch_walk` and `death` all sort BEFORE `idle.fbx`, and an
		# animation-only clip whose skeleton does not exist yet imports to nothing -- reported
		# as "FAILED (no objects)". Exactly four failures per character, exactly the four that
		# sort before idle, and `idle` plus the four after it succeeded.
		#
		# Running the script a second time makes them all appear, which is the worst kind of
		# fix: it would have been written down as "run it twice" instead of as an ordering bug
		# that the script can simply not have.
		for name in sorted(files, key=lambda n: (0 if n.lower() == MESH_CARRYING else 1, n)):
			if name.startswith(".") or name.lower().endswith(SKIP_EXT):
				continue
			rel = os.path.relpath(os.path.join(root, name), SRC)
			folder = os.path.dirname(rel).replace(os.sep, "_") or "Misc"
			# An import that lands but fails to NORMALIZE counts as a failure: the
			# asset exists, and a green run over it would certify clips that make
			# bodies skate. Both halves or nothing.
			imported = _import(rel, folder)
			if imported is not None and _normalize(rel, imported, folder):
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
