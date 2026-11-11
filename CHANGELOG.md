# Changelog

## 0.4.0 (2026-11-11): call and response

Still not installed on a physical FM-1.

Abyss Engine
- **CALL & RESPONSE:** the bass plays a 4-bar form, A B A C, on the 4-bar grid. **SEL + SEQ** turns it on and off.
  - A is the bass as it is: the pattern's line and the knobs' sound.
  - B and C answer it, each with its own line (A's, or pattern P1–P4's bass line) and its own sound (A's, or one of
    the ten sweetspots). Defaults: B is A's line as REESE, C is P2's line as LASER.
  - The SEQUENCE page shows the form with the bar playing lit; K1–K4 set B LINE, B SOUND, C LINE, C SOUND.
  - The settings are saved. The saved settings' format changed: settings saved by 0.3 load as the defaults once
    (patterns and scenes are kept).
- Tests: an engine check of the form (A's notes, B's notes and sound, C's line and sound), a render of it
  (`F_call_response`), and the host tour through every B and C setting with no text cut.

Break Surgeon
- **GROOVES:** ALGO on the SEQUENCE page loads one of 14 classic drum & bass grooves into the pattern: 2-STEP,
  DELAYED 2-STEP, STEPPA, EXTENDED STEPPA (2 bars), STOMPA, HALF-TIME, DANCEHALL, FOOTWORK, TRIPLET 2-STEP, ROLLER,
  JUMP UP, TECHSTEP, DANCEFLOOR, AMEN.
  - A groove writes KICK, SNARE, GHOST, HATS and the swing, and re-chops the break to it by its slices' classes
    (kick slices on the kicks, snare slices on the snares, the ride in between).
  - SEL + LFO undoes it (the whole pattern back).
  - The common forms of the genre, written on the 16th grid; the names as STRANJAH's tutorials teach them.
- Tests: every groove's kicks and snares land on the break's kick and snare slices; undo; a render of all 14
  (`grooves`); the host tour through every groove's name.

## 0.3.1 (2026-11-10): an Amen to start with

Still not installed on a physical FM-1.

Break Surgeon
- **The built-in test break plays the Amen pattern:** kicks on 1, 1-and, 3-and and 3-a; full snares on 2 and 4,
  lighter ones in between; a ride on 8ths with its quarters on the bell. 170 BPM, 16 slices, the same classes as
  before on the slices the patterns use (kicks on 1 and 3-and, snares on 2 and 4).
- Synthesized from Break Surgeon's own kick, snare and hat models: the pattern only, nothing of the 1969 recording,
  which is copyrighted. Its name on the BREAK page: AMEN PATTERN 170.

## 0.3.0 (2026-11-10): the drum & bass look; text that fits

Still not installed on a physical FM-1. The sound is unchanged since 0.2.0.

The screens (both firmwares)
- **Text always fits its box.** Every text on the pages, in the header, the strip and the knob boxes is drawn by
  `ui_fit`: in its font if it fits, else in the next smaller one, cut only as a last resort.
  - The host simulator reports any cut on stderr, and `tools/build_all.sh` fails on it.
  - The simulator scripts now tour every page, track and part, plain and with SEL or SAVE held. No text is cut.
  - Break Surgeon's mute boxes are wider, so IMPACT fits.
- **A drum & bass look:** near-black, one loud hue per instrument.
  - BREAK SURGEON: acid lime values over hazard yellow. ABYSS ENGINE: abyss cyan over neuro violet.
  - Hazard stripes along the header's edge; they turn red on an alarm, and the countdown runs over them.
  - A slanted page tab.
  - The knob boxes have big values, a cut corner and a top edge that lights while turned.
  - A start screen with the name between hazard bars.

The site
- **The same look on every page** (`site/dnb.css`, linked by `tools/make_site.sh`): the front pages, the manuals,
  the installer and the emulator.
  - Black, with the instrument's colours, hazard tape under the bar, big condensed capitals with an offset print.
  - Screenshots get a hard offset shadow.

## 0.2.0 (2026-11-10): a drum & bass kit, and controls you can read off the screen

Still not installed on a physical FM-1.

Break Surgeon's sound
- **Kick:** 45–75 Hz (55 Hz by default; the sub is Abyss Engine's), shorter, with a tonal beater click (1.8–3.8 kHz)
  and a noise snap.
- **Snare:** a tuned body with a pitch drop; the stick's crack, the wires' snap and their tail, each with its own
  envelope.
- **Velocity shapes the timbre** of every model, not only the level: a hard hit is brighter, with more crack and
  click.
- **The ghost snare** is that snare played softly. The generated ghosts sit about 8 ms late.
- **Hats:** noise-led (shaker-like) unless METAL is up.
- **DRUM PRESSURE**
  - ROOM: a small FDN room fed by each track's send (snare and ghosts most, the kick almost none). It goes before
    the glue.
  - GRIT: CRUSH's lower half is now a serial sampler stage (12 down to 10 bits, a held rate, a darker top).
  - GLUE is a fast compressor (0.8–4 ms attack) whose makeup holds the level, so the kit gets denser instead of
    quieter.
  - PRESSURE drives the saturation harder.
  - Measured on the DROP groove, ROLLING BREAKS preset, 8 bars: about −16 dBFS RMS with a 10 ms crest of 11 dB.
    Before: −20 dBFS and 13 dB.
- **The built-in test break** is a new original two-step groove: ghosts soft and late, accented offbeat hats, an
  8th shaker, a small room, 12 bits at 22 kHz.
- **The patterns:** 174 BPM and straight; P2 ROLLER plays a break edit with a snare roll and a rolling kick.

Controls (both firmwares)
- **One map for both:**
  - REC = RETURN; ENV = FILL (SEL + ENV: 2 bars).
  - LFO = MUTATE, SEL + LFO = UNDO.
  - SEL + white 1–8 = queue a scene, SAVE + white 1–8 = store one, on every page; SEL + black = mute.
  - PERFORM: ALGO steps through the sweetspots, PRESET snaps.
  - Abyss Engine: SELECT picks the part the keys play (was ENV), PAD HOLD is SEL + REC (was REC), and there is a fill
    of its own.
- **The strip:** above the knob boxes, what the three encoders and every run of keys do on the page. It changes to
  the SEL or SAVE layer while that button is held. With SEL held, the page lists the SEL combinations.
- **The header** shows a countdown bar while a pattern, a scene, a fill or RETURN waits for its bar. Every action
  answers with a message (FILL 1 BAR / FROM THE NEXT BAR, MUTE KICK, P2 ROLLER / AT THE NEXT BAR …).
- **Names:** ENERGY is now FILL AMT, CRUSH is now GRIT, REVERT is now UNDO. Abyss's UNREVIEWED moved from PERFORM to
  SYSTEM.
- **Break Surgeon's encoders:** SOUND PRESET is the drum's level; FX PRESET is ROOM (SEL + PRESET: trim); SEQUENCE
  PRESET is the pattern.
- **The manuals** start with an eight-move quick start and describe the screen.

Compatibility
- Break Surgeon's settings and scenes have a new size (the room). The first start after the update uses their
  defaults; the patterns are kept.

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
