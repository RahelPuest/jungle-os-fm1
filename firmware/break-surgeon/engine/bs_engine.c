/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
#include "bs_engine.h"

const char *const bs_track_name[BS_NTRACKS] = {"BREAK", "KICK", "SNARE", "GHOST", "HATS", "PERC", "IMPACT"};
const char *const bs_perf_name[BS_NPERF] = {"CLEAN", "ROLL", "CHOP", "REVERSE", "GLITCH", "DESTROY", "FREEZE"};

enum { C_PLAY, C_BPM, C_PERF, C_FILL, C_RETURN, C_PATTERN, C_SCENE, C_SLICE, C_DRUM, C_MUTE, C_PANIC };

static uint32_t nolock(void) { return 0; }
static void nounlock(uint32_t f) { (void)f; }
static uint32_t (*lock_fn)(void) = nolock;
static void (*unlock_fn)(uint32_t) = nounlock;
void bs_set_lock_hooks(uint32_t (*l)(void), void (*u)(uint32_t))
{
    lock_fn = l ? l : nolock;
    unlock_fn = u ? u : nounlock;
}

static inline float unit(uint32_t h) { return (float)(h >> 8) * (1.0f / 16777216.0f); }

/* ---------------------------------------------------------------- graphs --- */
static const jg_param_t RPARAM[BS_NRPARAMS] = {
    {"CHOP", 0.0f, 1.0f, JG_LIN, 0.3f},  {"RATCH", 0.0f, 1.0f, JG_LIN, 0.3f}, {"REV", 0.0f, 1.0f, JG_LIN, 0.3f},
    {"STUTTER", 0.0f, 1.0f, JG_LIN, 0.3f}, {"GHOST", 0.0f, 1.0f, JG_LIN, 0.3f}, {"HATS", 0.0f, 1.0f, JG_LIN, 0.3f},
    {"GATE", 0.3f, 1.0f, JG_LIN, 0.2f},  {"PITCH", 0.0f, 1.0f, JG_LIN, 0.3f},
};
static const jg_param_t KPARAM[BS_NKPARAMS] = {
    {"K WEIGHT", 0, 1, JG_LIN, 0}, {"K PUNCH", 0, 1, JG_LIN, 0}, {"K PITCH", 0, 1, JG_LIN, 0}, {"K DRIVE", 0, 1, JG_LIN, 0},
    {"S BODY", 0, 1, JG_LIN, 0},   {"S CRACK", 0, 1, JG_LIN, 0}, {"S TAIL", 0, 1, JG_LIN, 0},  {"S PRESS", 0, 1, JG_LIN, 0},
    {"H METAL", 0, 1, JG_LIN, 0},  {"H BRIGHT", 0, 1, JG_LIN, 0},{"H LENGTH", 0, 1, JG_LIN, 0},{"H HUMAN", 0, 1, JG_LIN, 0},
    {"G TONE", 0, 1, JG_LIN, 0},   {"G DECAY", 0, 1, JG_LIN, 0}, {"P TONE", 0, 1, JG_LIN, 0},  {"P DECAY", 0, 1, JG_LIN, 0},
};

static void graphs_init(bs_engine_t *e)
{
    /* Rhythm transformations: initial hypotheses, UNREVIEWED until listened to (docs/development/sweetspots.md) */
    static const struct { const char *n; float v[BS_NRPARAMS]; } RN[] = {
        {"CLEAN", {0.0f, 0.0f, 0.0f, 0.0f, 0.2f, 0.25f, 1.0f, 0.0f}},
        {"ROLLING", {0.12f, 0.15f, 0.0f, 0.04f, 0.5f, 0.55f, 1.0f, 0.0f}},
        {"CHOPPED", {0.55f, 0.25f, 0.12f, 0.1f, 0.45f, 0.45f, 0.75f, 0.1f}},
        {"GLITCHED", {0.8f, 0.55f, 0.3f, 0.4f, 0.3f, 0.35f, 0.55f, 0.35f}},
    };
    static const struct { const char *n; float v[BS_NKPARAMS]; float trim; } KN[] = {
        {"DUSTY ROLLER", {0.4f, 0.45f, 0.3f, 0.3f, 0.55f, 0.45f, 0.45f, 0.35f, 0.2f, 0.35f, 0.35f, 0.5f, 0.4f, 0.4f, 0.4f, 0.5f}, 0.0f},
        {"JUNGLE SNAP", {0.45f, 0.5f, 0.35f, 0.15f, 0.6f, 0.55f, 0.55f, 0.25f, 0.25f, 0.5f, 0.3f, 0.4f, 0.5f, 0.35f, 0.5f, 0.4f}, 0.0f},
        {"TIGHT NEURO", {0.25f, 0.8f, 0.5f, 0.35f, 0.4f, 0.85f, 0.2f, 0.6f, 0.6f, 0.75f, 0.15f, 0.1f, 0.7f, 0.2f, 0.6f, 0.25f}, -1.0f},
        {"METAL TECHSTEP", {0.3f, 0.7f, 0.45f, 0.5f, 0.35f, 0.9f, 0.3f, 0.7f, 0.9f, 0.8f, 0.2f, 0.1f, 0.8f, 0.25f, 0.8f, 0.2f}, -1.5f},
        {"INDUSTRIAL", {0.5f, 0.9f, 0.6f, 0.85f, 0.5f, 0.95f, 0.35f, 0.95f, 0.85f, 0.6f, 0.25f, 0.2f, 0.6f, 0.3f, 0.7f, 0.3f}, -2.5f},
    };
    uint32_t i, k;
    jg_graph_t *g = &e->rhythm;
    g->param = RPARAM;
    g->nparams = BS_NRPARAMS;
    g->nnodes = 4;
    for (i = 0; i < 4u; i++) {
        g->node[i].name = RN[i].n;
        for (k = 0; k < BS_NRPARAMS; k++) g->node[i].v[k] = jg_to_norm(&RPARAM[k], RN[i].v[k]);
        g->node[i].trim_db = 0.0f;
        g->node[i].review = JG_UNREVIEWED;
    }
    g->nedges = 3;
    for (i = 0; i < 3u; i++) g->edge[i].a = (uint8_t)i, g->edge[i].b = (uint8_t)(i + 1u), g->edge[i].flags = 0;
    g->npath = 4;
    for (i = 0; i < 4u; i++) g->path[i] = (uint8_t)i;
    g = &e->kit;
    g->param = KPARAM;
    g->nparams = BS_NKPARAMS;
    g->nnodes = 5;
    for (i = 0; i < 5u; i++) {
        g->node[i].name = KN[i].n;
        for (k = 0; k < BS_NKPARAMS; k++) g->node[i].v[k] = KN[i].v[k];
        g->node[i].trim_db = KN[i].trim;
        g->node[i].review = JG_UNREVIEWED;
    }
    g->nedges = 4;
    for (i = 0; i < 4u; i++) g->edge[i].a = (uint8_t)i, g->edge[i].b = (uint8_t)(i + 1u), g->edge[i].flags = 0;
    g->npath = 5;
    for (i = 0; i < 5u; i++) g->path[i] = (uint8_t)i;
}

