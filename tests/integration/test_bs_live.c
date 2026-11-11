/* SPDX-License-Identifier: MPL-2.0 */
/* (0.5) the sequencer-first controls and the live tools of Break Surgeon: FORM and variations on the bar grid, the
 * fill types and the phrase-end fill, trig conditions in the engine, RECORD's quantization, ERASE, DENSITY and
 * HUMANIZE, MUTATE on a drum track, TAPE STOP, and undo / redo across them (shared/seq/jo_undo.h, as app.c uses it) */
#include "../jo_test.h"
#include "../../firmware/break-surgeon/engine/bs_engine.h"
#include "../../shared/seq/jo_undo.h"

static bs_engine_t E, E2;
static int16_t pcm[70000], room[JF_VERB_TOTAL];
static int32_t lr[2 * 32];

/* what fired, per bar and track: a count and a per-16th mask of the first hits */
#define NB 16
static uint32_t n_ev[NB][BS_NTRACKS], mask[NB][BS_NTRACKS];
static uint8_t step_at[NB][16];   /* (0.6) the BREAK step played at each 16th (its index: a route shows here) */
static void tr(void *ctx, const js_event_t *ev)
{
    uint32_t bar = ev->abs_step >> 4;
    (void)ctx;
    if (bar < NB && ev->track < BS_NTRACKS) {
        n_ev[bar][ev->track]++;
        if (!ev->hit) mask[bar][ev->track] |= 1u << (ev->abs_step & 15u);
        if (!ev->hit && ev->track == BS_T_BREAK) step_at[bar][ev->abs_step & 15u] = (uint8_t)ev->step;
    }
}
static float bar_s(void) { return 4.0f * 60.0f / 174.0f; }
static void go(bs_engine_t *e, float seconds)
{
    uint32_t i, n = (uint32_t)(seconds * JO_SR / 32.0f);
    for (i = 0; i < n; i++) bs_render(e, lr, 32, 0.9f);
}
static void start(bs_engine_t *e)
{
    memset(n_ev, 0, sizeof n_ev);
    memset(mask, 0, sizeof mask);
    bs_init(e, pcm, 0, 0);
    bs_set_room_buf(e, room);
    e->trace = tr;
    bs_cmd_bpm(e, 174.0f);
}
static int same_pat(const js_pattern_t *a, const js_pattern_t *b) { return memcmp(a, b, sizeof *a) == 0; }

/* FORM A B A C: the bars play patterns 1 2 1 3, on the global 4-bar grid; a variation picked by hand lands at the bar */
TEST(test_form_and_variations)
{
    uint32_t bar;
    int seen[6];
    start(&E);
    bs_cmd_form(&E, 1);
    bs_cmd_play(&E, 1);
    for (bar = 0; bar < 6u; bar++) {
        go(&E, bar_s() * 0.5f);
        seen[bar] = E.cur_pat;
        go(&E, bar_s() * 0.5f);
    }
    /* bar 0 starts on P1 (the form switches at bar starts after the first) */
    CHECKF(seen[0] == 0 && seen[1] == 1 && seen[2] == 0 && seen[3] == 2 && seen[4] == 0 && seen[5] == 1,
           "form bars %d %d %d %d %d %d", seen[0], seen[1], seen[2], seen[3], seen[4], seen[5]);
    CHECK(bs_form_part(&E, 3) == 2 && bs_form_part(&E, 7) == 2);
    /* off, then variation D by hand: at the next bar, not before */
    bs_cmd_form(&E, 0);
    bs_cmd_pattern_q(&E, 3, BS_Q_BAR);
    go(&E, 0.01f);
    CHECK(E.next_pat == 3 && E.cur_pat != 3);
    go(&E, bar_s() * 1.05f);
    CHECK(E.cur_pat == 3);
    /* a pattern at the phrase's end: phrase 4 bars, queued in bar 6 -> lands at bar 8 */
    bs_cmd_pattern_q(&E, 1, BS_Q_PHRASE);
    go(&E, 0.01f);
    CHECKF(E.pending_at == 8u * 16u, "phrase end %u", E.pending_at);
    CHECK(strcmp(bs_form_name[1], "A B A C") == 0);
}

