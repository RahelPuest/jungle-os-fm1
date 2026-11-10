/* SPDX-License-Identifier: MPL-2.0 */
#include "../jo_test.h"
#include "../../shared/dsp/jo_dsp.h"

TEST(test_math_accuracy)
{
    float x, worst = 0.0f;
    for (x = -10.0f; x < 10.0f; x += 0.013f) {
        float e = fabsf(jo_exp2f(x) - exp2f(x)) / exp2f(x);
        if (e > worst) worst = e;
    }
    CHECKF(worst < 1e-5f, "exp2 rel err %g", (double)worst);
    worst = 0.0f;
    for (x = 0.001f; x < 1000.0f; x *= 1.01f) {
        float e = fabsf(jo_log2f(x) - log2f(x));
        if (e > worst) worst = e;
    }
    CHECKF(worst < 2e-4f, "log2 abs err %g", (double)worst);
    worst = 0.0f;
    for (x = -2.0f; x < 2.0f; x += 0.0007f) {
        float e = fabsf(jo_sin_turns(x) - sinf(6.2831853f * x));
        if (e > worst) worst = e;
    }
    CHECKF(worst < 0.0015f, "sin abs err %g", (double)worst);
    worst = 0.0f;
    for (x = -6.0f; x < 6.0f; x += 0.001f) {
        float e = fabsf(jo_tanh(x) - tanhf(x));
        if (e > worst) worst = e;
    }
    CHECKF(worst < 0.03f, "tanh abs err %g", (double)worst);
}

TEST(test_saturators_bounded)
{
    float x;
    int ok = 1, mono = 1;
    float prev = -2.0f;
    for (x = -1e6f; x < 1e6f; x = x < -1.0f ? x * 0.9f : x > 1.0f ? x * 1.1f : x + 0.001f) {
        float a = jo_tanh(x), b = jo_softclip(x), c = jo_fold(x * 0.001f), d = jo_asym(x, 0.3f);
        if (!(fabsf(a) <= 1.0f && fabsf(b) <= 1.0f && fabsf(c) <= 1.0001f && fabsf(d) <= 2.0f)) ok = 0;
        if (a < prev - 1e-6f) mono = 0;
        prev = a;
        if (x > -1.0f && x < -0.9999f) x = -1.0f;
    }
    CHECK(ok);
    CHECK(mono);
    CHECK(!jo_finite(NAN) && !jo_finite(INFINITY) && jo_finite(1.0f));
    CHECK(jo_sane(NAN) == 0.0f && jo_sane(1e-30f) == 0.0f);
}

TEST(test_svf_stable)
{
    jo_svf_t f = {0, 0};
    jo_rng_t r;
    float peak = 0.0f, hz;
    int i, ok = 1;
    jo_rng_seed(&r, 1);
    for (hz = 20.0f; hz < 22000.0f; hz *= 1.5f) {
        float g = jo_svf_g(hz);
        for (i = 0; i < 4000; i++) {
            jo_svf_out_t o = jo_svf(&f, jo_noise(&r), g, 1.0f);
            if (!jo_finite(o.lp) || !jo_finite(o.bp) || !jo_finite(o.hp)) ok = 0;
            if (fabsf(o.bp) > peak) peak = fabsf(o.bp);
        }
    }
    CHECK(ok);
    CHECKF(peak < 200.0f, "bp peak at max res %g", (double)peak);
}

TEST(test_rng_deterministic)
{
    jo_rng_t a, b;
    int i, same = 1, inrange = 1;
    jo_rng_seed(&a, 42);
    jo_rng_seed(&b, 42);
    for (i = 0; i < 1000; i++) {
        uint32_t x = jo_rng_below(&a, 7);
        if (x != jo_rng_below(&b, 7)) same = 0;
        if (x >= 7) inrange = 0;
    }
    CHECK(same && inrange);
}

void run_math(void)
{
    RUN(test_math_accuracy);
    RUN(test_saturators_bounded);
    RUN(test_svf_stable);
    RUN(test_rng_deterministic);
}
