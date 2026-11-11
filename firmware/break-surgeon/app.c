/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* BREAK SURGEON: the app (screens, controls, storage, MIDI, the bank loader). The engine is engine/bs_engine.c
 * (-O2 unit dsp_o2.c). Part of fm1-base's unity build: gfx.c and announce.c are in scope.
 * The control map is firmware/break-surgeon/docs/MANUAL.md; keep the two in step. */
#include "engine/bs_engine.h"
#include "../../shared/store/jo_store.h"
#include "../../shared/seq/jo_undo.h"
#include "../../shared/ui/jo_ui.c"

/* ------------------------------------------------------------------ state --- */
static bs_engine_t eng PLAT_POOL;
static int16_t test_pcm[31200] PLAT_POOL;   /* the procedural test break at 22,059 Hz (jb_testbreak_frames) */
static int16_t room_buf[JF_VERB_TOTAL] PLAT_POOL; /* DRUM PRESSURE's room: 32 KiB */
static ju_stack_t undo PLAT_POOL;            /* (0.5) UNDO / REDO: 8 pattern snapshots, 27 KiB */
static bs_dna_t dna PLAT_POOL;               /* (0.6) DNA: 16 genomes, 54 KiB */
_Static_assert(sizeof test_pcm / 2u >= 31142u, "the test break fits");

#define SET_MAGIC 0x32534253u               /* "SBS2" (0.5: the phrase, FORM, the fill length) */
#define SET_VERSION 1u
typedef struct {
    float bpm, rhythm_t, kit_t, energy;
    jf_drum_macros_t fx;
    uint8_t clock_src, link_ch, send_clock, send_kicks;
    uint8_t slices, tempo_sync, breaklock, anchors;
    uint8_t scene_quant, theme, link_usb_host, rsv;
    uint8_t phrase, form, fill_bars, decay;  /* (0.5); (0.6) decay: TAPE DECAY 0 off, 1..20 (5 % steps) */
} settings_t;
static settings_t set;
#define PAT_MAGIC 0x31544150u               /* "PAT1" */
#define SCN_MAGIC 0x314E4353u               /* "SCN1" */
static struct { jo_rec_hdr_t h; uint8_t p[sizeof(js_pattern_t) > sizeof(bs_scene_t) * BS_NSCENES ? sizeof(js_pattern_t)
                                                                                    : sizeof(bs_scene_t) * BS_NSCENES]; } io;
_Static_assert(sizeof io <= 3840u, "a store object holds 3,840 bytes");
static uint8_t dirty, load_report;          /* load_report: what was replaced by defaults at boot (bits) */

/* (0.5) the pages: HOME the main page (the track big), SEQ the overview, EDIT SOUND, FX, ARP SONG, GLO SYSTEM */
enum { PG_MAIN, PG_SEQ, PG_SOUND, PG_FX, PG_SONG, PG_SYSTEM, PG_DNA, NPAGES }; /* (0.6) DNA: SEQ's second tap */
static const char *const PAGE_NAME[NPAGES] = {"MAIN", "OVERVIEW", "SOUND", "FX", "SONG", "SYSTEM", "DNA"};
static const char *const SCENE_NAME[BS_NSCENES] = {"INTRO", "BUILD", "DROP", "MUTATION", "VOID", "IMPACT", "SCENE 7", "SCENE 8"};
static const char *const QUANT_NAME[6] = {"NOW", "BEAT", "BAR", "2 BAR", "4 BAR", "PHRASE"};
/* (0.5) the held layers, in the order a held button wins */
enum { L_NONE, L_SEL, L_STORE, L_ERASE, L_FILL, L_VARY, L_SONG, L_FX, L_ROUTE, L_ENERGY }; /* (0.6) SEQ, HOME */
static const char *const LAYER_NAME[10] = {"", "SEL", "STORE", "ERASE", "FILL", "VARY", "SONG", "FX", "ROUTE", "ENERGY"};
static const char *const LOCK_NAME[3] = {"OFF", "LAYER", "SEPARATE"};

static struct {
    uint8_t page, track, step_page, redraw;
    int8_t held_step;                      /* SEQUENCE: the white key held (-1) */
    int8_t groove, groove_of[BS_NPATTERNS]; /* SEQUENCE: the groove last loaded, and into which pattern (-1) */
    uint32_t key_used;                     /* keys whose hold did something (no toggle on release) */
    int8_t hi_macro;                       /* the macro box last turned */
    uint32_t hi_until;
    char msg[2][24];
    uint32_t msg_until;
    float mutate_amt, ghost_density;
    uint32_t mutate_seed;
    ui_in_t in;
    uint32_t last_draw, splash_until;
    /* (0.5) */
    uint8_t rec;                           /* live RECORD on */
    uint8_t undo_kind, undo_slot;          /* the last undo snapshot's kind (edits of one kind coalesce) */
    uint32_t undo_t;
    float humanize;                        /* VARY's HUMANIZE amount */
    uint32_t save_at;                      /* autosave: when (0 none) */
    uint8_t was_running;
    /* (0.6) */
    uint8_t route_target, route_latch;     /* ROUTE: 0 the selected track, 1 BREAK, 2 ALL; latch writes it */
    uint8_t draw[JS_MAXSTEPS], draw_n;     /* ROUTE: the white keys pressed in order this hold */
    uint8_t route_undone;                  /* ROUTE: this hold's undo snapshot taken */
    int8_t cross_a;                        /* DNA: CROSS armed (-1: waiting for A), A (0..15), -2: off */
    uint32_t last_bar;                     /* TAPE DECAY: the bar last faded */
} ui;

/* ------------------------------------------------------------------ MIDI --- */
static void link_out(uint32_t pkt) { plat_midi_send(pkt, TO_USB); }

/* the arrival stamp (see abyss-engine/app.c: the host simulator's stand-in) */
static uint32_t midi_t24(void)
{
#ifdef PLAT_HOST
    return eng.ms * 24000u + (eng.ms_rem * 24000u) / 44118u;
#else
    return plat_midi_in_t24();
#endif
}

static void midi_in_render(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu;
        if ((st & 0xF0u) == 0xB0u) { /* (0.6) the link's ENERGY: followed, not sent back */
            jl_event_t ev;
            if (jl_decode(&eng.link, pkt, eng.ms, &ev) && ev.type == JL_EV_ENERGY) bs_follow_energy(&eng, (float)ev.a * (1.0f / 127.0f));
            continue;
        }
        if (st == 0xF8u) bs_midi_clock_in(&eng, midi_t24());
        else if (eng.clock.source == JS_CLK_MIDI && (st == 0xFAu || st == 0xFCu)) {
            if (st == 0xFAu) js_clock_midi_start(&eng.clock);
            else { js_clock_stop(&eng.clock); js_queue_clear(&eng.q); jb_release(&eng.player); }
        } else if ((st & 0xF0u) == 0x90u && (pkt >> 24)) { /* notes in: live drums (36 kick .. 41) and slices (48..) */
            uint32_t note = (pkt >> 16) & 0x7Fu;
            float vel = (float)(pkt >> 24) * (1.0f / 127.0f);
            if (note >= 36u && note < 36u + BS_NTRACKS - 1u) jd_trigger(&eng.dv[note - 35u], &eng.snd[note - 35u], vel, 0);
            else if (note >= 48u && note < 80u) jb_play(&eng.player, note - 48u, vel, 0, 0.0f);
        }
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    midi_in_render();
    bs_render(&eng, lr, n, (float)master_q12 * (1.0f / 4096.0f));
}

void app_panic(void)
{
    js_clock_stop(&eng.clock);
    js_queue_clear(&eng.q);
    jb_release(&eng.player);
}

/* --------------------------------------------------------------- storage --- */
static void settings_defaults(void)
{
    set.bpm = 174.0f;
    set.rhythm_t = 0.0f;
    set.kit_t = 0.25f;
    set.energy = 0.5f;
    jf_drum_preset(&set.fx, JF_DRUM_ROLLING);
    set.clock_src = JS_CLK_INT;
    set.link_ch = 15;
    set.send_clock = 1;
    set.send_kicks = 1;
    set.slices = 16;
    set.tempo_sync = 1;
    set.breaklock = BS_LOCK_OFF;
    set.anchors = 1;
    set.scene_quant = BS_Q_BAR;
    set.theme = 1;
    set.link_usb_host = 0;
    set.phrase = 4;
    set.form = 0;
    set.fill_bars = 1;
    set.decay = 0;
}

static int load_rec(uint32_t obj, uint32_t magic, void *dst, uint32_t len)
{
    const uint8_t *pl;
    uint32_t got_len;
    int got = plat_store_load(obj, &io, sizeof io);
    if (jo_rec_check(&io, got, magic, 1, 1, &pl, &got_len, 0) != JO_REC_OK || got_len != len) return -1;
    memcpy(dst, pl, len);
    return 0;
}
static int save_rec(uint32_t obj, uint32_t magic, const void *src, uint32_t len)
{
    memcpy(io.p, src, len);
    jo_rec_seal(&io.h, magic, 1, io.p, len);
    return plat_store_save(obj, &io, sizeof io.h + len);
}

/* (0.6) TAPE DECAY from the setting: off, or 5 % .. 100 % (a pass loses 10 .. 64 / 256 of a step's velocity) */
static void decay_apply(void)
{
    if (set.decay > 20u) set.decay = 20u;
    eng.decay_on = set.decay != 0;
    eng.decay_amt = (uint8_t)(8u + (uint32_t)set.decay * 56u / 20u);
}

static void apply_settings(void)
{
    bs_cmd_bpm(&eng, set.bpm);
    js_clock_set_bpm(&eng.clock, set.bpm);
    bs_set_rhythm(&eng, set.rhythm_t);
    bs_set_kit(&eng, set.kit_t);
    eng.energy = set.energy;
    eng.bus.m = set.fx;
    jf_drum_update(&eng.bus);
    eng.clock.source = set.clock_src;
    jl_init(&eng.link, set.link_ch);
    eng.send_clock = set.send_clock;
    eng.send_kicks = set.send_kicks;
    eng.tempo_sync = set.tempo_sync;
    eng.breaklock = set.breaklock;
    eng.anchors = set.anchors;
    if (set.slices != 16) bs_set_slices(&eng, set.slices);
    bs_set_phrase(&eng, set.phrase);
    eng.form = set.form % BS_NFORMS;
    decay_apply();
}

static void load_all(void)
{
    uint32_t p;
    load_report = 0;
    if (load_rec(OBJ_SETTINGS, SET_MAGIC, &set, sizeof set) || !(set.bpm >= 40.0f && set.bpm <= 300.0f) ||
        set.link_ch > 15u || set.clock_src > 1u || set.breaklock > 2u || set.scene_quant > BS_Q_PHRASE ||
        set.form >= BS_NFORMS || set.fill_bars < 1u || set.fill_bars > 2u) {
        settings_defaults();
        load_report |= 1u;
    }
    for (p = 0; p < BS_NPATTERNS; p++) {
        js_pattern_t tmp;
        if (load_rec(OBJ_PAT0 + p, PAT_MAGIC, &tmp, sizeof tmp) == 0 && tmp.ntracks == BS_NTRACKS) {
            uint32_t t, ok = 1;
            for (t = 0; t < BS_NTRACKS; t++)
                if (tmp.t[t].len < 1u || tmp.t[t].len > JS_MAXSTEPS || tmp.t[t].div > 2u) ok = 0;
            if (ok) { eng.pat[p] = tmp; continue; }
        }
        load_report |= 2u; /* this pattern stays the factory one */
    }
    if (load_rec(OBJ_SCENES, SCN_MAGIC, eng.scene, sizeof eng.scene)) load_report |= 4u;
    apply_settings();
}

static void store_settings_from_engine(void)
{
    set.rhythm_t = eng.rhythm_t;
    set.kit_t = eng.kit_t;
    set.energy = eng.energy;
    set.fx = eng.bus.m;
    set.breaklock = eng.breaklock;
    set.anchors = eng.anchors;
    set.tempo_sync = eng.tempo_sync;
    set.slices = eng.slices;
    set.phrase = eng.phrase;
    set.form = eng.form;
}

/* SAVE: every object; while playing it is refused, since a flash write stalls the audio (plat.h) */
static uint8_t auto_save;                   /* (0.5) the save is the automatic one (3 s after STOP) */
static void save_all(void)
{
    uint32_t p;
    int rc = 0;
    if (eng.clock.running) {
        app_say("STOP FIRST", "SAVING STALLS SOUND");
        return;
    }
    store_settings_from_engine();
    rc |= save_rec(OBJ_SETTINGS, SET_MAGIC, &set, sizeof set);
    for (p = 0; p < BS_NPATTERNS; p++) rc |= save_rec(OBJ_PAT0 + p, PAT_MAGIC, &eng.pat[p], sizeof eng.pat[p]);
    rc |= save_rec(OBJ_SCENES, SCN_MAGIC, eng.scene, sizeof eng.scene);
    if (rc) app_say("SAVE", "FAILED");
    else { dirty = 0; app_say(auto_save ? "AUTOSAVED" : "SAVED", "PATTERNS + SCENES"); }
}

