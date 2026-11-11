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
#include "../../../shared/seq/jo_energy.h"

enum { AB_T_BASS, AB_T_STAB, AB_T_PAD, AB_NTRACKS };
enum { AB_PART_BASS, AB_PART_STAB, AB_PART_PAD, AB_NPARTS };
extern const char *const ab_track_name[AB_NTRACKS];
#define AB_NPATTERNS 4
#define AB_NSCENES 8
#define AB_BLOCK_MAX 256u
#define AB_POOL_SAMPLES (2u * JF_DELAY_LEN + JF_VERB_TOTAL) /* int16: the delay's two lines and the reverb */
enum { AB_LK_MORPH, AB_LK_BITE, AB_LK_MOTION, AB_LK_OCT, AB_NLOCKS }; /* bass-step locks */
enum { AB_VOICE_FULL, AB_VOICE_NO_PAD, AB_VOICE_BASS_ONLY };         /* voice budget modes (CPU degradation) */

/* (0.4) CALL & RESPONSE, (0.5) VARIATIONS and FORM: four variations of the bass, A B C D. A is the bass as it is
 * (the pattern's line, MORPH / EXPLORE / MUTATE); B, C and D answer it, each with its own line and sound. The FORM
 * plays them bar by bar on the 4-bar grid (OFF: the variation picked, var_sel); CALL & RESPONSE is FORM A B A C */
#define AB_CR_BARS 4
#define AB_NVAR 4
enum { AB_FORM_OFF, AB_FORM_ABAC, AB_FORM_AAAB, AB_FORM_ABAB, AB_FORM_ABCD, AB_FORM_AABB, AB_NFORMS };
extern const char *const ab_form_name[AB_NFORMS];
extern const uint8_t AB_FORMS[AB_NFORMS][AB_CR_BARS];
extern const uint8_t AB_CR_FORM[AB_CR_BARS];      /* {0, 1, 0, 2}: A B A C */
/* (0.5) the performance FX (held; AB_FX_NONE when none) */
enum { AB_FX_NONE, AB_FX_OCT_UP, AB_FX_OCT_DOWN, AB_FX_DIVE, AB_FX_WOBBLE, AB_FX_STUTTER, AB_FX_TAPE, AB_FX_BITE,
       AB_FX_SUB_ONLY, AB_NFX };
extern const char *const ab_fx_name[AB_NFX];
/* (0.5) the fills: ROLLS (the bass rolls, octave jumps on the rolls), OCTAVES, DROP OUT (the bass and sub silent),
 * SLIDE DOWN (every note slides down the scale) */
enum { AB_FILL_ROLLS, AB_FILL_OCTAVES, AB_FILL_DROP, AB_FILL_SLIDE, AB_NFILLS };
extern const char *const ab_fill_name[AB_NFILLS];
/* (0.5) the library: bass lines (scale-aware) and chord rhythms for STAB and PAD. (0.7) The bass's entries are
 * STYLES: a sound, a line (16 or 32 steps, accents, slides, octave jumps), a FORM and the answers' sounds, the way
 * the drum & bass archetype is played (docs/research/dnb-bass-styles.md); ab_style_hint says how in one line */
