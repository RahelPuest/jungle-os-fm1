# JUNGLE LINK v1

The two firmwares talk plain MIDI, so any route that carries USB-MIDI (a computer, a host box, an adapter into the
TRS input, later Bluetooth) carries the link. Source: `shared/link/jo_link.{h,c}`; tests: `test_link_protocol`,
`test_scene_over_link`.

## Roles

- **Break Surgeon is the master**: it sends the clock (when on its internal clock), transport, kick and snare
  triggers, scene and fill commands, and a heartbeat.
- **Abyss Engine follows** (SCENE page K4 / SYSTEM K4 FOLLOW = LINK): clock, transport, scenes, fills, RESET.
- One way is the minimum and all that is implemented. Both firmwares work alone; without the link Abyss runs its
  own clock and can duck on its own beat (SCENE page: SOLO DUCK).

## Messages

Real-time (any channel): `F8` clock (24 PPQN), `FA` start, `FB` continue, `FC` stop.

On the link channel (default 16, SYSTEM page LINK CH, the same on both units):

| Message | Bytes | Meaning |
|---|---|---|
| HELLO | CC 102 = 1 | protocol version, every 500 ms; 1.5 s without any link message = link lost |
| SCENE_PREPARE | CC 103 = scene \| quant << 4 | arm scene 0..15 with a quantization (0 now, 1 beat, 2 bar, 3 two bars, 4 four bars) |
| SCENE_COMMIT | CC 104 = scene | executes only if it matches the last PREPARE; sent twice, the second ignored |
| FILL_BEGIN | CC 105 = bars | a fill from the next bar (Abyss: bass rolls, octave jumps) |
| FILL_END | CC 106 | |
| DROP | CC 107 | (reserved for scene logic) |
| RESET | CC 108 | back to the stable state (Break Surgeon's RETURN; Abyss: mutation and explore off) |
| ENERGY | CC 109 = 0..127 | |
| ROOT, SCALE | CC 110 = 0..11, CC 111 = scale id | |
| KICK_TRIGGER | Note 36 on/off | Abyss's ducking envelope; at most one per 15 ms |
| SNARE_TRIGGER | Note 38 on/off | (received, not used yet) |

**No half scenes**: a lost PREPARE makes the COMMIT a no-op on the follower, a lost COMMIT is covered by the
repeat; a doubled COMMIT is ignored because a COMMIT clears the PREPARE. Both devices switch on the bar boundary
of the shared clock, so a switch sent mid-bar lands together. If the master's switch went through and the
follower's did not (both messages lost), the next scene command realigns them; press the scene again.

## Bandwidth

At 190 BPM with a kick on every 8th: 6.3 kicks/s × 2 messages × 3 bytes ≈ 40 B/s; the clock 76 msg/s; heartbeat 2
msg/s. Under 1 % of a USB-MIDI full-speed link and ~10 % of a 31,250-baud DIN/TRS line.

## Latency and jitter

- Break Surgeon sends a trigger from the render when the hit is fired, before that block's audio reaches the
  codec, so the trigger leaves about one block (2.9 ms) ahead of the sound. Transport delays add to that.
- Abyss stamps each clock at arrival (fm1-base `midi_stamp`, 0.05 ms) and follows a smoothed interval; it pulls its
  step phase toward the clock count by at most 1/8 of the error per clock and never across half a step, so a lost
  clock does not jump a step. 500 ms without a clock: it keeps playing at the last tempo on its own clock.
- Tolerances are not measured on hardware. Expected: USB direct or through a computer: a few ms; BLE: 7.5–30 ms
  connection-interval jitter (the time stamps then matter more).

## Physical routes (test G: PENDING)

See docs/research/hardware.md §MIDI. In order of preference to try: (1) Break Surgeon as USB host (SYSTEM page;
fm1-base `usb_host`) with an OTG cable to Abyss — unverified; (2) a computer or USB-MIDI host routing A's USB MIDI
out to B's USB MIDI in; (3) A's USB into a USB-MIDI→TRS adapter into B's TRS input.

## Manual operation without a link

Set the same tempo on both (SYSTEM BPM, 0.5 BPM steps), start them together on a downbeat (PLAY on both at once),
and nudge by stopping/starting Abyss on a bar. Turn SOLO DUCK on in Abyss for the pump. Scenes are pressed on each
device separately; quantized to the bar, a press within the same bar on both lands together.
