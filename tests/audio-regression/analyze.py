# SPDX-License-Identifier: MPL-2.0
"""Measures the host renders: peak, near-peak density, crest factor, band energies, and for stereo files the
side energy below 120 Hz (WIDTH must not widen the sub).  python3 -I audio-regression/analyze.py build/renders"""
import glob, os, sys, wave
import numpy as np

def load(f):
    w = wave.open(f)
    x = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(float) / 32768
    return x[0::2], x[1::2], w.getframerate()

def bands(x, sr):
    n = min(len(x), 2 ** 19)
    X = np.abs(np.fft.rfft(x[:n] * np.hanning(n))) ** 2
    fr = np.fft.rfftfreq(n, 1 / sr)
    tot = X.sum() + 1e-20
    return lambda a, b: 10 * np.log10(X[(fr >= a) & (fr < b)].sum() / tot + 1e-12)

for f in sorted(glob.glob(os.path.join(sys.argv[1] if len(sys.argv) > 1 else 'build/renders', '*.wav'))):
    l, r, sr = load(f)
    m = 0.5 * (l + r)
    pk = max(abs(l).max(), abs(r).max())
    b = bands(m, sr)
    side = 0.5 * (l - r)
    sb = bands(side, sr)
    side_ratio = 10 * np.log10((side ** 2).mean() / ((m ** 2).mean() + 1e-20) + 1e-12)
    print(f"{os.path.basename(f)[:-4]:26s} pk {pk:.3f} near {100 * (abs(m) > 0.97 * pk).mean():4.2f}% "
          f"crest {20 * np.log10(pk / np.sqrt((m ** 2).mean() + 1e-20)):5.1f} | <60 {b(20, 60):5.1f} 60-200 {b(60, 200):5.1f} "
          f"200-2k {b(200, 2000):5.1f} 2-8k {b(2000, 8000):5.1f} >8k {b(8000, 22000):5.1f} | side {side_ratio:6.1f} dB"
          + (f", side<120Hz {sb(20, 120) + side_ratio:6.1f} dB" if side_ratio > -90 else ""))
