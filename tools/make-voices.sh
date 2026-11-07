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
# Voice casting is deliberate: en_GB-alba (woman, British) is the homeowner, en_GB-northern_english_male
# is the brute, en_US-amy is Alina (flirty), en_US-lessac is the Dancer (the one who is frightened).
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
dl en_GB-northern_english_male-medium en/en_GB/northern_english_male/medium
dl en_US-amy-medium                 en/en_US/amy/medium
dl en_US-lessac-medium              en/en_US/lessac/medium

say() {  # say <voice> <asset-name> <text>
  echo "$3" | "$VENV/bin/piper" -m "$SRC/$1.onnx" -f "$WAV/$2.wav" 2>/dev/null
}

# ---- the homeowner: a woman, British
say en_GB-alba-medium owner_getout  "Get out"
say en_GB-alba-medium owner_cell_1  "How do you like this"
say en_GB-alba-medium owner_cell_2  "It's party time"
say en_GB-alba-medium owner_cell_3  "Awww, it's going to hurt more"
# ---- the brute: a deep man
say en_GB-northern_english_male-medium brute_1 "You hit like a girl"
say en_GB-northern_english_male-medium brute_2 "Doesn't hurt"
say en_GB-northern_english_male-medium brute_3 "Loser"
# ---- Alina: flirts, then threatens
say en_US-amy-medium alina_1 "I kind of like this"
say en_US-amy-medium alina_2 "Set me free and I will be your girlfriend"
say en_US-amy-medium alina_3 "Yeah baby"
say en_US-amy-medium alina_4 "Yes, I have been bad"
say en_US-amy-medium alina_5 "My boyfriend is a lawyer, I will see you in court"
# ---- the Dancer: the one who is actually frightened
say en_US-lessac-medium dancer_1 "Ouch"
say en_US-lessac-medium dancer_2 "Stop, it hurts"
say en_US-lessac-medium dancer_3 "Ouch ouch ouch"
say en_US-lessac-medium dancer_4 "Yaooooo"
say en_US-lessac-medium dancer_5 "I will be good"

echo "rendered $(ls "$WAV"/*.wav | wc -l) lines into $WAV"
# ---- import them as sound assets (the editor is needed for that, not for the rendering)
"${UE_CMD:-/data/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd}" "$PWD/HIUnreal.uproject" \
  -unattended -nosplash -nopause -NullRHI \
  -ExecutePythonScript="$PWD/tools/import-voices.py" -log -abslog=/tmp/import_voices.log >/dev/null 2>&1
grep -a "^VOICE" /tmp/import_voices.log || echo "see /tmp/import_voices.log"