void bs_set_rhythm(bs_engine_t *e, float t)
{
    e->rhythm_t = jo_clampf(t, 0.0f, 1.0f);
    jg_eval(&e->rhythm, e->rhythm_t, &e->rv);
}

void bs_set_kit(bs_engine_t *e, float t)
{
    const float *p;
    e->kit_t = jo_clampf(t, 0.0f, 1.0f);
    jg_eval(&e->kit, e->kit_t, &e->kv);
    p = e->kv.phys;
    e->snd[BS_T_KICK].m[0] = p[0], e->snd[BS_T_KICK].m[1] = p[1], e->snd[BS_T_KICK].m[2] = p[2], e->snd[BS_T_KICK].m[3] = p[3];
    e->snd[BS_T_SNARE].m[0] = p[4], e->snd[BS_T_SNARE].m[1] = p[5], e->snd[BS_T_SNARE].m[2] = p[6], e->snd[BS_T_SNARE].m[3] = p[7];
    e->snd[BS_T_HATS].m[0] = p[8], e->snd[BS_T_HATS].m[1] = p[9], e->snd[BS_T_HATS].m[2] = p[10], e->snd[BS_T_HATS].m[3] = p[11];
    e->snd[BS_T_GHOST].m[0] = p[12], e->snd[BS_T_GHOST].m[2] = p[13];
    e->snd[BS_T_PERC].m[0] = p[14], e->snd[BS_T_PERC].m[1] = p[15];
}

/* --------------------------------------------------------------- patterns --- */
/* one track from a 16-character string: '.' off, 'x' on (vel 100), 'X' accent (122), 'o' soft (60), 'g' ghost (38),
 * 'O' open hat (hats); slices default to the step index */
static void track_str(js_track_t *t, const char *s)
{
    uint32_t i;
    for (i = 0; i < 16u && s[i]; i++) {
        js_step_t *st = &t->s[i];
        js_step_clear(st);
        st->data = (uint8_t)i;
        switch (s[i]) {
        case 'x': st->flags = JS_ON; st->vel = 100; break;
        case 'X': st->flags = JS_ON | JS_ACCENT; st->vel = 122; break;
        case 'o': st->flags = JS_ON; st->vel = 60; break;
        case 'g': st->flags = JS_ON; st->vel = 38; break;
        case 'O': st->flags = JS_ON | BS_FLAG_OPEN; st->vel = 80; break;
        default: break;
        }
    }
    t->len = 16;
}

void bs_default_patterns(bs_engine_t *e)
{
    /* at 174 BPM; the test break's own hits (jo_break.c, the Amen pattern): kicks on 1, 1 and, 3 and, 3 a; snares on 2 and 4 */
    static const char *const P[BS_NPATTERNS][BS_NTRACKS] = {
        /* 0 DROP: the break whole under a two-step kick and backbeat, offbeat hats */
        {"xxxxxxxxxxxxxxxx", "X.........x.....", "....X.......X...", "", "..o...x...o...O.", ".......o.......o", ""},
        /* 1 ROLLER: the break edited (its first half again, a snare pushed), a rolling kick, 16th shakers */
        {"xxxxxxxxxxxxxxxx", "X.........x..x..", "....X.......X...", "", "goxogoxogoxogoxO", "", ""},
        /* 2 HALF-TIME: kick and snare at half time, the break on 8ths */
        {"x.x.x.x.x.x.x.x.", "X..........x....", "........X.......", "", "o.x.o.x.o.x.o.O.", "...o.......o....", ""},
        /* 3 INTRO: the break thinned out, no synthetic kick or snare */
        {"x.x.x..xx.x.x.x.", "................", "................", "", "..o...o...o...o.", "", ""},
    };
    /* ROLLER's break edit: slices 1-8, then 1-2 again, the rest, the last snare doubled into a roll */
    static const uint8_t ROLL_EDIT[16] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 10, 11, 12, 13, 12, 15};
    uint32_t p, t, i;
    for (p = 0; p < BS_NPATTERNS; p++) {
        js_pattern_clear(&e->pat[p], BS_NTRACKS, 16);
        e->pat[p].seed = (uint16_t)(101u + p);
        e->pat[p].swing = 50; /* drum & bass sits straight; the feel is in the ghosts and the microtiming */
        for (t = 0; t < BS_NTRACKS; t++)
            if (P[p][t][0]) track_str(&e->pat[p].t[t], P[p][t]);
    }
    for (i = 0; i < 16u; i++) e->pat[1].t[BS_T_BREAK].s[i].data = ROLL_EDIT[i];
    e->pat[1].t[BS_T_BREAK].s[14].ratch = 1;           /* the doubled snare as a roll */
    e->pat[1].t[BS_T_BREAK].s[14].vel = 84;
    e->pat[0].t[BS_T_PERC].s[7].data = 0;
    /* ghosts on the patterns that drive */
    {
        uint8_t keep = e->cur_pat;
        e->cur_pat = 0;
        bs_ghosts(e, 0.45f, 7);
        e->cur_pat = 1;
        bs_ghosts(e, 0.6f, 11);
        e->cur_pat = keep;
    }
}

