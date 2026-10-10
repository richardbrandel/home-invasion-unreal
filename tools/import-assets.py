"""Import SOURCE files (wav, fbx, png, ...) into .uassets, headlessly.

Run through `tools/import-assets.sh`. This is the other half of the finding in
`tools/make-assets.py`: that file proves the project can AUTHOR an asset without an editor
session, and this one proves it can IMPORT one. Together they are what makes a shopping list
of source files useful -- a WAV or an FBX can be dropped in and turned into a game asset by a
script, so no one has to open a GUI to receive it.

SOURCE files are the thing worth asking a person for. A .uasset is an engine-specific binary
that only an editor can make; a .wav is a .wav.
"""

import os
import unreal

# Where the source files live on the build machine, and where the assets should land.
SRC = "/data/projects/HIUnreal/SourceAssets"
DEST = "/Game/HIImported"


def _import(rel_path, subdir):
	"""Import one file, returning the asset path or None."""
	full = os.path.join(SRC, rel_path)
	if not os.path.isfile(full):
		print("HI_IMPORT: MISSING %s" % full)
		return None

	task = unreal.AssetImportTask()
	task.set_editor_property("filename", full)
	task.set_editor_property("destination_path", "%s/%s" % (DEST, subdir))
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

	# Every .wav found, so dropping a file in is the whole instruction.
	wavs = sorted(f for f in os.listdir(os.path.join(SRC, "audio"))
		if f.lower().endswith(".wav")) if os.path.isdir(os.path.join(SRC, "audio")) else []
	if not wavs:
		print("HI_IMPORT: no .wav under %s/audio" % SRC)

	ok = 0
	for name in wavs:
		if _import(os.path.join("audio", name), "Audio") is not None:
			ok += 1
	print("HI_IMPORT: %d of %d wav files imported" % (ok, len(wavs)))
	print("HI_IMPORT: done")


main()
