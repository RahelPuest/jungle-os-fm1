#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 JUNGLE OS contributors
"""JUNGLE OS sample prep: user-owned breaks (WAV / AIFF) -> a JOB1 sample bank for BREAK SURGEON.

    jo_sample_prep.py build OUT.bank IN.wav[:bars[:slices[:name]]] ... [--rate 22059] [--snap] [--onsets]
    jo_sample_prep.py info BANK
    jo_sample_prep.py testbreak OUT.wav      (an original synthetic break to try the workflow with)

Each input: its bars (default: from --bpm or 1), its slices (8, 16, 32; default 16), its name (default the file's).
--snap moves each grid slice point to the nearest onset within an eighth of a slice; --onsets slices at the
detected onsets instead of a grid (up to 32). The tempo is the file's length over its bars (4/4).
The bank format is docs/development/sample-format.md; the firmware's parser is shared/sampler/jo_break.c, and
tests/tools/test_sample_prep.py checks the two agree. Needs numpy.

Only process audio you have the rights to. The repository ships no third-party breaks.
"""
import argparse, aifc, json, os, struct, sys, wave, zlib
import numpy as np

MAGIC, VERSION, HDR, ENTRY, MAXBREAKS, MAXSLICES = b"JOB1", 1, 32, 256, 8, 32
BANK_MAX = 0xDC000 - 0xA1000          # the flash region (firmware/break-surgeon/app.h BS_BANK_LO..HI)
CLASS = {"none": 0, "kick": 1, "snare": 2, "hat": 3, "other": 4}
FM1_RATE = 24_000_000 / 544           # 44,117.6 Hz


def read_audio(path):
    """-> (float32 mono in [-1, 1], rate)"""
    opener = aifc.open if path.lower().endswith((".aif", ".aiff")) else wave.open
    with opener(path, "rb") as f:
        ch, width, rate, n = f.getnchannels(), f.getsampwidth(), f.getframerate(), f.getnframes()
        raw = f.readframes(n)
    big = opener is aifc.open
    if width == 1:
        x = (np.frombuffer(raw, np.uint8).astype(np.float32) - 128.0) / 128.0
    elif width == 2:
        x = np.frombuffer(raw, ">i2" if big else "<i2").astype(np.float32) / 32768.0
    elif width == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        v = (b[:, 0] << 16 | b[:, 1] << 8 | b[:, 2]) if big else (b[:, 2] << 16 | b[:, 1] << 8 | b[:, 0])
        x = (np.where(v >= 1 << 23, v - (1 << 24), v)).astype(np.float32) / 8388608.0
    elif width == 4:
        x = np.frombuffer(raw, ">i4" if big else "<i4").astype(np.float32) / 2147483648.0
    else:
        raise SystemExit(f"{path}: {8 * width}-bit samples are not supported")
    x = x.reshape(-1, ch).mean(axis=1)
    return x, rate


def resample(x, src, dst):
    """windowed-sinc resampling (Blackman, 32 zero crossings), with the cutoff below the lower Nyquist"""
    if src == dst:
        return x.copy()
    ratio = dst / src
    n_out = int(round(len(x) * ratio))
    cutoff = min(1.0, ratio) * 0.94
    half = 32
    t = np.arange(n_out) / ratio
    out = np.zeros(n_out, np.float32)
    base = np.floor(t).astype(np.int64)
    frac = t - base
    for k in range(-half + 1, half + 1):
        idx = base + k
        d = k - frac
        w = 0.42 + 0.5 * np.cos(np.pi * d / half) + 0.08 * np.cos(2 * np.pi * d / half)
        h = cutoff * np.sinc(cutoff * d) * w
        valid = (idx >= 0) & (idx < len(x))
        out[valid] += (x[idx[valid]] * h[valid]).astype(np.float32)
    return out