void bs_ghosts(bs_engine_t *e, float density, uint32_t seed)
{
    js_pattern_t *p = &e->pat[e->cur_pat];
    js_track_t *g = &p->t[BS_T_GHOST], tmp;
    const js_track_t *sn = &p->t[BS_T_SNARE];
    uint32_t i, run = 0, len = sn->len ? sn->len : 16u;
    uint32_t f;
    tmp = *g;
    tmp.len = (uint8_t)len;
    for (i = 0; i < len; i++) {
        js_step_t *st = &tmp.s[i];
        uint32_t pos = i & 15u, h = js_hash(seed, i, 0x6B057u, 3u);
        int snare_here = (sn->s[i].flags & JS_ON) != 0;
        int before_snare = (sn->s[(i + 1u) % len].flags & JS_ON) != 0;
        js_step_clear(st);
        if (snare_here || (pos % 4u) == 0u || run >= 2u) { run = 0; continue; } /* never on a backbeat or a beat */
        /* the hierarchy: the 16th before a snare is the likeliest and loudest, odd 16ths next, 8ths least */
        {
            float w = before_snare ? 1.0f : (pos & 1u) ? 0.75f : 0.45f;
            if (unit(h) < density * w) {
                st->flags = JS_ON;
                st->vel = (uint8_t)(before_snare ? 62 : 30 + (int)(unit(h * 7u + 1u) * 22.0f));
                st->micro = (int8_t)(e->snd[BS_T_GHOST].m[3] * 24.0f); /* GHOST's SWING: a lazier ghost (~8 ms at 0.5) */
                run++;
            } else
                run = 0;
        }
    }
    f = lock_fn();
    *g = tmp;
    unlock_fn(f);
}

void bs_mutate(bs_engine_t *e, float amount, uint32_t seed)
{
    js_pattern_t *p = &e->pat[e->cur_pat];
    js_track_t tmp = p->t[BS_T_BREAK];
    uint32_t i, len = tmp.len ? tmp.len : 16u, n = e->breaks[e->cur_break].nslices ? e->breaks[e->cur_break].nslices : 16u;
    uint32_t f;
    amount = jo_clampf(amount, 0.0f, 1.0f);
    e->undo = *p;
    e->undo_valid = 1;
    e->undo_pattern = e->cur_pat;
    for (i = 0; i < len; i++) {
        js_step_t *st = &tmp.s[i];
        uint32_t h = js_hash(seed, i, 0x3D7u, 5u);
        int anchor = e->anchors && (i & 3u) == 0u;
        if (anchor || unit(h) >= amount) continue;
        /* a slice from elsewhere in the break, of the same class when the bank knows classes */
        {
            uint32_t pick = js_hash(seed, i, 1u, 9u) % n, tries;
            const jb_break_t *b = &e->breaks[e->cur_break];
            for (tries = 0; tries < 8u && b->cls[st->data % n] && b->cls[pick] != b->cls[st->data % n]; tries++)
                pick = js_hash(seed, i, tries + 2u, 9u) % n;
            st->data = (uint8_t)pick;
        }
        st->flags |= JS_ON;
        if (amount > 0.5f && unit(h * 3u + 1u) < (amount - 0.5f) * 0.5f) st->flags ^= JS_REVERSE;
        if (amount > 0.3f && unit(h * 5u + 2u) < (amount - 0.3f) * 0.3f) st->ratch = 1;
    }
    f = lock_fn();
    p->t[BS_T_BREAK] = tmp;
    unlock_fn(f);
}

int bs_undo(bs_engine_t *e)
{
    uint32_t f;
    if (!e->undo_valid) return -1;
    f = lock_fn();
    e->pat[e->undo_pattern].t[BS_T_BREAK] = e->undo.t[BS_T_BREAK];
    unlock_fn(f);
    e->undo_valid = 0;
    return 0;
}

