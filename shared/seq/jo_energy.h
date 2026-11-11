/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* (0.6) ENERGY: one intensity 0..1 that the steps' levels, the sound and the link follow, and its curve over a
 * phrase. The curve is recorded per beat (up to 16 bars = 64 beats) while ENERGY is turned with REC CURVE on, then
 * played back every phrase until cleared. Header-only, main loop or render (single bytes and floats). */
#pragma once
#include "jo_seq.h"

#define JE_BEATS 64u
typedef struct {
    float value;               /* the energy now, 0..1 (the knob, or the curve when it plays) */
    float knob;                /* the knob's own value */
    uint8_t lane[JE_BEATS];    /* the recorded curve, 0..255 per beat */
    uint8_t beats;             /* the curve's length in beats (phrase * 4), 0: no curve */
    uint8_t recording, playing;
} je_energy_t;

static void je_init(je_energy_t *e) { e->value = e->knob = 1.0f; e->beats = 0; e->recording = e->playing = 0; }
/* the steps' gate: energy 0..1 to the sequencer's level 1..4 (0..0.25 -> 1, ... 0.75..1 -> 4) */
static uint8_t je_level(float v) { return (uint8_t)(v < 0.25f ? 1 : v < 0.5f ? 2 : v < 0.75f ? 3 : 4); }
/* once a 16th: records the knob into the curve, or plays the curve; abs_step the global 16th, phrase in bars */
static void je_step(je_energy_t *e, uint32_t abs_step, uint32_t phrase)
{
    uint32_t n = phrase * 4u > JE_BEATS ? JE_BEATS : phrase * 4u, beat = (abs_step >> 2) % (n ? n : 1u);
    if (e->recording) {
        e->lane[beat] = (uint8_t)(e->knob * 255.0f + 0.5f);
        e->beats = (uint8_t)n;
        e->value = e->knob;
        if (beat == n - 1u && (abs_step & 3u) == 3u) e->recording = 0, e->playing = 1; /* one phrase recorded */
    } else if (e->playing && e->beats) {
        e->value = (float)e->lane[beat % e->beats] * (1.0f / 255.0f);
    } else
        e->value = e->knob;
}
