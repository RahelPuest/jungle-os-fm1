/* SPDX-License-Identifier: MPL-2.0 */
/* Engine-level acceptance checks (docs/development/testing.md, scenarios A-F on the host). */
#include "../jo_test.h"
#include "../../firmware/break-surgeon/engine/bs_engine.h"
#include "../../firmware/abyss-engine/engine/ab_engine.h"

static bs_engine_t BS;
static ab_engine_t AB;
static int16_t testpcm[70000], roombuf[JF_VERB_TOTAL];
static int16_t pool[AB_POOL_SAMPLES];
static int32_t lr[2 * 128];

/* a per-bar fingerprint of what fired: track, step, slice, flags, hit, sub-step offset */
#define NBARS 64
static uint32_t bar_fp[NBARS], bar_n[NBARS];
static void trace(void *ctx, const js_event_t *ev)
{
    uint32_t bar = ev->abs_step >> 4;
    (void)ctx;
    /* the deterministic tracks only: GHOST and HATS vary per bar by design (seeded probability) */
    if (ev->track == BS_T_GHOST || ev->track == BS_T_HATS || ev->track == BS_T_PERC || ev->track == BS_T_IMPACT) return;
    if (bar < NBARS) {
        bar_fp[bar] = js_hash(bar_fp[bar], ev->track | (uint32_t)ev->step << 8, ev->data | (uint32_t)ev->flags << 8,
                              ev->hit | (uint32_t)(ev->abs_step & 15u) << 8);
        bar_n[bar]++;
    }
}
static void bs_go(float seconds)
{
    uint32_t i, n = (uint32_t)(seconds * JO_SR / 128.0f);
    for (i = 0; i < n; i++) bs_render(&BS, lr, 128, 0.9f);
}
static void bs_start(float bpm)
{
    memset(bar_fp, 0, sizeof bar_fp);
    memset(bar_n, 0, sizeof bar_n);
    bs_init(&BS, testpcm, 0, 0);
    bs_set_room_buf(&BS, roombuf);
    BS.trace = trace;
    bs_cmd_bpm(&BS, bpm);
    bs_cmd_play(&BS, 1);
}
static float bar_s(float bpm) { return 240.0f / bpm; }

TEST(test_A_fill_and_revert)
{
    /* bars 0-2 plain, a 1-bar fill queued in bar 2 lands on bar 3, bars 4.. plain again and identical to bar 1 */
    float b = bar_s(174.0f);
    bs_start(174.0f);
    bs_go(2.5f * b);
    bs_cmd_fill(&BS, 1);
    bs_go(4.0f * b);
    CHECKF(bar_fp[3] != bar_fp[1], "the fill bar differs");
    CHECKF(bar_fp[4] == bar_fp[1] && bar_fp[5] == bar_fp[1], "after the fill: %08x %08x vs %08x", bar_fp[4], bar_fp[5], bar_fp[1]);
    CHECKF(bar_n[3] > bar_n[1], "the fill is denser (%u vs %u)", bar_n[3], bar_n[1]);
    CHECK(BS.q.dropped == 0);
}

TEST(test_B_mutate_anchors_undo_scene)
{
    js_track_t before;
    uint32_t i;
    int anchors_kept = 1, changed = 0;
    bs_start(174.0f);
    before = BS.pat[0].t[BS_T_BREAK];
    bs_mutate(&BS, 1.0f, 99);
    for (i = 0; i < 16; i++) {
        if ((i & 3u) == 0 && memcmp(&before.s[i], &BS.pat[0].t[BS_T_BREAK].s[i], sizeof(js_step_t))) anchors_kept = 0;
        if (memcmp(&before.s[i], &BS.pat[0].t[BS_T_BREAK].s[i], sizeof(js_step_t))) changed++;
    }
    CHECK(anchors_kept);
    CHECKF(changed >= 6, "mutated steps %d", changed);
    CHECK(bs_undo(&BS) == 0 && !memcmp(&before, &BS.pat[0].t[BS_T_BREAK], sizeof before));
    CHECK(bs_undo(&BS) == -1);
    /* a scene queued mid-bar lands on the bar: the old pattern plays the rest of the bar out */
    bs_go(1.3f * bar_s(174.0f));
    bs_cmd_scene(&BS, 5, BS_Q_BAR); /* IMPACT: pattern 2 */
    bs_go(0.1f * bar_s(174.0f));
    CHECK(BS.cur_pat == 0 && BS.scene_pending == 5);
    bs_go(1.0f * bar_s(174.0f));
    CHECK(BS.cur_pat == 2 && BS.scene_cur == 5 && BS.scene_pending == -1);
    CHECK(BS.pending_at % 16u == 0u);
}