/* --------------------------------------------------------------- scenes --- */
static void scenes_default(bs_engine_t *e)
{
    /* INTRO, BUILD, DROP, MUTATION, VOID, IMPACT: the conceptual states of docs/user-guide/live-performance.md */
    static const struct { uint8_t pat, mute; float r, k, en; uint8_t fx; } S[6] = {
        {3, 0x00, 0.0f, 0.0f, 0.2f, JF_DRUM_CLEAN_JUNGLE},
        {1, 0x00, 0.3f, 0.25f, 0.5f, JF_DRUM_ROLLING},
        {0, 0x00, 0.33f, 0.5f, 0.6f, JF_DRUM_TIGHT_NEURO},
        {0, 0x00, 0.72f, 0.65f, 0.75f, JF_DRUM_CRUSHED},
        {3, (1u << BS_T_KICK) | (1u << BS_T_SNARE) | (1u << BS_T_GHOST) | (1u << BS_T_PERC), 0.9f, 0.3f, 0.1f, JF_DRUM_CLEAN_JUNGLE},
        {2, 0x00, 0.5f, 1.0f, 0.9f, JF_DRUM_INDUSTRIAL},
    };
    uint32_t i;
    for (i = 0; i < BS_NSCENES; i++) {
        bs_scene_t *s = &e->scene[i];
        uint32_t k = i < 6u ? i : 2u;
        s->used = i < 6u;
        s->pattern = S[k].pat;
        s->mute = S[k].mute;
        s->rhythm_t = S[k].r;
        s->kit_t = S[k].k;
        s->energy = S[k].en;
        s->quant = BS_Q_BAR;
        jf_drum_preset(&s->fx, S[k].fx);
    }
}

void bs_scene_store(bs_engine_t *e, int s)
{
    bs_scene_t *sc;
    if (s < 0 || s >= BS_NSCENES) return;
    sc = &e->scene[s];
    sc->used = 1;
    sc->pattern = e->cur_pat;
    sc->mute = e->seq.mute;
    sc->rhythm_t = e->rhythm_t;
    sc->kit_t = e->kit_t;
    sc->energy = e->energy;
    sc->fx = e->bus.m;
}

/* the first step of the next boundary of quant after the step being evaluated */
static uint32_t next_boundary(const bs_engine_t *e, int quant)
{
    static const uint32_t Q[5] = {1u, 4u, 16u, 32u, 64u};
    uint32_t q = Q[quant < 0 || quant > 4 ? 2 : quant];
    uint32_t a = e->clock.running ? e->clock.abs_step + 2u : 0u; /* +2: past the step already queued */
    return (a + q - 1u) / q * q;
}


/* ---------------------------------------------------------- sequencing --- */
static void send(bs_engine_t *e, const uint32_t *p, uint32_t n)
{
    uint32_t i;
    if (e->midi_out)
        for (i = 0; i < n; i++) e->midi_out(p[i]);
}

static void apply_scene_pattern(bs_engine_t *e, const bs_scene_t *s)
{
    e->cur_pat = e->next_pat = s->pattern % BS_NPATTERNS;
    e->seq.pat = &e->pat[e->cur_pat];
    e->seq.mute = s->mute;
    js_seq_reset(&e->seq);
}
static void apply_scene_sound(bs_engine_t *e, const bs_scene_t *s)
{
    bs_set_rhythm(e, s->rhythm_t);
    bs_set_kit(e, s->kit_t);
    e->energy = s->energy;
    e->bus.m = s->fx;
    jf_drum_update(&e->bus);
}

static void on_step(void *ctx, uint32_t abs, uint32_t off)
{
    bs_engine_t *e = (bs_engine_t *)ctx;
    (void)off;
    if (e->fill_on && !js_before(abs, e->fill_end + 1u)) e->fill_on = 0;
    /* switches land at the start of the step before the boundary: the boundary step is evaluated after it */
    if (abs + 1u == e->pending_at) {
        if (e->scene_pending >= 0)
            apply_scene_pattern(e, &e->scene[e->scene_pending]);
        else if (e->next_pat != e->cur_pat) {
            e->cur_pat = e->next_pat;
            e->seq.pat = &e->pat[e->cur_pat];
            js_seq_reset(&e->seq);
        }
        if (e->return_pending) {
            e->perf = BS_PERF_CLEAN;
            e->fill_on = 0;
            bs_set_rhythm(e, 0.0f);
            js_seq_reset(&e->seq);
        }
    }
    if (abs == e->pending_at) {
        if (e->scene_pending >= 0) {
            apply_scene_sound(e, &e->scene[e->scene_pending]);
            e->scene_cur = e->scene_pending;
            e->scene_pending = -1;
        }
        e->return_pending = 0;
    }
}

static void on_clock_out(void *ctx, uint32_t off)
{
    bs_engine_t *e = (bs_engine_t *)ctx;
    uint32_t p = JL_PKT_CLOCK;
    (void)off;
    if (e->send_clock && e->clock.source == JS_CLK_INT) send(e, &p, 1);
}

