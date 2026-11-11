/* SPDX-License-Identifier: MPL-2.0 */
/* (0.5) the sequencer-first controls and the live tools of Abyss Engine: the variations and the FORM, the
 * performance FX, the fills, REC, DENSITY and HUMANIZE, UNDO / REDO with the sound, LOCK TO KICK, FILL ANSWERS and
 * the trig conditions, on the engine (the app's keys and screens: tests/host/ab_smoke.txt) */
#include "../jo_test.h"
#include "../../firmware/abyss-engine/engine/ab_engine.h"
#include "../../shared/seq/jo_undo.h"

static ab_engine_t L;
static int16_t lpool[AB_POOL_SAMPLES];
static int32_t llr[2 * 128];
static ju_stack_t lundo;

/* every fired bass event of the first 8 bars: per bar and 16th its data and hits; and the last one */
static uint8_t ev_on[8][16], ev_data[8][16], ev_hits[8][16];
static uint32_t ev_n[AB_NTRACKS];
static js_event_t ev_last;
static void trace(void *ctx, const js_event_t *ev)
{
    uint32_t bar = ev->abs_step >> 4, st = ev->abs_step & 15u;
    (void)ctx;
    ev_n[ev->track % AB_NTRACKS]++;
    if (ev->track != AB_T_BASS) return;
    ev_last = *ev;
    if (bar < 8u && !ev->hit) ev_on[bar][st] = 1, ev_data[bar][st] = ev->data, ev_hits[bar][st] = ev->nhits;
}
static float bar_secs(void) { return 16.0f * 60.0f / 174.0f / 4.0f; }
static void go(float seconds)
{
    uint32_t i, n = (uint32_t)(seconds * JO_SR / 128.0f);
    for (i = 0; i < n; i++) ab_render(&L, llr, 128, 0.9f);
}
static void start(void)
{
    ab_init(&L, lpool);
    memset(ev_on, 0, sizeof ev_on);
    memset(ev_n, 0, sizeof ev_n);
    L.trace = trace, L.trace_ctx = &L;
    ab_cmd_bpm(&L, 174.0f);
    ab_cmd_play(&L, 1);
}
/* the bass line of a pattern played in a bar as it should: every on step at its note */
static int bar_is_line(uint32_t bar, const js_track_t *t)
{
    uint32_t s;
    for (s = 0; s < 16u; s++) {
        int on = (t->s[s].flags & JS_ON) != 0;
        if (ev_on[bar][s] != on || (on && ev_data[bar][s] != t->s[s].data)) return 0;
    }
    return 1;
}

TEST(test_ab_form_variations)
{
    uint8_t node[8];
    uint32_t bar;
    start();
    ab_set_form(&L, AB_FORM_ABCD);
    CHECK(ab_cr_part(&L, 0) == 0 && ab_cr_part(&L, 16) == 1 && ab_cr_part(&L, 32) == 2 && ab_cr_part(&L, 48) == 3);
    for (bar = 0; bar < 4u; bar++) {
        go(bar_secs() * 0.5f);
        node[bar] = L.fv.near;
        go(bar_secs() * 0.5f);
    }
    /* A: P1's line; B: A's line (REESE); C: P2's (LASER); D: P3's (GROWL) */
    CHECK(bar_is_line(0, &L.pat[0].t[AB_T_BASS]));
    CHECK(bar_is_line(1, &L.pat[0].t[AB_T_BASS]));
    CHECK(bar_is_line(2, &L.pat[1].t[AB_T_BASS]));
    CHECK(bar_is_line(3, &L.pat[2].t[AB_T_BASS]));
    CHECKF(strcmp(L.fm.node[node[1]].name, "REESE") == 0, "B as %s", L.fm.node[node[1]].name);
    CHECKF(strcmp(L.fm.node[node[2]].name, "LASER") == 0, "C as %s", L.fm.node[node[2]].name);
    CHECKF(strcmp(L.fm.node[node[3]].name, "GROWL") == 0, "D as %s", L.fm.node[node[3]].name);
    /* the other forms on the bar grid */
    ab_set_form(&L, AB_FORM_AAAB);
    CHECK(ab_cr_part(&L, 0) == 0 && ab_cr_part(&L, 32) == 0 && ab_cr_part(&L, 48) == 1);
    ab_set_form(&L, AB_FORM_AABB);
    CHECK(ab_cr_part(&L, 16) == 0 && ab_cr_part(&L, 32) == 1);
    /* a variation picked: FORM OFF, it plays from the next bar on */
    ab_cmd_variation(&L, 3);
    go(0.01f);
    CHECK(L.form == AB_FORM_OFF && L.var_sel != 3);
    go(bar_secs());
    CHECK(L.var_sel == 3 && ab_cr_part(&L, L.clock.abs_step) == 3);
    /* 0.4's CALL & RESPONSE is FORM A B A C */
    ab_set_cr(&L, 1);
    CHECK(L.form == AB_FORM_ABAC && L.cr_on);
    ab_set_cr(&L, 0);
    CHECK(L.form == AB_FORM_OFF && ab_cr_part(&L, 16) == 0);
}

