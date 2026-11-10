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

/* ------------------------------------------------------------ test break --- */
/* one bar of 16ths at 170 BPM, an original two-step groove in the jungle manner (not a copy of a recorded break):
 *   K . h . S . h g h . K h S . h g   K kick, S snare, g ghost snare, h hat; a shaker on every 8th under it
 * played as a drummer would: ghosts soft and a little late, the hats' offbeats accented, every hit's velocity
 * varied a little; then a small room (early reflections that feed back), a sampler's 12 bits at 22 kHz. */
#define TB_BPM 170.0f
uint32_t jb_testbreak_frames(uint32_t rate) { return (uint32_t)((float)rate * 60.0f / TB_BPM * 4.0f); }

static const char TB_PAT[] = "K.h.S.hgh.KhS.hg";

void jb_testbreak_make(jb_break_t *b, int16_t *buf, uint32_t rate)
{
    enum { V_K, V_S, V_G, V_H, V_SH, NV };
    jd_voice_t v[NV];
    jd_sound_t snd[NV];
    float chunk[32];
    /* the drums run at the FM-1 rate; resampled by holding (rate <= JO_SR) through a phase counter */
    float step = (float)rate * JO_INV_SR, acc = 0.0f, step_src = 60.0f / TB_BPM / 4.0f * JO_SR;
    uint32_t frames = jb_testbreak_frames(rate), w = 0, src = 0, i, s, k;
    struct { uint32_t at; uint8_t voice; float vel; } ev[40];
    uint32_t nev = 0, next = 0;
    jo_rng_t rng;
    static const uint8_t MODEL[NV] = {JD_KICK, JD_SNARE, JD_GHOST, JD_HATS, JD_HATS};
    jo_rng_seed(&rng, 170u);
    for (k = 0; k < NV; k++) {
        jd_voice_init(&v[k], MODEL[k], k + 1u);
        jd_sound_default(&snd[k], MODEL[k]);
    }
    snd[V_K].m[0] = 0.4f; snd[V_K].m[1] = 0.5f;                       /* a rounder kick than the synth track's */
    snd[V_S].m[0] = 0.6f; snd[V_S].m[2] = 0.5f; snd[V_S].tune = -1.0f; /* a fuller, longer snare */
    snd[V_H].m[0] = 0.35f; snd[V_H].m[1] = 0.45f;
    snd[V_SH].m[0] = 0.0f; snd[V_SH].m[1] = 0.6f; snd[V_SH].m[2] = 0.2f; /* the shaker: noise only */
    snd[V_SH].level = 0.35f;
    /* the events, in source samples */
    for (s = 0; s < 16u; s++) {
        uint32_t at = (uint32_t)((float)s * step_src);
        float hum = 0.92f + 0.08f * jo_rng_bi(&rng);
        switch (TB_PAT[s]) {
        case 'K': ev[nev].voice = V_K; ev[nev].vel = (s == 0 ? 1.0f : 0.9f) * hum; ev[nev++].at = at; break;
        case 'S': ev[nev].voice = V_S; ev[nev].vel = 1.0f * hum; ev[nev++].at = at; break;
        case 'g': ev[nev].voice = V_G; ev[nev].vel = (0.55f + 0.2f * jo_rng_bi(&rng)); ev[nev++].at = at + (uint32_t)(0.009f * JO_SR); break;
        case 'h': ev[nev].voice = V_H; ev[nev].vel = ((s & 3u) == 2u ? 0.85f : 0.55f) * hum; ev[nev++].at = at; break;
        default: break;
        }
        if (!(s & 1u)) { ev[nev].voice = V_SH; ev[nev].vel = ((s & 3u) == 2u ? 0.9f : 0.5f) * hum; ev[nev++].at = at + 40u; }
    }
    /* (in order of time: the shaker's 40-sample lag and the ghosts' 9 ms keep them after their step's hit) */
    for (i = 1; i < nev; i++)
        for (k = i; k > 0u && ev[k - 1u].at > ev[k].at; k--) {
            uint32_t ta = ev[k].at; uint8_t tv = ev[k].voice; float tl = ev[k].vel;
            ev[k].at = ev[k - 1u].at, ev[k].voice = ev[k - 1u].voice, ev[k].vel = ev[k - 1u].vel;
            ev[k - 1u].at = ta, ev[k - 1u].voice = tv, ev[k - 1u].vel = tl;
        }
    while (w < frames) {
        uint32_t n = 32u;
        while (next < nev && ev[next].at <= src) {
            jd_trigger(&v[ev[next].voice], &snd[ev[next].voice], ev[next].vel, 0);
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
        uint32_t d[5];
        for (k = 0; k < 5u; k++) d[k] = (uint32_t)(RT_MS[k] * 0.001f * (float)rate);
        for (i = d[4] + 1u; i < frames; i++) {
            float y = (float)buf[i];
            for (k = 0; k < 5u; k++) y += RG[k] * 0.5f * ((float)buf[i - d[k]] + (float)buf[i - d[k] - 1u]);
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
    for (i = 0; i < 16u; i++) b->name[i] = "TEST BREAK 170\0\0"[i];
    b->name[16] = 0;
    jb_slice_even(b, 16);
    for (s = 0; s < 16u; s++)
        b->cls[s] = TB_PAT[s] == 'K' ? JB_CLASS_KICK : TB_PAT[s] == 'S' ? JB_CLASS_SNARE
                    : TB_PAT[s] == 'h' ? JB_CLASS_HAT : TB_PAT[s] == 'g' ? JB_CLASS_OTHER : JB_CLASS_NONE;
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
