/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
#include "jo_seq.h"

#define TWO32 4294967296.0f

/* ------------------------------------------------------------------ clock --- */
void js_clock_init(js_clock_t *c, float bpm)
{
    uint8_t *p = (uint8_t *)c;
    uint32_t i;
    for (i = 0; i < sizeof *c; i++)
        p[i] = 0;
    js_clock_set_bpm(c, bpm);
}

void js_clock_set_bpm(js_clock_t *c, float bpm)
{
    float steps_per_sample;
    bpm = jo_clampf(bpm, 40.0f, 300.0f);
    c->bpm = bpm;
    steps_per_sample = bpm * (4.0f / 60.0f) * JO_INV_SR; /* 16ths per sample, ~0.026 at 174 */
    c->inc = (uint32_t)(steps_per_sample * TWO32);
}

float js_clock_samples_per_step(const js_clock_t *c) { return TWO32 / (float)(c->inc ? c->inc : 1u); }

void js_clock_start(js_clock_t *c)
{
    c->running = 1;
    c->abs_step = 0;
    /* step 0 is evaluated at the half crossing of the step before: start just past that point so step 0's trigs
     * are queued at once and play half a step after START (a fixed, documented latency of 1/32 note) */
    c->phase = 0x80000000u - 1u;
    c->abs_step = 0xFFFFFFFFu; /* "step -1" */
    c->out_clock_sub = 0;
}

void js_clock_stop(js_clock_t *c) { c->running = 0; }

void js_clock_midi_start(js_clock_t *c)
{
    js_clock_start(c);
    c->mc_count = 0;
    c->mc_valid = 0;
}

void js_clock_midi_tick(js_clock_t *c, uint32_t t24)
{
    if (c->mc_last_t24) {
        float dt = (float)(t24 - c->mc_last_t24) * (1.0f / 24.0f); /* us */
        if (dt > 2000.0f && dt < 60000.0f) {                      /* 40..1250 bpm-range sanity */
            c->mc_interval_us = c->mc_valid ? c->mc_interval_us + (dt - c->mc_interval_us) * 0.1f : dt;
            c->mc_valid = 1;
            js_clock_set_bpm(c, 60.0e6f / (c->mc_interval_us * 24.0f));
        }
    }
    c->mc_last_t24 = t24;
    if (c->running) {
        /* where the clock count says we are inside the step (6 clocks per 16th), against where we are */
        uint32_t sub = c->mc_count % 6u;
        uint32_t want = (uint32_t)((float)sub * (TWO32 / 6.0f));
        int32_t err = (int32_t)(want - c->phase);
        /* only pull when within half a step (else a lost clock would jump a whole step) */
        if (err > -0x40000000 && err < 0x40000000)
            c->phase += (uint32_t)(err / 8);
    }
    c->mc_count++;
}

