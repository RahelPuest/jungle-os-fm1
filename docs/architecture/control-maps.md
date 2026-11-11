# Control-map design

The FM-1 has 14 named buttons, 7 detented encoders without push, 16 white + 11 black keys with LEDs, MASTER, and a
240×240 screen (docs/research/hardware.md). Two control maps were considered; B is implemented.

## Variant A: held-button layers (TONIC / Hortator style)

Every button is a layer while held (hold FX: the knobs become effects, the keys become performance FX; hold SEQ:
step tools...), a tap opens its page.
- + very deep, one hand on a button, the other on knobs/keys
- − two-handed for most live actions; a forgotten hold changes what every key does; at 174 BPM a held layer
  competes with holding performance-mode keys (also holds): two holds at once for core gestures

## Variant B: pages by tap, one key = one action on PERFORM (implemented)

A tap on a button selects a page (HOME, SEQ, EDIT, FX, ARP, GLO); each page gives every key and knob one meaning;
SEL and SAVE are the only modifiers. The PERFORM page puts every essential live action under a single key or knob:
performance modes on held white keys, mutes, fills, scenes and undo on single presses, the four graph/macro knobs.
- + no essential live action needs more than one layer (PERFORM) and at most one modifier (SEL+PLAY panic, SEL
  for solo, SAVE+key to store — two hands on purpose, never by accident)
- + destructive actions are two-handed or reversible: storing a scene needs SAVE held; mutation has UNDO; RETURN
  and SNAP are always one press away; SAVE is refused while playing
- − editing depth lives on other pages (SEQUENCE, SOUND), one tap away

