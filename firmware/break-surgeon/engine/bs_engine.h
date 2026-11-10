/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* BREAK SURGEON's engine: the sliced break and six synthesized drums on one sequencer, the rhythm and kit
 * sweetspot graphs, performance modes, fills, mutation, scenes, DRUM PRESSURE, and the link's sender side.
 * Platform-free: the app (app.c) and the host tests drive it through this interface. Threading: bs_render runs
 * in the audio interrupt; everything else is the main loop's, and reaches the render through the command ring
 * or single-byte writes to the pattern. */
#pragma once
#include "../../../shared/seq/jo_seq.h"
#include "../../../shared/seq/jo_cmdq.h"
#include "../../../shared/drums/jo_drums.h"
#include "../../../shared/sampler/jo_break.h"
#include "../../../shared/fx/jo_fx.h"
#include "../../../shared/sweetspot/jo_graph.h"
#include "../../../shared/link/jo_link.h"

enum { BS_T_BREAK, BS_T_KICK, BS_T_SNARE, BS_T_GHOST, BS_T_HATS, BS_T_PERC, BS_T_IMPACT, BS_NTRACKS };
extern const char *const bs_track_name[BS_NTRACKS];
#define BS_NPATTERNS 4
#define BS_NSCENES 8
#define BS_BLOCK_MAX 256u
#define BS_FLAG_OPEN JS_SLIDE /* on the HATS track: an open hat */

/* parameter-lock ids (js_step_t lk_id) */
enum {
    BS_LK_M0, BS_LK_M1, BS_LK_M2, BS_LK_M3, /* the track's four macros (drums) */
    BS_LK_TUNE,                             /* 128 = 0, 4 steps per semitone (drums and break) */
    BS_LK_LEVEL,
    BS_LK_GATE,                             /* break: the slice's gate 0..255 */
    BS_LK_LOOP,                             /* break: a microloop, 1..8 = 1/(2^n) of a step */
    BS_NLOCKS
};

enum { BS_PERF_CLEAN, BS_PERF_ROLL, BS_PERF_CHOP, BS_PERF_REVERSE, BS_PERF_GLITCH, BS_PERF_DESTROY, BS_PERF_FREEZE,
       BS_NPERF };
extern const char *const bs_perf_name[BS_NPERF];
enum { BS_LOCK_OFF, BS_LOCK_LAYER, BS_LOCK_SEPARATE }; /* BREAK-LOCK */
enum { BS_Q_NOW, BS_Q_BEAT, BS_Q_BAR, BS_Q_2BAR, BS_Q_4BAR };

/* the rhythm graph's parameters (Clean <-> Rolling <-> Chopped <-> Glitched) */
enum { BS_R_CHOP, BS_R_RATCH, BS_R_REV, BS_R_STUTTER, BS_R_GHOST, BS_R_HATS, BS_R_GATE, BS_R_PITCH, BS_NRPARAMS };
/* the kit graph's: kick 4, snare 4, hats 4, ghost tone/decay, perc tone/decay */
#define BS_NKPARAMS 16

typedef struct {
    uint8_t used, pattern, mute, quant;
    float rhythm_t, kit_t, energy;
    jf_drum_macros_t fx;
} bs_scene_t;

