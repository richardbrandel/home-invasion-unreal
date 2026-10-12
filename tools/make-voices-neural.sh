#!/bin/bash
# Generate the bark lines with a LOCAL NEURAL text-to-speech model, instead of macOS `say`.
#
#     ssh ubuntu-dev '...'   is not needed -- this runs on the Mac and writes into SourceAssets
#     tools/make-voices-neural.sh [outdir]
#
# WHY THIS EXISTS. `tools/make-voices.sh` uses macOS's built-in `say`, which is a formant
# synthesiser: free, offline, and unmistakably robotic. Richard's objection was the right one
# -- "AI can generate complex spreadsheets and you can't generate .wav files" -- and it was
# correct about `say`: this Mac has **177 voices and ZERO of them Premium or Enhanced**, so
# `say` was never going to sound like a person.
#
# Kokoro is a small neural TTS model (82M parameters) that runs on this machine through
# `mlx_audio`. It is free, offline, no API key, no per-line cost, and it is a genuine neural
# voice rather than a formant one. The whole model is a few hundred MB.
#
# WHAT IT NEEDS, installed once:
#
#     export PATH="$HOME/.local/bin:$PATH"
#     V=~/.dsh/tools/.embed-venv
#     VIRTUAL_ENV=$V uv pip install "misaki[en]"          # G2P for Kokoro
#     VIRTUAL_ENV=$V uv pip install \
#       https://github.com/explosion/spacy-models/releases/download/en_core_web_sm-3.8.0/en_core_web_sm-3.8.0-py3-none-any.whl
#
# The spaCy model is the awkward one: Kokoro's English G2P needs it, and it is not on PyPI, so
# it has to come from explosion's GitHub releases as a wheel. The version must match the
# installed spaCy (3.8.x here).
set -u

OUT=${1:-${OUT:-/data/projects/HIUnreal/SourceAssets/voice}}
TTS="$HOME/.dsh/tools/.embed-venv/bin/mlx_audio.tts.generate"
MODEL=${MODEL:-mlx-community/Kokoro-82M-bf16}

# Two voices that are audibly different people, as in the `say` script: the intruders are
# British and the homeowner is American, matching THIEF_VOICE=Daniel / OWNER_VOICE=Reed.
THIEF_VOICE=${THIEF_VOICE:-bm_george}   # en_GB male
OWNER_VOICE=${OWNER_VOICE:-am_michael}  # en_US male

if [ ! -x "$TTS" ]; then
	echo "make-voices-neural: no mlx_audio at $TTS" >&2
	exit 1
fi

mkdir -p "$OUT"
echo "generating NEURAL barks into $OUT  (model $MODEL)"

line() {
	local voice="$1" name="$2" text="$3"
	local tmp
	tmp=$(mktemp -d)
	# `--join_audio` writes one file named exactly <prefix>.wav. WITHOUT it the tool writes
	# NUMBERED SEGMENTS (<prefix>_000.wav, <prefix>_001.wav, ...) and a check for
	# <prefix>.wav silently finds nothing -- which is how the first version of this script
	# reported nine failures while generating nine perfectly good files.
	"$TTS" --model "$MODEL" --text "$text" --voice "$voice" \
		--output_path "$tmp" --file_prefix "$name" --join_audio >/dev/null 2>&1

	local produced
	produced=$(ls "$tmp/$name"*.wav 2>/dev/null | head -1)
	if [ -z "$produced" ]; then
		echo "  FAILED  $name" >&2
		rm -rf "$tmp"
		return 1
	fi

	# Kokoro emits 24 kHz mono. The game's other audio is 48 kHz mono 16-bit PCM, and the
	# importer and the engine are both happier when everything agrees, so resample here --
	# exactly as the `say` script does, and with the same tool.
	afconvert -f WAVE -d LEI16@48000 -c 1 "$produced" "$OUT/$name.wav" || {
		echo "  FAILED to convert $name" >&2
		rm -rf "$tmp"
		return 1
	}
	rm -rf "$tmp"
	printf "  %-22s %-11s %s\n" "$name.wav" "$voice" "$text"
}

# ---- the intruders
line "$THIEF_VOICE" thief_spot      "Someone's home. Move."
line "$THIEF_VOICE" thief_order     "Get the stuff and go."
line "$THIEF_VOICE" thief_pain      "Ah! He got me."
line "$THIEF_VOICE" thief_shoved    "Get off me!"
line "$THIEF_VOICE" thief_flee      "Forget it, let's go!"
line "$THIEF_VOICE" thief_down      "I can't... I can't."
# ---- the homeowner
line "$OWNER_VOICE" owner_challenge "Get out of my house!"
line "$OWNER_VOICE" owner_shout     "I said get out!"
line "$OWNER_VOICE" owner_police    "I'm calling the police!"

echo "--- generated ---"
ls -la "$OUT"/*.wav 2>/dev/null | awk '{printf "  %-34s %s bytes\n", $NF, $5}'
