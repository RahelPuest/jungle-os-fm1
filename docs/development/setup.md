# Building JUNGLE OS

Tested on macOS 14 (Apple silicon), 2026-11-10.

## Prerequisites

| What | For | How |
|---|---|---|
| a C compiler (`cc`), make | the engine tests and renders | Xcode command line tools |
| Python 3.10+ with numpy | sample prep, analysis | `pip3 install numpy` |
| Pillow, fontTools | fm1-base renders its fonts at build time (host and device builds) | `pip3 install Pillow fonttools` |
| libraqm / libfribidi | the font generator uses OpenType features | Linux: `apt install libfribidi0`. macOS: a libfribidi in `~/.local/fribidi` or Homebrew's `fribidi`; `tools/env.sh` points `DYLD_LIBRARY_PATH` at it |
| Emscripten (optional) | the browser emulator and the site | `~/emsdk` (emsdk), picked up by `tools/env.sh` |
| Docker (macOS) | the JieLi toolchain is Linux x86-64; fm1-base runs it in a `linux/amd64` container | Docker Desktop, running |
| JieLi toolchain | the device builds | `base/tools/get_toolchain.sh` (→ `~/.jieli/toolchain`; this machine: `jieli-linux-toolchains-20260730.1`) |
| JieLi AC79 SDK | three Apache-2.0 files in the package | `git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK` |
| mido, python-rtmidi | sending a bank to a real FM-1 | `pip3 install mido python-rtmidi` |
| fm1-emu (optional) | CPU lower bounds on the device binary | github.com/joakimeriksson/fm1-emu, `cargo build --release` |

```sh
git clone --recursive https://github.com/RahelPuest/jungle-os-fm1 jungle_os && cd jungle_os   # base/ = fm1-base b0976f2
. tools/env.sh        # JIELI_TOOLCHAIN, AC79_SDK, libfribidi, Emscripten
```

## Build

```sh
make -C tests                 # the engine tests (124 checks)
make -C tests renders         # the audio-regression renders + golden hashes (WAVs in tests/build/renders)
python3 tests/tools/test_sample_prep.py
tools/build_all.sh            # both firmwares: host simulators, device packages, host scripts
tools/make_site.sh 0.3.0      # release packages (release/), both browser emulators and the site (release/site)
```

`release/site` is committed; `.github/workflows/pages.yml` publishes it to GitHub Pages
(https://rahelpuest.github.io/jungle-os-fm1/) on every push to main that changes it.

One firmware by hand (from its directory; the app must live inside this repository, because fm1-base's build
mounts the common root of the app and `base/` into the toolchain container):

```sh
cd firmware/break-surgeon
python3 ../../base/tools/build.py . --host    # build/host/breaksurgeon_host (the simulator)
python3 ../../base/tools/build.py .           # build/breaksurgeon.fwsc (package identity FM-1_8300)
python3 ../../base/tools/build.py . --release 0.1   # a release build (needs install/repo/screenshot in fm1.toml)
```

Abyss Engine is the same in `firmware/abyss-engine` (`abyssengine.fwsc`, FM-1_8400). The device build prints the
memory budget and runs fm1-base's checks (no soft-double, no erratum-prone 64-bit multiply, register access only
through felucca-bsp, RAM-code calls).

## fm1-base settings worth knowing

- `id_prefix`: digits only after `FM-1_`. JUNGLE OS uses 83 and 84; collisions with other community firmwares
  were not checked (the ids decide which installed firmware an update replaces).
- `words.tsv` (screen reader) needs a `# url:` line; it points at the words files on the site
  (`https://rahelpuest.github.io/jungle-os-fm1/<firmware>/words/`).
- `tags` come from fm1-base's vocabulary (docs/FIRMWARE-JSON.md in base/).
- The host simulator stamps MIDI with the wall clock while it runs ~200× faster than real time: the apps use
  simulated time for MIDI stamps under `PLAT_HOST` (see `midi_t24` in the app.c files).

## Repository layout

| Path | |
|---|---|
| `base/` | fm1-base (submodule, unmodified) |
| `shared/` | the engines both firmwares use (dsp, seq, drums, sampler, synth, fx, sweetspot, link, store, ui) |
| `firmware/break-surgeon/`, `firmware/abyss-engine/` | each: `fm1.toml`, `app.h`, `app.c` (UI, storage, MIDI), `dsp_o2.c` (the -O2 unit), `engine/` |
| `tests/` | `unit/`, `integration/`, `audio-regression/` (renders, golden hashes, analysis), `host/` (simulator scripts), `tools/` |
| `tools/sample-prep/` | bank builder and SysEx sender |
| `firmware/*/docs/MANUAL.md`, `firmware/*/site/` | each firmware's manual (and its screenshots) and its front page on the site |
| `site/landing.md` | JUNGLE OS's front page on the site |
| `release/` | release packages, SHA256SUMS and the generated site (`tools/make_site.sh`) |
| `docs/` | research, architecture, the live-set guide, development |
