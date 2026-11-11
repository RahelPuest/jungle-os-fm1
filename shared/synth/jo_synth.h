/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* Abyss Engine's voices: the bass (0.7: five models, FM NEURO, REESE, WOMP, HORN, SINE), the clean sub, the stab
 * engine and the pad.
 * Parameters are physical (Hz, seconds, ratios); the sweetspot graph and the macros produce them. */
#pragma once
#include "../dsp/jo_dsp.h"

/* ---------------------------------------------------------- neuro bass --- */
/* (0.7) the bass models (0.7.1: REESE, HORN rebuilt, HOOVER its own model):
 *   FM      two-operator FM with feedback (growls, talking basses, lasers, metallic neuro)
 *   REESE   two saws detuned against each other, free-running, their detune drifting slowly (the phase
 *           movement), and a saw an octave below; RATIO = the detune (3..30 cents), INDEX = the octave-down saw,
 *           FDBK = the drift
 *   WOMP    saw / square with a per-note "wom" (the filter and the vowel open and close on every note) under the
 *           synced LFO: jump-up womps and wubs; INDEX = the square, FDBK = the wom's vowel depth
 *   HORN    the foghorn (0.7.2, after Stranjah's and MusicTech's patches): an octave down, three stacked squares
 *           FMed by a sine RATIO times up (8: three octaves), a 24 dB low-pass that an AD (100 ms up, MODDEC down)
 *           opens, driven hard, and a reverb send (the engine's); INDEX = the FM depth, FDBK = the modulator's
 *           self-feedback
 *   SINE    a driven sine (808, dub, rollers' clean mid) with a little FM grit (INDEX)
 *   HOOVER  the Alpha Juno hoover: PWM saws on three octaves, the pulse width stirred by a fast LFO, a pitch
 *           envelope that dives from sharp onto the note (PITCH > 0), a deep chorus; INDEX = the top octave, FDBK =
 *           the PWM rate and depth */
enum { JN_FM_CREATURE, JN_REESE, JN_WOMP, JN_HORN, JN_SINE, JN_HOOVER, JN_NMODELS };
extern const char *const jn_model_name[JN_NMODELS];
/* the bass parameter vector, as the sweetspot graph orders it (bass_params in jo_synth.c) */
enum {
    JN_P_MODEL,     /* discrete: JN_* */
    JN_P_RATIO,     /* FM ratio 0.5..8 (log); REESE: detune spread */
    JN_P_INDEX,     /* FM index 0..12; REESE: sub-saw blend */
    JN_P_FEEDBACK,  /* modulator feedback 0..0.85 */
    JN_P_VOWEL,     /* formant position 0..1 (u o a e i) */
    JN_P_FORMANT,   /* formant filter mix 0..1 */
    JN_P_CUTOFF,    /* low-pass Hz 80..12000 (log) */
    JN_P_RES,       /* 0..0.9 */
    JN_P_DRIVE,     /* 1..10 (log) */
    JN_P_MODENV,    /* the index/cutoff envelope amount 0..1 */
    JN_P_MODDEC,    /* its decay s 0.02..1.5 (log) */
    JN_P_LFODIV,    /* discrete 0..7: 1/1 1/2 1/4 1/4T 1/8 1/8T 1/16 1/32 */
    JN_P_LFODEPTH,  /* 0..1 */
    JN_P_FOLD,      /* wavefold 0..1 */
    JN_P_GLIDE,     /* s 0.001..0.15 (log) */
    JN_P_PHASER,    /* (0.7) a 4-stage phaser swept by the LFO (static and nasal without LFO depth), 0..1 */
    JN_P_PITCH,     /* (0.7) the pitch envelope: the note starts PITCH semitones off (-12..+24) and falls / rises
                       to it over MODDEC / 2 (lasers, 808 drops, the hoover's bend) */
    JN_NPARAMS
};

typedef struct {
    float p[JN_NPARAMS];  /* physical */
    float motion;         /* MOTION macro: LFO depth scaling + rate coupling, 0..1 */
    float lfo_shape;      /* 0 sine, 1 exp-saw down, 2 pulse, 3 asym, 4 stepped (MOD SHAPE) */
    float coupling;       /* MOD COUPLING: how many destinations follow the LFO, 0..1 */
} jn_patch_t;

#define JN_CH_LEN 512u /* the hoover's chorus line: 11.6 ms */
typedef struct {
    float freq, target, glide_k;
    float ph_c, ph_m, fb_y1, fb_y2;
    float ph_r[4], drift_ph[4];
    float pe, pe_mul;        /* the pitch envelope (semitones), its decay */
    float ap[4], ap_fb;      /* the phaser's allpass states, its feedback */
    jo_adsr_t amp;
    float modenv, modenv_mul;
    jo_svf_t f1, f2, lp;
    jo_dcb_t dc;
    float lfo_ph, lfo_held;
    uint32_t lfo_last_step;
    uint8_t gate;
    float vel;
    float xfade, prev_out;   /* model-switch crossfade */
    uint8_t last_model;
    /* (0.7.1) the foghorn's second low-pass stage and its AD's attack; the hoover's PWM LFO and chorus */
    jo_svf_t lp2;
    float fatt, pwm_ph, ch_ph, fm_y[3];
    uint32_t ch_w;
    float ch_buf[JN_CH_LEN];
} jn_bass_t;

