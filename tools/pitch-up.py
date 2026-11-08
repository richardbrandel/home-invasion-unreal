#!/usr/bin/env python3
"""Raise a WAV's pitch by resampling it -- no time-stretch, so it also gets slightly shorter.

Used for the Dancer (Richard, 2026-11-05: "I would like the voice to be higher, more girlish"). A
resample is the honest cheap way to do that: pitch AND formants go up together, which is what reads
as younger rather than as a chipmunk, and for one-second barks the 11% shorter duration is inaudible.

Pure numpy on purpose -- no scipy dependency, so this runs anywhere python3 and numpy exist.
"""
import sys, wave
import numpy as np


def pitch(src, dst, factor):
    with wave.open(src, "rb") as r:
        params = r.getparams()
        raw = r.readframes(params.nframes)
    data = np.frombuffer(raw, dtype=np.int16).astype(np.float64)
    channels = params.nchannels
    if channels == 2:
        data = data.reshape(-1, 2)
    n_in = data.shape[0]
    n_out = int(round(n_in / factor))
    # Linear interpolation onto the shorter time base: this is a resample, so the same samples play
    # back faster and therefore higher.
    src_idx = np.linspace(0.0, n_in - 1.0, n_out)
    if channels == 2:
        out = np.stack([np.interp(src_idx, np.arange(n_in), data[:, c]) for c in range(2)], axis=1)
    else:
        out = np.interp(src_idx, np.arange(n_in), data)
    with wave.open(dst, "wb") as w:
        w.setparams(params)
        w.setnframes(n_out)
        w.writeframes(np.clip(out, -32768, 32767).astype(np.int16).tobytes())


if __name__ == "__main__":
    pitch(sys.argv[1], sys.argv[2], float(sys.argv[3]))
