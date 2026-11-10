/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
#include "jo_fx.h"

/* ------------------------------------------------------------- compressor --- */
void jf_comp_set(jf_comp_t *c, float thresh_db, float ratio, float attack_s, float release_s, float makeup_db)
{
    c->thresh_db = thresh_db;
    c->ratio = jo_maxf(ratio, 1.0f);
    c->knee_db = 6.0f;
    c->ka = jo_tau_coef(attack_s, JO_SR);
    c->kr = jo_tau_coef(release_s, JO_SR);
    c->makeup_db = makeup_db;
}

float jf_comp_gain(jf_comp_t *c, float x)
{
    float a = jo_absf(x), over, gr;
    c->env += (a - c->env) * (a > c->env ? c->ka : c->kr);
    c->env = jo_flush(c->env);
    over = jo_lin2db(c->env + 1e-9f) - c->thresh_db;
    /* soft knee */
    if (over <= -0.5f * c->knee_db)
        gr = 0.0f;
    else if (over >= 0.5f * c->knee_db)
        gr = over * (1.0f / c->ratio - 1.0f);
    else {
        float t = over + 0.5f * c->knee_db;
        gr = (1.0f / c->ratio - 1.0f) * t * t / (2.0f * c->knee_db);
    }
    c->gr_db = gr;
    return jo_db2lin(gr + c->makeup_db);
}

/* ------------------------------------------------------------- peak guard --- */
void jf_guard_init(jf_guard_t *g, float ceiling_db)
{
    g->g = 1.0f;
    g->kr = jo_tau_coef(0.08f, JO_SR);
    g->ceiling = jo_db2lin(ceiling_db);
    g->hits = 0;
}

float jf_guard(jf_guard_t *g, float x)
{
    float a = jo_absf(x) * g->g;
    if (a > g->ceiling) { /* instant attack: the gain that puts this sample at the ceiling */
        g->g = g->ceiling / (jo_absf(x) + 1e-12f);
        g->hits++;
    } else
        g->g += (1.0f - g->g) * g->kr;
    return jo_clampf(x * g->g, -g->ceiling, g->ceiling);
}

void jf_guard2(jf_guard_t *g, float *l, float *r)
{
    float a = jo_maxf(jo_absf(*l), jo_absf(*r));
    if (a * g->g > g->ceiling) {
        g->g = g->ceiling / (a + 1e-12f);
        g->hits++;
    } else
        g->g += (1.0f - g->g) * g->kr;
    *l = jo_clampf(*l * g->g, -g->ceiling, g->ceiling);
    *r = jo_clampf(*r * g->g, -g->ceiling, g->ceiling);
}

/* -------------------------------------------------------------- drum bus --- */
/* the grit's low-pass: 18 kHz (none) down to 10 kHz */
static float xfreq_lerp(float g) { return 18000.0f * jo_exp2f(-0.85f * g); }

const char *const jf_drum_preset_name[JF_DRUM_NPRESETS] = {"CLEAN JUNGLE", "ROLLING BREAKS", "TIGHT NEURO",
                                                           "CRUSHED AMEN", "INDUSTRIAL"};

void jf_drum_preset(jf_drum_macros_t *m, int preset)
{
    /* PUNCH, CRUSH (below 0.4: sampler grit; above: the parallel crush), GLUE, PRESSURE, ROOM */
    static const float P[JF_DRUM_NPRESETS][5] = {
        {0.4f, 0.2f, 0.35f, 0.3f, 0.35f},   /* clean jungle: open, a sampler's grit, a room */
        {0.45f, 0.3f, 0.6f, 0.6f, 0.3f},   /* rolling breaks: glued, gritty, a room */
        {0.75f, 0.1f, 0.55f, 0.65f, 0.15f},  /* tight neuro: hard transients, dense, dry */
        {0.5f, 0.75f, 0.55f, 0.65f, 0.25f}, /* crushed: the parallel crush up */
        {0.6f, 0.55f, 0.75f, 0.9f, 0.4f},   /* industrial: everything pushed, a bigger room */
    };
    int p = preset < 0 || preset >= JF_DRUM_NPRESETS ? 0 : preset;
    m->punch = P[p][0];
    m->crush = P[p][1];
    m->glue = P[p][2];
    m->pressure = P[p][3];
    m->room = P[p][4];
    m->trim_db = 0.0f;
}

