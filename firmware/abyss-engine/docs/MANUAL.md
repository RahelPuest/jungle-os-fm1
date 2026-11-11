# The ABYSS ENGINE manual

ABYSS ENGINE is a bass sequencer: a neuro bass (FM CREATURE, REESE MATRIX) through a curated graph of sweetspot
sounds, a separate clean sub, stabs and an evolving pad, on three tracks of steps in a shared key. The live tools
(performance FX, fills, variations, scenes) sit on buttons you hold. Status: it runs in the simulators and the browser
emulator; **it has not been tested on an FM-1 yet**.

## The one rule

**Tap a button: a page. Hold a button: a layer, while you hold it. Let go: back, always safe.**

On every page the 16 white keys are the 16 steps of the selected track, and black keys 1–3 pick the track: BASS,
STAB, PAD. The screen's strip shows, at all times, what the encoders and every run of keys do, and names the held
layer.

## Quick start

1. **PLAY.** Pattern 1 plays: a bass line in D minor at 174 BPM.
2. **Tap a white key:** a step on or off. **Hold it and turn K1:** its note.
3. **ALGO:** the library: a new bass line (ROLLING, OFFBEAT, NEURO STABS, HALF-TIME, REESE LONG, TWO-STEP, ACID,
   WOBBLE 8THS), in the key. On STAB and PAD, chord rhythms. SEL + LFO undoes it.
4. **K1 CHAR** moves the bass through its sounds (HOLLOW to BROKEN). K2 MOTION, K3 BITE, K4 GLIDE.
5. **Hold FX and a white key:** a performance FX while you hold (OCT UP, FILTER DIVE, TAPE STOP …).
6. **Tap ENV:** a fill in the last bar of the phrase. **Hold ENV + white 1–4:** a fill from the next bar.
7. **Hold LFO + white 9 to 14:** a FORM: the bass answers itself, A B A C.
8. **Tap REC** and play the keys: they are recorded into the track. Tap REC again to stop.

The same buttons do the same on BREAK SURGEON.

## The screen

