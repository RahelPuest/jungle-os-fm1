/* SPDX-License-Identifier: MPL-2.0 */
#include "../jo_test.h"
#include "../../shared/drums/jo_drums.h"
#include "../../shared/sampler/jo_break.h"
#include "../../shared/sweetspot/jo_graph.h"
#include "../../shared/link/jo_link.h"
#include "../../shared/store/jo_store.h"
#include "../../shared/fx/jo_fx.h"
#include "../../shared/synth/jo_synth.h"

TEST(test_drums_bounded_and_decay)
{
    /* every model, macros at 0, 0.5, 1 and tune at the extremes: finite, bounded, silent within 5 s */
    static float buf[44118 * 5];
    int m, ok = 1, quiet = 1;
    float worst = 0.0f;
    for (m = 0; m < JD_NMODELS; m++) {
        int c;
        for (c = 0; c < 27; c++) {
            jd_voice_t v;
            jd_sound_t s;
            uint32_t i, n = sizeof buf / sizeof buf[0];
            jd_voice_init(&v, m, 1);
            jd_sound_default(&s, m);
            s.m[0] = (float)(c % 3) * 0.5f;
            s.m[1] = (float)(c / 3 % 3) * 0.5f;
            s.m[2] = (float)(c / 9 % 3) * 0.5f;
            s.m[3] = 1.0f;
            s.tune = c & 1 ? 24.0f : -24.0f;
            s.level = 1.0f;
            s.variant = (uint8_t)(c % JD_PERC_NVAR);
            memset(buf, 0, sizeof buf);
            jd_trigger(&v, &s, 1.0f, c & 1);
            jd_render(&v, buf, n);
            for (i = 0; i < n; i++) {
                if (!jo_finite(buf[i])) ok = 0;
                if (fabsf(buf[i]) > worst) worst = fabsf(buf[i]);
            }
            { float w = 0; for (i = 0; i < n; i++) w = fmaxf(w, fabsf(buf[i])); if (w > 1.2f) printf("  model %d combo %d peak %.2f\n", m, c, (double)w); }
            if (v.active) { quiet = 0; printf("  model %d combo %d still sounding after 5 s\n", m, c); }
        }
    }
    CHECK(ok);
    CHECKF(worst <= 1.6f, "drum peak %f", (double)worst);
    CHECK(quiet);
}

TEST(test_drum_retrigger_declick)
{
    /* a retrigger mid-decay: the step between consecutive samples stays small (the de-click tail) */
    static float buf[8000];
    jd_voice_t v;
    jd_sound_t s;
    uint32_t i;
    float jump = 0.0f;
    jd_voice_init(&v, JD_KICK, 1);
    jd_sound_default(&s, JD_KICK);
    jd_trigger(&v, &s, 1.0f, 0);
    jd_render(&v, buf, 3001);
    jd_trigger(&v, &s, 1.0f, 0);
    jd_render(&v, buf + 3001, 4000);
    for (i = 2995; i < 3010; i++) if (fabsf(buf[i + 1] - buf[i]) > jump) jump = fabsf(buf[i + 1] - buf[i]);
    CHECKF(jump < 0.35f, "retrigger step %f", (double)jump);
}

/* a bank image with one break, for the parser */
static uint8_t img[JB_HDR + JB_ENTRY + 4000];
static uint32_t make_bank(void)
{
    uint32_t i, off = JB_HDR + JB_ENTRY, frames = 1900, total = off + frames * 2u, crc;
    memset(img, 0, sizeof img);
    memcpy(img, "JOB1", 4);
    img[4] = 1; img[6] = 1;
    memcpy(img + 8, &total, 4);
    memcpy(img + JB_HDR, "TESTBRK", 7);
    memcpy(img + JB_HDR + 16, &off, 4);
    memcpy(img + JB_HDR + 20, &frames, 4);
    i = 22050; memcpy(img + JB_HDR + 24, &i, 4);
    i = 17000; memcpy(img + JB_HDR + 28, &i, 2);
    img[JB_HDR + 30] = 1; img[JB_HDR + 31] = 4;
    for (i = 0; i < 4; i++) {
        uint32_t st = i * 475u;
        memcpy(img + JB_HDR + 36 + 4 * i, &st, 4);
        img[JB_HDR + 164 + i] = (uint8_t)(i == 0 ? JB_CLASS_KICK : JB_CLASS_HAT);
        img[JB_HDR + 196 + i] = 128;
    }
    for (i = 0; i < frames; i++) { int16_t v = (int16_t)(i * 7 % 20000 - 10000); memcpy(img + off + 2 * i, &v, 2); }
    crc = jb_crc32(img + JB_HDR, total - JB_HDR);
    memcpy(img + 12, &crc, 4);
    return total;
}

