# Control-map design

The FM-1 has 14 named buttons, 7 detented encoders without push, 16 white + 11 black keys with LEDs, MASTER, and a
240×240 screen (docs/research/hardware.md). Two control maps were considered; B is implemented.

## Variant A: held-button layers (TONIC / Hortator style)

Every button is a layer while held (hold FX: the knobs become effects, the keys become performance FX; hold SEQ:
step tools...), a tap opens its page.
- + very deep, one hand on a button, the other on knobs/keys
- − two-handed for most live actions; a forgotten hold changes what every key does; at 174 BPM a held layer
  competes with holding performance-mode keys (also holds): two holds at once for core gestures

## Variant B: pages by tap, one key = one action on PERFORM (implemented)

A tap on a button selects a page (HOME, SEQ, EDIT, FX, ARP, GLO); each page gives every key and knob one meaning;
SEL and SAVE are the only modifiers. The PERFORM page puts every essential live action under a single key or knob:
performance modes on held white keys, mutes, fills, scenes and undo on single presses, the four graph/macro knobs.
- + no essential live action needs more than one layer (PERFORM) and at most one modifier (SEL+PLAY panic, SEL
  for solo, SAVE+key to store — two hands on purpose, never by accident)
- + destructive actions are two-handed or reversible: storing a scene needs SAVE held; mutation has UNDO; RETURN
  and SNAP are always one press away; SAVE is refused while playing
- − editing depth lives on other pages (SEQUENCE, SOUND), one tap away

**0.2: one map for both, and the map on the screen.** Testers found both firmwares cryptic: the two used the same
buttons for different things (REC was RETURN on one and PAD HOLD on the other, ENV a fill or the keyboard's part),
and nothing on the screen said what a key did. Since 0.2:
- the buttons mean the same on both (PLAY, REC = RETURN, ENV = FILL, LFO = MUTATE with SEL + LFO = UNDO, SEL +
  white = scene, SAVE + white = store a scene, SEL + black = mute, SEL + PLAY = panic), and on PERFORM ALGO steps
  through the sweetspots and PRESET snaps onto one on both;
- a strip above the knob boxes (shared/ui/jo_ui.c `ui_strip`) shows what the three encoders and every run of keys
  do on the page, and switches to the SEL or SAVE layer while that button is held; with SEL held the page lists the
  SEL button combinations;
- the header shows a countdown bar while a change waits for its bar.

Evaluated against the hardware: the black keys (11) carry the discrete live choices (scenes, fills, undo) where
fingers rest naturally between white-key holds; LEDs show state on the keys themselves (mutes lit = playing, the
current scene lit, the queued one dim), so the screen is not needed mid-gesture.

## A 174 BPM rehearsal (in the simulator)

`tests/host/bs_smoke.txt` and `ab_smoke.txt` play through the main gestures (fill, held mode, RETURN, step edit
with a held key, scene queue, save refused while playing, save, reboot). A hands-on rehearsal on hardware without
a computer is **pending** (testing.md).
