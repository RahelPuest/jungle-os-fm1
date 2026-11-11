# Drum & bass drums: how they are made to sound good, and the classic patterns

Research notes behind Break Surgeon 0.7's kit, bus, built-in breaks and grooves. Sources are listed at the end.
Where a source gave only a chapter title or a summary, that is said; nothing here was taken from listening, since
the work was done without listening. Patterns are rhythms (not protected); no recording is used anywhere in JUNGLE
OS.

## The kick

What the sources agree on:
- **Three parts: transient, body (the fundamental) and texture.** STRANJAH's "10 Tips to Make Your DnB Kicks Hit
  Harder" is organised around exactly that anatomy: choose the kick, transient / fundamental / texture, get the
  length right, shape the fundamental, tuning, the transient, pitch modulation for punch, texture, saturation for
  harmonics, compression, layering hats and noise, sidechain (chapter list only; the video itself was not
  transcribed). The synthesis guides describe the same split: body, pitch sweep, transient.
- **The body is a sine with a fast pitch envelope**, starting high and dropping quickly; a base around 50-60 Hz
  is typical. The envelope gives the punch, the attack stays at zero.
- **Short in drum & bass:** the kick is short and transient-heavy, so its exact pitch matters less than in house
  and techno; its length leaves room for the sub (here Abyss Engine's).
- **The click** is a second, shorter layer (a tight sine blip or a sampled click) on top of the body.
- **Saturation adds harmonics** so a 50 Hz kick is heard on small speakers; compression shapes its punch.

What Break Surgeon 0.7 does (shared/drums/jo_drums.c, `JD_KICK`):
- body: a sine at PITCH (45-72 Hz, 54 Hz by default) with two pitch stages, a fast punch (3-9 x the pitch over
  2.5-12 ms) and a slower knock (+100-220 % over ~70 ms, the 80-150 Hz thump), held at full level for 8-35 ms
  before it decays (WEIGHT);
- transient: a 1.8-4 kHz blip and a noise tick, with a 0.25 ms onset so a retrigger does not click;
- texture: an asymmetric saturation (tanh plus an even term: the second harmonic), its DC removed.
- Measured on the default hit (scratch harness, first 100 ms, Goertzel bands relative to the loudest): 0.6's kick
  had its 110 and 220 Hz bands level with 55 Hz (a long, boomy sweep) and the 3 kHz click at -36 dB; 0.7's has
  110 Hz at -6, 220 Hz at -10, 500 Hz at -19 (0.6: -18), the click at -36 dB (with its 0.25 ms onset), the pitch
  at 56 Hz, the length to -40 dB about 130 ms (0.6: 131 ms), RMS +1.5 dB. Whether this is better is for the ear to
  decide; it follows the sources' recipe more closely (a held body, a two-stage drop, saturation harmonics).

## The snare

- **Body** at roughly 120-220 Hz (or reinforced by a pitched-down kick low-passed around 300 Hz); **crack** high,
  6-9 kHz (a clap, a noise burst, filtered white noise); a **tail** of noise, often two noises of different bands
  and lengths, and reverb on the tail.
- **Claps** are layered on top; their transients must line up with the snare's or the hit sounds loose.
- Drum & bass producers stack many layers on a snare, and it is easy to overdo.

Break Surgeon 0.7 (`JD_SNARE`, `JD_GHOST`): the tuned body (~190 Hz and a partial) with its pitch drop, the wires'
band, the stick crack, plus a **clap layer** (three bursts 7 ms apart, then a short tail, band-passed around
1.2-1.6 kHz, from the first sample, so it lines up) and an **air crack** (noise above ~6 kHz for 8 ms). The ghost
is the same drum played softly (no clap). Default hit: peak 0.56 → 0.76, RMS +2.9 dB, the 3 kHz band -25 → -18 dB.

## Hats, rides, shakers

- 16th hats carry the speed of drum & bass; open hats are choked by the next closed one; shakers and top loops
  sit over breaks. The classic drum machines make their metal from six square oscillators through band-pass
  filters.
- Break Surgeon 0.7 (`JD_HATS`): the six squares through two bands (around 7 and 10 kHz) plus high-passed noise;
  closed 25-110 ms (0.6: 12-90 ms), open five times longer; soft hits get a slower attack (a shaker's swell).
  Default closed hat: 16 → 30 ms, RMS +10 dB; open hat 72 → 142 ms, RMS +10.7 dB.

## The break, and the bus

- **The 90s chain: EQ → saturation → compression.** STRANJAH's "Why 90s Jungle Sounded Better" walks through that
  analog chain on breaks, sub and one-shots (chapter list). Tutorials recreate the overdriven crunch of 90s
  hardware with EQ (harsh treble shelved down), parallel distortion and compression on the drum group.
- **The sampler's grit:** 12 bits is the classic setting; lower adds heavier artifacts.
- **Compression:** parallel or multiband compression for punch and glue; gentle, so the drums are not flattened.
- **Layering a break over programmed drums:** chop it into kicks, snares and ghosts and rearrange; layer it under a
  programmed kick and snare. STRANJAH's "How I Sidechain Breaks in Jungle & DnB" covers sidechaining the break and
  mixing breaks against the sub (chapter list; no settings were given in text). "Make Any Break Sound Like Your
  Favourite Break" EQ-matches one break to another.
- **Pitching breaks:** the classic breaks were recorded at 90-140 BPM and pitched up to jungle tempo.

Break Surgeon 0.7:
- **DRUM PRESSURE** gains a console EQ ahead of its saturation: below ~90 Hz +1.5-4 dB with PRESSURE, 200-500 Hz
  -1.5-3 dB with GLUE (the box of a stacked kit), above ~4 kHz +1-3.5 dB with PUNCH; then the saturation and the
  fast glue compressor as before (jf_drum_*).
- **The break under programmed drums:** while the KICK track plays, the break's lows below ~70 Hz are cut (eased
  in and out), and the break ducks by up to -3.5 dB under every synthesized kick, released over ~70 ms.
- Measured on the A render (8.7 s of P1 DROP with a fill): RMS -15.6 → -14.4 dBFS, the 10 ms crest 8.7 → 7.4 dB
  (denser), the share above 4 kHz 6.8 → 10.4 %, below 150 Hz 35.9 → 36.3 %.

## Ghosts, swing, microtiming

- Ghost notes and the hats' placement carry the feel; drum & bass mostly sits straight (swing 50 %), rollers and
  liquid shuffle a little, the 90s breaks were played swung by real drummers.
- The built-in breaks are played with small velocity changes, ghosts late (+8 ms), lighter snares +4 ms, and
  their own swing (Funky Drummer 10 % of a 16th, Apache 6 %, Hot Pants 5 %, Think 3 %).

## The classic breaks (as rhythms)

Approximate grids after common transcriptions (16ths, one bar; K kick, S snare, s light snare, g ghost, h hat,
o open hat, r ride, R ride bell, x shaker, p conga). Exact positions vary between transcriptions and between the
bars of each record; these are the forms the built-in breaks play (shared/sampler/jo_break.c).

| Break | Kick | Snare | Tops |
|---|---|---|---|
| Amen (The Winstons, 1969) | `K.K.......KK....` | `....S..s.s..S..s` | `R.r.R.r.R.r.R.r.` |
| Think (Lyn Collins, 1972) | `K.....k.K.......` | `....S..g....S..g` | `hhHhhhHhhhHhhhoh` |
| Apache (Incredible Bongo Band, 1973) | `K.......K.k.....` | `....S.......S...` | `H.h.H.h.H.h.H.h.` + bongos `..p.P..p..p.P..p` |
| Funky Drummer (James Brown, 1970) | `K.K.......K..k..` | `....S..g.g..S..g` | `hhhhhhhohhhhhohh` |
| Hot Pants (Bobby Byrd, 1971) | `K.....k...K..k..` | `....S..g....S.g.` | `HhHhHhHhHhHhHhHh` |
| a jungle roller (original) | `K.........K.....` | `....S.g..g..S.gg` | `x.x.x.x.x.x.x.x.` |

Notes: the Amen's kick is not on every downbeat of the original four bars and its last snare is displaced; the
Funky Drummer's build-up in Ethan Hein's notation has kicks on the offbeats, ghosts around beat three and open hats
before three and after four (the text gives the build-up steps; the final grid is an image). Think and Hot Pants
grids were not found in text sources; theirs here are the common forms of the rhythm, marked as approximate.

## The classic drum & bass patterns

From STRANJAH's pattern tutorials (chapter lists: "Top 10 DnB Drum Patterns", "Most Popular Drum Patterns", "What
Makes Each DnB Subgenre Unique?") and the genre's common forms; the grids are this firmware's (GROOVES, firmware/
break-surgeon/engine/bs_engine.c):