void jf_drum_init(jf_drum_bus_t *b)
{
    uint8_t *q = (uint8_t *)b;
    uint32_t i;
    for (i = 0; i < sizeof *b; i++) q[i] = 0;
    jf_drum_preset(&b->m, JF_DRUM_ROLLING);
    jf_guard_init(&b->guard, -0.5f);
    jf_drum_update(b);
}

void jf_drum_room_buf(jf_drum_bus_t *b, int16_t *buf)
{
    b->has_room = buf != 0;
    if (buf) {
        jf_verb_init(&b->room, buf);
        jf_verb_set(&b->room, 0.22f, 0.42f, 6500.0f); /* a small, bright-ish room: ~12-24 ms lines */
    }
}

void jf_drum_update(jf_drum_bus_t *b)
{
    const jf_drum_macros_t *m = &b->m;
    float pr = jo_clampf(m->pressure, 0.0f, 1.0f), gl = jo_clampf(m->glue, 0.0f, 1.0f);
    b->tr_amt = (jo_clampf(m->punch, 0.0f, 1.0f) - 0.3f) * 2.0f; /* -0.6 (softer) .. +1.4 (harder attacks) */
    {
        float c = jo_clampf(m->crush, 0.0f, 1.0f), gr = jo_clampf(c * 2.5f, 0.0f, 1.0f), up = jo_clampf((c - 0.4f) / 0.6f, 0.0f, 1.0f);
        /* the lower half: a sampler's grit in series (12 -> 10 bits, the rate held to 44 -> 26 kHz, the top darker) */
        b->grit = gr;
        b->grit_bits = 2048.0f * jo_exp2f(-2.0f * gr);
        b->grit_hold = 1.0f + 0.7f * gr;
        b->grit_k = jo_lp1_k(xfreq_lerp(gr));
        /* the upper half: the parallel crush (12 .. 4 bits, a held rate) */
        b->crush_mix = up * 0.6f;
        b->crush_bits = 4096.0f * jo_exp2f(-8.0f * up);
        b->crush_hold = 1.0f + 5.0f * up;
    }
    b->room_gain = 3.0f * jo_clampf(m->room, 0.0f, 1.0f);
    /* PRESSURE: saturation drive, compressor threshold and the compensating trim move together */
    /* (0.2) PRESSURE drives the saturation into its knee (the peaks rounded off, the body brought up); GLUE is a
     * fast compressor (0.8-4 ms attack: it catches the hits, so the kit gets denser, not peakier) with its makeup */
    b->sat_drive = 1.0f + 5.0f * pr;
    b->sat_comp = 1.0f / jo_tanh(b->sat_drive * 0.5f) * 0.5f;                      /* keeps a -6 dB peak level */
    jf_comp_set(&b->comp, -10.0f - 14.0f * gl, 1.5f + 4.5f * gl, 0.004f - 0.0032f * gl, 0.015f + 0.015f * gl, 0.0f);
    /* the makeup: what a -6 dBFS hit loses, given back (the bodies keep their level, the peaks run into the clip) */
    b->comp.makeup_db = 0.9f * jo_maxf(-6.0f - b->comp.thresh_db, 0.0f) * (1.0f - 1.0f / b->comp.ratio);
    b->out_gain = jo_db2lin(m->trim_db - 1.0f * pr); /* the denser it gets, the less level: loudness held */
    b->fast.env = b->fast.env;
}

