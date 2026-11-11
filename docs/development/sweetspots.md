# Sweetspots: how a node becomes certified

The graphs' nodes are design hypotheses, all `JG_UNREVIEWED` today:
- Abyss bass (0.7): DUB, ROLLER, REESE, REESE DARK, HOOVER, GROWL, TALK, SNARL (the MORPH path), WOMP, WUB, FOGHORN,
  808, LASER, METALLIC, TECH STAB, BROKEN (`firmware/abyss-engine/engine/ab_engine.c` `FMN[]`, physical units; the
  archetypes behind them: docs/research/dnb-bass-styles.md; trims measured by `test_ab_sounds`)
- Break Surgeon rhythm: CLEAN, ROLLING, CHOPPED, GLITCHED; kit: DUSTY ROLLER, JUNGLE SNAP, TIGHT NEURO, METAL
  TECHSTEP, INDUSTRIAL (`bs_engine.c` `graphs_init`)

## Steps

1. **Measured checks** (automated): finite, bounded output across the node and its edges (`render` scenario
   `abuse_extremes`, `test_E_morph_loudness`); loudness within the spread; no DC.
2. **Loudness trim**: `test_E_morph_loudness` prints each path node's RMS on a held D2 (`make -C tests`). The trims
   in `FMN[]` were set from that print toward −13 dBFS (2026-11-10). RMS over-rates bass-heavy and under-rates
   bright sounds: correct by ear, then re-run the test to keep the spread small.
3. **Listening** (docs/development/testing.md checklist) on at least headphones and monitors; at 160, 174 and 188
   BPM; at the node itself and halfway to each neighbour.
4. Mark it: `review = JG_REVIEWED` for that node, the listener and date in the commit message, the golden hashes
   updated (`JO_REGOLD=1 make -C tests renders`).

## Parameter ranges (hypotheses, adjustable)

FM CREATURE: ratio 0.5–8 (log), index 0–12 (further limited per note so the sidebands stay under 0.42 fs —
Carson's rule), feedback 0–0.85 (averaged two-sample feedback for stability), cutoff 80–12,000 Hz (log),
resonance 0–0.9, drive 1–10 (log), glide 1–150 ms (log). Discrete: model, LFO division (switch at the segment's
middle, model switches crossfade over ~5 ms).

## Transferable techniques used (no proprietary algorithms, presets or assets copied)

- Two-operator FM with modulator feedback and index envelopes: J. Chowning, "The Synthesis of Complex Audio
  Spectra by Means of Frequency Modulation", JAES 1973; Yamaha DX-series feedback operator (general technique).
- Carson's bandwidth rule for limiting FM index against aliasing (classic communications result).
- PolyBLEP oscillators: Välimäki & Huovilainen, "Antialiasing Oscillators in Subtractive Synthesis", IEEE SPM 2007.
- Topology-preserving SVF: V. Zavalishin, "The Art of VA Filter Design" (2012–2018).
- Formant (vowel) movement by parallel band-passes at averaged adult F1/F2 values.
- The neuro production split into a clean mono sub and a separately distorted mid bass, and "resampled" movement
  through curated states (common practice in DnB production tutorials; implemented here as graph navigation, not
  audio resampling).
- Feedback delay network reverb with a Householder matrix: Jot & Chaigne 1991; J. O. Smith, "Physical Audio
  Signal Processing".
