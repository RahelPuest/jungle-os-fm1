/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* The break sample engine: sliced breaks read in place (from the flash bank or a RAM buffer), played by a pair of
 * voices that crossfade so a new slice never clicks against the old one.
 *
 * Bank format "JOB1" (docs/development/sample-format.md; written by tools/sample-prep/jo_sample_prep.py):
 *   header 32 B: magic "JOB1", u16 version (1), u16 count, u32 total length, u32 CRC-32 of bytes [32, total),
 *                16 B reserved
 *   count entries of 256 B: name[16], u32 pcm offset (from the bank's start, 4-aligned), u32 frames, u32 rate (Hz),
 *                u16 bpm x 100, u8 bars, u8 nslices, u8 flags, u8 reserved[3], u32 slice start[32] (frames),
 *                u8 slice class[32] (JB_CLASS_*), u8 slice gain[32] (128 = unity), reserved to 256
 *   PCM: signed 16-bit mono little-endian */
#pragma once
#include "../dsp/jo_dsp.h"

#define JB_MAGIC 0x31424F4Au /* "JOB1" */
#define JB_VERSION 1u
#define JB_MAXBREAKS 8
#define JB_MAXSLICES 32
#define JB_HDR 32u
#define JB_ENTRY 256u
enum { JB_CLASS_NONE, JB_CLASS_KICK, JB_CLASS_SNARE, JB_CLASS_HAT, JB_CLASS_OTHER };

typedef struct {
    const int16_t *pcm;
    uint32_t frames, rate;
    float bpm;
    uint8_t bars, nslices;
    uint32_t start[JB_MAXSLICES + 1]; /* start[nslices] = frames */
    uint8_t cls[JB_MAXSLICES];
    uint8_t gain[JB_MAXSLICES];
    char name[17];
} jb_break_t;

/* parses and validates a bank image (CRC, bounds of every entry and slice); fills up to max breaks.
 * Returns the count, or a negative error: -1 magic, -2 version, -3 length, -4 CRC, -5 an entry out of bounds */
int jb_bank_parse(const uint8_t *img, uint32_t len, jb_break_t *out, int max);
uint32_t jb_crc32(const uint8_t *p, uint32_t n);
/* even slicing of a break into n (8, 16, 32) slices; keeps classes unknown */
void jb_slice_even(jb_break_t *b, uint32_t n);

/* the original procedural test break (no samples, no third-party audio): one bar at 170 BPM rendered with the
 * drum models into buf (int16, at rate), sliced in 16 with known classes. buf needs frames_for(rate) entries */
uint32_t jb_testbreak_frames(uint32_t rate);
void jb_testbreak_make(jb_break_t *b, int16_t *buf, uint32_t rate);   /* the first: the Amen pattern */
/* (0.7) the built-in breaks, the classic breaks' rhythms played by the drum models: AMEN, THINK, APACHE, FUNKY
 * DRUMMER, HOT PANTS, ROLLER (docs/research/dnb-drums.md); the same RAM and slicing as above */
#define JB_NTESTBREAKS 6
void jb_testbreak_make_n(jb_break_t *b, int16_t *buf, uint32_t rate, uint32_t id);
const char *jb_testbreak_name(uint32_t id);

/* ------------------------------------------------------------------ player --- */
typedef struct {
    uint8_t active, reverse;
    float pos, rate;           /* in source frames */
    float lo, hi;              /* the slice's bounds */
    float loop_lo, loop_len;   /* microloop: active when loop_len > 0 */
    float gain, env, env_step; /* fade in / out */
    float fade_out_at;         /* frames left in the slice when the fade-out starts */
    uint8_t releasing;
} jb_voice_t;

typedef struct {
    const jb_break_t *brk;
    jb_voice_t v[2];
    uint8_t cur;
    float pitch;               /* semitones, applied to every new slice */
    float tempo_ratio;         /* bpm now / the break's bpm (repitch to tempo); 1 = off */
    float gate;                /* 0..1 of the slice's length; 1 = play to its end */
    float level;
} jb_player_t;

void jb_player_init(jb_player_t *p, const jb_break_t *b);
/* plays slice i (wrapped into the break's slices) at velocity vel; reverse; pitch in semitones added to the
 * player's; gain 0..1 */
void jb_play(jb_player_t *p, uint32_t slice, float vel, int reverse, float semis);
/* microloop of the playing slice: len_frames (source frames) from the current position; 0 ends it */
void jb_microloop(jb_player_t *p, float len_frames);
void jb_release(jb_player_t *p);   /* a short fade to silence */
/* adds n samples (mono) into out */
void jb_render(jb_player_t *p, float *out, uint32_t n);
