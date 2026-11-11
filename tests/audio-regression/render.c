/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* The host audio simulator's scenarios: each renders the engines exactly as the FM-1's render calls them
 * (128-frame blocks at 44,117.6 Hz), writes a WAV, measures it, and compares a hash of the 16-bit output with
 * audio-regression/golden.txt (the first run writes it; JO_REGOLD=1 rewrites it after an intended change).
 *   render OUTDIR GOLDEN */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "../../firmware/break-surgeon/engine/bs_engine.h"
#include "../../firmware/abyss-engine/engine/ab_engine.h"

#define BLOCK 128u
#define SR_FILE 44118u

typedef struct {
    const char *name;
    float peak, rms, dc;
    uint32_t nans, clips, frames;
    uint32_t hash;
    double us_per_block; /* host CPU time per block (not the FM-1's) */
} result_t;

static void wav_write(const char *path, const int16_t *lr, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t data = frames * 4u, v;
    uint16_t s;
    if (!f) { perror(path); exit(2); }
    fwrite("RIFF", 1, 4, f); v = 36u + data; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
    v = SR_FILE; fwrite(&v, 4, 1, f); v = SR_FILE * 4u; fwrite(&v, 4, 1, f);
    s = 4; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(lr, 4, frames, f);
    fclose(f);
}

static void measure(result_t *r, const int32_t *lr24, int16_t *lr16, uint32_t frames)
{
    double sum = 0.0, sq = 0.0;
    uint32_t i, h = 2166136261u;
    r->peak = 0.0f;
    r->clips = r->nans = 0;
    for (i = 0; i < frames * 2u; i++) {
        float x = (float)lr24[i] * (1.0f / 8388608.0f);
        int16_t q = (int16_t)(lr24[i] >> 8);
        if (fabsf(x) > r->peak) r->peak = fabsf(x);
        if (fabsf(x) >= 0.999f) r->clips++;
        sum += (double)x;
        sq += (double)x * (double)x;
        lr16[i] = q;
        h = (h ^ (uint16_t)q) * 16777619u;
    }
    r->frames = frames;
    r->rms = (float)sqrt(sq / (double)(frames * 2u));
    r->dc = (float)(sum / (double)(frames * 2u));
    r->hash = h;
}

static bs_engine_t BS;
static ab_engine_t AB;
static int16_t testpcm[70000], roombuf[JF_VERB_TOTAL];
static int16_t abpool[AB_POOL_SAMPLES];
static int32_t lr24[44118 * 2 * 52];
static int16_t lr16[44118 * 2 * 52];

/* runs fn(block index) before each block, rendering `seconds` of e */
typedef void (*script_t)(uint32_t blk);
static void run_bs(result_t *r, float seconds, script_t script)
{
    uint32_t frames = (uint32_t)(seconds * JO_SR), blk = 0, done = 0;
    clock_t c0 = clock();
    while (done + BLOCK <= frames) {
        if (script) script(blk);
        bs_render(&BS, lr24 + 2u * done, BLOCK, 0.9f);
        done += BLOCK;
        blk++;
    }
    r->us_per_block = (double)(clock() - c0) * 1e6 / CLOCKS_PER_SEC / (double)blk;
    measure(r, lr24, lr16, done);
}
static void run_ab(result_t *r, float seconds, script_t script)
{
    uint32_t frames = (uint32_t)(seconds * JO_SR), blk = 0, done = 0;
    clock_t c0 = clock();
    while (done + BLOCK <= frames) {
        if (script) script(blk);
        ab_render(&AB, lr24 + 2u * done, BLOCK, 0.9f);
        done += BLOCK;
        blk++;
    }
    r->us_per_block = (double)(clock() - c0) * 1e6 / CLOCKS_PER_SEC / (double)blk;
    measure(r, lr24, lr16, done);
}

/* blocks from seconds */
#define AT(s) ((uint32_t)((s) * JO_SR / (float)BLOCK))