static int xform(void *ctx, uint32_t t, uint32_t abs, js_step_t *st)
{
    bs_engine_t *e = (bs_engine_t *)ctx;
    const float *r = e->rv.phys;
    uint32_t pos = abs & 15u, seed = e->seq.pat->seed;
    int anchor = e->anchors && (pos & 3u) == 0u;
    int in_fill = e->fill_on && !js_before(abs, e->fill_start) && js_before(abs, e->fill_end);
    float prog = in_fill ? (float)(abs - e->fill_start) / (float)(e->fill_end - e->fill_start) : 0.0f;
    float chop = r[BS_R_CHOP], ratch = r[BS_R_RATCH], rev = r[BS_R_REV], stut = r[BS_R_STUTTER];
    const jb_break_t *b = &e->breaks[e->cur_break];
    uint32_t n = b->nslices ? b->nslices : 16u;
    switch (e->perf) {
    case BS_PERF_CHOP: chop = jo_maxf(chop, 0.85f); break;
    case BS_PERF_REVERSE: rev = 1.0f; break;
    case BS_PERF_GLITCH: chop = jo_maxf(chop, 0.6f), ratch = jo_maxf(ratch, 0.5f), stut = jo_maxf(stut, 0.45f); break;
    case BS_PERF_DESTROY: chop = 0.9f, ratch = 0.7f, stut = 0.6f, rev = jo_maxf(rev, 0.3f); break;
    default: break;
    }
    if (in_fill) { /* intensity rises through the fill, scaled by ENERGY */
        float k = 0.3f + 0.7f * e->energy;
        chop = jo_maxf(chop, k * (0.25f + 0.6f * prog));
        ratch = jo_maxf(ratch, k * prog * prog);
    }
    switch (t) {
    case BS_T_BREAK: {
        uint32_t h = js_hash(seed, abs, 0xB4EAu, 1u);
        e->cur_eval_step = abs;
        e->cur_slice_cls = JB_CLASS_NONE;
        if (e->perf == BS_PERF_FREEZE) return 0; /* the microloop holds; nothing new */
        if (e->perf == BS_PERF_ROLL) {
            st->flags = JS_ON;
            st->data = e->perf_slice;
            st->ratch = e->roll_div;
            st->vel = 100;
            e->cur_slice_cls = b->cls[st->data % n];
            return 1;
        }
        if (!(st->flags & JS_ON)) return 1;
        if (!anchor && unit(h) < chop) st->data = (uint8_t)(js_hash(seed, abs, 2u, 11u) % n);
        if (!anchor && unit(h * 3u + 7u) < ratch) st->ratch = (uint8_t)(1u + js_hash(seed, abs, 3u, 0u) % 3u);
        if (unit(h * 5u + 3u) < rev) st->flags ^= JS_REVERSE;
        if (!anchor && unit(h * 7u + 5u) < stut) js_step_set_lock(st, BS_LK_LOOP, (int)(2u + js_hash(seed, abs, 4u, 0u) % 3u));
        if (r[BS_R_GATE] < 0.99f && js_step_lock(st, BS_LK_GATE) < 0)
            js_step_set_lock(st, BS_LK_GATE, (int)(r[BS_R_GATE] * 255.0f));
        if (unit(h * 11u + 1u) < r[BS_R_PITCH] * 0.5f && js_step_lock(st, BS_LK_TUNE) < 0)
            js_step_set_lock(st, BS_LK_TUNE, 128 - 4 * (int)(5u + js_hash(seed, abs, 5u, 0u) % 8u));
        e->cur_slice_cls = b->cls[st->data % n];
        return 1;
    }
    case BS_T_KICK:
    case BS_T_SNARE: {
        uint8_t want = t == BS_T_KICK ? JB_CLASS_KICK : JB_CLASS_SNARE;
        int cls_here = e->cur_eval_step == abs && e->cur_slice_cls == want;
        if (e->breaklock == BS_LOCK_SEPARATE && cls_here && (st->flags & JS_ON)) return 0;
        if (e->breaklock == BS_LOCK_LAYER && cls_here && !(st->flags & JS_ON)) st->flags = JS_ON, st->vel = 92;
        if (in_fill && t == BS_T_SNARE && prog >= 0.5f) { /* the roll: the fill's second half, building */
            float q = (prog - 0.5f) * 2.0f;
            st->flags = JS_ON;
            st->vel = (uint8_t)(48.0f + 74.0f * q);
            st->ratch = (uint8_t)(q > 0.75f ? 3 : q > 0.4f ? 1 : 0);
        }
        if (in_fill && t == BS_T_KICK && prog > 0.75f && pos != 0u) return 0; /* a gap before the drop */
        return 1;
    }
    case BS_T_GHOST:
        if (st->flags & JS_ON)
            st->prob = (uint8_t)jo_clampi((int32_t)((float)st->prob * (0.3f + 1.4f * r[BS_R_GHOST])), 0, 100);
        return 1;
    case BS_T_HATS:
        if (!(st->flags & JS_ON) && (pos & 3u) != 0u && unit(js_hash(seed, abs, 0x4A75u, 2u)) < r[BS_R_HATS] * 0.5f) {
            st->flags = JS_ON;
            st->vel = 44;
        }
        return 1;
    case BS_T_IMPACT:
        if (e->fill_on && abs == e->fill_end) { st->flags = JS_ON; st->vel = 110; }
        return 1;
    default:
        return 1;
    }
}

/* ------------------------------------------------------------------ fire --- */
static float lockf(const js_event_t *ev, uint8_t id, float dflt)
{
    uint32_t i;
    for (i = 0; i < JS_NLOCK; i++)
        if (ev->lk_id[i] == id) return (float)ev->lk_val[i] * (1.0f / 255.0f);
    return dflt;
}
static int locki(const js_event_t *ev, uint8_t id)
{
    uint32_t i;
    for (i = 0; i < JS_NLOCK; i++)
        if (ev->lk_id[i] == id) return ev->lk_val[i];
    return -1;
}