TEST(test_bank_parse_and_reject)
{
    jb_break_t b[JB_MAXBREAKS];
    uint32_t len = make_bank();
    CHECK(jb_bank_parse(img, len, b, JB_MAXBREAKS) == 1);
    CHECK(b[0].nslices == 4 && b[0].start[4] == 1900 && b[0].cls[0] == JB_CLASS_KICK && fabsf(b[0].bpm - 170.0f) < 0.01f);
    CHECK(jb_bank_parse(img, len - 1, b, JB_MAXBREAKS) == -3);      /* truncated */
    img[len - 3] ^= 0x55;
    CHECK(jb_bank_parse(img, len, b, JB_MAXBREAKS) == -4);          /* corrupted PCM: CRC */
    img[len - 3] ^= 0x55;
    img[0] = 'X';
    CHECK(jb_bank_parse(img, len, b, JB_MAXBREAKS) == -1);
    CHECK(jb_bank_parse(0, 0, b, JB_MAXBREAKS) == -1);
    /* a slice table out of order is refused even with a good CRC */
    make_bank();
    { uint32_t bad = 2000, crc; memcpy(img + JB_HDR + 36 + 8, &bad, 4);
      crc = jb_crc32(img + JB_HDR, len - JB_HDR); memcpy(img + 12, &crc, 4); }
    CHECK(jb_bank_parse(img, len, b, JB_MAXBREAKS) == -5);
}

TEST(test_break_player_bounds)
{
    /* every slice, both directions, extreme pitch, microloops: finite, never past the buffer, ends by itself */
    static float out[44118];
    jb_break_t b[1];
    jb_player_t p;
    uint32_t len = make_bank(), s, i;
    int ok = 1, ended = 1;
    jb_bank_parse(img, len, b, 1);
    jb_player_init(&p, &b[0]);
    for (s = 0; s < 9; s++) {
        float semis = s & 1 ? 24.0f : -24.0f;
        memset(out, 0, sizeof out);
        p.gate = (float)(s % 3) * 0.4f + 0.1f;
        jb_play(&p, s, 1.0f, (int)(s & 2), semis);
        if (s == 4) jb_microloop(&p, 100.0f);
        jb_render(&p, out, 20000);
        if (s == 4) { jb_microloop(&p, 0.0f); jb_render(&p, out, 20000); }
        for (i = 0; i < 20000; i++) if (!jo_finite(out[i]) || fabsf(out[i]) > 1.01f) ok = 0;
        if (p.v[0].active || p.v[1].active) ended = 0;
    }
    CHECK(ok);
    CHECK(ended);
}

TEST(test_break_crossfade_no_click)
{
    /* a slice replaced mid-play: the output's sample-to-sample step stays near the material's own */
    static int16_t pcm[30000];
    static float out[4000];
    jb_break_t b;
    jb_player_t p;
    uint32_t i;
    float jump = 0.0f;
    for (i = 0; i < 30000; i++) pcm[i] = (int16_t)(20000.0f * sinf((float)i * 0.01f));
    b.pcm = pcm; b.frames = 30000; b.rate = 44118; b.bpm = 170; b.bars = 1;
    jb_slice_even(&b, 4);
    jb_player_init(&p, &b);
    jb_play(&p, 0, 1.0f, 0, 0.0f);
    jb_render(&p, out, 1000);
    jb_play(&p, 2, 1.0f, 0, 0.0f);
    jb_render(&p, out + 1000, 1000);
    for (i = 990; i < 1200; i++) if (fabsf(out[i + 1] - out[i]) > jump) jump = fabsf(out[i + 1] - out[i]);
    CHECKF(jump < 0.03f, "crossfade step %f", (double)jump);
}

