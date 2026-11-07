# Import every WAV in SourceAssets/voices/wav as a sound asset in /Game/HIImported/voice.
# Run by tools/make-voices.sh; the game loads them by name through FHIAudio::VoiceCue.
import unreal, os
SRC = os.path.join(os.path.dirname(os.path.abspath(unreal.Paths.project_dir())), "HIUnreal",
                   "SourceAssets", "voices", "wav")
if not os.path.isdir(SRC):
    SRC = "/data/projects/HIUnreal/SourceAssets/voices/wav"
DEST = "/Game/HIImported/voice"
tasks = []
names = sorted(f for f in os.listdir(SRC) if f.endswith(".wav"))
for f in names:
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(SRC, f))
    t.set_editor_property("destination_path", DEST)
    t.set_editor_property("destination_name", f[:-4])
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    t.set_editor_property("factory", unreal.SoundFactory())
    tasks.append(t)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
for f in names:
    a = unreal.EditorAssetLibrary.load_asset(DEST + "/" + f[:-4])
    print("VOICE %s %s %s" % (f[:-4], "ok" if a else "FAILED",
          ("%.2fs" % a.get_editor_property("duration")) if a else ""))