/* ------------------------------------------------------- the bank loader --- */
/* SysEx F0 7D 4A 42 cmd ... F7 (docs/development/sample-format.md):
 *   01 BEGIN len(5)          02 DATA offset(5) data(7-bit packed: per 7 bytes, a byte of high bits then 7)
 *   03 END                   04 INFO
 * Answers F0 7D 4A 42 7F cmd status(1) value(5) F7. Status 0 ok, 1 playing, 2 too big, 3 order, 4 flash, 5 bad
 * bank (with the parser's code) */
static struct { uint32_t len, next, erased_to; uint8_t open; } ld;

static void sx_u35(uint8_t *p, uint32_t v) { uint32_t i; for (i = 0; i < 5u; i++) p[i] = (uint8_t)((v >> (7u * i)) & 0x7Fu); }
static uint32_t sx_get35(const uint8_t *p) { uint32_t v = 0, i; for (i = 0; i < 5u; i++) v |= (uint32_t)(p[i] & 0x7Fu) << (7u * i); return v; }
static void sx_reply(uint8_t cmd, uint8_t status, uint32_t value)
{
    uint8_t f[13] = {0xF0, 0x7D, 0x4A, 0x42, 0x7F, cmd, status};
    sx_u35(f + 7, value);
    f[12] = 0xF7;
    plat_sysex_send(f, sizeof f);
}

static void bank_detach(void)
{
    /* the render must not read the flash being rewritten: back to the RAM test break */
    uint32_t f = plat_lock();
    jb_release(&eng.player);
    eng.player.v[0].active = eng.player.v[1].active = 0;
    jb_testbreak_make(&eng.breaks[0], test_pcm, 22059u);
    eng.nbreaks = 1;
    eng.cur_break = 0;
    eng.has_bank = 0;
    eng.player.brk = &eng.breaks[0];
    plat_unlock(f);
}

static int bank_attach(void)
{
    const uint8_t *img = plat_data(BS_BANK_LO);
    int n;
    uint32_t f;
    if (!img) return -9;
    f = plat_lock();
    n = bs_load_bank(&eng, img, BS_BANK_HI - BS_BANK_LO);
    if (n <= 0) {
        jb_testbreak_make(&eng.breaks[0], test_pcm, 22059u);
        eng.nbreaks = 1;
        eng.player.brk = &eng.breaks[0];
    } else if (eng.slices != eng.breaks[0].nslices && !eng.breaks[0].cls[0])
        bs_set_slices(&eng, eng.slices);
    plat_unlock(f);
    return n;
}

int app_sysex(const uint8_t *p, uint32_t n)
{
    uint8_t buf[560];
    uint32_t off, k, m;
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x4Au || p[2] != 0x42u) return 0;
    switch (p[3]) {
    case 0x01:
        if (n < 9u) return 1;
        if (eng.clock.running) { sx_reply(1, 1, 0); return 1; }
        ld.len = sx_get35(p + 4);
        if (ld.len < JB_HDR || ld.len > BS_BANK_HI - BS_BANK_LO) { sx_reply(1, 2, BS_BANK_HI - BS_BANK_LO); return 1; }
        bank_detach();
        ld.next = 0;
        ld.erased_to = 0;
        ld.open = 1;
        app_say("LOADING", "SAMPLE BANK");
        sx_reply(1, 0, ld.len);
        return 1;
    case 0x02:
        if (!ld.open || n < 9u) { sx_reply(2, 3, 0); return 1; }
        off = sx_get35(p + 4);
        if (off != ld.next) { sx_reply(2, 3, ld.next); return 1; }
        /* unpack */
        for (k = 9u, m = 0; k < n && m + 7u <= sizeof buf;) {
            uint32_t hi = p[k++], j;
            for (j = 0; j < 7u && k < n; j++) buf[m++] = (uint8_t)(p[k++] | ((hi >> j) & 1u) << 7);
        }
        if (off + m > ld.len) m = ld.len - off;
        /* erase each sector before the first write into it; the audio is stopped (the transport) meanwhile */
        while (ld.erased_to < off + m) {
            if (plat_data_erase(BS_BANK_LO + ld.erased_to)) { ld.open = 0; sx_reply(2, 4, ld.erased_to); return 1; }
            ld.erased_to += 4096u;
        }
        if (plat_data_write(BS_BANK_LO + off, buf, m)) { ld.open = 0; sx_reply(2, 4, off); return 1; }
        ld.next = off + m;
        sx_reply(2, 0, ld.next);
        return 1;
    case 0x03: {
        int rc;
        ld.open = 0;
        if (ld.next != ld.len) { sx_reply(3, 3, ld.next); return 1; }
        rc = bank_attach();
        if (rc > 0) { app_say("BANK LOADED", eng.breaks[0].name); sx_reply(3, 0, (uint32_t)rc); }
        else { app_say("BANK", "REJECTED"); sx_reply(3, 5, (uint32_t)(-rc)); }
        ui.redraw = 1;
        return 1;
    }
    case 0x04: sx_reply(4, eng.has_bank ? 0 : 5, eng.nbreaks); return 1;
    default: return 0;
    }
}

/* ------------------------------------------------------------------ init --- */
void app_init(void)
{
    const uint8_t *img = plat_data(BS_BANK_LO);
    bs_init(&eng, test_pcm, img, img ? BS_BANK_HI - BS_BANK_LO : 0u);
    bs_set_room_buf(&eng, room_buf);
    bs_set_lock_hooks(plat_lock, plat_unlock);
    eng.midi_out = link_out;
    load_all();
    ui.held_step = -1;
    ui.groove = -1;
    for (uint32_t i = 0; i < BS_NPATTERNS; i++) ui.groove_of[i] = -1;
    ui.hi_macro = -1;
    ui.mutate_amt = 0.5f;
    ui.ghost_density = 0.45f;
    ui.mutate_seed = 1;
    ju_init(&undo);
    dna.playing = -1; /* (0.6) the tree starts when the DNA page opens */
    ui.cross_a = -2;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++) ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++) ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1400u;
    ui.redraw = 1;
    if (ann_on()) {
        char t[52] = "";
        ui_cat(t, ui.msg[0]);
        ui_cat(t, " ");
        ui_cat(t, ui.msg[1]);
        ann_say(t, 0, ANN_NOW);
    }
}

/* ---------------------------------------------------------------- undo --- */
/* (0.5) every pattern-changing action takes a snapshot first (shared/seq/jo_undo.h); edits of one kind on one pattern
 * within 2 s are one snapshot (turning a knob, browsing grooves, tapping steps) */
enum { U_STEP, U_STEPEDIT, U_TRACK, U_GROOVE, U_MUTATE, U_DENSITY, U_HUMANIZE, U_GHOSTS, U_RECORD, U_ERASE,
       U_LEVEL, U_ROUTE, U_DNA };
static void undo_mark(uint8_t kind)
{
    uint32_t now = plat_ms();
    if (ju_can_undo(&undo) && kind == ui.undo_kind && eng.cur_pat == ui.undo_slot && now - ui.undo_t < 2000u &&
        kind != U_MUTATE && kind != U_ERASE && kind != U_RECORD && kind != U_DNA) {
        ui.undo_t = now;
        return;
    }
    ju_push(&undo, eng.cur_pat, &eng.pat[eng.cur_pat], 0, 0);
    ui.undo_kind = kind, ui.undo_slot = eng.cur_pat, ui.undo_t = now;
    dirty = 1;
}
static void undo_do(int redo)
{
    uint8_t slot = 0;
    uint32_t f = plat_lock();
    int rc = redo ? ju_redo(&undo, eng.pat, 0, 0, &slot) : ju_undo(&undo, eng.pat, 0, 0, &slot);
    plat_unlock(f);
    ui.undo_t = 0; /* the next edit takes a fresh snapshot */
    if (rc) { app_say(redo ? "REDO" : "UNDO", redo ? "NOTHING TO REDO" : "NOTHING TO UNDO"); return; }
    ui.groove_of[slot % BS_NPATTERNS] = -1;
    dirty = 1;
    {
        static const char *const PN[BS_NPATTERNS] = {"P1 RESTORED", "P2 RESTORED", "P3 RESTORED", "P4 RESTORED"};
        app_say(redo ? "REDO" : "UNDO", PN[slot % BS_NPATTERNS]);
    }
}

/* ------------------------------------------------------------- helpers --- */
static const char *const PATTERN_NAME[BS_NPATTERNS] = {"P1 DROP", "P2 ROLLER", "P3 HALF-TIME", "P4 INTRO"};
static const char *const VAR_NAME[BS_NPATTERNS] = {"A (P1)", "B (P2)", "C (P3)", "D (P4)"};
static const char *const PAT_KEY[BS_NPATTERNS] = {"P1 DROP", "P2 ROLL", "P3 HALF", "P4 INTRO"}; /* (a key box's) */
static const char *const FILL_KEY[BS_NFILLS] = {"ROLL", "SN BUILD", "DROP OUT", "CHOP"};
static const char *const PERF_KEY[8] = {"ROLL", "ROLL 32", "CHOP", "REVERSE", "GLITCH", "DESTROY", "FREEZE", "TAPE STOP"};
static const uint8_t PERF_MODE[8] = {BS_PERF_ROLL, BS_PERF_ROLL, BS_PERF_CHOP, BS_PERF_REVERSE, BS_PERF_GLITCH,
                                     BS_PERF_DESTROY, BS_PERF_FREEZE, BS_PERF_TAPESTOP};
static const uint8_t DRUM_MODEL[BS_NTRACKS] = {0, JD_KICK, JD_SNARE, JD_GHOST, JD_HATS, JD_PERC, JD_IMPACT};

static int layer(void)
{
    const ui_in_t *in = &ui.in;
    if (ui_held(in, B_SEL)) return L_SEL;
    if (ui_held(in, B_SAVE)) return L_STORE;
    if (ui_held(in, B_REC)) return L_ERASE;
    if (ui_held(in, B_ENV)) return L_FILL;
    if (ui_held(in, B_LFO)) return L_VARY;
    if (ui_held(in, B_ARP)) return L_SONG;
    if (ui_held(in, B_FX)) return L_FX;
    if (ui_held(in, B_SEQ)) return L_ROUTE;
    if (ui_held(in, B_HOME)) return L_ENERGY;
    return L_NONE;
}
static void use_layer(int l)
{
    static const uint8_t LB[10] = {0, B_SEL, B_SAVE, B_REC, B_ENV, B_LFO, B_ARP, B_FX, B_SEQ, B_HOME};
    if (l) ui_use(&ui.in, LB[l]);
}
/* the keys are the steps on these pages (SOUND auditions; REC records) */
static int keys_are_steps(void) { return ui.page != PG_SOUND && ui.page != PG_DNA && !ui.rec; }
/* (0.6) ROUTE's target tracks */
static uint32_t route_mask(void)
{
    return ui.route_target == 2 ? (1u << BS_NTRACKS) - 1u : ui.route_target == 1 ? 1u << BS_T_BREAK : 1u << ui.track;
}
/* the route in force on a track (its live one over its own) */
static uint8_t route_of(uint32_t t)
{
    return eng.seq.route_ovr[t] != 0xFFu ? eng.seq.route_ovr[t] : eng.pat[eng.cur_pat].t[t].route;
}
static void dna_open(void)
{
    if (dna.playing < 0) bs_dna_root(&eng, &dna);
}
static js_step_t *held_step(void)
{
    return ui.held_step < 0 ? 0 : &eng.pat[eng.cur_pat].t[ui.track].s[(ui.step_page * 16 + ui.held_step) % JS_MAXSTEPS];
}
static int pat_quant(void) { return set.scene_quant == BS_Q_PHRASE ? BS_Q_PHRASE : BS_Q_BAR; }

static void pattern_step(int dir)
{
    int p = (eng.next_pat + BS_NPATTERNS + dir) % BS_NPATTERNS;
    if (eng.form) { bs_cmd_form(&eng, 0); }
    bs_cmd_pattern_q(&eng, p, pat_quant());
    app_say(PATTERN_NAME[p], !eng.clock.running ? "" : pat_quant() == BS_Q_PHRASE ? "AT THE PHRASE'S END" : "AT THE NEXT BAR");
}

static void scene_key(int s)
{
    if (s < 0 || s >= BS_NSCENES) return;
    bs_cmd_scene(&eng, s, set.scene_quant);
    app_say(SCENE_NAME[s], QUANT_NAME[set.scene_quant % 6u]);
}