TEST(test_testbreak)
{
    static int16_t buf[70000];
    jb_break_t b;
    uint32_t i;
    int32_t pk = 0;
    CHECK(jb_testbreak_frames(22059) < 70000);
    jb_testbreak_make(&b, buf, 22059);
    for (i = 0; i < b.frames; i++) if (abs(buf[i]) > pk) pk = abs(buf[i]);
    CHECK(b.nslices == 16 && b.cls[0] == JB_CLASS_KICK && b.cls[4] == JB_CLASS_SNARE);
    CHECKF(pk > 8000 && pk < 32767, "test break peak %d", pk);
}

static const jg_param_t GP[3] = {{"F", 50.0f, 5000.0f, JG_LOG, 0.2f}, {"X", -1.0f, 1.0f, JG_LIN, 0.2f}, {"D", 0.0f, 3.0f, JG_DISCRETE, 0}};
TEST(test_graph)
{
    jg_graph_t g;
    jg_eval_t ev;
    float t, prev = 0.0f, v[JG_MAXPARAMS], w[JG_MAXPARAMS];
    jg_history_t h;
    int mono = 1, i, inb = 1;
    memset(&g, 0, sizeof g);
    g.param = GP; g.nparams = 3; g.nnodes = 3;
    g.node[0].v[0] = 0.0f; g.node[0].v[1] = 0.0f; g.node[0].v[2] = 0.0f; g.node[0].trim_db = 0.0f;
    g.node[1].v[0] = 0.5f; g.node[1].v[1] = 1.0f; g.node[1].v[2] = 1.0f; g.node[1].trim_db = -3.0f;
    g.node[2].v[0] = 1.0f; g.node[2].v[1] = 0.2f; g.node[2].v[2] = 0.67f; g.node[2].trim_db = -6.0f;
    g.nedges = 2; g.edge[0].a = 0; g.edge[0].b = 1; g.edge[1].a = 1; g.edge[1].b = 2;
    g.npath = 3; g.path[0] = 0; g.path[1] = 1; g.path[2] = 2;
    CHECK(jg_validate(&g) == 0);
    for (t = 0.0f; t <= 1.0f; t += 0.001f) {
        jg_eval(&g, t, &ev);
        if (ev.phys[0] < prev - 1e-3f) mono = 0; /* the frequency rises monotonically along this path */
        prev = ev.phys[0];
        if (ev.phys[0] < 49.9f || ev.phys[0] > 5000.1f || ev.phys[2] != floorf(ev.phys[2])) inb = 0;
    }
    CHECK(mono && inb);
    jg_eval(&g, 0.5f, &ev);
    CHECKF(fabsf(ev.phys[0] - 500.0f) < 5.0f, "log midpoint %f", (double)ev.phys[0]); /* geometric mean */
    CHECK(fabsf(ev.trim_db + 3.0f) < 0.01f);
    CHECK(jg_snap(&g, 0.3f) == 1 && jg_snap(&g, 0.2f) == 0);
    /* mutation: bounded by radius x slew, deterministic, in range */
    for (i = 0; i < 100; i++) {
        jg_mutate(&g, g.node[1].v, v, 0.5f, (uint32_t)i);
        jg_mutate(&g, g.node[1].v, w, 0.5f, (uint32_t)i);
        if (memcmp(v, w, sizeof(float) * 3) || fabsf(v[0] - 0.5f) > 0.1f + 1e-6f || v[1] < 0.0f || v[1] > 1.0f) inb = 0;
    }
    CHECK(inb);
    memset(&h, 0, sizeof h);
    jg_history_push(&h, g.node[0].v, 3);
    jg_history_push(&h, g.node[2].v, 3);
    CHECK(jg_history_pop(&h, v, 3) == 0 && v[0] == 1.0f);
    CHECK(jg_history_pop(&h, v, 3) == 0 && v[0] == 0.0f);
    CHECK(jg_history_pop(&h, v, 3) == -1);
    g.npath = 3; g.path[1] = 2; g.path[2] = 0; /* 2 -> 0 has no edge */
    CHECK(jg_validate(&g) == 7);
}

