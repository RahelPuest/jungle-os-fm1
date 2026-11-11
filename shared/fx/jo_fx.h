/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* Master processing for both instruments.
 *
 * DRUM PRESSURE (Break Surgeon):  mono drum bus + the room (a short FDN fed by each track's send) -> sampler grit
 *   (CRUSH's lower half: 12 down to 10 bits, a darker top, a held rate) -> transient contour -> parallel crush
 *   (CRUSH's upper half) -> saturation -> glue compressor -> soft clip -> peak guard -> output trim. Macros PUNCH,
 *   CRUSH, GLUE, PRESSURE, and ROOM. The room goes in before the glue, so it is compressed with the kit, as a
 *   break recorded in a room is.
 * NEURO PRESSURE (Abyss Engine):  clean sub (mono, own path) + dirty bass (high-passed above the crossover so its
 *   distortion never carries sub) + pads/stabs (stereo, sends) -> ducking on sub and bass -> width on the
 *   high-passed side of pads/stabs only (sub and bass stay centred) -> glue -> soft clip -> peak guard. Macros WEIGHT, DIRT, WIDTH, PRESSURE.
 *
 * The peak guard is a fast-attack gain rider in front of a hard ceiling; there is no look-ahead and no
 * oversampling, so it is NOT a true-peak limiter (docs/architecture/audio-graph.md). */
#pragma once
#include "../dsp/jo_dsp.h"

#ifndef JO_POOL
#define JO_POOL /* the app puts big buffers in the FM-1's .pool RAM */
#endif

/* ------------------------------------------------------------- compressor --- */
typedef struct {
    float env, gr_db;            /* the detector (linear) and the gain reduction now (dB, <= 0) */
    float ka, kr;
    float thresh_db, ratio, knee_db, makeup_db;
} jf_comp_t;
void jf_comp_set(jf_comp_t *c, float thresh_db, float ratio, float attack_s, float release_s, float makeup_db);
float jf_comp_gain(jf_comp_t *c, float detector_in); /* per sample: the linear gain to apply */

/* ------------------------------------------------------------- peak guard --- */
typedef struct { float g, kr, ceiling; uint32_t hits; } jf_guard_t;
void jf_guard_init(jf_guard_t *g, float ceiling_db);
float jf_guard(jf_guard_t *g, float x);           /* mono */
void jf_guard2(jf_guard_t *g, float *l, float *r); /* linked stereo */

/* ------------------------------------------------------- reverb (small) --- */
/* four-line feedback delay network with damping, int16 lines; a room-to-hall space for pads and stabs */
#define JF_VERB_TOTAL 16384u
typedef struct {
    int16_t *buf;                       /* JF_VERB_TOTAL samples, the app's storage */
    uint32_t len[4], off[4], w[4];
    float fb, damp_k, lp[4];
    jo_onepole_t hp;
} jf_verb_t;
void jf_verb_init(jf_verb_t *v, int16_t *buf);
void jf_verb_set(jf_verb_t *v, float size, float decay, float damp_hz);
void jf_verb_process(jf_verb_t *v, const float *in, float *l, float *r, uint32_t n);

/* ------------------------------------------------------------ drum bus --- */
enum { JF_DRUM_CLEAN_JUNGLE, JF_DRUM_ROLLING, JF_DRUM_TIGHT_NEURO, JF_DRUM_CRUSHED, JF_DRUM_INDUSTRIAL, JF_DRUM_NPRESETS };
extern const char *const jf_drum_preset_name[JF_DRUM_NPRESETS];
typedef struct {
    float punch, crush, glue, pressure; /* the macros, 0..1 */
    float trim_db;                      /* output trim */
    float room;                         /* the room's level, 0..1 */
} jf_drum_macros_t;
void jf_drum_preset(jf_drum_macros_t *m, int preset);

typedef struct {
    jf_drum_macros_t m;
    /* derived (jf_drum_update, at most once a block) */
    float tr_amt, crush_mix, crush_bits, crush_hold, sat_drive, sat_comp, out_gain;
    float grit, grit_bits, grit_hold, grit_k, room_gain;
    float eq_lo, eq_box, eq_hi;         /* (0.7) the console EQ before the saturation: weight, the box cut, presence */
    /* state */
    jo_onepole_t eq_l, eq_b1, eq_b2, eq_h;
    jo_follow_t fast, slow;
    float crush_ph, crush_held, grit_ph, grit_held, grit_lp;
    jf_verb_t room;                     /* active once jf_drum_room_buf gave it its lines */
    uint8_t has_room;
    jf_comp_t comp;
    jf_guard_t guard;
    jo_dcb_t dc;
    float peak;                         /* the output peak since the last read (meter) */
} jf_drum_bus_t;
void jf_drum_init(jf_drum_bus_t *b);
void jf_drum_room_buf(jf_drum_bus_t *b, int16_t *buf); /* JF_VERB_TOTAL samples: the room (0: none) */
void jf_drum_update(jf_drum_bus_t *b);  /* after a macro change */
/* processes n samples in place (mono); send: the room's input (each track's share of it), or 0 */
void jf_drum_process(jf_drum_bus_t *b, float *x, const float *send, uint32_t n);

/* --------------------------------------------------------------- ducking --- */
/* an envelope a trigger (a kick, from the sequencer or the link) starts: depth, attack, hold, release */
typedef struct { float depth, ka, hold_s, kr; float env; uint32_t hold_left; uint8_t stage; } jf_duck_t;
void jf_duck_set(jf_duck_t *d, float depth, float attack_s, float hold_s, float release_s);
void jf_duck_trigger(jf_duck_t *d, float vel);
float jf_duck_tick(jf_duck_t *d);       /* the gain to apply, 1 - depth .. 1 */

/* -------------------------------------------------- delay (ping-pong) ----- */
#define JF_DELAY_LEN 16384u             /* per side: 0.37 s at 44.1 kHz (a quarter at >= 162 BPM) */
typedef struct {
    int16_t *bl, *br;                   /* JF_DELAY_LEN each, the app's (pool) storage */
    uint32_t w;
    float time_smp, fb, tone_k, mix;
    jo_onepole_t lpl, lpr;
} jf_delay_t;
void jf_delay_init(jf_delay_t *d, int16_t *bl, int16_t *br);
void jf_delay_set(jf_delay_t *d, float seconds, float feedback, float tone_hz);
/* in: mono send; adds the wet signal into l, r */
void jf_delay_process(jf_delay_t *d, const float *in, float *l, float *r, uint32_t n);

/* -------------------------------------------------------- neuro master --- */
typedef struct { float weight, dirt, width, pressure, trim_db; } jf_neuro_macros_t;
typedef struct {
    jf_neuro_macros_t m;
    float sub_gain, dirt_drive, dirt_comp, width;
    jo_onepole_t xo_lo, side_hp_l, side_hp_r;   /* the crossover and the side's high-pass */
    jo_dcb_t dcl, dcr;
    jf_comp_t comp;
    jf_guard_t guard;
    float peak;
} jf_neuro_t;
void jf_neuro_init(jf_neuro_t *b);
void jf_neuro_update(jf_neuro_t *b);
/* sub: mono clean sub; bass: mono dirty bass; l, r: pads + stabs + returns (stereo) in, the master out.
 * duck: per-sample gain for sub and bass (or 0 for none) */
void jf_neuro_process(jf_neuro_t *b, const float *sub, const float *bass, float *l, float *r, const float *duck,
                      uint32_t n);
