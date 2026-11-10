/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* The JUNGLE OS step sequencer and clock: instrument-agnostic, sample-accurate, run inside the render.
 *
 * Timing model. The clock advances a 32-bit step phase per sample (one wrap = one 16th note). Step S is evaluated
 * half a step early, when step S-1's phase crosses one half; its trigs are queued with a delay of
 * (0.5 + swing + micro) steps, so microtiming reaches -0.5 .. +0.5 of a step and swing up to 75 % without
 * look-back. Ratchets are queued as several events within the step. Events carry an absolute sample time; the
 * instrument renders up to each event and fires it on its exact sample (js_queue_*).
 *
 * Determinism. Probability and generated variations draw from a hash of (seed, bar, step, track), never from a
 * running RNG, so a pattern plays identically on every pass and after a restart with the same seed. */
#pragma once
#include "../dsp/jo_math.h"

#define JS_MAXSTEPS 32   /* per track; longer arrangements chain patterns */
#define JS_MAXTRACKS 8
#define JS_NLOCK 2       /* parameter locks per step */
#define JS_STEPS_PER_BAR 16u
#define JS_QLEN 64       /* pending events */
#define JS_LOCK_NONE 0xFFu

enum { JS_ON = 1, JS_ACCENT = 2, JS_SLIDE = 4, JS_REVERSE = 8, JS_TIE = 16 };

typedef struct {
    uint8_t flags;            /* JS_* */
    uint8_t vel;              /* 1..127 */
    uint8_t prob;             /* 0..100 % */
    uint8_t ratch;            /* hits in the step - 1 (0 = one hit), 0..7 */
    int8_t micro;             /* -64..+63: -0.5 .. +0.5 step */
    uint8_t data;             /* the instrument's: a slice number, a note */
    uint8_t lk_id[JS_NLOCK];  /* locked parameter ids, JS_LOCK_NONE = free */
    uint8_t lk_val[JS_NLOCK]; /* their values, 0..255 */
} js_step_t;

typedef struct {
    js_step_t s[JS_MAXSTEPS];
    uint8_t len;   /* 1..JS_MAXSTEPS */
    uint8_t div;   /* 0: 1/16, 1: 1/32 (double speed), 2: 1/8 (half) */
    uint8_t rsv[2];
} js_track_t;

typedef struct {
    js_track_t t[JS_MAXTRACKS];
    uint8_t ntracks;
    uint8_t swing;  /* 50..75 (%), MPC-style: the second 16th of each pair delayed */
    uint16_t seed;
} js_pattern_t;

/* an event as the instrument gets it */
typedef struct {
    uint32_t due;         /* absolute sample time */
    uint8_t track, vel, data, flags;
    uint8_t lk_id[JS_NLOCK], lk_val[JS_NLOCK];
    uint8_t hit, nhits;   /* ratchet index of this hit, and the count */
    uint16_t step;        /* the step's index in its track */
    uint32_t abs_step;    /* the global 16th count of the step (bar = abs_step / 16) */
} js_event_t;

typedef struct {
    js_event_t e[JS_QLEN];
    uint32_t n;
    uint32_t dropped;     /* events lost to a full queue (a diagnostic; never expected) */
} js_queue_t;

/* ------------------------------------------------------------------ clock --- */
enum { JS_CLK_INT, JS_CLK_MIDI };
typedef struct {
    float bpm;            /* the running tempo */
    uint32_t phase;       /* within the current 16th, a full turn = 2^32 */
    uint32_t inc;         /* per sample */
    uint32_t abs_step;    /* 16ths since start */
    uint32_t now;         /* samples since start (wraps) */
    uint8_t running, source;
    /* MIDI clock follow (24 PPQN: 6 clocks a 16th) */
    uint32_t mc_last_t24; /* the last clock's arrival, plat_ticks24 units (24 per us) */
    float mc_interval_us; /* smoothed interval between clocks */
    uint32_t mc_count;    /* clocks since START */
    uint8_t mc_valid;
    /* MIDI clock out: 6 per step, at phases k/6 */
    uint8_t out_clock_sub;
} js_clock_t;

void js_clock_init(js_clock_t *c, float bpm);
void js_clock_set_bpm(js_clock_t *c, float bpm);
void js_clock_start(js_clock_t *c);   /* from step 0 */
void js_clock_stop(js_clock_t *c);
float js_clock_samples_per_step(const js_clock_t *c);
/* MIDI clock in: one 0xF8 at time t24; the tempo follows a smoothed interval, the phase is pulled to the clock's
 * count so the two devices stay locked without jumps (at most 1/8 of the error corrected per clock) */
void js_clock_midi_tick(js_clock_t *c, uint32_t t24);
void js_clock_midi_start(js_clock_t *c);

/* ------------------------------------------------------------- sequencer --- */
typedef struct {
    const js_pattern_t *pat;   /* the playing pattern (the instrument owns the storage) */
    uint8_t mute;              /* bitmask, muted tracks emit nothing */
    uint8_t solo;              /* bitmask, if nonzero only these play */
    uint16_t pos[JS_MAXTRACKS];/* current step index per track */
    uint32_t sub[JS_MAXTRACKS];/* per-track sub-step counters for div != 0 */
} js_seq_t;

void js_seq_init(js_seq_t *s, const js_pattern_t *p);
void js_seq_reset(js_seq_t *s);       /* every track back to step 0 */

/* the per-block driver: advances the clock over n samples and queues the trigs that fall due (in the queue, by
 * absolute time). on_step, if given, is called at each 16th boundary (abs_step, sample offset in the block) for
 * bar-quantized actions (pattern / scene switches, fills). on_clock_out, if given, at each MIDI clock to send. */
typedef struct {
    void (*on_step)(void *ctx, uint32_t abs_step, uint32_t offset);
    void (*on_clock_out)(void *ctx, uint32_t offset);
    /* the instrument's last word on each step before it is queued (fills, performance modes, the rhythm graph,
     * break-lock), called for every track's step, on or off: it edits *st (a copy; the pattern is not changed),
     * which may switch a step on, add ratchets, change its data; return 0 to drop the step */
    int (*transform)(void *ctx, uint32_t track, uint32_t abs_step, js_step_t *st);
    void *ctx;
} js_hooks_t;
void js_run(js_seq_t *s, js_clock_t *c, js_queue_t *q, uint32_t n, const js_hooks_t *h);

/* the queue: due-time ordered pops */
void js_queue_clear(js_queue_t *q);
int js_queue_push(js_queue_t *q, const js_event_t *e);
/* the next event due before `before` (absolute samples), removed from the queue into *out; 0 = none */
int js_queue_pop_due(js_queue_t *q, uint32_t before, js_event_t *out);
/* the earliest due time in the queue, or `fallback` if empty */
uint32_t js_queue_next_due(const js_queue_t *q, uint32_t fallback);

/* ---------------------------------------------------------- pattern utils --- */
void js_pattern_clear(js_pattern_t *p, uint8_t ntracks, uint8_t len);
void js_step_clear(js_step_t *st);
/* the lock value of id in the step, or -1 */
int js_step_lock(const js_step_t *st, uint8_t id);
/* sets (or with val < 0 removes) a lock; 0 = done, -1 = both slots used by other ids */
int js_step_set_lock(js_step_t *st, uint8_t id, int val);
/* the deterministic hash behind probability and generators */
uint32_t js_hash(uint32_t seed, uint32_t a, uint32_t b, uint32_t c);
/* wrap-safe "time a is before time b" */
JO_INLINE int js_before(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }
