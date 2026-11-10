# Profiling

## What is known (2026-11-10)

| Measurement | Break Surgeon | Abyss Engine | Kind |
|---|---|---|---|
| The firmware's own load readout (`plat_cpu_pct`, header), in fm1-emu at 240 MHz | 8–16 % playing (fill, GLITCH held) | 28 % (bass + sub + pad), 38 % with a stab chord on top | **lower bound**: fm1-emu counts one cycle per instruction, no cache misses, no flash wait states |
| Whole-system share of non-idle cycles in fm1-emu | 92–93 % | 92–94 % | not a load: the main loop polls and redraws continuously |
| Host CPU per 128-frame block (Mac, -O2) | ~10 µs | 18–34 µs | relative only; the budget is 2,901 µs of audio |
| On a physical FM-1 | — | — | **PENDING** |

Runs:

```sh
LIVE=path/to/fm1-emu/target/release/fm1-live
FM1_BENCH_KEYS='2000:PLAY,2100:PLAY^,3000:ENV,3100:ENV^,4500:n7' FM1_BENCH_PANEL=bs.ppm \
  $LIVE --fwsc firmware/break-surgeon/build/breaksurgeon.fwsc --bench 7000
FM1_BENCH_KEYS='2000:PLAY,2100:PLAY^,2200:REC,2300:REC^,2400:ENV,2500:ENV^,6800:n7' FM1_BENCH_PANEL=ab.ppm \
  $LIVE --fwsc firmware/abyss-engine/build/abyssengine.fwsc --bench 7000
```

The load is read off the panel image's header (the `%` at the right). Key ids: buttons by name, `n0`..`n26` the keys.

## Why real loads will be higher, and what to do

All engine code runs from flash through the XIP cache (`.ram_text` holds only fm1-base's own 5 KB). Cache misses
and flash wait states are not in fm1-emu. If the device's load is too high:
1. Move the per-sample loops into RAM (`PLAT_RAM_CODE`, 27 KB of RAMTEXT free): `jd_render`, `jb_render`,
   `jn_bass_render`, `jn_stab_render`, `jn_pad_render`, `jf_*_process`. The math is already always-inline so this
   is possible; the build's RAM-code check will confirm.
2. Abyss: control-rate the pad's and stab's filter coefficients further, or drop to 3 pad voices; use VOICES modes.
3. Automatic shedding in `APP_OVERLOAD` (priority order in docs/architecture/overview.md).
4. The second core (fm1-base `cpu2`), last: it needs the supply-voltage fix and job checks upstream firmwares
   describe.

On the device, record here: the SYSTEM page's CPU with everything playing, XRUNS after 10 minutes, and the clock
(`plat_cpu_khz`, built in with `APP_CPU_KHZ`).
