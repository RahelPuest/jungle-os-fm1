/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
#include "ab_engine.h"

const char *const ab_track_name[AB_NTRACKS] = {"BASS", "STAB", "PAD"};
enum { C_PLAY, C_BPM, C_NOTE, C_STAB, C_PAD, C_PATTERN, C_SCENE, C_PANIC, C_MUTE, C_FILL, C_RETURN, C_VAR, C_FILLT, C_GEST };

/* ----------------------------------------------------------- the graph --- */
static const jg_param_t FMP[JN_NPARAMS] = {
    {"MODEL", 0.0f, 1.0f, JG_DISCRETE, 0.0f},   {"RATIO", 0.5f, 8.0f, JG_LOG, 0.08f},
    {"INDEX", 0.0f, 12.0f, JG_LIN, 0.15f},      {"FDBK", 0.0f, 0.85f, JG_LIN, 0.15f},
    {"VOWEL", 0.0f, 1.0f, JG_LIN, 0.3f},        {"FORMNT", 0.0f, 1.0f, JG_LIN, 0.25f},
    {"CUTOFF", 80.0f, 12000.0f, JG_LOG, 0.15f}, {"RES", 0.0f, 0.9f, JG_LIN, 0.15f},
    {"DRIVE", 1.0f, 10.0f, JG_LOG, 0.15f},      {"MODENV", 0.0f, 1.0f, JG_LIN, 0.25f},
    {"MODDEC", 0.02f, 1.5f, JG_LOG, 0.2f},      {"LFODIV", 0.0f, 7.0f, JG_DISCRETE, 0.0f},
    {"LFODEP", 0.0f, 1.0f, JG_LIN, 0.25f},      {"FOLD", 0.0f, 1.0f, JG_LIN, 0.2f},
    {"GLIDE", 0.001f, 0.15f, JG_LOG, 0.2f},
};
/* the curated nodes: initial hypotheses in physical units, UNREVIEWED until they pass the listening checklist
 * and the measured checks (docs/development/sweetspots.md). Order of the values: FMP above. The path nodes'
 * trims were set from the RMS of a held D2 toward -13 dBFS (tests/integration/test_engines.c prints it): a
 * proxy for loudness, to be corrected by ear */
typedef struct { const char *n; float v[JN_NPARAMS]; float trim; } node_def_t;
static const node_def_t FMN[] = {
    /*            MODEL RATIO INDEX FDBK VOWEL FORM  CUT     RES   DRIVE MENV  MDEC  DIV  DEP   FOLD  GLIDE */
    {"HOLLOW",   {0, 1.0f, 1.5f, 0.1f, 0.2f, 0.5f, 900.0f, 0.3f, 1.5f, 0.2f, 0.15f, 4, 0.3f, 0.0f, 0.03f}, 2.6f},
    {"RUBBER",   {0, 2.0f, 2.5f, 0.05f, 0.1f, 0.7f, 600.0f, 0.75f, 2.0f, 0.7f, 0.3f, 4, 0.5f, 0.0f, 0.06f}, -5.6f},
    {"TALK",     {0, 1.0f, 3.5f, 0.2f, 0.5f, 0.9f, 2500.0f, 0.45f, 2.5f, 0.3f, 0.2f, 3, 0.6f, 0.1f, 0.04f}, 0.6f},
    {"GROWL",    {0, 0.5f, 5.0f, 0.45f, 0.3f, 0.6f, 1800.0f, 0.5f, 4.0f, 0.4f, 0.25f, 4, 0.55f, 0.2f, 0.05f}, 0.0f},
    {"SNARL",    {0, 1.5f, 7.0f, 0.6f, 0.75f, 0.5f, 4000.0f, 0.55f, 6.0f, 0.5f, 0.12f, 6, 0.45f, 0.35f, 0.02f}, -4.1f},
    {"METALLIC", {0, 2.7f, 6.0f, 0.3f, 0.9f, 0.3f, 6000.0f, 0.6f, 3.0f, 0.6f, 0.08f, 5, 0.3f, 0.1f, 0.01f}, -2.9f},
    {"LASER",    {0, 3.5f, 9.0f, 0.2f, 1.0f, 0.2f, 8000.0f, 0.7f, 2.5f, 0.9f, 0.05f, 7, 0.4f, 0.0f, 0.1f}, -3.6f},
    {"BROKEN",   {0, 5.0f, 11.0f, 0.8f, 0.6f, 0.4f, 5000.0f, 0.4f, 8.0f, 0.3f, 0.1f, 6, 0.8f, 0.6f, 0.02f}, -4.6f},
    {"REESE",    {1, 3.0f, 6.0f, 0.0f, 0.4f, 0.2f, 1200.0f, 0.3f, 2.0f, 0.1f, 0.4f, 1, 0.5f, 0.0f, 0.05f}, -1.0f},
    {"REESE DARK", {1, 6.0f, 9.0f, 0.0f, 0.2f, 0.5f, 700.0f, 0.5f, 4.0f, 0.2f, 0.4f, 0, 0.7f, 0.1f, 0.05f}, -2.0f},
};
#define NFMN (sizeof FMN / sizeof FMN[0])

static void graph_init(ab_engine_t *e)
{
    jg_graph_t *g = &e->fm;
    uint32_t i, k;
    static const uint8_t PATH[] = {0, 1, 2, 3, 4, 5, 6, 7};
    static const jg_edge_t EXTRA[] = {{0, 8, JG_EDGE_XFADE}, {8, 9, 0}, {9, 3, JG_EDGE_XFADE}, {2, 4, 0}, {3, 7, 0}};
    g->param = FMP;
    g->nparams = JN_NPARAMS;
    g->nnodes = NFMN;
    for (i = 0; i < NFMN; i++) {
        g->node[i].name = FMN[i].n;
        for (k = 0; k < JN_NPARAMS; k++) g->node[i].v[k] = jg_to_norm(&FMP[k], FMN[i].v[k]);
        g->node[i].trim_db = FMN[i].trim;
        g->node[i].review = JG_UNREVIEWED;
    }
    g->nedges = 0;
    for (i = 0; i + 1u < sizeof PATH; i++) {
        g->edge[g->nedges].a = PATH[i];
        g->edge[g->nedges].b = PATH[i + 1u];
        g->edge[g->nedges++].flags = 0;
    }
    for (i = 0; i < sizeof EXTRA / sizeof EXTRA[0]; i++) g->edge[g->nedges++] = EXTRA[i];
    g->npath = sizeof PATH;
    for (i = 0; i < sizeof PATH; i++) g->path[i] = PATH[i];
}

const char *ab_node_name(const ab_engine_t *e) { return e->fm.node[e->fv.near].name; }

void ab_set_morph(ab_engine_t *e, float t)
{
    e->morph = jo_clampf(t, 0.0f, 1.0f);
    e->explore_node = -1;
}

int ab_snap(ab_engine_t *e)
{
    uint32_t k = jg_snap(&e->fm, e->morph);
    e->morph = jg_path_t(&e->fm, k);
    e->explore_node = -1;
    e->mutated = 0;
    return e->fm.path[k];
}

int ab_explore(ab_engine_t *e, int dir)
{
    uint8_t nb[8];
    uint32_t from = e->explore_node >= 0 ? (uint32_t)e->explore_node : e->fv.near;
    uint32_t n = jg_neighbours(&e->fm, from, nb, 8), i, pick = 0;
    if (!n) return (int)from;
    /* the neighbours in index order: dir +1 takes the next above, -1 the next below */
    for (i = 0; i < n; i++)
        if (dir > 0 ? nb[i] > from && (nb[pick] <= from || nb[i] < nb[pick]) : nb[i] < from && (nb[pick] >= from || nb[i] > nb[pick]))
            pick = i;
    e->explore_node = (int8_t)nb[pick];
    e->mutated = 0;
    return nb[pick];
}

void ab_mutate(ab_engine_t *e, float radius, uint32_t seed)
{
    jg_history_push(&e->hist, e->mutated ? e->mutate_v : e->fv.norm, JN_NPARAMS);
    jg_mutate(&e->fm, e->mutated ? e->mutate_v : e->fv.norm, e->mutate_v, radius, seed);
    e->mutated = 1;
}

