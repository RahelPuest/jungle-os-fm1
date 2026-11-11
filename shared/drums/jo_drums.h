/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* Break Surgeon's synthesized drums: procedural models, no sample playback (0.7: the kick in three layers with a
 * hold, a two-stage pitch drop and an asymmetric saturation; the snare with a clap layer and an air crack; the hats
 * as two metal bands plus noise, longer and louder; docs/research/dnb-drums.md). Each voice takes four curated
 * macros (0..1) and derives its physical parameters once per hit, so a parameter lock or a sweetspot morph costs
 * nothing per sample. The velocity shapes the timbre as well as the level (a harder hit is brighter, snappier,
 * with more click), and GHOST SNARE is the snare's own drum played softly. Monophonic per track, with a de-click
 * tail on retrigger; METAL HATS has a closed/open choke. */
#pragma once
#include "../dsp/jo_dsp.h"

enum { JD_KICK, JD_SNARE, JD_GHOST, JD_HATS, JD_PERC, JD_IMPACT, JD_NMODELS };
enum { JD_PERC_TOM, JD_PERC_RIM, JD_PERC_SHAKER, JD_PERC_CLICK, JD_PERC_NVAR };
#define JD_NMACRO 4

/* names of each model's four macros, for the UI and the docs */
extern const char *const jd_model_name[JD_NMODELS];
extern const char *const jd_macro_name[JD_NMODELS][JD_NMACRO];

typedef struct {
    float m[JD_NMACRO]; /* the macros, 0..1 */
    float level;        /* 0..1, after the model's own loudness trim */
    float tune;         /* semitones, -24..+24 */
    uint8_t variant;    /* PERC: JD_PERC_*; HATS: unused (open/closed is per hit) */
} jd_sound_t;

typedef struct {
    uint8_t model, active, open;
    /* derived at the hit */
    float amp, amp_mul;          /* main amplitude envelope */
    float amp2, amp2_mul;        /* second envelope (noise / snap) */
    float click, click_mul;      /* transient */
    float snap, snap_mul;        /* snare and ghost: the wires' snap (a short bright band of noise) */
    float f0, fsweep, fsweep_mul;/* body pitch: f0 + fsweep (decaying) in Hz */
    float ph[6], inc[6];         /* body / metallic oscillators */
    float tone_mix, noise_mix, drive, outgain, bright_g, res, sweep_g, sweep_mul;
    jo_svf_t svf, svf2;
    jo_onepole_t hp;
    float declick;               /* the last output at a retrigger, faded out over ~1.5 ms */
    float last;
    jo_rng_t rng;
    /* (0.7) the kick's hold before its decay and its second (knock) pitch stage; the snare's clap bursts; a
     * swell for soft attacks (shakers); a second high-pass; the saturation's DC blocker */
    uint32_t t, hold;
    float knock, knock_mul, clap, clap_env, clap_mul, clap_tail_mul, att, att_k;
    jo_svf_t svf_c;
    jo_onepole_t hp2;
    jo_dcb_t dcb;
} jd_voice_t;

void jd_sound_default(jd_sound_t *s, int model);
void jd_voice_init(jd_voice_t *v, int model, uint32_t seed);
/* a hit: vel 0..1, the sound with any locks already applied; open: hats only */
void jd_trigger(jd_voice_t *v, const jd_sound_t *s, float vel, int open);
void jd_choke(jd_voice_t *v);    /* a fast fade (the open hat choked by a closed one) */
/* adds n samples into out (mono); returns 0 once silent */
int jd_render(jd_voice_t *v, float *out, uint32_t n);