void jf_drum_process(jf_drum_bus_t *b, float *x, const float *send, uint32_t n)
{
    uint32_t i;
    const float kfa = 0.35f, kfr = 0.0035f, ksa = 0.006f, ksr = 0.0018f; /* ~0.1/6 ms and ~4/12 ms */
    float pk = b->peak;
    if (send && b->has_room && b->room_gain > 0.0f) { /* the room, mono, in before the bus (it is glued with the kit) */
        float rl[64], rr[64];
        uint32_t k, j;
        for (k = 0; k < n; k += 64u) {
            uint32_t m = n - k < 64u ? n - k : 64u;
            for (j = 0; j < m; j++) rl[j] = rr[j] = 0.0f;
            jf_verb_process(&b->room, send + k, rl, rr, m);
            for (j = 0; j < m; j++) x[k + j] += (rl[j] + rr[j]) * 0.5f * b->room_gain;
        }
    }
    for (i = 0; i < n; i++) {
        float s = x[i], ef, es, tr, c, y;
        /* the sampler's grit: held rate, fewer bits, a darker top */
        if (b->grit > 0.0f) {
            b->grit_ph += 1.0f;
            if (b->grit_ph >= b->grit_hold) {
                b->grit_ph -= b->grit_hold;
                b->grit_held = jo_floorf(s * b->grit_bits + 0.5f) / b->grit_bits;
            }
            b->grit_lp += (b->grit_held - b->grit_lp) * b->grit_k;
            b->grit_lp = jo_flush(b->grit_lp);
            s = s + (b->grit_lp - s) * b->grit;
        }
        /* transient contour: fast minus slow envelope = the attack; scale it */
        ef = jo_follow(&b->fast, s, kfa, kfr);
        es = jo_follow(&b->slow, s, ksa, ksr);
        tr = (ef - es) / (ef + 1e-4f);              /* 0..1 during an attack */
        s *= 1.0f + b->tr_amt * jo_clampf(tr, 0.0f, 1.0f);
        /* parallel crush: sample-hold + bit reduction, blended */
        if (b->crush_mix > 0.0f) {
            b->crush_ph += 1.0f;
            if (b->crush_ph >= b->crush_hold) {
                b->crush_ph -= b->crush_hold;
                b->crush_held = jo_floorf(s * b->crush_bits + 0.5f) / b->crush_bits;
            }
            s += (jo_tanh(b->crush_held * 2.0f) * 0.5f - s * 0.5f) * b->crush_mix;
        }
        /* saturation */
        s = jo_tanh(s * b->sat_drive * 0.5f) * b->sat_comp * 2.0f;
        /* glue */
        c = jf_comp_gain(&b->comp, s);
        s *= c;
        s = jo_dcb(&b->dc, s);
        /* soft clip, then the peak guard */
        y = jo_softclip(s * b->out_gain * 1.2f) * (1.0f / 1.2f);
        y = jf_guard(&b->guard, y);
        if (jo_absf(y) > pk) pk = jo_absf(y);
        x[i] = jo_sane(y);
    }
    b->peak = pk;
}

/* --------------------------------------------------------------- ducking --- */
void jf_duck_set(jf_duck_t *d, float depth, float attack_s, float hold_s, float release_s)
{
    d->depth = jo_clampf(depth, 0.0f, 1.0f);
    d->ka = jo_tau_coef(jo_maxf(attack_s, 0.0005f), JO_SR);
    d->hold_s = jo_maxf(hold_s, 0.0f);
    d->kr = jo_tau_coef(jo_maxf(release_s, 0.005f), JO_SR);
}
void jf_duck_trigger(jf_duck_t *d, float vel)
{
    d->stage = 1;
    d->hold_left = (uint32_t)(d->hold_s * JO_SR);
    (void)vel;
}
float jf_duck_tick(jf_duck_t *d)
{
    switch (d->stage) {
    case 1:
        d->env += (1.0f - d->env) * d->ka;
        if (d->env > 0.98f) d->stage = 2;
        break;
    case 2:
        if (d->hold_left) d->hold_left--; else d->stage = 3;
        break;
    case 3:
        d->env -= d->env * d->kr;
        if (d->env < 1e-4f) { d->env = 0.0f; d->stage = 0; }
        break;
    default: break;
    }
    return 1.0f - d->depth * d->env;
}