TEST(test_ab_perf_fx)
{
    float cut0, drive0, f;
    uint32_t i, all32 = 1, same = 1, nst = 0;
    uint8_t d0 = 0;
    /* OCT UP / OCT DOWN: the notes an octave off their steps */
    start();
    ab_set_perf(&L, AB_FX_OCT_UP);
    go(bar_secs());
    CHECKF(L.last_bass_note == (float)(L.root + (int)ev_last.data - 64) + 12.0f, "oct up %f", (double)L.last_bass_note);
    ab_set_perf(&L, AB_FX_OCT_DOWN);
    go(bar_secs());
    CHECKF(L.last_bass_note == (float)(L.root + (int)ev_last.data - 64) - 12.0f, "oct down %f", (double)L.last_bass_note);
    /* FILTER DIVE: the cutoff five octaves down after a bar; BITE MAX: the drive of BITE 1; WOBBLE: the LFO full */
    ab_set_perf(&L, AB_FX_NONE);
    go(0.05f);
    cut0 = L.patch.p[JN_P_CUTOFF], drive0 = L.patch.p[JN_P_DRIVE];
    ab_set_perf(&L, AB_FX_DIVE);
    go(bar_secs() * 1.1f);
    CHECKF(L.patch.p[JN_P_CUTOFF] < cut0 / 16.0f || L.patch.p[JN_P_CUTOFF] <= 80.0f, "dive %f of %f", (double)L.patch.p[JN_P_CUTOFF], (double)cut0);
    ab_set_perf(&L, AB_FX_BITE);
    go(0.05f);
    f = L.patch.p[JN_P_DRIVE] / drive0;
    CHECKF(f > 1.5f, "bite max x%f", (double)f);
    ab_set_perf(&L, AB_FX_WOBBLE);
    go(0.05f);
    CHECK(L.patch.motion == 1.0f && L.patch.p[JN_P_LFODIV] == 4.0f && L.patch.p[JN_P_LFODEPTH] == 1.0f);
    /* STUTTER: one note repeated in 32nds on every 16th */
    ab_set_perf(&L, AB_FX_NONE);
    go(0.05f);
    memset(ev_on, 0, sizeof ev_on);
    ab_set_perf(&L, AB_FX_STUTTER);
    go(bar_secs() * 1.2f);
    for (i = 0; i < 8u * 16u; i++) {
        uint32_t b = i >> 4, s = i & 15u;
        if (!ev_on[b][s]) continue;
        if (!nst) d0 = ev_data[b][s];
        nst++;
        if (ev_hits[b][s] != 2) all32 = 0;
        if (ev_data[b][s] != d0) same = 0;
    }
    CHECKF(nst >= 12u && all32 && same, "stutter: %u steps, 32nds %d, one note %d", nst, all32, same);
    /* TAPE STOP: silent after 0.6 s, its pitch two octaves down; back on release */
    ab_set_perf(&L, AB_FX_NONE);
    go(0.05f);
    ab_set_perf(&L, AB_FX_TAPE);
    go(0.7f);
    CHECK(L.tape_gain == 0.0f);
    {
        float pk = 0.0f;
        for (i = 0; i < 128u; i++) pk = jo_maxf(pk, jo_absf(L.b_bass[i]) + jo_absf(L.b_sub[i]));
        CHECKF(pk == 0.0f, "tape stop leaves %f", (double)pk);
    }
    ab_set_perf(&L, AB_FX_NONE);
    go(0.05f);
    CHECK(L.tape_gain == 1.0f);
    /* SUB ONLY: the mid bass silent, the sub playing */
    ab_set_perf(&L, AB_FX_SUB_ONLY);
    {
        float sub = 0.0f, mid = 0.0f;
        uint32_t k;
        for (k = 0; k < 200u; k++) {
            ab_render(&L, llr, 128, 0.9f);
            for (i = 0; i < 128u; i++) sub = jo_maxf(sub, jo_absf(L.b_sub[i])), mid = jo_maxf(mid, jo_absf(L.b_bass[i]));
        }
        CHECKF(mid == 0.0f && sub > 0.01f, "sub only: mid %f sub %f", (double)mid, (double)sub);
    }
}