| Pattern | Kick | Snare | Notes |
|---|---|---|---|
| 2-step | `X.........x.....` | `....X.......X...` | the backbone |
| delayed 2-step | as 2-step | `....X........X..` | the second snare a 16th late |
| steppa | `X.......X.......` | backbeat | straight 8th hats |
| stompa | `X.....x.X.......` | backbeat | |
| half-time | `X.........x.....` | `........X.......` | one snare a bar |
| dancehall | `X.......X.......` | `...x..X....x..X.` | the dembow over DnB |
| footwork | `X..x..x...x..x..` | backbeat | dotted kicks, fake triplets |
| triplet 2-step | 2-step | backbeat | 16th hats swung 67 % |
| rollers | 2-step | backbeat | rolling ghosts, 16th hats, swing 56 % |
| jump up | `X.......X.x.....` | backbeat | |
| techstep | `X.........X..x..` | backbeat | hard, straight |
| dancefloor | `X......x..X.....` | backbeat | |
| liquid (0.7) | 2-step | backbeat | ghosts all around, 16th hats, swing 54 % |
| neuro (0.7) | `X..x......x..x..` | backbeat | tight and dry |

**Classic jungle edits** (re-chopping the break, as the STRANJAH "10 Most Iconic Amen Patterns" chapter list and
the chopping tutorials describe): the break's first half twice with its last snares doubled (JUNGLE EDIT, slices
`0 1 2 3 4 5 6 7 0 1 2 3 C D C F`) and a snare repeated over the last four 16ths, building (SNARE ROLL EDIT,
`0 1 2 3 4 5 6 7 8 9 A B C C C C`). P2 ROLLER uses the first edit with a roll on its 15th 16th.

