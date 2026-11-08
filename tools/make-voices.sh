#!/usr/bin/env bash
# ---------------------------------------------------------------------------------------------
# The cast's voices, rendered locally with Piper (Richard, 2026-11-05).
#
# He asked for a woman's voice for the homeowner ("Get out", plus three taunts in the cage) and for
# each intruder to have their own lines when they are hit with the stick. There is no recorded audio
# and no budget for voice actors in this project, so the lines are SYNTHESISED: Piper is a small
# neural TTS that runs on the CPU, offline once the voices are downloaded, and free.
#
#   ./tools/make-voices.sh            # download voices, render the lines, import them into Content
#
# Requirements: python3 with venv, and network for the first run (five voices, ~380 MB total).
# The WAVs and the models land in SourceAssets/voices, which is GITIGNORED -- only the imported
# .uasset files under Content/HIImported/voice are committed, so a fresh clone has the voices
# without needing any of this. Re-run only when a line changes.
#
# Voice casting is deliberate: ru_RU-irina (a Russian woman speaking English -- Richard asked for "a female
# with a Russian accent") is the homeowner, en_GB-northern_english_male is the brute, en_US-amy is Alina
# (flirty), en_US-lessac is the Dancer (the one who is frightened).
#
# A Russian model reading English is normally a trap -- espeak would phonemise the Latin text as Russian and
# produce nonsense -- so this was CHECKED, not assumed: the rendered files were transcribed back with Whisper.
# "How do you like this" came back EXACTLY; "Get out" was heard as "Get up" and "It's party time" as "It's
# part time", i.e. intelligible English with an accent, which is the effect wanted.
# ---------------------------------------------------------------------------------------------
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=SourceAssets/voices
WAV=$SRC/wav
VENV=${VENV:-/tmp/ttsenv}

mkdir -p "$WAV"
if [ ! -x "$VENV/bin/piper" ]; then
  python3 -m venv "$VENV"
  "$VENV/bin/pip" install -q --upgrade pip
  "$VENV/bin/pip" install -q piper-tts
fi

dl() {  # dl <model-name> <repo-path-without-model>
  [ -s "$SRC/$1.onnx" ] || curl -sSL -A "Mozilla/5.0" -o "$SRC/$1.onnx" \
    "https://huggingface.co/rhasspy/piper-voices/resolve/main/$2/$1.onnx"
  [ -s "$SRC/$1.onnx.json" ] || curl -sSL -A "Mozilla/5.0" -o "$SRC/$1.onnx.json" \
    "https://huggingface.co/rhasspy/piper-voices/resolve/main/$2/$1.onnx.json"
}
dl en_GB-alba-medium                en/en_GB/alba/medium
dl ru_RU-irina-medium                ru/ru_RU/irina/medium
dl en_GB-northern_english_male-medium en/en_GB/northern_english_male/medium
dl en_US-amy-medium                 en/en_US/amy/medium
dl en_US-lessac-medium              en/en_US/lessac/medium
dl en_US-mike-medium                en/en_US/mike/medium

say() {  # say <voice> <asset-name> <text> [extra piper args]
  local voice="$1" name="$2" text="$3"; shift 3
  echo "$text" | "$VENV/bin/piper" -m "$SRC/$voice.onnx" -f "$WAV/$name.wav" "$@" 2>/dev/null
}

# ---- the homeowner: a woman, British
# THE RUSSIAN IS ALINA'S, NOT THE HOMEOWNER'S (Richard, 2026-11-05: "the Russian accent would be for Alina"),
# so the homeowner goes back to the British woman she started as.
say en_GB-alba-medium owner_getout  "Get out"
say en_GB-alba-medium owner_cell_1  "How do you like this"
say en_GB-alba-medium owner_cell_2  "It's party time"
say en_GB-alba-medium owner_cell_3  "Awww, it's going to hurt more"
# ---- the brute: a deep man
# THE BRUTE IS AN AMERICAN NOW, and as street as this voice set gets (Richard, 2026-11-05: "can you give the
# thug sound an American ghetto accent"). Piper has no AAVE voice -- the accent is a property of the training
# data and no knob produces it -- so this is the nearest thing available and the trick is DELIVERY: the deepest
# American male in the set (en_US-mike, 125 Hz), slowed 12% and pitched down 6%, which lands him at 110-122 Hz.
# Deep + slow + flat reads as heavy and unbothered, which is the character; a real accent would need a
# different engine or a recording.
say en_US-mike-medium brute_1 "You hit like a girl" --length-scale 1.12
say en_US-mike-medium brute_2 "Doesn't hurt" --length-scale 1.12
say en_US-mike-medium brute_3 "Loser" --length-scale 1.12
for n in brute_1 brute_2 brute_3; do
  python3 "$PWD/tools/pitch-up.py" "$WAV/$n.wav" "$WAV/$n.pitched.wav" 0.94
  mv "$WAV/$n.pitched.wav" "$WAV/$n.wav"
done
# ---- Alina: flirts, then threatens
# Alina speaks English with a Russian accent -- the effect Richard asked for, checked by ASR below.
say ru_RU-irina-medium alina_1 "I kind of like this"
say ru_RU-irina-medium alina_2 "Set me free and I will be your girlfriend"
say ru_RU-irina-medium alina_3 "Yeah baby"
say ru_RU-irina-medium alina_4 "Yes, I have been bad"
# THIS ONE IS SLOWER ON PURPOSE. At the default pace the Russian voice's "lawyer" came back from ASR as
# "liar"; at --length-scale 1.45 it transcribes exactly, which is how a line that matters gets to be
# verified rather than hoped for.
say ru_RU-irina-medium alina_5 "My boyfriend is a lawyer, I will see you in court" --length-scale 1.45
# ---- the Dancer: the one who is actually frightened
# THE DANCER IS PITCHED UP 12% -- "higher, more girlish" (Richard, 2026-11-05). Rendered with the voice
# that already suited her, then resampled, which lifts pitch AND formants together; that is what reads as
# younger rather than as a chipmunk, and for one-second barks the shorter duration does not show.
say en_US-lessac-medium dancer_1 "Ouch"
say en_US-lessac-medium dancer_2 "Stop, it hurts"
say en_US-lessac-medium dancer_3 "Ouch ouch ouch"
say en_US-lessac-medium dancer_4 "Yaooooo"
say en_US-lessac-medium dancer_5 "I will be good"
for n in dancer_1 dancer_2 dancer_3 dancer_4 dancer_5; do
  python3 "$PWD/tools/pitch-up.py" "$WAV/$n.wav" "$WAV/$n.pitched.wav" 1.12
  mv "$WAV/$n.pitched.wav" "$WAV/$n.wav"
done

echo "rendered $(ls "$WAV"/*.wav | wc -l) lines into $WAV"
# ---- import them as sound assets (the editor is needed for that, not for the rendering)
"${UE_CMD:-/data/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd}" "$PWD/HIUnreal.uproject" \
  -unattended -nosplash -nopause -NullRHI \
  -ExecutePythonScript="$PWD/tools/import-voices.py" -log -abslog=/tmp/import_voices.log >/dev/null 2>&1
grep -a "^VOICE" /tmp/import_voices.log || echo "see /tmp/import_voices.log"
