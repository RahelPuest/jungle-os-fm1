# JUNGLE OS architecture

## Layers

```
firmware/break-surgeon/app.c      firmware/abyss-engine/app.c        screens, controls, storage, MIDI, loader
        │ (unity build with fm1-base: -Os)        │
firmware/*/engine/bs_engine.c     firmware/*/engine/ab_engine.c      the instruments (platform-free)
        │ (dsp_o2.c: one -O2 unit per firmware)   │
shared/seq   shared/drums  shared/sampler  shared/synth  shared/fx  shared/sweetspot  shared/link  shared/store
shared/dsp (jo_math.h, jo_dsp.h: always-inline, float, no double)        shared/ui (fm1-base canvas helpers)
        │
base/ = fm1-base b0976f2 (MPL-2.0): plat.h — audio ISR, LCD, panel, LEDs, MIDI, store, USB host, installer
base/bsp/ = felucca-bsp: the FM-1 board support
```

Everything under `shared/` and `firmware/*/engine/` builds on a Mac with plain `cc` (`make -C tests`); the
`app.c` files need fm1-base (its host simulator or the JieLi toolchain). The brief's `/shared/hal`, `/shared/audio`,
`/shared/midi`, `/shared/storage` and `/shared/diagnostics` are fm1-base itself (`plat.h`); JUNGLE OS does not
wrap it again.

## Threads of execution

| Context | Runs | May |
|---|---|---|
| Audio interrupt (every 128 frames, 2.9 ms) | `app_render` → MIDI in (clock, link), the command ring, the sequencer, voices, master | read the flash (the bank), send MIDI; never wait, allocate, log or write the flash |
| Main loop (~60 frames/s) | `app_frame`: controls, screen, storage, the bank loader, mutation/ghost generation | write the flash (only when stopped), lock the interrupt briefly (`plat_lock`) for bulk pattern changes |

Main loop → audio: the **command ring** (`shared/seq/jo_cmdq.h`, 32 entries, single producer / single consumer, no
lock; a full ring refuses and counts). Single-byte edits of a pattern (a step on/off, a velocity) are written in
place — a byte store is atomic. A whole-track change (mutation, ghosts, undo) is built in a copy, then swapped in
under `plat_lock` (~1 µs: a 324-byte copy).

Audio → main loop: plain reads of engine fields (meters, positions); stale by at most a block.

## The sequencer (shared/seq)

- Clock: a 32-bit phase per 16th (one wrap = one step), an integer sample counter as the time base. No float
  accumulates time (a float loses whole samples after 2^24 ≈ 6.3 minutes).
- Step S is evaluated when step S−1's phase crosses ½; its hits are queued at
  `grid + (0.5 + micro) step (+ swing)`, so microtiming reaches ±½ step and swing 75 % without look-back.
  Latency from START to the first step: ½ step (10.8 ms at 174 BPM).
- A **transform hook** gives the instrument the last word on every step, on or off (fills, performance modes, the
  rhythm graph, BREAK-LOCK), on a copy — the stored pattern never changes by playing.
- Probability and generated variations use `js_hash(seed, bar, step, track)`: repeatable per pass and seed.
- The **event queue** (64 entries) is ordered by due time, then track, then ratchet hit: simultaneous events always
  fire in the same order. The render is split at each due event, so every hit starts on its exact sample.
- Quantized switches (pattern, scene, RETURN) land at the start of the step before the boundary (the boundary
  step is evaluated after that), and their sound changes at the boundary itself.

## Break Surgeon's audio graph

```
                  ┌─ break player (2 voices: crossfade, fades, reverse, microloop) ─┐
step events ──────┤                                                                  ├─ mono bus ─► DRUM PRESSURE ─► L = R
(seq + transform) └─ 6 drum voices (KICK SNARE GHOST HATS PERC IMPACT, mono each) ───┘
DRUM PRESSURE: transient contour → parallel crush (hold + bits) → tanh saturation → glue compressor (soft knee)
               → DC blocker → soft clip → peak guard (−0.5 dBFS) → MASTER
```
Voices: one per drum track (a retrigger de-clicks by fading the last output over ~1.5 ms; a closed hat chokes the
open one by retriggering the same voice); two break voices. No stealing is needed: the count is fixed.