TEST(test_ab_fills)
{
    uint32_t s, bar, n_in = 0;
    start();
    go(0.1f);
    /* DROP OUT from the next bar: no bass in bar 1, back in bar 2 */
    ab_cmd_fill_type(&L, AB_FILL_DROP, 1, 0);
    go(bar_secs() * 2.5f);
    for (s = 0; s < 16u; s++) n_in += ev_on[1][s];
    CHECKF(n_in == 0, "drop out bar played %u", n_in);
    n_in = 0;
    for (s = 0; s < 16u; s++) n_in += ev_on[2][s];
    CHECK(n_in > 0);
    /* at the phrase's end: the last bar of the 4-bar phrase (bar 3), 2 bars: bars 6 and 7 of the 8-bar phrase */
    ab_cmd_fill_type(&L, AB_FILL_OCTAVES, 1, 1);
    go(0.01f);
    CHECKF(L.fill_start == 48u + 64u * ((L.clock.abs_step + 2u) / 64u) && L.fill_end == L.fill_start + 16u, "phrase end at %u", L.fill_start);
    ab_set_phrase(&L, 8);
    ab_cmd_fill_type(&L, AB_FILL_SLIDE, 2, 1);
    go(0.01f);
    bar = L.fill_start >> 4;
    CHECKF(bar % 8u == 6u && L.fill_end - L.fill_start == 32u, "2-bar fill from bar %u", bar);
    /* OCTAVES: the second 8th of each beat an octave up */
    start();
    go(0.1f);
    ab_cmd_fill_type(&L, AB_FILL_OCTAVES, 1, 0);
    go(bar_secs() * 2.2f);
    {
        const js_track_t *t = &L.pat[0].t[AB_T_BASS];
        uint32_t ok = 1, seen = 0;
        for (s = 0; s < 16u; s++)
            if (ev_on[1][s]) {
                uint8_t want = (uint8_t)(t->s[s].data + (((s >> 1) & 1u) ? 12u : 0u));
                seen++;
                if (ev_data[1][s] != want) ok = 0;
            }
        CHECKF(ok && seen > 0, "octaves (%u notes)", seen);
    }
}