int ab_revert(ab_engine_t *e)
{
    if (jg_history_pop(&e->hist, e->mutate_v, JN_NPARAMS)) {
        e->mutated = 0;
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------ call & response --- */
const uint8_t AB_CR_FORM[AB_CR_BARS] = {0, 1, 0, 2};
const char *const ab_form_name[AB_NFORMS] = {"OFF", "A B A C", "A A A B", "A B A B", "A B C D", "A A B B"};
const uint8_t AB_FORMS[AB_NFORMS][AB_CR_BARS] = {{0, 0, 0, 0}, {0, 1, 0, 2}, {0, 0, 0, 1}, {0, 1, 0, 1}, {0, 1, 2, 3}, {0, 0, 1, 1}};
const char *const ab_fx_name[AB_NFX] = {"NONE", "OCT UP", "OCT DOWN", "FILTER DIVE", "WOBBLE", "STUTTER", "TAPE STOP",
                                        "BITE MAX", "SUB ONLY"};
const char *const ab_fill_name[AB_NFILLS] = {"ROLLS", "OCTAVES", "DROP OUT", "SLIDE DOWN"};
const char *const ab_answer_name[AB_NANS] = {"ECHO", "INVERT", "RETRO", "DISPLACE", "OCT DOWN", "SPARSE"};
const char *const ab_curve_name[AB_NCURVES] = {"OFF", "REC", "PLAY"};

/* is a fill playing at 16th abs */
static int fill_at(const ab_engine_t *e, uint32_t abs)
{
    return e->fill_on && !js_before(abs, e->fill_start) && js_before(abs, e->fill_end);
}

/* the variation 16th abs plays: a link fill's answer (FILL ANSWERS: C), the FORM's bar, or the one picked */
int ab_cr_part(const ab_engine_t *e, uint32_t abs)
{
    if (e->duet_answer && e->ans_valid && (abs >> 4) == e->ans_bar) return 2; /* (0.6) ANSWER ME: C's sound */
    if (e->answer_on && !js_before(abs, e->answer_start) && js_before(abs, e->answer_end)) return 2;
    if (e->form != AB_FORM_OFF) return AB_FORMS[e->form % AB_NFORMS][(abs >> 4) % AB_CR_BARS];
    return e->var_sel % AB_NVAR;
}

void ab_set_form(ab_engine_t *e, int form)
{
    e->form = (uint8_t)(form > 0 && form < AB_NFORMS ? form : AB_FORM_OFF);
    e->cr_on = e->form != AB_FORM_OFF;
}

void ab_set_cr(ab_engine_t *e, int on)
{
    ab_set_form(e, on ? AB_FORM_ABAC : AB_FORM_OFF);
    if (!on) e->cr_play = 0, e->var_sel = 0;
}

void ab_set_phrase(ab_engine_t *e, int bars)
{
    e->phrase = (uint8_t)(bars >= 16 ? 16 : bars >= 8 ? 8 : 4);
    e->seq.phrase = e->phrase;
}

/* (0.6) the player's FX; the render plays it, or GESTURE ECHO's when none is held */
void ab_set_perf(ab_engine_t *e, int fx)
{
    e->perf_user = (uint8_t)(fx > 0 && fx < AB_NFX ? fx : AB_FX_NONE);
    e->perf = e->perf_user ? e->perf_user : e->perf_echo;
}

void ab_sound_get(const ab_engine_t *e, ab_sound_state_t *s)
{
    uint32_t i;
    for (i = 0; i < JN_NPARAMS; i++) s->v[i] = e->mutate_v[i];
    s->explore = e->explore_node;
    s->mutated = e->mutated;
}
void ab_sound_set(ab_engine_t *e, const ab_sound_state_t *s)
{
    uint32_t i;
    for (i = 0; i < JN_NPARAMS; i++) e->mutate_v[i] = s->v[i];
    e->explore_node = s->explore;
    e->mutated = s->mutated;
}

/* the bass track 16th abs plays: the pattern's (A, or a response on A's line), or a response's line */
static const js_track_t *bass_src(const ab_engine_t *e, uint32_t abs)
{
    int part = ab_cr_part(e, abs);
    int line = part ? e->cr[part - 1].line : -1;
    return line >= 0 && line < AB_NPATTERNS ? &e->pat[line].t[AB_T_BASS] : &e->seq.pat->t[AB_T_BASS];
}

/* the bass patch for this block: the graph (path, explored node or mutation; a response's own node), then BITE and
 * the locks */
static void patch_update(ab_engine_t *e)
{
    uint32_t i;
    float bite = e->perf == AB_FX_BITE ? 1.0f : e->bite_lock >= 0.0f ? e->bite_lock : jo_clampf(e->bite + e->echo_off[2], 0.0f, 1.0f);
    float target = e->morph_lock >= 0 ? (float)e->morph_lock * (1.0f / 255.0f) : jo_clampf(e->morph + e->echo_off[0], 0.0f, 1.0f);
    if (e->en.value < 1.0f) bite *= 0.4f + 0.6f * e->en.value; /* (0.6) less ENERGY: less BITE */
    e->morph_s += (target - e->morph_s) * (e->morph_lock >= 0 ? 0.5f : 0.15f); /* ~20 ms knob smoothing */
    int resp = e->cr_play ? e->cr[(e->cr_play - 1u) % (AB_NVAR - 1u)].node : -1;
    if (resp >= 0 && (uint32_t)resp < e->fm.nnodes) jg_eval_node(&e->fm, (uint32_t)resp, &e->fv);
    else if (e->explore_node >= 0) jg_eval_node(&e->fm, (uint32_t)e->explore_node, &e->fv);
    else jg_eval(&e->fm, e->morph_s, &e->fv);
    if (e->mutated && resp < 0) {
        for (i = 0; i < JN_NPARAMS; i++) e->fv.norm[i] = e->mutate_v[i];
        for (i = 0; i < JN_NPARAMS; i++) e->fv.phys[i] = jg_to_phys(&FMP[i], e->mutate_v[i]);
    }
    for (i = 0; i < JN_NPARAMS; i++) e->patch.p[i] = e->fv.phys[i];
    /* BITE: drive, brightness, fold and formant together, its loudness taken back by the trim below */
    e->patch.p[JN_P_DRIVE] *= 1.0f + 2.0f * bite;
    e->patch.p[JN_P_CUTOFF] = jo_minf(e->patch.p[JN_P_CUTOFF] * jo_exp2f(bite * 1.5f), 14000.0f);
    e->patch.p[JN_P_FOLD] = jo_clampf(e->patch.p[JN_P_FOLD] + 0.3f * bite, 0.0f, 1.0f);
    e->patch.p[JN_P_FORMANT] = jo_clampf(e->patch.p[JN_P_FORMANT] + 0.2f * bite, 0.0f, 1.0f);
    {
        float mo = jo_clampf(e->motion + e->echo_off[1], 0.0f, 1.0f), gl = jo_clampf(e->glide_amt + e->echo_off[3], 0.0f, 1.0f);
        e->patch.motion = mo;
        e->patch.coupling = 0.3f + 0.7f * mo;
        /* (0.5) GLIDE, and the performance FX that shape the patch */
        if (gl != 0.5f)
            e->patch.p[JN_P_GLIDE] = jo_clampf(e->patch.p[JN_P_GLIDE] * jo_exp2f(4.0f * (gl - 0.5f)), 0.001f, 0.3f);
    }
    if (e->perf == AB_FX_DIVE) { /* the low-pass falls five octaves over a bar */
        float bar = js_clock_samples_per_step(&e->clock) * 16.0f, k = jo_minf((float)e->perf_n / bar, 1.0f);
        e->patch.p[JN_P_CUTOFF] = jo_maxf(e->patch.p[JN_P_CUTOFF] * jo_exp2f(-5.0f * k), 80.0f);
    }
    if (e->perf == AB_FX_WOBBLE) {
        e->patch.motion = 1.0f, e->patch.coupling = 1.0f;
        e->patch.p[JN_P_LFODIV] = 4.0f; /* 1/8 */
        e->patch.p[JN_P_LFODEPTH] = 1.0f;
    }
}

/* ------------------------------------------------------------ patterns --- */
/* a bass line: 16 steps as "note:flags" with '.' rests; notes relative to the root, 'S' slide, 'A' accent */
static void bass_line(js_track_t *t, const int8_t *rel, const char *flags, const int16_t *morph_lock)
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        js_step_t *st = &t->s[i];
        js_step_clear(st);
        if (rel[i] == -99) continue;
        st->flags = JS_ON;
        st->data = (uint8_t)(64 + rel[i]); /* 64 = the root */
        st->vel = 100;
        if (flags[i] == 'S') st->flags |= JS_SLIDE;
        if (flags[i] == 'A') st->flags |= JS_ACCENT, st->vel = 124;
        if (morph_lock && morph_lock[i] >= 0) js_step_set_lock(st, AB_LK_MORPH, morph_lock[i]);
    }
    t->len = 16;
}