## Abyss Engine's audio graph

```
bass track ─► neuro bass (FM CREATURE | REESE, crossfaded switch) ─► × node trim × BITE trim ─┐
          └─► clean sub (sine/tri, folded to 36..72 Hz) ──────────────────────────────────────┤
stab track ─► 4 stab voices ─┐                                                                  │
pad track ──► 4 pad voices ──┴─ stereo ─► send ─► ping-pong delay + FDN reverb ─► stereo ──────┤
                                                                                                ▼
NEURO PRESSURE: sub × WEIGHT × duck ─┐                                      (mono, centred)
                bass ─► HP 115 Hz ─► DIRT drive × duck ─┤                   (mono, centred)
                stereo ─► mid + side (side high-passed 140 Hz, × WIDTH) ─┴─► glue comp ─► DC ─► soft clip ─► guard
```
The sub never passes the dirty drive or the width stage; the width stage cannot widen below 140 Hz (measured:
side energy below 120 Hz ≤ −57 dB re mid in the renders).

Voice allocation: bass and sub monophonic, last note wins, legato on SLIDE (no retrigger, glide); stabs: a chord
takes the 4 voices (fewer notes release the rest); pad: voices already on a wanted note keep sounding, the others
release and are re-used (so chord changes do not cut common tones). Budget modes FULL / NO PAD / BASS ONLY drop
whole parts by priority (sub and bass last).

## Sweetspot graph (shared/sweetspot)

A model declares its parameters (range, curve: LIN / LOG / DISCRETE, mutation slew). Nodes are complete normalized
vectors with a gain trim and a review flag; edges say which transitions exist (and which must crossfade); MORPH
walks a designed path with a smoothstep inside each segment (nodes sit flat: easy to land on), discrete
parameters switch at the segment's middle, trims interpolate in dB. SNAP jumps to the nearest path node, EXPLORE
steps to graph neighbours, MUTATE moves each parameter by at most radius × its slew (deterministic per seed) with a
history for REVERT. `jg_validate` rejects ranges, edges or paths out of bounds and paths that use a missing edge.

Graphs today: Abyss FM (15 parameters, 10 nodes, path of 8); Break Surgeon rhythm (8, 4 nodes: CLEAN, ROLLING,
CHOPPED, GLITCHED) and kit (16, 5 nodes). All nodes are UNREVIEWED.

## Flash map (both firmwares)

| Range | Use |
|---|---|
| 0x04000..0x92FFF | the firmware (written only by the update loader) |
| 0x97000..0xA0FFF | patterns 1–4 and the scenes: store objects (A/B sector pairs) |
| 0xA1000..0xDBFFF | Break Surgeon: the JOB1 sample bank (241,664 B). Abyss: unused |
| 0xDC000..0xDDFFF | the panel calibration (fm1-base's object, at the template's address) |
| 0xDE000..0xDFFFF | settings |
| 0xFF000.. | the unit's key/MAC: never touched |

Records inside the store objects carry `magic, version, length, CRC-32` (`shared/store`); anything that does not
check is replaced by defaults, and the SYSTEM page says so ("STORE DEFAULTS USED").

## Resource budget

| | Break Surgeon | Abyss Engine | Limit |
|---|---|---|---|
| Firmware image | 141,380 B | 142,320 B | 581,564 B |
| `.data + .bss` | 26,652 B | 29,452 B | 98,304 B |
| Pool (incl. fm1-base's canvas) | 153,940 B | 189,316 B | 344,064 B |
| Biggest buffers | test break 62 KB | delay 64 KB + reverb 32 KB | |
| Sample bank (flash) | 241,664 B ≈ 5.5 s at 22 kHz | — | |
| Audio load, fm1-emu at 240 MHz (lower bound) | 8–16 % | 28–38 % (bass + sub + pad + stab) | 90 % (fm1-base's overload) |

The fm1-emu figures count instructions of the real binary at one per cycle, with no cache misses or flash wait
states: real loads will be higher by an unknown factor (docs/development/profiling.md).

Priorities when time runs out (design; automatic shedding is not implemented yet): 1 clock and audio stability,
2 kick/snare/break on A and sub/bass on B, 3 master safety, 4 stabs, 5 pads and ambience, 6 extras. On Abyss the
voice modes apply them by hand today.