typedef struct {
    /* sound */
    jb_break_t breaks[JB_MAXBREAKS];
    uint8_t nbreaks, cur_break, has_bank;
    jb_player_t player;
    jd_voice_t dv[BS_NTRACKS];         /* [0] unused (the break) */
    jd_sound_t snd[BS_NTRACKS];        /* the sounds as the kit graph and the SOUND page set them */
    float break_level, break_pitch;
    uint8_t slices;                    /* 8, 16, 32: the grid the break is cut in (even, or the bank's own) */
    uint8_t tempo_sync;                /* repitch the break to the tempo */
    /* sequencing */
    js_pattern_t pat[BS_NPATTERNS];
    js_pattern_t undo;                 /* the break track before the last mutation (one level) */
    uint8_t undo_valid, undo_pattern;
    uint8_t cur_pat, next_pat;         /* next_pat != cur_pat: a switch at the bar */
    js_clock_t clock;
    js_seq_t seq;
    js_queue_t q;
    jo_cmdq_t cmd;
    /* performance */
    uint8_t perf, perf_slice, roll_div; /* the held mode; ROLL's slice; 1 = 1/16, 3 = 1/32 */
    uint8_t fill_on, fill_bars, fill_pending, return_pending, breaklock, anchors;
    uint32_t fill_start, fill_end;     /* abs steps */
    float energy;                      /* 0..1: fill intensity, ghost density */
    uint8_t cur_slice_cls;             /* the class of the break slice of the step being evaluated */
    uint32_t cur_eval_step;
    /* graphs */
    jg_graph_t rhythm, kit;
    float rhythm_t, kit_t;
    jg_eval_t rv, kv;
    uint8_t kit_dirty;
    /* scenes */
    bs_scene_t scene[BS_NSCENES];
    int8_t scene_pending, scene_cur;
    uint8_t scene_quant;
    uint32_t pending_at;               /* the boundary a pending pattern / scene / return waits for */
    /* master */
    jf_drum_bus_t bus;
    float bus_buf[BS_BLOCK_MAX];
    float send_buf[BS_BLOCK_MAX], tmp_buf[BS_BLOCK_MAX]; /* the room's send; one voice at a time */
    /* link (sender) */
    jl_link_t link;
    void (*midi_out)(uint32_t pkt);    /* the app's: plat_midi_send to the link's ports; 0 = none */
    uint8_t send_clock, send_kicks;
    uint32_t ms, ms_rem;               /* milliseconds from the sample count */
    /* diagnostics; trace: every fired event (the tests; 0 on the device) */
    void (*trace)(void *ctx, const js_event_t *ev);
    void *trace_ctx;
    uint32_t blocks, events_fired;
    float peak;
    /* the test break's RAM (when no bank is installed) */
    int16_t *test_pcm;
} bs_engine_t;

/* test_pcm: RAM for the procedural test break, jb_testbreak_frames(22059) samples (or 0: no test break);
 * bank: an installed JOB1 image (or 0) */
void bs_init(bs_engine_t *e, int16_t *test_pcm, const uint8_t *bank, uint32_t bank_len);
void bs_render(bs_engine_t *e, int32_t *lr, uint32_t n, float out_gain); /* the audio interrupt: 24-bit stereo */
void bs_set_room_buf(bs_engine_t *e, int16_t *buf);  /* JF_VERB_TOTAL samples for DRUM PRESSURE's room (after bs_init) */
void bs_render_float(bs_engine_t *e, float *mono, uint32_t n);           /* the same, mono float (tests) */

/* ---- the main loop's commands (through the ring; applied at the next block) ---- */
void bs_cmd_play(bs_engine_t *e, int on);
void bs_cmd_bpm(bs_engine_t *e, float bpm);
void bs_cmd_perf(bs_engine_t *e, int mode);           /* a held mode (CLEAN releases) */
void bs_cmd_fill(bs_engine_t *e, int bars);           /* from the next bar, 1/2/4 bars */
void bs_cmd_return(bs_engine_t *e);                   /* back to the stable groove at the next bar */
void bs_cmd_pattern(bs_engine_t *e, int p);           /* at the next bar */
void bs_cmd_scene(bs_engine_t *e, int s, int quant);  /* queued; also sent over the link */
void bs_cmd_live_slice(bs_engine_t *e, int slice, float vel);
void bs_cmd_live_drum(bs_engine_t *e, int track, float vel);
void bs_cmd_mute(bs_engine_t *e, int track, int toggle_solo);
void bs_cmd_panic(bs_engine_t *e);
void bs_midi_clock_in(bs_engine_t *e, uint32_t t24);  /* following an external clock */

/* ---- main-loop edits (they change patterns in place; the lock hook guards bulk changes) ---- */
void bs_set_lock_hooks(uint32_t (*lock)(void), void (*unlock)(uint32_t));
void bs_mutate(bs_engine_t *e, float amount, uint32_t seed); /* the break track, anchors kept if e->anchors */
int bs_undo(bs_engine_t *e);                                 /* 0 = restored */
void bs_ghosts(bs_engine_t *e, float density, uint32_t seed);/* generates the GHOST track */
void bs_default_patterns(bs_engine_t *e);
void bs_scene_store(bs_engine_t *e, int s);
void bs_set_rhythm(bs_engine_t *e, float t);
void bs_set_kit(bs_engine_t *e, float t);
void bs_set_slices(bs_engine_t *e, int n);
void bs_fx_update(bs_engine_t *e);                           /* after a change of e->bus.m */
void bs_snap(bs_engine_t *e);                                /* BREAK and KIT to their nearest nodes */
const char *bs_kit_step(bs_engine_t *e, int dir);            /* KIT to the next / previous node; its name */
int bs_load_bank(bs_engine_t *e, const uint8_t *bank, uint32_t len);