void ab_default_patterns(ab_engine_t *e)
{
#define R (-99)
    static const int8_t B0[16] = {0, R, R, 0, R, R, 3, R, 0, R, -2, 0, R, R, -5, R};
    static const char F0[17] = "A..S..A.A.SS..S.";
    static const int16_t M0[16] = {-1, -1, -1, 150, -1, -1, 200, -1, -1, -1, 90, 120, -1, -1, 230, -1};
    static const int8_t B1[16] = {0, R, 0, R, 0, 12, R, 0, R, 0, R, 7, R, 5, 3, R};
    static const char F1[17] = "A.S.ASSA.S.SAS..";
    static const int8_t B2[16] = {0, R, R, R, R, R, R, R, -2, R, R, R, R, R, -5, R};
    static const char F2[17] = "A.......S.....S.";
    uint32_t p, i;
#undef R
    for (p = 0; p < AB_NPATTERNS; p++) {
        js_pattern_clear(&e->pat[p], AB_NTRACKS, 16);
        e->pat[p].seed = (uint16_t)(301u + p);
        e->pat[p].swing = 52;
        for (i = 0; i < 16u; i++) e->pat[p].t[AB_T_STAB].s[i].data = 0, e->pat[p].t[AB_T_PAD].s[i].data = 0;
    }
    bass_line(&e->pat[0].t[AB_T_BASS], B0, F0, M0);
    bass_line(&e->pat[1].t[AB_T_BASS], B1, F1, 0);
    bass_line(&e->pat[2].t[AB_T_BASS], B2, F2, 0);
    /* pattern 3: no bass (the intro: a pad alone) */
    /* stabs: the off-beat rave stab of pattern 1, a single dark hit in 2 */
    e->pat[1].t[AB_T_STAB].s[6].flags = JS_ON, e->pat[1].t[AB_T_STAB].s[14].flags = JS_ON;
    e->pat[1].t[AB_T_STAB].s[14].data = 5; /* degree VI */
    e->pat[2].t[AB_T_STAB].s[0].flags = JS_ON;
    /* the pad: a chord change at the bar in every pattern (i, then VI on the second bar of 32 steps) */
    for (p = 0; p < AB_NPATTERNS; p++) {
        js_track_t *t = &e->pat[p].t[AB_T_PAD];
        t->len = 32;
        t->s[0].flags = JS_ON;
        t->s[0].data = 0;
        t->s[16].flags = JS_ON;
        t->s[16].data = p == 2 ? 3 : 5;
    }
    { /* (0.6) every step's ENERGY level from its role (at full ENERGY they all play, as before) */
        uint8_t keep = e->cur_pat, k;
        for (p = 0; p < AB_NPATTERNS; p++)
            for (k = 0; k < AB_NTRACKS; k++) e->cur_pat = (uint8_t)p, ab_auto_levels(e, k);
        e->cur_pat = keep;
    }
}

static void scenes_default(ab_engine_t *e)
{
    /* INTRO, BUILD, DROP, MUTATION, VOID, IMPACT */
    static const struct { uint8_t pat, pad; float mo, mt, bi, sp, w, d, wi, pr; } S[6] = {
        {3, 1, 0.0f, 0.2f, 0.0f, 0.8f, 0.4f, 0.1f, 0.7f, 0.2f}, {2, 1, 0.2f, 0.4f, 0.2f, 0.6f, 0.5f, 0.25f, 0.6f, 0.35f},
        {0, 0, 0.45f, 0.6f, 0.45f, 0.3f, 0.6f, 0.4f, 0.4f, 0.55f}, {0, 0, 0.75f, 0.9f, 0.7f, 0.3f, 0.6f, 0.6f, 0.4f, 0.65f},
        {3, 1, 0.1f, 0.1f, 0.0f, 1.0f, 0.3f, 0.0f, 0.9f, 0.1f}, {1, 0, 0.6f, 0.7f, 0.8f, 0.4f, 0.65f, 0.7f, 0.5f, 0.7f},
    };
    uint32_t i;
    for (i = 0; i < AB_NSCENES; i++) {
        ab_scene_t *s = &e->scene[i];
        uint32_t k = i < 6u ? i : 2u;
        s->used = i < 6u;
        s->pattern = S[k].pat;
        s->pad_on = S[k].pad;
        s->mute = 0;
        s->morph = S[k].mo, s->motion = S[k].mt, s->bite = S[k].bi, s->space = S[k].sp;
        s->fx.weight = S[k].w, s->fx.dirt = S[k].d, s->fx.width = S[k].wi, s->fx.pressure = S[k].pr, s->fx.trim_db = 0;
        s->root = 38;
        s->scale = JH_MINOR;
    }
}

void ab_scene_store(ab_engine_t *e, int s)
{
    ab_scene_t *sc;
    if (s < 0 || s >= AB_NSCENES) return;
    sc = &e->scene[s];
    sc->used = 1;
    sc->pattern = e->cur_pat;
    sc->mute = e->seq.mute;
    sc->pad_on = e->pad_on;
    sc->morph = e->morph, sc->motion = e->motion, sc->bite = e->bite, sc->space = e->space;
    sc->fx = e->master.m;
    sc->root = e->root;
    sc->scale = e->scale;
}

/* ---------------------------------------------------------------- notes --- */
static void pad_chord_on(ab_engine_t *e, int degree)
{
    float notes[5];
    int base = jh_degree(e->root + 24, e->scale, degree); /* two octaves over the bass root */
    uint32_t n = jh_chord(base, e->chord, e->tension, e->inversion, notes, JN_PAD_VOICES);
    jn_pad_chord(&e->pad, &e->pad_p, notes, n);
}
static void stab_chord_on(ab_engine_t *e, int note, float vel)
{
    float notes[5];
    uint32_t n = jh_chord(note, e->stab_p.chord > 0.5f ? JH_CH_MIN7 : e->chord, e->stab_p.chord, e->inversion, notes,
                          JN_STAB_VOICES);
    jn_stab_chord(&e->stab, &e->stab_p, notes, n, vel);
}

static void bass_on(ab_engine_t *e, float note, float vel, int slide)
{
    int legato = slide && e->bass_on;
    jn_bass_note(&e->bass, note, vel, legato, &e->patch);
    jn_sub_note(&e->sub, note, e->patch.p[JN_P_GLIDE], legato);
    e->bass_on = 1;
    e->last_bass_note = note;
}
static void bass_off(ab_engine_t *e)
{
    jn_bass_off(&e->bass);
    jn_sub_off(&e->sub);
    e->bass_on = 0;
}

static void fire_(ab_engine_t *e, const js_event_t *ev);
static void fire(ab_engine_t *e, const js_event_t *ev)
{
    if (e->trace) e->trace(e->trace_ctx, ev);
    fire_(e, ev);
}
static void fire_(ab_engine_t *e, const js_event_t *ev)
{
    float vel = (float)ev->vel * (1.0f / 127.0f), sps = js_clock_samples_per_step(&e->clock);
    switch (ev->track) {
    case AB_T_BASS: {
        /* the step's line: the pattern's, or (CALL & RESPONSE) a response's, which xform put in its place */
        const js_track_t *t = bass_src(e, ev->abs_step);
        uint32_t len = t->len ? t->len : 16u, idx = t == &e->seq.pat->t[AB_T_BASS] ? ev->step : ev->abs_step % len;
        const js_step_t *nx = &t->s[(idx + 1u) % len];
        int oct = -1;
        float note;
        uint32_t i;
        e->morph_lock = -1;
        e->bite_lock = -1.0f;
        for (i = 0; i < JS_NLOCK; i++) {
            if (ev->lk_id[i] == AB_LK_MORPH) e->morph_lock = ev->lk_val[i];
            if (ev->lk_id[i] == AB_LK_BITE) e->bite_lock = (float)ev->lk_val[i] * (1.0f / 255.0f);
            if (ev->lk_id[i] == AB_LK_OCT) oct = ev->lk_val[i];
        }
        if (ab_cr_part(e, ev->abs_step) != (int)e->cr_play) { /* a new part of the form: its sound from this note on */
            e->cr_play = (uint8_t)ab_cr_part(e, ev->abs_step);
            patch_update(e);
        }
        note = (float)(e->root + (int)ev->data - 64) + (oct >= 0 ? 12.0f * (float)(oct - 2) : 0.0f);
        if (fill_at(e, ev->abs_step) && e->fill_type == AB_FILL_ROLLS) note += ev->hit ? 12.0f : 0.0f; /* octave jumps on the rolls */
        if (fill_at(e, ev->abs_step) && e->fill_type == AB_FILL_SLIDE) note = (float)jh_snap(e->root, e->scale, (int)note);
        if (e->perf == AB_FX_OCT_UP) note += 12.0f;
        if (e->perf == AB_FX_OCT_DOWN) note -= 12.0f;
        if (e->perf == AB_FX_TAPE) break; /* the tape has stopped: no new notes */
        bass_on(e, note, vel, (ev->flags & JS_SLIDE) != 0);
        /* the gate: past the next step when it slides (legato), else 3/4 of a step (or of a ratchet hit) */
        e->bass_gate_left = (nx->flags & JS_ON) && (nx->flags & JS_SLIDE) ? (uint32_t)(sps * 1.1f)
                                                                           : (uint32_t)(sps * 0.75f / (float)ev->nhits);
        break;
    }
    case AB_T_STAB:
        stab_chord_on(e, jh_degree(e->root + 24, e->scale, ev->data), vel);
        e->stab_gate_left = (uint32_t)(sps * 0.6f);
        break;
    case AB_T_PAD:
        if (e->pad_on && e->voice_mode == AB_VOICE_FULL) pad_chord_on(e, ev->data);
        break;
    default: break;
    }
}