static void fire_(bs_engine_t *e, const js_event_t *ev);
static void fire(bs_engine_t *e, const js_event_t *ev)
{
    if (e->trace) e->trace(e->trace_ctx, ev);
    fire_(e, ev);
}
static void fire_(bs_engine_t *e, const js_event_t *ev)
{
    float vel = (float)ev->vel * (1.0f / 127.0f);
    uint32_t pk[2], np;
    e->events_fired++;
    if (ev->nhits > 1u) vel *= 0.6f + 0.4f * (float)(ev->hit + 1u) / (float)ev->nhits; /* rolls crescendo */
    if (ev->track == BS_T_BREAK) {
        int tune = locki(ev, BS_LK_TUNE), loop = locki(ev, BS_LK_LOOP);
        const jb_break_t *b = &e->breaks[e->cur_break];
        e->player.gate = lockf(ev, BS_LK_GATE, 1.0f);
        jb_play(&e->player, ev->data, vel, (ev->flags & JS_REVERSE) != 0, tune >= 0 ? (float)(tune - 128) * 0.25f : 0.0f);
        if (loop > 0) /* a stutter: 1/2^n of a step of source */
            jb_microloop(&e->player, js_clock_samples_per_step(&e->clock) * e->player.v[e->player.cur].rate /
                                         (float)(1u << (loop > 6 ? 6 : loop)));
        if (e->send_kicks && b->nslices && b->cls[ev->data % b->nslices] == JB_CLASS_KICK && ev->hit == 0u &&
            (np = jl_trigger(&e->link, JL_NOTE_KICK, ev->vel, e->ms, pk)))
            send(e, pk, np);
        return;
    }
    if (ev->track < BS_NTRACKS) {
        jd_sound_t s = e->snd[ev->track];
        int tune = locki(ev, BS_LK_TUNE);
        uint32_t k;
        for (k = 0; k < JD_NMACRO; k++) s.m[k] = lockf(ev, (uint8_t)(BS_LK_M0 + k), s.m[k]);
        if (tune >= 0) s.tune += (float)(tune - 128) * 0.25f;
        s.level = lockf(ev, BS_LK_LEVEL, s.level);
        vel *= jo_db2lin(e->kv.trim_db);
        jd_trigger(&e->dv[ev->track], &s, vel, ev->track == BS_T_HATS && (ev->flags & BS_FLAG_OPEN));
        if (e->send_kicks && ev->hit == 0u && (ev->track == BS_T_KICK || ev->track == BS_T_SNARE) &&
            (np = jl_trigger(&e->link, ev->track == BS_T_KICK ? JL_NOTE_KICK : JL_NOTE_SNARE, ev->vel, e->ms, pk)))
            send(e, pk, np);
    }
}

/* ------------------------------------------------------------------ init --- */
int bs_load_bank(bs_engine_t *e, const uint8_t *bank, uint32_t len)
{
    int n = jb_bank_parse(bank, len, e->breaks, JB_MAXBREAKS);
    if (n <= 0) return n;
    e->nbreaks = (uint8_t)n;
    e->cur_break = 0;
    e->has_bank = 1;
    jb_player_init(&e->player, &e->breaks[0]);
    e->player.level = e->break_level;
    return n;
}

void bs_set_slices(bs_engine_t *e, int n)
{
    jb_break_t *b = &e->breaks[e->cur_break];
    e->slices = (uint8_t)(n <= 8 ? 8 : n <= 16 ? 16 : 32);
    if (!e->has_bank) { /* the test break's classes are known per 16th; re-derive them for 8 / 32 */
        uint8_t cls16[16];
        uint32_t i;
        for (i = 0; i < 16u; i++) cls16[i] = b->nslices == 16u ? b->cls[i] : JB_CLASS_NONE;
        jb_slice_even(b, e->slices);
        for (i = 0; i < e->slices; i++) b->cls[i] = cls16[(i * 16u) / e->slices];
    } else
        jb_slice_even(b, e->slices);
}

void bs_init(bs_engine_t *e, int16_t *test_pcm, const uint8_t *bank, uint32_t bank_len)
{
    uint8_t *q = (uint8_t *)e;
    uint32_t i;
    for (i = 0; i < sizeof *e; i++) q[i] = 0;
    e->test_pcm = test_pcm;
    e->break_level = 0.8f;
    e->anchors = 1;
    e->tempo_sync = 1;
    e->roll_div = 1;
    e->energy = 0.5f;
    e->slices = 16;
    e->scene_pending = -1;
    e->scene_cur = 2;
    e->send_kicks = 1;
    e->send_clock = 1;
    for (i = 1; i < BS_NTRACKS; i++) {
        static const uint8_t M[BS_NTRACKS] = {0, JD_KICK, JD_SNARE, JD_GHOST, JD_HATS, JD_PERC, JD_IMPACT};
        jd_voice_init(&e->dv[i], M[i], i);
        jd_sound_default(&e->snd[i], M[i]);
    }
    e->snd[BS_T_PERC].variant = JD_PERC_RIM;
    if (!(bank && bs_load_bank(e, bank, bank_len) > 0) && test_pcm) {
        jb_testbreak_make(&e->breaks[0], test_pcm, 22059u);
        e->nbreaks = 1;
        jb_player_init(&e->player, &e->breaks[0]);
    }
    e->player.level = e->break_level;
    graphs_init(e);
    bs_set_rhythm(e, 0.0f);
    bs_set_kit(e, 0.25f);
    js_clock_init(&e->clock, 174.0f);
    bs_default_patterns(e);
    js_seq_init(&e->seq, &e->pat[0]);
    js_queue_clear(&e->q);
    scenes_default(e);
    jf_drum_init(&e->bus);
    jl_init(&e->link, 15);
    e->pending_at = 0xFFFFFFFFu;
}

/* SNAP: the BREAK and KIT knobs to their nearest nodes; the kit one node along its path (PERFORM's PRESET and ALGO,
 * as Abyss Engine's SNAP and EXPLORE) */
