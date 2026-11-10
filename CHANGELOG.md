# Changelog

## 0.1.0-dev (2026-11-10) — first vertical slices, simulator-tested

Not yet installed on a physical FM-1.

Platform
- Monorepo on fm1-base `b0976f2` (submodule); two firmware targets: `breaksurgeon.fwsc` (FM-1_8300) and
  `abyssengine.fwsc` (FM-1_8400); host-native engine tests, audio-regression renders, fm1-base host-simulator
  scripts, fm1-emu runs.
- Shared engines: single-precision always-inline math, TPT SVF, polyBLEP, envelopes, delay lines; the step
  sequencer and clock (sample-accurate, half-step look-ahead, seeded probability, deterministic event order, MIDI
  clock follow); the command ring; the sweetspot graph; JUNGLE LINK v1; versioned CRC'd store records.

Break Surgeon
- Break player (two-voice crossfade, reverse, gate, pitch, tempo repitch, microloop); JOB1 bank in flash with a
  SysEx loader; a generated test break.
- Six drum models with four macros each; 7 tracks × 32 steps; velocity, accent, probability, ratchets,
  microtiming, swing, two parameter locks a step; mute/solo; fills; ghost generator; mutation with anchors and undo;
  BREAK-LOCK; seven performance modes and RETURN; rhythm and kit graphs; six scenes; DRUM PRESSURE with five
  presets; six pages.

Abyss Engine
- FM CREATURE and REESE MATRIX through a 10-node graph; clean sub folded to 36–72 Hz; four stab models; a 4-voice
  pad; 3-track harmonic sequencer with slides and MORPH/BITE/octave locks; scales and chords; delay and reverb
  sends; kick ducking from the link; NEURO PRESSURE; voice budget modes; six scenes; six pages; velocity keyboard.

Site
- GitHub Pages (https://rahelpuest.github.io/jungle-os-fm1/): both firmwares in the browser (fm1-base's emulator
  with Web MIDI and the data area), their manuals, web installers and release packages
  (`breaksurgeon-0.1.0-dev.fwsc` FM-1_83000100, `abyssengine-0.1.0-dev.fwsc` FM-1_84000100), the live set;
  `tools/make_site.sh`.

Tools
- `tools/sample-prep/jo_sample_prep.py` (WAV/AIFF → JOB1, resampling, onsets, slice snapping, heuristic classes),
  `jo_send_bank.py` (SysEx transfer; simulator scripts).

Fixed during development (found by the tests)
- Absolute event time kept in an integer (a float time base loses samples after ~6 minutes).
- Simultaneous events fired in a history-dependent order; now due time, then track, then hit.
- The sub could fall to 27 Hz with the default bass line; now folded into 36–72 Hz and 4 dB lower.
- PERC's RIM and CLICK overshot to 1.66; bounded.
- The clock follower dropped back to the internal clock between clocks when no heartbeat came; now it tracks
  clocks themselves (500 ms timeout).