#define AB_NLINES 13
#define AB_NRHYTHMS 5
extern const char *const ab_line_name[AB_NLINES];
extern const char *const ab_style_hint[AB_NLINES];
extern const char *const ab_stab_rhythm_name[AB_NRHYTHMS];
extern const char *const ab_pad_rhythm_name[AB_NRHYTHMS];
/* (0.6) DUET: ANSWER ME's transforms of the bar you played */
enum { AB_ANS_ECHO, AB_ANS_INVERT, AB_ANS_RETRO, AB_ANS_DISPLACE, AB_ANS_OCT_DOWN, AB_ANS_SPARSE, AB_NANS };
extern const char *const ab_answer_name[AB_NANS];
/* (0.6) the ENERGY curve's modes */
enum { AB_CURVE_OFF, AB_CURVE_REC, AB_CURVE_PLAY, AB_NCURVES };
extern const char *const ab_curve_name[AB_NCURVES];
#define AB_NGEST 48
typedef struct { uint16_t at, end; uint8_t type, val; } ab_gest_t; /* type 0: a performance FX (val), 1..4: knob 0..3 */
typedef struct {
    int8_t line;   /* the pattern whose bass line plays (0..AB_NPATTERNS-1), -1: A's line */
    int8_t node;   /* the sweetspot node it sounds as, -1: A's sound */
} ab_resp_t;

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
    uint8_t cr_on;                     /* a FORM plays (form != AB_FORM_OFF; kept for 0.4's callers) */
    uint8_t cr_play;                   /* the variation sounding: 0 A .. 3 D (the render's) */
    ab_resp_t cr[AB_NVAR - 1];         /* B, C and D */
    uint8_t form;                      /* AB_FORM_* (main loop writes, render reads) */
    uint8_t var_sel, var_next;         /* FORM OFF: the variation playing, and the one queued for var_at */
    uint32_t var_at;
    /* (0.5) fills: the type, from fill_start to fill_end (16ths); seq.fill follows */
    uint8_t fill_type;
    uint32_t fill_start;
    /* (0.5) link helpers: LOCK TO KICK (the bass plays on the link's kicks), FILL ANSWERS (a link fill's bars play
     * variation C) */
    uint8_t lock_kick, fill_answers, answer_on;
    uint32_t answer_start, answer_end;
    uint8_t phrase;                    /* 4, 8, 16 bars */
    uint8_t quant;                     /* patterns and variations: 0 next bar, 1 the phrase's end */
    /* (0.5) the performance FX */
    uint8_t perf, perf_was;
    uint32_t perf_n;                   /* samples since the FX began */
    uint8_t stutter_have;
    js_step_t stutter_step;
    float tape_bass, tape_sub, tape_gain;
    float glide_amt;                   /* GLIDE: 0.5 = the node's glide */
    float humanize;                    /* HUMANIZE 0..1: velocity and microtiming offsets in the playback */
    /* (0.6) ENERGY: the knob or its curve gates the steps' levels (seq.energy) and moves the sound a little; over the
     * link both ways (CC 109). midi_out: where the engine sends (the app: USB; 0: nowhere) */
    je_energy_t en;
    uint8_t en_armed;                  /* curve REC: recording starts at the next phrase's start */
    uint8_t en_sent;                   /* the last ENERGY value sent (0..127, 0xFF: none) */
    void (*midi_out)(uint32_t pkt);
    /* (0.6) DUET. ANSWER ME: the bar you played (cap_*), answered the next bar (ans_*) */
    uint8_t duet_answer, duet_xform;
    uint8_t cap_note[16], cap_vel[16], cap_n;
    uint32_t cap_bar;
    uint8_t ans_note[16], ans_vel[16], ans_valid;
    uint32_t ans_bar;
    /* POCKET: the kicks of the last bar (from the link's kick triggers) and the drums' DENSITY (CC 112) */
    float pocket_amt, drum_density;
    uint16_t kick_cur, kick_next, kick_mask;
    uint32_t kick_bar;
    uint8_t pocket_carry_on, pocket_last;
    js_step_t pocket_carry;
    /* GESTURE ECHO: one phrase's performance FX and knob moves, played back the next phrase at half depth */
    uint8_t gesture;
    ab_gest_t g_rec[AB_NGEST], g_play[AB_NGEST];
    uint8_t g_nrec, g_nplay, perf_user, perf_echo, g_last_user;
    uint16_t perf_echo_end;
    float g_base_rec[4], g_base_play[4], echo_off[4];
    /* (0.6) TAPE DECAY: what REC records fades a little each bar (ab_decay_tick) */
    uint8_t decay_on;
    float decay_amt;
    uint32_t decay_bar;
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
void ab_cmd_fill(ab_engine_t *e, int bars);      /* a 1- or 2-bar fill (the bass rolls), as the link's */
void ab_cmd_return(ab_engine_t *e);              /* RETURN: SNAP, the mutation and the fill ended */