/* the fill types, and ENV's tap: a fill in the phrase's last bar */
TEST(test_fill_types)
{
    uint32_t s;
    int build_all = 1;
    /* DROP OUT: no break, kick or snare in the fill bar; they are back after it */
    start(&E);
    bs_cmd_play(&E, 1);
    go(&E, bar_s() * 0.5f);
    bs_cmd_fill_ex(&E, 1, BS_FILL_DROP, 0);
    go(&E, bar_s() * 2.6f);
    CHECKF(n_ev[1][BS_T_BREAK] == 0 && n_ev[1][BS_T_KICK] == 0 && n_ev[1][BS_T_SNARE] == 0, "drop bar: break %u kick %u snare %u",
           n_ev[1][BS_T_BREAK], n_ev[1][BS_T_KICK], n_ev[1][BS_T_SNARE]);
    CHECK(n_ev[0][BS_T_BREAK] > 0 && n_ev[2][BS_T_BREAK] > 0 && n_ev[1][BS_T_HATS] > 0);
    CHECKF(n_ev[2][BS_T_IMPACT] > 0, "the impact on the drop: %u", n_ev[2][BS_T_IMPACT]);
    /* SNARE BUILD: the snare on every 16th of the fill bar; the kick only on its first */
    start(&E);
    bs_cmd_play(&E, 1);
    go(&E, bar_s() * 0.5f);
    bs_cmd_fill_ex(&E, 1, BS_FILL_BUILD, 0);
    go(&E, bar_s() * 2.0f);
    for (s = 0; s < 16u; s++) if (!(mask[1][BS_T_SNARE] >> s & 1u)) build_all = 0;
    CHECKF(build_all, "snare build mask %04x", mask[1][BS_T_SNARE]);
    CHECKF(mask[1][BS_T_KICK] == 1u, "kick in the build %04x", mask[1][BS_T_KICK]);
    /* CHOP: the break keeps playing, more hits (ratchets) than the bar before */
    start(&E);
    bs_cmd_play(&E, 1);
    go(&E, bar_s() * 0.5f);
    bs_cmd_fill_ex(&E, 1, BS_FILL_CHOP, 0);
    go(&E, bar_s() * 2.0f);
    CHECKF(n_ev[1][BS_T_BREAK] > n_ev[0][BS_T_BREAK], "chop %u vs %u", n_ev[1][BS_T_BREAK], n_ev[0][BS_T_BREAK]);
    /* at the phrase's end: phrase 4 -> the fill is bar 3; with 2 bars, bars 2-3; too late -> the next phrase's */
    start(&E);
    bs_cmd_play(&E, 1);
    go(&E, bar_s() * 0.5f);
    bs_cmd_fill_ex(&E, 1, BS_FILL_ROLL, 1);
    go(&E, 0.01f);
    CHECKF(E.fill_start == 48u && E.fill_end == 64u, "phrase-end fill %u..%u", E.fill_start, E.fill_end);
    bs_cmd_fill_ex(&E, 2, BS_FILL_ROLL, 1);
    go(&E, 0.01f);
    CHECKF(E.fill_start == 32u, "2-bar phrase-end fill at %u", E.fill_start);
    go(&E, bar_s() * 2.8f); /* now in bar 3 */
    bs_cmd_fill_ex(&E, 2, BS_FILL_ROLL, 1);
    go(&E, 0.01f);
    CHECKF(E.fill_start == 96u, "too late: the next phrase's (%u)", E.fill_start);
    bs_set_phrase(&E, 8);
    CHECK(E.phrase == 8 && E.seq.phrase == 8);
}

