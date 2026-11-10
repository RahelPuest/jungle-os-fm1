# The ABYSS ENGINE manual

Neuro bass (FM CREATURE, REESE MATRIX) navigated through a curated sweetspot graph, a separate clean sub, stabs, an
evolving pad, a harmonic sequencer on a shared key, ducking from Break Surgeon's kicks, and a bass-safe master.
Status: runs in the simulators; **not yet tested on an FM-1**.

## Buttons (every page)

| Button | Tap | With SEL held |
|---|---|---|
| HOME / SEQ / EDIT / FX / ARP / GLO | PERFORM / SEQUENCE / SOUND / FX / SCENE / SYSTEM | |
| PLAY | start / stop (with FOLLOW = LINK, Break Surgeon's transport starts and stops it too) | **PANIC**: all notes off, stop |
| ENV | which part the keyboard plays: BASS → STAB → PAD | |
| REC | PAD HOLD on / off (the pad holds the current chord; off lets the tails ring out) | |
| LFO | MUTATE the bass sound (radius: SEQUENCE K3) | **REVERT** the last mutation |
| SAVE | save (refused while playing) | |
| OCT− / OCT+ | keyboard octave (−2..+2) | |

**SEL + white 1..8** queues scene 1..8 from any page but SEQUENCE and SCENE.

## The keyboard

On PERFORM, SOUND, FX and SYSTEM every key plays the focused part, with velocity: BASS from F1 at the lowest key
(bass and sub together, legato when keys overlap), STAB a chord on each key (the chord type and tension of the
SOUND page), PAD a held chord on the key. The root's keys glow dimly.

## PERFORM (HOME)

![PERFORM: the bass node SNARL on the sweetspot path, MORPH MOTION BITE SPACE](img/perform.png "PERFORM: the bass node SNARL on the sweetspot path, MORPH MOTION BITE SPACE")

The screen: the nearest sweetspot node (and UNREVIEWED while no node is certified), MUTATED / EXPLORE, the MORPH
path with its nodes, the keyboard's part and octave, PAD state, scene, duck and output meters, LINK / SOLO.

| Control | Does |
|---|---|
| K1 MORPH | along the path HOLLOW → RUBBER → TALK → GROWL → SNARL → METALLIC → LASER → BROKEN, every parameter at once, trimmed for loudness |
| K2 MOTION | the groove LFO's depth and how many destinations follow it (index, cutoff, vowel) |
| K3 BITE | drive, brightness, fold and formant together, with a compensating trim |
| K4 SPACE | the delay/reverb send of stabs and pad (the bass and sub stay dry) |
| ALGO | EXPLORE: step to the next / previous neighbour of the nearest node (this reaches REESE and REESE DARK; a model change crossfades) |
| PRESET | SNAP: MORPH jumps to the nearest node, mutation cleared |
| SELECT | tempo |

## SEQUENCE (SEQ)

![SEQUENCE: the bass, stab and pad tracks; a held step's locks below](img/seq.png "SEQUENCE: the bass, stab and pad tracks; a held step's locks below")

Three tracks, 16 steps shown: BASS, STAB, PAD. Lit steps on the white keys.

| Control | Does |
|---|---|
| white (tap) | step on / off |
| white (hold) + K1 | the note: BASS in scale steps around the root; STAB and PAD the chord's scale degree |
| white (hold) + K2 / K3 / K4 | VELOCITY / a MORPH lock / a BITE lock (BASS) |
| white (hold) + ALGO | an octave lock (−2..+2) |
| white (hold) + black 4 / black 5 | ACCENT / SLIDE (legato glide into this note) |
| black 1..3, SELECT | the track |
| black 11 | mute the track |
| PRESET | the next / previous pattern, at the next bar |
| K1..K4 (no step held) | LENGTH (1–32; steps 17–32 are not editable on this page yet), SWING, MUTATE RADIUS, SPEED (1/16, 1/32, 1/8) |

## SOUND (EDIT)

![SOUND on the STAB voice: RAVE CHORD, key D minor](img/sound.png "SOUND on the STAB voice: RAVE CHORD, key D minor")

The focused part (ENV). SELECT: the scale (MINOR, HARM MIN, PHRYGIAN, DORIAN, CHROMA). PRESET: the root. ALGO: the
chord type (MIN, MIN7, SUS2, MIN9, MAJ7, DIM, CLUSTER, FIFTH) — or, on STAB, the stab model (RAVE CHORD, ORCH HIT,
GLASS FM, DARK CHORD).

| Part | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| BASS | MOD SHAPE (SINE, EXP, PULSE, ASYM, STEP) | SUB SAT | SUB OCT (−1 / 0) | TENSION (adds 9 / b9 to chords) |
| STAB | CHORD / TENSION | ATTACK | BITE | TAIL |
| PAD | DARKNESS | EVOLVE | TENSION | DEPTH |

The BASS screen shows all 15 parameters of the bass as the graph sets them now.

## FX (NEURO PRESSURE)

![FX: NEURO PRESSURE](img/fx.png "FX: NEURO PRESSURE")

K1 WEIGHT (the sub's level), K2 DIRT (drive of the bass above 115 Hz), K3 WIDTH (stereo of pads/stabs above
140 Hz — never the sub), K4 PRESSURE (glue and density with a compensating trim). PRESET: output trim. The screen:
peak, glue reduction, guard catches.

## SCENE (ARP)

white 1..8 queue, SAVE + white store. K1 QUANT, K2 DUCK depth (the kick ducking), K3 SOLO DUCK (duck on every beat
when no link is heard), K4 FOLLOW (LINK: the link's clock, transport, scenes and fills; OFF: ignore them).
A scene holds the pattern, mutes, PAD HOLD, MORPH / MOTION / BITE / SPACE, NEURO PRESSURE, root and scale.

## SYSTEM (GLO)

![SYSTEM: clock, link, load](img/system.png "SYSTEM: clock, link, load")

K1 BPM, K2 VOICES (FULL, NO PAD, BASS ONLY: drop parts when the load is too high), K3 LINK CH, K4 FOLLOW. PRESET:
the USB port as a host (unverified with another FM-1). The page: load and dropouts, the link, kicks received, the
store, the USB mode.