def onsets(x, rate, max_n=64):
    """energy-flux onsets: frames of 256, half-wave rectified log-energy rise, peak-picked, refined back to the
    quietest point just before the rise (a slice starts on its transient, not on its middle)"""
    hop = 256
    n = len(x) // hop
    if n < 4:
        return []
    e = np.array([np.sum(x[i * hop:(i + 1) * hop] ** 2) for i in range(n)]) + 1e-9
    le = 10 * np.log10(e)
    flux = np.maximum(0, np.diff(le, prepend=le[0]))
    thr = np.median(flux) + 2.0 * np.std(flux)
    picks, last = [], -10
    for i in range(1, n - 1):
        if flux[i] > thr and flux[i] >= flux[i - 1] and flux[i] >= flux[i + 1] and i - last > 3:
            picks.append(i)
            last = i
    out = []
    for i in picks:
        a = max(0, (i - 1) * hop)
        seg = np.abs(x[a:i * hop + hop])
        out.append(a + int(np.argmin(seg[: max(1, len(seg) // 2)])))
    strengths = [flux[i] for i in picks]
    order = np.argsort(strengths)[::-1][:max_n]
    return sorted(out[j] for j in order)


def classify(x, rate, a, b):
    """a heuristic class of a slice from its first 40 ms: low band -> kick, broadband mid -> snare, highs -> hat.
    Used by BREAK-LOCK and mutation; marked heuristic in the metadata. Check by ear"""
    seg = x[a:min(b, a + int(0.04 * rate))]
    if len(seg) < 64 or np.sqrt(np.mean(seg ** 2)) < 0.02:
        return "none"
    # a class only for a slice that starts on an attack: its first 10 ms well above the 10 ms before it (else it
    # is the tail of the hit before)
    w = int(0.01 * rate)
    before = x[max(0, a - w):a]
    now = x[a:a + w]
    if len(before) >= w // 2 and np.sqrt(np.mean(now ** 2)) < 1.6 * np.sqrt(np.mean(before ** 2)) + 0.01:
        return "none"
    sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    f = np.fft.rfftfreq(len(seg), 1 / rate)
    tot = sp.sum() + 1e-12
    lo, mid, hi = sp[f < 150].sum() / tot, sp[(f >= 150) & (f < 2500)].sum() / tot, sp[f >= 5000].sum() / tot
    if lo > 0.45:
        return "kick"
    if hi > 0.35 and lo < 0.1:
        return "hat"
    if mid > 0.4:
        return "snare"
    return "other"


def prepare(path, bars, nslices, name, rate, snap, by_onsets, normalize_db=-1.0):
    x, src = read_audio(path)
    x = resample(x, src, rate)
    pk = float(np.max(np.abs(x))) if len(x) else 0.0
    if pk > 0:
        x *= (10 ** (normalize_db / 20)) / pk
    fades = int(0.002 * rate)                        # 2 ms fades at the ends: no click at the loop point
    x[:fades] *= np.linspace(0, 1, fades)
    x[-fades:] *= np.linspace(1, 0, fades)
    frames = len(x)
    bpm = 60.0 * 4 * bars / (frames / rate)
    det = onsets(x, rate)
    if by_onsets and det:
        starts = sorted(set([0] + det))[:MAXSLICES]
    else:
        starts = [int(round(frames * k / nslices)) for k in range(nslices)]
        if snap and det:
            win = frames / nslices / 8
            for k in range(1, len(starts)):
                near = min(det, key=lambda d: abs(d - starts[k]))
                if abs(near - starts[k]) <= win:
                    starts[k] = near
    starts = sorted(set(min(max(0, s), frames - 1) for s in starts))
    classes = [classify(x, rate, s, (starts[i + 1] if i + 1 < len(starts) else frames)) for i, s in enumerate(starts)]
    pcm = np.clip(np.round(x * 32767), -32768, 32767).astype("<i2")
    return {"name": (name or os.path.splitext(os.path.basename(path))[0])[:16], "pcm": pcm, "rate": int(round(rate)),
            "bpm": bpm, "bars": bars, "starts": starts, "classes": classes, "onsets": det, "source": os.path.basename(path)}


def pack_bank(breaks):
    if not 1 <= len(breaks) <= MAXBREAKS:
        raise SystemExit(f"a bank holds 1..{MAXBREAKS} breaks")
    off = HDR + ENTRY * len(breaks)
    entries, blobs = b"", b""
    for b in breaks:
        off = (off + 3) & ~3
        pad = off - (HDR + ENTRY * len(breaks) + len(blobs))
        blobs += b"\0" * pad
        e = bytearray(ENTRY)
        e[0:16] = b["name"].upper().encode("ascii", "replace")[:16].ljust(16, b"\0")
        frames = len(b["pcm"])
        struct.pack_into("<IIIHBBB", e, 16, off, frames, b["rate"], int(round(b["bpm"] * 100)), b["bars"],
                         len(b["starts"]), 0)
        for i, s in enumerate(b["starts"]):
            struct.pack_into("<I", e, 36 + 4 * i, s)
            e[164 + i] = CLASS[b["classes"][i]]
            e[196 + i] = 128
        entries += bytes(e)
        blobs += b["pcm"].tobytes()
        off += frames * 2
    body = entries + blobs
    total = HDR + len(body)
    head = MAGIC + struct.pack("<HHII", VERSION, len(breaks), total, zlib.crc32(body) & 0xFFFFFFFF) + b"\0" * 16
    return head + body


def parse_bank(img):
    """the firmware parser's rules (jb_bank_parse), for `info` and the tests"""
    if img[:4] != MAGIC:
        raise ValueError("magic")
    ver, count, total, crc = struct.unpack_from("<HHII", img, 4)
    if ver != VERSION:
        raise ValueError("version")
    if total > len(img) or count > MAXBREAKS or total < HDR + count * ENTRY:
        raise ValueError("length")
    if zlib.crc32(img[HDR:total]) & 0xFFFFFFFF != crc:
        raise ValueError("crc")
    out = []
    for i in range(count):
        e = img[HDR + i * ENTRY: HDR + (i + 1) * ENTRY]
        off, frames, rate, bpm100, bars, ns, flags = struct.unpack_from("<IIIHBBB", e, 16)
        starts = list(struct.unpack_from(f"<{ns}I", e, 36))
        if off & 3 or off < HDR + count * ENTRY or frames > (total - off) // 2 or not 8000 <= rate <= 48000:
            raise ValueError(f"entry {i} bounds")
        if not 1 <= ns <= MAXSLICES or any(b <= a for a, b in zip(starts, starts[1:])) or starts[-1] >= frames:
            raise ValueError(f"entry {i} slices")
        out.append({"name": e[:16].rstrip(b"\0").decode(), "frames": frames, "rate": rate, "bpm": bpm100 / 100,
                    "bars": bars, "slices": ns, "starts": starts, "classes": list(e[164:164 + ns])})
    return {"count": count, "bytes": total, "breaks": out}


def testbreak(path, rate=44100, bpm=170.0):
    """an original synthetic one-bar break (sine kick, noise snare, hats): no third-party audio"""
    n = int(rate * 60 / bpm * 4)
    x = np.zeros(n, np.float32)
    step = n // 16
    rng = np.random.default_rng(7)
    def hit(at, kind, vel):
        t = np.arange(min(n - at, int(0.4 * rate))) / rate
        if kind == "k":
            f = 50 + 200 * np.exp(-t / 0.012)
            y = np.sin(2 * np.pi * np.cumsum(f) / rate) * np.exp(-t / 0.12)
        elif kind == "s":
            y = (0.6 * rng.standard_normal(len(t)) + 0.5 * np.sin(2 * np.pi * 190 * t)) * np.exp(-t / 0.07)
        else:
            y = np.diff(rng.standard_normal(len(t) + 1)) * 0.3 * np.exp(-t / 0.02)
        x[at:at + len(t)] += vel * y.astype(np.float32)
    for i, c in enumerate("K..hS.hk.hK.S.hg"):
        if c in "Kk": hit(i * step, "k", 1.0 if c == "K" else 0.6)
        elif c == "S": hit(i * step, "s", 1.0)
        elif c == "g": hit(i * step, "s", 0.3)
        if c in "h" or i % 2 == 0: hit(i * step, "h", 0.5)
    x = np.clip(x / np.max(np.abs(x)) * 0.9, -1, 1)
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes((x * 32767).astype("<i2").tobytes())


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("out")
    b.add_argument("inputs", nargs="+")
    b.add_argument("--rate", type=float, default=FM1_RATE / 2, help="the bank's sample rate (default 22,059 Hz)")
    b.add_argument("--snap", action="store_true")
    b.add_argument("--onsets", action="store_true")
    i = sub.add_parser("info")
    i.add_argument("bank")
    t = sub.add_parser("testbreak")
    t.add_argument("out")
    a = ap.parse_args(argv)
    if a.cmd == "testbreak":
        testbreak(a.out)
        print(f"{a.out}: an original synthetic break, one bar at 170 BPM")
        return 0
    if a.cmd == "info":
        print(json.dumps(parse_bank(open(a.bank, "rb").read()), indent=1))
        return 0
    breaks = []
    for spec in a.inputs:
        parts = spec.split(":")
        path = parts[0]
        bars = int(parts[1]) if len(parts) > 1 and parts[1] else 1
        ns = int(parts[2]) if len(parts) > 2 and parts[2] else 16
        if ns not in (8, 16, 32):
            raise SystemExit(f"{spec}: slices are 8, 16 or 32")
        breaks.append(prepare(path, bars, ns, parts[3] if len(parts) > 3 else "", a.rate, a.snap, a.onsets))
    img = pack_bank(breaks)
    if len(img) > BANK_MAX:
        raise SystemExit(f"the bank is {len(img)} bytes; the FM-1's region holds {BANK_MAX} "
                         f"({BANK_MAX / 2 / a.rate:.1f} s of mono audio at {a.rate:.0f} Hz). Shorter breaks or fewer.")
    parse_bank(img)  # the firmware's rules, before anything is written
    open(a.out, "wb").write(img)
    meta = {"bank": os.path.basename(a.out), "bytes": len(img), "of": BANK_MAX, "breaks": [
        {k: (v if k != "pcm" else None) for k, v in br.items() if k != "pcm"} | {"frames": len(br["pcm"]),
         "seconds": round(len(br["pcm"]) / br["rate"], 3), "classes_note": "heuristic: check by ear"} for br in breaks]}
    with open(os.path.splitext(a.out)[0] + ".json", "w") as f:
        json.dump(meta, f, indent=1, default=float)
    for br in breaks:
        print(f"{br['name']:16s} {len(br['pcm']) / br['rate']:6.2f} s  {br['bpm']:6.1f} BPM  {len(br['starts']):2d} slices  "
              f"{''.join('.KSHo'[CLASS[c]] for c in br['classes'])}")
    print(f"{a.out}: {len(img)} of {BANK_MAX} bytes ({100 * len(img) / BANK_MAX:.0f}%)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
