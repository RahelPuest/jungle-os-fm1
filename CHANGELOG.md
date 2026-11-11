# Changelog

## 0.7.2 (2026-11-11): the foghorn, FM

Not installed on a physical FM-1; not checked by ear.

- **FOGHORN rebuilt again, as the tutorials make it:** an FM sound. Three stacked squares an octave below the note,
  each FMed by a sine three octaves up (RATIO; INDEX the depth, FDBK the modulator's self-feedback), a 24 dB
  low-pass from 90 Hz opened by a 100 ms / 1.7 s AD, hard drive, and the bass in the reverb (above ~250 Hz, the
  SPACE macro). 0.7.1's version was not a foghorn.

## 0.7.1 (2026-11-11): a Reese, a hoover and a foghorn that sound like them

Not installed on a physical FM-1; not checked by ear.

Abyss Engine
- **REESE rebuilt:** two free-running saws against each other (3–30 cents), the detune drifting slowly, a saw an
  octave below; the REESE and REESE DARK sounds lose the quarter-note filter wobble (the LFO now moves over a bar).
  0.7's four saws sounded like a chorus.
- **HOOVER, a model of its own:** PWM saws on three octaves, the pulse width stirred by a fast LFO, a deep chorus,
  a pitch envelope that dives from 5 semitones sharp onto the note (0.7 bent up from below, without chorus).
- **FOGHORN rebuilt:** an octave below the note, three unison saw-squares with FM, a 24 dB low-pass that opens on
  every note and closes over 1.5 s, two hard drive stages (after Attack Magazine's foghorn).
- Trims re-measured (every sound -13 dBFS RMS on a held D2). The width check allows -25 dB of trace in the mono
  sum (the master's cubic soft clip and the brighter Reese; it was -30 dB).
- Measured on the host, per block: FOGHORN 9.8 us (before 10.4), HOOVER 6.6 (7.7), REESE 4.7 (6.6); GROWL stays
  the dearest at 10.7. Not measured in fm1-emu.

## 0.7.0 (2026-11-11): the bass styles, a new kit, the classic breaks

Not installed on a physical FM-1; the new sounds are not checked by ear.

Abyss Engine
- **A new bass voice, five models:** FM NEURO; a real REESE (four saws 4–40 cents apart, each drifting at its own slow
  rate, blended into PWM pulses for the hoover); WOMP (the filter and the vowel open and close on every note, under the
  synced LFO); HORN (detuned squares and an octave, swelling open); SINE (a driven sine; its pitch drop is the sub's
  too: the 808). A phaser and a pitch envelope on every model. Mid-bass width above ~250 Hz into the side only (FX
  WIDTH; the mono sum and the sub are unchanged).
- **Sixteen sounds** for the drum & bass archetypes: DUB, ROLLER, REESE, REESE DARK, HOOVER, GROWL, TALK, SNARL (the
  MORPH path), WOMP, WUB, FOGHORN, 808, LASER, METALLIC, TECH STAB, BROKEN; trims measured to -13 dBFS on a held D2.
  The default sound is REESE; B's default answer REESE DARK.
- **STYLES on ALGO** (replacing the line library): a sound, a line (16 or 32 steps, accents, slides, octave jumps,
  ENERGY levels), a FORM and its answers' sounds, the way each archetype is played: ROLLING REESE, DARK TECHSTEP,
  HOOVER, NEURO SWITCH, TALKER, JUMP-UP WOMP, WOBBLE, FOGHORN, LIQUID ROLLER, 808 HALF-TIME, JUNGLE DUB, LASER ZAPS,
  RUFIGE STAB; a box on the screen says how it is played; undoable.
- **The SOUND page browses every sound by name** (ALGO on BASS).
- Pattern P1's MORPH locks are gone (the knob's sound plays throughout).
- docs/research/dnb-bass-styles.md: the archetypes, how each is made and played, with sources.
- Saved settings from 0.6 load as defaults once (the sounds are new).
- Tests: every sound (finite, no DC, bounded, top-octave energy, trim within 1.5 dB), the width's mono safety, the
  styles (in the scale, their sounds and answers); a render of every style (`G_bass_styles`).

Break Surgeon
- **A rebuilt kit:** a kick with a held 56 Hz body, a two-stage pitch drop, asymmetric saturation and a soft 0.25 ms
  onset; a snare with a clap layer and an air crack over its body and tail; hats from two metal bands and noise,
  longer and louder.
- **The bus** gets a console EQ before its saturation; under the synthesized kick the break loses its lows (below
  ~70 Hz) and ducks up to 3.5 dB.
- **Six built-in breaks**, synthesized from their rhythms (no recordings): AMEN, THINK, APACHE, FUNKY DRUMMER, HOT
  PANTS and an original ROLLER; ALGO on the BREAK track's SOUND page picks one (saved). THINK and HOT PANTS are
  approximate (no written grid found).
- **Grooves:** 18 now, with LIQUID, NEURO and two jungle edits that re-chop the break (JUNGLE EDIT, SNARE ROLL
  EDIT); P2 ROLLER uses the classic re-chop.
- docs/research/dnb-drums.md: how DnB drums are made to sound good, the classic breaks and patterns, with sources.
- Tests: the drum models (bounded, no DC, the kick's pitch and length, the hats' lengths), the breaks' classes and
  playback, the new chops; a render of every break (`breaks`).

Both firmwares' renders change on purpose (the new sounds). Tests: 620 checks.

## 0.6.0 (2026-11-11): routes, DNA, duet, energy, tape decay

Still not installed on a physical FM-1. The sound at the defaults is unchanged (every render keeps its hash).

Both firmwares
- **ENERGY (hold HOME):** every step has a level 1–4; ENERGY decides which levels play, so a build-up is one knob
  over 16 bars. The white keys cycle the levels, K2 records ENERGY's curve over a phrase and plays it back, K3 sets
  the levels from each hit's role. Grooves and library lines load with levels. The two FM-1s follow each other's
  ENERGY over JUNGLE LINK (CC 109). Below full it also thins Break Surgeon's room and grit and Abyss's bite and pad.
- **TAPE DECAY (SONG page):** what REC records fades a little every pass and goes after a few; a step held for
  half a second is burned in.

Break Surgeon
- **ROUTES (hold SEQ):** the playhead's path through the steps: FORWARD, REVERSE, PENDULUM, SNAKE, SPIRAL, SWAP,
  STUTTER, WALK on black 1–8, or a CUSTOM route drawn by pressing the steps in order. On the selected track, the
  break or all tracks; while held, or latched into the pattern (undoable). The bar is kept, the steps unchanged.
- **DNA (tap SEQ twice):** a family tree of up to 16 grooves. LFO breeds a child, the white keys play a genome
  from the next bar, black 1 keeps, 2 kills a branch, 3 crosses two (A's drums, B's chops), 4 starts a new tree;
  SAVE + white 9–12 stores the playing one as P1–P4.
- Sends DENSITY (CC 112) once a bar. BREAK-LOCK moved to SYSTEM's ALGO.

Abyss Engine
- **DUET (hold SEQ):**
  - ANSWER ME: you play a bar, the machine answers it the next bar, in variation C's sound: ECHO, INVERT, RETRO,
    DISPLACE, OCT DOWN or SPARSE.
  - POCKET: the bass moves off Break Surgeon's kicks into the gaps, thinner when the drums are busy (DENSITY).
  - GESTURE ECHO: your FX and knob moves come back the next phrase at half depth.

Saved patterns (and Abyss's settings) from 0.5 load as defaults once; scenes are kept. The DNA tree and the energy
curve are not saved. Tests: 541 checks.

## 0.5.0 (2026-11-11): sequencers first, live tools on held buttons

Still not installed on a physical FM-1. The sound is unchanged (every render keeps its hash).

Both firmwares (docs/architecture/control-maps.md, "0.5")
- **The grid is always there:** the white keys are the selected track's 16 steps on every page (OCT−/OCT+: 17–32),
  the black keys pick the track, SELECT the track, ALGO the library, PRESET the pattern, K1–K4 the track's sound.
  A held step shows a popup; with it held the knobs set its values, ALGO its trig condition, PRESET its slice,
  tune or octave.
- **HOME** shows the selected track big and every track as a mini row; the header shows the pattern (or the
  variation), the bar in the phrase and the countdown.
- **A tap opens a page, a hold is a layer:**
  - FX: performance FX on the white keys, the bus on the knobs.
  - ARP (SONG): scenes, patterns, mutes.
  - ENV (FILL): four fill types; a tap fills the phrase's last bar.
  - LFO (VARY): variations A–D, FORM (OFF, A B A C, A A A B, A B A B, A B C D, A A B B), DENSITY, HUMANIZE, MUTATE;
    a tap mutates.
  - REC: a tap records the keys into the track, quantized; held, it erases steps or tracks.
  - SAVE: a tap saves; held, it stores scenes.
  - SEL: + LFO undo, + ENV redo (8 levels, shared/seq/jo_undo.h), + PLAY panic, + REC RETURN, + SELECT tempo.
- **Trig conditions** on every step: 1:2, 2:2, 1:4 … 4:4, FILL, NO FILL, PHRASE 1ST, PHRASE END, on the global
  bar grid so both FM-1s agree.
- **Phrases** of 4, 8 or 16 bars (SONG page); QUANT = PHRASE lands scenes, patterns and variations on the phrase's
  end.
- **Autosave** 3 s after STOP when something changed.
- Saved settings and patterns from 0.4 load as defaults once (new formats); scenes are kept.

Break Surgeon
- The variations are P1–P4; FILL types ROLL, SNARE BUILD, DROP OUT, CHOP; a new performance FX, TAPE STOP; DENSITY,
  HUMANIZE, track mutation; record on drums (soft to hard across the keys) and on the break (the keys are slices).
- The link's fill message now goes out 4 steps before the fill, so Abyss's fill lines up; a scene queued with
  QUANT = PHRASE goes over the link as 4 BAR.

Abyss Engine
- The variations are the bass's answers: A as it is, B, C, D each a LINE and a SOUND (0.4's CALL & RESPONSE is
  FORM A B A C).
- A library on ALGO: 8 bass lines in the scale (ROLLING, OFFBEAT, NEURO STABS, HALF-TIME, REESE LONG, TWO-STEP,
  ACID, WOBBLE 8THS) and 5 chord rhythms each for STAB and PAD.
- Performance FX: OCT UP, OCT DOWN, FILTER DIVE, WOBBLE, STUTTER, TAPE STOP, BITE MAX, SUB ONLY.
- FILL types ROLLS, OCTAVES, DROP OUT, SLIDE DOWN.
- Over JUNGLE LINK: LOCK TO KICK (the bass on Break Surgeon's kicks, the notes from its line) and FILL ANSWERS
  (a fill from Break Surgeon plays answer C).
- Undo also covers the sound's mutation. HUMANIZE is a playback amount (turn it back to undo it).

Tests: 274 checks (153 in 0.4): trig conditions, the undo stack, and two new suites for the live tools of each
firmware; both host tours go through every page and every layer with no text cut.

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