/* trig conditions as the engine plays them: 1:2 on the kick's downbeat, FILL on a snare step */
TEST(test_conditions_in_engine)
{
    js_track_t *k, *s;
    start(&E);
    k = &E.pat[0].t[BS_T_KICK];
    s = &E.pat[0].t[BS_T_SNARE];
    k->s[0].cond = JS_C_1_2;
    s->s[14].flags = JS_ON, s->s[14].vel = 100, s->s[14].cond = JS_C_FILL;
    bs_cmd_play(&E, 1);
    go(&E, bar_s() * 0.5f);
    bs_cmd_fill_ex(&E, 1, BS_FILL_CHOP, 0); /* (CHOP leaves the drums as they are) */
    go(&E, bar_s() * 3.4f);
    CHECKF((mask[0][BS_T_KICK] & 1u) && !(mask[1][BS_T_KICK] & 1u) && (mask[2][BS_T_KICK] & 1u),
           "1:2 kick %04x %04x %04x", mask[0][BS_T_KICK], mask[1][BS_T_KICK], mask[2][BS_T_KICK]);
    CHECKF((mask[1][BS_T_SNARE] >> 14 & 1u) && !(mask[0][BS_T_SNARE] >> 14 & 1u) && !(mask[2][BS_T_SNARE] >> 14 & 1u),
           "FILL snare %04x %04x %04x", mask[0][BS_T_SNARE], mask[1][BS_T_SNARE], mask[2][BS_T_SNARE]);
}

/* RECORD: a hit in a step's first half goes to that step, in its second half to the next; stopped, nothing is written */
TEST(test_record_and_erase)
{
    js_track_t *k;
    int a, b, c;
    start(&E);
    k = &E.pat[0].t[BS_T_KICK];
    CHECK(bs_record(&E, BS_T_KICK, 0.8f, 0) == -1);
    bs_cmd_play(&E, 1);
    while (!(E.clock.running && E.clock.abs_step == 5u && E.clock.phase < 0x40000000u)) bs_render(&E, lr, 32, 0.9f);
    a = bs_record(&E, BS_T_KICK, 0.8f, 0);
    while (!(E.clock.abs_step == 6u && E.clock.phase > 0xB0000000u)) bs_render(&E, lr, 32, 0.9f);
    b = bs_record(&E, BS_T_KICK, 0.5f, 0);
    CHECKF(a == 5 && b == 7, "recorded at %d and %d", a, b);
    CHECK((k->s[5].flags & JS_ON) && k->s[5].vel == 102 && (k->s[7].flags & JS_ON) && k->s[7].vel == 64);
    /* the break: the slice played is the one written */
    while (!(E.clock.abs_step == 9u && E.clock.phase < 0x40000000u)) bs_render(&E, lr, 32, 0.9f);
    c = bs_record(&E, BS_T_BREAK, 0.86f, 12);
    CHECK(c == 9 && E.pat[0].t[BS_T_BREAK].s[9].data == 12);
    /* recorded, it plays on the next pass */
    go(&E, bar_s() * 1.2f);
    CHECKF(mask[1][BS_T_KICK] >> 5 & 1u, "the recorded kick plays: %04x", mask[1][BS_T_KICK]);
    /* ERASE */
    bs_erase_step(&E, BS_T_KICK, 5);
    CHECK(!(k->s[5].flags & JS_ON) && k->s[5].cond == 0 && k->s[5].vel == 100);
    bs_erase_track(&E, BS_T_SNARE);
    {
        uint32_t i, any = 0;
        for (i = 0; i < JS_MAXSTEPS; i++) any |= E.pat[0].t[BS_T_SNARE].s[i].flags & JS_ON;
        CHECK(!any);
    }
}

/* DENSITY: deterministic; an empty track fills the downbeat first, then the beats; thinning takes the 16ths first.
 * HUMANIZE: the same amount gives the same pattern, 0 puts it back on the grid */
