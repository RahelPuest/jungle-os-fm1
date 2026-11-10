/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* Techniques (transferable, no proprietary algorithms or presets copied): two-operator FM with modulator
 * feedback (Chowning 1973; Yamaha's feedback operator), bandwidth-limited index via Carson's rule to keep the
 * sidebands under Nyquist, parallel band-pass formants for vowel movement, detuned polyBLEP saws for the Reese
 * (Valimaki & Huovilainen 2007), a mono sine sub kept apart from the distorted mids (the common neuro
 * production split of sub and "mid bass"). */
#include "jo_synth.h"

const char *const jn_param_name[JN_NPARAMS] = {"MODEL", "RATIO", "INDEX", "FDBK", "VOWEL", "FORMNT", "CUTOFF", "RES",
                                               "DRIVE", "MODENV", "MODDEC", "LFODIV", "LFODEP", "FOLD", "GLIDE"};

/* ---------------------------------------------------------- neuro bass --- */
/* formant pairs (F1, F2) for u o a e i, Hz: averaged adult values, rounded */
static const float VOWEL[5][2] = {{300.0f, 870.0f}, {500.0f, 1000.0f}, {750.0f, 1250.0f}, {500.0f, 1750.0f},
                                  {300.0f, 2300.0f}};
/* LFO divisions in 16ths per cycle */
static const float LFO_STEPS[8] = {16.0f, 8.0f, 4.0f, 8.0f / 3.0f, 2.0f, 4.0f / 3.0f, 1.0f, 0.5f};

void jn_bass_init(jn_bass_t *v)
{
    uint8_t *q = (uint8_t *)v;
    uint32_t i;
    for (i = 0; i < sizeof *v; i++) q[i] = 0;
    v->freq = v->target = 55.0f;
    jo_adsr_set(&v->amp, 0.002f, 0.3f, 0.85f, 0.06f);
    v->xfade = 1.0f;
}

void jn_bass_note(jn_bass_t *v, float note, float vel, int legato, const jn_patch_t *p)
{
    v->target = jo_mtof(note);
    v->glide_k = legato ? jo_tau_coef(p->p[JN_P_GLIDE] * 0.33f, JO_SR) : 1.0f;
    if (!legato) {
        v->freq = v->target;
        v->modenv = 1.0f;
        jo_adsr_gate(&v->amp, 1);
    }
    v->modenv_mul = jo_decay_mul(p->p[JN_P_MODDEC]);
    v->vel = jo_clampf(vel, 0.0f, 1.0f);
    v->gate = 1;
}

void jn_bass_off(jn_bass_t *v)
{
    v->gate = 0;
    jo_adsr_gate(&v->amp, 0);
}

static float lfo_shape(float ph, float shape, float *held, uint32_t *last, uint32_t idx)
{
    switch ((int)shape) {
    case 1: return 1.0f - 2.0f * ph * ph;                   /* exponential-ish falling saw */
    case 2: return ph < 0.5f ? 1.0f : -1.0f;                /* pulse */
    case 3: return ph < 0.25f ? ph * 4.0f : 1.0f - (ph - 0.25f) * (8.0f / 3.0f); /* asymmetric */
    case 4:                                                 /* stepped: a new random level each cycle quarter */
        if (idx != *last) {
            *last = idx;
            *held = (float)((idx * 2654435761u) >> 8 & 0xFFFFu) * (2.0f / 65535.0f) - 1.0f;
        }
        return *held;
    default: return jo_sin_turns(ph);
    }
}

void jn_bass_render(jn_bass_t *v, const jn_patch_t *p, float steps_now, float sps_inv, float *out, uint32_t n)
{
    uint32_t i;
    int model = (int)p->p[JN_P_MODEL];
    float ratio = p->p[JN_P_RATIO], index = p->p[JN_P_INDEX], fbk = jo_clampf(p->p[JN_P_FEEDBACK], 0.0f, 0.85f);
    float vowel = jo_clampf(p->p[JN_P_VOWEL], 0.0f, 1.0f) * 3.999f;
    uint32_t vi = (uint32_t)vowel;
    float vf = vowel - (float)vi;
    float lfo_steps = LFO_STEPS[jo_clampi((int32_t)p->p[JN_P_LFODIV], 0, 7)];
    float depth = p->p[JN_P_LFODEPTH] * (0.3f + 0.7f * p->motion);
    float coup = jo_clampf(p->coupling, 0.0f, 1.0f);
    float drive = p->p[JN_P_DRIVE], fold = p->p[JN_P_FOLD], fmix = p->p[JN_P_FORMANT];
    float outc = 1.0f / jo_tanh(drive * 0.6f);
    /* the per-block modulation (control rate: one block = 2.9 ms) */
    float lph = steps_now / lfo_steps;
    float lfo = lfo_shape(lph - jo_floorf(lph), p->lfo_shape, &v->lfo_held, &v->lfo_last_step,
                          (uint32_t)(steps_now * 4.0f / lfo_steps));
    float me = v->modenv * p->p[JN_P_MODENV];
    float idx_mod = index * (1.0f + depth * lfo) + 6.0f * me;
    float cut = p->p[JN_P_CUTOFF] * jo_exp2f(depth * coup * lfo * 2.0f + me * 3.0f);
    float g_lp = jo_svf_g(cut), res = p->p[JN_P_RES];
    float vw = jo_clampf(vowel + depth * coup * lfo * 1.5f, 0.0f, 3.999f);
    float g1, g2;
    (void)sps_inv;
    vi = (uint32_t)vw;
    vf = vw - (float)vi;
    g1 = jo_svf_g(jo_lerpf(VOWEL[vi][0], VOWEL[vi + 1u][0], vf));
    g2 = jo_svf_g(jo_lerpf(VOWEL[vi][1], VOWEL[vi + 1u][1], vf));
    if (model != v->last_model) { /* a model switch: crossfade from the last output over ~5 ms */
        v->last_model = (uint8_t)model;
        v->xfade = 0.0f;
    }
    if (v->amp.stage == 0 && v->xfade >= 1.0f) {
        v->modenv *= jo_exp2f((float)n * jo_log2f(v->modenv_mul + 1e-9f));
        return;
    }
    for (i = 0; i < n; i++) {
        float e = jo_adsr_tick(&v->amp), s, x;
        float f = v->freq;
        v->freq += (v->target - v->freq) * v->glide_k;
        if (model == JN_FM_CREATURE) {
            /* Carson: the sidebands reach about (index + 1) * fm; keep them under 0.42 fs */
            float fm = f * ratio;
            float lim = (0.42f * JO_SR) / (fm + 1.0f) - 1.0f;
            float ix = jo_clampf(idx_mod, 0.0f, jo_maxf(lim, 0.0f));
            float m;
            v->ph_m = jo_wrap1(v->ph_m + fm * JO_INV_SR);
            m = jo_sin_turns(v->ph_m + fbk * 0.5f * (v->fb_y1 + v->fb_y2) * 0.159f);
            v->fb_y2 = v->fb_y1;
            v->fb_y1 = m;
            v->ph_c = jo_wrap1(v->ph_c + f * JO_INV_SR);
            s = jo_sin_turns(v->ph_c + ix * m * 0.159155f);
        } else {
            /* REESE: three saws spread by RATIO's position (cents), INDEX blends an octave-down saw */
            float spread = (ratio - 0.5f) * (1.0f / 7.5f) * 0.012f + 0.002f; /* 0.2 % .. 1.4 % */
            float d0 = f * JO_INV_SR, d1 = d0 * (1.0f + spread), d2 = d0 * (1.0f - spread * 0.93f);
            v->ph_r[0] = jo_wrap1(v->ph_r[0] + d0 * 0.5f);
            v->ph_r[1] = jo_wrap1(v->ph_r[1] + d1);
            v->ph_r[2] = jo_wrap1(v->ph_r[2] + d2);
            s = 0.5f * (jo_saw_blep(v->ph_r[1], d1) + jo_saw_blep(v->ph_r[2], d2)) +
                jo_clampf(index * (1.0f / 12.0f), 0.0f, 1.0f) * 0.5f * jo_saw_blep(v->ph_r[0], d0 * 0.5f);
        }
        if (fold > 0.0f)
            s = jo_lerpf(s, jo_fold(s * (1.0f + 4.0f * fold)), fold);
        /* formants in parallel with the dry signal */
        if (fmix > 0.0f) {
            jo_svf_out_t a = jo_svf(&v->f1, s, g1, 0.75f), b = jo_svf(&v->f2, s, g2, 0.75f);
            s = jo_lerpf(s, (a.bp + 0.7f * b.bp) * 1.6f, fmix);
        }
        x = jo_svf(&v->lp, s, g_lp, res).lp;
        x = jo_tanh(x * drive * 0.6f) * outc * 0.5f;
        x = jo_dcb(&v->dc, x) * e * (0.4f + 0.6f * v->vel);
        if (v->xfade < 1.0f) {
            x = jo_lerpf(v->prev_out, x, v->xfade);
            v->xfade += 1.0f / 220.0f;
        } else
            v->prev_out = x;
        out[i] += jo_sane(x);
        v->modenv *= v->modenv_mul;
    }
}

/* ----------------------------------------------------------------- sub --- */
void jn_sub_init(jn_sub_t *s)
{
    uint8_t *q = (uint8_t *)s;
    uint32_t i;
    for (i = 0; i < sizeof *s; i++) q[i] = 0;
    s->freq = s->target = 41.2f;
    s->att_k = jo_tau_coef(0.002f, JO_SR);  /* 2 ms: no click at the onset */
    s->rel_k = jo_tau_coef(0.012f, JO_SR);
    s->octave = -1;
    s->sat = 0.15f;
}

void jn_sub_note(jn_sub_t *s, float note, float glide_s, int legato)
{
    float f = jo_mtof(note + 12.0f * (float)s->octave);
    /* the sub folds into one octave, 36..72 Hz: below, a PA wastes headroom on what it cannot play; above, it
     * would double the mid bass */
    while (f < 36.0f) f *= 2.0f;
    while (f >= 72.0f) f *= 0.5f;
    s->target = f;
    s->glide_k = legato ? jo_tau_coef(jo_maxf(glide_s * 0.33f, 0.0005f), JO_SR) : 1.0f;
    if (!legato) s->freq = s->target; /* the phase runs on: no restart click */
    s->gate = 1;
}

void jn_sub_off(jn_sub_t *s) { s->gate = 0; }

void jn_sub_render(jn_sub_t *s, float *out, uint32_t n)
{
    uint32_t i;
    float drive = 1.0f + 2.0f * s->sat, comp = 1.0f / jo_tanh(drive);
    if (!s->gate && s->env < 1e-5f) {
        s->env = 0.0f;
        return;
    }
    for (i = 0; i < n; i++) {
        float x, t;
        s->freq += (s->target - s->freq) * s->glide_k;
        s->ph = jo_wrap1(s->ph + s->freq * JO_INV_SR);
        s->env += ((s->gate ? 1.0f : 0.0f) - s->env) * (s->gate ? s->att_k : s->rel_k);
        x = jo_sin_turns(s->ph);
        t = 4.0f * jo_absf(s->ph - 0.5f) - 1.0f; /* triangle, a touch of odd harmonics for small speakers */
        x = jo_lerpf(x, -t, s->tri * 0.5f);
        out[i] += jo_tanh(x * drive) * comp * s->env * 0.5f;
    }
}

/* --------------------------------------------------------------- stabs --- */
const char *const jn_stab_name[JN_STAB_NMODELS] = {"RAVE CHORD", "ORCH HIT", "GLASS FM", "DARK CHORD"};

void jn_stab_init(jn_stab_t *s)
{
    uint8_t *q = (uint8_t *)s;
    uint32_t i;
    for (i = 0; i < sizeof *s; i++) q[i] = 0;
    jo_rng_seed(&s->rng, 77);
}

void jn_stab_chord(jn_stab_t *s, const jn_stab_patch_t *p, const float *notes, uint32_t nn, float vel)
{
    uint32_t k, j;
    float att = 0.001f + 0.08f * p->attack * p->attack, dec = 0.08f + 0.9f * p->tail * p->tail;
    for (k = 0; k < JN_STAB_VOICES; k++) {
        jn_stab_voice_t *v = &s->v[k];
        if (k >= nn) {
            jo_adsr_gate(&v->amp, 0);
            continue;
        }
        {
            float f = jo_mtof(notes[k]);
            float det = p->model == JN_STAB_DARK ? 0.012f : 0.006f;
            v->inc[0] = f * JO_INV_SR;
            v->inc[1] = f * (1.0f + det) * JO_INV_SR;
            v->inc[2] = p->model == JN_STAB_GLASS ? f * 3.5f * JO_INV_SR : f * (1.0f - det) * JO_INV_SR;
            for (j = 0; j < 3u; j++) v->ph[j] = jo_rng_unit(&s->rng); /* free-running feel, no phasey stack */
        }
        jo_adsr_set(&v->amp, att, dec, p->model == JN_STAB_ORCH ? 0.25f : 0.0f, 0.06f + 0.5f * p->tail);
        v->amp.level = 0.0f;
        jo_adsr_gate(&v->amp, 1);
        v->fenv = 1.0f;
        v->fenv_mul = jo_decay_mul(0.05f + 0.4f * p->tail);
        v->pitchenv = p->model == JN_STAB_ORCH ? 0.6f : 0.0f; /* the brass "blip" up into the note */
        v->pitchenv_mul = jo_decay_mul(0.03f);
        v->vel = vel;
        v->active = 1;
        v->f.s1 = v->f.s2 = 0.0f;
    }
}

void jn_stab_release(jn_stab_t *s)
{
    uint32_t k;
    for (k = 0; k < JN_STAB_VOICES; k++) jo_adsr_gate(&s->v[k].amp, 0);
}

void jn_stab_render(jn_stab_t *s, const jn_stab_patch_t *p, float *l, float *r, uint32_t n)
{
    uint32_t k, i;
    for (k = 0; k < JN_STAB_VOICES; k++) {
        jn_stab_voice_t *v = &s->v[k];
        float pan = (float)k * 0.33f - 0.5f, gl = 0.5f - 0.4f * pan, gr = 0.5f + 0.4f * pan;
        float base_cut = 300.0f * jo_exp2f(p->bite * 5.0f);
        if (!v->active) continue;
        float g;
        if (v->amp.stage == 0) { v->active = 0; continue; }
        g = jo_svf_g(base_cut * (1.0f + 6.0f * v->fenv)); /* the filter envelope at control rate (a block) */
        for (i = 0; i < n; i++) {
            float e = jo_adsr_tick(&v->amp), x, pm = 1.0f + v->pitchenv * 0.06f;
            v->ph[0] = jo_wrap1(v->ph[0] + v->inc[0] * pm);
            v->ph[1] = jo_wrap1(v->ph[1] + v->inc[1] * pm);
            v->ph[2] = jo_wrap1(v->ph[2] + v->inc[2] * pm);
            switch (p->model) {
            case JN_STAB_GLASS: /* a bell: sine carrier, inharmonic modulator, index decays with the envelope */
                x = jo_sin_turns(v->ph[0] + (1.0f + 4.0f * p->bite) * v->fenv * 0.16f * jo_sin_turns(v->ph[2]));
                x += 0.3f * jo_sin_turns(v->ph[1] * 2.0f);
                break;
            case JN_STAB_ORCH: /* saw + pulse, brassy */
                x = 0.6f * jo_saw_blep(v->ph[0], v->inc[0]) + 0.5f * (v->ph[1] < 0.4f ? 1.0f : -1.0f);
                break;
            default: /* rave / dark: detuned saws */
                x = 0.5f * (jo_saw_blep(v->ph[0], v->inc[0]) + jo_saw_blep(v->ph[1], v->inc[1]) +
                            jo_saw_blep(v->ph[2], v->inc[2])) * 0.67f;
                break;
            }
            if (p->model != JN_STAB_GLASS)
                x = jo_svf(&v->f, x, g, p->model == JN_STAB_DARK ? 0.6f : 0.3f).lp;
            x = jo_tanh(x * (1.0f + 2.0f * p->bite)) * e * v->vel * 0.35f;
            l[i] += x * gl;
            r[i] += x * gr;
            v->fenv *= v->fenv_mul;
            v->pitchenv *= v->pitchenv_mul;
        }
    }
}

/* ----------------------------------------------------------------- pad --- */
void jn_pad_init(jn_pad_t *p)
{
    uint8_t *q = (uint8_t *)p;
    uint32_t i;
    for (i = 0; i < sizeof *p; i++) q[i] = 0;
}

void jn_pad_chord(jn_pad_t *p, const jn_pad_patch_t *pp, const float *notes, uint32_t nn)
{
    uint32_t k, j;
    uint8_t keep[JN_PAD_VOICES] = {0}, used[JN_PAD_VOICES] = {0};
    /* voices already on a wanted note keep sounding */
    for (j = 0; j < nn && j < JN_PAD_VOICES; j++)
        for (k = 0; k < JN_PAD_VOICES; k++)
            if (!keep[k] && p->v[k].active && p->v[k].amp.stage && p->v[k].amp.stage != 3 &&
                jo_absf(p->v[k].note - notes[j]) < 0.01f) {
                keep[k] = 1;
                used[j] = 1;
                break;
            }
    for (k = 0; k < JN_PAD_VOICES; k++)
        if (!keep[k]) jo_adsr_gate(&p->v[k].amp, 0);
    for (j = 0; j < nn && j < JN_PAD_VOICES; j++) {
        if (used[j]) continue;
        for (k = 0; k < JN_PAD_VOICES; k++) { /* a free (or releasing) voice */
            if (keep[k]) continue;
            keep[k] = 1;
            {
                jn_pad_voice_t *v = &p->v[k];
                v->note = notes[j];
                v->base_inc = jo_mtof(notes[j]) * JO_INV_SR;
                v->pan = (float)k * 0.66f - 1.0f;
                jo_adsr_set(&v->amp, pp->attack_s, 1.0f, 1.0f, pp->release_s);
                jo_adsr_gate(&v->amp, 1);
                v->active = 1;
            }
            break;
        }
    }
}

void jn_pad_release(jn_pad_t *p)
{
    uint32_t k;
    for (k = 0; k < JN_PAD_VOICES; k++) jo_adsr_gate(&p->v[k].amp, 0);
}

void jn_pad_render(jn_pad_t *p, const jn_pad_patch_t *pp, float *l, float *r, uint32_t n)
{
    uint32_t k, i;
    float lfo_rate = 0.03f + 0.4f * pp->evolve * pp->evolve; /* Hz */
    float l1, l2, cut;
    p->lfo1 = jo_wrap1(p->lfo1 + lfo_rate * (float)n * JO_INV_SR);
    p->lfo2 = jo_wrap1(p->lfo2 + lfo_rate * 0.618f * (float)n * JO_INV_SR);
    l1 = jo_sin_turns(p->lfo1);
    l2 = jo_sin_turns(p->lfo2);
    cut = 3500.0f * jo_exp2f(-4.5f * pp->darkness + pp->evolve * 1.2f * l1);
    for (k = 0; k < JN_PAD_VOICES; k++) {
        jn_pad_voice_t *v = &p->v[k];
        float g = jo_svf_g(cut * (1.0f + 0.1f * (float)k));
        /* TENSION: beating detune and a slightly inharmonic second oscillator */
        float det = 0.002f + 0.006f * pp->tension + 0.002f * pp->evolve * l2;
        float ratio2 = 1.0f + det + 0.03f * pp->tension * pp->tension;
        float gl = 0.5f - 0.35f * v->pan, gr = 0.5f + 0.35f * v->pan;
        if (!v->active) continue;
        if (v->amp.stage == 0) { v->active = 0; continue; }
        for (i = 0; i < n; i++) {
            float e = jo_adsr_tick(&v->amp), x;
            float d0 = v->base_inc, d1 = v->base_inc * ratio2;
            v->ph[0] = jo_wrap1(v->ph[0] + d0);
            v->ph[1] = jo_wrap1(v->ph[1] + d1);
            x = 0.5f * (jo_saw_blep(v->ph[0], d0) + jo_saw_blep(v->ph[1], d1));
            x = jo_svf(&v->f, x, g, 0.2f).lp * e * 0.22f;
            l[i] += x * gl;
            r[i] += x * gr;
        }
    }
}

/* --------------------------------------------------------------- harmony --- */
const char *const jh_scale_name[JH_NSCALES] = {"MINOR", "HARM MIN", "PHRYGIAN", "DORIAN", "CHROMA"};
const char *const jh_chord_name[JH_NCHORDS] = {"MIN", "MIN7", "SUS2", "MIN9", "MAJ7", "DIM", "CLUSTER", "FIFTH"};
static const int8_t SCALE[JH_NSCALES][7] = {
    {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 1, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 2, 3, 4, 5, 6}};

int jh_degree(int root, int scale, int deg)
{
    int oct, d;
    if (scale < 0 || scale >= JH_NSCALES) scale = 0;
    if (scale == JH_CHROMATIC) return root + deg;
    oct = deg >= 0 ? deg / 7 : -((-deg + 6) / 7);
    d = deg - oct * 7;
    return root + 12 * oct + SCALE[scale][d];
}

int jh_snap(int root, int scale, int note)
{
    int best = note, bd = 99, k, o;
    if (scale == JH_CHROMATIC) return note;
    for (o = -1; o <= 1; o++)
        for (k = 0; k < 7; k++) {
            int c = root + 12 * ((note - root) / 12 + o) + SCALE[scale < 0 || scale >= JH_NSCALES ? 0 : scale][k];
            int d = c > note ? c - note : note - c;
            if (d < bd || (d == bd && c < best)) bd = d, best = c;
        }
    return best;
}

uint32_t jh_chord(int note, int chord, float tension, int inversion, float *out, uint32_t max)
{
    static const int8_t CH[JH_NCHORDS][4] = {{0, 3, 7, -1}, {0, 3, 7, 10}, {0, 2, 7, -1}, {0, 3, 10, 14},
                                             {0, 4, 7, 11}, {0, 3, 6, -1}, {0, 1, 3, 7}, {0, 7, 12, -1}};
    uint32_t n = 0, k;
    int notes[5];
    if (chord < 0 || chord >= JH_NCHORDS) chord = 0;
    for (k = 0; k < 4u; k++)
        if (CH[chord][k] >= 0) notes[n++] = note + CH[chord][k];
    if (tension > 0.66f && n < 5u) notes[n++] = note + 13;      /* b9: the dark one */
    else if (tension > 0.33f && n < 5u) notes[n++] = note + 14; /* 9 */
    for (k = 0; k < (uint32_t)inversion % 4u && n; k++) {      /* inversion: the lowest goes up an octave */
        uint32_t lo = 0, j;
        for (j = 1; j < n; j++) if (notes[j] < notes[lo]) lo = j;
        notes[lo] += 12;
    }
    if (n > max) n = max;
    for (k = 0; k < n; k++) out[k] = (float)notes[k];
    return n;
}
