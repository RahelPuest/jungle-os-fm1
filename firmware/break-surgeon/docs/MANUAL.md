# The BREAK SURGEON manual

A sliced breakbeat player and six synthesized drum models on one sequencer, for 160–190 BPM jungle and drum &
bass. Status: runs in the simulators; **not yet tested on an FM-1** ([docs/research/compatibility-matrix.md](https://github.com/RahelPuest/jungle-os-fm1/blob/main/docs/research/compatibility-matrix.md)).

## The controls you have

14 buttons, 7 encoders (no push), 16 white and 11 black keys, the MASTER knob, the screen. Below, *white 1..16*
are the white keys left to right (F3 … G5), *black 1..11* the black keys left to right.

Every page has the same frame: the page name top left (or a message), the tempo, `>` playing / `#` stopped, the
pattern (`P1`, a `*` when a switch is queued), the scene and the audio load (red bar: overload or a dropout), and
the four K-knob boxes at the bottom.

## Buttons (every page)

| Button | Tap | With SEL held |
|---|---|---|
| HOME | PERFORM page | |
| SEQ | SEQUENCE page | |
| EDIT | SOUND page | |
| FX | FX page (DRUM PRESSURE) | |
| ARP | SCENE page | |
| GLO | SYSTEM page | |
| PLAY | start / stop | **PANIC**: stop, cut every voice, clear performance modes |
| REC | **RETURN**: back to the stable groove at the next bar (performance modes, fill and rhythm graph off) | |
| ENV | FILL from the next bar, 1 bar | 2-bar fill |
| LFO | MUTATE the break (amount: SEQUENCE page K4) | **UNDO** the last mutation |
| SAVE | save patterns, scenes, settings (refused while playing: a flash write stalls the audio) | |
| OCT− / OCT+ | SEQUENCE: steps 1–16 / 17–32. Elsewhere: previous / next pattern at the next bar | |
| SEL | a modifier (and fine steps for the K knobs while held) | |

Encoders on every page unless the page says otherwise: **SELECT** tempo (0.5 BPM a click), **ALGO** swing,
**PRESET** ENERGY.

## PERFORM (HOME)

![PERFORM while playing: scene DROP, rhythm CLEAN, kit JUNGLE SNAP, the mutes, BREAK KIT PRESSURE ENERGY](img/perform_playing.png "PERFORM while playing: scene DROP, rhythm CLEAN, kit JUNGLE SNAP, the mutes, BREAK KIT PRESSURE ENERGY")

The screen: the scene (and the queued one), the rhythm and kit nodes the BREAK and KIT knobs are nearest, the
mode, the seven mutes, the output meter, LINK OK / SOLO.

| Control | Does |
|---|---|
| K1 BREAK | the rhythm graph: CLEAN → ROLLING → CHOPPED → GLITCHED (slice substitution, ratchets, reverse, stutter, gate, pitch drops; anchors kept) |
| K2 KIT | the kit graph: DUSTY ROLLER → JUNGLE SNAP → TIGHT NEURO → METAL TECHSTEP → INDUSTRIAL (all 16 drum macros move together, loudness trimmed) |
| K3 PRESSURE | DRUM PRESSURE's joint drive / compression / ceiling (as on the FX page) |
| K4 ENERGY | fill intensity |
| white 1 (hold) | ROLL: the current slice repeated in 16ths |
| white 2 (hold) | ROLL in 32nds |
| white 3 (hold) | CHOP: a new slice every step (beats kept) |
| white 4 (hold) | REVERSE: every slice backwards |
| white 5 (hold) | GLITCH: chops, ratchets, stutters |
| white 6 (hold) | DESTROY: all of it harder |
| white 7 (hold) | FREEZE: a 4 ms loop of where the break is; release to let go |
| white 8 | FILL, 1 bar |
| white 9..15 | mute BREAK, KICK, SNARE, GHOST, HATS, PERC, IMPACT (lit = playing). SEL + key: solo |
| white 16 | MUTATE |
| black 1..8 | queue scene 1..8 (INTRO, BUILD, DROP, MUTATION, VOID, IMPACT, 7, 8) at the SCENE page's quantization. SAVE + key: store the current state there |
| black 9 / 10 | FILL 2 bars / 4 bars |
| black 11 | UNDO the last mutation |

Releasing a held mode key ends that mode at once; REC (RETURN) ends everything at the next bar.

## SEQUENCE (SEQ)

![SEQUENCE: all seven tracks, 16 steps, the playheads](img/seq.png "SEQUENCE: all seven tracks, 16 steps, the playheads")

All seven tracks at once, 16 steps of the current page (OCT−/OCT+), the selected track marked, mutes struck
through, the playhead above each.

| Control | Does |
|---|---|
| white key (tap) | step on / off on the selected track |
| white key (hold) + K1 / K2 / K3 / K4 | that step's VELOCITY / PROBABILITY / RATCHET (1–8 hits) / MICROTIMING (±½ step) |
| white key (hold) + SEL + K1..K4 | a parameter lock of one of the track's four sound macros on that step (two locks a step) |
| white key (hold) + ALGO | BREAK track: the step's slice. Drum tracks: a tune lock (¼ semitone a click) |
| white key (hold) + black 11 | HATS: open / closed. BREAK: reverse the step |
| black 1..7 | select BREAK, KICK, SNARE, GHOST, HATS, PERC, IMPACT (SELECT turns through them too) |
| black 8 | generate the GHOST track (density: K3) |
| black 9 / 10 | MUTATE / UNDO |
| K1..K4 (no step held) | track LENGTH (1–32), SWING (50–75 %), GHOST density, MUTATE amount |

Turning a knob while a step key is held does not toggle the step when you let go.

## SOUND (EDIT)

![SOUND on the BREAK track: the waveform with its slices](img/sound_break.png "SOUND on the BREAK track: the waveform with its slices")

| Control | Does |
|---|---|
| black 1..7, SELECT | the track |
| white keys | audition: BREAK plays slice 1..16; a drum track plays its sound (softer to louder left to right) |
| K1..K4, drum tracks | the model's four macros: KICK WEIGHT PUNCH PITCH DRIVE · SNARE BODY CRACK TAIL PRESSURE · GHOST TONE SNAP DECAY SWING · HATS METAL BRIGHT LENGTH HUMAN · PERC TONE DECAY NOISE BEND · IMPACT SIZE SWEEP NOISE DIRT |
| ALGO, PERC track | the PERC voice: TOM, RIM, SHAKER, CLICK |
| K1..K4, BREAK track | PITCH (±12 semitones), LEVEL, SLICES (8 / 16 / 32), GATE |

The BREAK screen shows the break's name, tempo, slice count, whether it comes from the bank or is the built-in test
break, and the waveform with the slice marks.

## FX (DRUM PRESSURE)

![FX: DRUM PRESSURE's knobs and meters](img/fx.png "FX: DRUM PRESSURE's knobs and meters")

K1 PUNCH (transient contour), K2 CRUSH (parallel bit/rate crush), K3 GLUE (bus compression), K4 PRESSURE
(saturation, threshold and a compensating trim together: denser, not just louder). ALGO: the presets CLEAN JUNGLE,
ROLLING BREAKS, TIGHT NEURO, CRUSHED AMEN, INDUSTRIAL. PRESET: output trim. The screen: peak meter, glue gain
reduction, the peak guard's catches. The ceiling is −0.5 dBFS; the guard has no look-ahead (not a true-peak
limiter).

## SCENE (ARP)

![SCENE: the eight scenes, quantization, BREAK-LOCK](img/scene.png "SCENE: the eight scenes, quantization, BREAK-LOCK")

white 1..8: queue a scene; SAVE + white: store. K1 QUANT (NOW, BEAT, BAR, 2 BAR, 4 BAR), K2 BREAK-LOCK (OFF; LAYER:
the synth kick/snare also play where the break plays a kick/snare slice; SEPARATE: they skip those steps, no
doubling), K3 ANCHORS (mutation and the rhythm graph keep the beats), K4 LINK KICK (send kick/snare triggers to
Abyss Engine). A scene holds the pattern, mutes, BREAK and KIT positions, ENERGY and the DRUM PRESSURE settings.

## SYSTEM (GLO)

![SYSTEM: clock, link, bank, load](img/system.png "SYSTEM: clock, link, bank, load")

K1 BPM, K2 CLOCK (INT / MIDI in), K3 LINK CH, K4 SYNC OUT (clock and transport out). PRESET: the USB port as a host
(to host the other FM-1 over an OTG cable — unverified on hardware). The page shows the load and dropouts, the
clock, the link, the bank, the store's state ("DEFAULTS USED" when saved data did not check), queue and command
drops, the USB mode.

## MIDI

In: clock (`F8`, with CLOCK = MIDI), start/stop, notes 36–41 play KICK … IMPACT live, notes 48–79 play slices
1–32. Out (USB): clock and transport (SYNC OUT), the link ([docs/architecture/link-protocol.md](https://github.com/RahelPuest/jungle-os-fm1/blob/main/docs/architecture/link-protocol.md)).

## Loading your own breaks

1. Prepare: `python3 tools/sample-prep/jo_sample_prep.py build my.bank amen_like.wav:2:16:ROLLER other.wav:1:8 --snap`
   (each input `file:bars:slices:name`; WAV or AIFF; up to 8 breaks, 241,664 bytes ≈ 5.5 s at 22 kHz in all).
2. Stop the FM-1 (not playing), connect USB, then
   `python3 tools/sample-prep/jo_send_bank.py my.bank` (needs `pip3 install mido python-rtmidi`).
3. The screen says BANK LOADED and the break's name. The bank stays across power-offs.

Untested on a physical FM-1 so far (it is tested in fm1-base's simulator). If a transfer fails, Break Surgeon keeps
working with its built-in test break. Use only audio you have the rights to.