TEST(test_density_humanize)
{
    js_track_t *p;
    uint32_t i, ok = 1;
    start(&E);
    start(&E2);
    bs_erase_track(&E, BS_T_PERC);
    bs_erase_track(&E2, BS_T_PERC);
    CHECK(bs_density_step(&E, BS_T_PERC, -1) == -1);
    CHECK(bs_density_step(&E, BS_T_PERC, 1) == 0);
    for (i = 0; i < 3u; i++) {
        int s = bs_density_step(&E, BS_T_PERC, 1);
        if (s != 4 && s != 8 && s != 12) ok = 0;
    }
    CHECKF(ok, "the beats come second");
    for (i = 0; i < 4u; i++) bs_density_step(&E2, BS_T_PERC, 1);
    CHECK(same_pat(&E.pat[0], &E2.pat[0]));
    /* a full track thins out its 16ths first, the downbeat last */
    for (i = 0; i < 16u; i++) bs_density_step(&E, BS_T_PERC, 1);
    CHECK(bs_density_step(&E, BS_T_PERC, 1) == -1);
    {
        int s = bs_density_step(&E, BS_T_PERC, -1);
        CHECKF(s & 1, "first removed: a 16th (%d)", s);
    }
    for (i = 0; i < 14u; i++) bs_density_step(&E, BS_T_PERC, -1);
    CHECK(E.pat[0].t[BS_T_PERC].s[0].flags & JS_ON);
    /* HUMANIZE */
    p = &E.pat[0].t[BS_T_KICK];
    bs_humanize(&E, BS_T_KICK, 0.6f);
    E2.pat[0] = E.pat[0];
    bs_humanize(&E, BS_T_KICK, 0.6f);
    CHECK(same_pat(&E.pat[0], &E2.pat[0]));
    {
        int off = 0;
        for (i = 0; i < 16u; i++) if ((p->s[i].flags & JS_ON) && (p->s[i].micro != 0 || (p->s[i].vel != 100 && p->s[i].vel != 122))) off = 1;
        CHECK(off);
    }
    bs_humanize(&E, BS_T_KICK, 0.0f);
    for (i = 0, ok = 1; i < 16u; i++)
        if ((p->s[i].flags & JS_ON) && (p->s[i].micro != 0 || (p->s[i].vel != 100 && p->s[i].vel != 122 && p->s[i].vel != 60 && p->s[i].vel != 38))) ok = 0;
    CHECK(ok);
}

/* undo / redo across a groove, a mutation of a drum track, a density step and an erase (the app's way: a snapshot
 * before each); MUTATE on a drum track keeps the number of hits and the beats */
static ju_stack_t U;
TEST(test_undo_across_actions)
{
    js_pattern_t p0, p1, p2, p3;
    uint8_t slot;
    uint32_t i, n0 = 0, n1 = 0;
    start(&E);
    ju_init(&U);
    p0 = E.pat[0];
    ju_push(&U, 0, &E.pat[0], 0, 0); bs_groove(&E, 0); p1 = E.pat[0];
    for (i = 0; i < 16u; i++) n0 += (E.pat[0].t[BS_T_HATS].s[i].flags & JS_ON) != 0;
    ju_push(&U, 0, &E.pat[0], 0, 0); bs_mutate_track(&E, BS_T_HATS, 1.0f, 99); p2 = E.pat[0];
    for (i = 0; i < 16u; i++) n1 += (E.pat[0].t[BS_T_HATS].s[i].flags & JS_ON) != 0;
    CHECKF(!same_pat(&p1, &p2) && n0 == n1, "mutate moved hits (%u -> %u)", n0, n1);
    ju_push(&U, 0, &E.pat[0], 0, 0); bs_density_step(&E, BS_T_KICK, 1);
    ju_push(&U, 0, &E.pat[0], 0, 0); bs_erase_track(&E, BS_T_SNARE); p3 = E.pat[0];
    CHECK(ju_undo(&U, E.pat, 0, 0, &slot) == 0 && slot == 0);
    CHECK(ju_undo(&U, E.pat, 0, 0, &slot) == 0);
    CHECK(ju_undo(&U, E.pat, 0, 0, &slot) == 0 && same_pat(&E.pat[0], &p1));
    CHECK(ju_undo(&U, E.pat, 0, 0, &slot) == 0 && same_pat(&E.pat[0], &p0));
    CHECK(ju_undo(&U, E.pat, 0, 0, &slot) == -1);
    CHECK(ju_redo(&U, E.pat, 0, 0, &slot) == 0 && same_pat(&E.pat[0], &p1));
    CHECK(ju_redo(&U, E.pat, 0, 0, &slot) == 0 && same_pat(&E.pat[0], &p2));
    ju_redo(&U, E.pat, 0, 0, &slot);
    CHECK(ju_redo(&U, E.pat, 0, 0, &slot) == 0 && same_pat(&E.pat[0], &p3));
}