/* (0.6) ANSWER ME: the bar you played (cap_*) transformed into the answer for bar `bar` */
static void duet_build(ab_engine_t *e, uint32_t bar)
{
    uint32_t i, k = 0;
    for (i = 0; i < 16u; i++) e->ans_note[i] = 0, e->ans_vel[i] = 0;
    for (i = 0; i < 16u; i++) {
        uint32_t to = i;
        int d = e->cap_note[i];
        if (!d) continue;
        switch (e->duet_xform) {
        case AB_ANS_INVERT: /* mirrored around the root, on the scale */
            d = 64 + jh_snap(e->root, e->scale, e->root - (d - 64)) - e->root;
            break;
        case AB_ANS_RETRO: to = 15u - i; break;
        case AB_ANS_DISPLACE: to = (i + 2u) & 15u; break;
        case AB_ANS_OCT_DOWN: d -= 12; break;
        case AB_ANS_SPARSE: if (k++ & 1u) continue; break;
        default: break;
        }
        e->ans_note[to] = (uint8_t)jo_clampi(d, 28, 110);
        e->ans_vel[to] = e->cap_vel[i];
    }
    e->ans_bar = bar;
    e->ans_valid = 1;
    e->cap_n = 0;
}

uint16_t ab_pocket_mask(const ab_engine_t *e, uint32_t bar)
{
    if (bar == e->kick_bar + 1u) return e->kick_cur; /* evaluated just before the bar's turn: the bar so far */
    if (bar == e->kick_bar) return e->kick_mask;
    return 0;
}

/* (0.6) POCKET: a bass step on a kick moves to the next gap; the drums' DENSITY thins (high) or fills (low) the
 * bass. Returns 0 to drop the step */
static int pocket(ab_engine_t *e, uint32_t abs, js_step_t *st)
{
    uint32_t i = abs & 15u, h = js_hash(e->seq.pat->seed, abs, 0x50Cu, 3u) % 100u;
    uint16_t mask = ab_pocket_mask(e, abs >> 4);
    int kick = (mask >> i) & 1u, on = (st->flags & JS_ON) != 0;
    float d = e->drum_density, a = e->pocket_amt;
    if (kick) {
        if (on) e->pocket_carry = *st, e->pocket_carry_on = 1; /* to the next gap */
        return 0;
    }
    if (!on && e->pocket_carry_on) { *st = e->pocket_carry; on = 1; }
    e->pocket_carry_on = 0;
    if (on && i != 0u && d > 0.5f && (float)h < a * (d - 0.5f) * 200.0f) return 0;
    if (!on && d < 0.5f && (float)h < a * (0.5f - d) * 120.0f && e->pocket_last) {
        js_step_clear(st);
        st->flags = JS_ON;
        st->data = e->pocket_last;
        st->vel = 90;
        on = 1;
    }
    if (on) e->pocket_last = st->data;
    return 1;
}

static int xform(void *ctx, uint32_t t, uint32_t abs, js_step_t *st)
{
    ab_engine_t *e = (ab_engine_t *)ctx;
    if (t == AB_T_BASS && e->duet_answer) { /* (0.6) ANSWER ME: your bars are yours, the machine's are its answers */
        uint32_t bar = abs >> 4, i = abs & 15u;
        if (i == 0u && e->cap_n && e->cap_bar + 1u == bar) duet_build(e, bar);
        if (!(e->ans_valid && e->ans_bar == bar)) return 0;
        js_step_clear(st);
        if (!e->ans_note[i]) return 0;
        st->flags = JS_ON;
        st->data = e->ans_note[i];
        st->vel = e->ans_vel[i];
        return 1;
    }
    if (t == AB_T_BASS) { /* a variation's bar plays its own line's step */
        const js_track_t *src = bass_src(e, abs);
        if (src != &e->seq.pat->t[AB_T_BASS]) *st = src->s[abs % (src->len ? src->len : 16u)];
        /* LOCK TO KICK: the link's kicks play the bass (ab_link_kick), the steps only give it notes */
        if (e->lock_kick && jl_alive(&e->link, e->ms)) return 0;
        if (e->pocket_amt > 0.0f && jl_alive(&e->link, e->ms) && !pocket(e, abs, st)) return 0;
    }
    if (t == AB_T_BASS && fill_at(e, abs) && (st->flags & JS_ON)) {
        switch (e->fill_type) {
        case AB_FILL_ROLLS: /* the rolls in the bass */
            if ((abs & 15u) >= 8u) st->ratch = (uint8_t)((abs & 15u) >= 12u ? 2 : 1);
            break;
        case AB_FILL_OCTAVES: if ((abs >> 1) & 1u) st->data = (uint8_t)(st->data + 12u); break;
        case AB_FILL_DROP: return 0;
        case AB_FILL_SLIDE: st->flags |= JS_SLIDE; st->data = (uint8_t)(st->data - ((abs & 15u) >> 1)); break;
        default: break;
        }
    }
    if (t == AB_T_BASS && e->perf == AB_FX_STUTTER) { /* the first note under the FX, repeated in 32nds */
        if (!e->stutter_have && (st->flags & JS_ON)) e->stutter_step = *st, e->stutter_have = 1;
        if (e->stutter_have) {
            *st = e->stutter_step;
            st->flags = (uint8_t)((st->flags | JS_ON) & ~JS_SLIDE);
            st->ratch = 1;
        }
    }
    if (e->humanize > 0.0f && (st->flags & JS_ON)) { /* HUMANIZE: fixed small offsets per step and bar */
        uint32_t h = js_hash(e->seq.pat->seed, abs, t, 77u);
        st->vel = (uint8_t)jo_clampi((int)st->vel + (int)((float)((int)(h & 31u) - 16) * e->humanize * 1.2f), 1, 127);
        st->micro = (int8_t)jo_clampi((int)st->micro + (int)((float)((int)((h >> 5) & 31u) - 16) * e->humanize), -64, 63);
    }
    if (t == AB_T_STAB && e->voice_mode == AB_VOICE_BASS_ONLY) return 0;
    return 1;
}

static uint32_t next_boundary(const ab_engine_t *e, int quant)
{
    static const uint32_t Q[5] = {1u, 4u, 16u, 32u, 64u};
    uint32_t q = quant == 5 ? 16u * (e->phrase ? e->phrase : 4u) : Q[quant < 0 || quant > 4 ? 2 : quant]; /* 5: the phrase */
    uint32_t a = e->clock.running ? e->clock.abs_step + 2u : 0u;
    return (a + q - 1u) / q * q;
}

static void apply_scene(ab_engine_t *e, const ab_scene_t *s, int pattern_part)
{
    if (pattern_part) {
        e->cur_pat = e->next_pat = s->pattern % AB_NPATTERNS;
        e->seq.pat = &e->pat[e->cur_pat];
        e->seq.mute = s->mute;
        js_seq_reset(&e->seq);
        return;
    }
    e->morph = s->morph, e->motion = s->motion, e->bite = s->bite, e->space = s->space;
    e->explore_node = -1;
    e->mutated = 0;
    e->master.m = s->fx;
    jf_neuro_update(&e->master);
    e->root = s->root;
    e->scale = s->scale;
    if (s->pad_on != e->pad_on) {
        e->pad_on = s->pad_on;
        if (e->pad_on) pad_chord_on(e, 0);
        else jn_pad_release(&e->pad); /* the release tail plays out (the voices are not cut) */
    }
}