TEST(test_link_protocol)
{
    jl_link_t tx, rx;
    jl_event_t ev;
    uint32_t p[4], n;
    jl_init(&tx, 15);
    jl_init(&rx, 15);
    n = jl_scene(&tx, 5, JL_Q_BAR, p);
    CHECK(n == 3);
    CHECK(jl_decode(&rx, p[0], 0, &ev) == 0);          /* PREPARE: nothing yet */
    CHECK(jl_decode(&rx, p[1], 0, &ev) == 1 && ev.type == JL_EV_SCENE && ev.a == 5 && ev.b == JL_Q_BAR);
    CHECK(jl_decode(&rx, p[2], 0, &ev) == 0);          /* the redundant commit: ignored */
    CHECK(jl_decode(&rx, p[1], 0, &ev) == 0);          /* a commit with no prepare: ignored */
    CHECK(jl_decode(&rx, JL_PKT_CLOCK, 0, &ev) == 1 && ev.type == JL_EV_CLOCK);
    CHECK(jl_decode(&rx, JL_PKT_START, 0, &ev) == 1 && ev.type == JL_EV_START);
    CHECK(jl_trigger(&tx, JL_NOTE_KICK, 100, 1000, p) == 2);
    CHECK(jl_trigger(&tx, JL_NOTE_KICK, 100, 1005, p) == 0 && tx.rate_limited == 1); /* within 15 ms */
    CHECK(jl_trigger(&tx, JL_NOTE_KICK, 100, 1020, p) == 2);
    CHECK(jl_decode(&rx, p[0], 1020, &ev) == 1 && ev.type == JL_EV_KICK && ev.a == 100);
    CHECK(jl_decode(&rx, p[1], 1020, &ev) == 0);       /* its note-off */
    CHECK(jl_hello(&tx, 2000, p) == 1 && jl_hello(&tx, 2100, p) == 0 && jl_hello(&tx, 2600, p) == 1);
    CHECK(jl_decode(&rx, p[0], 2600, &ev) == 1 && ev.type == JL_EV_HELLO);
    CHECK(jl_alive(&rx, 3000) && !jl_alive(&rx, 4200));
    { jl_link_t other; jl_init(&other, 3); n = jl_cc(&other, JL_CC_DROP, 0); CHECK(jl_decode(&rx, n, 0, &ev) == 0); }
    CHECK(jl_decode(&rx, jl_cc(&tx, JL_CC_HELLO, 7), 0, &ev) == 1 && ev.type == JL_EV_VERSION_MISMATCH);
}

TEST(test_store_records)
{
    struct { jo_rec_hdr_t h; uint8_t p[100]; } r;
    const uint8_t *pl;
    uint32_t len;
    uint16_t ver;
    int i;
    for (i = 0; i < 100; i++) r.p[i] = (uint8_t)i;
    jo_rec_seal(&r.h, 0x31545342u, 2, r.p, 100);
    CHECK(jo_rec_check(&r, sizeof r, 0x31545342u, 1, 2, &pl, &len, &ver) == JO_REC_OK && len == 100 && ver == 2);
    CHECK(jo_rec_check(&r, 10, 0x31545342u, 1, 2, &pl, &len, &ver) == JO_REC_SHORT);
    CHECK(jo_rec_check(&r, sizeof r, 0x11111111u, 1, 2, &pl, &len, &ver) == JO_REC_MAGIC);
    CHECK(jo_rec_check(&r, sizeof r, 0x31545342u, 3, 4, &pl, &len, &ver) == JO_REC_VERSION);
    r.p[50] ^= 1;
    CHECK(jo_rec_check(&r, sizeof r, 0x31545342u, 1, 2, &pl, &len, &ver) == JO_REC_CRC);
    CHECK(jo_crc32("123456789", 9) == 0xCBF43926u); /* the standard check value */
}

