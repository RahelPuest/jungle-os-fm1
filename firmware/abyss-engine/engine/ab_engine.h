/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* ABYSS ENGINE's engine: neuro bass (FM CREATURE / REESE MATRIX) through the sweetspot graph, the clean sub,
 * stabs, the pad, a harmonic sequencer on a shared key, ducking from the link's kicks, NEURO PRESSURE.
 * Platform-free (the app and the host tests drive it). Threading as Break Surgeon's: ab_render and
 * ab_link_packet in the audio interrupt, everything else in the main loop through the command ring. */
#pragma once
#include "../../../shared/seq/jo_seq.h"
#include "../../../shared/seq/jo_cmdq.h"
#include "../../../shared/synth/jo_synth.h"
#include "../../../shared/fx/jo_fx.h"
#include "../../../shared/sweetspot/jo_graph.h"
#include "../../../shared/link/jo_link.h"

enum { AB_T_BASS, AB_T_STAB, AB_T_PAD, AB_NTRACKS };
enum { AB_PART_BASS, AB_PART_STAB, AB_PART_PAD, AB_NPARTS };
extern const char *const ab_track_name[AB_NTRACKS];
#define AB_NPATTERNS 4
#define AB_NSCENES 8
#define AB_BLOCK_MAX 256u
#define AB_POOL_SAMPLES (2u * JF_DELAY_LEN + JF_VERB_TOTAL) /* int16: the delay's two lines and the reverb */
enum { AB_LK_MORPH, AB_LK_BITE, AB_LK_MOTION, AB_LK_OCT, AB_NLOCKS }; /* bass-step locks */
enum { AB_VOICE_FULL, AB_VOICE_NO_PAD, AB_VOICE_BASS_ONLY };         /* voice budget modes (CPU degradation) */

typedef struct {
    uint8_t used, pattern, mute, pad_on;
    float morph, motion, bite, space;
    jf_neuro_macros_t fx;
    int8_t root;
    uint8_t scale;
} ab_scene_t;

typedef struct {
    /* voices */
    jn_bass_t bass;
    jn_patch_t patch;
    jn_sub_t sub;
    jn_stab_t stab;
    jn_stab_patch_t stab_p;
    jn_pad_t pad;
    jn_pad_patch_t pad_p;
    uint8_t pad_on, voice_mode;
    /* the sweetspot graph of the neuro bass */
    jg_graph_t fm;
    jg_eval_t fv;
    jg_history_t hist;
    float morph, motion, bite, space;  /* the PERFORM macros, 0..1 */
    float morph_s;                     /* the morph as the render smooths it */
    int16_t morph_lock;                /* a step's MORPH lock, -1 none */
    float bite_lock;                   /* a step's BITE lock, < 0 none */
    int8_t explore_node;               /* EXPLORE: a node held instead of the path, -1 none */
    float mutate_v[JG_MAXPARAMS];      /* a mutation applied over the path's vector */
    uint8_t mutated;
    /* harmony */
    int8_t root;                       /* MIDI note of the key's root, e.g. 38 = D2 */
    uint8_t scale, chord, inversion;
    float tension;
    /* sequencing */
    js_pattern_t pat[AB_NPATTERNS];
    uint8_t cur_pat, next_pat;
    js_clock_t clock;
    js_seq_t seq;
    js_queue_t q;
    jo_cmdq_t cmd;
    uint32_t bass_gate_left, stab_gate_left;
    uint8_t bass_on;
    float last_bass_note;
    uint32_t pending_at;
    int8_t scene_pending, scene_cur;
    ab_scene_t scene[AB_NSCENES];
    uint8_t fill_on;
    uint32_t fill_end;
    float energy;
    /* master */
    jf_neuro_t master;
    jf_delay_t delay;
    jf_verb_t verb;
    jf_duck_t duck;
    float duck_depth;
    uint8_t duck_internal;             /* standalone: duck on the beat when no link is heard */
    float b_sub[AB_BLOCK_MAX], b_bass[AB_BLOCK_MAX], b_l[AB_BLOCK_MAX], b_r[AB_BLOCK_MAX], b_send[AB_BLOCK_MAX],
        b_duck[AB_BLOCK_MAX];
    /* link (receiver) */
    jl_link_t link;
    uint8_t follow_link;               /* follow the link's clock and transport */
    uint32_t ms, ms_rem;
    uint32_t link_kicks;
    uint32_t last_clock_ms;            /* the last MIDI clock: 500 ms without one = back to the internal clock */
    /* diagnostics; trace: every fired event (the tests; 0 on the device) */
    void (*trace)(void *ctx, const js_event_t *ev);
    void *trace_ctx;
    uint32_t blocks;
    float peak;
} ab_engine_t;

/* pool: AB_POOL_SAMPLES int16 for the delay and the reverb */
void ab_init(ab_engine_t *e, int16_t *pool);
void ab_render(ab_engine_t *e, int32_t *lr, uint32_t n, float out_gain);
/* a MIDI packet in the audio interrupt: the link (clock, transport, kicks, scenes); t24 its arrival */
void ab_link_packet(ab_engine_t *e, uint32_t pkt, uint32_t t24);
void ab_link_kick(ab_engine_t *e, uint8_t vel);  /* (a kick by hand, or from the tests) */

void ab_cmd_play(ab_engine_t *e, int on);
void ab_cmd_bpm(ab_engine_t *e, float bpm);
void ab_cmd_note(ab_engine_t *e, int part, int note, float vel, int on); /* the keyboard */
void ab_cmd_stab_chord(ab_engine_t *e, float vel);                       /* the current chord, now */
void ab_cmd_pad(ab_engine_t *e, int on);                                 /* hold / release the pad */
void ab_cmd_pattern(ab_engine_t *e, int p);
void ab_cmd_scene(ab_engine_t *e, int s, int quant);
void ab_cmd_panic(ab_engine_t *e);
void ab_cmd_mute(ab_engine_t *e, int track);

void ab_set_morph(ab_engine_t *e, float t);
int ab_snap(ab_engine_t *e);                     /* MORPH to the nearest node; returns its index */
int ab_explore(ab_engine_t *e, int dir);         /* the next / previous neighbour of the nearest node */
void ab_mutate(ab_engine_t *e, float radius, uint32_t seed);
int ab_revert(ab_engine_t *e);                   /* the last mutation undone */
void ab_master_update(ab_engine_t *e);
void ab_scene_store(ab_engine_t *e, int s);
void ab_default_patterns(ab_engine_t *e);
const char *ab_node_name(const ab_engine_t *e);  /* the nearest node's name */