/* --------------------------------------------------------------- knobs --- */
static void track_knob(int k, int32_t d, float step)
{
    if (ui.track == BS_T_BREAK) {
        if (k == 0) eng.player.pitch = jo_clampf(eng.player.pitch + 0.5f * (float)d, -12.0f, 12.0f);
        else if (k == 1) { eng.break_level = jo_clampf(eng.break_level + step * (float)d, 0.0f, 1.0f); eng.player.level = eng.break_level; }
        else if (k == 2) { int s = eng.slices; s = d > 0 ? (s == 8 ? 16 : 32) : (s == 32 ? 16 : 8); bs_set_slices(&eng, s); }
        else eng.player.gate = jo_clampf(eng.player.gate + 0.02f * (float)d, 0.05f, 1.0f);
    } else
        eng.snd[ui.track].m[k] = jo_clampf(eng.snd[ui.track].m[k] + step * (float)d, 0.0f, 1.0f);
    dirty = 1;
}

static void turn_k(int k, int32_t d)
{
    int l = layer();
    float step = l == L_SEL ? 0.0025f : 0.01f;
    js_pattern_t *pt = &eng.pat[eng.cur_pat];
    js_track_t *tr = &pt->t[ui.track];
    js_step_t *st = held_step();
    ui.hi_macro = (int8_t)k;
    ui.hi_until = plat_ms() + 1200u;
    ui.redraw = 1;
    if (st && keys_are_steps() && (l == L_NONE || l == L_SEL)) { /* the held step's values (SEL: its macro locks) */
        ui.key_used |= ui_white_bit(ui.held_step);
        undo_mark(U_STEPEDIT);
        if (l == L_SEL && ui.track != BS_T_BREAK) {
            int cur = js_step_lock(st, (uint8_t)(BS_LK_M0 + k));
            if (cur < 0) cur = (int)(eng.snd[ui.track].m[k] * 255.0f);
            use_layer(L_SEL);
            if (js_step_set_lock(st, (uint8_t)(BS_LK_M0 + k), jo_clampi(cur + d * 4, 0, 255))) app_say("LOCKS", "TWO PER STEP");
        } else if (k == 0) st->vel = (uint8_t)jo_clampi(st->vel + d * 2, 1, 127);
        else if (k == 1) st->prob = (uint8_t)jo_clampi(st->prob + d * 5, 0, 100);
        else if (k == 2) st->ratch = (uint8_t)jo_clampi(st->ratch + (d > 0 ? 1 : -1), 0, 7);
        else st->micro = (int8_t)jo_clampi(st->micro + d * 4, -64, 63);
        st->flags |= JS_ON;
        return;
    }
    if (l == L_SEL) use_layer(L_SEL);
    switch (l) {
    case L_FX: /* the bus, as the FX page */
        use_layer(l);
        {
            float *v = k == 0 ? &eng.bus.m.punch : k == 1 ? &eng.bus.m.crush : k == 2 ? &eng.bus.m.glue : &eng.bus.m.pressure;
            *v = jo_clampf(*v + 0.01f * (float)d, 0.0f, 1.0f);
            jf_drum_update(&eng.bus);
            dirty = 1;
        }
        return;
    case L_FILL:
        use_layer(l);
        if (k == 0) set.fill_bars = d > 0 ? 2 : 1, dirty = 1;
        return;
    case L_VARY:
        use_layer(l);
        if (k == 0) { /* DENSITY: a step a click */
            undo_mark(U_DENSITY);
            if (bs_density_step(&eng, ui.track, d) < 0) app_say("DENSITY", d > 0 ? "FULL" : "EMPTY");
        } else if (k == 1) {
            undo_mark(U_HUMANIZE);
            ui.humanize = jo_clampf(ui.humanize + 0.05f * (float)d, 0.0f, 1.0f);
            bs_humanize(&eng, ui.track, ui.humanize);
        } else if (k == 2) ui.mutate_amt = jo_clampf(ui.mutate_amt + 0.02f * (float)d, 0.0f, 1.0f);
        else {
            undo_mark(U_GHOSTS);
            ui.ghost_density = jo_clampf(ui.ghost_density + 0.05f * (float)d, 0.0f, 1.0f);
            bs_ghosts(&eng, ui.ghost_density, 7u);
        }
        return;
    case L_ROUTE:
        use_layer(l);
        if (k == 0) ui.route_target = (uint8_t)jo_clampi(ui.route_target + (d > 0 ? 1 : -1), 0, 2);
        else if (k == 1) ui.route_latch = d > 0;
        return;
    case L_ENERGY:
        use_layer(l);
        if (k == 0) bs_set_energy(&eng, eng.nrg.knob + 0.02f * (float)d);
        else if (k == 1) {
            int m = jo_clampi(bs_energy_curve_mode(&eng) + (d > 0 ? 1 : -1), 0, 2);
            bs_energy_curve(&eng, m);
            app_say("ENERGY CURVE", m == 1 ? "RECORDS NEXT PHRASE" : m == 2 ? (eng.nrg.beats ? "PLAYS" : "NONE RECORDED") : "OFF");
        } else if (k == 2) {
            undo_mark(U_LEVEL);
            bs_auto_levels(&eng, ui.track);
            app_say("AUTO LEVELS", bs_track_name[ui.track]);
        }
        return;
    case L_SONG: use_layer(l); break; /* the SONG page's knobs */
    case L_STORE: case L_ERASE: use_layer(l); return;
    default: break;
    }
    switch (l == L_SONG ? PG_SONG : ui.page) {
    case PG_MAIN: track_knob(k, d, step); return;
    case PG_SEQ:
        undo_mark(U_TRACK);
        if (k == 0) tr->len = (uint8_t)jo_clampi(tr->len + (d > 0 ? 1 : -1), 1, JS_MAXSTEPS);
        else if (k == 1) pt->swing = (uint8_t)jo_clampi(pt->swing + d, 50, 75);
        else if (k == 2) { ui.ghost_density = jo_clampf(ui.ghost_density + 0.05f * (float)d, 0.0f, 1.0f); bs_ghosts(&eng, ui.ghost_density, 7u); }
        else tr->div = (uint8_t)jo_clampi(tr->div + (d > 0 ? 1 : -1), 0, 2);
        return;
    case PG_SOUND:
        if (k == 0) { eng.rhythm_t = jo_clampf(eng.rhythm_t + step * (float)d, 0.0f, 1.0f); bs_set_rhythm(&eng, eng.rhythm_t); }
        else if (k == 1) { eng.kit_t = jo_clampf(eng.kit_t + step * (float)d, 0.0f, 1.0f); bs_set_kit(&eng, eng.kit_t); }
        else if (k == 2) eng.energy = jo_clampf(eng.energy + step * (float)d, 0.0f, 1.0f);
        else if (ui.track == BS_T_BREAK) { eng.break_level = jo_clampf(eng.break_level + step * (float)d, 0.0f, 1.0f); eng.player.level = eng.break_level; }
        else eng.snd[ui.track].level = jo_clampf(eng.snd[ui.track].level + step * (float)d, 0.0f, 1.0f);
        dirty = 1;
        return;
    case PG_FX: {
        float *v = k == 0 ? &eng.bus.m.punch : k == 1 ? &eng.bus.m.crush : k == 2 ? &eng.bus.m.glue : &eng.bus.m.pressure;
        *v = jo_clampf(*v + step * (float)d, 0.0f, 1.0f);
        jf_drum_update(&eng.bus);
        dirty = 1;
        return;
    }
    case PG_SONG:
        if (k == 0) set.scene_quant = (uint8_t)jo_clampi(set.scene_quant + (d > 0 ? 1 : -1), 0, BS_Q_PHRASE);
        else if (k == 1) { int p = eng.phrase; p = d > 0 ? (p == 4 ? 8 : 16) : (p == 16 ? 8 : 4); bs_set_phrase(&eng, p); }
        else if (k == 2) { bs_cmd_form(&eng, jo_clampi(eng.form + (d > 0 ? 1 : -1), 0, BS_NFORMS - 1)); }
        else { set.decay = (uint8_t)jo_clampi(set.decay + (d > 0 ? 1 : -1), 0, 20); decay_apply(); } /* (0.6) */
        dirty = 1;
        return;
    case PG_DNA: /* K1: how far a child strays */
        if (k == 0) ui.mutate_amt = jo_clampf(ui.mutate_amt + 0.02f * (float)d, 0.0f, 1.0f);
        return;
    default: /* SYSTEM */
        if (k == 0) { set.bpm = jo_clampf((float)(int32_t)(eng.clock.bpm * 2.0f + 0.5f) * 0.5f + 0.5f * (float)d, 60.0f, 220.0f); bs_cmd_bpm(&eng, set.bpm); }
        else if (k == 1) { set.clock_src = d > 0; eng.clock.source = set.clock_src; }
        else if (k == 2) { set.link_ch = (uint8_t)jo_clampi(set.link_ch + (d > 0 ? 1 : -1), 0, 15); jl_init(&eng.link, set.link_ch); }
        else { eng.send_clock = d > 0; set.send_clock = eng.send_clock; }
        dirty = 1;
        return;
    }
}

/* --------------------------------------------------------------- input --- */
/* a white key with a layer held (down / up); 1 = handled */
static int layer_key(int l, int w, int b, int down)
{
    if (l == L_NONE || l == L_SEL) return 0;
    use_layer(l);
    switch (l) {
    case L_FX: /* the performance FX, while held */
        if (w >= 0 && w < 8) {
            if (down) { eng.roll_div = w == 1 ? 3 : 1; bs_cmd_perf(&eng, PERF_MODE[w]); app_say("PERFORM", PERF_KEY[w]); }
            else if (eng.perf == PERF_MODE[w]) bs_cmd_perf(&eng, BS_PERF_CLEAN);
        }
        return 1;
    case L_SONG:
        if (!down) return 1;
        if (w >= 0 && w < BS_NSCENES) scene_key(w);
        else if (w >= 8 && w < 8 + BS_NPATTERNS) {
            if (eng.form) bs_cmd_form(&eng, 0);
            bs_cmd_pattern_q(&eng, w - 8, pat_quant());
            app_say(PATTERN_NAME[w - 8], eng.form ? "FORM OFF" : QUANT_NAME[pat_quant()]);
        } else if (b >= 0 && b < BS_NTRACKS) {
            bs_cmd_mute(&eng, b, 0);
            app_say((eng.seq.mute >> b) & 1u ? "UNMUTE" : "MUTE", bs_track_name[b]);
        }
        return 1;
    case L_FILL:
        if (down && w >= 0 && w < BS_NFILLS) {
            bs_cmd_fill_ex(&eng, set.fill_bars, w, 0);
            app_say(bs_fill_name[w], set.fill_bars == 2 ? "2 BARS FROM THE NEXT" : "FROM THE NEXT BAR");
        }
        return 1;
    case L_VARY:
        if (!down) return 1;
        if (w >= 0 && w < BS_NPATTERNS) {
            if (eng.form) bs_cmd_form(&eng, 0);
            bs_cmd_pattern_q(&eng, w, BS_Q_BAR);
            app_say("VARIATION", VAR_NAME[w]);
        } else if (w >= 8 && w < 8 + BS_NFORMS) {
            bs_cmd_form(&eng, w - 8);
            dirty = 1;
            app_say("FORM", bs_form_name[w - 8]);
        }
        return 1;
    case L_ERASE:
        if (!down) return 1;
        if (w >= 0) { undo_mark(U_ERASE); bs_erase_step(&eng, ui.track, ui.step_page * 16 + w); }
        else if (b >= 0 && b < BS_NTRACKS) { undo_mark(U_ERASE); bs_erase_track(&eng, b); app_say("CLEARED", bs_track_name[b]); }
        return 1;
    case L_STORE:
        if (down && w >= 0 && w < BS_NSCENES) { bs_scene_store(&eng, w); dirty = 1; app_say("SCENE STORED", SCENE_NAME[w]); }
        else if (down && ui.page == PG_DNA && w >= 8 && w < 8 + BS_NPATTERNS && dna.playing >= 0) { /* (0.6) */
            static const char *const PS[BS_NPATTERNS] = {"INTO P1", "INTO P2", "INTO P3", "INTO P4"};
            if (w - 8 == eng.cur_pat) undo_mark(U_DNA);
            bs_dna_store(&eng, &dna, dna.playing, w - 8);
            dirty = 1;
            app_say("GENOME STORED", PS[w - 8]);
        }
        return 1;
    case L_ROUTE: /* (0.6) black 1-9: a route; white keys in order: a drawn one */
        if (!down) return 1;
        if (b >= 0 && b < JS_NROUTES) {
            if (ui.route_latch) {
                if (!ui.route_undone) { undo_mark(U_ROUTE); ui.route_undone = 1; }
                bs_route_latch(&eng, route_mask(), (uint8_t)b);
            } else
                bs_route_live(&eng, route_mask(), (uint8_t)b);
            app_say("ROUTE", js_route_name[b]);
        } else if (w >= 0 && ui.draw_n < JS_MAXSTEPS) {
            ui.draw[ui.draw_n++] = (uint8_t)(ui.step_page * 16 + w);
            if (ui.draw_n >= 2u) {
                if (ui.route_latch && !ui.route_undone) { undo_mark(U_ROUTE); ui.route_undone = 1; }
                bs_route_draw(&eng, route_mask(), ui.draw, ui.draw_n);
                if (ui.route_latch) bs_route_latch(&eng, route_mask(), JS_R_CUSTOM);
                else bs_route_live(&eng, route_mask(), JS_R_CUSTOM);
            }
        }
        return 1;
    case L_ENERGY: /* (0.6) white: the step's level 1 -> 2 -> 3 -> 4; black: the track */
        if (!down) return 1;
        if (w >= 0) {
            js_step_t *st = &eng.pat[eng.cur_pat].t[ui.track].s[(ui.step_page * 16 + w) % JS_MAXSTEPS];
            undo_mark(U_LEVEL);
            st->lvl = (uint8_t)((st->lvl ? st->lvl : 1u) % 4u + 1u);
        } else if (b >= 0 && b < BS_NTRACKS) ui.track = (uint8_t)b;
        return 1;
    default: return 1;
    }
}