/* ---------------------------------------------------------------- delay --- */
static inline int16_t to16(float x) { return (int16_t)(jo_clampf(x, -1.0f, 1.0f) * 32767.0f); }
static inline float from16(int16_t x) { return (float)x * (1.0f / 32768.0f); }

void jf_delay_init(jf_delay_t *d, int16_t *bl, int16_t *br)
{
    uint32_t i;
    d->bl = bl;
    d->br = br;
    d->w = 0;
    for (i = 0; i < JF_DELAY_LEN; i++) bl[i] = br[i] = 0;
    d->lpl.z = d->lpr.z = 0.0f;
    jf_delay_set(d, 0.26f, 0.4f, 4000.0f);
    d->mix = 1.0f;
}
void jf_delay_set(jf_delay_t *d, float seconds, float feedback, float tone_hz)
{
    d->time_smp = jo_clampf(seconds * JO_SR, 32.0f, (float)(JF_DELAY_LEN - 2u));
    d->fb = jo_clampf(feedback, 0.0f, 0.85f); /* bounded: never self-oscillates */
    d->tone_k = jo_lp1_k(tone_hz);
}
void jf_delay_process(jf_delay_t *d, const float *in, float *l, float *r, uint32_t n)
{
    uint32_t i, m = JF_DELAY_LEN - 1u, t = (uint32_t)d->time_smp;
    for (i = 0; i < n; i++) {
        float yl = from16(d->bl[(d->w - t) & m]), yr = from16(d->br[(d->w - t) & m]);
        /* ping-pong: the input enters the left line, each line feeds the other */
        d->bl[d->w] = to16(jo_lp1(&d->lpl, in[i] + yr * d->fb, d->tone_k));
        d->br[d->w] = to16(jo_lp1(&d->lpr, yl * d->fb, d->tone_k));
        d->w = (d->w + 1u) & m;
        l[i] += yl * d->mix;
        r[i] += yr * d->mix;
    }
}

/* --------------------------------------------------------------- reverb --- */
void jf_verb_init(jf_verb_t *v, int16_t *buf)
{
    uint32_t i;
    v->buf = buf;
    for (i = 0; i < JF_VERB_TOTAL; i++) buf[i] = 0;
    for (i = 0; i < 4u; i++) v->lp[i] = 0.0f, v->w[i] = 0;
    v->hp.z = 0.0f;
    jf_verb_set(v, 0.7f, 0.6f, 5000.0f);
}
void jf_verb_set(jf_verb_t *v, float size, float decay, float damp_hz)
{
    /* mutually prime-ish lengths, scaled by size; the four fit JF_VERB_TOTAL at size 1 */
    static const uint32_t L[4] = {1601u, 2053u, 2687u, 3319u};
    uint32_t i, off = 0;
    float s = jo_clampf(size, 0.2f, 1.0f) * 1.6f;
    for (i = 0; i < 4u; i++) {
        v->len[i] = (uint32_t)((float)L[i] * s);
        v->off[i] = off;
        off += (uint32_t)((float)L[i] * 1.6f);
        if (v->w[i] >= v->len[i]) v->w[i] = 0;
    }
    v->fb = jo_clampf(decay, 0.0f, 0.95f) * 0.5f; /* the Householder matrix's scale: 0.5 is lossless */
    v->damp_k = jo_lp1_k(damp_hz);
}
void jf_verb_process(jf_verb_t *v, const float *in, float *l, float *r, uint32_t n)
{
    uint32_t i, k;
    for (i = 0; i < n; i++) {
        float y[4], sum = 0.0f, x = jo_hp1(&v->hp, in[i], 0.02f);
        for (k = 0; k < 4u; k++) {
            y[k] = from16(v->buf[v->off[k] + v->w[k]]);
            v->lp[k] += (y[k] - v->lp[k]) * v->damp_k;
            v->lp[k] = jo_flush(v->lp[k]);
            sum += v->lp[k];
        }
        /* Householder feedback: y_k - 0.5 sum, scaled */
        for (k = 0; k < 4u; k++) {
            float fbk = (v->lp[k] - 0.5f * sum) * v->fb * 2.0f;
            v->buf[v->off[k] + v->w[k]] = to16(jo_tanh(x * 0.5f + fbk));
            if (++v->w[k] >= v->len[k]) v->w[k] = 0;
        }
        l[i] += (y[0] + y[2]) * 0.6f;
        r[i] += (y[1] + y[3]) * 0.6f;
    }
}

