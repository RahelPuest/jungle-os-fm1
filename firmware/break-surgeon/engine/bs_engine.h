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
#include "../../../shared/seq/jo_energy.h"
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
       BS_PERF_TAPESTOP, BS_NPERF };
extern const char *const bs_perf_name[BS_NPERF];
enum { BS_LOCK_OFF, BS_LOCK_LAYER, BS_LOCK_SEPARATE }; /* BREAK-LOCK */
enum { BS_Q_NOW, BS_Q_BEAT, BS_Q_BAR, BS_Q_2BAR, BS_Q_4BAR, BS_Q_PHRASE }; /* PHRASE: the end of the phrase (0.5) */
/* (0.5) the fills: ROLL (the break chops up, the snare rolls in the second half), SNARE BUILD (the snare on every
 * 16th, building; the kick out), DROP OUT (break, kick, snare and ghosts out, the hats left; the impact on the
 * drop), CHOP (the break cut up hard, ratchets and reverses; the drums as they are) */
enum { BS_FILL_ROLL, BS_FILL_BUILD, BS_FILL_DROP, BS_FILL_CHOP, BS_NFILLS };
extern const char *const bs_fill_name[BS_NFILLS];
/* (0.5) FORM: the variations A-D (the patterns P1-P4) bar by bar on the global 4-bar grid */
#define BS_NFORMS 6
extern const char *const bs_form_name[BS_NFORMS];  /* OFF, A B A C, A A A B, A B A B, A B C D, A A B B */
extern const uint8_t bs_form[BS_NFORMS][4];

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
    uint8_t undo_valid, undo_pattern, undo_all; /* undo_all: a groove was loaded (the whole pattern back) */
    uint8_t cur_pat, next_pat;         /* next_pat != cur_pat: a switch at the bar */
    js_clock_t clock;
    js_seq_t seq;
    js_queue_t q;
    jo_cmdq_t cmd;
    /* performance */
    uint8_t perf, perf_slice, roll_div; /* the held mode; ROLL's slice; 1 = 1/16, 3 = 1/32 */
    uint8_t fill_on, fill_bars, fill_pending, return_pending, breaklock, anchors;
    uint8_t fill_type, fill_sent;      /* (0.5) BS_FILL_*; the link's FILL_BEGIN sent */
    uint8_t form, phrase;              /* (0.5) the FORM (0 off); the phrase in bars (4, 8, 16) */
    float tape;                        /* (0.5) TAPE STOP: the speed, 1 .. 0 */
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
    /* (0.7) the break under programmed drums: its low cut (eased in while the synthesized kick plays) and its duck
     * under each synthesized kick */
    jo_onepole_t brk_lp;
    float brk_lo_cut, brk_duck;
    uint8_t test_id, brk_busy;           /* (0.7) the built-in break (JB_NTESTBREAKS), its regeneration */
#define BS_BREAK_DUCK 0.33f /* -3.5 dB under a full kick */
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
    /* (0.6) ENERGY (jo_energy.h): the steps' levels gate by it; it thins the room and the grit below full, and goes
     * over the link both ways (CC 109). nrg_arm: the curve records from the next phrase's start */
    je_energy_t nrg;
    uint8_t nrg_arm, nrg_cc;           /* nrg_cc: the last value sent or heard (no echo) */
    /* (0.6) DENSITY to the link once a bar: the hits of the bar before */
    uint32_t bar_hits;
    uint8_t density;                   /* the last sent, 0..127 */
    /* (0.6) TAPE DECAY: recorded steps fade (decay_amt / 256 a bar) */
    uint8_t decay_on, decay_amt;
    /* (0.6) DNA: a genome to play from the next bar (copied into the playing pattern slot) */
    const js_pattern_t *gen_next;
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
/* (0.5) a fill of a type (BS_FILL_*), 1 or 2 bars: from the next bar, or (at_phrase_end) in the phrase's last bars */
void bs_cmd_fill_ex(bs_engine_t *e, int bars, int type, int at_phrase_end);
void bs_cmd_pattern_q(bs_engine_t *e, int p, int quant); /* at the next bar, or with BS_Q_PHRASE the phrase's end */
void bs_cmd_form(bs_engine_t *e, int form);               /* FORM from the next bar (0: off) */

/* ---- main-loop edits (they change patterns in place; the lock hook guards bulk changes) ---- */
void bs_set_lock_hooks(uint32_t (*lock)(void), void (*unlock)(uint32_t));
void bs_mutate(bs_engine_t *e, float amount, uint32_t seed); /* the break track, anchors kept if e->anchors */
int bs_undo(bs_engine_t *e);                                 /* 0 = restored */
/* (0.4) the groove library: the classic drum & bass patterns, loaded into the current pattern's BREAK, KICK, SNARE,
 * GHOST and HATS tracks (PERC and IMPACT kept); the break re-chopped to the groove by its slices' classes.
 * Undone by bs_undo */
#define BS_NGROOVES 18
extern const char *const bs_groove_name[BS_NGROOVES];
void bs_groove(bs_engine_t *e, int g);
void bs_ghosts(bs_engine_t *e, float density, uint32_t seed);/* generates the GHOST track */
void bs_default_patterns(bs_engine_t *e);
void bs_scene_store(bs_engine_t *e, int s);
void bs_set_rhythm(bs_engine_t *e, float t);
void bs_set_kit(bs_engine_t *e, float t);
void bs_set_slices(bs_engine_t *e, int n);
/* (0.7) the built-in breaks (without a bank): AMEN, THINK, APACHE, FUNKY DRUMMER, HOT PANTS, ROLLER; main loop only
 * (regenerates the test break's RAM). bs_step_break: the next / previous one (a bank's breaks when one is in);
 * returns its name */
