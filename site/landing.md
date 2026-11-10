# JUNGLE OS

JUNGLE OS is two custom firmwares for two M-VAVE FM-1s that together make a hardware drum & bass, jungle and neurofunk live rig. Each works alone. Together they talk JUNGLE LINK over plain MIDI: clock, transport, scenes, fills and kick triggers for ducking.

![BREAK SURGEON's PERFORM page while playing](break-surgeon/img/home.png "BREAK SURGEON")
![ABYSS ENGINE's PERFORM page: the bass node SNARL](abyss-engine/img/home.png "ABYSS ENGINE")

Version {{VERSION}}. **Neither firmware has been tested on a physical FM-1 yet.** Both are tested in software: 124 engine checks, audio-regression renders, fm1-base's host simulator, and fm1-emu, an instruction-level emulator that runs the real firmware files. The two-FM-1 link is untested. Installing is at your own risk; the installer can put M-VAVE's own firmware back.

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