static void on_step(void *ctx, uint32_t abs, uint32_t off)
{
    ab_engine_t *e = (ab_engine_t *)ctx;
    uint32_t ph = 16u * (e->phrase ? e->phrase : 4u), at = abs % ph, i;
    (void)off;
    /* (0.6) ENERGY: the curve (REC from the phrase's start), the steps' gate */
    if (e->en_armed && at == 0u) e->en_armed = 0, e->en.recording = 1, e->en.playing = 0;
    je_step(&e->en, abs, e->phrase ? e->phrase : 4u);
    e->seq.energy = je_level(e->en.value);
    /* (0.6) DUET: the answer's bar over; POCKET's kicks of the bar before; GESTURE ECHO's phrase */
    if (e->ans_valid && (abs >> 4) > e->ans_bar) e->ans_valid = 0;
    if ((abs & 15u) == 0u) {
        uint32_t bar = abs >> 4;
        if (bar == e->kick_bar + 1u) e->kick_mask = e->kick_cur, e->kick_cur = e->kick_next;
        else if (bar != e->kick_bar) e->kick_mask = 0, e->kick_cur = e->kick_next;
        e->kick_next = 0;
        e->kick_bar = bar;
    }
    if (at == 0u) { /* the phrase turns: what was recorded plays, at half depth */
        float now[4];
        now[0] = e->morph, now[1] = e->motion, now[2] = e->bite, now[3] = e->glide_amt;
        e->g_nplay = 0;
        for (i = 0; i < e->g_nrec; i++) {
            ab_gest_t g = e->g_rec[i];
            if (g.type == 0u && g.val) { /* an FX: half as long as it was held */
                uint32_t k, end = ph;
                for (k = i + 1u; k < e->g_nrec; k++) if (e->g_rec[k].type == 0u) { end = e->g_rec[k].at; break; }
                g.end = (uint16_t)(g.at + (end > g.at ? (end - g.at + 1u) / 2u : 1u));
            }
            if (g.type || g.val) e->g_play[e->g_nplay++] = g;
        }
        for (i = 0; i < 4u; i++) e->g_base_play[i] = e->g_base_rec[i], e->g_base_rec[i] = now[i], e->echo_off[i] = 0.0f;
        e->g_nrec = 0;
        e->perf_echo = 0;
    }
    if (e->gesture || e->g_nplay) {
        for (i = 0; i < e->g_nplay; i++) {
            const ab_gest_t *g = &e->g_play[i];
            if (g->at != at) continue;
            if (g->type == 0u) e->perf_echo = g->val, e->perf_echo_end = g->end;
            else e->echo_off[(g->type - 1u) & 3u] = 0.5f * ((float)g->val * (1.0f / 255.0f) - e->g_base_play[(g->type - 1u) & 3u]);
        }
        if (e->perf_echo && at >= e->perf_echo_end) e->perf_echo = 0;
        if (!e->gesture) e->g_nplay = 0, e->perf_echo = 0;
    }
    if (e->fill_on && !js_before(abs, e->fill_end)) e->fill_on = 0;
    if (e->answer_on && !js_before(abs, e->answer_end)) e->answer_on = 0;
    e->seq.fill = (uint8_t)fill_at(e, abs + 1u); /* for the next step's trig conditions */
    if (abs + 1u == e->var_at) e->var_sel = e->var_next, e->var_at = 0xFFFFFFFFu;
    if (abs + 1u == e->pending_at) {
        if (e->scene_pending >= 0) apply_scene(e, &e->scene[e->scene_pending], 1);
        else if (e->next_pat != e->cur_pat) {
            e->cur_pat = e->next_pat;
            e->seq.pat = &e->pat[e->cur_pat];
            js_seq_reset(&e->seq);
        }
    }
    if (abs == e->pending_at && e->scene_pending >= 0) {
        apply_scene(e, &e->scene[e->scene_pending], 0);
        e->scene_cur = e->scene_pending;
        e->scene_pending = -1;
    }
    /* standalone ducking: on every beat when no link is heard */
    if (e->duck_internal && !jl_alive(&e->link, e->ms) && (abs & 3u) == 0u) jf_duck_trigger(&e->duck, 1.0f);
}

/* ----------------------------------------------------------------- init --- */
void ab_init(ab_engine_t *e, int16_t *pool)
{
    uint8_t *q = (uint8_t *)e;
    uint32_t i;
    for (i = 0; i < sizeof *e; i++) q[i] = 0;
    jn_bass_init(&e->bass);
    jn_sub_init(&e->sub);
    jn_stab_init(&e->stab);
    jn_pad_init(&e->pad);
    e->stab_p.model = JN_STAB_RAVE;
    e->stab_p.chord = 0.3f, e->stab_p.attack = 0.1f, e->stab_p.bite = 0.5f, e->stab_p.tail = 0.4f;
    e->pad_p.darkness = 0.5f, e->pad_p.evolve = 0.4f, e->pad_p.tension = 0.3f, e->pad_p.depth = 0.6f;
    e->pad_p.attack_s = 1.5f, e->pad_p.release_s = 3.0f;
    e->morph = e->morph_s = 0.4f;
    e->motion = 0.5f, e->bite = 0.3f, e->space = 0.4f;
    e->morph_lock = -1;
    e->bite_lock = -1.0f;
    e->explore_node = -1;
    e->root = 38; /* D */
    e->scale = JH_MINOR;
    e->chord = JH_CH_MIN;
    e->tension = 0.2f;
    e->scene_pending = -1;
    e->scene_cur = 2;
    je_init(&e->en);
    e->en_sent = 0xFFu;
    e->duet_xform = AB_ANS_ECHO;
    e->decay_amt = 0.3f;
    e->kick_bar = 0xFFFFFFF0u;
    e->follow_link = 1;
    e->duck_depth = 0.6f;
    e->cr[0].line = -1, e->cr[0].node = 8;  /* B: A's line as REESE */
    e->cr[1].line = 1, e->cr[1].node = 6;   /* C: P2's line as LASER, the turnaround */
    e->cr[2].line = 2, e->cr[2].node = 3;   /* D: P3's line as GROWL */
    e->var_at = 0xFFFFFFFFu;
    e->glide_amt = 0.5f;
    e->tape_gain = 1.0f;
    graph_init(e);
    patch_update(e);
    js_clock_init(&e->clock, 174.0f);
    ab_default_patterns(e);
    js_seq_init(&e->seq, &e->pat[0]);
    ab_set_phrase(e, 4);
    scenes_default(e);
    jf_neuro_init(&e->master);
    if (pool) {
        jf_delay_init(&e->delay, pool, pool + JF_DELAY_LEN);
        jf_verb_init(&e->verb, pool + 2u * JF_DELAY_LEN);
    }
    jf_duck_set(&e->duck, e->duck_depth, 0.002f, 0.03f, 0.12f);
    jl_init(&e->link, 15);
    e->pending_at = 0xFFFFFFFFu;
}

void ab_master_update(ab_engine_t *e) { jf_neuro_update(&e->master); }

/* ------------------------------------------------------------- commands --- */
static void cmd(ab_engine_t *e, uint8_t op, uint8_t a, uint8_t b, uint8_t c, float f) { jo_cmdq_push(&e->cmd, op, a, b, c, f); }
void ab_cmd_play(ab_engine_t *e, int on) { cmd(e, C_PLAY, (uint8_t)(on != 0), 0, 0, 0.0f); }
void ab_cmd_bpm(ab_engine_t *e, float bpm) { cmd(e, C_BPM, 0, 0, 0, bpm); }
void ab_cmd_note(ab_engine_t *e, int part, int note, float vel, int on) { cmd(e, C_NOTE, (uint8_t)part, (uint8_t)note, (uint8_t)(on != 0), vel); }
void ab_cmd_stab_chord(ab_engine_t *e, float vel) { cmd(e, C_STAB, 0, 0, 0, vel); }
void ab_cmd_pad(ab_engine_t *e, int on) { cmd(e, C_PAD, (uint8_t)(on != 0), 0, 0, 0.0f); }
void ab_cmd_pattern(ab_engine_t *e, int p) { cmd(e, C_PATTERN, (uint8_t)p, 0, 0, 0.0f); }
void ab_cmd_scene(ab_engine_t *e, int s, int quant) { cmd(e, C_SCENE, (uint8_t)s, (uint8_t)quant, 0, 0.0f); }
void ab_cmd_panic(ab_engine_t *e) { cmd(e, C_PANIC, 0, 0, 0, 0.0f); }
void ab_cmd_mute(ab_engine_t *e, int t) { cmd(e, C_MUTE, (uint8_t)t, 0, 0, 0.0f); }
void ab_cmd_fill(ab_engine_t *e, int bars) { cmd(e, C_FILL, (uint8_t)bars, 0, 0, 0.0f); }
void ab_cmd_return(ab_engine_t *e) { cmd(e, C_RETURN, 0, 0, 0, 0.0f); }
void ab_cmd_variation(ab_engine_t *e, int v) { cmd(e, C_VAR, (uint8_t)(v % AB_NVAR), 0, 0, 0.0f); }
void ab_cmd_fill_type(ab_engine_t *e, int t, int bars, int at_end) { cmd(e, C_FILLT, (uint8_t)t, (uint8_t)bars, (uint8_t)(at_end != 0), 0.0f); }

static void transport(ab_engine_t *e, int on)
{
    if (on && !e->clock.running) {
        js_seq_reset(&e->seq);
        js_queue_clear(&e->q);
        js_clock_start(&e->clock);
    } else if (!on && e->clock.running) {
        js_clock_stop(&e->clock);
        js_queue_clear(&e->q);
        bass_off(e);
        jn_stab_release(&e->stab);
    }
}


