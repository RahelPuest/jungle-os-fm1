# Installing on an FM-1, and getting back

> **Nothing here has been done yet.** No JUNGLE OS build has been installed on a physical FM-1. The tools are
> fm1-base's, which other firmwares (CTL-1, Radio, TONIC) install with; JUNGLE OS adds nothing to the update path.
> Flashing a device needs your explicit go-ahead, step by step.

## Risks

- A custom firmware replaces M-VAVE's. The update loader writes only the app area (0x04000..0x92FFF); the boot
  loader, the calibration and the unit key are never written (felucca-bsp `loader/ldr_core.c`), so the unit stays
  recoverable through the same update channel in the normal case.
- Installing JUNGLE OS loses nothing of M-VAVE's firmware itself, but **its user patches** live in flash areas
  JUNGLE OS uses for its data (0x97000..0xDFFFF): back them up first.
- A firmware that crashes at boot: fm1-base has a safe mode after repeated crashes and the update stays reachable;
  the soft UBOOT entry (`F0 22 24 35 7D F7`) and M-VAVE's own recovery remain (see ../digitone/docs/INSTALL.md's
  recovery routes, written for fm1-base firmwares).
- Two units with different hardware revisions or flash chips: fm1-base's flash allow-list covers the known chips
  (P25Q80H, UC25HQ80).

## Plan

1. **Back up** the whole flash of each unit: `python3 base/tools/fm1_backup.py` / fm1-base's installer page offers
   a backup first (read its README; the exact command is fm1-base's).
2. Keep M-VAVE's package for the unit (e.g. `FM-1_015.fwsc`) at hand: installing it the same way goes back.
3. Install: `pip3 install mido python-rtmidi && python3 base/tools/fm1_install.py firmware/break-surgeon/build/breaksurgeon.fwsc`
   (or fm1-base's web installer). The package identity is FM-1_8300 (Abyss FM-1_8400).
4. First boot: the SYSTEM page says "STORE DEFAULTS USED" (nothing saved yet). Run the hardware tests in
   testing.md in order.
5. **Back to stock**: `python3 base/tools/fm1_install.py FM-1_015.fwsc` (M-VAVE's package), then restore the
   backup if the stock patches are wanted.

## Rollback between JUNGLE OS versions

Saved records carry a version; a newer firmware reads older records or falls back to defaults (never a crash).
An older firmware given newer records falls back to defaults: back up first (fm1-base's backup is not built into
JUNGLE OS yet — `backup = true` is on the roadmap).