TEST(test_ab_record_density_humanize)
{
    int idx, want;
    uint32_t i, n_on = 0;
    js_track_t keep;
    start();
    ab_cmd_play(&L, 0);
    go(0.01f);
    CHECK(ab_record(&L, AB_T_BASS, 45, 0.8f) == -1); /* stopped: nothing recorded */
    ab_cmd_play(&L, 1);
    go(bar_secs() * 0.33f);
    /* the step nearest now: the playing 16th, or the next one past its half */
    want = (int)((L.clock.abs_step + (L.clock.phase >= 0x80000000u ? 1u : 0u)) % L.pat[0].t[AB_T_BASS].len);
    idx = ab_record(&L, AB_T_BASS, L.root + 7, 0.8f);
    CHECKF(idx == want, "recorded at %d, nearest %d", idx, want);
    CHECK((L.pat[0].t[AB_T_BASS].s[idx].flags & JS_ON) && L.pat[0].t[AB_T_BASS].s[idx].data == 64 + 7 &&
          L.pat[0].t[AB_T_BASS].s[idx].vel == 102);
    idx = ab_record(&L, AB_T_STAB, jh_degree(L.root + 24, L.scale, 3), 1.0f);
    CHECK(idx >= 0 && L.pat[0].t[AB_T_STAB].s[idx].data == 3);
    /* DENSITY: up adds the best-ranked empty step, down removes the worst-ranked one; deterministic */
    keep = L.pat[0].t[AB_T_STAB];
    for (i = 0; i < 16u; i++) L.pat[0].t[AB_T_STAB].s[i].flags = 0;
    CHECK(ab_density(&L, AB_T_STAB, 1) && (L.pat[0].t[AB_T_STAB].s[0].flags & JS_ON));
    CHECK(ab_density(&L, AB_T_STAB, 1) && (L.pat[0].t[AB_T_STAB].s[8].flags & JS_ON));
    CHECK(ab_density(&L, AB_T_STAB, 1) && (L.pat[0].t[AB_T_STAB].s[4].flags & JS_ON));
    CHECK(ab_density(&L, AB_T_STAB, -1) && !(L.pat[0].t[AB_T_STAB].s[4].flags & JS_ON) && (L.pat[0].t[AB_T_STAB].s[0].flags & JS_ON));
    while (ab_density(&L, AB_T_STAB, 1)) n_on++;
    CHECK(n_on == 14u);
    L.pat[0].t[AB_T_STAB] = keep;
    /* HUMANIZE: the same offsets on every run, none at 0 */
    {
        uint8_t a[8][16], b[8][16];
        start();
        L.humanize = 0.6f;
        go(bar_secs() * 2.0f);
        memcpy(a, ev_data, sizeof a);
        {
            static js_event_t first;
            (void)first;
        }
        start();
        L.humanize = 0.6f;
        go(bar_secs() * 2.0f);
        memcpy(b, ev_data, sizeof b);
        CHECK(memcmp(a, b, sizeof a) == 0);
    }
    {
        uint32_t v_h = 0, v_0 = 0;
        start();
        L.humanize = 0.8f;
        go(bar_secs());
        v_h = ev_last.vel;
        start();
        go(bar_secs());
        v_0 = ev_last.vel;
        CHECKF(v_h != v_0, "humanized velocity %u vs %u", v_h, v_0);
    }
}

TEST(test_ab_undo_sound)
{
    ab_sound_state_t s;
    uint8_t slot, f0;
    start();
    ju_init(&lundo);
    /* a mutation and a step edit, undone together, then redone */
    f0 = L.pat[0].t[AB_T_BASS].s[1].flags;
    ab_sound_get(&L, &s);
    ju_push(&lundo, L.cur_pat, &L.pat[L.cur_pat], &s, sizeof s);
    ab_mutate(&L, 0.5f, 7);
    L.pat[0].t[AB_T_BASS].s[1].flags ^= JS_ON;
    CHECK(L.mutated);
    ab_sound_get(&L, &s);
    CHECK(ju_undo(&lundo, L.pat, &s, sizeof s, &slot) == 0 && slot == 0);
    ab_sound_set(&L, &s);
    CHECK(!L.mutated && L.pat[0].t[AB_T_BASS].s[1].flags == f0);
    ab_sound_get(&L, &s);
    CHECK(ju_redo(&lundo, L.pat, &s, sizeof s, &slot) == 0);
    ab_sound_set(&L, &s);
    CHECK(L.mutated && L.pat[0].t[AB_T_BASS].s[1].flags == (uint8_t)(f0 ^ JS_ON));
    CHECK(sizeof(ab_sound_state_t) <= JU_EXTRA);
}