static void input(void)
{
    ui_in_t *in = &ui.in;
    uint32_t k;
    int32_t d;
    int l;
    ui_poll(in);
    if (in->btn_down || in->btn_up || in->key_down || in->key_up) ui.redraw = 1;
    /* SEL's combinations first: the button pressed with SEL held is used, its tap and its layer do not happen */
    if (ui_held(in, B_SEL)) {
        if (ui_pressed(in, B_LFO)) { ui_use(in, B_SEL); ui_use(in, B_LFO); undo_do(0); }
        if (ui_pressed(in, B_ENV)) { ui_use(in, B_SEL); ui_use(in, B_ENV); undo_do(1); }
        if (ui_pressed(in, B_REC)) { ui_use(in, B_SEL); ui_use(in, B_REC); bs_cmd_return(&eng); app_say("RETURN", "TO THE GROOVE: NEXT BAR"); }
    }
    /* taps: a page, or the button's action (a hold is its layer) */
    if (ui_tapped(in, B_HOME)) ui.page = PG_MAIN;
    if (ui_tapped(in, B_SEQ)) { /* (0.6) the overview, then DNA, in turn */
        ui.page = ui.page == PG_SEQ ? PG_DNA : PG_SEQ;
        if (ui.page == PG_DNA) { dna_open(); ui.cross_a = -2; }
    }
    if (ui_pressed(in, B_SEQ)) ui.draw_n = 0, ui.route_undone = 0; /* ROUTE: a fresh drawing each hold */
    if ((in->btn_up & (1u << B_SEQ)) && !ui.route_latch) bs_route_live(&eng, (1u << BS_NTRACKS) - 1u, 0xFFu);
    if (ui_tapped(in, B_EDIT)) ui.page = PG_SOUND;
    if (ui_tapped(in, B_FX)) ui.page = PG_FX;
    if (ui_tapped(in, B_ARP)) ui.page = PG_SONG;
    if (ui_tapped(in, B_GLO)) ui.page = PG_SYSTEM;
    if (ui_tapped(in, B_ENV)) {
        bs_cmd_fill_ex(&eng, set.fill_bars, BS_FILL_ROLL, 1);
        app_say("FILL", "AT THE PHRASE'S END");
    }
    if (ui_tapped(in, B_LFO) && ui.page == PG_DNA) { /* (0.6) breed: a child of the playing genome */
        int c;
        dna_open();
        undo_mark(U_DNA);
        c = bs_dna_breed(&eng, &dna, ui.mutate_amt, ui.mutate_seed++);
        if (c < 0) app_say("DNA", "FULL: KILL OR UNKEEP");
        else { char t[16] = "GENOME "; char v[4]; ui_int(v, c + 1); ui_cat(t, v); app_say("BRED", t); }
    } else if (ui_tapped(in, B_LFO)) {
        undo_mark(U_MUTATE);
        bs_mutate_track(&eng, ui.track, ui.mutate_amt, ui.mutate_seed++);
        app_say("MUTATE", "SEL+LFO: UNDO");
    }
    if (ui_tapped(in, B_REC)) {
        ui.rec = !ui.rec;
        ui.held_step = -1;
        if (ui.rec) { ui.undo_t = 0; undo_mark(U_RECORD); }
        app_say(ui.rec ? "RECORD" : "RECORD OFF", ui.rec ? "PLAY THE KEYS" : "");
    }
    if (ui_tapped(in, B_SAVE)) { auto_save = 0; save_all(); }
    if (in->btn_up & (1u << B_FX) && eng.perf) bs_cmd_perf(&eng, BS_PERF_CLEAN); /* FX let go: the FX end */
    if (ui_pressed(in, B_PLAY)) {
        if (ui_held(in, B_SEL)) { ui_use(in, B_SEL); bs_cmd_panic(&eng); bs_cmd_play(&eng, 0); app_say("PANIC", "ALL STOPPED"); }
        else bs_cmd_play(&eng, !eng.clock.running);
    }
    if (ui_pressed(in, B_OCTDN) || ui_pressed(in, B_OCTUP)) ui.step_page = ui_pressed(in, B_OCTUP) ? 1 : 0;
    /* keys */
    l = layer();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        int w = ui_white(k), b = ui_black(k);
        int down = (in->key_down & bit) != 0, up = (in->key_up & bit) != 0;
        if (!down && !up) continue;
        if (layer_key(l, w, b, down)) { if (down) ui.key_used |= bit; continue; }
        if (up && (ui.key_used & bit) && ui.held_step != w) { ui.key_used &= ~bit; continue; }
        if (ui.page == PG_DNA) { /* (0.6) white: a genome; black 1 KEEP, 2 KILL, 3 CROSS, 4 a NEW tree */
            if (!down) continue;
            dna_open();
            if (w >= 0 && w < BS_DNA_MAX && dna.g[w].used) {
                if (ui.cross_a == -1) { ui.cross_a = (int8_t)w; app_say("CROSS", "NOW PARENT B"); }
                else if (ui.cross_a >= 0) {
                    int c;
                    undo_mark(U_DNA);
                    c = bs_dna_cross(&eng, &dna, ui.cross_a, w);
                    ui.cross_a = -2;
                    app_say("CROSSED", c < 0 ? "NO ROOM" : "A'S DRUMS, B'S BREAK");
                } else { undo_mark(U_DNA); bs_dna_select(&eng, &dna, w); app_say("GENOME", eng.clock.running ? "AT THE NEXT BAR" : "NOW"); }
            } else if (b == 0) { bs_dna_keep(&dna, dna.playing); app_say("KEEP", dna.g[(int)dna.playing].keep ? "STAR ON" : "STAR OFF"); }
            else if (b == 1) app_say("KILL", bs_dna_kill(&eng, &dna, dna.playing) ? "THE ROOT STAYS" : "WITH ITS CHILDREN");
            else if (b == 2) { ui.cross_a = ui.cross_a == -2 ? -1 : -2; app_say("CROSS", ui.cross_a == -1 ? "PRESS PARENT A" : "OFF"); }
            else if (b == 3) { bs_dna_root(&eng, &dna); app_say("DNA", "NEW TREE FROM THIS PATTERN"); }
            continue;
        }
        if (b >= 0) { /* the black keys: the track, everywhere */
            if (down && b < BS_NTRACKS) ui.track = (uint8_t)b, ui.held_step = -1;
            continue;
        }
        if (w < 0) continue;
        if (ui.page == PG_SOUND) { /* audition */
            if (down) {
                if (ui.track == BS_T_BREAK) bs_cmd_live_slice(&eng, w, 1.0f);
                else bs_cmd_live_drum(&eng, ui.track, 0.3f + 0.7f * (float)w / 15.0f);
            }
        } else if (ui.rec) { /* RECORD: played and written, quantized */
            if (down) {
                float vel = ui.track == BS_T_BREAK ? 0.86f : 0.3f + 0.7f * (float)w / 15.0f;
                bs_record(&eng, ui.track, vel, w);
                dirty = 1;
            }
        } else { /* the steps: tap on / off; hold + a knob or encoder edits */
            if (down) { ui.held_step = (int8_t)w; ui.key_used &= ~bit; }
            else {
                if (!(ui.key_used & bit)) {
                    js_step_t *st = &eng.pat[eng.cur_pat].t[ui.track].s[(ui.step_page * 16 + w) % JS_MAXSTEPS];
                    undo_mark(U_STEP);
                    st->flags ^= JS_ON;
                }
                ui.key_used &= ~bit;
                if (ui.held_step == w) ui.held_step = -1;
            }
        }
    }
    /* encoders */
    if ((d = plat_enc(EN_SELECT)) != 0) {
        if (layer() == L_ENERGY) { use_layer(L_ENERGY); bs_set_energy(&eng, eng.nrg.knob + 0.02f * (float)d); }
        else if (ui_held(in, B_SEL)) { /* tempo */
            ui_use(in, B_SEL);
            set.bpm = jo_clampf(eng.clock.bpm + 0.5f * (float)d, 60.0f, 220.0f);
            bs_cmd_bpm(&eng, set.bpm);
            dirty = 1;
        } else { ui.track = (uint8_t)jo_clampi(ui.track + (d > 0 ? 1 : -1), 0, BS_NTRACKS - 1); ui.held_step = -1; }
        ui.redraw = 1;
    }
    if ((d = plat_enc(EN_ALGO)) != 0) {
        js_step_t *st = held_step();
        if (st && keys_are_steps()) { /* the step's trig condition */
            ui.key_used |= ui_white_bit(ui.held_step);
            undo_mark(U_STEPEDIT);
            st->cond = (uint8_t)jo_clampi(st->cond + (d > 0 ? 1 : -1), 0, JS_NCOND - 1);
            st->flags |= JS_ON;
        } else if (ui.page == PG_MAIN || ui.page == PG_SEQ) { /* the groove library */
            undo_mark(U_GROOVE);
            ui.groove = (int8_t)((ui.groove + (d > 0 ? 1 : BS_NGROOVES - 1)) % BS_NGROOVES);
            if (ui.groove < 0) ui.groove = 0;
            bs_groove(&eng, ui.groove);
            ui.groove_of[eng.cur_pat] = ui.groove;
            app_say(bs_groove_name[ui.groove], "SEL+LFO: UNDO");
        } else if (ui.page == PG_SOUND && ui.track == BS_T_PERC) {
            static const char *const PV[JD_PERC_NVAR] = {"TOM", "RIM", "SHAKER", "CLICK"};
            eng.snd[BS_T_PERC].variant = (uint8_t)((eng.snd[BS_T_PERC].variant + (d > 0 ? 1u : JD_PERC_NVAR - 1u)) % JD_PERC_NVAR);
            app_say("PERC VOICE", PV[eng.snd[BS_T_PERC].variant]);
        } else if (ui.page == PG_SOUND) {
            app_say("KIT", bs_kit_step(&eng, d));
        } else if (ui.page == PG_FX) {
            static int preset = JF_DRUM_ROLLING;
            preset = (preset + (d > 0 ? 1 : JF_DRUM_NPRESETS - 1)) % JF_DRUM_NPRESETS;
            jf_drum_preset(&eng.bus.m, preset);
            jf_drum_update(&eng.bus);
            app_say("DRUM BUS", jf_drum_preset_name[preset]);
        } else if (ui.page == PG_SONG) {
            eng.anchors = d > 0;
            app_say("ANCHORS", eng.anchors ? "ON: THE BEATS STAY" : "OFF");
        } else if (ui.page == PG_SYSTEM) { /* (0.6) BREAK-LOCK moved here from SONG's K4 */
            eng.breaklock = (uint8_t)jo_clampi(eng.breaklock + (d > 0 ? 1 : -1), 0, 2);
            app_say("BREAK-LOCK", LOCK_NAME[eng.breaklock % 3u]);
        }
        dirty = 1;
        ui.redraw = 1;
    }
    if ((d = plat_enc(EN_PRESET)) != 0) {
        js_step_t *st = held_step();
        if (st && keys_are_steps()) { /* the BREAK step's slice, or a drum step's tune lock */
            ui.key_used |= ui_white_bit(ui.held_step);
            undo_mark(U_STEPEDIT);
            if (ui.track == BS_T_BREAK) st->data = (uint8_t)((st->data + (uint32_t)(d > 0 ? 1 : eng.slices - 1)) % eng.slices);
            else {
                int cur = js_step_lock(st, BS_LK_TUNE);
                if (js_step_set_lock(st, BS_LK_TUNE, jo_clampi((cur < 0 ? 128 : cur) + 4 * d, 0, 255))) app_say("LOCKS", "TWO PER STEP");
            }
            st->flags |= JS_ON;
        } else if (ui.page == PG_SYSTEM) { /* the link over the USB port as a host (fm1.toml usb_host) */
            set.link_usb_host = d > 0;
            plat_usb_host(set.link_usb_host);
            app_say("USB", set.link_usb_host ? "HOST: LOOKING" : "DEVICE");
        } else if (ui.page == PG_FX && ui_held(in, B_SEL)) {
            ui_use(in, B_SEL);
            eng.bus.m.trim_db = jo_clampf(eng.bus.m.trim_db + 0.5f * (float)d, -12.0f, 6.0f);
            jf_drum_update(&eng.bus);
        } else if (ui.page == PG_FX) {
            eng.bus.m.room = jo_clampf(eng.bus.m.room + 0.02f * (float)d, 0.0f, 1.0f);
            jf_drum_update(&eng.bus);
        } else if (ui.page == PG_SONG) {
            eng.send_kicks = d > 0;
            app_say("LINK KICKS", eng.send_kicks ? "SENT" : "OFF");
        } else pattern_step(d > 0 ? 1 : -1);
        dirty = 1;
        ui.redraw = 1;
    }
    for (k = 0; k < 4u; k++)
        if ((d = plat_enc(EN_K1 + (int)k)) != 0) turn_k((int)k, d);
    /* (0.6) TAPE DECAY: a step held half a second (nothing turned) burns in */
    if (eng.decay_on && ui.held_step >= 0 && keys_are_steps() && layer() == L_NONE) {
        uint32_t bit = ui_white_bit(ui.held_step), kk;
        for (kk = 0; kk < NKEYS; kk++)
            if ((bit >> kk) & 1u) break;
        if (kk < NKEYS && !(ui.key_used & bit) && plat_ms() - in->key_t[kk] >= 500u) {
            undo_mark(U_STEPEDIT);
            bs_burn_step(&eng, ui.track, ui.step_page * 16 + ui.held_step);
            ui.key_used |= bit;
            app_say("BURNED IN", "IT STAYS");
        }
    }
    /* autosave: 3 s after STOP (or after the last change while stopped) */
    if (eng.clock.running || !dirty) ui.save_at = 0;
    else if (!ui.save_at || in->btn_down || in->key_down) ui.save_at = plat_ms() + 3000u;
}