/* A: 174 BPM, the break with synthesized kick / snare / hats; a one-bar fill at 4 s, then back */
static void scen_a(uint32_t b)
{
    if (b == 0) { bs_cmd_bpm(&BS, 174.0f); bs_cmd_play(&BS, 1); }
    if (b == AT(4.0f)) bs_cmd_fill(&BS, 1);
    if (b == AT(9.0f)) bs_cmd_return(&BS);
}
/* B: mutate the break with anchors kept, undo, then a quantized scene change */
static void scen_b(uint32_t b)
{
    if (b == 0) { bs_cmd_bpm(&BS, 174.0f); bs_cmd_play(&BS, 1); }
    if (b == AT(3.0f)) bs_mutate(&BS, 0.7f, 1234);
    if (b == AT(6.0f)) bs_undo(&BS);
    if (b == AT(7.0f)) bs_cmd_scene(&BS, 3, BS_Q_BAR);
    if (b == AT(10.0f)) bs_cmd_scene(&BS, 5, BS_Q_BAR);
}
/* the performance modes in turn, each held a bar and a half */
static void scen_perf(uint32_t b)
{
    uint32_t k;
    if (b == 0) { bs_cmd_bpm(&BS, 172.0f); bs_cmd_play(&BS, 1); }
    for (k = 1; k < BS_NPERF; k++) {
        if (b == AT(1.0f + 2.0f * (float)k)) bs_cmd_perf(&BS, (int)k);
        if (b == AT(2.4f + 2.0f * (float)k)) bs_cmd_perf(&BS, BS_PERF_CLEAN);
    }
}
/* the kit graph swept end to end while playing */
static void scen_kitsweep(uint32_t b)
{
    if (b == 0) { bs_cmd_bpm(&BS, 174.0f); bs_cmd_play(&BS, 1); }
    bs_set_kit(&BS, (float)b / (float)AT(12.0f));
    if (b == AT(6.0f)) { BS.bus.m.pressure = 1.0f; BS.bus.m.crush = 0.8f; bs_fx_update(&BS); }
}

/* (0.4) the groove library, each groove two bars, in the order of bs_groove_name */
static void scen_grooves(uint32_t b)
{
    uint32_t two = AT(2.0f * 4.0f * 60.0f / 174.0f), g = two ? b / two : 0u;
    if (b == 0) { bs_cmd_bpm(&BS, 174.0f); bs_cmd_play(&BS, 1); }
    if (two && b % two == 0 && g < BS_NGROOVES) bs_groove(&BS, (int)g);
}
/* (0.7) the built-in breaks (AMEN, THINK, APACHE, FUNKY DRUMMER, HOT PANTS, ROLLER) under P1 DROP, two bars each */
static void scen_breaks(uint32_t b)
{
    uint32_t two = AT(2.0f * 4.0f * 60.0f / 174.0f), g = two ? b / two : 0u;
    if (b == 0) { bs_cmd_bpm(&BS, 174.0f); bs_cmd_play(&BS, 1); }
    if (two && b % two == 0 && g > 0u && g < JB_NTESTBREAKS) bs_set_test_break(&BS, (int)g);
}
/* C: FM growl over the clean sub, stabs over a held pad */
static void scen_c(uint32_t b)
{
    if (b == 0) { ab_cmd_bpm(&AB, 174.0f); ab_cmd_play(&AB, 1); ab_cmd_pad(&AB, 1); }
    if (b == AT(4.0f)) ab_cmd_stab_chord(&AB, 0.9f);
    if (b == AT(5.5f)) ab_cmd_stab_chord(&AB, 0.9f);
}
/* D: kick-linked ducking: link kicks every beat into the sub */
static void scen_d(uint32_t b)
{
    uint32_t beat = AT(60.0f / 174.0f);
    if (b == 0) { ab_cmd_bpm(&AB, 174.0f); ab_cmd_play(&AB, 1); AB.duck_depth = 0.7f; }
    if (b > 0 && beat && b % beat == 0) ab_link_kick(&AB, 120);
}
/* E: MORPH through the FM creature's curated nodes */
static void scen_e(uint32_t b)
{
    if (b == 0) { ab_cmd_bpm(&AB, 174.0f); ab_cmd_play(&AB, 1); }
    ab_set_morph(&AB, (float)b / (float)AT(16.0f));
}
/* F (0.4): CALL & RESPONSE, the bass's form A B A C twice: A the pattern as it is, B its line as REESE, C P2's line
 * as LASER */