TEST(test_ab_link_helpers)
{
    uint32_t s, n_bass = 0;
    start();
    ab_link_packet(&L, jl_cc(&L.link, JL_CC_HELLO, JL_VERSION), 0);
    CHECK(jl_alive(&L.link, L.ms));
    /* LOCK TO KICK: the steps don't play; a kick plays its line's note */
    L.lock_kick = 1;
    go(bar_secs());
    for (s = 0; s < 16u; s++) n_bass += ev_on[0][s];
    CHECKF(n_bass == 0, "the steps played %u notes", n_bass);
    L.bass_on = 0;
    ab_link_kick(&L, 120);
    {
        const js_track_t *t = &L.pat[0].t[AB_T_BASS];
        uint32_t idx = L.clock.abs_step % t->len, k;
        float want = -1.0f;
        for (k = 0; k < t->len; k++)
            if (t->s[(idx + t->len - k) % t->len].flags & JS_ON) { want = (float)(L.root + (int)t->s[(idx + t->len - k) % t->len].data - 64); break; }
        CHECKF(L.bass_on && L.last_bass_note == want, "kick note %f, the line's %f", (double)L.last_bass_note, (double)want);
    }
    L.lock_kick = 0;
    /* FILL ANSWERS: a link fill's bars play variation C */
    L.fill_answers = 1;
    ab_link_packet(&L, jl_cc(&L.link, JL_CC_HELLO, JL_VERSION), 0);
    ab_link_packet(&L, jl_cc(&L.link, JL_CC_FILL_BEGIN, 1), 0);
    CHECK(L.answer_on);
    {
        uint32_t next = (L.clock.abs_step + 2u + 15u) / 16u * 16u;
        CHECK(ab_cr_part(&L, next) == 2 && ab_cr_part(&L, next + 15u) == 2 && ab_cr_part(&L, next + 16u) == 0);
    }
    go(bar_secs() * 2.5f);
    CHECK(!L.answer_on);
}

TEST(test_ab_conditions)
{
    uint32_t bar, fired = 0;
    start();
    ab_cmd_play(&L, 0);
    go(0.01f);
    js_step_clear(&L.pat[0].t[AB_T_BASS].s[0]);
    L.pat[0].t[AB_T_BASS].s[0].flags = JS_ON;
    L.pat[0].t[AB_T_BASS].s[0].data = 64;
    L.pat[0].t[AB_T_BASS].s[0].cond = JS_C_2_4;
    memset(ev_on, 0, sizeof ev_on);
    ab_cmd_play(&L, 1);
    go(bar_secs() * 4.3f);
    for (bar = 0; bar < 4u; bar++) fired |= (uint32_t)ev_on[bar][0] << bar;
    CHECKF(fired == 2u, "2:4 fired in bars %x", fired);
    /* FILL: only under a fill */
    L.pat[0].t[AB_T_BASS].s[0].cond = JS_C_FILL;
    memset(ev_on, 0, sizeof ev_on);
    ab_cmd_play(&L, 0);
    go(0.01f);
    ab_cmd_play(&L, 1);
    go(bar_secs() * 0.5f);
    ab_cmd_fill_type(&L, AB_FILL_ROLLS, 1, 0);
    go(bar_secs() * 2.0f);
    CHECK(!ev_on[0][0] && ev_on[1][0] && !ev_on[2][0]);
}

TEST(test_ab_library)
{
    uint32_t l, s, ok = 1;
    start();
    for (l = 0; l < AB_NLINES; l++) {
        const js_track_t *t = &L.pat[0].t[AB_T_BASS];
        uint32_t on = 0;
        ab_load_line(&L, (int)l);
        for (s = 0; s < 16u; s++)
            if (t->s[s].flags & JS_ON) {
                int note = L.root + (int)t->s[s].data - 64;
                on++;
                if (jh_snap(L.root, L.scale, note) != note) ok = 0; /* in the scale */
            }
        if (!on || t->len != 16u) ok = 0;
    }
    CHECK(ok);
    ab_load_rhythm(&L, AB_T_PAD, 2);
    CHECK(L.pat[0].t[AB_T_PAD].len == 32u && (L.pat[0].t[AB_T_PAD].s[8].flags & JS_ON) && L.pat[0].t[AB_T_PAD].s[24].data == 6);
    ab_load_rhythm(&L, AB_T_STAB, 4);
    for (s = 0; s < 16u; s++) CHECK(!(L.pat[0].t[AB_T_STAB].s[s].flags & JS_ON));
    go(bar_secs() * 2.0f); /* the lines play */
    CHECK(ev_n[AB_T_BASS] > 0);
}

/* ------------------------------------------------------------------ 0.6 --- */
/* to the start of 16th abs (before its half, so a note played now lands on it) */
static void go_to(uint32_t abs)
{
    uint32_t g = 0;
    while (!(L.clock.abs_step == abs && L.clock.phase < 0x50000000u) && g++ < 2000000u) ab_render(&L, llr, 128, 0.9f);
}
static uint32_t sent[8], nsent;
static void capture_out(uint32_t pkt) { if (nsent < 8u) sent[nsent] = pkt; nsent++; }
static void hello(void) { ab_link_packet(&L, jl_cc(&L.link, JL_CC_HELLO, JL_VERSION), 0); }
/* to 16th abs with the link heard every beat */
static void go_link(uint32_t abs)
{
    uint32_t a = L.clock.abs_step + 1u;
    for (; js_before(a, abs); a += 4u) { go_to(a); hello(); }
    go_to(abs);
    hello();
}