void bs_set_test_break(bs_engine_t *e, int id);
const char *bs_step_break(bs_engine_t *e, int dir);
void bs_fx_update(bs_engine_t *e);                           /* after a change of e->bus.m */
void bs_snap(bs_engine_t *e);                                /* BREAK and KIT to their nearest nodes */
const char *bs_kit_step(bs_engine_t *e, int dir);            /* KIT to the next / previous node; its name */
int bs_load_bank(bs_engine_t *e, const uint8_t *bank, uint32_t len);

/* ---- (0.5) the sequencer's smart help (main loop; the pattern edited in place under the lock hook) ---- */
void bs_set_phrase(bs_engine_t *e, int bars);               /* 4, 8 or 16 */
uint32_t bs_phrase_bar(const bs_engine_t *e);               /* the bar playing, within the phrase (0 ..) */
int bs_form_part(const bs_engine_t *e, uint32_t bar);       /* the variation (pattern) FORM plays in a bar, or -1 */
/* DENSITY: one step more (dir > 0: the most important empty one: downbeats, then 8ths, then 16ths) or less (dir < 0:
 * the least important: 16ths and ghosts first, the downbeat last) on a track of the current pattern; -1: none left */
int bs_density_step(bs_engine_t *e, int track, int dir);
/* HUMANIZE: each on step's velocity and microtiming set a little off its grid value (amount 0..1, deterministic:
 * the same amount gives the same pattern; 0 puts the steps back on the grid) */
void bs_humanize(bs_engine_t *e, int track, float amount);
void bs_mutate_track(bs_engine_t *e, int track, float amount, uint32_t seed); /* BREAK: bs_mutate; drums: moves hits */
/* RECORD: a hit on a track now, written into the step it is nearest to (quantized to the 16th) and played; data =
 * the slice on BREAK. Returns the step written, or -1 (not playing: only played) */
int bs_record(bs_engine_t *e, int track, float vel, int data);
void bs_erase_step(bs_engine_t *e, int track, int step);
void bs_erase_track(bs_engine_t *e, int track);

/* ---- (0.6) ENERGY, ROUTES, DNA, TAPE DECAY ---- */
/* ENERGY 0..1 from the knob (the main loop); following the link's, it is not sent back */
void bs_set_energy(bs_engine_t *e, float v);
void bs_follow_energy(bs_engine_t *e, float v);
/* the curve: 1 arms recording (one phrase from the next phrase start), 2 plays it, 0 off */
void bs_energy_curve(bs_engine_t *e, int mode);
int bs_energy_curve_mode(const bs_engine_t *e);  /* 0 off, 1 armed or recording, 2 playing */
/* AUTO LEVELS: a track of the current pattern gets levels from its musical role: the beats 1 (hats, perc and impact
 * 2), other 8ths 2, ghosts and soft hits 3, ratchets 4, odd 16ths 3 (break, kick, snare) or 4 (the rest) */
void bs_auto_levels(bs_engine_t *e, int track);
/* ROUTES: a live route over tracks (mask of tracks; JS_R_*, or 0xFF to clear) */
void bs_route_live(bs_engine_t *e, uint32_t mask, uint8_t route);
/* a route written into the current pattern's tracks (latched; FORWARD clears it) */
void bs_route_latch(bs_engine_t *e, uint32_t mask, uint8_t route);
/* a drawn CUSTOM route (n steps in order) into the tracks of mask (their order[]; their route is not changed) */
void bs_route_draw(bs_engine_t *e, uint32_t mask, const uint8_t *order, uint32_t n);
/* the step a track plays at its position p (0 .. len-1), with the route now in force */
uint32_t bs_route_at(const bs_engine_t *e, int track, uint32_t p);
/* TAPE DECAY: once a bar (the main loop): the current pattern's fading steps fade */
void bs_decay_bar(bs_engine_t *e);
void bs_burn_step(bs_engine_t *e, int track, int step); /* the step stays (JS_BURN) */

/* DNA: a family tree of genomes (pattern snapshots); breed = a mutated child of the playing one, cross = A's drums with
 * B's break; selecting plays a genome from the next bar (it is copied into the playing pattern slot) */
#define BS_DNA_MAX 16
typedef struct {
    js_pattern_t pat;
    int8_t parent;                     /* -1: the root */
    uint8_t used, keep, gen;           /* gen: the depth in the tree */
    uint16_t born;                     /* the order of birth (culling takes the oldest) */
} bs_genome_t;
typedef struct {
    bs_genome_t g[BS_DNA_MAX];
    int8_t playing;                    /* -1: no tree yet */
    uint16_t births;
} bs_dna_t;
void bs_dna_root(bs_engine_t *e, bs_dna_t *d);             /* a new tree from the current pattern */
int bs_dna_breed(bs_engine_t *e, bs_dna_t *d, float amount, uint32_t seed); /* the child's index, or -1 (full, all kept) */
int bs_dna_cross(bs_engine_t *e, bs_dna_t *d, int a, int b);              /* the child's index, or -1 */
int bs_dna_select(bs_engine_t *e, bs_dna_t *d, int i);     /* 0 = queued */
void bs_dna_keep(bs_dna_t *d, int i);                        /* a star on / off */
int bs_dna_kill(bs_engine_t *e, bs_dna_t *d, int i);       /* the genome and its children; -1 = the root (refused) */
int bs_dna_count(const bs_dna_t *d);
void bs_dna_store(bs_engine_t *e, const bs_dna_t *d, int i, int slot); /* a genome into pattern P1-P4 */

