# Drum & bass bass archetypes: how they are made and how they are played

Research notes behind ABYSS ENGINE 0.7's sounds and STYLES. What is sourced is linked; the rest is common genre
practice as producers' tutorials teach it, written down here in our words. The lines in the firmware are genre-
typical forms written for it, not any record's riff. Nothing here was checked by ear by the author of these notes:
the sounds are hypotheses until the listening checklist (docs/development/sweetspots.md) passes them.

How we researched: web searches and articles below, and the chapter lists and descriptions of STRANJAH's YouTube
tutorials (the videos themselves could not be watched or transcribed from here).

## The common ground

- **Sub and mid apart.** The sub is a clean sine, mono, kept out of distortion; the "mid bass" carries the
  character above ~100 Hz and takes the distortion, the width and the movement. Sources: Attack Magazine's sub
  processing (low-pass the sub around 80 Hz, never lose its low end to drive) and the rolling band at 80–250 Hz
  ([Attack Magazine](https://www.attackmagazine.com/?p=109053)); Stranjah's "10 Serum 2 Tricks" chapters (sub
  oscillator, bus routing for a clean low end, multiband compression, a channel splitter for low / high processing:
  [video](https://www.youtube.com/watch?v=cwbh4ykjP8U)). In ABYSS ENGINE: the sub voice and the master's 115 Hz
  split (since 0.7 the mid-bass above ~250 Hz gets width into the side only).
- **The bass gets out of the kick's way** (ducking / sidechain): Attack Magazine, as above; Stranjah "How I
  Sidechain Breaks" ([video](https://www.youtube.com/watch?v=gg7hS0BU6po)). In ABYSS ENGINE: the link's kick ducking,
  DUET's POCKET.
- **Two patterns that alternate** keep a bassline from repeating: a jump-up tutorial switches between two patterns
  ([Loopmasters](https://www.loopmasters.com/articles/4480-How-To-Get-the-Perfect-Jump-Up-Bass-A-D-B-Bass-Tutorial));
  a neurofunk tutorial layers a growl that fills the main bass's gaps
  ([Native Instruments](https://blog.native-instruments.com/watch-how-to-make-neurofunk-drum-and-bass)). In ABYSS
  ENGINE: the FORM and its answers B C D.

## The archetypes

### REESE (rolling / classic)
- **Origin:** Kevin Saunderson's "Just Want Another Chance" (as Reese, 1988), into jungle when Ray Keith sampled it
  for "Terrorist" (1994) ([eMastered](https://emastered.com/blog/reese-bass), [Splice](https://splice.com/blog/?p=12971),
  [MusicRadar](https://www.musicradar.com/how-to/classic-reese-bass-renegade-terrorist)).
- **Design:** two or more detuned saws; the beating between them, the phase movement, is the sound (a single stack
  without it is not a Reese); low-pass with an envelope or LFO on the cutoff; chorus / flanger; played mono
  ([eMastered](https://emastered.com/blog/reese-bass), [LANDR](https://blog.landr.com/reese-bass),
  [Attack Magazine](https://www.attackmagazine.com/technique/tutorials/reese-bass-redux/)). Stranjah: make it
  pulse with the beat, phase movement, distortion, filtering, width, layering
  ([video](https://www.youtube.com/watch?v=AqzMLafeNCE)).
- **Use:** long notes under the break, one or two notes a bar, slow slides between root, b7, b6, b3; the beating
  gives the movement, so the rhythm stays simple. Rollers: the same notes rolling for 16 or 32 bars.
- **ABYSS ENGINE:** model REESE (0.7.1: two free-running saws 3–30 cents apart, their detune drifting slowly, a saw
  an octave below; 0.7's four saws sounded like a chorus, not a Reese); sounds ROLLER,
  REESE, REESE DARK; style ROLLING REESE.

### HOOVER / dark techstep Reese
- **Origin and design:** the Roland Alpha Juno's "WhatThe" preset, named for its pitch modulation; PWM pulses,
  chorus, a pitch envelope of several semitones ([MusicRadar](https://musicradar.com/how-to/how-to-create-a-classic-90s-hoover-sound),
  [Bonedo](https://www.bonedo.de/artikel/origins-of-sound-der-hoover)). Stranjah's Doc Scott "Unofficial Ghost"
  chapters: PWM, a filter around 1700 Hz, the "Dominator" pitch trick
  ([video](https://www.youtube.com/watch?v=lysxHbtQxRc)).
- **Use:** long, bending notes in dark minor (the b6 and the b2 give the tension), half-bar to two-bar lengths,
  gaps between them in techstep.
- **The patch (0.7.1):** Eric Persing's Alpha Juno "What The": PWM sawtooths an octave apart, the pulse width
  stirred by a fast LFO, a wide, deep chorus ("harder than feels polite"), a resonant low-pass, and a pitch envelope
  that starts sharp and dives onto the note ([Bonedo](https://www.bonedo.de/artikel/origins-of-sound-der-hoover/),
  [Music Production Wiki](https://musicproductionwiki.com/articles/how-to-recreate-the-hoover-sound)). No source
  gives numbers; ours are a first guess.
- **ABYSS ENGINE:** model HOOVER (0.7.1, its own: PWM saws on three octaves, PWM at 1.5–6 Hz, a two-tap chorus,
  PITCH +5 diving in ~0.1 s; in 0.7 it was the REESE model with pulses and a bend from below); sounds HOOVER, REESE DARK; styles
  HOOVER, DARK TECHSTEP.

### NEURO GROWL and the switch-up
- **Design:** FM (popular for growls), filter and resonance moved by an LFO, drive, then resampling; heavily
  processed, distorted, swooping ([MusicRadar](https://www.musicradar.com/tuition/tech/how-to-create-a-neurofunk-bass-sound-in-xfer-records-serum-640465),
  [Dubspot](https://blog.dubspot.com/sound-design-tutorial-native-instruments-komplete-fm8),
  [Native Instruments](https://blog.native-instruments.com/watch-how-to-make-neurofunk-drum-and-bass)).
- **Use:** short 1/16 and 1/8 hits with gaps between, the sound changing from bar to bar (the switch-up; Stranjah's
  "Peep Show" chapters: "Modern Reese Bass Switch-Up": [video](https://www.youtube.com/watch?v=750xP4NWFFQ)), a growl
  filling another bass's gaps.
- **ABYSS ENGINE:** model FM; sounds GROWL, SNARL, METALLIC, BROKEN; style NEURO SWITCH (FORM A B C D: a new sound
  every bar).

### TALKING (formant) bass
- **Design:** formant filters that move between vowels (Stranjah, "Formant Filters for Talking Basslines":
  [video](https://www.youtube.com/watch?v=cwbh4ykjP8U)); Optical-style "dirty talking bass" ([video](https://www.youtube.com/watch?v=ZG7VORXUJxw)).
- **Use:** slides on the offbeats so the vowels move while the pitch moves.
- **ABYSS ENGINE:** sound TALK; style TALKER.

### WOMP and WUB (jump-up)
- **Design:** an oscillator into a low-pass with a synced LFO (Stranjah's womp chapters: oscillators, low-pass, LFO,
  FX, a sub: [video](https://www.youtube.com/watch?v=ne_nJI11WrU)); a slow-rising envelope on filter and FM gives the
  wub, resonance for grit ([Loopmasters](https://www.loopmasters.com/articles/4480-How-To-Get-the-Perfect-Jump-Up-Bass-A-D-B-Bass-Tutorial));
  a phaser for the nasal jump-up tone (Stranjah, "Phaser Filters for Nasal Jump-Up Bass").
- **Use:** big one- or two-note hooks in the midrange, often dancehall-like melodies
  ([Loopmasters](https://www.loopmasters.com/articles/4480-How-To-Get-the-Perfect-Jump-Up-Bass-A-D-B-Bass-Tutorial));
  the womp bounces on the offbeats with octave jumps; the wub holds notes and lets the LFO play the rhythm.
- **ABYSS ENGINE:** model WOMP (the "wom": filter and vowel open and close on every note); sounds WOMP, WUB; styles
  JUMP-UP WOMP, WOBBLE.

### FOGHORN
- **Design (0.7.1):** a very low note (Attack Magazine plays F0) of a saw halfway to a square, FM from two octaves
  up at about 30 %, three noisy unison voices, a 24 dB low-pass at about 200 Hz that opens on every note (23 ms)
  and closes over seconds, then heavy multiband distortion, the lows cut and a sine sub on its own track: "a huge
  blaaat that cuts through the mix" ([Attack Magazine](https://attackmagazine.com/?p=99684)). Stranjah's "Top 10
  Bass Sounds" covers the classic jump-up basses: [video](https://www.youtube.com/watch?v=CqFnOsymyNk).
- **Use:** one long note, then space for the drums; the next one lower.
- **ABYSS ENGINE:** model HORN (0.7.1: an octave below the note, three unison saw-squares, FM, a 24 dB low-pass
  from 160 Hz opening to ~1.4 kHz, two drive stages); sound FOGHORN; style FOGHORN.

### LIQUID ROLLER
- **Design:** a warm, lightly saturated mid over the sub ([Soundbridge](https://www.soundbridge.io/liquid-grooves),
  [EDMProd](https://www.edmprod.com/how-to-make-liquid-drum-and-bass/)).
- **Use:** melodic, legato 8ths through the chord tones, following the harmony rather than only the kick.
- **ABYSS ENGINE:** sound ROLLER; style LIQUID ROLLER.

### 808 / kick-sub (dancefloor, half-time)
- **Design:** a sine with drive and a pitch drop at the onset (genre practice; Stranjah's "Ultimate 808 Guide for DnB":
  [video](https://www.youtube.com/watch?v=6bEKiX9rPSo)).
- **Use:** half-time hits, glides up into the next phrase.
- **ABYSS ENGINE:** model SINE with PITCH (the sub drops too); sound 808; style 808 HALF-TIME.

### LASER / ZAP
- **Design:** a fast pitch drop over a bright FM tone.
- **Use:** short zaps between the snares, answers to a growl.
- **ABYSS ENGINE:** sound LASER; style LASER ZAPS.

### 90s JUNGLE DUB SUB
- **Design:** a deep sine-ish mid with drive (dub soundsystem bass, genre practice).
- **Use:** root and octave in a skank, much space for the break.
- **ABYSS ENGINE:** sound DUB; style JUNGLE DUB.

### RUFIGE / techstep SAW STAB
- **Design:** a short, driven saw stab (Stranjah's Rufige-style tutorial:
  [video](https://www.youtube.com/watch?v=OWGKzArdDQE); techstep: [video](https://www.youtube.com/watch?v=BJx3UhfFNt4)).
- **Use:** short stabs on the 2-step's kicks.
- **ABYSS ENGINE:** sound TECH STAB; style RUFIGE STAB.

## Gaps

- No source covered the 808's glide in drum & bass or the foghorn's design in detail; those two follow common
  practice.
- The rhythmic side is the least documented in writing; the styles' lines are our genre-typical forms.