TEST(test_fx_bounds)
{
    /* the buses under full-scale noise with every macro at its maximum: under the ceiling, finite */
    static float x[20000], sub[20000], l[20000], r[20000];
    jf_drum_bus_t b;
    jf_neuro_t nm;
    jo_rng_t rng;
    uint32_t i;
    float pk = 0.0f;
    int ok = 1;
    jo_rng_seed(&rng, 9);
    jf_drum_init(&b);
    b.m.punch = b.m.crush = b.m.glue = b.m.pressure = 1.0f;
    b.m.trim_db = 6.0f;
    jf_drum_update(&b);
    for (i = 0; i < 20000; i++) x[i] = jo_noise(&rng) * 8.0f;
    jf_drum_process(&b, x, 20000);
    for (i = 0; i < 20000; i++) { if (!jo_finite(x[i])) ok = 0; if (fabsf(x[i]) > pk) pk = fabsf(x[i]); }
    CHECK(ok);
    CHECKF(pk <= jo_db2lin(-0.5f) + 1e-4f, "drum bus peak %f", (double)pk);
    jf_neuro_init(&nm);
    nm.m.weight = nm.m.dirt = nm.m.width = nm.m.pressure = 1.0f;
    jf_neuro_update(&nm);
    for (i = 0; i < 20000; i++) { sub[i] = 3.0f * jo_noise(&rng); x[i] = 5.0f * jo_noise(&rng); l[i] = 4.0f * jo_noise(&rng); r[i] = -l[i]; }
    jf_neuro_process(&nm, sub, x, l, r, 0, 20000);
    pk = 0.0f;
    for (i = 0; i < 20000; i++) { if (!jo_finite(l[i]) || !jo_finite(r[i])) ok = 0; pk = fmaxf(pk, fmaxf(fabsf(l[i]), fabsf(r[i]))); }
    CHECK(ok);
    CHECKF(pk <= jo_db2lin(-0.5f) + 1e-4f, "neuro peak %f", (double)pk);
}

TEST(test_harmony)
{
    float n[6];
    CHECK(jh_degree(50, JH_MINOR, 0) == 50 && jh_degree(50, JH_MINOR, 2) == 53 && jh_degree(50, JH_MINOR, 7) == 62);
    CHECK(jh_degree(50, JH_MINOR, -1) == 48);
    CHECK(jh_degree(50, JH_PHRYGIAN, 1) == 51 && jh_degree(50, JH_HARM_MINOR, 6) == 61);
    CHECK(jh_snap(50, JH_MINOR, 54) == 53 || jh_snap(50, JH_MINOR, 54) == 55);
    CHECK(jh_snap(50, JH_MINOR, 52) == 52);
    CHECK(jh_chord(50, JH_CH_MIN, 0.0f, 0, n, 6) == 3 && n[1] == 53.0f && n[2] == 57.0f);
    CHECK(jh_chord(50, JH_CH_MIN, 0.9f, 0, n, 6) == 4 && n[3] == 63.0f);   /* the b9 */
    CHECK(jh_chord(50, JH_CH_MIN, 0.0f, 1, n, 6) == 3 && n[0] == 62.0f);   /* first inversion */
}

void run_modules(void)
{
    RUN(test_drums_bounded_and_decay);
    RUN(test_drum_retrigger_declick);
    RUN(test_bank_parse_and_reject);
    RUN(test_break_player_bounds);
    RUN(test_break_crossfade_no_click);
    RUN(test_testbreak);
    RUN(test_graph);
    RUN(test_link_protocol);
    RUN(test_store_records);
    RUN(test_fx_bounds);
    RUN(test_harmony);
}