## Sources

- STRANJAH, YouTube (chapter lists and descriptions read; videos not transcribed): [10 Tips to Make Your DnB Kicks
  Hit Harder](https://www.youtube.com/watch?v=BUYMunlw3Sw), [Top 10 DnB Drum Patterns](https://www.youtube.com/watch?v=zXGz-M1Fo3g),
  [Most Popular Drum Patterns](https://www.youtube.com/watch?v=aF5G1WeeQ8A), [What Makes Each DnB Subgenre
  Unique?](https://www.youtube.com/watch?v=sNgF8SPRYps), [10 Most Iconic Amen Patterns](https://www.youtube.com/watch?v=zqTB5XSjmzI),
  [Everything about the DNB Beat](https://www.youtube.com/watch?v=vmPY1xlZmm4), [How I Sidechain Breaks](https://www.youtube.com/watch?v=gg7hS0BU6po),
  [Why 90s Jungle Sounded Better](https://www.youtube.com/watch?v=t7xqFbIEiuM), [Make Any Break Sound Like Your
  Favourite Break](https://www.youtube.com/watch?v=r5W364Wr02g)
- Kick synthesis: [MusicRadar: synthesize a kick in Massive](https://musicradar.com/tuition/tech/how-to-synthesize-a-kick-drum-in-nis-massive-639049),
  [Mode Audio: drum synth kick & snare](https://modeaudio.com/magazine/drum-synth-sound-design-kick-snare),
  [Mode Audio: synths for drums, kicks](https://modeaudio.com/magazine/using-synths-for-drums-part-1-kicks),
  [Beat Kitchen: drum design from scratch](https://beatkitchen.io/guides/electronic-music/02-drum-design/),
  [Soundbridge: kick drum tune](https://soundbridge.io/kick-drum-tune)
- Snares: [MusicRadar: a multi-layered DnB snare in Serum](https://www.musicradar.com/how-to/how-to-design-a-multi-layered-drum-and-bass-snare-in-serum),
  [Attack Magazine: layering drums](https://www.attackmagazine.com/technique/tutorials/secrets-dance-music-production-layering-drums/),
  [Audiotent: layering claps and snares](https://audiotent.com/blogs/production-tips/how-to-layer-claps-and-snares),
  [UKF: dat snare tho](https://ukf.com/read/dat-snare-tho/)
- Breaks and processing: [Sound On Sound: SampleX review](https://www.soundonsound.com/reviews/beatskillz-samplex),
  [MusicRadar: clean up and enhance a sampled breakbeat](https://musicradar.com/tuition/tech/how-to-clean-up-and-enhance-a-sampled-breakbeat-621106),
  [eMastered: how to make jungle music](https://emastered.com/blog/how-to-make-jungle-music),
  [EDMProd: how to make jungle](https://www.edmprod.com/how-to-make-jungle-music/),
  [Reverb: the samplers behind 90s jungle](https://reverb.com/news/the-samplers-behind-90s-jungle-and-drum-and-bass),
  [MusicRadar: old-skool jungle breaks](https://www.musicradar.com/tuition/tech/how-to-make-old-skool-jungle-style-breaks-464788)
- Break rhythms: [Ethan Hein: rhythmic simples](https://www.ethanhein.com/wp/2015/rhythmic-simples/),
  [Ethan Hein: building the Funky Drummer beat](https://ethanhein.substack.com/p/building-the-funky-drummer-beat),
  [Ethan Hein: building the Amen break](https://ethanhein.substack.com/p/building-the-amen-break),
  [MusicRadar: the greatest drum breaks](https://www.musicradar.com/news/best-drum-breaks-of-all-time),
  [Soundtrap: guide to drum breaks](https://blog.soundtrap.com/guide-to-drum-breaks-and-breakbeats/),
  [Hudson Music: The Breakbeat Bible sampler](https://hudsonmusic.com/wp-content/uploads/2015/03/Breakbeat-Bible-Sampler.pdf),
  [Transmission Samples: DnB drum patterns](https://www.transmissionsamples.com/tutorials/drum-and-bass-production/dnb-drum-patterns)
