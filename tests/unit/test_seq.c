/* SPDX-License-Identifier: MPL-2.0 */
#include "../jo_test.h"
#include "../../shared/seq/jo_seq.h"

static js_pattern_t P;
static js_clock_t C;
static js_seq_t S;
static js_queue_t Q;

/* runs the sequencer for `samples` in blocks, collecting fired events in order */
static uint32_t collect(uint32_t samples, js_event_t *out, uint32_t max, const js_hooks_t *h)
{
    uint32_t got = 0, done = 0;
    while (done < samples) {
        uint32_t n = 128, t0 = C.now;
        js_event_t e;
        js_run(&S, &C, &Q, n, h);
        while (js_queue_pop_due(&Q, t0 + n, &e))
            if (got < max) out[got++] = e;
        done += n;
    }
    return got;
}

static void setup(float bpm)
{
    js_pattern_clear(&P, 1, 16);
    js_clock_init(&C, bpm);
    js_seq_init(&S, &P);
    js_queue_clear(&Q);
    js_clock_start(&C);
}

TEST(test_step_timing_174)
{
    static js_event_t ev[64];
    uint32_t i, n;
    float sps;
    int ok = 1;
    setup(174.0f);
    for (i = 0; i < 16; i++) P.t[0].s[i].flags = JS_ON;
    sps = js_clock_samples_per_step(&C);
    CHECKF(fabsf(sps - 44117.647f * 60.0f / (174.0f * 4.0f)) < 0.05f, "sps %f", (double)sps);
    n = collect((uint32_t)(sps * 32.2f), ev, 64, 0);
    CHECKF(n == 32, "events %u", n);
    for (i = 1; i < n; i++) {
        float d = (float)(ev[i].due - ev[0].due) - sps * (float)i;
        if (fabsf(d) > 1.0f) { ok = 0; printf("  step %u off by %f samples\n", i, (double)d); break; }
        if (ev[i].step != i % 16) ok = 0;
    }
    CHECK(ok);
    /* the start latency: step 0 lands half a step after START */
    CHECKF(fabsf((float)ev[0].due - 0.5f * sps) <= 1.5f, "first due %u", ev[0].due);
}

TEST(test_long_run_no_drift)
{
    /* 20 minutes at 174: the 16th grid stays within a sample of the ideal (integer time base) */
    uint32_t steps = 0, last = 0, k;
    js_event_t e;
    setup(174.0f);
    P.t[0].s[0].flags = JS_ON; /* one event per bar */
    for (k = 0; k < (uint32_t)(20.0f * 60.0f * 44117.647f / 128.0f); k++) {
        uint32_t t0 = C.now;
        js_run(&S, &C, &Q, 128, 0);
        while (js_queue_pop_due(&Q, t0 + 128, &e)) { last = e.due; steps = e.abs_step; }
    }
    {
        /* the clock's own rate (inc) is the reference: the grid follows it exactly */
        float ideal = (0.5f + (float)steps) * (4294967296.0f / (float)C.inc);
        float err = fabsf((float)last - ideal);
        CHECKF(err < 2.0f + ideal * 1e-6f, "drift %f samples after %u steps", (double)err, steps);
    }
}

TEST(test_swing_micro_ratchet)
{
    static js_event_t ev[64];
    uint32_t n;
    float sps;
    setup(170.0f);
    P.swing = 75; /* the second 16th of a pair, half a step late */
    P.t[0].s[0].flags = JS_ON;
    P.t[0].s[1].flags = JS_ON;
    P.t[0].s[2].flags = JS_ON;
    P.t[0].s[2].micro = -64; /* half a step early */
    P.t[0].s[4].flags = JS_ON;
    P.t[0].s[4].ratch = 3;   /* four hits */
    sps = js_clock_samples_per_step(&C);
    n = collect((uint32_t)(sps * 6.0f), ev, 64, 0);
    CHECKF(n == 7, "events %u", n);
    if (n == 7) {
        float t0 = (float)ev[0].due;
        CHECKF(fabsf((float)ev[1].due - t0 - 1.5f * sps) < 1.5f, "swing %f", (double)((float)ev[1].due - t0));
        CHECKF(fabsf((float)ev[2].due - t0 - 1.5f * sps) < 1.5f, "micro %f", (double)((float)ev[2].due - t0));
        CHECKF(fabsf((float)ev[3].due - t0 - 4.0f * sps) < 1.5f, "ratchet 0");
        CHECKF(fabsf((float)ev[6].due - t0 - 4.75f * sps) < 1.5f, "ratchet 3 %f", (double)((float)ev[6].due - t0));
        CHECK(ev[6].hit == 3 && ev[6].nhits == 4);
    }
}