void bs_snap(bs_engine_t *e)
{
    bs_set_rhythm(e, jg_path_t(&e->rhythm, jg_snap(&e->rhythm, e->rhythm_t)));
    bs_set_kit(e, jg_path_t(&e->kit, jg_snap(&e->kit, e->kit_t)));
}

const char *bs_kit_step(bs_engine_t *e, int dir)
{
    int k = (int)jg_snap(&e->kit, e->kit_t) + (dir > 0 ? 1 : -1);
    k = k < 0 ? 0 : k >= (int)e->kit.npath ? (int)e->kit.npath - 1 : k;
    bs_set_kit(e, jg_path_t(&e->kit, (uint32_t)k));
    return e->kit.node[e->kit.path[k]].name;
}

void bs_fx_update(bs_engine_t *e) { jf_drum_update(&e->bus); }

/* ------------------------------------------------------------- commands --- */
static void cmd(bs_engine_t *e, uint8_t op, uint8_t a, uint8_t b, float f) { jo_cmdq_push(&e->cmd, op, a, b, 0, f); }
void bs_cmd_play(bs_engine_t *e, int on) { cmd(e, C_PLAY, (uint8_t)(on != 0), 0, 0.0f); }
void bs_cmd_bpm(bs_engine_t *e, float bpm) { cmd(e, C_BPM, 0, 0, bpm); }
void bs_cmd_perf(bs_engine_t *e, int m) { cmd(e, C_PERF, (uint8_t)m, 0, 0.0f); }
void bs_cmd_fill(bs_engine_t *e, int bars) { cmd(e, C_FILL, (uint8_t)bars, 0, 0.0f); }
void bs_cmd_return(bs_engine_t *e) { cmd(e, C_RETURN, 0, 0, 0.0f); }
void bs_cmd_pattern(bs_engine_t *e, int p) { cmd(e, C_PATTERN, (uint8_t)p, 0, 0.0f); }
void bs_cmd_scene(bs_engine_t *e, int s, int quant) { cmd(e, C_SCENE, (uint8_t)s, (uint8_t)quant, 0.0f); }
void bs_cmd_live_slice(bs_engine_t *e, int s, float vel) { cmd(e, C_SLICE, (uint8_t)s, 0, vel); }
void bs_cmd_live_drum(bs_engine_t *e, int t, float vel) { cmd(e, C_DRUM, (uint8_t)t, 0, vel); }
void bs_cmd_mute(bs_engine_t *e, int t, int solo) { cmd(e, C_MUTE, (uint8_t)t, (uint8_t)solo, 0.0f); }
void bs_cmd_panic(bs_engine_t *e) { cmd(e, C_PANIC, 0, 0, 0.0f); }

void bs_midi_clock_in(bs_engine_t *e, uint32_t t24)
{
    if (e->clock.source == JS_CLK_MIDI) js_clock_midi_tick(&e->clock, t24);
}

static void do_cmd(bs_engine_t *e, const jo_cmd_t *c)
{
    uint32_t pk[3], i;
    switch (c->op) {
    case C_PLAY:
        if (c->a && !e->clock.running) {
            js_seq_reset(&e->seq);
            js_queue_clear(&e->q);
            js_clock_start(&e->clock);
            pk[0] = JL_PKT_START;
            if (e->send_clock) send(e, pk, 1);
        } else if (!c->a && e->clock.running) {
            js_clock_stop(&e->clock);
            js_queue_clear(&e->q);
            jb_release(&e->player);
            e->fill_on = 0;
            pk[0] = JL_PKT_STOP;
            if (e->send_clock) send(e, pk, 1);
        }
        break;
    case C_BPM: js_clock_set_bpm(&e->clock, c->f); break;
    case C_PERF:
        if (c->a == BS_PERF_ROLL || c->a == BS_PERF_FREEZE) {
            e->perf_slice = 0;
            /* ROLL repeats the slice playing now; FREEZE holds a tiny loop of it */
            {
                const jb_voice_t *v = &e->player.v[e->player.cur];
                const jb_break_t *b = &e->breaks[e->cur_break];
                for (i = 0; b->nslices && i < b->nslices; i++)
                    if ((float)b->start[i] <= v->lo) e->perf_slice = (uint8_t)i;
            }
            if (c->a == BS_PERF_FREEZE) jb_microloop(&e->player, 0.004f * (float)e->breaks[e->cur_break].rate);
        }
        if (e->perf == BS_PERF_FREEZE && c->a != BS_PERF_FREEZE) jb_microloop(&e->player, 0.0f);
        e->perf = c->a < BS_NPERF ? c->a : 0u;
        break;
    case C_FILL:
        e->fill_start = next_boundary(e, BS_Q_BAR);
        e->fill_bars = (uint8_t)(c->a == 4 ? 4 : c->a == 2 ? 2 : 1);
        e->fill_end = e->fill_start + 16u * e->fill_bars;
        e->fill_on = 1;
        {
            uint32_t n = 1;
            pk[0] = jl_cc(&e->link, JL_CC_FILL_BEGIN, e->fill_bars);
            send(e, pk, n);
        }
        break;
    case C_RETURN:
        e->return_pending = 1;
        e->pending_at = next_boundary(e, BS_Q_BAR);
        pk[0] = jl_cc(&e->link, JL_CC_RESET, 0);
        send(e, pk, 1);
        if (!e->clock.running) { e->perf = 0; e->fill_on = 0; bs_set_rhythm(e, 0.0f); }
        break;
    case C_PATTERN:
        e->next_pat = c->a % BS_NPATTERNS;
        e->pending_at = next_boundary(e, BS_Q_BAR);
        if (!e->clock.running) { e->cur_pat = e->next_pat; e->seq.pat = &e->pat[e->cur_pat]; }
        break;
    case C_SCENE:
        if (c->a < BS_NSCENES && e->scene[c->a].used) {
            if (!e->clock.running || c->b == BS_Q_NOW) {
                apply_scene_pattern(e, &e->scene[c->a]);
                apply_scene_sound(e, &e->scene[c->a]);
                e->scene_cur = (int8_t)c->a;
            } else {
                e->scene_pending = (int8_t)c->a;
                e->pending_at = next_boundary(e, c->b);
            }
            send(e, pk, jl_scene(&e->link, c->a, c->b, pk));
        }
        break;
    case C_SLICE: jb_play(&e->player, c->a, c->f, 0, 0.0f); break;
    case C_DRUM:
        if (c->a > 0 && c->a < BS_NTRACKS) jd_trigger(&e->dv[c->a], &e->snd[c->a], c->f, 0);
        break;
    case C_MUTE:
        if (c->b) e->seq.solo ^= (uint8_t)(1u << c->a);
        else e->seq.mute ^= (uint8_t)(1u << c->a);
        break;
    case C_PANIC:
        js_queue_clear(&e->q);
        jb_release(&e->player);
        for (i = 1; i < BS_NTRACKS; i++) jd_choke(&e->dv[i]);
        e->perf = 0;
        e->fill_on = 0;
        break;
    default: break;
    }
}