/* --------------------------------------------------------------- screens --- */
/* how far a queued change is from landing (0..1), or -1: a pattern, a scene, RETURN or a fill waiting for its bar */
static float pending_frac(void)
{
    uint32_t now = eng.clock.abs_step, at = 0xFFFFFFFFu, rem, span;
    if (!eng.clock.running) return -1.0f;
    if (eng.next_pat != eng.cur_pat || eng.scene_pending >= 0 || eng.return_pending) at = eng.pending_at;
    if (eng.fill_on && js_before(now, eng.fill_start) && (at == 0xFFFFFFFFu || js_before(eng.fill_start, at))) at = eng.fill_start;
    if (at == 0xFFFFFFFFu || !js_before(now, at)) return -1.0f;
    rem = at - now;
    span = rem > 128u ? 256u : rem > 64u ? 128u : rem > 32u ? 64u : rem > 16u ? 32u : 16u;
    return 1.0f - (float)rem / (float)span;
}

static void draw_header(void)
{
    char mid[24] = "", right[28] = "", t[8];
    int alarm = plat_cpu_pct() > 85u || plat_xruns(), l = layer();
    if (ui.msg_until && plat_ms() < ui.msg_until) {
        ui_header(ui.msg[0], ui.msg[1], 0, 0);
        return;
    }
    ui_fix1(mid, eng.clock.bpm);
    ui_cat(mid, eng.clock.source == JS_CLK_MIDI ? " EXT" : "");
    ui_cat(mid, eng.clock.running ? " >" : " #");
    if (ui.rec) ui_cat(right, "REC ");
    if (eng.form) { t[0] = (char)('A' + eng.cur_pat); t[1] = 0; ui_cat(right, t); }
    else { ui_cat(right, "P"); t[0] = (char)('1' + eng.cur_pat); t[1] = 0; ui_cat(right, t); }
    if (eng.next_pat != eng.cur_pat || eng.scene_pending >= 0) ui_cat(right, "*");
    ui_cat(right, " ");
    ui_int(t, (int32_t)bs_phrase_bar(&eng) + 1); ui_cat(right, t); ui_cat(right, "/");
    ui_int(t, eng.phrase); ui_cat(right, t);
    ui_cat(right, " E"); /* (0.6) ENERGY's level */
    t[0] = (char)('0' + je_level(eng.nrg.value)); t[1] = 0; ui_cat(right, t);
    ui_cat(right, " ");
    ui_int(t, (int32_t)plat_cpu_pct()); ui_cat(right, t); ui_cat(right, "%");
    ui_header2(l ? LAYER_NAME[l] : PAGE_NAME[ui.page], mid, right, alarm, pending_frac());
}

/* the step cells of a track for a 16-step view (the step page's) */
static void track_cells(uint32_t t, uint8_t on[16], uint8_t vel[16], uint8_t mark[16], int *play)
{
    const js_track_t *tr = &eng.pat[eng.cur_pat].t[t];
    uint32_t i, base = ui.step_page * 16u;
    for (i = 0; i < 16u; i++) {
        const js_step_t *st = &tr->s[(base + i) % JS_MAXSTEPS];
        int in = base + i < tr->len;
        on[i] = !in || !(st->flags & JS_ON) ? 0 : st->flags & JS_ACCENT ? 2 : 1;
        if (vel) vel[i] = st->vel;
        if (mark) mark[i] = (uint8_t)((st->lk_id[0] != JS_LOCK_NONE ? 1 : 0) | (st->cond ? 2 : 0) | (st->prob < 100u ? 4 : 0) |
                                      (st->flags & (t == BS_T_HATS ? BS_FLAG_OPEN : JS_REVERSE) ? 8 : 0));
        if (!in && mark) mark[i] = 0;
    }
    *play = -1;
    if (eng.clock.running && tr->len) {
        uint32_t p = bs_route_at(&eng, (int)t, (eng.seq.pos[t] + tr->len - 1u) % tr->len); /* (0.6) the route's */
        if (p >= base && p < base + 16u) *play = (int)(p - base);
    }
}

/* (0.6) over the main grid: the route's order (the position each step plays at), TAPE DECAY's fading (a dim frame)
 * and burned steps (a white top), and in the ENERGY layer the levels (1-4) */
static void grid_overlay(uint32_t t, int levels)
{
    const js_track_t *tr = &eng.pat[eng.cur_pat].t[t];
    uint32_t i, len = tr->len ? tr->len : 16u, base = ui.step_page * 16u;
    uint8_t r = route_of(t);
    for (i = 0; i < 16u; i++) {
        int32_t x = 4 + (int32_t)(i & 7u) * 29, y = 19 + (int32_t)(i >> 3) * 26;
        const js_step_t *st = &tr->s[(base + i) % JS_MAXSTEPS];
        char v[4];
        if (base + i >= len) continue;
        if (levels) {
            v[0] = (char)('0' + (st->lvl ? st->lvl : 1u)); v[1] = 0;
            ui_fit(x + 10, y + 6, 12, &FONT_XS, v, je_level(eng.nrg.value) >= (st->lvl ? st->lvl : 1u) ? C_WHITE : C_DIM);
            continue;
        }
        if ((st->flags & (JS_ON | JS_DECAY | JS_BURN)) == (JS_ON | JS_DECAY)) {
            cv_rect(x + 1, y + 1, 25, 1, C_DIM); cv_rect(x + 1, y + 20, 25, 1, C_DIM);
        }
        if (st->flags & JS_BURN) cv_rect(x + 2, y + 1, 23, 2, C_WHITE);
    }
    if (!levels && r != JS_R_FORWARD) { /* the order: the first position each step is played at */
        uint32_t p, seen = 0;
        for (p = 0; p < len && p < 32u; p++) {
            uint32_t at = bs_route_at(&eng, (int)t, p);
            char v[4];
            if (at < base || at >= base + 16u || (seen >> (at - base) & 1u)) continue;
            seen |= 1u << (at - base);
            ui_int(v, (int32_t)(p + 1u));
            ui_fit(4 + (int32_t)((at - base) & 7u) * 29 + 3, 19 + (int32_t)((at - base) >> 3) * 26 + 3, 14, &FONT_T, v, C_GRAY);
        }
    }
}

/* a track as a thin row (the main page's mini rows, the overview): x0, cell width w, height h */
static void mini_row(int32_t x0, int32_t y, int32_t w, int32_t h, uint32_t t, int sel)
{
    uint8_t on[16];
    int play, i, muted = (eng.seq.mute >> t) & 1u;
    track_cells(t, on, 0, 0, &play);
    for (i = 0; i < 16; i++) {
        uint16_t c = on[i] ? (muted ? C_DIM : sel ? C_HI : C_AMB) : (i & 3) ? C_BLACK : C_LINE;
        cv_rect(x0 + i * w, y, w - 1, h, on[i] || (i & 3) == 0 ? c : C_LINE);
        if (!on[i] && (i & 3)) cv_rect(x0 + i * w, y + h - 1, w - 1, 1, C_LINE);
        if (i == play) cv_rect(x0 + i * w, y, w - 1, h, C_WHITE);
    }
    if (muted) cv_rect(x0, y + h / 2, 16 * w - 1, 1, UI_RED);
}