static void do_cmd(ab_engine_t *e, const jo_cmd_t *c)
{
    switch (c->op) {
    case C_PLAY: transport(e, c->a); break;
    case C_BPM: js_clock_set_bpm(&e->clock, c->f); break;
    case C_NOTE:
        if (c->a == AB_PART_BASS) {
            if (c->c && e->duet_answer && e->clock.running) { /* (0.6) ANSWER ME hears the bar you play */
                uint32_t pos = e->clock.abs_step + (e->clock.phase >= 0x80000000u ? 1u : 0u), bar;
                if ((pos >> 4) != (e->clock.abs_step >> 4)) pos = e->clock.abs_step; /* not into the next bar */
                bar = pos >> 4;
                if (!(e->ans_valid && e->ans_bar == bar)) {
                    if (!e->cap_n || e->cap_bar != bar) {
                        uint32_t k;
                        for (k = 0; k < 16u; k++) e->cap_note[k] = 0, e->cap_vel[k] = 0;
                        e->cap_n = 0, e->cap_bar = bar;
                    }
                    e->cap_note[pos & 15u] = (uint8_t)jo_clampi((int)c->b - e->root + 64, 28, 110);
                    e->cap_vel[pos & 15u] = (uint8_t)jo_clampi((int)(c->f * 127.0f + 0.5f), 1, 127);
                    e->cap_n++;
                }
            }
            if (c->c) bass_on(e, (float)c->b, c->f, e->bass_on), e->bass_gate_left = 0xFFFFFFFFu;
            else if (e->last_bass_note == (float)c->b) bass_off(e);
        } else if (c->a == AB_PART_STAB) {
            if (c->c) stab_chord_on(e, c->b, c->f), e->stab_gate_left = 0xFFFFFFFFu;
            else jn_stab_release(&e->stab);
        } else if (c->c) {
            e->pad_on = 1;
            {
                float notes[5];
                uint32_t n = jh_chord(c->b, e->chord, e->tension, e->inversion, notes, JN_PAD_VOICES);
                jn_pad_chord(&e->pad, &e->pad_p, notes, n);
            }
        }
        break;
    case C_STAB: stab_chord_on(e, jh_degree(e->root + 24, e->scale, 0), c->f); e->stab_gate_left = (uint32_t)(0.15f * JO_SR); break;
    case C_PAD:
        e->pad_on = c->a;
        if (c->a) pad_chord_on(e, 0);
        else jn_pad_release(&e->pad);
        break;
    case C_PATTERN:
        e->next_pat = c->a % AB_NPATTERNS;
        e->pending_at = next_boundary(e, e->quant ? 5 : JL_Q_BAR);
        if (!e->clock.running) e->cur_pat = e->next_pat, e->seq.pat = &e->pat[e->cur_pat];
        break;
    case C_SCENE:
        if (c->a < AB_NSCENES && e->scene[c->a].used) {
            if (!e->clock.running || c->b == JL_Q_NOW) {
                apply_scene(e, &e->scene[c->a], 1);
                apply_scene(e, &e->scene[c->a], 0);
                e->scene_cur = (int8_t)c->a;
            } else {
                e->scene_pending = (int8_t)c->a;
                e->pending_at = next_boundary(e, c->b);
            }
        }
        break;
    case C_PANIC:
        js_queue_clear(&e->q);
        bass_off(e);
        jn_stab_release(&e->stab);
        jn_pad_release(&e->pad);
        e->pad_on = 0;
        break;
    case C_MUTE: e->seq.mute ^= (uint8_t)(1u << (c->a % AB_NTRACKS)); break;
    case C_FILL: /* (0.2) a fill by hand, as the link's: the bass's rolls and octave jumps up to the bar after next */
        e->fill_on = 1;
        e->fill_type = AB_FILL_ROLLS;
        e->fill_start = e->clock.abs_step;
        e->fill_end = next_boundary(e, JL_Q_BAR) + 16u * (c->a == 2 ? 2u : 1u);
        break;
    case C_FILLT: { /* (0.5) a fill of a type: from the next bar, or in the phrase's last bar(s) */
        uint32_t bars = c->b == 2 ? 2u : 1u, start = next_boundary(e, JL_Q_BAR);
        if (c->c) {
            uint32_t ph = 16u * (e->phrase ? e->phrase : 4u), end = next_boundary(e, 5);
            if (js_before(end - 16u * bars, start)) end += ph; /* too late for this phrase: the next one's */
            start = end - 16u * bars;
        }
        e->fill_on = 1;
        e->fill_type = c->a % AB_NFILLS;
        e->fill_start = start;
        e->fill_end = start + 16u * bars;
        break;
    }
    case C_VAR: /* (0.5) a variation picked: FORM OFF, from the next bar (or the phrase's end) */
        ab_set_form(e, AB_FORM_OFF);
        e->var_next = c->a % AB_NVAR;
        if (!e->clock.running) e->var_sel = e->var_next, e->var_at = 0xFFFFFFFFu;
        else e->var_at = next_boundary(e, e->quant ? 5 : JL_Q_BAR);
        break;
    case C_GEST: /* (0.6) a knob move for GESTURE ECHO */
        if (e->gesture && e->clock.running && e->g_nrec < AB_NGEST) {
            ab_gest_t *g = &e->g_rec[e->g_nrec++];
            g->at = (uint16_t)(e->clock.abs_step % (16u * (e->phrase ? e->phrase : 4u)));
            g->end = 0;
            g->type = (uint8_t)(1u + (c->a & 3u));
            g->val = c->b;
        }
        break;
    case C_RETURN: /* (0.2) back to the base: the mutation and the explore left, the fill ended, MORPH on its node */
        e->fill_on = 0;
        ab_snap(e);
        break;
    default: break;
    }
}

/* ----------------------------------------------------------------- link --- */
void ab_link_kick(ab_engine_t *e, uint8_t vel)
{
    e->link_kicks++;
    jf_duck_trigger(&e->duck, (float)vel * (1.0f / 127.0f));
    if (e->clock.running) { /* (0.6) POCKET learns where the kicks are, to the nearest 16th */
        uint32_t pos = e->clock.abs_step + (e->clock.phase >= 0x80000000u ? 1u : 0u);
        if ((pos >> 4) == e->kick_bar || e->kick_bar == 0xFFFFFFF0u) e->kick_cur |= (uint16_t)(1u << (pos & 15u)), e->kick_bar = pos >> 4;
        else if ((pos >> 4) == e->kick_bar + 1u) e->kick_next |= (uint16_t)(1u << (pos & 15u));
    }
    /* (0.5) LOCK TO KICK: the kick plays the bass, the note of its line's step now (or the last one before it) */
    if (e->lock_kick && e->clock.running && e->perf != AB_FX_TAPE) {
        uint32_t abs = e->clock.abs_step, k;
        const js_track_t *t = bass_src(e, abs);
        uint32_t len = t->len ? t->len : 16u, idx = abs % len;
        int part = ab_cr_part(e, abs);
        for (k = 0; k < len; k++) {
            const js_step_t *st = &t->s[(idx + len - k) % len];
            if (st->flags & JS_ON) {
                float note = (float)(e->root + (int)st->data - 64);
                if (part != (int)e->cr_play) { e->cr_play = (uint8_t)part; patch_update(e); }
                if (e->perf == AB_FX_OCT_UP) note += 12.0f;
                if (e->perf == AB_FX_OCT_DOWN) note -= 12.0f;
                bass_on(e, note, 0.5f + 0.5f * (float)vel * (1.0f / 127.0f), 0);
                e->bass_gate_left = (uint32_t)(js_clock_samples_per_step(&e->clock) * 1.5f);
                break;
            }
        }
    }
}

void ab_link_packet(ab_engine_t *e, uint32_t pkt, uint32_t t24)
{
    jl_event_t ev;
    if (!jl_decode(&e->link, pkt, e->ms, &ev)) return;
    switch (ev.type) {
    case JL_EV_CLOCK:
        if (e->follow_link) {
            e->clock.source = JS_CLK_MIDI;
            e->last_clock_ms = e->ms;
            js_clock_midi_tick(&e->clock, t24);
        }
        break;
    case JL_EV_START:
        if (e->follow_link) {
            js_seq_reset(&e->seq);
            js_queue_clear(&e->q);
            js_clock_midi_start(&e->clock);
        }
        break;
    case JL_EV_CONTINUE: if (e->follow_link) e->clock.running = 1; break;
    case JL_EV_STOP: if (e->follow_link) transport(e, 0); break;
    case JL_EV_KICK: ab_link_kick(e, ev.a); break;
    case JL_EV_SCENE: {
        jo_cmd_t c = {C_SCENE, ev.a, ev.b, 0, 0.0f};
        do_cmd(e, &c);
        break;
    }
    case JL_EV_FILL_BEGIN:
        e->fill_on = 1;
        e->fill_type = AB_FILL_ROLLS;
        e->fill_start = e->clock.abs_step;
        e->fill_end = next_boundary(e, JL_Q_BAR) + 16u * ev.a;
        if (e->fill_answers) { /* FILL ANSWERS: the fill's bars play variation C */
            e->answer_on = 1;
            e->answer_start = next_boundary(e, JL_Q_BAR);
            e->answer_end = e->fill_end;
        }
        break;
    case JL_EV_FILL_END: e->fill_on = 0; e->answer_on = 0; break;
    case JL_EV_RESET: e->mutated = 0; e->explore_node = -1; e->fill_on = 0; break;
    case JL_EV_ENERGY: e->en.knob = (float)ev.a * (1.0f / 127.0f); e->en_sent = ev.a; break; /* (0.6) follow, no echo */
    case JL_EV_DENSITY: e->drum_density = (float)ev.a * (1.0f / 127.0f); break;
    case JL_EV_ROOT: e->root = (int8_t)(36 + ev.a); break;
    case JL_EV_SCALE: e->scale = ev.a < JH_NSCALES ? ev.a : 0u; break;
    default: break;
    }
}

