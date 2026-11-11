/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
#include "jo_break.h"
#include "../drums/jo_drums.h"

#define FADE_IN 48.0f   /* output samples, ~1.1 ms */
#define FADE_OUT 96.0f  /* ~2.2 ms: the old voice of a crossfade, a slice's end, a release */

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

uint32_t jb_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

int jb_bank_parse(const uint8_t *img, uint32_t len, jb_break_t *out, int max)
{
    uint32_t count, total, i, k;
    if (!img || len < JB_HDR || rd32(img) != JB_MAGIC)
        return -1;
    if (rd16(img + 4) != JB_VERSION)
        return -2;
    count = rd16(img + 6);
    total = rd32(img + 8);
    if (total > len || total < JB_HDR + count * JB_ENTRY || count > JB_MAXBREAKS)
        return -3;
    if (jb_crc32(img + JB_HDR, total - JB_HDR) != rd32(img + 12))
        return -4;
    for (i = 0; i < count && (int)i < max; i++) {
        const uint8_t *e = img + JB_HDR + i * JB_ENTRY;
        jb_break_t *b = &out[i];
        uint32_t off = rd32(e + 16), frames = rd32(e + 20), rate = rd32(e + 24);
        if (off & 3u || off < JB_HDR + count * JB_ENTRY || frames == 0 || frames > (total - off) / 2u ||
            rate < 8000u || rate > 48000u)
            return -5;
        for (k = 0; k < 16u; k++)
            b->name[k] = (char)e[k];
        b->name[16] = 0;
        b->pcm = (const int16_t *)(const void *)(img + off);
        b->frames = frames;
        b->rate = rate;
        b->bpm = (float)rd16(e + 28) * 0.01f;
        b->bars = e[30];
        b->nslices = e[31];
        if (b->nslices == 0 || b->nslices > JB_MAXSLICES || b->bpm < 40.0f || b->bpm > 300.0f)
            return -5;
        for (k = 0; k < b->nslices; k++) {
            b->start[k] = rd32(e + 36 + 4 * k);
            b->cls[k] = e[164 + k];
            b->gain[k] = e[196 + k];
            if (b->start[k] >= frames || (k && b->start[k] <= b->start[k - 1]))
                return -5;
        }
        b->start[b->nslices] = frames;
    }
    return (int)i;
}

void jb_slice_even(jb_break_t *b, uint32_t n)
{
    uint32_t k;
    if (n < 1u) n = 1u;
    if (n > JB_MAXSLICES) n = JB_MAXSLICES;
    b->nslices = (uint8_t)n;
    for (k = 0; k <= n; k++)
        b->start[k] = (uint32_t)((float)b->frames * (float)k / (float)n);
    for (k = 0; k < n; k++)
        b->cls[k] = JB_CLASS_NONE, b->gain[k] = 128;
}

/* ----------------------------------------------------------- test breaks --- */
/* (0.7) the built-in breaks: one bar of 16ths at 170 BPM each, synthesized from JUNGLE OS's own drum models in the
 * rhythm of a classic break as drum transcriptions write it (docs/research/dnb-drums.md). The rhythms only: no
 * sound of any recording is used (the recordings are copyrighted; a rhythm is not). Each line, per 16th:
 *   kick  K full, k soft          snare S full, s lighter (+4 ms), g ghost (+8 ms, soft)
 *   tops  h closed hat, H accented, o open hat (a closed one chokes it), r ride, R ride bell, x shaker
 *   perc  p, P (accented): a conga / bongo voice
 * played as a drummer would (every hit's velocity varied a little, the odd 16ths swung by `swing` of a step), then
 * a small room (early reflections that feed back) and a sampler's 12 bits at 22 kHz. */
#define TB_BPM 170.0f
uint32_t jb_testbreak_frames(uint32_t rate) { return (uint32_t)((float)rate * 60.0f / TB_BPM * 4.0f); }