/* a held layer: the banner, and what each white key does in it (4 x 4 boxes, the lit one active) */
static void layer_page(int l)
{
    static const char *const HINT[8] = {"", "COMBINATIONS", "STORE A SCENE", "CLEAR STEPS", "PLAY A FILL",
                                        "VARIATIONS + FORM", "SCENES PATTERNS MUTES", "WHILE HELD"};
    const char *name[16] = {0};
    int lit[16] = {0};
    char t[40];
    uint32_t i;
    const char *black = 0;
    ui_banner(LAYER_NAME[l], HINT[l]);
    if (l == L_ENERGY) { /* (0.6) the energy, its curve, and the track's levels */
        uint8_t on[16], vel[16], mark[16];
        int play, cm = bs_energy_curve_mode(&eng);
        char v[8];
        ui_banner("ENERGY", "STEP LEVELS 1-4");
        track_cells(ui.track, on, vel, mark, &play);
        for (i = 0; i < 16u; i++) {
            const js_step_t *st = &eng.pat[eng.cur_pat].t[ui.track].s[(ui.step_page * 16u + i) % JS_MAXSTEPS];
            vel[i] = (uint8_t)(31u * (st->lvl ? st->lvl : 1u));
            mark[i] = 0;
            if (!on[i]) on[i] = 0;
        }
        ui_grid(21, on, vel, mark, play, -1);
        grid_overlay(ui.track, 1);
        cv_rect(4, 75, 232, 8, C_LINE);
        cv_rect(4, 75, (int32_t)(232.0f * eng.nrg.value), 8, C_HI);
        t[0] = 0; ui_cat(t, "ENERGY "); ui_pct(v, eng.nrg.value); ui_cat(t, v);
        ui_cat(t, "  LEVEL "); v[0] = (char)('0' + je_level(eng.nrg.value)); v[1] = 0; ui_cat(t, v);
        ui_cat(t, "  CURVE "); ui_cat(t, cm == 1 ? (eng.nrg.recording ? "RECORDING" : "ARMED") : cm == 2 ? "PLAYING" : "OFF");
        ui_fit(4, 86, 232, &FONT_XS, t, C_WHITE);
        t[0] = 0; ui_cat(t, bs_track_name[ui.track]); ui_cat(t, ": WHITE KEYS SET LEVELS  BLACK: TRACK");
        ui_fit(4, 101, 232, &FONT_T, t, C_GRAY);
        return;
    }
    if (l == L_ROUTE) { /* (0.6) the routes on the black keys, the drawing, target and latch */
        static const char *const TG[3] = {"THE TRACK", "BREAK", "ALL TRACKS"};
        uint8_t cur = route_of(ui.track == BS_T_BREAK || ui.route_target != 1 ? ui.track : BS_T_BREAK);
        ui_banner("ROUTE", ui.route_latch ? "LATCHED: IT STAYS" : "WHILE HELD");
        for (i = 0; i < JS_NROUTES; i++) {
            int32_t x = 2 + (int32_t)(i % 3u) * 79, y = 21 + (int32_t)(i / 3u) * 19;
            char nm[16] = "";
            char v[4];
            ui_int(v, (int32_t)i + 1); ui_cat(nm, v); ui_cat(nm, " "); ui_cat(nm, js_route_name[i]);
            cv_rect(x, y, 76, 17, i == cur ? C_HI : C_LINE);
            ui_fit(x + 3, y + 4, 71, &FONT_T, nm, i == cur ? C_BLACK : C_WHITE);
        }
        t[0] = 0; ui_cat(t, "TARGET "); ui_cat(t, TG[ui.route_target % 3u]);
        if (ui.route_target == 0) { ui_cat(t, " ("); ui_cat(t, bs_track_name[ui.track]); ui_cat(t, ")"); }
        ui_fit(4, 80, 232, &FONT_XS, t, C_WHITE);
        if (ui.draw_n) {
            uint32_t k2;
            char v[4];
            t[0] = 0; ui_cat(t, "DRAWN:");
            for (k2 = 0; k2 < ui.draw_n && k2 < 12u; k2++) { ui_cat(t, " "); ui_int(v, ui.draw[k2] + 1); ui_cat(t, v); }
            ui_fit(4, 93, 232, &FONT_XS, t, C_HI);
        } else
            ui_fit(4, 93, 232, &FONT_XS, "BLACK 1-9: A ROUTE  WHITE IN ORDER: DRAW ONE", C_GRAY);
        ui_fit(4, 105, 232, &FONT_T, "K1 TARGET  K2 LATCH", C_DIM);
        return;
    }
    if (l == L_SEL) {
        ui_fit(6, 24, 228, &FONT_S, "SEL + LFO   UNDO", C_WHITE);
        ui_fit(6, 40, 228, &FONT_S, "SEL + ENV   REDO", C_WHITE);
        ui_fit(6, 56, 228, &FONT_S, "SEL + REC   RETURN TO THE GROOVE", C_WHITE);
        ui_fit(6, 72, 228, &FONT_S, "SEL + PLAY  PANIC", C_WHITE);
        ui_fit(6, 88, 228, &FONT_XS, "SEL + SELECT: TEMPO   SEL + KNOB: FINE", C_GRAY);
        ui_fit(6, 101, 228, &FONT_XS, "STEP HELD + SEL + KNOB: A SOUND LOCK", C_GRAY);
        return;
    }
    switch (l) {
    case L_FX:
        for (i = 0; i < 8u; i++) { name[i] = PERF_KEY[i]; lit[i] = eng.perf && eng.perf == PERF_MODE[i] && (i != 1u || eng.roll_div == 3) && (i != 0u || eng.roll_div != 3); }
        black = "KNOBS: PUNCH GRIT GLUE PRESSURE";
        break;
    case L_SONG:
        for (i = 0; i < BS_NSCENES; i++) { name[i] = SCENE_NAME[i]; lit[i] = (int)i == eng.scene_cur || (int)i == eng.scene_pending; }
        for (i = 0; i < BS_NPATTERNS; i++) { name[8 + i] = PAT_KEY[i]; lit[8 + i] = i == eng.cur_pat; }
        black = "BLACK KEYS: MUTE / UNMUTE A TRACK";
        break;
    case L_FILL:
        for (i = 0; i < BS_NFILLS; i++) { name[i] = FILL_KEY[i]; lit[i] = eng.fill_on && eng.fill_type == i; }
        black = set.fill_bars == 2 ? "K1 LENGTH: 2 BARS" : "K1 LENGTH: 1 BAR";
        break;
    case L_VARY:
        for (i = 0; i < BS_NPATTERNS; i++) { name[i] = VAR_NAME[i]; lit[i] = i == eng.cur_pat; }
        for (i = 0; i < BS_NFORMS; i++) { name[8 + i] = bs_form_name[i]; lit[8 + i] = i == eng.form; }
        black = "K1 DENSITY K2 HUMANIZE K3 MUTATE K4 GHOSTS";
        break;
    case L_ERASE:
        t[0] = 0; ui_cat(t, "WHITE: CLEAR A STEP OF "); ui_cat(t, bs_track_name[ui.track]);
        ui_fit(6, 28, 228, &FONT_S, t, C_WHITE);
        ui_fit(6, 48, 228, &FONT_S, "BLACK: CLEAR A WHOLE TRACK", C_WHITE);
        ui_fit(6, 72, 228, &FONT_XS, "SEL + LFO UNDOES IT", C_GRAY);
        return;
    case L_STORE:
        for (i = 0; i < BS_NSCENES; i++) { name[i] = SCENE_NAME[i]; lit[i] = eng.scene[i].used; }
        black = ui.page == PG_DNA ? "WHITE 9-12: THE GENOME INTO P1-P4" : "A WHITE KEY STORES THE STATE THERE";
        if (ui.page == PG_DNA) for (i = 0; i < BS_NPATTERNS; i++) name[8 + i] = PAT_KEY[i];
        break;
    default: break;
    }
    for (i = 0; i < 16u; i++) {
        int32_t x = 2 + (int32_t)(i & 3u) * 59, y = 21 + (int32_t)(i >> 2) * 20;
        if (!name[i]) { cv_rect(x, y, 56, 18, C_BLACK); cv_rect(x, y + 17, 56, 1, C_LINE); continue; }
        cv_rect(x, y, 56, 18, lit[i] ? C_HI : C_LINE);
        ui_fit(x + 3, y + 4, 51, &FONT_T, name[i], lit[i] ? C_BLACK : C_WHITE);
    }
    if (black) ui_fit(6, 103, 228, &FONT_T, black, C_GRAY);
}

/* the main page: the selected track big, every track as a mini row */
static void main_page(void)
{
    uint8_t on[16], vel[16], mark[16];
    int play;
    uint32_t i;
    const char *gname = ui.groove_of[eng.cur_pat] >= 0 ? bs_groove_name[ui.groove_of[eng.cur_pat]] : PATTERN_NAME[eng.cur_pat];
    ui_fit(4, 1, 96, &FONT_B, bs_track_name[ui.track], C_HI);
    if (route_of(ui.track) == JS_R_FORWARD) ui_fit(102, 4, 98, &FONT_XS, gname, C_GRAY);
    ui_fit(204, 4, 34, &FONT_XS, ui.step_page ? "17-32" : "1-16", C_DIM);
    track_cells(ui.track, on, vel, mark, &play);
    if (route_of(ui.track) != JS_R_FORWARD) for (i = 0; i < 16u; i++) mark[i] &= (uint8_t)~4u; /* room for the order */
    ui_grid(19, on, vel, mark, play, ui.held_step);
    grid_overlay(ui.track, 0);
    if (route_of(ui.track) != JS_R_FORWARD) { char rn[20] = "ROUTE "; ui_cat(rn, js_route_name[route_of(ui.track) % JS_NROUTES]); ui_fit(102, 4, 98, &FONT_XS, rn, C_HI); }
    for (i = 0; i < BS_NTRACKS; i++) mini_row(0, 73 + (int32_t)i * 6, 15, 4, i, i == ui.track);
    if (ui.rec) { cv_rect(206, 1, 32, 14, UI_RED); ui_fit(209, 3, 28, &FONT_XS, "REC", C_WHITE); }
    if (held_step() && keys_are_steps()) { /* the held step's values */
        const js_step_t *st = held_step();
        char t[48], v[8];
        int tune = js_step_lock(st, BS_LK_TUNE);
        t[0] = 0;
        ui_cat(t, "STEP "); ui_int(v, ui.step_page * 16 + ui.held_step + 1); ui_cat(t, v);
        ui_cat(t, "  "); ui_cat(t, js_cond_name[st->cond % JS_NCOND]);
        if (ui.track == BS_T_BREAK) { ui_cat(t, "  SLICE "); ui_int(v, st->data + 1); ui_cat(t, v); }
        else if (tune >= 0) { ui_cat(t, "  TUNE "); ui_fix1(v, (float)(tune - 128) * 0.25f); ui_cat(t, v); }
        {
            char u[48] = "";
            ui_cat(u, "VEL "); ui_int(v, st->vel); ui_cat(u, v);
            ui_cat(u, "  PROB "); ui_int(v, st->prob); ui_cat(u, v); ui_cat(u, "%");
            ui_cat(u, "  RATCH "); ui_int(v, st->ratch + 1); ui_cat(u, v);
            ui_cat(u, "  MICRO "); ui_int(v, st->micro); ui_cat(u, v);
            ui_popup(76, t, u);
        }
    }
}

/* the overview: every track's 16 steps with its name, mute and length */
static void overview_page(void)
{
    uint32_t i;
    char t[40], v[8];
    for (i = 0; i < BS_NTRACKS; i++) {
        int32_t y = 2 + (int32_t)i * 13;
        int sel = i == ui.track;
        ui_fit(2, y, 30, &FONT_T, bs_track_name[i], sel ? C_WHITE : (eng.seq.mute >> i) & 1u ? C_DIM : C_GRAY);
        mini_row(32, y + 1, 13, 9, i, sel);
    }
    t[0] = 0;
    ui_cat(t, "LENGTH "); ui_int(v, eng.pat[eng.cur_pat].t[ui.track].len); ui_cat(t, v);
    ui_cat(t, "  SWING "); ui_int(v, eng.pat[eng.cur_pat].swing); ui_cat(t, v); ui_cat(t, "%  ");
    ui_cat(t, ui.groove_of[eng.cur_pat] >= 0 ? bs_groove_name[ui.groove_of[eng.cur_pat]] : PATTERN_NAME[eng.cur_pat]);
    ui_fit(4, 96, 232, &FONT_XS, t, C_GRAY);
}

/* (0.6) the DNA page: the family tree, by generation left to right; the playing genome lit, kept ones starred */
static void dna_page(void)
{
    int32_t gx[BS_DNA_MAX], gy[BS_DNA_MAX];
    uint32_t rows[8] = {0}, i;
    char t[48], v[8];
    if (dna.playing < 0) { ui_fit(6, 40, 228, &FONT_S, "NO TREE YET", C_GRAY); return; }
    for (i = 0; i < BS_DNA_MAX; i++) { /* positions: column = generation (from 7 on, the last), row = order in it */
        uint32_t c = dna.g[i].gen > 6u ? 6u : dna.g[i].gen;
        if (!dna.g[i].used) { gx[i] = gy[i] = -1; continue; }
        gx[i] = 3 + (int32_t)c * 34;
        gy[i] = 3 + (int32_t)(rows[c] % 6u) * 15;
        rows[c]++;
    }
    for (i = 0; i < BS_DNA_MAX; i++) { /* the lines to the parents */
        int p = dna.g[i].parent;
        if (gx[i] < 0 || p < 0 || gx[p] < 0) continue;
        cv_rect(gx[p] + 28, gy[p] + 6, 3, 1, C_DIM);
        cv_rect(gx[p] + 30, gy[p] < gy[i] ? gy[p] + 6 : gy[i] + 6, 1, (gy[p] < gy[i] ? gy[i] - gy[p] : gy[p] - gy[i]) + 1, C_DIM);
        cv_rect(gx[p] + 30, gy[i] + 6, gx[i] - gx[p] - 30, 1, C_DIM);
    }
    for (i = 0; i < BS_DNA_MAX; i++) {
        int pl = (int)i == dna.playing, cr = (int)i == ui.cross_a;
        if (gx[i] < 0) continue;
        cv_rect(gx[i], gy[i], 28, 13, pl ? C_HI : cr ? C_WHITE : C_LINE);
        t[0] = 0; ui_int(v, (int32_t)i + 1); ui_cat(t, v); if (dna.g[i].keep) ui_cat(t, "*");
        ui_fit(gx[i] + 3, gy[i] + 3, 24, &FONT_T, t, pl || cr ? C_BLACK : C_WHITE);
    }
    t[0] = 0; ui_cat(t, "PLAYING "); ui_int(v, dna.playing + 1); ui_cat(t, v);
    ui_cat(t, "  GEN "); ui_int(v, dna.g[(int)dna.playing].gen); ui_cat(t, v);
    ui_cat(t, "  "); ui_int(v, bs_dna_count(&dna)); ui_cat(t, v); ui_cat(t, "/16");
    if (ui.cross_a == -1) ui_cat(t, "  CROSS: PRESS A");
    else if (ui.cross_a >= 0) ui_cat(t, "  CROSS: PRESS B");
    ui_fit(4, 92, 232, &FONT_XS, t, C_WHITE);
    ui_fit(4, 104, 232, &FONT_T, "LFO BREED  B1 KEEP  B2 KILL  B3 CROSS  B4 NEW", C_GRAY);
}

static void song_page(void)
{
    uint32_t i, bar = eng.clock.running ? (eng.clock.abs_step >> 4) & 3u : 4u;
    char t[48], v[8];
    for (i = 0; i < BS_NSCENES; i++) {
        int32_t x = 2 + (int32_t)(i % 4u) * 59, y = 2 + (int32_t)(i / 4u) * 30;
        int cur = (int)i == eng.scene_cur, pend = (int)i == eng.scene_pending;
        cv_rect(x, y, 56, 27, cur ? C_HI : pend ? C_DIM : C_LINE);
        ui_fit(x + 3, y + 3, 51, &FONT_XS, SCENE_NAME[i], cur ? C_BLACK : C_WHITE);
        ui_fit(x + 3, y + 15, 51, &FONT_T, eng.scene[i].used ? "" : "EMPTY", C_GRAY);
    }
    ui_fit(4, 66, 46, &FONT_XS, "FORM", C_GRAY);
    for (i = 0; i < 4u; i++) {
        int32_t x = 52 + (int32_t)i * 24;
        int lit = eng.form && i == bar;
        char c[2] = {(char)('A' + (eng.form ? bs_form[eng.form][i] : eng.cur_pat)), 0};
        cv_rect(x, 63, 21, 20, lit ? C_HI : eng.form ? C_LINE : C_BLACK);
        ui_fit(x + 5, 65, 14, &FONT_B, eng.form ? c : "-", lit ? C_BLACK : eng.form ? C_WHITE : C_DIM);
    }
    ui_fit(152, 66, 84, &FONT_XS, bs_form_name[eng.form % BS_NFORMS], eng.form ? C_HI : C_GRAY);
    t[0] = 0;
    ui_cat(t, "PHRASE "); ui_int(v, eng.phrase); ui_cat(t, v); ui_cat(t, " BARS  QUANT ");
    ui_cat(t, QUANT_NAME[set.scene_quant % 6u]);
    ui_fit(4, 88, 232, &FONT_XS, t, C_GRAY);
    t[0] = 0;
    ui_cat(t, "ANCHORS "); ui_cat(t, eng.anchors ? "ON" : "OFF");
    ui_cat(t, "  LINK KICKS "); ui_cat(t, eng.send_kicks ? "ON" : "OFF");
    ui_cat(t, "  DECAY "); /* (0.6) */
    if (eng.decay_on) { ui_int(v, set.decay * 5); ui_cat(t, v); ui_cat(t, "%"); } else ui_cat(t, "OFF");
    ui_fit(4, 101, 232, &FONT_XS, t, C_DIM);
}