extern const char *const jn_param_name[JN_NPARAMS];
void jn_bass_init(jn_bass_t *v);
void jn_bass_note(jn_bass_t *v, float note, float vel, int legato, const jn_patch_t *p);
void jn_bass_off(jn_bass_t *v);
/* step_phase: the clock's position in 16ths (for the synced LFO); out: mono, added */
void jn_bass_render(jn_bass_t *v, const jn_patch_t *p, float steps_now, float steps_per_sample, float *out, uint32_t n);

/* ----------------------------------------------------------------- sub --- */
typedef struct {
    float freq, target, glide_k, ph;
    float env, att_k, rel_k, sat, tri;
    float pe, pe_mul;        /* (0.7) a pitch envelope in semitones (the 808 drop; 0: none) */
    uint8_t gate;
    int8_t octave;           /* relative to the bass note: -1 (default), 0; then folded into 36..72 Hz */
} jn_sub_t;
void jn_sub_init(jn_sub_t *s);
void jn_sub_note(jn_sub_t *s, float note, float glide_s, int legato);
/* (0.7) the sub's pitch envelope for the note just started: semitones and decay (s); 0: none */
void jn_sub_pitch_env(jn_sub_t *s, float semis, float decay_s);
void jn_sub_off(jn_sub_t *s);
void jn_sub_render(jn_sub_t *s, float *out, uint32_t n);

/* --------------------------------------------------------------- stabs --- */
enum { JN_STAB_RAVE, JN_STAB_ORCH, JN_STAB_GLASS, JN_STAB_DARK, JN_STAB_NMODELS };
extern const char *const jn_stab_name[JN_STAB_NMODELS];
#define JN_STAB_VOICES 4
typedef struct {
    uint8_t model;
    float chord;     /* CHORD / TENSION macro 0..1 */
    float attack;    /* ATTACK 0..1 */
    float bite;      /* BITE / BRIGHT 0..1 */
    float tail;      /* TAIL / SPACE 0..1 (decay; the space send is the engine's) */
} jn_stab_patch_t;
typedef struct {
    float ph[3], inc[3];
    jo_adsr_t amp;
    float fenv, fenv_mul, pitchenv, pitchenv_mul;
    jo_svf_t f;
    float vel;
    uint8_t active;
} jn_stab_voice_t;
typedef struct { jn_stab_voice_t v[JN_STAB_VOICES]; jo_rng_t rng; } jn_stab_t;
void jn_stab_init(jn_stab_t *s);
/* a chord: up to JN_STAB_VOICES notes; gate_s: the hold before release */
void jn_stab_chord(jn_stab_t *s, const jn_stab_patch_t *p, const float *notes, uint32_t nnotes, float vel);
void jn_stab_release(jn_stab_t *s);
void jn_stab_render(jn_stab_t *s, const jn_stab_patch_t *p, float *l, float *r, uint32_t n);

/* ----------------------------------------------------------------- pad --- */
#define JN_PAD_VOICES 4
typedef struct {
    float darkness, evolve, tension, depth; /* the macros 0..1 */
    float attack_s, release_s;
} jn_pad_patch_t;
typedef struct {
    float ph[2], inc[2], base_inc;
    jo_adsr_t amp;
    jo_svf_t f;
    float pan;
    uint8_t active;
    float note;
} jn_pad_voice_t;
typedef struct { jn_pad_voice_t v[JN_PAD_VOICES]; float lfo1, lfo2; uint8_t hold; } jn_pad_t;
void jn_pad_init(jn_pad_t *p);
/* sets the held chord (up to JN_PAD_VOICES notes): voices that keep their note keep playing (no retrigger),
 * the others release and start again on the new notes */
void jn_pad_chord(jn_pad_t *p, const jn_pad_patch_t *pp, const float *notes, uint32_t nnotes);
void jn_pad_release(jn_pad_t *p);
void jn_pad_render(jn_pad_t *p, const jn_pad_patch_t *pp, float *l, float *r, uint32_t n);

/* --------------------------------------------------------------- harmony --- */
enum { JH_MINOR, JH_HARM_MINOR, JH_PHRYGIAN, JH_DORIAN, JH_CHROMATIC, JH_NSCALES };
extern const char *const jh_scale_name[JH_NSCALES];
enum { JH_CH_MIN, JH_CH_MIN7, JH_CH_SUS2, JH_CH_MIN9, JH_CH_MAJ7, JH_CH_DIM, JH_CH_CLUSTER, JH_CH_FIFTH, JH_NCHORDS };
extern const char *const jh_chord_name[JH_NCHORDS];
/* the note of scale degree deg (any integer) over root (MIDI note) */
int jh_degree(int root, int scale, int deg);
/* the nearest scale note to note (scale snapping; JH_CHROMATIC passes everything) */
int jh_snap(int root, int scale, int note);
/* a chord's notes over note: tension 0..1 adds a 9th / b9 / 11th; inversion 0..3; returns the count (<= max) */
uint32_t jh_chord(int note, int chord, float tension, int inversion, float *out, uint32_t max);