/* TAPE STOP: the drums stop at once, the break slows to a stop; let go, it is back at speed */
TEST(test_tape_stop)
{
    uint32_t before;
    start(&E);
    bs_cmd_play(&E, 1);
    go(&E, bar_s());
    bs_cmd_perf(&E, BS_PERF_TAPESTOP);
    go(&E, 0.05f);
    before = n_ev[1][BS_T_KICK] + n_ev[1][BS_T_SNARE] + n_ev[2][BS_T_KICK] + n_ev[2][BS_T_SNARE];
    go(&E, 1.0f);
    CHECKF(E.tape < 0.01f, "tape %f", (double)E.tape);
    CHECK(n_ev[1][BS_T_KICK] + n_ev[1][BS_T_SNARE] + n_ev[2][BS_T_KICK] + n_ev[2][BS_T_SNARE] == before);
    bs_cmd_perf(&E, BS_PERF_CLEAN);
    go(&E, 0.01f);
    CHECK(E.tape == 1.0f && E.perf == BS_PERF_CLEAN);
    CHECK(strcmp(bs_perf_name[BS_PERF_TAPESTOP], "TAPE STOP") == 0);
}

/* ============================================================== (0.6) === */
/* ROUTES through the engine: on BREAK, each route's 16ths play the steps js_route_idx gives, every 16th of the bar
 * still plays (the bar is kept), and the other tracks stay forward */
TEST(test_routes_engine)
{
    uint32_t r, p, bad = 0, full = 1;
    for (r = 0; r < JS_NROUTES - 1u; r++) { /* (CUSTOM below) */
        start(&E);
        memset(step_at, 0xFF, sizeof step_at);
        bs_route_live(&E, 1u << BS_T_BREAK, (uint8_t)r);
        bs_cmd_play(&E, 1);
        go(&E, bar_s() * 2.5f);
        for (p = 0; p < 16u; p++) {
            uint32_t want = js_route_idx(&E.pat[0].t[BS_T_BREAK], (uint8_t)r, p, 16, 1, E.pat[0].seed);
            if (step_at[1][p] != want) bad++;
        }
        if (n_ev[1][BS_T_BREAK] < 16u || mask[1][BS_T_BREAK] != 0xFFFFu) full = 0;
        if (mask[1][BS_T_KICK] != mask[0][BS_T_KICK]) full = 0; /* the kick unrouted */
    }
    CHECKF(bad == 0, "%u routed 16ths off", bad);
    CHECK(full);
    /* the route ends: forward again */
    bs_route_live(&E, 0x7Fu, 0xFFu);
    go(&E, bar_s());
    CHECK(step_at[3][5] == 5 && bs_route_at(&E, BS_T_BREAK, 5) == 5);
}