typedef struct {
    const char *name, *k, *s, *h, *p;
    float swing, ktune, stune, hmetal;
} tb_def_t;
static const tb_def_t TB[JB_NTESTBREAKS] = {
    /* the Amen: kicks on 1, the and of 1, the and of 3 and right after; snares on 2 and 4, lighter ones between; a
     * ride on 8ths, its quarters on the bell */
    {"AMEN 170", "K.K.......KK....", "....S..s.s..S..s", "R.r.R.r.R.r.R.r.", "", 0.0f, 1.5f, 2.0f, 0.9f},
    /* Think: the kick pushed onto the and of 2, busy 16th hats, an open hat on the and of 4 */
    {"THINK 170", "K.....k.K.......", "....S..g....S..g", "hhHhhhHhhhHhhhoh", "", 0.03f, 1.0f, 3.0f, 0.6f},
    /* Apache: a straight boom-bap under 8th hats and the bongos' run */
    {"APACHE 170", "K.......K.k.....", "....S.......S...", "H.h.H.h.H.h.H.h.", "..p.P..p..p.P..p", 0.06f, 0.0f, 1.0f, 0.7f},
    /* Funky Drummer: kicks on 1, the and of 1, the and of 3 and 4 e; ghost snares around the backbeat; 16th hats
     * with the open hats on the a of 2 and the e of 4; swung */
    {"FUNKY DRUMMER", "K.K.......K..k..", "....S..g.g..S..g", "hhhhhhhohhhhhohh", "", 0.10f, 0.5f, 2.5f, 0.55f},
    /* Hot Pants: a syncopated kick, ghosts before the backbeat, driving accented 16th hats */
    {"HOT PANTS 170", "K.....k...K..k..", "....S..g....S.g.", "HhHhHhHhHhHhHhHh", "", 0.05f, 0.5f, 3.5f, 0.6f},
    /* a jungle roller: the two-step kick, rolling ghosts, a shaker on 8ths */
    {"ROLLER 170", "K.........K.....", "....S.g..g..S.gg", "x.x.x.x.x.x.x.x.", "", 0.0f, 0.0f, 1.5f, 0.1f},
};
const char *jb_testbreak_name(uint32_t id) { return TB[id % JB_NTESTBREAKS].name; }

void jb_testbreak_make(jb_break_t *b, int16_t *buf, uint32_t rate) { jb_testbreak_make_n(b, buf, rate, 0); }