![HOME: the BASS track's 16 steps big, with their notes; all three tracks as mini rows below](img/perform.png "HOME")

- **The header:** the page (or the held layer, or REC in red), the tempo (LINK: following Break Surgeon), the
  pattern and the variation playing (P1 A), and the bar of the phrase (BAR 2/4). A bar along its bottom fills up
  while a pattern or a scene waits to land.
- **The page.** HOME shows the selected track's 16 steps big: a lit box is a step, its bar the velocity; on BASS
  the note is written in it. Marks: a red corner = a trig condition, a blue one = a lock, a grey dot = a probability,
  a white line = slide. Below, every track as a mini row.
- **The strip:** what SELECT, ALGO and PRESET do, and what each run of keys does.
- **The four knob boxes.**

![A held step: its popup with the note, velocity, condition and octave](img/step.png "A held step")

## Steps and tracks (every page)

| Control | Does |
|---|---|
| white key, tap | the step on / off |
| white key, hold | edit the step: a popup shows its values |
| hold + K1 / K2 / K3 / K4 | NOTE (BASS: scale steps; STAB, PAD: the chord degree) / VELOCITY / a CHARACTER lock / a BITE lock |
| hold + ALGO | the TRIG CONDITION: ALWAYS, 1:2, 2:2, 1:4, 2:4, 3:4, 4:4, FILL, NO FILL, PHRASE 1ST, PHRASE END |
| hold + PRESET | the step's octave (−2 … +2) |
| hold + black 4 / 5 | ACCENT / SLIDE (legato into the note) |
| black 1–3, SELECT | the track: BASS, STAB, PAD |
| ALGO | the library (above) |
| PRESET | the pattern P1–P4, at the next bar (or the phrase's end, QUANT = PHRASE) |
| OCT− / OCT+ | steps 1–16 / 17–32 of a longer track |

Trig conditions count bars on the global grid, so a 1:4 step lands in the same bar on both FM-1s. FILL steps play
only under a fill: put a few extra notes there and every fill brings them.

## The buttons

| Button | Tap | Hold: the layer |
|---|---|---|
| HOME | HOME (the steps) | — |
| SEQ | SEQUENCE: the three tracks, lengths, mutes | — |
| EDIT | SOUND: the track's sound in depth; the keys play it | — |
| FX | FX: NEURO PRESSURE | **FX:** white 1–8 = OCT UP, OCT DOWN, FILTER DIVE, WOBBLE, STUTTER, TAPE STOP, BITE MAX, SUB ONLY, while held. K1–K4: WEIGHT, DIRT, WIDTH, PRESSURE |
| ARP | SONG: form, phrase, link helpers | **SONG:** white 1–8 = queue scene 1–8, white 9–12 = queue P1–P4, black 1–3 = mute / unmute BASS, STAB, PAD |
| GLO | SYSTEM | — |
| ENV | a fill in the phrase's last bar | **FILL:** white 1–4 = ROLLS, OCTAVES, DROP OUT, SLIDE DOWN from the next bar; K1 = 1 or 2 bars |
| LFO | MUTATE the bass sound | **VARY:** white 1–4 = variation A–D from the next bar; white 9–14 = FORM OFF, A B A C, A A A B, A B A B, A B C D, A A B B; ALGO = the variation's LINE; K1 DENSITY, K2 HUMANIZE, K3 MUTATE amount, K4 the variation's SOUND |
| REC | RECORD on / off | **ERASE:** white = clear the step, black 1–3 = clear the track |
| SAVE | save | **STORE:** white 1–8 = store the state as scene 1–8 |
| SEL | — | **SEL:** SEL + LFO = UNDO, SEL + ENV = REDO, SEL + PLAY = PANIC, SEL + REC = RETURN (SNAP, back to the scene), SEL + SELECT = tempo, SEL + knob = fine |
| PLAY | start / stop | |

A hold is a hold after you use it, or after 1.5 s; a short press without anything else is a tap.

![SEL held: its combinations](img/sel.png "SEL held")

### FX: performance FX

![FX held, FILTER DIVE on white 3](img/layer_fx.png "FX held")

Each FX plays while its key is down and ends when you let go. OCT UP / OCT DOWN move the bass an octave. FILTER DIVE
closes the bass's low-pass five octaves over a bar. WOBBLE turns the bass's LFO to full at 1/8. STUTTER repeats the
note under it in 32nds. TAPE STOP slows the bass and sub two octaves down to silence over 0.6 s. BITE MAX is BITE
at full. SUB ONLY mutes the mid bass and leaves the sub.

### SONG and STORE: scenes, patterns, mutes

![SONG held: the scenes, the patterns, the mutes](img/layer_song.png "SONG held")

A scene holds the pattern, the mutes, PAD HOLD, the bass's CHARACTER / MOTION / BITE and SPACE, NEURO PRESSURE, the
root and the scale. Hold SAVE and press white 1–8 to store one.

### FILL

![FILL held: the four fills and the length](img/layer_fill.png "FILL held")

ROLLS: the bass rolls in the bar's second half, with octave jumps. OCTAVES: every second 8th an octave up. DROP OUT:
the bass and sub silent. SLIDE DOWN: every note slides down the scale. A tap of ENV plays the last chosen fill in the
phrase's last bar (or bars, at 2 bars).

### VARY: variations and the FORM

![VARY held: A–D with their lines and sounds, the FORM list](img/layer_vary.png "VARY held")

The bass has four variations. **A** is the bass as it is: the pattern's line and the knobs' sound. **B, C, D**
answer it, each with a **LINE** (A's, or the bass line of P1–P4) and a **SOUND** (A's, or one of the ten sweetspots,
REESE and REESE DARK included). Defaults: B = A's line as REESE, C = P2's line as LASER, D = P3's line as GROWL.

The **FORM** plays them bar by bar on the 4-bar grid: A B A C is the call and two answers (0.4's CALL &
RESPONSE), A A A B a switch-up in the fourth bar, A B A B, A B C D, A A B B. FORM OFF plays the variation you picked
last (white 1–4). Each part takes its sound from its first note on; the sub plays every part's notes.

DENSITY makes the selected track denser or emptier one step a click (the offbeat 16ths come and go first, the
downbeat last). HUMANIZE adds small fixed velocity and timing offsets as it plays. Both are undone like any edit.

### REC and ERASE

![REC: the keys play and write the bass](img/rec.png "REC")

With REC on, the keys play the selected track and write each note into its nearest step: BASS the note, STAB and PAD
the nearest chord degree, with the velocity. OCT− / OCT+ then move the keyboard. A whole record pass is one UNDO.
Hold REC to erase steps or a track.

## The pages

### SEQUENCE (SEQ)

![SEQUENCE: the three tracks, their lengths and speeds](img/seq.png "SEQUENCE")

All three tracks with their LEN and SPEED, MUTED in red. K1 LENGTH (1–32), K2 SWING, K3 SPEED (1/16, 1/32, 1/8),
K4 SPACE (the delay / reverb send of stabs and pad).

### SOUND (EDIT)

![SOUND on BASS: the patch as the graph sets it now](img/sound.png "SOUND")

The keys play the track here without recording. ALGO: the chord type (BASS, PAD) or the stab model (STAB). PRESET:
the root; SEL + PRESET: the scale.

| Track | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| BASS | MOD SHAPE | SUB SAT | SUB OCT (−1 / 0) | TENSION |
| STAB | SPACE | TENSION | INVERSION | — |
| PAD | SPACE | TENSION | ATTACK | RELEASE |

On HOME the knobs are each track's four main controls: BASS CHAR (the sweetspot path), MOTION, BITE, GLIDE; STAB
CHORD, ATTACK, BITE, TAIL; PAD DARKNESS, EVOLVE, TENSION, DEPTH.

### FX (NEURO PRESSURE)

![FX: NEURO PRESSURE](img/fx.png "FX: NEURO PRESSURE")

K1 WEIGHT (the sub's level), K2 DIRT (the drive above 115 Hz), K3 WIDTH (pads and stabs above 140 Hz, never the
sub), K4 PRESSURE (glue and density with a compensating trim). PRESET: the output trim.

### SONG (ARP)

![SONG: the FORM, the variations, the phrase and the link helpers](img/song.png "SONG")

ALGO: the FORM. K1 PHRASE (4, 8, 16 bars: the header's BAR count, the fills' and PHRASE END's grid). K2 QUANT: when
scenes land (NOW, BEAT, BAR, 2 BAR, 4 BAR, PHRASE; PHRASE also lands patterns and variations at the phrase's end).
K3 **LOCK TO KICK**: with Break Surgeon linked, the bass plays on its kicks, the line giving the notes. K4 **FILL
ANSWERS**: a fill from Break Surgeon makes its bars play variation C.

### SYSTEM (GLO)

![SYSTEM: clock, link, store](img/system.png "SYSTEM")

K1 BPM, K2 VOICES (FULL, NO PAD, BASS ONLY, to drop parts under load), K3 LINK CH, K4 FOLLOW (LINK: the link's
clock, transport, scenes and fills). SELECT: DUCK (how deep Break Surgeon's kicks duck the sub); ALGO: SOLO DUCK
(duck on every beat when no link is heard); PRESET: the USB port as a host (unverified with another FM-1). The page
shows the load, the link, the store, the USB mode and how many sweetspots are still unreviewed: none is certified by
listening yet ([docs/development/sweetspots.md](https://github.com/RahelPuest/jungle-os-fm1/blob/main/docs/development/sweetspots.md)).

## Saving

SAVE saves the settings, the four patterns and the scenes (not while playing: a flash write stalls the sound). It
also happens by itself 3 s after STOP when something changed. 0.5 changed the stored formats: settings and patterns
saved by 0.4 load as the defaults once.