/* ---------------------------------------------------------------- render --- */
/* each track's share of the room (DRUM PRESSURE's ROOM sets the room's level): the snare and its ghosts most, the
 * kick almost none (a dry kick keeps its punch) */
static const float ROOM_SEND[BS_NTRACKS] = {0.2f, 0.04f, 0.55f, 0.45f, 0.12f, 0.3f, 0.5f};

static void render_voices(bs_engine_t *e, float *buf, float *send, uint32_t n)
{
    float *tmp = e->tmp_buf;
    uint32_t t, i;
    for (t = 0; t < BS_NTRACKS; t++) {
        if (t == 0 ? !(e->player.v[0].active || e->player.v[1].active) : !e->dv[t].active) continue;
        for (i = 0; i < n; i++) tmp[i] = 0.0f;
        if (t == 0) jb_render(&e->player, tmp, n);
        else jd_render(&e->dv[t], tmp, n);
        for (i = 0; i < n; i++) {
            buf[i] += tmp[i];
            send[i] += tmp[i] * ROOM_SEND[t];
        }
    }
}

void bs_set_room_buf(bs_engine_t *e, int16_t *buf) { jf_drum_room_buf(&e->bus, buf); }

void bs_render_float(bs_engine_t *e, float *buf, uint32_t n)
{
    js_hooks_t h;
    jo_cmd_t c;
    js_event_t ev;
    uint32_t t0, pos = 0, i;
    const jb_break_t *b = &e->breaks[e->cur_break];
    if (n > BS_BLOCK_MAX) n = BS_BLOCK_MAX;
    while (jo_cmdq_pop(&e->cmd, &c)) do_cmd(e, &c);
    /* the break follows the tempo by repitching (the classic jungle way) */
    e->player.tempo_ratio = e->tempo_sync && b->bpm > 0.0f ? e->clock.bpm / b->bpm : 1.0f;
    h.on_step = on_step;
    h.on_clock_out = on_clock_out;
    h.transform = xform;
    h.ctx = e;
    t0 = e->clock.now;
    js_run(&e->seq, &e->clock, &e->q, n, &h);
    for (i = 0; i < n; i++) buf[i] = e->send_buf[i] = 0.0f;
    for (;;) {
        uint32_t nd, end;
        while (js_queue_pop_due(&e->q, t0 + pos + 1u, &ev)) fire(e, &ev);
        if (pos >= n) break;
        nd = js_queue_next_due(&e->q, t0 + n);
        end = js_before(nd, t0 + n) ? nd - t0 : n;
        if (end <= pos) end = pos + 1u;
        render_voices(e, buf + pos, e->send_buf + pos, end - pos);
        pos = end;
    }
    jf_drum_process(&e->bus, buf, e->send_buf, n);
    /* link heartbeat, and the millisecond count from the samples */
    {
        uint32_t tot = n * 1000u + e->ms_rem, pk[1];
        e->ms += tot / 44118u;
        e->ms_rem = tot % 44118u;
        if (e->midi_out && jl_hello(&e->link, e->ms, pk)) send(e, pk, 1);
    }
    e->blocks++;
}

void bs_render(bs_engine_t *e, int32_t *lr, uint32_t n, float out_gain)
{
    float *buf = e->bus_buf;
    uint32_t i, done = 0;
    float pk = e->peak;
    while (done < n) {
        uint32_t m = n - done > BS_BLOCK_MAX ? BS_BLOCK_MAX : n - done;
        bs_render_float(e, buf, m);
        for (i = 0; i < m; i++) {
            float s = jo_clampf(buf[i] * out_gain, -1.0f, 1.0f);
            int32_t v = (int32_t)(s * 8388607.0f);
            if (jo_absf(s) > pk) pk = jo_absf(s);
            lr[2u * (done + i)] = v;
            lr[2u * (done + i) + 1u] = v;
        }
        done += m;
    }
    e->peak = pk;
}
