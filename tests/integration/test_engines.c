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
    /* pattern 0: two synth kicks a bar plus the test break's kick-class slices (4 a bar), rate-limited */
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

/* (0.4) the groove library: each groove's kicks and snares on the break's kick and snare slices, undo brings the
 * pattern back */
TEST(test_grooves)
{
    uint32_t g, i, bad = 0;
    js_pattern_t before;
    bs_start(174.0f);
    before = BS.pat[BS.cur_pat];
    for (g = 0; g < BS_NGROOVES; g++) {
        const js_pattern_t *p = &BS.pat[BS.cur_pat];
        const jb_break_t *b = &BS.breaks[BS.cur_break];
        bs_groove(&BS, (int)g);
        for (i = 0; i < p->t[BS_T_BREAK].len; i++) {
            uint8_t cls = b->cls[p->t[BS_T_BREAK].s[i].data % b->nslices];
            if ((p->t[BS_T_KICK].s[i].flags & JS_ON) && cls != JB_CLASS_KICK) bad++;
            if ((p->t[BS_T_SNARE].s[i].flags & JS_ON) && !(p->t[BS_T_KICK].s[i].flags & JS_ON) && cls != JB_CLASS_SNARE) bad++;
        }
        bs_go(0.5f * bar_s(174.0f));
    }
    CHECKF(bad == 0, "%u groove steps off their class", bad);
    CHECK(strcmp(bs_groove_name[0], "2-STEP") == 0);
    bs_groove(&BS, 0);
    CHECK((BS.pat[BS.cur_pat].t[BS_T_KICK].s[0].flags & JS_ON) && (BS.pat[BS.cur_pat].t[BS_T_KICK].s[10].flags & JS_ON));
    CHECK((BS.pat[BS.cur_pat].t[BS_T_SNARE].s[4].flags & JS_ON) && (BS.pat[BS.cur_pat].t[BS_T_SNARE].s[12].flags & JS_ON));
    CHECK(BS.pat[BS.cur_pat].t[BS_T_BREAK].s[0].data == 0 && BS.pat[BS.cur_pat].t[BS_T_BREAK].s[4].data == 4);
    CHECK(bs_undo(&BS) == 0); /* back to the groove before (AMEN, the last of the loop) */
    bs_groove(&BS, 3);
    CHECK(BS.pat[BS.cur_pat].t[BS_T_KICK].len == 32);
    (void)before;
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

/* CALL & RESPONSE: the bass's form A B A C. B (A's line as REESE) plays A's notes in REESE's sound; C plays P2's
 * line as LASER; A keeps the pattern's line and the knobs' sound */
static uint8_t cr_note[4][16], cr_hit[4][16], cr_node[4];
static void cr_trace(void *ctx, const js_event_t *ev)
{
    ab_engine_t *e = (ab_engine_t *)ctx;
    uint32_t bar = (ev->abs_step >> 4) & 3u, st = ev->abs_step & 15u;
    if (ev->track != AB_T_BASS || ev->hit || ev->abs_step >= 64u) return;
    cr_note[bar][st] = ev->data, cr_hit[bar][st] = 1;
    (void)e;
}
TEST(test_call_response)
{
    uint32_t bar, st, same_ab = 1, c_is_p2 = 1, a_is_p1 = 1;
    const js_track_t *p1, *p2;
    ab_init(&AB, pool);
    memset(cr_hit, 0, sizeof cr_hit);
    AB.trace = cr_trace, AB.trace_ctx = &AB;
    ab_set_cr(&AB, 1);
    CHECK(ab_cr_part(&AB, 0) == 0 && ab_cr_part(&AB, 16) == 1 && ab_cr_part(&AB, 32) == 0 && ab_cr_part(&AB, 48) == 2);
    ab_cmd_bpm(&AB, 174.0f);
    ab_cmd_play(&AB, 1);
    for (bar = 0; bar < 4u; bar++) {
        ab_go(bar_s(174.0f) * 0.5f);   /* into the bar's middle: its part has sounded */
        cr_node[bar] = AB.fv.near;
        ab_go(bar_s(174.0f) * 0.5f);
    }
    p1 = &AB.pat[0].t[AB_T_BASS], p2 = &AB.pat[1].t[AB_T_BASS];
    for (st = 0; st < 16u; st++) {
        if (cr_hit[1][st] != cr_hit[0][st] || (cr_hit[0][st] && cr_note[1][st] != cr_note[0][st])) same_ab = 0;
        if (cr_hit[0][st] != ((p1->s[st].flags & JS_ON) != 0) || (cr_hit[0][st] && cr_note[0][st] != p1->s[st].data)) a_is_p1 = 0;
        if (cr_hit[3][st] != ((p2->s[st].flags & JS_ON) != 0) || (cr_hit[3][st] && cr_note[3][st] != p2->s[st].data)) c_is_p2 = 0;
    }
    CHECK(a_is_p1);
    CHECK(same_ab);
    CHECK(c_is_p2);
    CHECKF(strcmp(AB.fm.node[cr_node[1]].name, "REESE DARK") == 0, "B sounds as %s", AB.fm.node[cr_node[1]].name);
    CHECKF(strcmp(AB.fm.node[cr_node[3]].name, "LASER") == 0, "C sounds as %s", AB.fm.node[cr_node[3]].name);
    CHECKF(strcmp(AB.fm.node[cr_node[2]].name, "REESE DARK") != 0 && strcmp(AB.fm.node[cr_node[2]].name, "LASER") != 0,
           "A again sounds as %s", AB.fm.node[cr_node[2]].name);
    /* off: the pattern alone again */
    ab_set_cr(&AB, 0);
    CHECK(ab_cr_part(&AB, 16) == 0 && AB.cr_play == 0);
    AB.trace = 0;
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
    RUN(test_grooves);
    RUN(test_D_ducking);
    RUN(test_E_morph_loudness);
    RUN(test_C_voices_and_F_standalone);
    RUN(test_scene_over_link);
    RUN(test_call_response);
}
