# JUNGLE OS

JUNGLE OS is two custom firmwares for two M-VAVE FM-1s that together make a hardware drum & bass, jungle and neurofunk live rig. Each works alone. Together they talk JUNGLE LINK over plain MIDI: clock, transport, scenes, fills and kick triggers for ducking.

![BREAK SURGEON's main page: the selected track big, every track below](break-surgeon/img/home.png "BREAK SURGEON")
![ABYSS ENGINE's main page: the bass line with its notes](abyss-engine/img/home.png "ABYSS ENGINE")

Version {{VERSION}}. **Neither firmware has been tested on a physical FM-1 yet.** Both are tested in software: 620 engine checks, audio-regression renders, fm1-base's host simulator, and fm1-emu, an instruction-level emulator that runs the real firmware files. The two-FM-1 link is untested. Installing is at your own risk; the installer can put M-VAVE's own firmware back.

**New in 0.7:** the sound. ABYSS ENGINE gets five bass models (FM NEURO, REESE, WOMP, HORN, SINE/808), sixteen drum & bass sounds from the rolling Reese and the hoover to the neuro growl, and thirteen STYLES that play each the way the genre does. BREAK SURGEON gets a rebuilt kit (kick, snare with a clap, hats), a console EQ on the bus, six synthesized classic breaks (AMEN, THINK, APACHE, FUNKY DRUMMER, HOT PANTS, ROLLER: the patterns, not the recordings) and 18 grooves with jungle edits.

**New in 0.6:** the sequencers get ideas of their own. BREAK SURGEON cuts with ROUTES (the playhead's path through the break: snake, spiral, stutter, a random walk or one you draw) and breeds grooves in a DNA family tree. ABYSS ENGINE plays a DUET: it answers what you play, moves its bass into the gaps of the drums and echoes your gestures. One ENERGY joins both: every step has a level, one knob builds and drops the whole rig. TAPE DECAY lets recorded steps fade unless you burn them in.

**New in 0.5:** both are step sequencers first. The white keys are always the steps, the black keys the tracks; the live tools sit on held buttons (FX, SONG, FILL, VARY, REC, STORE) and letting go is always safe. Trig conditions, 4/8/16-bar phrases, variations A–D with a FORM, live recording, undo and redo. ABYSS ENGINE can play on BREAK SURGEON's kicks and answer its fills.

**New in 0.4:** BREAK SURGEON has a library of 14 classic drum & bass grooves, the break re-chopped to each. ABYSS ENGINE plays call and response: a 4-bar bass form A B A C, the answers B and C each with their own line and sound (SEL + SEQ).

**New in 0.3.1:** BREAK SURGEON starts with the Amen pattern, synthesized from its own drums (the pattern, not the copyrighted recording).

**New in 0.3:** both firmwares look like drum & bass. Black screens, acid lime for BREAK SURGEON and abyss cyan for ABYSS ENGINE, hazard stripes, big values. Texts always fit their boxes.

**New in 0.2:** BREAK SURGEON's kit was rebuilt for drum & bass:
- a short kick with a beater click;
- a snare with crack, snap and tail;
- ghosts that are the snare played softly;
- a room and a sampler's grit on the bus.

Both firmwares now share one control map (the same button does the same thing on both). Each shows on its screen what every key and encoder does.

## BREAK SURGEON: the rhythm

Sliced breaks and six synthesized drum models on one sample-accurate sequencer, with fills, mutation, BREAK-LOCK, held performance modes, rhythm and kit sweetspot graphs, scenes and the DRUM PRESSURE master bus.

- [Try it in the browser](break-surgeon/emu/)
- [Manual](break-surgeon/manual/)
- [Install](break-surgeon/install/)
- [About BREAK SURGEON](break-surgeon/)

## ABYSS ENGINE: the bass

A neuro bass played through a graph of sweetspot sounds, a clean sub, stabs, an evolving pad, a harmonic sequencer, kick-linked ducking and the bass-safe NEURO PRESSURE master.

- [Try it in the browser](abyss-engine/emu/)
- [Manual](abyss-engine/manual/)
- [Install](abyss-engine/install/)
- [About ABYSS ENGINE](abyss-engine/)

## Playing them together

- [The live set](live/): a 20-minute set, transitions, gain staging, linking the two.
- [Source code on GitHub](https://github.com/RahelPuest/jungle-os-fm1): the engines, the tests, the sample tools, the research notes.

The FM-1 has no audio input and its TRS jack is MIDI in only. JUNGLE OS designs around both: breaks are prepared on a computer and loaded over USB, and MIDI out goes over USB.

Built on fm1-base and felucca-bsp (MPL-2.0). JUNGLE OS is MPL-2.0. It is not affiliated with or endorsed by M-VAVE; M-VAVE and FM-1 are trademarks of their owners.