static void scen_f(uint32_t b)
{
    if (b == 0) { ab_cmd_bpm(&AB, 174.0f); ab_set_cr(&AB, 1); ab_cmd_play(&AB, 1); }
}
/* pathological settings: every macro at its extremes, resonance and feedback up */
static void scen_abuse(uint32_t b)
{
    if (b == 0) { ab_cmd_bpm(&AB, 190.0f); ab_cmd_play(&AB, 1); ab_cmd_pad(&AB, 1); }
    ab_set_morph(&AB, (b / 37u) & 1u ? 1.0f : 0.0f);
    AB.motion = 1.0f; AB.bite = 1.0f; AB.space = 1.0f;
    AB.master.m.dirt = 1.0f; AB.master.m.pressure = 1.0f; AB.master.m.weight = 1.0f;
    if (b % 50u == 0) { jf_neuro_update(&AB.master); ab_cmd_stab_chord(&AB, 1.0f); }
}

typedef struct { const char *name; int which; float seconds; script_t s; float peak_max; } scen_t;
static const scen_t SC[] = {
    {"A_break_drums_fill_174", 0, 12.0f, scen_a, 0.95f},
    {"B_mutate_undo_scene", 0, 13.0f, scen_b, 0.95f},
    {"perf_modes", 0, 15.0f, scen_perf, 0.95f},
    {"kit_sweep_pressure", 0, 12.0f, scen_kitsweep, 0.95f},
    {"grooves", 0, 49.6f, scen_grooves, 0.95f},
    {"breaks", 0, 16.5f, scen_breaks, 0.95f},
    {"C_growl_sub_stab_pad", 1, 8.0f, scen_c, 0.95f},
    {"D_kick_ducking", 1, 6.0f, scen_d, 0.95f},
    {"E_morph_fm_nodes", 1, 16.0f, scen_e, 0.95f},
    {"abuse_extremes", 1, 8.0f, scen_abuse, 0.95f},
    {"F_call_response", 1, 11.3f, scen_f, 0.95f},
};

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "build/renders";
    const char *golden = argc > 2 ? argv[2] : 0;
    char path[512], line[256];
    uint32_t i, k, ng = 0, nsc = sizeof SC / sizeof SC[0], hashes[32];
    int fails = 0, regold = getenv("JO_REGOLD") != 0;
    struct { char n[64]; uint32_t h; } gold[32];
    FILE *g;
    snprintf(path, sizeof path, "mkdir -p %s", out);
    if (system(path)) return 2;
    if (golden && !regold && (g = fopen(golden, "r"))) {
        while (ng < 32 && fgets(line, sizeof line, g))
            if (sscanf(line, "%63s %x", gold[ng].n, &gold[ng].h) == 2) ng++;
        fclose(g);
    }
    printf("%-26s %7s %7s %8s %5s %5s %9s %8s\n", "scenario", "peak", "rms", "dc", "nan", "clip", "hash", "us/blk");
    for (i = 0; i < nsc; i++) {
        result_t r;
        int bad = 0;
        if (SC[i].which == 0) { bs_init(&BS, testpcm, 0, 0); bs_set_room_buf(&BS, roombuf); run_bs(&r, SC[i].seconds, SC[i].s); }
        else { ab_init(&AB, abpool); run_ab(&r, SC[i].seconds, SC[i].s); }
        snprintf(path, sizeof path, "%s/%s.wav", out, SC[i].name);
        wav_write(path, lr16, r.frames);
        /* the acceptance bounds: under the ceiling, no clipped or non-finite samples, no DC, not silent */
        if (r.peak > SC[i].peak_max || r.clips || r.nans || fabsf(r.dc) > 0.01f || r.rms < 0.005f) bad = 1;
        for (k = 0; k < ng; k++)
            if (!strcmp(gold[k].n, SC[i].name) && gold[k].h != r.hash) {
                printf("  changed against the golden hash %08x\n", gold[k].h);
                bad = 1;
            }
        printf("%-26s %7.3f %7.3f %8.5f %5u %5u  %08x %8.1f %s\n", SC[i].name, (double)r.peak, (double)r.rms,
               (double)r.dc, r.nans, r.clips, r.hash, r.us_per_block, bad ? "FAIL" : "ok");
        hashes[i] = r.hash;
        fails += bad;
    }
    if (golden && (regold || !ng) && (g = fopen(golden, "w"))) {
        for (i = 0; i < nsc; i++) fprintf(g, "%s %08x\n", SC[i].name, hashes[i]);
        fclose(g);
        printf("golden written: %s\n", golden);
    }
    printf("budget: one block = %.0f us of audio at 44,117.6 Hz\n", 1e6 * BLOCK / 44117.647);
    return fails ? 1 : 0;
}
