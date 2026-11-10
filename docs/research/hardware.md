# M-VAVE FM-1: hardware facts for JUNGLE OS

Inspected 2026-11-10. Labels: **VERIFIED** = read in source code or measured; **REPORTED** = a README, doc comment or
another person's measurement, not re-checked here; **UNKNOWN** = no source settles it.

Sources (local clones; paths below are relative to each):
- fm1-base `b0976f2` (github.com/charlesvestal/fm1-base, MPL-2.0) — this repository's `base/` submodule; its
  `bsp/` is felucca-bsp `5b0879b`.
- The platform study `research/fm1/FM1_PLATFORM.md` of the TONIC project (`../digitone`, 2026-10-23), which read
  Felucca `63f88ec`, felucca-bsp `0f08fca`, M-VAVE's fm1_open `4c7496d`, fm1-emu `72adb81` and others. Where this
  page repeats it, the label is the study's.
- fm1-emu `5c6ed85` (github.com/joakimeriksson/fm1-emu), used here to run the JUNGLE OS device binaries.

## Processor and memory

| Fact | Value | Label | Where |
|---|---|---|---|
| SoC | JieLi AC791N family (AC7911B), pi32v2 CPU, 2 cores, single-precision FPU | REPORTED (PCB marking), dual core VERIFIED in BSP start code | FM1_PLATFORM.md §1 |
| CPU clock | 240 or 360 MHz — sources disagree; never measured on a unit | **UNKNOWN** | `base/include/plat.h` (`plat_cpu_khz`: "360000 when its registers hold the stock set-up") |
| No `double` | fm1-base's build rejects soft-double routines | VERIFIED (our builds pass the check) | `base/tools/build.py` |
| CPU erratum | inlined 32×32→64-bit multiply beside 16-bit loads can corrupt a register | REPORTED (fm1-radio), checked by the build | felucca-bsp `docs/CPU.md`; JUNGLE OS has no 64-bit multiply |
| SRAM | 512 KiB internal, no external RAM. fm1-base map: RAM code 32 KiB, `.data/.bss` 96 KiB, pool 336 KiB | VERIFIED | `base/device/app.ld` |
| Flash | 1 MiB SPI NOR; code executes in place (XIP) through a cache; an erase/write stalls XIP, so audio stops briefly | VERIFIED | `base/include/plat.h` (data, store) |
| App slot | 581,564 B | VERIFIED | FM1_PLATFORM.md §3 |
| Data area | 0x97000..0xDFFFF for store objects (A/B sector pairs, ≤ 3,840 B each) and app data | VERIFIED | `plat.h` `PLAT_DATA_LO/HI`, `plat_store_*` |

## Audio

| Fact | Value | Label | Where |
|---|---|---|---|
| Path | I2S (ALNK0) to an external codec, 24-bit left-justified stereo | VERIFIED | felucca-bsp `boards/fm1/fm1_audio.h` |
| Codec part | not identified | UNKNOWN | — |
| Sample rate | 24 MHz / 544 = **44,117.6 Hz** | VERIFIED | `base/device/main_fm1.c` |
| Outputs | stereo, headphones and speaker after the MASTER pot | REPORTED | fm1-base README |
| **Audio input** | **none**: no line or mic input. The only audio *into* an FM-1 is USB audio from a computer (fm1-base `usb_audio_out`, the computer plays through the FM-1; PIGEON records from such a stream) | VERIFIED (fm1-base options) | `base/README.md` "Optional parts" |
| Render contract | `app_render(int32 *lr, n, master_q12)` in the audio interrupt; `plat_cpu_pct` load, xrun count, `APP_OVERLOAD` above 90 % | VERIFIED | `plat.h` |

Consequence: **live external sampling is not possible on the device.** JUNGLE OS prepares samples offline
(`tools/sample-prep`) and loads them over USB-MIDI SysEx. Resampling the FM-1's own output (an internal bounce)
would be possible in principle but is not implemented (see the compatibility matrix).

## Controls and display