void jb_testbreak_make_n(jb_break_t *b, int16_t *buf, uint32_t rate, uint32_t id)
{
    enum { V_K, V_S, V_H, V_O, V_R, V_P, NV };
    const tb_def_t *d = &TB[id % JB_NTESTBREAKS];
    jd_voice_t v[NV];
    jd_sound_t snd[NV];
    float chunk[32];
    /* the drums run at the FM-1 rate; resampled by holding (rate <= JO_SR) through a phase counter */
    float step = (float)rate * JO_INV_SR, acc = 0.0f, step_src = 60.0f / TB_BPM / 4.0f * JO_SR;
    uint32_t frames = jb_testbreak_frames(rate), w = 0, src = 0, i, s, k;
    struct { uint32_t at; uint8_t voice, open; float vel; } ev[112];
    uint32_t nev = 0, next = 0;
    jo_rng_t rng;
    static const uint8_t MODEL[NV] = {JD_KICK, JD_SNARE, JD_HATS, JD_HATS, JD_HATS, JD_PERC};
    jo_rng_seed(&rng, 170u + id);
    for (k = 0; k < NV; k++) {
        jd_voice_init(&v[k], MODEL[k], k + 1u + 7u * id);
        jd_sound_default(&snd[k], MODEL[k]);
    }
    snd[V_K].m[0] = 0.35f; snd[V_K].m[1] = 0.45f; snd[V_K].tune = d->ktune;  /* a round, tight sixties kick */
    snd[V_S].m[0] = 0.75f; snd[V_S].m[2] = 0.45f; snd[V_S].tune = d->stune;  /* a bright, ringing snare */
    snd[V_H].m[0] = d->hmetal; snd[V_H].m[1] = 0.5f; snd[V_H].m[2] = 0.35f; snd[V_H].level = 0.55f;
    snd[V_O] = snd[V_H];
    snd[V_R].m[0] = 0.95f; snd[V_R].m[1] = 0.35f; snd[V_R].m[2] = 0.8f; snd[V_R].level = 0.5f; /* the ride: washy */
    snd[V_P].variant = JD_PERC_TOM; snd[V_P].m[0] = 0.75f; snd[V_P].m[1] = 0.25f; snd[V_P].level = 0.6f;
    if (d->h[0] == 'x') { snd[V_H].m[0] = 0.0f; snd[V_H].m[1] = 0.65f; } /* a shaker: noise only */
    /* the events, in source samples */
    for (s = 0; s < 16u; s++) {
        uint32_t at = (uint32_t)((float)s * step_src + ((s & 1u) ? d->swing * step_src : 0.0f));
        float hum = 0.92f + 0.08f * jo_rng_bi(&rng);
        char kc = d->k[s], sc = d->s[s], hc = d->h[s], pc = d->p[0] ? d->p[s] : '.';
        uint8_t vo = 0xFFu, op = 0;
        float ve = 0.0f;
        uint32_t lag = 0;
        if (nev + 4u > sizeof ev / sizeof ev[0]) break;
        if (kc == 'K' || kc == 'k') {
            ev[nev].voice = V_K; ev[nev].open = 0; ev[nev].at = at;
            ev[nev++].vel = (kc == 'K' ? (s == 0 ? 1.0f : 0.9f) : 0.7f) * hum;
        }
        if (sc == 'S' || sc == 's' || sc == 'g') {
            ev[nev].voice = V_S; ev[nev].open = 0;
            ev[nev].at = at + (sc == 's' ? (uint32_t)(0.004f * JO_SR) : sc == 'g' ? (uint32_t)(0.008f * JO_SR) : 0u);
            ev[nev++].vel = sc == 'S' ? 1.0f * hum : sc == 's' ? 0.68f * hum : 0.36f + 0.08f * jo_rng_bi(&rng);
        }
        switch (hc) {
        case 'h': vo = V_H; ve = 0.55f; lag = 20u; break;
        case 'H': vo = V_H; ve = 0.85f; lag = 20u; break;
        case 'x': vo = V_H; ve = (s & 3u) == 2u ? 0.8f : 0.5f; lag = 40u; break;
        case 'o': vo = V_O; ve = 0.8f; op = 1; lag = 20u; break;
        case 'r': vo = V_R; ve = 0.6f; op = 1; lag = 30u; break;
        case 'R': vo = V_R; ve = 0.9f; op = 1; lag = 30u; break;
        default: break;
        }
        if (vo != 0xFFu) { ev[nev].voice = vo; ev[nev].open = op; ev[nev].at = at + lag; ev[nev++].vel = ve * hum; }
        if (pc == 'p' || pc == 'P') {
            ev[nev].voice = V_P; ev[nev].open = 0; ev[nev].at = at + 60u;
            ev[nev++].vel = (pc == 'P' ? 0.9f : 0.55f) * hum;
        }
    }
    /* in order of time (the small lags keep each after its step's hit) */
    for (i = 1; i < nev; i++)
        for (k = i; k > 0u && ev[k - 1u].at > ev[k].at; k--) {
            uint32_t ta = ev[k].at; uint8_t tv = ev[k].voice, to = ev[k].open; float tl = ev[k].vel;
            ev[k].at = ev[k - 1u].at, ev[k].voice = ev[k - 1u].voice, ev[k].open = ev[k - 1u].open, ev[k].vel = ev[k - 1u].vel;
            ev[k - 1u].at = ta, ev[k - 1u].voice = tv, ev[k - 1u].open = to, ev[k - 1u].vel = tl;
        }
    while (w < frames) {
        uint32_t n = 32u;
        while (next < nev && ev[next].at <= src) {
            if (ev[next].voice == V_H) jd_choke(&v[V_O]); /* a closed hat chokes the open one */
            jd_trigger(&v[ev[next].voice], &snd[ev[next].voice], ev[next].vel, ev[next].open);
            next++;
        }
        if (next < nev && ev[next].at - src < n) n = ev[next].at - src;
        if (n == 0u) n = 1u;
        for (i = 0; i < n; i++) chunk[i] = 0.0f;
        for (k = 0; k < NV; k++) jd_render(&v[k], chunk, n);
        for (i = 0; i < n && w < frames; i++) {
            acc += step;
            if (acc >= 1.0f) {
                acc -= 1.0f;
                buf[w++] = (int16_t)(jo_softclip(chunk[i] * 0.62f) * 32000.0f);
            }
        }
        src += n;
    }
    /* the room: early reflections at 7-31 ms that feed back (each reads what is already reflected), damped by
     * averaging two neighbours; the gains sum to 0.62 (stable), the tail about 0.25 s */
    {
        static const float RT_MS[5] = {7.0f, 11.0f, 17.0f, 23.0f, 31.0f};
        static const float RG[5] = {0.18f, 0.15f, 0.12f, 0.1f, 0.07f};
        uint32_t dl[5];
        for (k = 0; k < 5u; k++) dl[k] = (uint32_t)(RT_MS[k] * 0.001f * (float)rate);
        for (i = dl[4] + 1u; i < frames; i++) {
            float y = (float)buf[i];
            for (k = 0; k < 5u; k++) y += RG[k] * 0.5f * ((float)buf[i - dl[k]] + (float)buf[i - dl[k] - 1u]);
            /* the sampler: 12 bits, a soft ceiling */
            y = jo_softclip(y * (1.0f / 32000.0f)) * 32000.0f;
            buf[i] = (int16_t)(jo_floorf(y * (1.0f / 16.0f) + 0.5f) * 16.0f);
        }
    }
    b->pcm = buf;
    b->frames = frames;
    b->rate = rate;
    b->bpm = TB_BPM;
    b->bars = 1;
    for (i = 0; i < 16u && d->name[i]; i++) b->name[i] = d->name[i];
    for (; i <= 16u; i++) b->name[i] = 0;
    jb_slice_even(b, 16);
    for (s = 0; s < 16u; s++)
        b->cls[s] = d->k[s] == 'K' || d->k[s] == 'k' ? JB_CLASS_KICK
                    : d->s[s] == 'S' || d->s[s] == 's' ? JB_CLASS_SNARE
                    : d->s[s] == 'g' ? JB_CLASS_OTHER : JB_CLASS_HAT;
}

