# Third-party notices

## In the firmware

| Component | Where | Upstream | Commit | Licence | Copyright | Modified |
|---|---|---|---|---|---|---|
| fm1-base (platform layer, build tools, host simulator, installer) | `base/` (git submodule) | https://github.com/charlesvestal/fm1-base | `b0976f295ccaeaf77a8053a5a785a9cc21f5345f` | MPL-2.0 (`base/LICENSE`, `base/LICENSING.md`) | Charles Vestal; Leo Kuroshita (Hügelton Instruments) and contributors | no |
| felucca-bsp (board support) | `base/bsp/` (fm1-base's submodule) | https://github.com/hugelton/felucca-bsp | `5b0879bfadc69a17dbebb917fb6b86d516f19c44` | MPL-2.0 | Leo Kuroshita and contributors | no |
| Barlow Semi Condensed, Terminus fonts | rendered at build time from `base/assets/fonts/` | (via fm1-base) | — | SIL OFL 1.1 | The Barlow Project Authors; Dimitar Toshkov Zhekov | no |
| JieLi AC79 SDK files `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` | put in each `.fwsc` package by fm1-base's build, read from your SDK checkout | https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK (AC79NN_SDK_V1.2.1_2023-12-13) | — | Apache-2.0 | Zhuhai Jieli Technology Co., Ltd. | no |

No JieLi binary library (Bluetooth, WiFi) is linked: the builds have neither.

All files under `shared/`, `firmware/`, `tests/`, `tools/` and `docs/` are original JUNGLE OS work, MPL-2.0: the
source files each carry an `SPDX-License-Identifier` header; the documents are under the repository's licence.

## Studied, not copied

These were read for facts, measurements and design ideas (recorded with file:line references in
docs/research/upstream-audit.md). **No code, data, presets or audio from them is in JUNGLE OS.** They are
GPL-3.0-only, which JUNGLE OS deliberately does not take on so that its licence stays MPL-2.0.

| Project | Upstream | Commit | Licence |
|---|---|---|---|
| SLOOP | https://github.com/isod89/sloop-fm1 | `bac9c05` | GPL-3.0-only |
| X0X | https://github.com/charlesvestal/fm1-x0x | `a0d1a13` | GPL-3.0-only |
| Felucca 1.6.1 | https://github.com/hugelton/Felucca | `63f88ec` | GPL-3.0-only (engines MIT, sample pack CC0) |
| PIGEON | https://github.com/TheShrimpBlimp/pigeon | `036fb26` | GPL-3.0-only |
| sloopDX | https://github.com/zvenson/dxsloop | `8c1035b` | GPL-3.0-only |
| TONIC | https://github.com/RahelPuest/tonic-fm1 | `64634b6` | MPL-2.0 (its structure was a model; no file copied) |

## Test tools (not part of the firmware)

| Tool | Upstream | Licence |
|---|---|---|
| fm1-emu (instruction-level FM-1 emulator, used for CPU lower bounds) | https://github.com/joakimeriksson/fm1-emu `5c6ed85` | see its LICENSE |
| numpy (sample prep, analysis) | https://numpy.org | BSD-3-Clause |
| mido, python-rtmidi (bank transfer) | PyPI | MIT |

## Technique references

Published methods the DSP follows are listed in docs/development/sweetspots.md (Chowning 1973; Välimäki &
Huovilainen 2007; Zavalishin; Jot & Chaigne 1991; Carson's rule).