**0.2: one map for both, and the map on the screen.** Testers found both firmwares cryptic: the two used the same
buttons for different things (REC was RETURN on one and PAD HOLD on the other, ENV a fill or the keyboard's part),
and nothing on the screen said what a key did. Since 0.2:
- the buttons mean the same on both (PLAY, REC = RETURN, ENV = FILL, LFO = MUTATE with SEL + LFO = UNDO, SEL +
  white = scene, SAVE + white = store a scene, SEL + black = mute, SEL + PLAY = panic), and on PERFORM ALGO steps
  through the sweetspots and PRESET snaps onto one on both;
- a strip above the knob boxes (shared/ui/jo_ui.c `ui_strip`) shows what the three encoders and every run of keys
  do on the page, and switches to the SEL or SAVE layer while that button is held; with SEL held the page lists the
  SEL button combinations;
- the header shows a countdown bar while a change waits for its bar.

Evaluated against the hardware: the black keys (11) carry the discrete live choices (scenes, fills, undo) where
fingers rest naturally between white-key holds; LEDs show state on the keys themselves (mutes lit = playing, the
current scene lit, the queued one dim), so the screen is not needed mid-gesture.

## A 174 BPM rehearsal (in the simulator)

`tests/host/bs_smoke.txt` and `ab_smoke.txt` play through the main gestures (fill, held mode, RETURN, step edit
with a held key, scene queue, save refused while playing, save, reboot). A hands-on rehearsal on hardware without
a computer is **pending** (testing.md).

## 0.5: a sequencer first, with live tools on held layers (implemented)

Testers still found both firmwares cryptic in 0.4: keys and encoders meant different things on every page, many
gestures were two-button combinations (SEL + white, SAVE + white, SEL + black, SEL + SEQ, SEL + LFO), the black keys
of Break Surgeon's SEQUENCE page were a grab-bag, the knobs were repurposed (CALL & RESPONSE took LENGTH and SWING),
the home page showed abstract sound spaces (the MORPH path, KIT NODE) instead of the pattern, and REC did not
record. 0.5 turns both into a step sequencer whose grid is always there, with the live tools on held layers (variant
A's strength), each one button. The rule: **a tap opens a page; a hold is a layer while held; letting go is always
safe.**

### The same on both firmwares

**Always, on every page except SOUND's audition and while REC records:**

| Control | Does |
|---|---|
| white keys 1–16 | the 16 steps of the selected track (OCT− / OCT+: steps 1–16 / 17–32): tap on/off, hold + knob or encoder = edit that step |
| black keys | select the track (Break Surgeon: BREAK KICK SNARE GHOST HATS PERC IMPACT = black 1–7; Abyss: BASS STAB PAD = black 1–3) |
| SELECT | the track |
| ALGO | the library: load the next / previous starting point into the selected track or pattern (Break Surgeon: GROOVES, the whole pattern; Abyss: BASS LINES for BASS, chord rhythms for STAB / PAD) |
| PRESET | the pattern P1–P4 (at the next bar, or the phrase's end with QUANT = PHRASE) |
| K1–K4 | the selected track's four sound controls (named in the boxes) |
| step held + K1–K4 | that step's VELOCITY, PROBABILITY, RATCHET, MICRO (Abyss: NOTE, VELOCITY, a CHARACTER lock, a BITE lock) |
| step held + ALGO | that step's TRIG CONDITION (ALWAYS, 1:2, 2:2, 1:4 … 4:4, FILL, NO FILL, PHRASE 1ST, PHRASE END) |
| step held + PRESET | Break Surgeon: the BREAK step's slice / a drum step's tune lock; Abyss: the step's octave |

A held step shows a popup with all its values. The header shows the pattern, the bar in the phrase (BAR 3/4,
4/8 or 16-bar phrases) and the countdown to anything queued.

**Buttons: tap = page, hold = layer.**

| Button | Tap | Hold (a layer while held: the strip and a banner name it; keys and knobs change, letting go returns) |
|---|---|---|
| HOME | the main page: the selected track big, every track as a mini row | — |
| SEQ | the overview: every track's 16 steps, mutes, lengths | — |
| EDIT | SOUND: the track's sound in depth; the keys audition it | — |
| FX | the FX / bus page | **FX layer**: white keys = performance FX while held (Break Surgeon: ROLL, ROLL 32, CHOP, REVERSE, GLITCH, DESTROY, FREEZE, TAPE STOP; Abyss: OCT UP, OCT DOWN, FILTER DIVE, WOBBLE, STUTTER, TAPE STOP, BITE MAX, SUB ONLY); K1–K4 = the bus macros |
| ARP | SONG: scenes, FORM, phrase, quantize | **SONG layer**: white 1–8 = queue scene 1–8, white 9–12 = queue pattern P1–P4, black = mute / unmute track |
| GLO | SYSTEM | — |
| ENV | a fill at the end of the phrase (the phrase's last bar) | **FILL layer**: white 1–4 = play that fill now, from the next bar (Break Surgeon: ROLL, SNARE BUILD, DROP OUT, CHOP; Abyss: ROLLS, OCTAVES, DROP OUT, SLIDE DOWN); K1 = length (1, 2 bars) |
| LFO | MUTATE the selected track (Abyss BASS: the sound) | **VARY layer**: white 1–4 = play variation A–D from the next bar; white 9–14 = the FORM (OFF, A B A C, A A A B, A B A B, A B C D, A A B B); K1 = DENSITY (the track denser / emptier), K2 = HUMANIZE, K3 = MUTATE amount, K4 (Abyss) = the held variation's SOUND |
| REC | live RECORD on / off: the keys play the selected track and write it into the steps, quantized to the 16th (Break Surgeon: the white keys are soft → hard hits, on BREAK the 16 slices; Abyss: the keys are notes, OCT− / OCT+ the octave) | **ERASE layer**: white = clear that step; black = clear that track |
| SAVE | save everything (refused while playing; also automatic 3 s after STOP when something changed) | **STORE layer**: white 1–8 = store the current state as scene 1–8 |
| SEL | — | **SEL layer**: SEL + LFO = UNDO, SEL + ENV = REDO (8 levels, every edit, groove, line, mutation, record pass, erase); SEL + PLAY = PANIC; SEL + knob = fine; SEL + SELECT = tempo |
| PLAY | start / stop | |
| OCT− / OCT+ | steps 1–16 / 17–32 (REC on Abyss: the octave) | |

**Variations and FORM.** Each firmware has four variations A–D and a FORM that plays them bar by bar on the 4-bar
(or 8-bar) grid; FORM OFF plays what is selected.
- Break Surgeon: the variations are the patterns P1–P4 (the whole kit).
- Abyss: the variations are the bass's answers: A is the bass as it is; B, C, D each have a LINE (A's or P1–P4's bass
  line) and a SOUND (A's or a sweetspot). CALL & RESPONSE of 0.4 is FORM A B A C.

**Smart help.**
- Trig conditions on every step (above), evaluated on the global bar grid so both FM-1s agree.
- The phrase (SONG: 4, 8, 16 bars) counts in the header; ENV's tap fills its last bar; QUANT = PHRASE lands scenes
  and patterns on its end.
- DENSITY adds or removes steps of the selected track by a fixed ranking (offbeats and ghosts go first, the
  downbeat last), HUMANIZE sets small velocity and microtiming offsets; both are undoable.
- Abyss over JUNGLE LINK: **LOCK TO KICK** (SONG page): the bass plays on Break Surgeon's kicks, its line giving the
  notes; **FILL ANSWERS** (SONG page): a fill from Break Surgeon makes that bar play variation C (the answer).
  Scenes already follow each other.
- Undo / redo for everything that changes a pattern or the sound (shared/seq/jo_undo.h).
- Saving happens by itself 3 s after STOP when something changed.

**What goes.** PERFORM as a page (its macros become the tracks' knobs or the FX page); SEL + white / SAVE + white /
SEL + black (now the SONG and STORE layers); SEL + SEQ (now VARY's FORM); REC = RETURN (performance FX end on
release, fills end themselves, everything else has undo; RETURN stays as SEL + REC: back to the stored scene's
state, quantized).