/* a drawn CUSTOM route, live; then latched (written into the track) and undone */
TEST(test_custom_route_latch_undo)
{
    static const uint8_t ORD[4] = {4, 0, 12, 8};
    uint32_t p, bad = 0;
    uint8_t slot;
    start(&E);
    ju_init(&U);
    memset(step_at, 0xFF, sizeof step_at);
    bs_route_draw(&E, 1u << BS_T_BREAK, ORD, 4);
    bs_route_live(&E, 1u << BS_T_BREAK, JS_R_CUSTOM);
    bs_cmd_play(&E, 1);
    go(&E, bar_s() * 1.5f);
    for (p = 0; p < 16u; p++) if (step_at[0][p] != ORD[p % 4u]) bad++;
    CHECKF(bad == 0, "custom route: %u off", bad);
    CHECK(E.pat[0].t[BS_T_BREAK].route == JS_R_FORWARD); /* live: the track's own route untouched */
    ju_push(&U, 0, &E.pat[0], 0, 0);
    bs_route_latch(&E, 1u << BS_T_BREAK, JS_R_CUSTOM);
    CHECK(E.pat[0].t[BS_T_BREAK].route == JS_R_CUSTOM && E.seq.route_ovr[BS_T_BREAK] == 0xFFu);
    go(&E, bar_s() * 1.6f);
    for (p = 0, bad = 0; p < 16u; p++) if (step_at[2][p] != ORD[p % 4u]) bad++;
    CHECKF(bad == 0, "latched custom: %u off", bad);
    CHECK(ju_undo(&U, E.pat, 0, 0, &slot) == 0 && E.pat[0].t[BS_T_BREAK].route == JS_R_FORWARD);
}

/* ENERGY's levels: AUTO LEVELS from the role, ENERGY's level gates the steps as the engine plays them */
TEST(test_energy_levels)
{
    const js_pattern_t *p;
    start(&E);
    p = &E.pat[0];
    /* the factory patterns come with levels: the kick's downbeat 1, its 3-and 2, the ghosts 3, hats' 8ths 2 */
    CHECK(p->t[BS_T_KICK].s[0].lvl == 1 && p->t[BS_T_KICK].s[10].lvl == 2);
    CHECK(p->t[BS_T_SNARE].s[4].lvl == 1 && p->t[BS_T_SNARE].s[12].lvl == 1);
    CHECK(p->t[BS_T_HATS].s[6].lvl == 2 && p->t[BS_T_BREAK].s[1].lvl == 3 && p->t[BS_T_BREAK].s[8].lvl == 1);
    E.pat[0].t[BS_T_KICK].s[3].flags = JS_ON, E.pat[0].t[BS_T_KICK].s[3].vel = 100, E.pat[0].t[BS_T_KICK].s[3].ratch = 2;
    bs_auto_levels(&E, BS_T_KICK);
    CHECK(E.pat[0].t[BS_T_KICK].s[3].lvl == 4);
    bs_set_energy(&E, 0.1f); /* level 1: the skeleton */
    CHECK(E.seq.energy == 1);
    bs_cmd_play(&E, 1);
    go(&E, bar_s() * 1.5f);
    CHECKF(mask[0][BS_T_KICK] == 0x0001u && mask[0][BS_T_SNARE] == 0x1010u && mask[0][BS_T_HATS] == 0,
           "level 1: kick %04x snare %04x hats %04x", mask[0][BS_T_KICK], mask[0][BS_T_SNARE], mask[0][BS_T_HATS]);
    CHECKF(mask[0][BS_T_BREAK] == 0x1111u, "level 1 break %04x", mask[0][BS_T_BREAK]);
    bs_set_energy(&E, 1.0f);
    go(&E, bar_s() * 1.6f);
    CHECKF(mask[2][BS_T_KICK] == 0x0409u && mask[2][BS_T_BREAK] == 0xFFFFu, "full: kick %04x break %04x",
           mask[2][BS_T_KICK], mask[2][BS_T_BREAK]);
}