/* --------------------------------------------------------------- render --- */
static void render_voices(ab_engine_t *e, uint32_t pos, uint32_t n)
{
    float steps = (float)e->clock.abs_step + (float)e->clock.phase * (1.0f / 4294967296.0f);
    uint32_t i;
    jn_bass_render(&e->bass, &e->patch, steps, 0.0f, e->b_bass + pos, n);
    jn_sub_render(&e->sub, e->b_sub + pos, n);
    if (e->voice_mode != AB_VOICE_BASS_ONLY) jn_stab_render(&e->stab, &e->stab_p, e->b_l + pos, e->b_r + pos, n);
    if (e->voice_mode == AB_VOICE_FULL) {
        if (e->en.value < 1.0f) { /* (0.6) less ENERGY: a darker pad */
            jn_pad_patch_t pp = e->pad_p;
            pp.darkness = jo_minf(pp.darkness + 0.4f * (1.0f - e->en.value), 1.0f);
            jn_pad_render(&e->pad, &pp, e->b_l + pos, e->b_r + pos, n);
        } else
            jn_pad_render(&e->pad, &e->pad_p, e->b_l + pos, e->b_r + pos, n);
    }
    for (i = 0; i < n; i++) e->b_duck[pos + i] = jf_duck_tick(&e->duck);
    /* the gates count down in the segment */
    if (e->bass_gate_left != 0xFFFFFFFFu) {
        if (e->bass_gate_left > n) e->bass_gate_left -= n;
        else if (e->bass_gate_left) { e->bass_gate_left = 0; bass_off(e); }
    }
    if (e->stab_gate_left != 0xFFFFFFFFu) {
        if (e->stab_gate_left > n) e->stab_gate_left -= n;
        else if (e->stab_gate_left) { e->stab_gate_left = 0; jn_stab_release(&e->stab); }
    }
}

static void render_block(ab_engine_t *e, uint32_t n)
{
    js_hooks_t h;
    jo_cmd_t c;
    js_event_t ev;
    uint32_t t0, pos = 0, i;
    float trim, send_amt;
    while (jo_cmdq_pop(&e->cmd, &c)) do_cmd(e, &c);
    e->perf = e->perf_user ? e->perf_user : e->perf_echo; /* (0.6) GESTURE ECHO plays an FX when you hold none */
    if (e->perf_user != e->g_last_user) {
        if (e->gesture && e->clock.running && e->g_nrec < AB_NGEST) {
            ab_gest_t *g = &e->g_rec[e->g_nrec++];
            g->at = (uint16_t)(e->clock.abs_step % (16u * (e->phrase ? e->phrase : 4u)));
            g->end = 0;
            g->type = 0;
            g->val = e->perf_user;
        }
        e->g_last_user = e->perf_user;
    }
    if (e->perf != e->perf_was) { /* (0.5) a performance FX begins or ends */
        if (e->perf_was == AB_FX_TAPE) { e->bass.target = e->tape_bass, e->sub.target = e->tape_sub; e->tape_gain = 1.0f; }
        e->perf_n = 0;
        e->stutter_have = 0;
        if (e->perf == AB_FX_TAPE) e->tape_bass = e->bass.target, e->tape_sub = e->sub.target;
        e->perf_was = e->perf;
    }
    if (e->perf == AB_FX_TAPE) { /* the tape slows: two octaves down and silent over 0.6 s */
        float k = jo_minf((float)e->perf_n * (1.0f / (0.6f * JO_SR)), 1.0f), f = jo_exp2f(-2.0f * k);
        e->bass.target = e->tape_bass * f;
        e->sub.target = e->tape_sub * f;
        e->tape_gain = 1.0f - k;
    }
    if (e->perf) e->perf_n += n;
    patch_update(e);
    jf_duck_set(&e->duck, e->duck_depth, 0.002f, 0.03f, 0.12f);
    h.on_step = on_step;
    h.on_clock_out = 0;
    h.transform = xform;
    h.ctx = e;
    t0 = e->clock.now;
    js_run(&e->seq, &e->clock, &e->q, n, &h);
    for (i = 0; i < n; i++) e->b_sub[i] = e->b_bass[i] = e->b_l[i] = e->b_r[i] = 0.0f;
    for (;;) {
        uint32_t nd, end;
        while (js_queue_pop_due(&e->q, t0 + pos + 1u, &ev)) fire(e, &ev);
        if (pos >= n) break;
        nd = js_queue_next_due(&e->q, t0 + n);
        end = js_before(nd, t0 + n) ? nd - t0 : n;
        if (end <= pos) end = pos + 1u;
        render_voices(e, pos, end - pos);
        pos = end;
    }
    /* the bass's loudness: the node's trim and BITE's compensation */
    trim = jo_db2lin(e->fv.trim_db - 4.0f * (e->bite_lock >= 0.0f ? e->bite_lock : e->bite));
    send_amt = 0.15f + 0.6f * e->space;
    if (e->perf == AB_FX_SUB_ONLY) trim = 0.0f;
    for (i = 0; i < n; i++) {
        e->b_bass[i] *= trim;
        e->b_send[i] = (e->b_l[i] + e->b_r[i]) * 0.5f * send_amt;
    }
    if (e->perf == AB_FX_TAPE)
        for (i = 0; i < n; i++) e->b_bass[i] *= e->tape_gain, e->b_sub[i] *= e->tape_gain;
    if (e->delay.bl) {
        float spb = js_clock_samples_per_step(&e->clock) * 3.0f * JO_INV_SR; /* a dotted 8th */
        jf_delay_set(&e->delay, spb, 0.3f + 0.35f * e->space, 3500.0f);
        e->delay.mix = 0.5f;
        jf_delay_process(&e->delay, e->b_send, e->b_l, e->b_r, n);
        jf_verb_process(&e->verb, e->b_send, e->b_l, e->b_r, n);
    }
    jf_neuro_process(&e->master, e->b_sub, e->b_bass, e->b_l, e->b_r, e->b_duck, n);
    {
        uint32_t tot = n * 1000u + e->ms_rem;
        e->ms += tot / 44118u;
        e->ms_rem = tot % 44118u;
    }
    /* the clock gone quiet for 500 ms: keep running at the last tempo, on the internal clock */
    if (e->clock.source == JS_CLK_MIDI && e->ms - e->last_clock_ms > 500u) e->clock.source = JS_CLK_INT;
    e->blocks++;
}

void ab_render(ab_engine_t *e, int32_t *lr, uint32_t n, float out_gain)
{
    uint32_t i, done = 0;
    float pk = e->peak;
    while (done < n) {
        uint32_t m = n - done > AB_BLOCK_MAX ? AB_BLOCK_MAX : n - done;
        render_block(e, m);
        for (i = 0; i < m; i++) {
            float l = jo_clampf(e->b_l[i] * out_gain, -1.0f, 1.0f), r = jo_clampf(e->b_r[i] * out_gain, -1.0f, 1.0f);
            if (jo_absf(l) > pk) pk = jo_absf(l);
            if (jo_absf(r) > pk) pk = jo_absf(r);
            lr[2u * (done + i)] = (int32_t)(l * 8388607.0f);
            lr[2u * (done + i) + 1u] = (int32_t)(r * 8388607.0f);
        }
        done += m;
    }
    e->peak = pk;
}

/* -------------------------------------------------------------- library --- */
/* (0.5) bass lines as scale degrees (R: rest; 7 = the octave above; negative: below the root), flags: 'A' accent,
 * 'S' slide (legato into the note), 'x' plain. The genre's common forms, written for this firmware */
#define R (-99)
const char *const ab_line_name[AB_NLINES] = {"ROLLING", "OFFBEAT", "NEURO STABS", "HALF-TIME", "REESE LONG",
                                             "TWO-STEP", "ACID", "WOBBLE 8THS"};
static const int8_t LINE_DEG[AB_NLINES][16] = {
    {0, R, R, 0, R, R, 0, R, 0, R, R, 0, R, R, 4, R},
    {R, R, 0, R, R, R, 0, R, R, R, 0, R, R, R, 3, R},
    {0, R, R, 0, R, R, R, R, R, R, 0, 0, R, R, R, R},
    {0, 0, 0, 0, 0, 0, R, R, R, R, R, R, R, R, -2, -2},
    {0, 0, 0, 0, 0, 0, 0, 0, -2, -2, -2, -2, -2, -2, -2, -2},
    {0, R, R, R, R, R, R, R, R, R, 0, R, R, 2, R, R},
    {0, 0, 7, 0, R, 0, 3, 0, 0, R, 7, 0, 4, 0, R, 3},
    {0, R, 0, R, 0, R, 0, R, 3, R, 3, R, 3, R, 3, R},
};
static const char LINE_FL[AB_NLINES][17] = {
    "A..x..x.A..x..S.", "..A...x...A...x.", "A..A......AA....", "ASSSSS........AS",
    "ASSSSSSSASSSSSSS", "A.........A..x..", "A.SA.S.AS.A.S.A.", "A.x.x.x.A.x.x.x.",
};
const char *const ab_stab_rhythm_name[AB_NRHYTHMS] = {"OFFBEAT STABS", "2-STEP STABS", "RAVE", "DOTTED", "NONE"};
const char *const ab_pad_rhythm_name[AB_NRHYTHMS] = {"WHOLE BARS", "I - VI", "FOUR CHORDS", "PULSE", "NONE"};