static void draw_body(void)
{
    char t[40];
    uint32_t i;
    int l = layer();
    cv_begin(240, UI_BODY_H, C_BLACK);
    if (l && !(l == L_SEL && held_step())) { layer_page(l); cv_blit(0, UI_BODY_Y); return; }
    switch (ui.page) {
    case PG_MAIN: main_page(); break;
    case PG_SEQ: overview_page(); break;
    case PG_SOUND:
        ui_fit(6, 2, 234 - (6), &FONT_M, bs_track_name[ui.track], C_HI);
        if (ui.track == BS_T_BREAK) {
            const jb_break_t *b = &eng.breaks[eng.cur_break];
            ui_fit(6, 30, 234 - (6), &FONT_B, b->name, C_AMB);
            t[0] = 0; ui_int(t, (int32_t)b->nslices); ui_cat(t, " SLICES  ");
            { char v[12]; ui_fix1(v, b->bpm); ui_cat(t, v); } ui_cat(t, " BPM  ");
            ui_cat(t, eng.has_bank ? "BANK" : "TEST BREAK");
            ui_fit(6, 47, 234 - (6), &FONT_S, t, C_GRAY);
            /* the waveform, slice marks over it */
            for (i = 0; i < 228u; i++) {
                uint32_t a = (uint32_t)((float)b->frames * (float)i / 228.0f), j, m = 0;
                for (j = 0; j < 64u && a + j < b->frames; j++) { int32_t sm = b->pcm[a + j]; sm = sm < 0 ? -sm : sm; if ((uint32_t)sm > m) m = (uint32_t)sm; }
                m = m * 18u / 32768u;
                cv_rect(6 + (int32_t)i, 84 - (int32_t)m, 1, (int32_t)(2u * m + 1u), C_DIM);
            }
            for (i = 0; i < b->nslices; i++) cv_rect(6 + (int32_t)(228.0f * (float)b->start[i] / (float)b->frames), 66, 1, 37, C_AMB);
            t[0] = 0; ui_cat(t, "RHYTHM "); ui_cat(t, eng.rhythm.node[eng.rv.near].name);
            ui_fit(6, 104, 234 - (6), &FONT_XS, t, C_GRAY);
        } else {
            static const char *const PV[JD_PERC_NVAR] = {"TOM", "RIM", "SHAKER", "CLICK"};
            char v[8];
            ui_fit(6, 30, 128, &FONT_B, jd_model_name[DRUM_MODEL[ui.track]], C_AMB);
            if (ui.track == BS_T_PERC) ui_fit(140, 30, 234 - (140), &FONT_B, PV[eng.snd[BS_T_PERC].variant % JD_PERC_NVAR], C_WHITE);
            ui_fit(6, 52, 234 - (6), &FONT_S, "KIT NODE", C_GRAY);
            ui_fit(80, 52, 234 - (80), &FONT_S, eng.kit.node[eng.kv.near].name, C_AMB);
            ui_fit(6, 68, 234 - (6), &FONT_S, "RHYTHM", C_GRAY);
            ui_fit(80, 68, 234 - (80), &FONT_S, eng.rhythm.node[eng.rv.near].name, C_AMB);
            t[0] = 0; ui_cat(t, "LEVEL "); ui_pct(v, eng.snd[ui.track].level); ui_cat(t, v);
            ui_fit(6, 84, 234 - (6), &FONT_S, t, C_GRAY);
            ui_fit(6, 101, 234 - (6), &FONT_XS, "THE KEYS PLAY IT: SOFT TO HARD", C_DIM);
        }
        break;
    case PG_FX: {
        char v[12];
        ui_fit(6, 2, 234 - (6), &FONT_M, "DRUM PRESSURE", C_HI);
        ui_fit(6, 32, 234 - (6), &FONT_S, "PEAK", C_GRAY);
        ui_meter(70, 37, 160, eng.bus.peak, C_HI);
        eng.bus.peak *= 0.6f;
        ui_fit(6, 48, 234 - (6), &FONT_S, "GLUE GR", C_GRAY);
        ui_meter(70, 53, 160, -eng.bus.comp.gr_db / 12.0f, C_AMB);
        ui_fit(6, 64, 234 - (6), &FONT_S, "ROOM", C_GRAY);
        ui_meter(70, 69, 160, eng.bus.m.room, C_AMB);
        t[0] = 0; ui_cat(t, "TRIM "); ui_fix1(v, eng.bus.m.trim_db); ui_cat(t, v); ui_cat(t, " DB   GUARD HITS ");
        ui_int(v, (int32_t)eng.bus.guard.hits); ui_cat(t, v);
        ui_fit(6, 82, 234 - (6), &FONT_S, t, C_GRAY);
        ui_fit(6, 100, 234 - (6), &FONT_XS, "HOLD FX: THE PERFORMANCE FX", C_DIM);
        break;
    }
    case PG_SONG: song_page(); break;
    case PG_DNA: dna_page(); break;
    default: { /* SYSTEM */
        char v[16];
        t[0] = 0; ui_cat(t, "CPU "); ui_int(v, (int32_t)plat_cpu_pct()); ui_cat(t, v); ui_cat(t, "%  XRUNS "); ui_int(v, (int32_t)plat_xruns()); ui_cat(t, v);
        ui_fit(6, 2, 234 - (6), &FONT_S, t, C_HI);
        t[0] = 0; ui_cat(t, "CLOCK "); ui_cat(t, eng.clock.source == JS_CLK_MIDI ? "MIDI IN" : "INTERNAL");
        ui_cat(t, eng.send_clock ? "  SYNC OUT" : "");
        ui_fit(6, 18, 234 - (6), &FONT_S, t, C_GRAY);
        t[0] = 0; ui_cat(t, "LINK CH "); ui_int(v, eng.link.channel + 1); ui_cat(t, v); ui_cat(t, jl_alive(&eng.link, eng.ms) ? "  PEER OK" : "  NO PEER");
        ui_fit(6, 34, 234 - (6), &FONT_S, t, C_GRAY);
        t[0] = 0; ui_cat(t, "BANK "); ui_cat(t, eng.has_bank ? eng.breaks[0].name : "NONE (TEST BREAK)");
        ui_cat(t, "  LOCK "); ui_cat(t, LOCK_NAME[eng.breaklock % 3u]);
        ui_fit(6, 50, 234 - (6), &FONT_S, t, C_GRAY);
        t[0] = 0; ui_cat(t, "STORE "); ui_cat(t, load_report ? "DEFAULTS USED" : "OK"); ui_cat(t, dirty ? "  UNSAVED" : "");
        ui_fit(6, 66, 234 - (6), &FONT_S, t, load_report ? C_WHITE : C_GRAY);
        t[0] = 0; ui_cat(t, "QUEUE DROPS "); ui_int(v, (int32_t)eng.q.dropped); ui_cat(t, v);
        ui_cat(t, "  CMD REFUSED "); ui_int(v, (int32_t)eng.cmd.refused); ui_cat(t, v);
        ui_fit(6, 82, 234 - (6), &FONT_S, t, C_GRAY);
        {
            static const char *const UH[3] = {"DEVICE", "HOST: LOOKING", "HOST: CONNECTED"};
            int st = plat_usb_host_state();
            t[0] = 0; ui_cat(t, "USB "); ui_cat(t, UH[st >= 0 && st <= 2 ? st : 0]);
            ui_fit(6, 98, 234 - (6), &FONT_S, t, st == USBH_CONNECTED ? C_HI : C_GRAY);
        }
        break;
    }
    }
    cv_blit(0, UI_BODY_Y);
}

/* the strip: the encoders and the keys on this page, or in the held layer */
static void draw_strip(void)
{
    ui_kg_t g[8];
    const char *enc[3] = {0, 0, 0};
    uint32_t n = 0;
    int l = layer(), stepheld = held_step() && keys_are_steps();
    switch (l) {
    case L_SEL:
        g[n++] = (ui_kg_t)UI_W(0, 15, "HOLD A STEP + SEL + KNOB: A SOUND LOCK");
        g[n++] = (ui_kg_t)UI_B(0, 6, "TRACKS");
        enc[0] = "TEMPO"; enc[2] = ui.page == PG_FX ? "TRIM" : 0;
        break;
    case L_STORE: g[n++] = (ui_kg_t)UI_W(0, 7, "STORE SCENE 1-8"); break;
    case L_ERASE: g[n++] = (ui_kg_t)UI_W(0, 15, "CLEAR THAT STEP"); g[n++] = (ui_kg_t)UI_B(0, 6, "CLEAR TRACK"); break;
    case L_FILL: g[n++] = (ui_kg_t)UI_W(0, 3, "FILL NOW"); break;
    case L_VARY: g[n++] = (ui_kg_t)UI_W(0, 3, "VARIATION"); g[n++] = (ui_kg_t)UI_W(8, 13, "FORM"); break;
    case L_SONG:
        g[n++] = (ui_kg_t)UI_W(0, 7, "SCENES"); g[n++] = (ui_kg_t)UI_W(8, 11, "PATTERNS");
        g[n++] = (ui_kg_t)UI_B(0, 6, "MUTE");
        break;
    case L_FX: g[n++] = (ui_kg_t)UI_W(0, 7, "PERFORM FX WHILE HELD"); break;
    case L_ROUTE: g[n++] = (ui_kg_t)UI_W(0, 15, "PRESS IN ORDER: DRAW A ROUTE"); g[n++] = (ui_kg_t)UI_B(0, 8, "ROUTES 1-9"); break;
    case L_ENERGY:
        g[n++] = (ui_kg_t)UI_W(0, 15, "THE STEP'S LEVEL 1-4"); g[n++] = (ui_kg_t)UI_B(0, 6, "TRACKS");
        enc[0] = "ENERGY";
        break;
    default:
        if (ui.page == PG_DNA) {
            g[n++] = (ui_kg_t)UI_W(0, 15, "PLAY A GENOME (NEXT BAR)");
            g[n++] = (ui_kg_t)UI_B(0, 3, "KEEP KILL CROSS NEW");
            enc[2] = "PATTERN";
            break;
        }
        if (ui.page == PG_SOUND) {
            g[n++] = (ui_kg_t)UI_W(0, 15, ui.track == BS_T_BREAK ? "PLAY SLICES 1-16" : "PLAY IT: SOFT (LEFT) TO HARD (RIGHT)");
            enc[1] = ui.track == BS_T_PERC ? "VOICE" : "KIT NODE";
        } else if (ui.rec)
            g[n++] = (ui_kg_t)UI_W(0, 15, ui.track == BS_T_BREAK ? "RECORD: PLAY SLICES 1-16" : "RECORD: SOFT (LEFT) TO HARD");
        else
            g[n++] = (ui_kg_t)UI_W(0, 15, stepheld ? "STEP HELD: TURN A KNOB OR AN ENCODER" : "STEPS: TAP ON/OFF, HOLD + KNOB EDITS");
        g[n++] = (ui_kg_t)UI_B(0, 6, "TRACKS");
        enc[0] = "TRACK";
        if (stepheld) { enc[1] = "CONDITION"; enc[2] = ui.track == BS_T_BREAK ? "SLICE" : "TUNE"; }
        else if (ui.page == PG_MAIN || ui.page == PG_SEQ) { enc[1] = "GROOVE"; enc[2] = "PATTERN"; }
        else if (ui.page == PG_SOUND) enc[2] = "PATTERN";
        else if (ui.page == PG_FX) { enc[1] = "PRESET"; enc[2] = "ROOM"; }
        else if (ui.page == PG_SONG) { enc[1] = "ANCHORS"; enc[2] = "LINK KICK"; }
        else { enc[1] = "BRK LOCK"; enc[2] = "USB HOST"; }
        break;
    }
    ui_strip(enc, g, n, l ? LAYER_NAME[l] : 0);
}