/* the ENERGY curve: armed, it records the next phrase's knob per beat, then plays it every phrase */
TEST(test_energy_curve)
{
    uint32_t bar;
    start(&E);
    bs_energy_curve(&E, 1);
    CHECK(bs_energy_curve_mode(&E) == 1);
    bs_cmd_play(&E, 1);
    for (bar = 0; bar < 8u; bar++) { /* phrase 1 (bars 0-3): not yet; phrase 2 (4-7): recorded */
        E.nrg.knob = bar == 4u || bar == 5u ? 0.1f : 0.95f;
        go(&E, bar_s());
    }
    go(&E, bar_s() * 0.05f);
    CHECK(bs_energy_curve_mode(&E) == 2 && E.nrg.beats == 16u);
    E.nrg.knob = 0.5f; /* the knob elsewhere: the curve plays */
    go(&E, bar_s() * 1.5f);           /* into bar 9 = phrase bar 1, recorded at 0.1 */
    CHECKF(E.seq.energy == 1, "curve bar 1 level %u", E.seq.energy);
    CHECKF(mask[9][BS_T_HATS] == 0 && mask[9][BS_T_KICK] == 0x0001u, "curve bar 1: hats %04x kick %04x",
           mask[9][BS_T_HATS], mask[9][BS_T_KICK]);
    go(&E, bar_s());                  /* bar 10: phrase bar 2, recorded at 0.95 */
    CHECKF(E.seq.energy == 4, "curve bar 2 level %u", E.seq.energy);
    bs_set_energy(&E, 0.7f);          /* the hand takes over */
    CHECK(bs_energy_curve_mode(&E) == 0);
}

/* DNA: breed, select at the bar, keep, kill with the children, cross, cull (the oldest unkept leaf), store */
static bs_dna_t D;
TEST(test_dna)
{
    int a, b, c, x, i, kept;
    start(&E);
    bs_dna_root(&E, &D);
    CHECK(bs_dna_count(&D) == 1 && D.playing == 0 && same_pat(&D.g[0].pat, &E.pat[0]));
    bs_cmd_play(&E, 1);
    go(&E, bar_s() * 0.3f);
    a = bs_dna_breed(&E, &D, 0.8f, 11);
    CHECK(a == 1 && D.g[a].parent == 0 && D.g[a].gen == 1 && D.playing == a);
    CHECK(!same_pat(&D.g[a].pat, &D.g[0].pat));
    CHECK(same_pat(&E.pat[0], &D.g[0].pat));  /* not yet: at the next bar */
    go(&E, bar_s());
    CHECK(same_pat(&E.pat[0], &D.g[a].pat));
    b = bs_dna_breed(&E, &D, 0.8f, 12);       /* a grandchild */
    CHECK(b == 2 && D.g[b].parent == a && D.g[b].gen == 2);
    /* cross: A = the root (its drums), B = the grandchild (its break) */
    x = bs_dna_cross(&E, &D, 0, b);
    CHECK(x == 3 && memcmp(&D.g[x].pat.t[BS_T_KICK], &D.g[0].pat.t[BS_T_KICK], sizeof(js_track_t)) == 0);
    CHECK(memcmp(&D.g[x].pat.t[BS_T_BREAK], &D.g[b].pat.t[BS_T_BREAK], sizeof(js_track_t)) == 0);
    /* kill the child: its grandchild goes too; playing (the cross, a child of the root) stays */
    CHECK(bs_dna_kill(&E, &D, a) == 0 && !D.g[a].used && !D.g[b].used && D.g[x].used && D.playing == x);
    CHECK(bs_dna_kill(&E, &D, 0) == -1);      /* the root stays */
    /* the playing one killed: back to its parent */
    bs_dna_select(&E, &D, x);
    c = bs_dna_breed(&E, &D, 0.5f, 13);
    CHECK(bs_dna_kill(&E, &D, c) == 0 && D.playing == x);
    /* fill the tree, a star on one leaf: breeding on culls the oldest unkept leaf, never the root or the star */
    bs_dna_select(&E, &D, 0);
    c = bs_dna_breed(&E, &D, 0.5f, 20);
    bs_dna_keep(&D, c);
    bs_dna_select(&E, &D, 0);
    for (i = 0; i < 30; i++) { bs_dna_breed(&E, &D, 0.5f, 100u + (uint32_t)i); bs_dna_select(&E, &D, 0); }
    kept = D.g[c].used && D.g[c].keep;
    CHECK(bs_dna_count(&D) == BS_DNA_MAX && D.g[0].used && kept);
    /* store a genome into P3 */
    bs_dna_store(&E, &D, c, 2);
    CHECK(same_pat(&E.pat[2], &D.g[c].pat));
}

