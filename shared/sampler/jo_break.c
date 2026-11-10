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
/* one bar of 16ths at 170 BPM, an original pattern: K . . . S . . k . . K . S . . g  plus hats */
#define TB_BPM 170.0f
uint32_t jb_testbreak_frames(uint32_t rate) { return (uint32_t)((float)rate * 60.0f / TB_BPM * 4.0f); }

void jb_testbreak_make(jb_break_t *b, int16_t *buf, uint32_t rate)
{
    static const char PAT[] = "K..hS.hk.hK.S.hg"; /* K kick, k soft kick, S snare, g ghost, h hat */
    jd_voice_t vk, vs, vg, vh;
    jd_sound_t sk, ss, sg, sh;
    uint32_t frames = jb_testbreak_frames(rate), per = frames / 16u, i, s;
    float chunk[64];
    /* the drums run at the FM-1 rate; resample by holding (rate <= JO_SR) through a phase counter */
    float step = (float)rate * JO_INV_SR, acc = 0.0f;
    uint32_t w = 0;
    jd_voice_init(&vk, JD_KICK, 1);
    jd_voice_init(&vs, JD_SNARE, 2);
    jd_voice_init(&vg, JD_GHOST, 3);
    jd_voice_init(&vh, JD_HATS, 4);
    jd_sound_default(&sk, JD_KICK);
    jd_sound_default(&ss, JD_SNARE);
    jd_sound_default(&sg, JD_GHOST);
    jd_sound_default(&sh, JD_HATS);
    sk.m[0] = 0.3f; ss.m[2] = 0.45f; sh.m[1] = 0.4f; /* a slightly dusty, roomy kit */
    for (s = 0; s < 16u && w < frames; s++) {
        uint32_t end = (s + 1u) * per, src_n = (uint32_t)((float)per / step) + 2u, done = 0;
        switch (PAT[s]) {
        case 'K': jd_trigger(&vk, &sk, 1.0f, 0); break;
        case 'k': jd_trigger(&vk, &sk, 0.6f, 0); break;
        case 'S': jd_trigger(&vs, &ss, 1.0f, 0); break;
        case 'g': jd_trigger(&vg, &sg, 0.7f, 0); break;
        case 'h': jd_trigger(&vh, &sh, 0.6f, 0); break;
        default: break;
        }
        if (!(s & 1u) && PAT[s] != 'h') jd_trigger(&vh, &sh, 0.35f, 0); /* quiet 8th hats under everything */
        while (done < src_n && w < end) {
            uint32_t n = src_n - done < 64u ? src_n - done : 64u;
            for (i = 0; i < n; i++) chunk[i] = 0.0f;
            jd_render(&vk, chunk, n);
            jd_render(&vs, chunk, n);
            jd_render(&vg, chunk, n);
            jd_render(&vh, chunk, n);
            for (i = 0; i < n && w < end; i++) {
                acc += step;
                if (acc >= 1.0f) {
                    acc -= 1.0f;
                    buf[w++] = (int16_t)(jo_softclip(chunk[i] * 0.7f) * 32000.0f);
                }
            }
            done += n;
        }
        while (w < end) buf[w++] = 0;
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
        b->cls[s] = PAT[s] == 'K' || PAT[s] == 'k' ? JB_CLASS_KICK : PAT[s] == 'S' ? JB_CLASS_SNARE
                    : PAT[s] == 'h' ? JB_CLASS_HAT : PAT[s] == 'g' ? JB_CLASS_OTHER : JB_CLASS_NONE;
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