| Fact | Value | Label | Where |
|---|---|---|---|
| Buttons | 14: FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC, OCT-, OCT+; every one has an LED; PLAY has a second (green) LED | VERIFIED | `plat.h` `B_*`; felucca-bsp `fm1_input.h` |
| Encoders | 7 detented: SELECT, ALGO, PRESET, K1..K4. **No push switches** | VERIFIED | `plat.h` `EN_*`; `fm1_input.h` keymap |
| Keys | 27 (F3..G5): 16 white, 11 black, each with an LED. Velocity (and pressure on 20 of them) optional via `APP_TOUCH` | VERIFIED | `plat.h`, `base/lib/touch.h` |
| MASTER | potentiometer, 0..4096, given to the render as a gain | VERIFIED | `plat.h` |
| Display | 240×240 ST7789-class IPS, RGB565, SPI; ~21 ms per full frame at ~44 MHz effective; no tear-effect line | VERIFIED (code) / REPORTED (timing) | `base/device/lcd.c` |
| Battery | Li-Po, read on ADC 3 | REPORTED | fm1-custom-fw RE notes |

There are **no pads, no 16-step pad grid and no push encoders**. JUNGLE OS's control maps use only the controls
above (the manuals: firmware/break-surgeon/docs/MANUAL.md, firmware/abyss-engine/docs/MANUAL.md).

## MIDI and links

| Fact | Value | Label | Where |
|---|---|---|---|
| USB | full-speed USB-C device: USB-MIDI in and out (+ USB audio) | VERIFIED | `plat.h` |
| TRS MIDI | **input only** (UART1 RX, 31,250 baud). No MIDI out jack | VERIFIED | `plat.h` comment, felucca-bsp `fm1_uart.h` |
| USB host | fm1-base `usb_host = true`: the port hosts a USB-MIDI device directly or behind a powered hub; it gives **no bus power**. Built into both JUNGLE OS firmwares | VERIFIED (code); hosting **another FM-1** is **UNVERIFIED** | `plat.h` `plat_usb_host*` |
| Bluetooth MIDI | possible (fm1-base `ble = true`, JieLi libraries or fm1-base's own stack); TONIC 0.28 reports one FM-1 connecting to another as a BLE MIDI central | REPORTED | fm1-base README; TONIC README |
| MIDI time stamps | `midi_stamp = true`: USB and TRS messages stamped within 0.05 ms of arrival | VERIFIED (code) | `plat.h` `plat_midi_in_t24` |

What this means for two devices (docs/architecture/link-protocol.md):
1. **USB host link** (candidate, unverified): Break Surgeon's port as host (SYSTEM page, or `plat_usb_host(1)`),
   an OTG cable to Abyss Engine. Both FM-1s run on battery, so the missing bus power may not matter. Must be tried on
   hardware before it is called working.
2. **Through a computer or a USB-MIDI host box** routing Break Surgeon's USB MIDI out to Abyss Engine's USB in:
   standard, expected to work, adds the router's latency.
3. **USB-MIDI → DIN/TRS adapter** into Abyss Engine's TRS input: one-way master → slave, which is all the link
   needs at minimum.
4. **BLE MIDI** (not built yet: no `ble` in the JUNGLE OS fm1.toml files). Reported to work between FM-1s; adds
   connection-interval jitter (7.5–30 ms), which the clock follower's time stamps would have to absorb.

## Firmware installation and recovery

| Fact | Label | Where |
|---|---|---|
| Packages are `.fwsc` files (fm1-base `tools/fm1pkg_make.py`) installed over USB-MIDI SysEx by `base/tools/fm1_install.py` or fm1-base's web installer; M-VAVE's own package (e.g. `FM-1_015.fwsc`) goes back the same way | REPORTED (fm1-base / TONIC docs; TONIC has installed this way on hardware) | `base/tools/fm1_install.py`, `../digitone/docs/INSTALL.md` |
| The update loader writes only the app area 0x04000..0x92FFF; the boot head, calibration (`key_mac` 0xFF000) are never written | VERIFIED (loader source) | felucca-bsp `loader/ldr_core.c` |
| A soft UBOOT entry exists (`F0 22 24 35 7D F7`) | VERIFIED (code) | felucca-bsp `chip/usb.c` |

JUNGLE OS has **not** been installed on a physical FM-1. docs/development/install-and-recovery.md has the plan,
which needs your approval before anything is flashed.