/* ------------------------------------------------------------- utilities --- */
uint32_t js_hash(uint32_t seed, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = seed * 0x9E3779B1u ^ a * 0x85EBCA77u ^ b * 0xC2B2AE3Du ^ c * 0x27D4EB2Fu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

void js_step_clear(js_step_t *st)
{
    st->flags = 0;
    st->vel = 100;
    st->prob = 100;
    st->ratch = 0;
    st->micro = 0;
    st->data = 0;
    st->lk_id[0] = st->lk_id[1] = JS_LOCK_NONE;
    st->lk_val[0] = st->lk_val[1] = 0;
}

void js_pattern_clear(js_pattern_t *p, uint8_t ntracks, uint8_t len)
{
    uint32_t t, s;
    p->ntracks = ntracks > JS_MAXTRACKS ? JS_MAXTRACKS : ntracks;
    p->swing = 50;
    p->seed = 1;
    for (t = 0; t < JS_MAXTRACKS; t++) {
        p->t[t].len = len < 1 ? 1 : len > JS_MAXSTEPS ? JS_MAXSTEPS : len;
        p->t[t].div = 0;
        p->t[t].rsv[0] = p->t[t].rsv[1] = 0;
        for (s = 0; s < JS_MAXSTEPS; s++) {
            js_step_clear(&p->t[t].s[s]);
            p->t[t].s[s].data = (uint8_t)s;
        }
    }
}

int js_step_lock(const js_step_t *st, uint8_t id)
{
    uint32_t i;
    for (i = 0; i < JS_NLOCK; i++)
        if (st->lk_id[i] == id)
            return st->lk_val[i];
    return -1;
}

int js_step_set_lock(js_step_t *st, uint8_t id, int val)
{
    uint32_t i;
    for (i = 0; i < JS_NLOCK; i++)
        if (st->lk_id[i] == id) {
            if (val < 0)
                st->lk_id[i] = JS_LOCK_NONE;
            else
                st->lk_val[i] = (uint8_t)jo_clampi(val, 0, 255);
            return 0;
        }
    if (val < 0)
        return 0;
    for (i = 0; i < JS_NLOCK; i++)
        if (st->lk_id[i] == JS_LOCK_NONE) {
            st->lk_id[i] = id;
            st->lk_val[i] = (uint8_t)jo_clampi(val, 0, 255);
            return 0;
        }
    return -1;
}

/* ------------------------------------------------------------------ queue --- */
void js_queue_clear(js_queue_t *q) { q->n = 0; }

int js_queue_push(js_queue_t *q, const js_event_t *e)
{
    if (q->n >= JS_QLEN) {
        q->dropped++;
        return -1;
    }
    q->e[q->n++] = *e;
    return 0;
}

uint32_t js_queue_next_due(const js_queue_t *q, uint32_t fallback)
{
    uint32_t i, best = fallback;
    for (i = 0; i < q->n; i++)
        if (js_before(q->e[i].due, best))
            best = q->e[i].due;
    return best;
}

/* the order of events: due time, then track, then ratchet hit, so simultaneous events always fire in the same
 * order whatever the queue's history */
static int ev_first(const js_event_t *a, const js_event_t *b)
{
    if (a->due != b->due) return js_before(a->due, b->due);
    if (a->track != b->track) return a->track < b->track;
    return a->hit < b->hit;
}

int js_queue_pop_due(js_queue_t *q, uint32_t before, js_event_t *out)
{
    uint32_t i, k = q->n;
    for (i = 0; i < q->n; i++)
        if (js_before(q->e[i].due, before) && (k == q->n || ev_first(&q->e[i], &q->e[k])))
            k = i;
    if (k == q->n)
        return 0;
    *out = q->e[k];
    q->e[k] = q->e[--q->n]; /* order is restored by due time at every pop */
    return 1;
}

/* -------------------------------------------------------------- sequencer --- */
void js_seq_init(js_seq_t *s, const js_pattern_t *p)
{
    uint32_t t;
    s->pat = p;
    s->mute = s->solo = 0;
    for (t = 0; t < JS_MAXTRACKS; t++)
        s->pos[t] = 0, s->sub[t] = 0;
}

void js_seq_reset(js_seq_t *s)
{
    uint32_t t;
    for (t = 0; t < JS_MAXTRACKS; t++)
        s->pos[t] = 0, s->sub[t] = 0;
}

/* queue one track's trig(s) for 16th abs_step, whose grid time is grid (absolute samples) */
/* base: an integer sample time; grid: the step's offset from it (small, so float keeps sample precision) */
static void queue_track(js_seq_t *s, js_queue_t *q, uint32_t t, uint32_t idx, uint32_t abs_step, uint32_t base,
                        float grid, float sps, uint32_t swung, const js_hooks_t *h)
{
    const js_pattern_t *p = s->pat;
    js_step_t copy = p->t[t].s[idx];
    const js_step_t *st = &copy;
    js_event_t e;
    uint32_t k, nh;
    float when, span;
    if (h && h->transform && !h->transform(h->ctx, t, abs_step, &copy))
        return;
    if (!(st->flags & JS_ON))
        return;
    if (st->prob < 100u && (js_hash(p->seed, abs_step >> 4, idx, t) % 100u) >= st->prob)
        return;
    e.track = (uint8_t)t;
    e.vel = st->vel ? st->vel : 1u;
    e.data = st->data;
    e.flags = st->flags;
    e.lk_id[0] = st->lk_id[0], e.lk_id[1] = st->lk_id[1];
    e.lk_val[0] = st->lk_val[0], e.lk_val[1] = st->lk_val[1];
    e.step = (uint16_t)idx;
    e.abs_step = abs_step;
    nh = (uint32_t)(st->ratch & 7u) + 1u;
    e.nhits = (uint8_t)nh;
    span = p->t[t].div == 1 ? sps * 0.5f : p->t[t].div == 2 ? sps * 2.0f : sps;
    when = grid + ((float)st->micro * (1.0f / 128.0f)) * span;
    if (swung)
        when += (float)(jo_clampi(p->swing, 50, 75) - 50) * (1.0f / 25.0f) * 0.5f * sps;
    for (k = 0; k < nh; k++) {
        e.hit = (uint8_t)k;
        {
            float off = when + span * (float)k / (float)nh;
            e.due = base + (uint32_t)(off > 0.0f ? off + 0.5f : 0.0f);
        }
        js_queue_push(q, &e);
    }
}

void js_run(js_seq_t *s, js_clock_t *c, js_queue_t *q, uint32_t n, const js_hooks_t *h)
{
    uint32_t i;
    float sps;
    if (!c->running) {
        c->now += n;
        return;
    }
    sps = js_clock_samples_per_step(c);
    for (i = 0; i < n; i++) {
        uint32_t before = c->phase;
        uint32_t sub6;
        c->phase += c->inc;
        /* MIDI clock out: 6 per 16th, at k/6 of the step */
        sub6 = ((c->phase >> 16) * 6u) >> 16; /* (phase / 2^32) * 6 in 32 bits */
        if (sub6 != c->out_clock_sub) {
            c->out_clock_sub = (uint8_t)sub6;
            if (h && h->on_clock_out)
                h->on_clock_out(h->ctx, i);
        }
        if (c->phase < before) { /* a new 16th */
            c->abs_step++;
            if (h && h->on_step)
                h->on_step(h->ctx, c->abs_step, i);
        }
        /* half crossing: evaluate the next 16th, whose grid time is half a step from now */
        if (before < 0x80000000u && c->phase >= 0x80000000u && s->pat) {
            uint32_t next = c->abs_step + 1u, t;
            uint32_t base = c->now + i;
            float grid = 0.5f * sps;
            uint32_t swung = next & 1u;
            for (t = 0; t < s->pat->ntracks; t++) {
                const js_track_t *tr = &s->pat->t[t];
                uint32_t len = tr->len ? tr->len : 1u, r, reps;
                int play = !((s->mute >> t) & 1u) && (!s->solo || ((s->solo >> t) & 1u));
                /* div: 1/32 tracks take two steps per 16th, 1/8 tracks one every other 16th */
                reps = tr->div == 1 ? 2u : 1u;
                if (tr->div == 2 && (next & 1u))
                    continue;
                for (r = 0; r < reps; r++) {
                    uint32_t idx = s->pos[t] % len;
                    if (play)
                        queue_track(s, q, t, idx, next, base, grid + (float)r * 0.5f * sps, sps, swung && tr->div == 0, h);
                    s->pos[t] = (uint16_t)((idx + 1u) % len);
                }
            }
        }
    }
    c->now += n;
}
