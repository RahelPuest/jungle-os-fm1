# Live performance with both instruments

Break Surgeon (A) on one mixer channel, Abyss Engine (B) on another. The mixer's model is not confirmed: nothing here
assumes it has MIDI, effects, sends or a particular routing. Each FM-1's output is stereo (headphone jack); A's
signal is mono-compatible by design (the drum bus is mono), B's sub and bass are centred.

## Before the set

1. Link (optional): see docs/architecture/link-protocol.md for the routes; set the same LINK CH on both. On A:
   SYSTEM K4 SYNC OUT = ON, CLOCK = INT. On B: FOLLOW = LINK. Without a link: same BPM on both, PLAY together, SOLO
   DUCK on B.
2. Gain: MASTER on both at about 3/4. A's ceiling is −0.5 dBFS with typical peaks around −2.5 dBFS (renders: peak
   0.75 at MASTER-equivalent 0.9); B peaks similar with more RMS (bass). Set the mixer channels so B's sub sits
   under A's kick, not on top: start with B's channel ~3 dB below A's and adjust by ear on the actual PA.
3. Low end: B's sub lives at 36–72 Hz, mono. If the PA has a crossover, check the sub on it before the set
   (docs/development/testing.md, listening checklist).
4. Save both (stopped).

## A 20-minute improvisation (one route through it)

| Time | Break Surgeon | Abyss Engine | Mixer |
|---|---|---|---|
| 0:00 | stopped | scene INTRO (pad only), PAD HOLD, MORPH low, SPACE high; play pad chords on the keys (SELECT → PAD; SEL + REC holds the pad) | B up, A down |
| 1:30 | PLAY with scene INTRO (thinned break, no synth kick/snare) | keep the pad; slowly raise DARKNESS / EVOLVE | bring A in |
| 3:00 | scene BUILD (rolling, more hats), K4 FILL AMT up | scene BUILD: a sparse bass enters | |
| 4:30 | FILL 2 bars (SEL + ENV) into... | FILL (ENV): bass rolls into the drop | |
| 5:00 | scene DROP (kick + snare + full break) | scene DROP (bass line, sub, pad released) | both full |
| 7:00 | BREAK knob toward CHOPPED; hold CHOP / REVERSE for single bars | MORPH through TALK → GROWL; MOTION up | |
| 9:00 | MUTATE (LFO), keep it or SEL+LFO to undo | MUTATE (LFO) on the bass; SNAP (PRESET) back to a node | |
| 10:30 | scene VOID (break fragments, drums muted) | scene VOID (pad, no bass), SPACE up | A down a little |
| 12:00 | FREEZE (white 7) held over the last beat, release on the one | stab chords on the keys (SELECT → STAB) | |
| 12:30 | scene IMPACT (half-time, INDUSTRIAL kit) | scene IMPACT (stabs, aggressive bass) | |
| 14:00 | FILL 4 bars (black 10), then scene MUTATION | scene MUTATION (BITE high) | |
| 16:00 | KIT knob toward METAL TECHSTEP; PRESSURE up | EXPLORE (ALGO) to REESE DARK | |
| 18:00 | REC (RETURN) on a bar: the clean groove | REC (RETURN): SNAP; MORPH to HOLLOW, BITE down | |
| 19:00 | mute KICK and SNARE (white 10, 11), leave the break | PAD HOLD on (SEL + REC), bass muted (SEL + black 1) | |
| 20:00 | STOP on a downbeat | the pad's tail rings out (SEL + REC: PAD HOLD off) | fade |

With the link, a scene pressed on A goes to B as well (same number, same bar). Scenes stored on each device
should correspond (scene 3 = DROP on both).

## The same hands on both

Both firmwares share their buttons, so a gesture on one does the same on the other:

| | Break Surgeon | Abyss Engine |
|---|---|---|
| PLAY / SEL + PLAY | start, stop / panic | start, stop / panic |
| REC | RETURN to the clean groove at the next bar | RETURN: SNAP, mutation and fill off |
| ENV / SEL + ENV | fill 1 / 2 bars | fill 1 / 2 bars (bass rolls) |
| LFO / SEL + LFO | mutate the break / undo | mutate the bass / undo |
| SEL + white 1–8 | queue scene 1–8 | queue scene 1–8 |
| SAVE + white 1–8 | store scene 1–8 | store scene 1–8 |
| SEL + black | mute track 1–7 | mute BASS, STAB, PAD |
| PERFORM: ALGO / PRESET | the kit one sweetspot along / SNAP | EXPLORE / SNAP |
| SEL + SEQ | — | CALL & RESPONSE: the bass in a 4-bar form A B A C |

**Call and response across the rig:** with Abyss's CALL & RESPONSE on, the answers B and C land on bars 2 and 4 of every 4 bars, the same grid Break Surgeon's scenes and fills use. A 1-bar fill on Break Surgeon (ENV) in bar 4 under C makes that bar the turnaround of the phrase.

Each screen shows, at the bottom, what the keys and encoders do right now, and what they do with SEL or SAVE held.

## Transitions that work

- **On the bar, every time**: scenes and pattern changes are quantized (SCENE K1); press anywhere in the bar
  before.
- **Fill into a scene**: FILL first, the scene on the same bar: the fill ends with an IMPACT hit on the downbeat.
- **Safe exit**: REC (RETURN) on either: A back to the stable groove at the next bar, B onto a curated node.
- **Panic**: SEL + PLAY on either device stops and silences it.

## Gain staging inside the instruments

- A: PRESSURE adds density, not level (it trims as it pushes). Watch the FX page's peak meter: red is the ceiling.
- B: BITE and DIRT are trimmed; WEIGHT changes only the sub. The guard catches show on the FX page: occasional is
  fine, constant means MASTER or PRESSURE is too high.

## Hardware checks still pending

Everything above is designed and runs in the simulators. Before relying on it live: the FM-1 tests in
docs/development/testing.md (§ hardware), especially CPU load with everything playing, saving, and the link.