TEST(test_ab_energy)
{
    const js_track_t *t;
    uint32_t s, n0 = 0, n2 = 0;
    start();
    t = &L.pat[0].t[AB_T_BASS];
    /* the default line's levels: the downbeat and the bar's middle 1, offbeat 16ths 4, the rest 2 */
    CHECK(t->s[0].lvl == 1 && t->s[8].lvl == 1 && t->s[3].lvl == 4 && t->s[11].lvl == 4 && t->s[6].lvl == 2);
    /* low ENERGY: only level 1 plays */
    ab_energy_set(&L, 0.1f);
    go(bar_secs() * 2.0f);
    for (s = 0; s < 16u; s++) n0 += ev_on[1][s];
    CHECKF(n0 == 2 && ev_on[1][0] && ev_on[1][8], "low energy played %u steps", n0);
    ab_energy_set(&L, 1.0f);
    go(bar_secs() * 1.0f);
    for (s = 0; s < 16u; s++) n2 += ev_on[2][s];
    CHECKF(n2 == 7, "full energy played %u steps", n2);
    /* the knob goes over the link once per change; a received ENERGY is followed and not sent back */
    L.midi_out = capture_out;
    nsent = 0;
    ab_energy_set(&L, 0.5f);
    ab_energy_set(&L, 0.5f);
    CHECK(nsent == 1 && sent[0] == jl_cc(&L.link, JL_CC_ENERGY, 64));
    ab_link_packet(&L, jl_cc(&L.link, JL_CC_ENERGY, 20), 0);
    CHECKF(fabsf(L.en.knob - 20.0f / 127.0f) < 0.001f && nsent == 1, "followed %f, sent %u", (double)L.en.knob, nsent);
    go(bar_secs() * 0.25f);
    CHECK(fabsf(L.en.value - 20.0f / 127.0f) < 0.001f && L.seq.energy == 1);
    L.midi_out = 0;
    /* the sound: less ENERGY, less BITE in the patch (full: as before) */
    {
        float drive_low, drive_full;
        ab_energy_set(&L, 0.0f); go(0.02f); drive_low = L.patch.p[JN_P_DRIVE];
        ab_energy_set(&L, 1.0f); go(0.02f); drive_full = L.patch.p[JN_P_DRIVE];
        CHECKF(drive_low < drive_full, "drive %f at no energy, %f at full", (double)drive_low, (double)drive_full);
    }
    /* AUTO LEVELS on an edited track */
    L.pat[0].t[AB_T_STAB].s[5].flags = JS_ON, L.pat[0].t[AB_T_STAB].s[5].vel = 40;
    ab_auto_levels(&L, AB_T_STAB);
    CHECK(L.pat[0].t[AB_T_STAB].s[5].lvl == 4);
    L.pat[0].t[AB_T_STAB].s[2].flags = JS_ON, L.pat[0].t[AB_T_STAB].s[2].vel = 40;
    ab_auto_levels(&L, AB_T_STAB);
    CHECK(L.pat[0].t[AB_T_STAB].s[2].lvl == 3);
}

TEST(test_ab_energy_curve)
{
    start();
    ab_energy_curve(&L, AB_CURVE_REC);
    CHECK(ab_energy_curve_mode(&L) == AB_CURVE_REC);
    ab_energy_set(&L, 0.1f);
    go_to(32);                     /* two bars low, two bars high: one phrase recorded */
    ab_energy_set(&L, 0.9f);
    go_to(66);
    CHECK(ab_energy_curve_mode(&L) == AB_CURVE_PLAY && L.en.beats == 16);
    ab_energy_set(&L, 0.5f);       /* the knob moves; the curve plays */
    go_to(72);
    CHECKF(fabsf(L.en.value - 0.1f) < 0.01f, "the curve's first half plays %f", (double)L.en.value);
    go_to(64 + 40);
    CHECKF(fabsf(L.en.value - 0.9f) < 0.01f, "the curve's second half plays %f", (double)L.en.value);
    ab_energy_curve(&L, AB_CURVE_OFF);
    CHECK(fabsf(L.en.value - 0.5f) < 0.001f);
}