TEST(test_perf_return)
{
    /* glitch held, then RETURN: the next bar is the plain pattern again */
    float b = bar_s(172.0f);
    bs_start(172.0f);
    bs_go(1.2f * b);
    bs_cmd_perf(&BS, BS_PERF_GLITCH);
    bs_set_rhythm(&BS, 0.9f);
    bs_go(1.5f * b);
    bs_cmd_return(&BS);
    bs_go(3.0f * b); /* bar 3 lands clean; bar 4 is checked whole */
    CHECK(BS.perf == BS_PERF_CLEAN && BS.rhythm_t == 0.0f);
    CHECKF(bar_fp[2] != bar_fp[0], "glitched bar differs");
    CHECKF(bar_fp[4] == bar_fp[0], "after RETURN %08x vs %08x", bar_fp[4], bar_fp[0]);
}

static uint32_t kick_pkts;
static void count_out(uint32_t pkt) { if ((pkt & 0xF0FFu) == 0x9009u && ((pkt >> 16) & 0x7Fu) == JL_NOTE_KICK) kick_pkts++; }
TEST(test_link_out_kicks)
{
    bs_start(174.0f);
    BS.midi_out = count_out;
    kick_pkts = 0;
    bs_go(4.0f * bar_s(174.0f));
    /* pattern 0: two synth kicks a bar plus the test break's kick-class slices (3 a bar), rate-limited */
    CHECKF(kick_pkts >= 8 && kick_pkts <= 4 * 5, "kick triggers %u", kick_pkts);
}

TEST(test_breaklock_separate)
{
    uint32_t i, n_layer = 0, n_sep = 0;
    bs_start(174.0f);
    BS.breaklock = BS_LOCK_SEPARATE;
    bs_go(4.0f * bar_s(174.0f));
    for (i = 0; i < 4; i++) n_sep += bar_n[i];
    bs_start(174.0f);
    BS.breaklock = BS_LOCK_LAYER;
    bs_go(4.0f * bar_s(174.0f));
    for (i = 0; i < 4; i++) n_layer += bar_n[i];
    CHECKF(n_layer > n_sep, "layer %u > separate %u", n_layer, n_sep);
}

static void ab_go(float seconds)
{
    uint32_t i, n = (uint32_t)(seconds * JO_SR / 128.0f);
    for (i = 0; i < n; i++) ab_render(&AB, lr, 128, 0.9f);
}

TEST(test_D_ducking)
{
    float g0, g1;
    uint32_t i;
    ab_init(&AB, pool);
    AB.duck_depth = 0.7f;
    jf_duck_set(&AB.duck, 0.7f, 0.002f, 0.03f, 0.12f);
    g0 = jf_duck_tick(&AB.duck);
    ab_link_kick(&AB, 127);
    for (i = 0; i < 400; i++) g1 = jf_duck_tick(&AB.duck); /* 9 ms */
    CHECK(g0 == 1.0f);
    CHECKF(g1 < 0.35f && g1 > 0.29f, "ducked gain %f", (double)g1);
    for (i = 0; i < 44118; i++) g1 = jf_duck_tick(&AB.duck);
    CHECKF(g1 > 0.999f, "recovered %f", (double)g1);
}

