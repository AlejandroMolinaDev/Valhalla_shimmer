#!/usr/bin/env python3
"""Spectrogram + envelope plots for WAV files rendered with `ab_render`.

Usage:
    python3 tools/spectrogram.py renders/octave_chord.wav [more.wav ...] -o renders/plot.png

Requires numpy and matplotlib. Reads 16-bit PCM or 32-bit float WAV files.
"""
import argparse
import struct
import sys

import numpy as np


def read_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a RIFF/WAVE file")
    pos, fmt, samples = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            samples = body
        pos += 8 + size + (size & 1)
    tag, channels, rate, _, _, bits = fmt
    dtype = np.float32 if tag == 3 else {16: np.int16, 32: np.int32}[bits]
    x = np.frombuffer(samples, dtype=dtype).astype(np.float64)
    if tag != 3:
        x /= float(np.iinfo(dtype).max)
    return x.reshape(-1, channels), rate


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wavs", nargs="+")
    ap.add_argument("-o", "--output", default="spectrogram.png")
    ap.add_argument("--fmax", type=float, default=8000.0)
    args = ap.parse_args()

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(len(args.wavs), 2, figsize=(14, 3.2 * len(args.wavs)), squeeze=False,
                             gridspec_kw={"width_ratios": [3, 1]})
    for row, path in zip(axes, args.wavs):
        x, sr = read_wav(path)
        mono = x.mean(axis=1)
        nfft, hop = 4096, 1024
        win = np.hanning(nfft)
        frames = [mono[i:i + nfft] * win for i in range(0, len(mono) - nfft, hop)]
        spec = 20 * np.log10(np.abs(np.fft.rfft(frames, axis=1)).T + 1e-9)
        t = np.arange(spec.shape[1]) * hop / sr
        f = np.fft.rfftfreq(nfft, 1 / sr)
        row[0].pcolormesh(t, f, spec, shading="auto", vmin=spec.max() - 90, vmax=spec.max(), cmap="magma")
        row[0].set_ylim(20, args.fmax)
        row[0].set_yscale("log")
        row[0].set_title(path)
        row[0].set_ylabel("Hz")

        block = int(sr * 0.02)
        env = [np.sqrt(np.mean(x[i:i + block] ** 2)) for i in range(0, len(x) - block, block)]
        row[1].plot(np.arange(len(env)) * 0.02, 20 * np.log10(np.array(env) + 1e-9))
        row[1].set_ylim(-100, 0)
        row[1].set_title("RMS envelope (dBFS)")
        row[1].grid(alpha=0.3)

    fig.tight_layout()
    fig.savefig(args.output, dpi=90)
    print(f"wrote {args.output}")


if __name__ == "__main__":
    sys.exit(main())