/* ---------------------------------------------------------------- player --- */
void jb_player_init(jb_player_t *p, const jb_break_t *b)
{
    uint8_t *q = (uint8_t *)p;
    uint32_t i;
    for (i = 0; i < sizeof *p; i++) q[i] = 0;
    p->brk = b;
    p->tempo_ratio = 1.0f;
    p->gate = 1.0f;
    p->level = 1.0f;
}

static void voice_fade(jb_voice_t *v)
{
    if (v->active && !v->releasing) {
        v->releasing = 1;
        v->env_step = -1.0f / FADE_OUT;
    }
}

void jb_play(jb_player_t *p, uint32_t slice, float vel, int reverse, float semis)
{
    const jb_break_t *b = p->brk;
    jb_voice_t *v;
    float len;
    if (!b || !b->nslices) return;
    slice %= b->nslices;
    voice_fade(&p->v[p->cur]); /* the old slice crossfades out */
    p->cur ^= 1u;
    v = &p->v[p->cur];
    v->active = 1;
    v->releasing = 0;
    v->reverse = (uint8_t)(reverse != 0);
    v->lo = (float)b->start[slice];
    v->hi = (float)b->start[slice + 1u];
    len = (v->hi - v->lo) * jo_clampf(p->gate, 0.05f, 1.0f);
    v->hi = v->lo + len;
    v->rate = (float)b->rate * JO_INV_SR * jo_exp2f((p->pitch + semis) * (1.0f / 12.0f)) * p->tempo_ratio;
    v->pos = v->reverse ? v->hi - 1.0f : v->lo;
    v->loop_len = 0.0f;
    v->gain = jo_clampf(vel, 0.0f, 1.0f) * (float)b->gain[slice] * (1.0f / 128.0f);
    v->env = 0.0f;
    v->env_step = 1.0f / FADE_IN;
    v->fade_out_at = FADE_OUT * v->rate; /* source frames before the end where the fade begins */
}

void jb_microloop(jb_player_t *p, float len_frames)
{
    jb_voice_t *v = &p->v[p->cur];
    if (!v->active) return;
    if (len_frames <= 0.0f) {
        v->loop_len = 0.0f;
        return;
    }
    v->loop_len = jo_maxf(len_frames, 64.0f);
    v->loop_lo = v->reverse ? v->pos - v->loop_len : v->pos;
    if (v->loop_lo < v->lo) v->loop_lo = v->lo;
}

void jb_release(jb_player_t *p)
{
    voice_fade(&p->v[0]);
    voice_fade(&p->v[1]);
}

void jb_render(jb_player_t *p, float *out, uint32_t n)
{
    const jb_break_t *b = p->brk;
    uint32_t k, i;
    if (!b) return;
    for (k = 0; k < 2u; k++) {
        jb_voice_t *v = &p->v[k];
        if (!v->active) continue;
        for (i = 0; i < n; i++) {
            uint32_t ip;
            float f, s, left;
            /* microloop: wrap inside [loop_lo, loop_lo + len), with the envelope dipping at each wrap */
            if (v->loop_len > 0.0f) {
                float rel = v->pos - v->loop_lo;
                if (rel >= v->loop_len || rel < 0.0f) {
                    v->pos = v->reverse ? v->loop_lo + v->loop_len - 1.0f : v->loop_lo;
                    v->env = 0.0f;
                    if (!v->releasing) v->env_step = 1.0f / FADE_IN;
                }
                left = v->reverse ? v->pos - v->loop_lo : v->loop_lo + v->loop_len - v->pos;
            } else {
                left = v->reverse ? v->pos - v->lo : v->hi - v->pos;
            }
            if (left <= 0.0f || v->pos < 0.0f || v->pos >= (float)(b->frames - 1u)) {
                if (v->loop_len <= 0.0f) {
                    v->active = 0;
                    break;
                }
                left = 0.0f;
            }
            /* the end of a slice (or a loop round) fades out */
            if (left < v->fade_out_at && !v->releasing) {
                float e = left / v->fade_out_at;
                if (e < v->env) v->env = e;
            } else {
                v->env += v->env_step;
                if (v->env > 1.0f) { v->env = 1.0f; v->env_step = 0.0f; }
                if (v->env <= 0.0f) { v->env = 0.0f; if (v->releasing) { v->active = 0; break; } }
            }
            ip = (uint32_t)v->pos;
            f = v->pos - (float)ip;
            s = ((float)b->pcm[ip] + ((float)b->pcm[ip + 1u] - (float)b->pcm[ip]) * f) * (1.0f / 32768.0f);
            out[i] += s * v->gain * v->env * p->level;
            v->pos += v->reverse ? -v->rate : v->rate;
        }
    }
}
