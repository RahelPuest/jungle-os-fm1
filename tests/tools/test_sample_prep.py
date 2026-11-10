# SPDX-License-Identifier: MPL-2.0
"""tools/sample-prep: format round trip, rejection of corrupted banks, the size limit, input formats.
    python3 tests/tools/test_sample_prep.py"""
import os, struct, sys, tempfile, wave
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools", "sample-prep"))
import jo_sample_prep as sp

fails = 0
def check(c, what):
    global fails
    print(("ok   " if c else "FAIL ") + what)
    fails += not c

with tempfile.TemporaryDirectory() as d:
    tb = os.path.join(d, "tb.wav")
    sp.testbreak(tb)
    # a stereo 24-bit copy at 48 kHz, for the reader and the resampler
    x, r = sp.read_audio(tb)
    y = sp.resample(x, r, 48000)
    st = os.path.join(d, "st.wav")
    v = np.clip(np.round(y * 8388607), -8388608, 8388607).astype(np.int32)
    b = np.stack([v & 255, (v >> 8) & 255, (v >> 16) & 255], 1).astype(np.uint8)
    with wave.open(st, "wb") as w:
        w.setnchannels(2); w.setsampwidth(3); w.setframerate(48000)
        w.writeframes(np.concatenate([b, b], 1).tobytes())  # L and R the same
    x2, r2 = sp.read_audio(st)
    check(r2 == 48000 and abs(len(x2) - len(y)) <= 1, "24-bit stereo read as mono")
    check(np.max(np.abs(x2 - y)) < 1e-4, "24-bit samples exact")
    z = sp.resample(np.sin(2 * np.pi * 1000 * np.arange(44100) / 44100).astype(np.float32), 44100, 22059)
    check(abs(len(z) - 22059) <= 1 and 0.9 < np.max(np.abs(z[100:-100])) < 1.05, "resampled sine keeps its level")
    brk = sp.prepare(tb, 1, 16, "T", 22059, True, False)
    img = sp.pack_bank([brk, sp.prepare(tb, 1, 8, "E", 22059, False, False)])
    info = sp.parse_bank(img)
    check(info["count"] == 2 and info["breaks"][0]["slices"] == 16 and info["breaks"][1]["slices"] == 8, "round trip")
    check(abs(info["breaks"][0]["bpm"] - 170.0) < 0.1, "tempo from length and bars")
    check(struct.unpack_from("<I", img, 32 + 16)[0] % 4 == 0 and struct.unpack_from("<I", img, 32 + 256 + 16)[0] % 4 == 0,
          "PCM 4-aligned")
    bad = bytearray(img); bad[-5] ^= 1
    try: sp.parse_bank(bytes(bad)); ok = False
    except ValueError as e: ok = str(e) == "crc"
    check(ok, "corrupted PCM rejected (CRC)")
    check(brk["classes"][0] == "kick" and brk["classes"][4] == "snare", "slice classes: kick on 1, snare on 5")
    # 200,000 frames (400 KB) cannot fit the 241,664-byte region: the size the build step refuses
    big = len(sp.pack_bank([dict(brk, pcm=np.zeros(200000, "<i2"))])) > sp.BANK_MAX
    check(big, "over the flash region: detected")
sys.exit(1 if fails else 0)