/* ---------------------------------------------------------- neuro master --- */
void jf_neuro_init(jf_neuro_t *b)
{
    uint8_t *q = (uint8_t *)b;
    uint32_t i;
    for (i = 0; i < sizeof *b; i++) q[i] = 0;
    b->m.weight = 0.5f;
    b->m.dirt = 0.3f;
    b->m.width = 0.4f;
    b->m.pressure = 0.4f;
    jf_guard_init(&b->guard, -0.5f);
    jf_neuro_update(b);
}

void jf_neuro_update(jf_neuro_t *b)
{
    float pr = jo_clampf(b->m.pressure, 0.0f, 1.0f);
    b->sub_gain = jo_db2lin(-6.0f + 9.0f * jo_clampf(b->m.weight, 0.0f, 1.0f)); /* -6 .. +3 dB */
    b->dirt_drive = 1.0f + 6.0f * b->m.dirt * b->m.dirt;
    b->dirt_comp = 1.0f / jo_tanh(b->dirt_drive * 0.5f) * 0.5f;
    b->width = jo_clampf(b->m.width, 0.0f, 1.0f);
    jf_comp_set(&b->comp, -8.0f - 10.0f * pr, 1.5f + 2.5f * pr, 0.012f, 0.15f, 0.0f);
    b->comp.makeup_db = 0.3f * 10.0f * pr * (1.0f - 1.0f / b->comp.ratio);
}

void jf_neuro_process(jf_neuro_t *b, const float *sub, const float *bass, float *l, float *r, const float *duck,
                      uint32_t n)
{
    uint32_t i;
    const float k_xo = 0.0164f;  /* ~115 Hz one-pole: the bass path's low cut */
    const float k_side = 0.0201f; /* ~140 Hz: no width below */
    float pk = b->peak;
    for (i = 0; i < n; i++) {
        float d = duck ? duck[i] : 1.0f;
        float s = sub[i] * b->sub_gain * d;
        float bs = jo_hp1(&b->xo_lo, bass[i], k_xo);
        float mid, side, hl, hr, g, outl, outr;
        bs = jo_tanh(bs * b->dirt_drive * 0.5f) * b->dirt_comp * 2.0f * d;
        /* width: the side signal's highs only (pads, stabs, returns); sub and bass stay centred (mono-safe) */
        hl = jo_hp1(&b->side_hp_l, l[i], k_side);
        hr = jo_hp1(&b->side_hp_r, r[i], k_side);
        mid = 0.5f * (l[i] + r[i]);
        side = 0.5f * (hl - hr) * (0.5f + 1.5f * b->width);
        outl = s + bs + mid + side;
        outr = s + bs + mid - side;
        g = jf_comp_gain(&b->comp, 0.5f * (outl + outr));
        outl = jo_dcb(&b->dcl, outl * g);
        outr = jo_dcb(&b->dcr, outr * g);
        outl = jo_softclip(outl * 1.1f) * (1.0f / 1.1f);
        outr = jo_softclip(outr * 1.1f) * (1.0f / 1.1f);
        jf_guard2(&b->guard, &outl, &outr);
        if (jo_absf(outl) > pk) pk = jo_absf(outl);
        if (jo_absf(outr) > pk) pk = jo_absf(outr);
        l[i] = jo_sane(outl);
        r[i] = jo_sane(outr);
    }
    b->peak = pk;
}