/* a scale degree (negative: below the root) as semitones from the root */
static int deg_rel(int root, int scale, int deg)
{
    int oct = 0;
    while (deg < 0) deg += 7, oct--;
    return jh_degree(root, scale, deg) + 12 * oct - root;
}

void ab_load_line(ab_engine_t *e, int l)
{
    js_track_t t = e->pat[e->cur_pat].t[AB_T_BASS];
    uint32_t i, k = (uint32_t)l % AB_NLINES;
    for (i = 0; i < JS_MAXSTEPS; i++) js_step_clear(&t.s[i]), t.s[i].data = 64;
    t.len = 16;
    t.div = 0;
    for (i = 0; i < 16u; i++) {
        js_step_t *st = &t.s[i];
        if (LINE_DEG[k][i] == R) continue;
        st->flags = JS_ON;
        st->data = (uint8_t)(64 + deg_rel(e->root, e->scale, LINE_DEG[k][i]));
        st->vel = 100;
        if (LINE_FL[k][i] == 'S') st->flags |= JS_SLIDE;
        if (LINE_FL[k][i] == 'A') st->flags |= JS_ACCENT, st->vel = 124;
    }
    e->pat[e->cur_pat].t[AB_T_BASS] = t;
    ab_auto_levels(e, AB_T_BASS);
}

void ab_load_rhythm(ab_engine_t *e, int track, int r)
{
    static const int8_t STAB[AB_NRHYTHMS][16] = {
        {R, R, R, R, R, R, 0, R, R, R, R, R, R, R, 5, R}, {0, R, R, R, R, R, R, R, R, R, 0, R, R, R, R, R},
        {R, R, 0, R, R, R, 0, R, R, R, 5, R, R, R, 4, R}, {0, R, R, 0, R, R, 0, R, R, R, R, R, R, R, R, R},
        {R, R, R, R, R, R, R, R, R, R, R, R, R, R, R, R}};
    /* the pad: the chords' degrees at 0 and 16 (two), or at 0 8 16 24 (four) */
    static const int8_t PADR[AB_NRHYTHMS][4] = {{0, 0, -1, -1}, {0, 5, -1, -1}, {0, 5, 2, 6}, {0, 0, 0, 0}, {-1, -1, -1, -1}};
    js_track_t t;
    uint32_t i, k = (uint32_t)r % AB_NRHYTHMS;
    if (track != AB_T_STAB && track != AB_T_PAD) return;
    t = e->pat[e->cur_pat].t[track];
    for (i = 0; i < JS_MAXSTEPS; i++) js_step_clear(&t.s[i]);
    t.div = 0;
    if (track == AB_T_STAB) {
        t.len = 16;
        for (i = 0; i < 16u; i++)
            if (STAB[k][i] != R) t.s[i].flags = JS_ON, t.s[i].data = (uint8_t)STAB[k][i];
    } else if (k == 3) { /* PULSE: the chord on every beat */
        t.len = 16;
        for (i = 0; i < 16u; i += 4u) t.s[i].flags = JS_ON, t.s[i].data = 0;
    } else {
        t.len = 32;
        for (i = 0; i < 4u; i++)
            if (PADR[k][i] >= 0) {
                uint32_t at = PADR[k][2] >= 0 ? i * 8u : i * 16u;
                if (at < 32u) t.s[at].flags = JS_ON, t.s[at].data = (uint8_t)PADR[k][i];
            }
    }
    e->pat[e->cur_pat].t[track] = t;
    ab_auto_levels(e, track);
}
#undef R

/* the ranking DENSITY adds by (the first first) and removes by (the last first): the downbeat, the beats, the 8ths,
 * then the offbeat 16ths */
static const uint8_t DRANK[16] = {0, 8, 5, 9, 2, 10, 6, 11, 1, 12, 4, 13, 3, 14, 7, 15};

int ab_density(ab_engine_t *e, int track, int dir)
{
    js_track_t *t = &e->pat[e->cur_pat].t[(uint32_t)track % AB_NTRACKS];
    uint32_t len = t->len ? t->len : 16u, i, best = 0xFFFFu, pick = 0xFFFFu;
    for (i = 0; i < len; i++) {
        uint32_t r = DRANK[i & 15u] * 2u + (i >= 16u), on = (t->s[i].flags & JS_ON) != 0;
        if (dir > 0 && !on && r < best) best = r, pick = i;
        if (dir < 0 && on && (pick == 0xFFFFu || r > best)) best = r, pick = i;
    }
    if (pick == 0xFFFFu) return 0;
    if (dir > 0) {
        js_step_t *st = &t->s[pick];
        uint32_t k;
        st->flags = JS_ON;
        st->vel = 100;
        st->data = track == AB_T_BASS ? 64u : 0u;
        for (k = 1; k < len; k++) /* the note of the step before it */
            if (t->s[(pick + len - k) % len].flags & JS_ON) { st->data = t->s[(pick + len - k) % len].data; break; }
    } else
        t->s[pick].flags &= (uint8_t)~JS_ON;
    return 1;
}

int ab_record(ab_engine_t *e, int track, int note, float vel)
{
    js_track_t *t;
    js_step_t *st;
    uint32_t len, idx;
    if (!e->clock.running || track < 0 || track >= AB_NTRACKS) return -1;
    t = &e->pat[e->cur_pat].t[track];
    len = t->len ? t->len : 16u;
    idx = (e->seq.pos[track] + len - 1u) % len; /* the step nearest now (the sequencer looks half a step ahead) */
    st = &t->s[idx];
    st->flags = (uint8_t)((st->flags | JS_ON) & ~JS_SLIDE);
    if (e->decay_on) st->flags = (uint8_t)((st->flags | JS_DECAY) & ~JS_BURN); /* (0.6) TAPE DECAY */
    st->vel = (uint8_t)jo_clampi((int)(vel * 127.0f + 0.5f), 1, 127);
    if (track == AB_T_BASS) st->data = (uint8_t)jo_clampi(note - e->root + 64, 40, 100);
    else { /* the nearest scale degree two octaves over the root */
        int d, bestd = 0, bestn = 999;
        for (d = 0; d < 14; d++) {
            int n = jh_degree(e->root + 24, e->scale, d), dist = n > note ? n - note : note - n;
            if (dist < bestn) bestn = dist, bestd = d;
        }
        st->data = (uint8_t)bestd;
    }
    return (int)idx;
}

/* ------------------------------------------------------------- 0.6 tools --- */
void ab_energy_set(ab_engine_t *e, float v)
{
    uint8_t b;
    e->en.knob = jo_clampf(v, 0.0f, 1.0f);
    if (!e->en.recording && !e->en.playing) e->en.value = e->en.knob;
    b = (uint8_t)(e->en.knob * 127.0f + 0.5f);
    if (b != e->en_sent && e->midi_out) { e->midi_out(jl_cc(&e->link, JL_CC_ENERGY, b)); e->en_sent = b; }
}

void ab_energy_curve(ab_engine_t *e, int mode)
{
    if (mode == AB_CURVE_REC) e->en_armed = 1, e->en.playing = 0;
    else if (mode == AB_CURVE_PLAY) e->en_armed = 0, e->en.recording = 0, e->en.playing = e->en.beats != 0;
    else e->en_armed = 0, e->en.recording = 0, e->en.playing = 0, e->en.value = e->en.knob;
}
int ab_energy_curve_mode(const ab_engine_t *e)
{
    return e->en_armed || e->en.recording ? AB_CURVE_REC : e->en.playing ? AB_CURVE_PLAY : AB_CURVE_OFF;
}

void ab_auto_levels(ab_engine_t *e, int track)
{
    js_track_t *t = &e->pat[e->cur_pat].t[(uint32_t)track % AB_NTRACKS];
    uint32_t i, len = t->len ? t->len : 16u;
    for (i = 0; i < len; i++) {
        js_step_t *st = &t->s[i];
        uint32_t p = i & 15u;
        if (!(st->flags & JS_ON)) { st->lvl = 0; continue; }
        if (p == 0u || p == 8u || ((st->flags & JS_ACCENT) && (p & 3u) == 0u)) st->lvl = 1;
        else if (st->ratch || (p & 1u)) st->lvl = 4;
        else if (st->vel < 70u) st->lvl = 3;
        else st->lvl = 2;
    }
}

void ab_set_answer(ab_engine_t *e, int on)
{
    e->duet_answer = (uint8_t)(on != 0);
    e->cap_n = 0;
    e->ans_valid = 0;
}

void ab_gesture_knob(ab_engine_t *e, int k, float v)
{
    cmd(e, C_GEST, (uint8_t)(k & 3), (uint8_t)(jo_clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f), 0, 0.0f);
}

int ab_decay_tick(ab_engine_t *e)
{
    uint32_t bar, t;
    if (!e->decay_on || !e->clock.running) return 0;
    bar = e->clock.abs_step >> 4;
    if (bar == e->decay_bar) return 0;
    e->decay_bar = bar;
    for (t = 0; t < AB_NTRACKS; t++) js_decay_pass(&e->pat[e->cur_pat].t[t], (uint8_t)(8.0f + e->decay_amt * 72.0f));
    return 1;
}