TEST(test_E_morph_loudness)
{
    /* the bass alone on a held note at every path node: the trims keep the nodes within a spread (hypothesis:
     * 6 dB; RMS is a proxy, not perceived loudness - the listening checklist decides) */
    static float buf[44118];
    float lo = 1e9f, hi = -1e9f;
    uint32_t k, i;
    char line[256] = "";
    for (k = 0; k < AB.fm.npath; k++) {
        double sq = 0.0;
        float rms_db, trim;
        ab_init(&AB, 0);
        AB.morph = AB.morph_s = jg_path_t(&AB.fm, k);
        for (i = 0; i < 20; i++) { AB.morph_s = AB.morph; }
        jn_bass_init(&AB.bass);
        { jg_eval_t ev; jg_eval(&AB.fm, AB.morph, &ev); for (i = 0; i < JN_NPARAMS; i++) AB.patch.p[i] = ev.phys[i];
          AB.patch.motion = 0.5f; AB.patch.coupling = 0.6f; trim = jo_db2lin(ev.trim_db); }
        jn_bass_note(&AB.bass, 38.0f, 1.0f, 0, &AB.patch);
        memset(buf, 0, sizeof buf);
        for (i = 0; i < 44118u / 128u; i++)
            jn_bass_render(&AB.bass, &AB.patch, (float)i * 128.0f / 3803.0f, 0.0f, buf + i * 128u, 128);
        for (i = 4410; i < 44118u / 128u * 128u; i++) sq += (double)(buf[i] * trim) * (double)(buf[i] * trim);
        rms_db = 10.0f * log10f((float)(sq / (double)(44118u / 128u * 128u - 4410u)) + 1e-12f);
        if (rms_db < lo) lo = rms_db;
        if (rms_db > hi) hi = rms_db;
        snprintf(line + strlen(line), sizeof line - strlen(line), " %s %.1f", AB.fm.node[AB.fm.path[k]].name, (double)rms_db);
    }
    printf("  node RMS (dBFS):%s\n", line);
    CHECKF(hi - lo < 6.0f, "loudness spread %.1f dB", (double)(hi - lo));
    CHECK(jg_validate(&AB.fm) == 0);
}

TEST(test_C_voices_and_F_standalone)
{
    uint32_t i;
    jl_event_t ev;
    ab_init(&AB, pool);
    ab_cmd_bpm(&AB, 174.0f);
    ab_cmd_play(&AB, 1);
    ab_cmd_pad(&AB, 1);
    ab_go(1.0f);
    CHECK(AB.pad_on && AB.pad.v[0].active);
    ab_cmd_stab_chord(&AB, 1.0f);
    ab_go(0.05f);
    {
        int stabs = 0, pads = 0;
        for (i = 0; i < JN_STAB_VOICES; i++) stabs += AB.stab.v[i].active;
        for (i = 0; i < JN_PAD_VOICES; i++) pads += AB.pad.v[i].active;
        CHECKF(stabs >= 3 && pads >= 3, "stabs %d pads %d at once", stabs, pads);
    }
    CHECK(AB.clock.source == JS_CLK_INT);
    /* a link clock: followed, then lost -> back to the internal clock at the same tempo */
    for (i = 0; i < 48; i++) {
        ab_link_packet(&AB, JL_PKT_CLOCK, 1000u + i * (uint32_t)(24.0f * 60e6f / (170.0f * 24.0f)));
        (void)ev;
    }
    CHECK(AB.clock.source == JS_CLK_MIDI);
    CHECKF(fabsf(AB.clock.bpm - 170.0f) < 1.0f, "followed bpm %f", (double)AB.clock.bpm);
    {
        uint32_t t = jl_cc(&AB.link, JL_CC_HELLO, JL_VERSION);
        ab_link_packet(&AB, t, 0);
    }
    ab_go(2.0f);
    CHECK(AB.clock.source == JS_CLK_INT && AB.clock.running);
    CHECKF(fabsf(AB.clock.bpm - 170.0f) < 1.0f, "kept bpm %f", (double)AB.clock.bpm);
}

TEST(test_scene_over_link)
{
    /* Break Surgeon's scene command goes out as PREPARE + COMMIT; Abyss queues it for the same bar */
    static uint32_t pk[64];
    static uint32_t npk;
    uint32_t i;
    bs_start(174.0f);
    ab_init(&AB, pool);
    ab_cmd_play(&AB, 1);
    ab_go(0.01f);
    npk = 0;
    {
        static uint32_t *P; static uint32_t *N; P = pk; N = &npk;
        (void)P; (void)N;
    }
    BS.midi_out = 0;
    {
        uint32_t out[3], n = jl_scene(&BS.link, 4, JL_Q_BAR, out);
        for (i = 0; i < n; i++) ab_link_packet(&AB, out[i], 0);
    }
    CHECK(AB.scene_pending == 4);
    ab_go(2.0f * bar_s(174.0f));
    CHECK(AB.scene_cur == 4 && AB.cur_pat == AB.scene[4].pattern);
}

void run_engines(void)
{
    RUN(test_A_fill_and_revert);
    RUN(test_B_mutate_anchors_undo_scene);
    RUN(test_perf_return);
    RUN(test_link_out_kicks);
    RUN(test_breaklock_separate);
    RUN(test_D_ducking);
    RUN(test_E_morph_loudness);
    RUN(test_C_voices_and_F_standalone);
    RUN(test_scene_over_link);
}