/* ANSWER ME: three notes in bar 1, the answer in bar 2 */
static void play_bar1(void)
{
    static const uint8_t AT[3] = {0, 4, 9}, NOTE[3] = {38, 41, 45};
    uint32_t i;
    for (i = 0; i < 3u; i++) {
        go_to(16u + AT[i]);
        ab_cmd_note(&L, AB_PART_BASS, NOTE[i], 0.8f, 1);
        go(0.01f);
        ab_cmd_note(&L, AB_PART_BASS, NOTE[i], 0.0f, 0);
    }
}
TEST(test_ab_answer_me)
{
    static const uint8_t AT[3] = {0, 4, 9}, D[3] = {64, 67, 71};
    uint32_t x, s, ok;
    for (x = 0; x < AB_NANS; x++) {
        uint8_t want[16];
        uint32_t k = 0;
        start();
        ab_set_answer(&L, 1);
        L.duet_xform = (uint8_t)x;
        memset(want, 0, sizeof want);
        for (s = 0; s < 3u; s++) {
            uint32_t to = AT[s];
            int d = D[s];
            if (x == AB_ANS_INVERT) d = 64 + jh_snap(L.root, L.scale, L.root - (d - 64)) - L.root;
            if (x == AB_ANS_RETRO) to = 15u - to;
            if (x == AB_ANS_DISPLACE) to = (to + 2u) & 15u;
            if (x == AB_ANS_OCT_DOWN) d -= 12;
            if (x == AB_ANS_SPARSE && (k++ & 1u)) continue;
            want[to] = (uint8_t)d;
        }
        play_bar1();
        go_to(32u + 8u);
        CHECKF(L.cr_play == 2 && L.fv.near == (uint8_t)L.cr[1].node, "the answer in C's sound (%s)", ab_answer_name[x]);
        go_to(48u + 2u);
        ok = 1;
        for (s = 0; s < 16u; s++) {
            if (ev_on[0][s] || ev_on[1][s]) ok = 0;                      /* your bars: the steps are silent */
            if (ev_on[2][s] != (want[s] != 0) || (want[s] && ev_data[2][s] != want[s])) ok = 0;
        }
        CHECKF(ok, "the answer %s", ab_answer_name[x]);
    }
    /* a silent bar gets no answer */
    go_to(64u + 14u);
    for (s = 0, ok = 1; s < 16u; s++) if (ev_on[3][s] || ev_on[4][s]) ok = 0;
    CHECK(ok && !L.ans_valid);
}

TEST(test_ab_pocket)
{
    const js_track_t *t;
    uint32_t s, ok = 1, n;
    start();
    hello();
    ab_link_packet(&L, jl_cc(&L.link, JL_CC_DENSITY, 100), 0);
    CHECKF(fabsf(L.drum_density - 100.0f / 127.0f) < 0.001f, "density %f", (double)L.drum_density);
    L.drum_density = 0.5f; /* neither thinner nor fuller */
    L.pocket_amt = 1.0f;
    t = &L.pat[0].t[AB_T_BASS];
    go(0.005f); /* the transport started */
    go_link(0); ab_link_kick(&L, 120);
    go_link(3); ab_link_kick(&L, 120);
    go_link(16u + 1u);
    CHECKF(ab_pocket_mask(&L, 1) == 0x9u, "the kicks learned %x", ab_pocket_mask(&L, 1));
    go_link(32u + 1u);
    /* bar 1: the bass's notes on 1 and 4 moved to the next gaps (2 and 5), the rest as the line */
    for (s = 0; s < 16u; s++) {
        int line = (t->s[s].flags & JS_ON) != 0, want = line && s != 0u && s != 3u;
        if (s == 1u || s == 4u) want = 1;
        if (ev_on[1][s] != want) ok = 0;
    }
    CHECKF(ok && ev_data[1][1] == t->s[0].data && ev_data[1][4] == t->s[3].data, "the bass around the kicks");
    /* bar 2 (no kicks in bar 1): the line */
    go_link(48u + 1u);
    CHECK(bar_is_line(2, t));
    /* busy drums: the bass thins to its downbeat; sparse drums: it fills in */
    L.drum_density = 1.0f;
    go_link(64u + 1u);
    for (s = 0, n = 0; s < 16u; s++) n += ev_on[3][s];
    CHECKF(n == 1 && ev_on[3][0], "busy drums: %u bass notes", n);
    L.drum_density = 0.0f;
    go_link(80u + 1u);
    for (s = 0, n = 0; s < 16u; s++) n += ev_on[4][s];
    CHECKF(n > 7u, "sparse drums: %u bass notes (the line has 7)", n);
    /* NO LINK: POCKET does nothing (kicks by hand, no link heard) */
    start();
    L.pocket_amt = 1.0f;
    L.drum_density = 0.5f;
    go(0.005f);
    go_to(0); ab_link_kick(&L, 120);
    go_to(3); ab_link_kick(&L, 120);
    go_to(32u + 1u);
    CHECK(!jl_alive(&L.link, L.ms) && bar_is_line(1, &L.pat[0].t[AB_T_BASS]));
}