static void draw_footer(void)
{
    const char *name[4] = {0, 0, 0, 0}, *vt[4] = {0, 0, 0, 0};
    float v[4] = {0, 0, 0, 0};
    char b[4][12];
    uint32_t k;
    int l = layer();
    js_pattern_t *pt = &eng.pat[eng.cur_pat];
    js_step_t *st = held_step();
    if (st && keys_are_steps() && (l == L_NONE || l == L_SEL)) {
        if (l == L_SEL && ui.track != BS_T_BREAK) {
            for (k = 0; k < 4u; k++) {
                int lk = js_step_lock(st, (uint8_t)(BS_LK_M0 + k));
                name[k] = jd_macro_name[DRUM_MODEL[ui.track]][k];
                v[k] = lk < 0 ? eng.snd[ui.track].m[k] : (float)lk / 255.0f;
                vt[k] = lk < 0 ? "-" : 0;
            }
        } else {
            name[0] = "VEL"; name[1] = "PROB"; name[2] = "RATCH"; name[3] = "MICRO";
            v[0] = (float)st->vel / 127.0f; v[1] = (float)st->prob / 100.0f; v[2] = (float)st->ratch / 7.0f; v[3] = ((float)st->micro + 64.0f) / 127.0f;
            ui_int(b[0], st->vel); vt[0] = b[0];
            ui_int(b[2], st->ratch + 1); vt[2] = b[2];
            ui_int(b[3], st->micro); vt[3] = b[3];
        }
        ui_macros(name, v, vt, ui.hi_until && plat_ms() < ui.hi_until ? ui.hi_macro : -1);
        return;
    }
    if (l == L_ROUTE || l == L_ENERGY) { /* (0.6) */
        static const char *const TG[3] = {"TRACK", "BREAK", "ALL"};
        static const char *const CM[3] = {"OFF", "REC", "PLAY"};
        if (l == L_ROUTE) {
            name[0] = "TARGET"; name[1] = "LATCH"; name[2] = name[3] = "";
            v[0] = (float)ui.route_target / 2.0f; v[1] = ui.route_latch;
            vt[0] = TG[ui.route_target % 3u]; vt[1] = ui.route_latch ? "ON" : "OFF"; vt[2] = vt[3] = "";
        } else {
            int cm = bs_energy_curve_mode(&eng);
            name[0] = "ENERGY"; name[1] = "CURVE"; name[2] = "AUTO LVL"; name[3] = "";
            v[0] = eng.nrg.knob; v[1] = (float)cm / 2.0f; v[2] = 0.0f;
            vt[1] = CM[cm % 3]; vt[2] = "TURN"; vt[3] = "";
        }
        ui_macros(name, v, vt, ui.hi_until && plat_ms() < ui.hi_until ? ui.hi_macro : -1);
        return;
    }
    switch (l == L_FX ? -1 : l == L_SONG ? PG_SONG : (int)ui.page) {
    case -1: case PG_FX:
        name[0] = "PUNCH"; name[1] = "GRIT"; name[2] = "GLUE"; name[3] = "PRESSURE";
        v[0] = eng.bus.m.punch; v[1] = eng.bus.m.crush; v[2] = eng.bus.m.glue; v[3] = eng.bus.m.pressure;
        break;
    case PG_MAIN:
        if (ui.track == BS_T_BREAK) {
            name[0] = "PITCH"; name[1] = "LEVEL"; name[2] = "SLICES"; name[3] = "GATE";
            v[0] = (eng.player.pitch + 12.0f) / 24.0f; v[1] = eng.break_level; v[2] = (float)eng.slices / 32.0f; v[3] = eng.player.gate;
            ui_fix1(b[0], eng.player.pitch); vt[0] = b[0];
            ui_int(b[2], eng.slices); vt[2] = b[2];
        } else
            for (k = 0; k < 4u; k++) { name[k] = jd_macro_name[DRUM_MODEL[ui.track]][k]; v[k] = eng.snd[ui.track].m[k]; }
        break;
    case PG_SEQ: {
        static const char *const DIV[3] = {"1/16", "1/32", "1/8"};
        name[0] = "LENGTH"; name[1] = "SWING"; name[2] = "GHOSTS"; name[3] = "SPEED";
        v[0] = (float)pt->t[ui.track].len / 32.0f; v[1] = ((float)pt->swing - 50.0f) / 25.0f; v[2] = ui.ghost_density; v[3] = (float)pt->t[ui.track].div / 2.0f;
        ui_int(b[0], pt->t[ui.track].len); vt[0] = b[0];
        ui_int(b[1], pt->swing); ui_cat(b[1], "%"); vt[1] = b[1];
        vt[3] = DIV[pt->t[ui.track].div % 3u];
        break;
    }
    case PG_SOUND:
        name[0] = "RHYTHM"; name[1] = "KIT"; name[2] = "FILL AMT"; name[3] = "LEVEL";
        v[0] = eng.rhythm_t; v[1] = eng.kit_t; v[2] = eng.energy;
        v[3] = ui.track == BS_T_BREAK ? eng.break_level : eng.snd[ui.track].level;
        break;
    case PG_SONG:
        name[0] = "QUANT"; name[1] = "PHRASE"; name[2] = "FORM"; name[3] = "DECAY";
        v[0] = (float)set.scene_quant / 5.0f; v[1] = eng.phrase == 4 ? 0.0f : eng.phrase == 8 ? 0.5f : 1.0f;
        v[2] = (float)eng.form / (float)(BS_NFORMS - 1); v[3] = (float)set.decay / 20.0f;
        vt[0] = QUANT_NAME[set.scene_quant % 6u]; ui_int(b[1], eng.phrase); vt[1] = b[1];
        vt[2] = eng.form ? bs_form_name[eng.form] : "OFF";
        if (set.decay) { ui_int(b[3], set.decay * 5); ui_cat(b[3], "%"); vt[3] = b[3]; } else vt[3] = "OFF";
        break;
    case PG_DNA:
        name[0] = "MUTATE"; name[1] = name[2] = name[3] = "";
        v[0] = ui.mutate_amt; vt[1] = vt[2] = vt[3] = "";
        break;
    default:
        name[0] = "BPM"; name[1] = "CLOCK"; name[2] = "LINK CH"; name[3] = "SYNC OUT";
        v[0] = (eng.clock.bpm - 60.0f) / 160.0f; v[1] = eng.clock.source; v[2] = (float)eng.link.channel / 15.0f; v[3] = eng.send_clock;
        ui_fix1(b[0], eng.clock.bpm); vt[0] = b[0];
        vt[1] = eng.clock.source == JS_CLK_MIDI ? "MIDI" : "INT";
        ui_int(b[2], eng.link.channel + 1); vt[2] = b[2];
        vt[3] = eng.send_clock ? "ON" : "OFF";
        break;
    }
    if (l == L_VARY) {
        name[0] = "DENSITY"; name[1] = "HUMANIZE"; name[2] = "MUTATE"; name[3] = "GHOSTS";
        { uint32_t i, on = 0, len = pt->t[ui.track].len; for (i = 0; i < len; i++) on += (pt->t[ui.track].s[i].flags & JS_ON) != 0; v[0] = len ? (float)on / (float)len : 0.0f; ui_int(b[0], (int32_t)on); vt[0] = b[0]; }
        v[1] = ui.humanize; v[2] = ui.mutate_amt; v[3] = ui.ghost_density;
        vt[1] = vt[2] = vt[3] = 0;
    } else if (l == L_FILL) {
        name[0] = "LENGTH"; name[1] = name[2] = name[3] = "";
        v[0] = set.fill_bars == 2 ? 1.0f : 0.5f; v[1] = v[2] = v[3] = 0;
        vt[0] = set.fill_bars == 2 ? "2 BARS" : "1 BAR"; vt[1] = vt[2] = vt[3] = "";
    }
    ui_macros(name, v, vt, ui.hi_until && plat_ms() < ui.hi_until ? ui.hi_macro : -1);
}

static void lights(void)
{
    uint32_t b = 0, keys = 0, dimk = 0, k;
    static const uint8_t PB[NPAGES] = {B_HOME, B_SEQ, B_EDIT, B_FX, B_ARP, B_GLO, B_SEQ};
    int l = layer();
    b |= 1u << PB[ui.page];
    if (eng.clock.running) b |= 1u << B_PLAY;
    if (dirty) b |= 1u << B_SAVE;
    if (eng.fill_on) b |= 1u << B_ENV;
    if (ui.rec || eng.return_pending) b |= 1u << B_REC;
    if (eng.form) b |= 1u << B_LFO;
    if (eng.perf) b |= 1u << B_FX;
    if (l == L_FX) {
        for (k = 0; k < 8u; k++) dimk |= ui_white_bit((int)k);
        if (eng.perf) for (k = 0; k < 8u; k++) if (PERF_MODE[k] == eng.perf && (k > 1u || (k == 1u) == (eng.roll_div == 3))) keys |= ui_white_bit((int)k);
    } else if (l == L_SONG || l == L_STORE) {
        for (k = 0; k < BS_NSCENES; k++) if (eng.scene[k].used) dimk |= ui_white_bit((int)k);
        if (eng.scene_cur >= 0) keys |= ui_white_bit(eng.scene_cur);
        if (l == L_SONG) {
            keys |= ui_white_bit(8 + eng.cur_pat);
            for (k = 0; k < BS_NTRACKS; k++) if (!((eng.seq.mute >> k) & 1u)) keys |= ui_black_bit((int)k);
        }
    } else if (l == L_VARY) {
        for (k = 0; k < 4u; k++) dimk |= ui_white_bit((int)k);
        for (k = 0; k < BS_NFORMS; k++) dimk |= ui_white_bit(8 + (int)k);
        keys |= ui_white_bit(eng.cur_pat) | ui_white_bit(8 + eng.form);
    } else if (l == L_FILL) {
        for (k = 0; k < BS_NFILLS; k++) dimk |= ui_white_bit((int)k);
        if (eng.fill_on) keys |= ui_white_bit(eng.fill_type);
    } else if (l == L_ROUTE) { /* (0.6) the routes on the black keys, the one in force lit */
        for (k = 0; k < JS_NROUTES; k++) dimk |= ui_black_bit((int)k);
        keys |= ui_black_bit(route_of(ui.track));
        for (k = 0; k < ui.draw_n; k++) if (ui.draw[k] / 16u == ui.step_page) keys |= ui_white_bit(ui.draw[k] % 16u);
    } else if (ui.page == PG_DNA && l == L_NONE) { /* the genomes: used dim, the playing one lit */
        for (k = 0; k < BS_DNA_MAX; k++) if (dna.g[k].used) dimk |= ui_white_bit((int)k);
        if (dna.playing >= 0) keys |= ui_white_bit(dna.playing);
        for (k = 0; k < 4u; k++) dimk |= ui_black_bit((int)k);
    } else if (ui.page == PG_SOUND || ui.rec) {
        keys = ui.in.keys;
        keys |= ui_black_bit(ui.track);
    } else {
        const js_track_t *tr = &eng.pat[eng.cur_pat].t[ui.track];
        for (k = 0; k < 16u; k++)
            if (tr->s[(ui.step_page * 16u + k) % JS_MAXSTEPS].flags & JS_ON) keys |= ui_white_bit((int)k);
        keys |= ui_black_bit(ui.track);
        if (eng.clock.running) {
            int play;
            uint8_t on[16];
            track_cells(ui.track, on, 0, 0, &play);
            if (play >= 0) dimk |= ui_white_bit(play);
        }
    }
    plat_lights(b, keys, 0, dimk, 0);
}

void app_ui_init(void)
{
    ui_theme(UI_THEME_ACID);
    if (set.link_usb_host) plat_usb_host(1); /* (main loop only: plat.h) */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui_splash("BREAK", "SURGEON", "JUNGLE OS " APP_VERSION "  -  BREAKS AND DRUMS");
    ui.splash_until = plat_ms() + 1200u;
    ui.redraw = 1;
    if (load_report & 1u) app_say("SETTINGS", "DEFAULTS LOADED");
}

void app_input_only(void) { input(); }

void app_frame(void)
{
    static uint32_t last_step, last_t;
    uint32_t now = plat_ms();
    input();
    lights();
    if (ui.save_at && now >= ui.save_at && !eng.clock.running && dirty) { auto_save = 1; save_all(); ui.save_at = 0; }
    /* (0.6) TAPE DECAY: the fading steps fade once a bar */
    if (eng.clock.running && (eng.clock.abs_step >> 4) != ui.last_bar) {
        ui.last_bar = eng.clock.abs_step >> 4;
        if (eng.decay_on) { bs_decay_bar(&eng); ui.redraw = 1; }
    }
    if (ui.splash_until) {
        if (now < ui.splash_until) return;
        ui.splash_until = 0;
    }
    if (eng.clock.abs_step != last_step) { last_step = eng.clock.abs_step; ui.redraw = 1; }
    if (ui.msg_until && now >= ui.msg_until) { ui.msg_until = 0; ui.redraw = 1; }
    if (ui.hi_until && now >= ui.hi_until) { ui.hi_until = 0; ui.redraw = 1; }
    if (now - last_t > 120u) { last_t = now; ui.redraw = 1; } /* the meters */
    if (!ui.redraw) return;
    ui.redraw = 0;
    draw_header();
    draw_body();
    draw_strip();
    draw_footer();
}
