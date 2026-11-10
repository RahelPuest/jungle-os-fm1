# Upstream firmware audit

Inspected 2026-11-10 (shallow clones, read only; nothing of theirs was built or run). VERIFIED = read in their code,
REPORTED = their README or docs. File:line references are into each repository at the commit listed.

## The repositories

| Project | Repository | Commit | Licence | Base | Audio block | Use in JUNGLE OS |
|---|---|---|---|---|---|---|
| **fm1-base** | github.com/charlesvestal/fm1-base | `b0976f2` (2026-11-08) | **MPL-2.0** | felucca-bsp | app's (`APP_BLOCK`) | **the foundation**: submodule `base/`, unmodified |
| felucca-bsp | github.com/hugelton/felucca-bsp | `5b0879b` (fm1-base's submodule) | **MPL-2.0** | — | — | through fm1-base |
| TONIC | ../digitone (RahelPuest/tonic-fm1) | `64634b6` (0.30) | **MPL-2.0** | fm1-base | 256 | studied (structure, the build, UI idioms); no file copied |
| SLOOP | github.com/isod89/sloop-fm1 | `bac9c05` (2026-11-03) | GPL-3.0-only | Felucca | 256 | ideas only |
| X0X | github.com/charlesvestal/fm1-x0x | `a0d1a13` (2026-11-08) | GPL-3.0-only | Felucca platform | 512 | ideas only |
| Felucca | github.com/hugelton/Felucca | `63f88ec` (1.6.1) | GPL-3.0-only (engines submodule MIT; sample pack CC0) | — | 128 | ideas only |
| PIGEON | github.com/TheShrimpBlimp/pigeon | `036fb26` (2026-10-29) | GPL-3.0-only (+ libmad GPL-2.0+) | Felucca 1.5.1 | 128 | ideas only |
| sloopDX | github.com/zvenson/dxsloop | `8c1035b` (2026-11-03) | GPL-3.0-only | SLOOP | 256 | ideas only |
| fm1_open | github.com/m-vave/fm1_open | `4c7496d` | see its LICENSE.md | M-VAVE SDK | 128 | not used |
| fm1-emu | github.com/joakimeriksson/fm1-emu | `5c6ed85` (0.4.0) | its LICENSE | — | — | a test tool: runs our device binaries |

"SLOOP", "X0X" and "Felucca" were named as leads; all three exist and are the projects above.

## The decision: one foundation, original engines

**JUNGLE OS is MPL-2.0 and is built on fm1-base, unmodified, as a git submodule.** Reasons:
- fm1-base is the platform layer most firmwares are converging on (CTL-1, Radio, TONIC), with a host simulator, a
  browser emulator, the installer, storage, MIDI (USB, TRS, USB host, BLE), screen reader and static safety checks
  (no soft-double, the CPU erratum, register access, stack and RAM-code checks). Those are exactly the subsystems
  the brief says not to rewrite.
- Its licence is per file, so an app built on it can be MPL-2.0 and still link JieLi's binary Bluetooth libraries
  later (a GPL-3.0 firmware could not).
- Every instrument firmware with sampling or drums (SLOOP, X0X, Felucca, PIGEON) is **GPL-3.0-only**. Copying any of
  their code would make JUNGLE OS GPL-3.0 and close the Bluetooth route. So: **no code from them**; their published
  *facts and interfaces* (memory maps, protocols' shapes, measured behaviour) informed the design, and every line
  here is original. This is recorded per idea below so a later contributor can check it.
- The two-firmware split is two fm1-base apps (`firmware/break-surgeon`, `firmware/abyss-engine`) sharing original
  engines in `shared/`. No fork, no multi-fork merge.

Reusable non-GPL material noted for later (not used yet): Felucca-Engines (MIT), the Hügelton Sample Pack generator
and its sounds (CC0 since Felucca 1.6.1 — older forks still say "all rights reserved"), CC0 VSCO-2/VCSL samples,
Open303 (MIT), Mutable Instruments Braids/stmlib (MIT, used by TONIC).

## What each upstream taught (facts, then what JUNGLE OS does)

### Sample storage and transfer
- Felucca, SLOOP and X0X store user samples in 80 KiB "FSMP" slots of 4-bit IMA ADPCM at 22.05 kHz (~7.4 s a
  slot), at 0xA0000 / 0xB4000 / 0xC8000 (SLOOP adds 0xE7000), read in place through XIP (VERIFIED: Felucca
  `firmware/src/eng_sample.c:45-49`; SLOOP `firmware/src/storage.c:22-26`).
- Transfer is USB-MIDI SysEx (`F0 7D 46 4C cmd`), 7-bit packed chunks of ≤ 256 bytes, a sector erased when a write
  reaches it, **the header committed last after a CRC check** (VERIFIED: SLOOP `web/EDITOR_PROTOCOL.md:65-69`;
  Felucca `firmware/src/editor_backup.c:5`).
- ADPCM slicing needs decoder state checkpoints at slice points and for reverse (VERIFIED: X0X
  `firmware/src/dsp/breaks.h:2-26`; SLOOP `firmware/src/eng_slice.c:3-33`).
- PIGEON reaches a 500 KiB pool by shrinking its app to ~360 KiB, and records host PCM over a USB-audio OUT stream
  into flash (VERIFIED: PIGEON `firmware/src/usb_sample_rx.h:1-30`).

JUNGLE OS: its own bank format **JOB1** (docs/development/sample-format.md) — 16-bit PCM at 22,059 Hz (half the
FM-1's rate), up to 8 breaks × 32 slices with slice classes, in one 236 KiB region (0xA1000..0xDBFFF), read in
place; transfer by its own SysEx protocol (`F0 7D 4A 42`), the bank validated by CRC and the full parser before it
is used. 16-bit PCM was chosen over ADPCM for the first version: random slice access, reverse and microloops need
no decoder checkpoints, at the cost of half the capacity (~5.5 s instead of ~11 s). ADPCM is on the roadmap.

### Sequencer timing
- Felucca, SLOOP and PIGEON run the sequencer once per 32-sample control block (0.73 ms grid) (VERIFIED: SLOOP
  `firmware/src/seq.c:4`). X0X splits the render at every due event, sample-accurate, with Q8 fractional times
  (VERIFIED: X0X `firmware/src/seq/sequencer.h:2-11`), and schedules swing from each step's grid slot so changing
  swing never drifts a part (VERIFIED: `sequencer.c:69-134`).

JUNGLE OS: sample-accurate too (the render is split at each event), with an integer sample time base, a 32-bit
step phase, and steps evaluated half a step early so negative microtiming and swing are scheduled ahead. Ties at the
same sample fire in a fixed order. Tested over 20 simulated minutes at 174 BPM with no drift
(`tests/unit/test_seq.c`).

### MIDI clock
- SLOOP: pulses stamped at 1 ms, the tempo an 8-beat mean, never running past the next pulse, back to the internal
  clock after 500 ms without a pulse; clock out over USB only while on the internal clock, integer BPM (VERIFIED:
  `seq.c:3131-3270`). X0X: the tick period measured in samples with a 1/8 IIR, clock out sample-accurate (VERIFIED:
  `sequencer.c:504-534`). Felucca/PIGEON: clock in only.

JUNGLE OS: clock in stamped by fm1-base at arrival (`midi_stamp`), the interval smoothed, the step phase pulled
toward the clock count (at most 1/8 of the error a clock, never across half a step); 500 ms without a clock returns to
the internal clock at the followed tempo; clock out from the render on the step phase, only on the internal clock.

### Drums and master processing
- Drum models across the forks: swept-sine kicks, two-sine + noise snares, six-square metallic hats at 808-style
  ratios (Felucca `drum_voice.c`, SLOOP `drum_synth.c`, X0X's 909/808 circuit models). Masters: look-ahead limiters
  into soft clippers (SLOOP, Felucca), X0X's compressor with a kick-keyed pump.

JUNGLE OS: its own six models (docs/architecture/overview.md) with four curated macros each and per-hit parameter
derivation; DRUM PRESSURE (transient contour, parallel crush, saturation, glue compressor, soft clip, peak guard);
the peak guard has **no look-ahead** and is documented as not a true-peak limiter.

### Stability lessons
- The second core needs raised supply voltages and a check of every job, with a fall-back to one core (SLOOP, X0X,
  Felucca). JUNGLE OS uses **one core**; fm1-base's `cpu2` option exists for later.
- Newer FM-1s with the UC25HQ80 flash need an allow-listed flash id: fm1-base's `device/fm1_flash_ok.h` covers it,
  and JUNGLE OS inherits it.
- Saving while playing stalls audio (flash erase stops XIP): Felucca saves only when stopped. JUNGLE OS refuses SAVE
  while the transport runs.

## Reuse plan (upstream component → JUNGLE OS module)

| Upstream | Target | Modification | Risk | Verification |
|---|---|---|---|---|
| fm1-base platform (boot, audio ISR, LCD, panel scan, LEDs, MIDI, store, installer, host sim) | `base/` submodule | none | its `plat.h` is beta and may change | both firmwares build; host scripts in `tests/host/` |
| fm1-base canvas and fonts (`device/gfx.c`) | `shared/ui/jo_ui.c` uses them | none | — | host screenshots |
| fm1-base store (A/B, CRC) | presets/patterns/scenes | a versioned record header on top (`shared/store`) | object size ≤ 3,840 B | `test_store_records`, reboot persistence in `tests/host/*.txt` |
| fm1-base `usb_host`, `midi_stamp` | the link | enabled in fm1.toml | FM-1↔FM-1 hosting unverified | hardware test G (pending) |
| fm1-base `tools/fm1_install.py` | installation | none | — | not yet run against a device |
| fm1-emu | CPU lower bounds | none | no cache / flash wait-state model | docs/development/profiling.md |