TEST(test_ab_gesture_echo)
{
    start();
    L.gesture = 1;
    go_to(8);
    ab_set_perf(&L, AB_FX_OCT_UP);   /* held from 8 to 24 */
    go_to(24);
    ab_set_perf(&L, AB_FX_NONE);
    go_to(40);
    ab_gesture_knob(&L, 0, 1.0f);    /* CHAR to the top (MORPH stays 0.4) */
    go_to(64u + 12u);
    CHECKF(L.perf == AB_FX_OCT_UP, "the FX came back (%d)", L.perf);
    go_to(64u + 20u);
    CHECKF(L.perf == AB_FX_NONE, "for half as long (%d)", L.perf);
    go_to(64u + 44u);
    CHECKF(fabsf(L.echo_off[0] - 0.5f * (1.0f - 0.4f)) < 0.01f, "the knob at half depth %f", (double)L.echo_off[0]);
    go_to(128u + 44u);
    CHECK(L.echo_off[0] == 0.0f && L.perf == AB_FX_NONE); /* nothing played in that phrase: nothing comes back */
    /* off: no echo */
    L.gesture = 0;
    go_to(192u + 1u);
    CHECK(L.g_nplay == 0);
}

TEST(test_ab_tape_decay)
{
    js_step_t *a, *b;
    uint32_t bar, ticks = 0, fall = 1;
    uint8_t last;
    start();
    CHECK(ab_decay_tick(&L) == 0); /* off */
    L.decay_on = 1;
    L.decay_amt = 0.3f;
    go_to(5);
    CHECK(ab_record(&L, AB_T_BASS, 40, 1.0f) >= 0);
    a = &L.pat[0].t[AB_T_BASS].s[(L.seq.pos[AB_T_BASS] + 15u) % 16u];
    b = &L.pat[0].t[AB_T_BASS].s[8];
    b->flags = (uint8_t)(b->flags | JS_DECAY | JS_BURN);
    CHECK((a->flags & JS_DECAY) && (a->flags & JS_ON));
    last = a->vel;
    for (bar = 1; bar < 40u && (a->flags & JS_ON); bar++) {
        go_to(bar * 16u + 1u);
        ticks += (uint32_t)ab_decay_tick(&L);
        CHECK(ab_decay_tick(&L) == 0); /* once a bar */
        if ((a->flags & JS_ON) && a->vel >= last) fall = 0;
        last = a->vel;
    }
    CHECKF(fall && !(a->flags & JS_ON) && ticks > 5u && ticks < 30u, "faded out after %u bars", ticks);
    CHECK((b->flags & JS_ON) && b->vel == 124);       /* burned in: it stays */
}

void run_ab_live(void)
{
    RUN(test_ab_energy);
    RUN(test_ab_energy_curve);
    RUN(test_ab_answer_me);
    RUN(test_ab_pocket);
    RUN(test_ab_gesture_echo);
    RUN(test_ab_tape_decay);
    RUN(test_ab_form_variations);
    RUN(test_ab_perf_fx);
    RUN(test_ab_fills);
    RUN(test_ab_record_density_humanize);
    RUN(test_ab_undo_sound);
    RUN(test_ab_link_helpers);
    RUN(test_ab_conditions);
    RUN(test_ab_library);
}
