#!/bin/bash
# Generate the bark lines as .wav files, offline and free, using macOS's own voices.
#
# WHY THIS EXISTS
# ---------------
# Fix-list item 16 says "synthesised audio cannot do voices; this needs either recording or a
# barking system with a different source". Both halves of that are worth looking at again:
# the barking system EXISTS and fires at the right moments, and the thing it plays is a sine
# tone at a shifted pitch -- not "synthesised audio cannot do voices" but "nobody tried the
# speech synthesiser that already ships with this machine".
#
# macOS ships 177 voices and `say` writes straight to a file; these are offline, free, and
# need no account. The result is clearly synthetic next to a recording, and it is still
# enormously better than a sine: the words are intelligible, the timing is right, and the
# player hears a man rather than a piece of equipment. A real recording is still the right
# end state -- see SOURCE ASSETS in the README -- but it is no longer the only option.
#
# Output goes to SourceAssets/voice/, which tools/import-assets.py imports.
#
# Usage: make-voices.sh [outdir]     (or set OUT)
set -eu

OUT=${1:-${OUT:-/data/projects/HIUnreal/SourceAssets/voice}}
mkdir -p "$OUT"

# The intruders: lower and slower, because a man breaking into a house talks quietly.
# Both defaults were CHECKED against `say -v '?'` on this Mac. `Alex` -- the obvious US male
# -- is NOT installed here (it is a downloadable voice on current macOS), and a missing voice
# makes `say` leave a silent or absent file behind. Hence the check below, rather than a
# hopeful default that looks like it worked.
THIEF_VOICE=${THIEF_VOICE:-Daniel}   # en_GB male
OWNER_VOICE=${OWNER_VOICE:-Reed}     # en_US male, and audibly a different person

for v in "$THIEF_VOICE" "$OWNER_VOICE"; do
	if ! say -v '?' | grep -qE "^$v[[:space:]]"; then
		echo "make-voices: voice '$v' is not installed on this machine" >&2
		echo "  installed en_US / en_GB voices:" >&2
		say -v '?' | grep -E "en_US|en_GB" | awk '{print "    " $1}' >&2
		exit 1
	fi
done

# rate is words per minute; `say` defaults to about 175, which is too brisk for menace.
say_line() {
	local voice="$1" rate="$2" name="$3" text="$4"
	# An .aiff first, then afconvert: `say -o file.wav` produces a WAVE that some importers
	# refuse, and afconvert is the documented path to a 48 kHz mono 16-bit PCM file.
	say -v "$voice" -r "$rate" -o "$OUT/$name.aiff" "$text"
	afconvert -f WAVE -d LEI16@48000 -c 1 "$OUT/$name.aiff" "$OUT/$name.wav"
	rm -f "$OUT/$name.aiff"
	printf "  %-22s %-8s %s\n" "$name.wav" "$voice" "$text"
}

echo "generating barks into $OUT"
# ---- the intruders
say_line "$THIEF_VOICE" 150 thief_spot      "Someone's home. Move."
say_line "$THIEF_VOICE" 150 thief_order     "Get the stuff and go."
say_line "$THIEF_VOICE" 165 thief_pain      "Ah! He got me."
say_line "$THIEF_VOICE" 160 thief_shoved    "Get off me!"
say_line "$THIEF_VOICE" 170 thief_flee      "Forget it, let's go!"
say_line "$THIEF_VOICE" 140 thief_down      "I can't... I can't."
# ---- the homeowner
say_line "$OWNER_VOICE" 165 owner_challenge "Get out of my house!"
say_line "$OWNER_VOICE" 170 owner_shout     "I said get out!"
say_line "$OWNER_VOICE" 155 owner_police    "I'm calling the police!"

echo "--- generated ---"
ls -la "$OUT"/*.wav | awk '{printf "  %-34s %s bytes\n", $NF, $5}'
