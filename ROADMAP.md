# Roadmap

Ordered by value; the hardware items gate everything after them.

## Next (needs a physical FM-1 and your go-ahead)
1. Back up a unit, install Break Surgeon, run the hardware tests (docs/development/testing.md §hardware): load,
   dropouts, save/restore, a bank over SysEx.
2. The same for Abyss Engine with everything playing; record real loads in docs/development/profiling.md.
3. If loads are high: the per-sample loops into RAM code (`PLAT_RAM_CODE`), then automatic shedding.
4. The link between two units: USB host (OTG), via a computer, via a USB-MIDI→TRS adapter; measure offset and jitter.
5. Listening: certify sweetspot nodes; correct the RMS trims by ear.

## Then
- Break Surgeon: 64-step patterns (two store objects or a pooled store), pattern chains, A/B snapshots and
  multi-level undo, internal bounce (RAM permitting), pattern capture; IMA ADPCM banks (2× capacity) with
  slice checkpoints; per-slice gain/trim editing on the device.
- Abyss Engine: PHASE MUTANT and COMB ORGANISM models; steps 17–32 editable; user pitch sets; LFO-to-pad
  routing; stab rhythm generator; automatic voice shedding.
- Both: fm1-base `backup = true` (save/restore over USB), Bluetooth MIDI for the link, a patch/graph editor (JSON export of the nodes, editing, re-import), published words files.
- Second core (fm1-base `cpu2`), only if loads require it.