TEST(test_probability_deterministic)
{
    static js_event_t a[256], b[256];
    uint32_t na, nb, i;
    int same = 1;
    float sps;
    setup(174.0f);
    for (i = 0; i < 16; i++) { P.t[0].s[i].flags = JS_ON; P.t[0].s[i].prob = 50; }
    P.seed = 1234;
    sps = js_clock_samples_per_step(&C);
    na = collect((uint32_t)(sps * 64.0f), a, 256, 0);
    setup(174.0f);
    for (i = 0; i < 16; i++) { P.t[0].s[i].flags = JS_ON; P.t[0].s[i].prob = 50; }
    P.seed = 1234;
    nb = collect((uint32_t)(sps * 64.0f), b, 256, 0);
    CHECK(na == nb);
    for (i = 0; i < na && i < nb; i++) if (a[i].abs_step != b[i].abs_step) same = 0;
    CHECK(same);
    CHECKF(na > 16 && na < 48, "50%% of 64 steps gave %u", na);
}

TEST(test_mute_solo_locks)
{
    static js_event_t ev[64];
    uint32_t n;
    js_step_t st;
    setup(174.0f);
    js_pattern_clear(&P, 3, 16);
    P.t[0].s[0].flags = P.t[1].s[0].flags = P.t[2].s[0].flags = JS_ON;
    S.mute = 1u << 1;
    n = collect(5000, ev, 64, 0);
    CHECKF(n == 2, "mute: %u", n);
    setup(174.0f);
    js_pattern_clear(&P, 3, 16);
    P.t[0].s[0].flags = P.t[1].s[0].flags = P.t[2].s[0].flags = JS_ON;
    S.solo = 1u << 2;
    n = collect(5000, ev, 64, 0);
    CHECK(n == 1 && ev[0].track == 2);
    js_step_clear(&st);
    CHECK(js_step_set_lock(&st, 3, 200) == 0 && js_step_set_lock(&st, 5, 7) == 0);
    CHECK(js_step_set_lock(&st, 9, 1) == -1);
    CHECK(js_step_lock(&st, 3) == 200 && js_step_lock(&st, 5) == 7 && js_step_lock(&st, 9) == -1);
    CHECK(js_step_set_lock(&st, 3, -1) == 0 && js_step_lock(&st, 3) == -1);
}

TEST(test_midi_clock_follow)
{
    /* clocks at 172 BPM: 24 PPQN -> interval 60e6 / (172 * 24) us */
    uint32_t i, t24 = 1000;
    float us = 60.0e6f / (172.0f * 24.0f);
    js_clock_init(&C, 120.0f);
    js_clock_midi_start(&C);
    for (i = 0; i < 200; i++) {
        js_clock_midi_tick(&C, t24);
        t24 += (uint32_t)(us * 24.0f + (i & 1 ? 300.0f : -300.0f)); /* +-12.5 us jitter */
    }
    CHECKF(fabsf(C.bpm - 172.0f) < 0.3f, "bpm %f", (double)C.bpm);
}

void run_seq(void)
{
    RUN(test_step_timing_174);
    RUN(test_long_run_no_drift);
    RUN(test_swing_micro_ratchet);
    RUN(test_probability_deterministic);
    RUN(test_mute_solo_locks);
    RUN(test_midi_clock_follow);
}