/* DENSITY goes to the link once a bar; ENERGY when it moves (and not back when it was heard) */
static uint32_t n_dens, n_nrg, last_nrg, last_dens;
static void out_cc(uint32_t pkt)
{
    jl_link_t l;
    jl_event_t ev;
    jl_init(&l, 15);
    if (!jl_decode(&l, pkt, 0, &ev)) return;
    if (ev.type == JL_EV_DENSITY) n_dens++, last_dens = ev.a;
    if (ev.type == JL_EV_ENERGY) n_nrg++, last_nrg = ev.a;
}
TEST(test_link_density_energy)
{
    start(&E);
    n_dens = n_nrg = 0;
    E.midi_out = out_cc;
    bs_cmd_play(&E, 1);
    go(&E, bar_s() * 4.2f);
    CHECKF(n_dens == 4u && last_dens > 10u, "density msgs %u last %u", n_dens, last_dens);
    CHECK(n_nrg == 0); /* full and unchanged: nothing sent */
    bs_set_energy(&E, 0.3f);
    go(&E, bar_s() * 0.2f);
    CHECKF(n_nrg == 1u && last_nrg == 38u, "energy msgs %u value %u", n_nrg, last_nrg);
    bs_follow_energy(&E, 0.8f);
    go(&E, bar_s() * 0.2f);
    CHECK(n_nrg == 1u && E.seq.energy == 4);
    E.midi_out = 0;
}

/* TAPE DECAY: a recorded hit fades bar by bar and goes; a burned one stays */
TEST(test_tape_decay)
{
    int s1, s2;
    uint32_t i;
    js_track_t *k;
    start(&E);
    E.decay_on = 1, E.decay_amt = 64;
    bs_cmd_play(&E, 1);
    go(&E, bar_s() * 0.4f);
    s1 = bs_record(&E, BS_T_PERC, 1.0f, 0);
    go(&E, bar_s() * 0.25f);
    s2 = bs_record(&E, BS_T_PERC, 1.0f, 0);
    k = &E.pat[0].t[BS_T_PERC];
    CHECK(s1 >= 0 && s2 >= 0 && s1 != s2 && (k->s[s1].flags & JS_DECAY) && (k->s[s2].flags & JS_DECAY));
    bs_burn_step(&E, BS_T_PERC, s2);
    for (i = 0; i < 3u; i++) bs_decay_bar(&E);
    CHECK((k->s[s1].flags & JS_ON) && k->s[s1].vel < 127u && k->s[s2].vel == 127u);
    for (i = 0; i < 20u; i++) bs_decay_bar(&E);
    CHECK(!(k->s[s1].flags & JS_ON) && (k->s[s2].flags & JS_ON) && (k->s[s2].flags & JS_BURN));
    E.decay_on = 0;
    s1 = bs_record(&E, BS_T_PERC, 1.0f, 0);
    CHECK(!(k->s[s1].flags & JS_DECAY)); /* decay off: recordings stay */
}

void run_bs_live(void)
{
    RUN(test_routes_engine);
    RUN(test_custom_route_latch_undo);
    RUN(test_energy_levels);
    RUN(test_energy_curve);
    RUN(test_dna);
    RUN(test_link_density_energy);
    RUN(test_tape_decay);
    RUN(test_form_and_variations);
    RUN(test_fill_types);
    RUN(test_conditions_in_engine);
    RUN(test_record_and_erase);
    RUN(test_density_humanize);
    RUN(test_undo_across_actions);
    RUN(test_tape_stop);
}
