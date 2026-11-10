/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* JUNGLE OS DSP primitives: everything fixed-size, no allocation, every state denormal- and NaN-guarded. */
#pragma once
#include "jo_math.h"

/* ---------------------------------------------------------------- smoothing --- */
/* one-pole parameter smoother: set the target from the control side, tick per sample or per block */
typedef struct { float y, target, k; } jo_smooth_t;
JO_INLINE void jo_smooth_init(jo_smooth_t *s, float v, float seconds, float rate)
{
    s->y = s->target = v;
    s->k = jo_tau_coef(seconds, rate);
}
JO_INLINE float jo_smooth_tick(jo_smooth_t *s)
{
    s->y += (s->target - s->y) * s->k;
    return s->y;
}

/* ------------------------------------------------------------------ filters --- */
typedef struct { float z; } jo_onepole_t;
JO_INLINE float jo_lp1(jo_onepole_t *f, float x, float k) { f->z = jo_flush(f->z + (x - f->z) * k); return f->z; }
JO_INLINE float jo_hp1(jo_onepole_t *f, float x, float k) { return x - jo_lp1(f, x, k); }
/* one-pole k for a cutoff in Hz (matched-ish, valid well below Nyquist) */
JO_INLINE float jo_lp1_k(float hz) { return jo_clampf(1.0f - jo_expf(-JO_TWO_PI * hz * JO_INV_SR), 0.0f, 1.0f); }

/* DC blocker, ~10 Hz */
typedef struct { float x1, y1; } jo_dcb_t;
JO_INLINE float jo_dcb(jo_dcb_t *d, float x)
{
    float y = x - d->x1 + 0.9986f * d->y1;
    d->x1 = x;
    d->y1 = jo_flush(y);
    return y;
}

/* trapezoidal (TPT / Zavalishin) state-variable filter: stable for any cutoff below Nyquist and any res.
 * g = tan(pi fc / fs) comes from jo_svf_g (a rational tan approximation, fc clamped to 0.45 fs). */
typedef struct { float s1, s2; } jo_svf_t;
typedef struct { float lp, bp, hp; } jo_svf_out_t;
JO_INLINE float jo_svf_g(float hz)
{
    float x = jo_clampf(hz, 10.0f, 0.45f * JO_SR) * (JO_PI * JO_INV_SR); /* [0, 0.45 pi] */
    float x2 = x * x;
    /* tan(x) ~ x (1 - x^2/15... ) Pade 5/4: within 0.1 % up to 0.45 pi */
    return x * (945.0f - 105.0f * x2 + x2 * x2) / (945.0f - 420.0f * x2 + 15.0f * x2 * x2);
}
/* res 0..1 -> damping 2..0.02 (q 0.5 .. 50) */
JO_INLINE jo_svf_out_t jo_svf(jo_svf_t *f, float x, float g, float res)
{
    jo_svf_out_t o;
    float r2 = 2.0f - 1.98f * jo_clampf(res, 0.0f, 1.0f);
    float h = 1.0f / (1.0f + g * (r2 + g));
    o.hp = (x - (r2 + g) * f->s1 - f->s2) * h;
    o.bp = g * o.hp + f->s1;
    f->s1 = jo_sane(g * o.hp + o.bp);
    o.lp = g * o.bp + f->s2;
    f->s2 = jo_sane(g * o.bp + o.lp);
    return o;
}

/* --------------------------------------------------------------- envelopes --- */
/* exponential decay: the drum workhorse. level *= mul per sample; mul from a -60 dB time */
JO_INLINE float jo_decay_mul(float seconds_60db)
{
    if (seconds_60db < 0.0005f) seconds_60db = 0.0005f;
    return jo_expf(-6.9077553f / (seconds_60db * JO_SR));
}

/* attack-decay-sustain-release with linear attack, exponential segments; gate on/off */
typedef struct {
    float level;
    float a_inc, d_k, s, r_k;
    uint8_t stage; /* 0 idle 1 attack 2 decay/sustain 3 release */
} jo_adsr_t;
JO_INLINE void jo_adsr_set(jo_adsr_t *e, float a_s, float d_s, float s, float r_s)
{
    e->a_inc = 1.0f / (jo_maxf(a_s, 0.0005f) * JO_SR);
    e->d_k = jo_tau_coef(jo_maxf(d_s, 0.001f) * 0.25f, JO_SR);
    e->s = jo_clampf(s, 0.0f, 1.0f);
    e->r_k = jo_tau_coef(jo_maxf(r_s, 0.002f) * 0.25f, JO_SR);
}
JO_INLINE void jo_adsr_gate(jo_adsr_t *e, int on) { e->stage = on ? 1u : (e->stage ? 3u : 0u); }
JO_INLINE float jo_adsr_tick(jo_adsr_t *e)
{
    switch (e->stage) {
    case 1:
        e->level += e->a_inc;
        if (e->level >= 1.0f) { e->level = 1.0f; e->stage = 2; }
        break;
    case 2: e->level += (e->s - e->level) * e->d_k; break;
    case 3:
        e->level -= e->level * e->r_k;
        if (e->level < 1e-5f) { e->level = 0.0f; e->stage = 0; }
        break;
    default: e->level = 0.0f; break;
    }
    return e->level;
}

/* ------------------------------------------------------------- oscillators --- */
/* polyBLEP residual for a discontinuity at phase 0 of a phasor with increment dt */
JO_INLINE float jo_blep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
JO_INLINE float jo_saw_blep(float ph, float dt) { return 2.0f * ph - 1.0f - jo_blep(ph, dt); }
JO_INLINE float jo_wrap1(float ph) { return ph >= 1.0f ? ph - 1.0f : (ph < 0.0f ? ph + 1.0f : ph); }

/* white noise from the shared PRNG, [-1, 1) */
JO_INLINE float jo_noise(jo_rng_t *r) { return jo_rng_bi(r); }

/* ---------------------------------------------------------------- dynamics --- */
/* peak envelope follower with separate attack/release coefficients */
typedef struct { float env; } jo_follow_t;
JO_INLINE float jo_follow(jo_follow_t *f, float x, float ka, float kr)
{
    float a = jo_absf(x);
    f->env += (a - f->env) * (a > f->env ? ka : kr);
    f->env = jo_flush(f->env);
    return f->env;
}

/* -------------------------------------------------------------- delay line --- */
/* power-of-two circular buffer over caller-owned storage (a static or .pool array) */
typedef struct { float *buf; uint32_t mask, w; } jo_delay_t;
JO_INLINE void jo_delay_init(jo_delay_t *d, float *buf, uint32_t len_pow2)
{
    uint32_t i;
    d->buf = buf;
    d->mask = len_pow2 - 1u;
    d->w = 0;
    for (i = 0; i < len_pow2; i++) buf[i] = 0.0f;
}
JO_INLINE void jo_delay_write(jo_delay_t *d, float x) { d->buf[d->w] = x; d->w = (d->w + 1u) & d->mask; }
JO_INLINE float jo_delay_tap(const jo_delay_t *d, uint32_t samples_back) /* 1 = the last written */
{
    return d->buf[(d->w - samples_back) & d->mask];
}
JO_INLINE float jo_delay_tapf(const jo_delay_t *d, float samples_back) /* linear interpolation, >= 1 */
{
    uint32_t i;
    float f;
    if (samples_back < 1.0f) samples_back = 1.0f;
    i = (uint32_t)samples_back;
    f = samples_back - (float)i;
    return jo_lerpf(jo_delay_tap(d, i), jo_delay_tap(d, i + 1u), f);
}
