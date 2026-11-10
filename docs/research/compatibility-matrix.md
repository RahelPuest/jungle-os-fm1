# Feature feasibility matrix

Status as of 2026-11-10 (CHANGELOG 0.1.0-dev).

- **VERIFIED**: works on a physical FM-1 (nothing is, yet: no device has run JUNGLE OS).
- **SIM-TESTED**: implemented, and passes automated tests on the host and/or in fm1-base's simulator; the device
  binary builds and runs in fm1-emu. Not yet on hardware.
- **IMPLEMENTED UNTESTED**: code exists, no automated test exercises it end to end.
- **PROTOTYPE**: a first version with known gaps.
- **DEFERRED**: designed or planned, not implemented.
- **BLOCKED**: not possible on this hardware, or waiting for something outside the code.

## Platform

| Feature | Status | Notes |
|---|---|---|
| Two firmware targets on fm1-base | SIM-TESTED | `breaksurgeon.fwsc`, `abyssengine.fwsc` build; all fm1-base static checks pass |
| Host simulator of each whole app | SIM-TESTED | `tests/host/*.txt` |
| Audio at 44,117.6 Hz, 128-frame blocks | SIM-TESTED | |
| Storage: versioned, CRC-checked records, defaults on bad data | SIM-TESTED | reboot persistence in the host scripts |
| Save refused while playing (flash stalls audio) | SIM-TESTED | |
| CPU load on the device | **BLOCKED** (hardware) | lower bounds from fm1-emu: docs/development/profiling.md |
| Second core | DEFERRED | single core by design for now |
| Bluetooth MIDI | DEFERRED | not in the fm1.toml files |

## Break Surgeon

| Feature | Status | Notes |
|---|---|---|
| Sliced break playback, two-voice crossfade, anti-click fades | SIM-TESTED | `test_break_*` |
| 8/16/32 even slices; bank slice tables; transient-snapped slices | SIM-TESTED | slicing offline in `tools/sample-prep` |
| Reverse, gate, pitch, tempo-sync (repitch), microloop, stutter | SIM-TESTED | |
| Procedural test break (no samples needed) | SIM-TESTED | |
| JOB1 bank format + parser with CRC/bounds rejection | SIM-TESTED | C and Python parsers agree |
| SysEx bank loader on the device | SIM-TESTED | transfer from a computer to a real FM-1: untested |
| Sample prep tool (WAV/AIFF, resample, normalize, onsets, classes) | SIM-TESTED | classes are heuristic |
| Six drum models (kick, snare, ghost, hats with choke, perc ×4, impact) | SIM-TESTED | bounded/decay tests; sound not judged by ear yet |
| Step sequencer: 7 tracks × up to 32 steps, per-track length/speed | SIM-TESTED | 64-step patterns via chains: DEFERRED (store object limit) |
| Velocity, accent, probability (seeded), ratchet, microtiming, swing | SIM-TESTED | |
| Parameter locks (2 per step) | SIM-TESTED | |
| Mute / solo | SIM-TESTED | |
| Quantized pattern and scene switching (beat/bar/2/4 bars) | SIM-TESTED | |
| FILL (1/2/4 bars) with instant return | SIM-TESTED | scenario A |
| Ghost generator | SIM-TESTED | |
| Mutation with anchors + one-level undo | SIM-TESTED | scenario B |
| BREAK-LOCK layer / separate | SIM-TESTED | |
| Performance modes CLEAN, ROLL, CHOP, REVERSE, GLITCH, DESTROY, FREEZE | SIM-TESTED | DESTROY's extra crush is the rhythm side only |
| RETURN to the stable groove at the next bar | SIM-TESTED | |
| Rhythm sweetspot graph (Clean ↔ Rolling ↔ Chopped ↔ Glitched) | PROTOTYPE | nodes UNREVIEWED |
| Kit sweetspot graph (5 nodes) | PROTOTYPE | nodes UNREVIEWED |
| DRUM PRESSURE bus + 5 presets | SIM-TESTED | ceiling −0.5 dBFS held in tests |
| Undo/redo beyond one level, A/B pattern snapshots | DEFERRED | |
| Pattern chains / song mode | DEFERRED | |
| Live resampling of external audio | **BLOCKED** | the FM-1 has no audio input |
| Internal bounce (resampling the FM-1's own output) | DEFERRED | RAM permits ~1.4 s at 22 kHz |
| Pattern capture (a performance into a pattern) | DEFERRED | |
| Two sample voices layered (two breaks at once) | DEFERRED | the player has two voices for crossfades only |

## Abyss Engine

| Feature | Status | Notes |
|---|---|---|
| FM CREATURE (2-op FM, feedback, formants, drive, synced LFO, Carson-limited index) | SIM-TESTED | |
| REESE MATRIX | SIM-TESTED | reachable through EXPLORE (crossfaded model switch) |
| PHASE MUTANT, COMB ORGANISM | DEFERRED | |
| Clean sub (mono, glide, anti-click, folded to 36–72 Hz) | SIM-TESTED | |
| Stabs: RAVE, ORCH, GLASS, DARK, chord memory, inversions, tension | SIM-TESTED | 4 voices |
| Pad: 4 voices, slow envelopes, LFO evolution, tension; tails survive scene changes | SIM-TESTED | |
| Harmonic sequencer: bass (slide, accent, MORPH/BITE/octave locks), stab, pad tracks | SIM-TESTED | |
| Scales: minor, harmonic minor, Phrygian, Dorian, chromatic | SIM-TESTED | user pitch sets: DEFERRED |
| Sweetspot graph: MORPH, SNAP, EXPLORE, MUTATE radius, UNDO history | SIM-TESTED | 10 nodes, UNREVIEWED |
| Loudness trims of the path nodes | PROTOTYPE | RMS-calibrated to −13 dBFS (a proxy); by ear: pending |
| NEURO PRESSURE (clean sub path, high-passed dirty bass, width only above 140 Hz) | SIM-TESTED | side < 120 Hz measured ≤ −57 dB re mid |
| Delay (ping-pong, synced) and small FDN reverb send | SIM-TESTED | |
| Kick-linked ducking | SIM-TESTED | scenario D |
| Standalone tempo ducking | IMPLEMENTED UNTESTED | |
| Voice budget modes (FULL / NO PAD / BASS ONLY) | IMPLEMENTED UNTESTED | manual; no automatic shedding yet |
| Keyboard play of each part, velocity | IMPLEMENTED UNTESTED | velocity needs APP_TOUCH on a device |

## Link and live

| Feature | Status | Notes |
|---|---|---|
| JUNGLE LINK v1 encode/decode, PREPARE/COMMIT, heartbeat, rate limiting | SIM-TESTED | `test_link_protocol` |
| Clock out (Break Surgeon) / follow (Abyss) with fall-back | SIM-TESTED | in the host simulator with simulated stamps |
| Kick triggers → Abyss ducking | SIM-TESTED | |
| Scene commands across devices | SIM-TESTED | engine to engine on the host |
| Physical link between two FM-1s | **BLOCKED** (hardware) | test G is PENDING; candidates in docs/research/hardware.md |
| Six scenes INTRO/BUILD/DROP/MUTATION/VOID/IMPACT on both devices | SIM-TESTED | values are starting points |
| Sweetspot certification (listening + measurements) | **BLOCKED** (needs ears and monitors) | checklist in docs/development/sweetspots.md |