void ab_set_morph(ab_engine_t *e, float t);
int ab_snap(ab_engine_t *e);                     /* MORPH to the nearest node; returns its index */
int ab_explore(ab_engine_t *e, int dir);         /* the next / previous neighbour of the nearest node */
void ab_mutate(ab_engine_t *e, float radius, uint32_t seed);
int ab_revert(ab_engine_t *e);                   /* the last mutation undone */
void ab_master_update(ab_engine_t *e);
void ab_scene_store(ab_engine_t *e, int s);
void ab_default_patterns(ab_engine_t *e);
const char *ab_node_name(const ab_engine_t *e);  /* the nearest node's name */
int ab_node_find(const ab_engine_t *e, const char *name); /* (0.7) a sound by name, or -1 */
/* CALL & RESPONSE: the part of the form (0 A, 1 B, 2 C) that 16th abs plays */
int ab_cr_part(const ab_engine_t *e, uint32_t abs);
void ab_set_cr(ab_engine_t *e, int on);          /* FORM A B A C on, or FORM OFF */
/* (0.5) */
void ab_set_form(ab_engine_t *e, int form);
void ab_cmd_variation(ab_engine_t *e, int v);    /* FORM OFF, variation v from the next bar (or the phrase's end) */
/* a fill of type t, bars 1 / 2; at_phrase_end: in the phrase's last bar(s), else from the next bar */
void ab_cmd_fill_type(ab_engine_t *e, int t, int bars, int at_phrase_end);
void ab_set_perf(ab_engine_t *e, int fx);        /* AB_FX_NONE ends it */
void ab_set_phrase(ab_engine_t *e, int bars);
/* the library: line l into the BASS track of the current pattern, or a rhythm r into STAB / PAD */
void ab_load_line(ab_engine_t *e, int l);
void ab_load_rhythm(ab_engine_t *e, int track, int r);
/* DENSITY: one step more (dir > 0) or less on a track of the current pattern, by a fixed ranking (the downbeat goes
 * last, offbeat 16ths first); returns 1 if a step changed */
int ab_density(ab_engine_t *e, int track, int dir);
/* REC: a note (BASS: MIDI note; STAB / PAD: MIDI note, written as the nearest scale degree) into the track's step
 * nearest to now, quantized; returns the step written or -1 (stopped) */
int ab_record(ab_engine_t *e, int track, int note, float vel);
/* the sound state UNDO saves with a pattern (shared/seq/jo_undo.h's extra blob, <= JU_EXTRA bytes) */
typedef struct {
    float v[JN_NPARAMS];
    float morph;                       /* (0.7) and what a STYLE sets: MORPH, the FORM, B C D's sounds */
    int8_t explore;
    uint8_t mutated, form;
    int8_t var_node[AB_NVAR - 1];
} ab_sound_state_t;
void ab_sound_get(const ab_engine_t *e, ab_sound_state_t *s);
void ab_sound_set(ab_engine_t *e, const ab_sound_state_t *s);
/* (0.6) ENERGY: the knob (main loop; sends CC 109 when its value changes), the curve (AB_CURVE_*), the levels of a
 * track's steps from their musical role (downbeat and backbeat 1, other hits 2, soft hits 3, ratchets and offbeat
 * 16ths 4) */
void ab_energy_set(ab_engine_t *e, float v);
void ab_energy_curve(ab_engine_t *e, int mode);
int ab_energy_curve_mode(const ab_engine_t *e);
void ab_auto_levels(ab_engine_t *e, int track);
/* (0.6) DUET */
void ab_set_answer(ab_engine_t *e, int on);
void ab_gesture_knob(ab_engine_t *e, int k, float v);  /* a knob move for GESTURE ECHO (0 CHAR 1 MOTION 2 BITE 3 GLIDE) */
uint16_t ab_pocket_mask(const ab_engine_t *e, uint32_t bar); /* the kick positions POCKET avoids in bar */
/* (0.6) TAPE DECAY: once a bar while playing, every decaying step of the playing pattern fades; returns 1 when it
 * ran (the main loop, under the render's lock) */
int ab_decay_tick(ab_engine_t *e);
