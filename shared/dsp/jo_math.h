/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* JUNGLE OS math: single precision only (the AC79's FPU; fm1-base's build rejects double), every helper
 * always inline so DSP may later move into RAM code (PLAT_RAM_CODE cannot call XIP functions).
 * Accuracy targets are audio-grade, not libm-grade; tests/unit/test_math.c checks the error bounds. */
#pragma once
#include <stdint.h>

#define JO_INLINE static inline __attribute__((always_inline))
#define JO_PI 3.14159265358979f
#define JO_TWO_PI 6.28318530717959f

/* the FM-1's real rate: 24 MHz / 544 (felucca-bsp, fm1-base device/main_fm1.c) */
#define JO_SR 44117.647f
#define JO_INV_SR (1.0f / JO_SR)

typedef union { float f; uint32_t u; int32_t i; } jo_bits_t;

JO_INLINE float jo_absf(float x) { jo_bits_t b; b.f = x; b.u &= 0x7FFFFFFFu; return b.f; }
JO_INLINE float jo_minf(float a, float b) { return a < b ? a : b; }
JO_INLINE float jo_maxf(float a, float b) { return a > b ? a : b; }
JO_INLINE float jo_clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
JO_INLINE int32_t jo_clampi(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : (x > hi ? hi : x); }
JO_INLINE float jo_lerpf(float a, float b, float t) { return a + (b - a) * t; }
/* NaN-safe finite check: NaN and +-Inf have an all-ones exponent */
JO_INLINE int jo_finite(float x) { jo_bits_t b; b.f = x; return (b.u & 0x7F800000u) != 0x7F800000u; }
/* denormal and tiny-value flush, for filter and feedback states */
JO_INLINE float jo_flush(float x) { return jo_absf(x) < 1e-15f ? 0.0f : x; }
/* replaces a non-finite state by 0: the last line of defence of a feedback path */
JO_INLINE float jo_sane(float x) { return jo_finite(x) ? jo_flush(x) : 0.0f; }

JO_INLINE float jo_floorf(float x)
{
    int32_t i = (int32_t)x;
    float f = (float)i;
    return f > x ? f - 1.0f : f;
}

/* 2^x for x in about [-126, 126]: integer part in the exponent, a 5th-order least-squares polynomial for the
 * fraction (max rel. error ~1e-7, tests/unit/test_math.c) */
JO_INLINE float jo_exp2f(float x)
{
    jo_bits_t b;
    float fl, f, p;
    int32_t n;
    x = jo_clampf(x, -126.0f, 126.0f);
    fl = jo_floorf(x);
    f = x - fl;
    n = (int32_t)fl;
    p = 0.999999927f + f * (0.693152965f + f * (0.240154545f + f * (0.0558235814f + f * (0.00899259431f + f * 0.00187623429f))));
    b.u = (uint32_t)(n + 127) << 23;
    return b.f * p;
}
JO_INLINE float jo_expf(float x) { return jo_exp2f(x * 1.44269504f); }

/* log2(x), x > 0: exponent plus a 5th-order polynomial on the mantissa (abs. error ~2e-5) */
JO_INLINE float jo_log2f(float x)
{
    jo_bits_t b;
    float m, e;
    b.f = x > 1e-30f ? x : 1e-30f;
    e = (float)((int32_t)((b.u >> 23) & 0xFFu) - 127);
    b.u = (b.u & 0x007FFFFFu) | 0x3F800000u;
    m = b.f; /* [1, 2) */
    return e + (-2.7941675f + (5.06979934f + (-3.52027094f + (1.61020814f + (-0.409484261f + 0.0439295708f * m) * m) * m) * m) * m);
}

JO_INLINE float jo_db2lin(float db) { return jo_exp2f(db * 0.166096405f); }
JO_INLINE float jo_lin2db(float g) { return 6.02059991f * jo_log2f(g); }

/* MIDI note -> Hz */
JO_INLINE float jo_mtof(float note) { return 440.0f * jo_exp2f((note - 69.0f) * (1.0f / 12.0f)); }

/* sine of a phase in turns [0, 1): parabola pair with a correction (max abs error ~0.001, smooth) */
JO_INLINE float jo_sin_turns(float ph)
{
    float t, s;
    ph -= jo_floorf(ph);
    t = ph < 0.5f ? ph : ph - 1.0f;               /* [-0.5, 0.5) turns */
    s = 8.0f * t - 16.0f * t * jo_absf(t);        /* parabola through the sine's zeros and peaks (~0.056) */
    return s * (0.775f + 0.225f * jo_absf(s));    /* the usual refinement: max error ~0.001 */
}
JO_INLINE float jo_sinf(float rad) { return jo_sin_turns(rad * (1.0f / JO_TWO_PI)); }

/* soft saturators, all bounded to [-1, 1] for any finite input */
JO_INLINE float jo_tanh(float x) /* rational Pade, clamped: max error ~0.002, odd, monotonic */
{
    float x2;
    x = jo_clampf(x, -4.5f, 4.5f);
    x2 = x * x;
    return jo_clampf(x * (27.0f + x2) / (27.0f + 9.0f * x2), -1.0f, 1.0f);
}
JO_INLINE float jo_softclip(float x) /* cubic, unity slope at 0, hard ceiling at +-1 from |x| >= 1.5 */
{
    if (x > 1.5f) return 1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (4.0f / 27.0f) * x * x * x;
}
JO_INLINE float jo_hardclip(float x) { return jo_clampf(x, -1.0f, 1.0f); }
/* asymmetric drive: even harmonics, bounded (DC is removed downstream) */
JO_INLINE float jo_asym(float x, float bias) { return jo_tanh(x + bias) - jo_tanh(bias); }
/* triangle wavefolder, bounded to [-1, 1] */
JO_INLINE float jo_fold(float x)
{
    float t = x * 0.25f + 0.25f;
    t -= jo_floorf(t);
    return 4.0f * jo_absf(t - 0.5f) - 1.0f;
}

JO_INLINE float jo_sqrtf(float x)
{
    jo_bits_t b;
    float y;
    if (x <= 0.0f) return 0.0f;
    b.f = x;
    b.u = 0x5F3759DFu - (b.u >> 1);
    y = b.f;
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    return x * y;
}

/* one-pole coefficient for a time constant in seconds (to 1 - 1/e), from the block or sample rate */
JO_INLINE float jo_tau_coef(float seconds, float rate)
{
    if (seconds <= 0.0f) return 1.0f;
    return 1.0f - jo_expf(-1.0f / (seconds * rate));
}

/* deterministic PRNG: xorshift32, never 0 */
typedef struct { uint32_t s; } jo_rng_t;
JO_INLINE void jo_rng_seed(jo_rng_t *r, uint32_t seed) { r->s = seed ? seed : 0x9E3779B9u; }
JO_INLINE uint32_t jo_rng_u32(jo_rng_t *r)
{
    uint32_t x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return r->s = x;
}
JO_INLINE float jo_rng_unit(jo_rng_t *r) { return (float)(jo_rng_u32(r) >> 8) * (1.0f / 16777216.0f); } /* [0,1) */
JO_INLINE float jo_rng_bi(jo_rng_t *r) { return jo_rng_unit(r) * 2.0f - 1.0f; }                         /* [-1,1) */
/* [0, n) for n <= 65536, without a 64-bit multiply (the pi32v2 erratum: felucca-bsp docs/CPU.md) */
JO_INLINE uint32_t jo_rng_below(jo_rng_t *r, uint32_t n) { return n ? ((jo_rng_u32(r) >> 16) * n) >> 16 : 0u; }
