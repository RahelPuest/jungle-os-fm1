# Testing

## Automated (no FM-1 needed)

| Suite | Command | What it checks |
|---|---|---|
| Unit + integration | `make -C tests` | math accuracy; saturator bounds; SVF stability at max resonance; PRNG determinism; sequencer timing at 174 BPM to the sample, 20 minutes without drift, swing/micro/ratchets, seeded probability, mute/solo, locks, MIDI clock following with jitter; drums bounded/finite/decaying for 27 macro combinations per model, retrigger de-click; bank parse + rejection (truncated, CRC, bad slice table); break player bounds (both directions, ±24 st, microloops); crossfade without a click; graph interpolation (log midpoint, monotonic, discrete), mutation bounds and determinism, history, validation; link protocol (PREPARE/COMMIT, lost/duplicate commits, rate limit, heartbeat, channel filter, version mismatch); store records (magic, version, length, CRC; CRC-32 check value); both master buses under +18 dB noise at max settings stay under −0.5 dBFS; harmony; engine scenarios A–F below |
| Audio regression | `make -C tests renders` | 8 scenarios rendered to WAV; bounds (peak ≤ 0.95, no clipped or non-finite samples, \|DC\| < 0.01, not silent) and a hash against `tests/audio-regression/golden.txt` (`JO_REGOLD=1` after an intended change). `python3 -I tests/audio-regression/analyze.py tests/build/renders` prints band energies and the side energy below 120 Hz |
| Sample prep | `python3 tests/tools/test_sample_prep.py` | WAV 24-bit stereo reading, resampling level, bank round trip, alignment, CRC rejection, slice classes, the size limit |
| Whole apps | `tests/host/*.txt` in each firmware's host simulator (`tools/build_all.sh`) | pages, gestures, scene switch, save refused while playing, save + reboot persistence, a link clock and kicks arriving; a tour of every page, track and part with SEL and SAVE held: a text cut to fit its box fails the run; screenshots |
| Bank over SysEx | `jo_send_bank.py BANK --host-script X.txt`, then run X in the Break Surgeon simulator | every frame of a transfer answered as expected; the bank attached; kept after a reboot |
| Device binary | fm1-emu (docs/development/profiling.md) | it boots, plays, makes sound; CPU lower bounds |

### Acceptance scenarios (host)

| | Scenario | Where | Result |
|---|---|---|---|
| A | 174 BPM: sliced break + synth kick/snare/hats, a one-bar fill, back | `test_A_fill_and_revert`, render A | PASS (host) |
| B | mutate with anchors, undo, a quantized scene change | `test_B_mutate_anchors_undo_scene`, render B | PASS (host) |
| C | FM growl over the sub, stabs over a held pad | `test_C_voices_and_F_standalone`, render C | PASS (host) |
| D | kick-linked ducking, stable sub, no master clipping | `test_D_ducking`, render D | PASS (host) |
| E | morph through ≥ 5 FM nodes with controlled loudness | `test_E_morph_loudness` (RMS spread of the 8 path nodes: 0.0 dB after trim calibration — a proxy), render E | PASS (host); by ear: PENDING |
| F | boot, presets, invalid data → defaults, standalone | host scripts (first boot "DEFAULTS USED", reboot "OK"), `test_store_records`, clock fall-back | PASS (host) |
| G | two FM-1s: synchronized start, tempo, fills, scenes | — | **PENDING** (hardware) |

## On hardware (not done yet)

Each step needs a unit and your go-ahead (docs/development/install-and-recovery.md).

1. Install Break Surgeon; it boots to PERFORM; the SYSTEM page shows CPU and no dropouts while stopped.
2. PLAY at 174 BPM: steady timing, the load figure with everything playing (record it in profiling.md), no XRUNS
   after 10 minutes; hold each performance mode; DESTROY + PRESSURE max for a minute.
3. SAVE when stopped; power off and on; the patterns are back. SAVE while playing is refused.
4. A bank over SysEx; power cycle; it is still there.
5. Abyss Engine: the same, then everything at once (bass, sub, pad hold, stab chords), MORPH sweeps, VOICES modes.
6. Velocity on the keys (APP_TOUCH) feels right; LED states readable in the dark.
7. Link: each route in docs/architecture/link-protocol.md; start/stop, tempo changes, a scene on A lands on B on
   the same bar, kicks duck B. Measure the offset between the two outputs on a two-channel recorder.
8. A 20-minute run of docs/user-guide/live-performance.md without the computer.

## Listening checklist (sweetspots, mixes)

For every node and scene, on headphones, near-field monitors and (when possible) a PA with a sub:
- the sub: one clean note, no wobble from other parts, no flub on the PA; mono sum unchanged
- kick and sub: the kick's thump is not masked; ducking audible but not pumping unless meant to
- no clicks at slice changes, retriggers, scene changes, model switches, pattern changes
- aliasing: no metallic whistle at high notes in SNARL / LASER / BROKEN (FM index is Carson-limited; listen)
- loudness: MORPH end to end at a fixed MASTER: no node jumps out or drops away (the RMS trims are a starting point)
- the clipper/guard: drums at PRESSURE max — crunchy on purpose, but transients still there
Mark a node `JG_REVIEWED` in the source only after it passes, with the date and listener in the commit message.
