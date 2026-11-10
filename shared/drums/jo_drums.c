/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* Procedural drum models tuned for 160-190 BPM drum & bass. The ranges are initial hypotheses, checked by
 * tests/unit/test_modules.c (bounded, finite, decaying) and by ear from tests/audio-regression renders.
 * 0.2: the kick sits at 45-75 Hz (55 Hz by default: the sub is Abyss Engine's) with a tonal beater click; the
 * snare is a tuned body with a pitch drop, a crack, the wires' snap and their tail, each scaled by how hard it is
 * hit; the ghost snare is that snare played softly; the hats lean to noise (shaker-like) unless METAL is up. */
#include "jo_drums.h"

const char *const jd_model_name[JD_NMODELS] = {"PUNCH KICK", "CRACK SNARE", "GHOST SNARE", "METAL HATS", "BREAK PERC",
                                               "IMPACT FX"};
const char *const jd_macro_name[JD_NMODELS][JD_NMACRO] = {
    {"WEIGHT", "PUNCH", "PITCH", "DRIVE"}, {"BODY", "CRACK", "TAIL", "PRESSURE"}, {"TONE", "SNAP", "DECAY", "SWING"},
    {"METAL", "BRIGHT", "LENGTH", "HUMAN"}, {"TONE", "DECAY", "NOISE", "BEND"},    {"SIZE", "SWEEP", "NOISE", "DIRT"},
};

/* exponential map of a macro onto [lo, hi] (times, frequencies) */
static inline float xmap(float m, float lo, float hi) { return lo * jo_exp2f(jo_clampf(m, 0.0f, 1.0f) * jo_log2f(hi / lo)); }
static inline float lmap(float m, float lo, float hi) { return lo + (hi - lo) * jo_clampf(m, 0.0f, 1.0f); }

void jd_sound_default(jd_sound_t *s, int model)
{
    static const float D[JD_NMODELS][JD_NMACRO] = {
        {0.45f, 0.6f, 0.4f, 0.25f}, /* kick: short, punchy, ~55 Hz */
        {0.5f, 0.6f, 0.4f, 0.3f},   /* snare */
        {0.45f, 0.4f, 0.35f, 0.5f}, /* ghost */
        {0.3f, 0.55f, 0.3f, 0.25f}, /* hats */
        {0.5f, 0.35f, 0.3f, 0.3f},  /* perc */
        {0.5f, 0.5f, 0.5f, 0.2f},   /* impact */
    };
    uint32_t i;
    for (i = 0; i < JD_NMACRO; i++)
        s->m[i] = D[model][i];
    s->level = model == JD_GHOST ? 0.55f : model == JD_HATS ? 0.5f : 0.8f;
    s->tune = 0.0f;
    s->variant = 0;
}

void jd_voice_init(jd_voice_t *v, int model, uint32_t seed)
{
    uint8_t *p = (uint8_t *)v;
    uint32_t i;
    for (i = 0; i < sizeof *v; i++)
        p[i] = 0;
    v->model = (uint8_t)model;
    jo_rng_seed(&v->rng, seed * 2654435761u + 17u);
}

void jd_choke(jd_voice_t *v)
{
    if (v->active) {
        float m = jo_decay_mul(0.012f);
        if (v->amp_mul > m) v->amp_mul = m;
        if (v->amp2_mul > m) v->amp2_mul = m;
        if (v->snap_mul > m) v->snap_mul = m;
    }
}

/* the 808-style metallic ratios (six square waves), scaled by METAL's spread */
static const float HAT_RATIO[6] = {1.0f, 1.4471f, 1.6170f, 1.9265f, 2.5028f, 2.6637f};

void jd_trigger(jd_voice_t *v, const jd_sound_t *s, float vel, int open)
{
    float m0 = s->m[0], m1 = s->m[1], m2 = s->m[2], m3 = s->m[3];
    float tune = jo_exp2f(jo_clampf(s->tune, -24.0f, 24.0f) * (1.0f / 12.0f));
    float g = jo_clampf(vel, 0.0f, 1.0f) * jo_clampf(s->level, 0.0f, 1.0f);
    uint32_t i;
    v->declick = v->active ? v->last : 0.0f;
    v->active = 1;
    v->open = (uint8_t)open;
    v->click = 0.0f;
    v->snap = 0.0f;
    v->amp2 = 0.0f;
    v->fsweep = 0.0f;
    v->fsweep_mul = 1.0f;
    v->sweep_g = 0.0f;
    v->sweep_mul = 1.0f;
    v->tone_mix = 1.0f;
    v->noise_mix = 0.0f;
    v->drive = 1.0f;
    v->res = 0.0f;
    v->svf.s1 = v->svf.s2 = v->svf2.s1 = v->svf2.s2 = 0.0f;
    for (i = 0; i < 6u; i++)
        v->ph[i] = 0.0f;
    switch (v->model) {
    case JD_KICK: { /* WEIGHT decay, PUNCH sweep + beater click, PITCH body (45-75 Hz), DRIVE */
        float hard = 0.55f + 0.45f * jo_clampf(vel, 0.0f, 1.0f); /* a harder hit: more sweep, more click */
        float tau = lmap(1.0f - m1, 0.004f, 0.022f);
        v->f0 = xmap(m2, 45.0f, 75.0f) * tune;
        v->fsweep = v->f0 * lmap(m1, 2.5f, 8.0f) * hard;
        v->fsweep_mul = jo_expf(-1.0f / (tau * JO_SR));
        v->amp = 1.0f;
        v->amp_mul = jo_decay_mul(xmap(m0, 0.08f, 0.5f));
        v->click = lmap(m1, 0.1f, 0.6f) * hard;            /* the beater: a tonal blip at 1.8-3.8 kHz */
        v->click_mul = jo_decay_mul(0.0035f);
        v->inc[1] = xmap(m1, 1800.0f, 3800.0f) * JO_INV_SR;
        v->amp2 = lmap(m1, 0.05f, 0.3f) * hard * hard;     /* and a shorter noise snap on top */
        v->amp2_mul = jo_decay_mul(0.0015f);
        v->drive = 1.0f + 6.0f * m3 * m3;
        v->outgain = g / jo_tanh(v->drive * 1.2f);         /* the driven peak held near the level */
        break;
    }
    case JD_SNARE:
    case JD_GHOST: {
        /* SNARE: BODY, CRACK, TAIL, PRESSURE. GHOST: the same drum played softly, TONE (brightness), SNAP, DECAY
         * (SWING is the sequencer's: where the ghosts fall). How hard it is hit scales the crack and the snap far
         * more than the body, so a soft hit is a dull tap and a hard one cracks. */
        int gh = v->model == JD_GHOST;
        float vv = jo_clampf(vel, 0.0f, 1.0f);
        float hard = gh ? 0.3f + 0.35f * vv : 0.45f + 0.55f * vv;
        float body = gh ? 0.4f : m0, crack = gh ? 0.6f * m1 : m1, tail = gh ? 0.45f * m2 : m2, bright = gh ? m0 : m1;
        v->f0 = 185.0f * tune * (1.0f + 0.05f * hard);
        v->fsweep = v->f0 * lmap(hard, 0.3f, 0.8f);       /* the head's pitch drop */
        v->fsweep_mul = jo_decay_mul(0.018f);
        v->tone_mix = lmap(body, 0.25f, 1.0f);
        v->amp = 1.0f;
        v->amp_mul = jo_decay_mul(lmap(body, 0.05f, 0.14f));
        v->noise_mix = 0.8f;
        v->amp2 = 1.0f;                                     /* the wires' tail */
        v->amp2_mul = jo_decay_mul(xmap(tail, 0.07f, 0.42f) * (0.6f + 0.4f * hard));
        v->bright_g = jo_svf_g(xmap(bright, 1800.0f, 6500.0f) * (0.6f + 0.4f * hard));
        v->res = 0.25f;
        v->snap = lmap(crack, 0.4f, 1.0f) * hard * jo_sqrtf(hard); /* the wires' snap */
        v->snap_mul = jo_decay_mul(xmap(crack, 0.018f, 0.04f));
        v->sweep_g = jo_svf_g(xmap(bright, 3000.0f, 7000.0f));
        v->click = lmap(crack, 0.15f, 0.9f) * hard * hard;  /* the stick's crack */
        v->click_mul = jo_decay_mul(0.0015f);
        v->drive = gh ? 1.0f : 1.0f + 5.0f * m3;
        v->outgain = (gh ? 0.9f : 0.7f) * g / jo_tanh(v->drive);
        break;
    }
    case JD_HATS: { /* METAL mix + spread (low: noise, a shaker; high: metal), BRIGHT high-pass, LENGTH, HUMAN */
        float human = m3 * jo_rng_bi(&v->rng), hard = 0.5f + 0.5f * jo_clampf(vel, 0.0f, 1.0f);
        float base = xmap(0.5f, 300.0f, 520.0f) * tune * (1.0f + 0.15f * (m0 - 0.5f));
        float len = xmap(m2, 0.012f, 0.09f) * (open ? 4.5f : 1.0f) * (0.7f + 0.3f * hard) * (1.0f + 0.25f * human);
        for (i = 0; i < 6u; i++)
            v->inc[i] = base * HAT_RATIO[i] * (1.0f + m0 * 0.07f * (float)i) * JO_INV_SR;
        v->tone_mix = 0.85f * m0;
        v->noise_mix = 1.0f - 0.55f * m0;
        v->bright_g = jo_svf_g(xmap(m1, 5000.0f, 11500.0f) * (0.8f + 0.2f * hard));
        v->res = 0.3f;
        v->amp = 0.0f;
        v->amp_mul = 0.0f;
        v->amp2 = 1.0f;
        v->amp2_mul = jo_decay_mul(len);
        v->outgain = 0.6f * g * (1.0f + 0.2f * human);
        break;
    }
    case JD_PERC: { /* a tonal body plus a band of noise, voiced by the variant */
        static const float F[JD_PERC_NVAR][2] = {{90.0f, 260.0f}, {700.0f, 1900.0f}, {3000.0f, 9000.0f}, {1200.0f, 5000.0f}};
        uint32_t var = s->variant < JD_PERC_NVAR ? s->variant : 0u;
        float f = xmap(m0, F[var][0], F[var][1]) * tune;
        float dec = var == JD_PERC_TOM ? xmap(m1, 0.08f, 0.6f) : var == JD_PERC_CLICK ? xmap(m1, 0.005f, 0.03f)
                                                                                    : xmap(m1, 0.02f, 0.18f);
        v->f0 = f;
        v->fsweep = f * lmap(m3, 0.0f, 1.5f);
        v->fsweep_mul = jo_decay_mul(0.04f);
        v->tone_mix = var == JD_PERC_SHAKER ? 0.0f : 1.0f - 0.8f * m2;
        v->noise_mix = var == JD_PERC_SHAKER ? 1.0f : m2;
        v->bright_g = jo_svf_g(f * (var == JD_PERC_SHAKER ? 1.0f : 1.5f) * (0.75f + 0.25f * jo_clampf(vel, 0.0f, 1.0f)));
        v->res = var == JD_PERC_CLICK ? 0.8f : 0.4f;
        v->amp = 1.0f;
        v->amp_mul = jo_decay_mul(dec);
        v->amp2 = var == JD_PERC_SHAKER ? 0.0f : 1.0f; /* shaker: a short swell instead */
        v->amp2_mul = jo_decay_mul(dec * 0.7f);
        v->click = var == JD_PERC_RIM || var == JD_PERC_CLICK ? 0.6f : 0.1f;
        v->click_mul = jo_decay_mul(0.002f);
        v->outgain = 0.8f * g;
        break;
    }
    default: { /* IMPACT: noise through a sweeping band plus a falling sub; SIZE length, SWEEP, NOISE, DIRT */
        float len = xmap(m0, 0.3f, 2.5f);
        v->f0 = 38.0f * tune;
        v->fsweep = 120.0f * m1;
        v->fsweep_mul = jo_decay_mul(len * 0.5f);
        v->tone_mix = 1.0f - 0.7f * m2;
        v->noise_mix = lmap(m2, 0.2f, 1.0f);
        v->sweep_g = xmap(m1, 800.0f, 9000.0f);
        v->sweep_mul = jo_decay_mul(len * 0.8f);
        v->res = 0.5f;
        v->amp = 1.0f;
        v->amp_mul = jo_decay_mul(len);
        v->amp2 = 1.0f;
        v->amp2_mul = jo_decay_mul(len * 0.7f);
        v->drive = 1.0f + 6.0f * m3;
        v->outgain = 0.8f * g / jo_tanh(v->drive);
        break;
    }
    }
}

int jd_render(jd_voice_t *v, float *out, uint32_t n)
{
    uint32_t i, k;
    if (!v->active)
        return 0;
    for (i = 0; i < n; i++) {
        float s = 0.0f, nz = jo_noise(&v->rng);
        switch (v->model) {
        case JD_KICK: {
            float f = v->f0 + v->fsweep;
            float body;
            v->fsweep *= v->fsweep_mul;
            v->ph[0] = jo_wrap1(v->ph[0] + f * JO_INV_SR);
            v->ph[1] = jo_wrap1(v->ph[1] + v->inc[1]);
            body = jo_sin_turns(v->ph[0]);
            s = (body * v->amp + jo_sin_turns(v->ph[1]) * v->click + jo_hp1(&v->hp, nz, 0.5f) * v->amp2) * v->drive;
            s = jo_tanh(s * 1.2f) * v->outgain;
            v->amp *= v->amp_mul;
            v->click *= v->click_mul;
            v->amp2 *= v->amp2_mul;
            break;
        }
        case JD_SNARE:
        case JD_GHOST: {
            float f = v->f0 + v->fsweep;
            jo_svf_out_t o, o2;
            float body;
            v->fsweep *= v->fsweep_mul;
            v->ph[0] = jo_wrap1(v->ph[0] + f * JO_INV_SR);
            v->ph[1] = jo_wrap1(v->ph[1] + f * 1.47f * JO_INV_SR);
            body = (jo_sin_turns(v->ph[0]) + 0.45f * jo_sin_turns(v->ph[1])) * v->amp * v->tone_mix;
            o = jo_svf(&v->svf, nz, v->bright_g, v->res);
            o2 = jo_svf(&v->svf2, nz, v->sweep_g, 0.35f);
            s = body + (o.bp + 0.7f * o.hp) * v->amp2 * v->noise_mix + o2.bp * v->snap * 1.6f +
                jo_hp1(&v->hp, nz, 0.6f) * v->click;
            s = jo_tanh(s * v->drive) * v->outgain;
            v->amp *= v->amp_mul;
            v->amp2 *= v->amp2_mul;
            v->snap *= v->snap_mul;
            v->click *= v->click_mul;
            break;
        }
        case JD_HATS: {
            float sq = 0.0f;
            jo_svf_out_t o;
            for (k = 0; k < 6u; k++) {
                v->ph[k] = jo_wrap1(v->ph[k] + v->inc[k]);
                sq += v->ph[k] < 0.5f ? 1.0f : -1.0f;
            }
            o = jo_svf(&v->svf, sq * (1.0f / 6.0f) * v->tone_mix + nz * v->noise_mix, v->bright_g, v->res);
            o = jo_svf(&v->svf2, o.hp, v->bright_g * 1.4f, 0.1f);
            s = (o.hp + 0.3f * o.bp) * v->amp2 * v->outgain;
            v->amp2 *= v->amp2_mul;
            break;
        }
        case JD_PERC: {
            float f = v->f0 + v->fsweep;
            jo_svf_out_t o;
            v->fsweep *= v->fsweep_mul;
            v->ph[0] = jo_wrap1(v->ph[0] + f * JO_INV_SR);
            o = jo_svf(&v->svf, nz + v->click * 8.0f, v->bright_g, v->res);
            /* the shaker's swell: amp2 rises from 0 toward amp */
            if (v->amp2 == 0.0f && v->noise_mix >= 1.0f) v->amp2 = 0.0001f;
            if (v->noise_mix >= 1.0f) v->amp2 += (v->amp - v->amp2) * 0.01f;
            s = jo_tanh(jo_sin_turns(v->ph[0]) * v->tone_mix * v->amp + o.bp * v->noise_mix * v->amp2) * v->outgain;
            v->amp *= v->amp_mul;
            if (v->noise_mix < 1.0f) v->amp2 *= v->amp2_mul;
            v->click *= v->click_mul;
            break;
        }
        default: {
            float f = v->f0 + v->fsweep;
            jo_svf_out_t o;
            v->fsweep *= v->fsweep_mul;
            v->ph[0] = jo_wrap1(v->ph[0] + f * JO_INV_SR);
            o = jo_svf(&v->svf, nz, jo_svf_g(200.0f + v->sweep_g), v->res);
            v->sweep_g *= v->sweep_mul;
            s = jo_sin_turns(v->ph[0]) * v->tone_mix * v->amp + o.bp * v->noise_mix * v->amp2;
            s = jo_tanh(s * v->drive) * v->outgain;
            v->amp *= v->amp_mul;
            v->amp2 *= v->amp2_mul;
            break;
        }
        }
        s += v->declick;
        v->declick *= 0.985f; /* ~1.5 ms */
        v->last = s;
        out[i] += s;
    }
    /* silent once every envelope is below -80 dB */
    if (jo_absf(v->amp) < 1e-4f && jo_absf(v->amp2) < 1e-4f && jo_absf(v->click) < 1e-4f && jo_absf(v->snap) < 1e-4f &&
        jo_absf(v->declick) < 1e-4f) {
        v->active = 0;
        v->last = 0.0f;
    }
    return v->active;
}
