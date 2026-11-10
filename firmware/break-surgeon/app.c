/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* BREAK SURGEON: the app (screens, controls, storage, MIDI, the bank loader). The engine is engine/bs_engine.c
 * (-O2 unit dsp_o2.c). Part of fm1-base's unity build: gfx.c and announce.c are in scope.
 * The control map is firmware/break-surgeon/docs/MANUAL.md; keep the two in step. */
#include "engine/bs_engine.h"
#include "../../shared/store/jo_store.h"
#include "../../shared/ui/jo_ui.c"

/* ------------------------------------------------------------------ state --- */
static bs_engine_t eng PLAT_POOL;
static int16_t test_pcm[31200] PLAT_POOL;   /* the procedural test break at 22,059 Hz (jb_testbreak_frames) */
static int16_t room_buf[JF_VERB_TOTAL] PLAT_POOL; /* DRUM PRESSURE's room: 32 KiB */
_Static_assert(sizeof test_pcm / 2u >= 31142u, "the test break fits");

#define SET_MAGIC 0x31534253u               /* "SBS1" */
#define SET_VERSION 1u
typedef struct {
    float bpm, rhythm_t, kit_t, energy;
    jf_drum_macros_t fx;
    uint8_t clock_src, link_ch, send_clock, send_kicks;
    uint8_t slices, tempo_sync, breaklock, anchors;
    uint8_t scene_quant, theme, link_usb_host, rsv;
} settings_t;
static settings_t set;
#define PAT_MAGIC 0x31544150u               /* "PAT1" */
#define SCN_MAGIC 0x314E4353u               /* "SCN1" */
static struct { jo_rec_hdr_t h; uint8_t p[sizeof(js_pattern_t) > sizeof(bs_scene_t) * BS_NSCENES ? sizeof(js_pattern_t)
                                                                                    : sizeof(bs_scene_t) * BS_NSCENES]; } io;
_Static_assert(sizeof io <= 3840u, "a store object holds 3,840 bytes");
static uint8_t dirty, load_report;          /* load_report: what was replaced by defaults at boot (bits) */

enum { PG_PERFORM, PG_SEQ, PG_SOUND, PG_FX, PG_SCENE, PG_SYSTEM, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"PERFORM", "SEQUENCE", "SOUND", "FX", "SCENE", "SYSTEM"};
static const char *const SCENE_NAME[BS_NSCENES] = {"INTRO", "BUILD", "DROP", "MUTATION", "VOID", "IMPACT", "SCENE 7", "SCENE 8"};
static const char *const QUANT_NAME[5] = {"NOW", "BEAT", "BAR", "2 BAR", "4 BAR"};
static const char *const LOCK_NAME[3] = {"OFF", "LAYER", "SEPARATE"};

static struct {
    uint8_t page, track, step_page, redraw;
    int8_t held_step;                      /* SEQUENCE: the white key held (-1) */
    uint32_t key_used;                     /* keys whose hold did something (no toggle on release) */
    int8_t hi_macro;                       /* the macro box last turned */
    uint32_t hi_until;
    char msg[2][24];
    uint32_t msg_until;
    float mutate_amt, ghost_density;
    uint32_t mutate_seed;
    ui_in_t in;
    uint32_t last_draw, splash_until;
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
}

static void load_all(void)
{
    uint32_t p;
    load_report = 0;
    if (load_rec(OBJ_SETTINGS, SET_MAGIC, &set, sizeof set) || !(set.bpm >= 40.0f && set.bpm <= 300.0f) ||
        set.link_ch > 15u || set.clock_src > 1u || set.breaklock > 2u || set.scene_quant > 4u) {
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
}

/* SAVE: every object; while playing it is refused, since a flash write stalls the audio (plat.h) */
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
    else { dirty = 0; app_say("SAVED", "PATTERNS + SCENES"); }
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
    ui.hi_macro = -1;
    ui.mutate_amt = 0.5f;
    ui.ghost_density = 0.45f;
    ui.mutate_seed = 1;
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

/* --------------------------------------------------------------- macros --- */
/* the four K knobs of each page: the value's pointer (0..1), or a handler for discrete ones */
static float *page_macro(int k)
{
    js_track_t *tr = &eng.pat[eng.cur_pat].t[ui.track];
    (void)tr;
    switch (ui.page) {
    case PG_PERFORM: return k == 0 ? &eng.rhythm_t : k == 1 ? &eng.kit_t : k == 2 ? &eng.bus.m.pressure : &eng.energy;
    case PG_SOUND:
        if (ui.track == BS_T_BREAK) return k == 1 ? &eng.break_level : 0;
        return &eng.snd[ui.track].m[k];
    case PG_FX: return k == 0 ? &eng.bus.m.punch : k == 1 ? &eng.bus.m.crush : k == 2 ? &eng.bus.m.glue : &eng.bus.m.pressure;
    default: return 0;
    }
}

static void macro_changed(void)
{
    if (ui.page == PG_PERFORM) {
        bs_set_rhythm(&eng, eng.rhythm_t);
        bs_set_kit(&eng, eng.kit_t);
        jf_drum_update(&eng.bus);
    } else if (ui.page == PG_FX)
        jf_drum_update(&eng.bus);
    else if (ui.page == PG_SOUND && ui.track == BS_T_BREAK)
        eng.player.level = eng.break_level;
    dirty = 1;
}

static void turn_k(int k, int32_t d)
{
    float *v = page_macro(k), step = ui_held(&ui.in, B_SEL) ? 0.0025f : 0.01f;
    js_pattern_t *pt = &eng.pat[eng.cur_pat];
    js_track_t *tr = &pt->t[ui.track];
    ui.hi_macro = (int8_t)k;
    ui.hi_until = plat_ms() + 1200u;
    ui.redraw = 1;
    if (ui.page == PG_SEQ && ui.held_step >= 0) { /* the held step's values (or, with SEL, its macro locks) */
        js_step_t *st = &tr->s[(ui.step_page * 16 + ui.held_step) % JS_MAXSTEPS];
        ui.key_used |= ui_white_bit(ui.held_step);
        if (ui_held(&ui.in, B_SEL) && ui.track != BS_T_BREAK) {
            int cur = js_step_lock(st, (uint8_t)(BS_LK_M0 + k));
            if (cur < 0) cur = (int)(eng.snd[ui.track].m[k] * 255.0f);
            ui_use(&ui.in, B_SEL);
            if (js_step_set_lock(st, (uint8_t)(BS_LK_M0 + k), jo_clampi(cur + d * 4, 0, 255))) app_say("LOCKS", "TWO PER STEP");
        } else if (k == 0) st->vel = (uint8_t)jo_clampi(st->vel + d * 2, 1, 127);
        else if (k == 1) st->prob = (uint8_t)jo_clampi(st->prob + d * 5, 0, 100);
        else if (k == 2) st->ratch = (uint8_t)jo_clampi(st->ratch + (d > 0 ? 1 : -1), 0, 7);
        else st->micro = (int8_t)jo_clampi(st->micro + d * 4, -64, 63);
        st->flags |= JS_ON;
        dirty = 1;
        return;
    }
    switch (ui.page) {
    case PG_SEQ:
        if (k == 0) tr->len = (uint8_t)jo_clampi(tr->len + (d > 0 ? 1 : -1), 1, JS_MAXSTEPS);
        else if (k == 1) pt->swing = (uint8_t)jo_clampi(pt->swing + d, 50, 75);
        else if (k == 2) ui.ghost_density = jo_clampf(ui.ghost_density + 0.02f * (float)d, 0.0f, 1.0f);
        else ui.mutate_amt = jo_clampf(ui.mutate_amt + 0.02f * (float)d, 0.0f, 1.0f);
        dirty = 1;
        return;
    case PG_SOUND:
        if (ui.track == BS_T_BREAK) {
            if (k == 0) eng.player.pitch = jo_clampf(eng.player.pitch + 0.5f * (float)d, -12.0f, 12.0f);
            else if (k == 2) { int s = eng.slices; s = d > 0 ? (s == 8 ? 16 : 32) : (s == 32 ? 16 : 8); bs_set_slices(&eng, s); }
            else if (k == 3) eng.player.gate = jo_clampf(eng.player.gate + 0.02f * (float)d, 0.05f, 1.0f);
        }
        break;
    case PG_SCENE:
        if (k == 0) set.scene_quant = (uint8_t)jo_clampi(set.scene_quant + (d > 0 ? 1 : -1), 0, 4);
        else if (k == 1) eng.breaklock = (uint8_t)jo_clampi(eng.breaklock + (d > 0 ? 1 : -1), 0, 2);
        else if (k == 2) eng.anchors = d > 0;
        else eng.send_kicks = d > 0;
        dirty = 1;
        return;
    case PG_SYSTEM:
        if (k == 0) { set.bpm = jo_clampf((float)(int32_t)(eng.clock.bpm * 2.0f + 0.5f) * 0.5f + 0.5f * (float)d, 60.0f, 220.0f); bs_cmd_bpm(&eng, set.bpm); }
        else if (k == 1) { set.clock_src = d > 0; eng.clock.source = set.clock_src; }
        else if (k == 2) { set.link_ch = (uint8_t)jo_clampi(set.link_ch + (d > 0 ? 1 : -1), 0, 15); jl_init(&eng.link, set.link_ch); }
        else { eng.send_clock = d > 0; set.send_clock = eng.send_clock; }
        dirty = 1;
        return;
    default: break;
    }
    if (v) {
        *v = jo_clampf(*v + step * (float)d, 0.0f, 1.0f);
        macro_changed();
    }
}

/* ---------------------------------------------------------------- input --- */
static void perf_key(int w, int down)
{
    static const uint8_t MODE[7] = {BS_PERF_ROLL, BS_PERF_ROLL, BS_PERF_CHOP, BS_PERF_REVERSE, BS_PERF_GLITCH,
                                    BS_PERF_DESTROY, BS_PERF_FREEZE};
    if (w < 7) { /* held modes: down engages, up releases */
        if (down) {
            eng.roll_div = w == 1 ? 3 : 1;
            bs_cmd_perf(&eng, MODE[w]);
        } else if (eng.perf == MODE[w])
            bs_cmd_perf(&eng, BS_PERF_CLEAN);
        return;
    }
    if (!down) return;
    if (w == 7) { bs_cmd_fill(&eng, 1); app_say("FILL 1 BAR", "FROM THE NEXT BAR"); }
    else if (w < 15) {
        int solo = ui_held(&ui.in, B_SEL);
        bs_cmd_mute(&eng, w - 8, solo);
        if (solo) ui_use(&ui.in, B_SEL);
        app_say(solo ? "SOLO" : (eng.seq.mute >> (w - 8)) & 1u ? "UNMUTE" : "MUTE", bs_track_name[w - 8]);
    } else { bs_mutate(&eng, ui.mutate_amt, ui.mutate_seed++); app_say("MUTATE", "SEL+LFO: UNDO"); }
}

static void scene_key(int s)
{
    if (s < 0 || s >= BS_NSCENES) return;
    if (ui_held(&ui.in, B_SAVE)) { /* SAVE held + a scene: store (two hands: never by accident) */
        ui_use(&ui.in, B_SAVE);
        bs_scene_store(&eng, s);
        dirty = 1;
        app_say("SCENE STORED", SCENE_NAME[s]);
        return;
    }
    bs_cmd_scene(&eng, s, set.scene_quant);
    app_say(SCENE_NAME[s], QUANT_NAME[set.scene_quant]);
}

static const char *const PATTERN_NAME[BS_NPATTERNS] = {"P1 DROP", "P2 ROLLER", "P3 HALF-TIME", "P4 INTRO"};
static void pattern_step(int dir)
{
    int p = (eng.next_pat + BS_NPATTERNS + dir) % BS_NPATTERNS;
    bs_cmd_pattern(&eng, p);
    app_say(PATTERN_NAME[p], eng.clock.running ? "AT THE NEXT BAR" : "");
}

static void input(void)
{
    ui_in_t *in = &ui.in;
    uint32_t k;
    int32_t d;
    ui_poll(in);
    /* pages */
    if (ui_tapped(in, B_HOME)) ui.page = PG_PERFORM, ui.redraw = 1;
    if (ui_tapped(in, B_SEQ)) ui.page = PG_SEQ, ui.redraw = 1;
    if (ui_tapped(in, B_EDIT)) ui.page = PG_SOUND, ui.redraw = 1;
    if (ui_tapped(in, B_FX)) ui.page = PG_FX, ui.redraw = 1;
    if (ui_tapped(in, B_ARP)) ui.page = PG_SCENE, ui.redraw = 1;
    if (ui_tapped(in, B_GLO)) ui.page = PG_SYSTEM, ui.redraw = 1;
    /* transport and the safety gestures */
    if (ui_pressed(in, B_PLAY)) {
        if (ui_held(in, B_SEL)) { ui_use(in, B_SEL); bs_cmd_panic(&eng); bs_cmd_play(&eng, 0); app_say("PANIC", "ALL STOPPED"); }
        else bs_cmd_play(&eng, !eng.clock.running);
    }
    if (ui_tapped(in, B_REC)) { bs_cmd_return(&eng); app_say("RETURN", "TO THE GROOVE: NEXT BAR"); }
    if (ui_tapped(in, B_ENV)) {
        int two = ui_held(in, B_SEL);
        if (two) ui_use(in, B_SEL);
        bs_cmd_fill(&eng, two ? 2 : 1);
        app_say(two ? "FILL 2 BARS" : "FILL 1 BAR", "FROM THE NEXT BAR");
    }
    if (ui_tapped(in, B_LFO)) {
        if (ui_held(in, B_SEL)) { ui_use(in, B_SEL); app_say("UNDO", bs_undo(&eng) ? "NOTHING TO UNDO" : "BREAK RESTORED"); }
        else { bs_mutate(&eng, ui.mutate_amt, ui.mutate_seed++); app_say("MUTATE", "SEL+LFO: UNDO"); }
        dirty = 1;
    }
    if (ui_tapped(in, B_SAVE)) save_all();
    if (ui_pressed(in, B_SEL) || ui_pressed(in, B_SAVE) || (in->btn_up & (1u << B_SEL | 1u << B_SAVE))) ui.redraw = 1;
    if (ui_pressed(in, B_OCTDN) || ui_pressed(in, B_OCTUP)) {
        int dir = ui_pressed(in, B_OCTUP) ? 1 : -1;
        if (ui.page == PG_SEQ) ui.step_page = (uint8_t)jo_clampi(ui.step_page + dir, 0, 1);
        else pattern_step(dir);
        ui.redraw = 1;
    }
    /* keys */
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        int w = ui_white(k), b = ui_black(k);
        int down = (in->key_down & bit) != 0, up = (in->key_up & bit) != 0;
        if (!down && !up) continue;
        ui.redraw = 1;
        /* on every page, as on Abyss Engine: SEL + white 1-8 queues a scene, SAVE + white 1-8 stores one,
         * SEL + black 1-7 mutes a track (PERFORM: SEL + white 9-15 solos) */
        if (down && w >= 0 && w < BS_NSCENES && (ui_held(in, B_SEL) || ui_held(in, B_SAVE))) {
            if (ui_held(in, B_SEL)) ui_use(in, B_SEL);
            scene_key(w);
            ui.key_used |= bit;
            continue;
        }
        if (down && b >= 0 && b < BS_NTRACKS && ui_held(in, B_SEL)) {
            ui_use(in, B_SEL);
            bs_cmd_mute(&eng, b, 0);
            app_say((eng.seq.mute >> b) & 1u ? "UNMUTE" : "MUTE", bs_track_name[b]);
            continue;
        }
        if (up && (ui.key_used & bit) && ui.page != PG_SEQ) { ui.key_used &= ~bit; continue; }
        switch (ui.page) {
        case PG_PERFORM:
            if (w >= 0) perf_key(w, down);
            else if (down) {
                if (b < 8) scene_key(b);
                else if (b == 8) bs_cmd_fill(&eng, 2), app_say("FILL", "2 BARS");
                else if (b == 9) bs_cmd_fill(&eng, 4), app_say("FILL", "4 BARS");
                else app_say("UNDO", bs_undo(&eng) ? "NOTHING TO UNDO" : "BREAK RESTORED");
            }
            break;
        case PG_SEQ:
            if (w >= 0) {
                if (down) { ui.held_step = (int8_t)w; ui.key_used &= ~bit; }
                else {
                    if (!(ui.key_used & bit)) {
                        js_step_t *st = &eng.pat[eng.cur_pat].t[ui.track].s[(ui.step_page * 16 + w) % JS_MAXSTEPS];
                        st->flags ^= JS_ON;
                        dirty = 1;
                    }
                    if (ui.held_step == w) ui.held_step = -1;
                }
            } else if (down) {
                if (b < BS_NTRACKS) ui.track = (uint8_t)b;
                else if (b == 7) { bs_ghosts(&eng, ui.ghost_density, ui.mutate_seed++); app_say("GHOSTS", "GENERATED"); dirty = 1; }
                else if (b == 8) { bs_mutate(&eng, ui.mutate_amt, ui.mutate_seed++); app_say("MUTATE", "B10: UNDO"); dirty = 1; }
                else if (b == 9) app_say("UNDO", bs_undo(&eng) ? "NOTHING TO UNDO" : "BREAK RESTORED");
                else if (ui.held_step >= 0 && ui.track == BS_T_HATS) {
                    js_step_t *st = &eng.pat[eng.cur_pat].t[BS_T_HATS].s[(ui.step_page * 16 + ui.held_step) % JS_MAXSTEPS];
                    st->flags ^= BS_FLAG_OPEN;
                    ui.key_used |= ui_white_bit(ui.held_step);
                } else if (ui.held_step >= 0 && ui.track == BS_T_BREAK) {
                    js_step_t *st = &eng.pat[eng.cur_pat].t[BS_T_BREAK].s[(ui.step_page * 16 + ui.held_step) % JS_MAXSTEPS];
                    st->flags ^= JS_REVERSE;
                    ui.key_used |= ui_white_bit(ui.held_step);
                }
            }
            break;
        case PG_SOUND:
            if (down && w >= 0) {
                if (ui.track == BS_T_BREAK) bs_cmd_live_slice(&eng, w, 1.0f);
                else bs_cmd_live_drum(&eng, ui.track, 0.4f + 0.04f * (float)w);
            } else if (down && b >= 0 && b < BS_NTRACKS) ui.track = (uint8_t)b;
            break;
        case PG_SCENE:
            if (down && w >= 0 && w < BS_NSCENES) scene_key(w);
            break;
        default: break;
        }
    }
    /* encoders (the strip names what they do on each page; SEL + SELECT is the tempo wherever SELECT is the track) */
    if ((d = plat_enc(EN_SELECT)) != 0) {
        if ((ui.page == PG_SEQ || ui.page == PG_SOUND) && !ui_held(in, B_SEL)) ui.track = (uint8_t)jo_clampi(ui.track + (d > 0 ? 1 : -1), 0, BS_NTRACKS - 1);
        else {
            if (ui_held(in, B_SEL)) ui_use(in, B_SEL);
            set.bpm = jo_clampf(eng.clock.bpm + 0.5f * (float)d, 60.0f, 220.0f);
            bs_cmd_bpm(&eng, set.bpm);
            dirty = 1;
        }
        ui.redraw = 1;
    }
    if ((d = plat_enc(EN_ALGO)) != 0) {
        if (ui.page == PG_SEQ && ui.held_step >= 0) {
            js_step_t *st = &eng.pat[eng.cur_pat].t[ui.track].s[(ui.step_page * 16 + ui.held_step) % JS_MAXSTEPS];
            ui.key_used |= ui_white_bit(ui.held_step);
            if (ui.track == BS_T_BREAK) st->data = (uint8_t)((st->data + (uint32_t)(d > 0 ? 1 : eng.slices - 1)) % eng.slices);
            else {
                int cur = js_step_lock(st, BS_LK_TUNE);
                if (js_step_set_lock(st, BS_LK_TUNE, jo_clampi((cur < 0 ? 128 : cur) + 4 * d, 0, 255))) app_say("LOCKS", "TWO PER STEP");
            }
            st->flags |= JS_ON;
        } else if (ui.page == PG_PERFORM) {
            app_say("KIT", bs_kit_step(&eng, d)); /* EXPLORE the kit's sweetspots, one node a click */
        } else if (ui.page == PG_FX) {
            static int preset = JF_DRUM_ROLLING;
            preset = (preset + (d > 0 ? 1 : JF_DRUM_NPRESETS - 1)) % JF_DRUM_NPRESETS;
            jf_drum_preset(&eng.bus.m, preset);
            jf_drum_update(&eng.bus);
            app_say("DRUM BUS", jf_drum_preset_name[preset]);
        } else if (ui.page == PG_SOUND && ui.track == BS_T_PERC) {
            static const char *const PV[JD_PERC_NVAR] = {"TOM", "RIM", "SHAKER", "CLICK"};
            eng.snd[BS_T_PERC].variant = (uint8_t)((eng.snd[BS_T_PERC].variant + (d > 0 ? 1u : JD_PERC_NVAR - 1u)) % JD_PERC_NVAR);
            app_say("PERC VOICE", PV[eng.snd[BS_T_PERC].variant]);
        }
        dirty = 1;
        ui.redraw = 1;
    }
    if ((d = plat_enc(EN_PRESET)) != 0) {
        if (ui.page == PG_SYSTEM) { /* the link over the USB port as a host (fm1.toml usb_host): another FM-1 on OTG */
            set.link_usb_host = d > 0;
            plat_usb_host(set.link_usb_host);
            app_say("USB", set.link_usb_host ? "HOST: LOOKING" : "DEVICE");
        } else if (ui.page == PG_PERFORM) {
            bs_snap(&eng);
            app_say("SNAP", eng.kit.node[eng.kv.near].name);
        } else if (ui.page == PG_SEQ) pattern_step(d > 0 ? 1 : -1);
        else if (ui.page == PG_FX && ui_held(in, B_SEL)) {
            ui_use(in, B_SEL);
            eng.bus.m.trim_db = jo_clampf(eng.bus.m.trim_db + 0.5f * (float)d, -12.0f, 6.0f);
            jf_drum_update(&eng.bus);
        } else if (ui.page == PG_FX) {
            eng.bus.m.room = jo_clampf(eng.bus.m.room + 0.02f * (float)d, 0.0f, 1.0f);
            jf_drum_update(&eng.bus);
        } else if (ui.page == PG_SOUND && ui.track != BS_T_BREAK)
            eng.snd[ui.track].level = jo_clampf(eng.snd[ui.track].level + 0.02f * (float)d, 0.0f, 1.0f);
        dirty = 1;
        ui.redraw = 1;
    }
    for (k = 0; k < 4u; k++)
        if ((d = plat_enc(EN_K1 + (int)k)) != 0) turn_k((int)k, d);
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
    span = rem > 32u ? 64u : rem > 16u ? 32u : 16u;
    return 1.0f - (float)rem / (float)span;
}

static void draw_header(void)
{
    char mid[24] = "", right[24] = "";
    int alarm = plat_cpu_pct() > 85u || plat_xruns();
    if (ui.msg_until && plat_ms() < ui.msg_until) {
        ui_header(ui.msg[0], ui.msg[1], 0, 0);
        return;
    }
    ui_fix1(mid, eng.clock.bpm);
    ui_cat(mid, eng.clock.source == JS_CLK_MIDI ? " EXT" : "");
    ui_cat(mid, eng.clock.running ? "  >" : "  #");
    ui_cat(right, "P");
    { char t[4] = {(char)('1' + eng.cur_pat), 0}; ui_cat(right, t); }
    if (eng.next_pat != eng.cur_pat || eng.scene_pending >= 0) ui_cat(right, "*");
    ui_cat(right, " ");
    ui_cat(right, eng.scene_cur >= 0 ? SCENE_NAME[eng.scene_cur] : "");
    ui_cat(right, " ");
    { char t[8]; ui_int(t, (int32_t)plat_cpu_pct()); ui_cat(right, t); ui_cat(right, "%"); }
    ui_header2(ui_held(&ui.in, B_SEL) ? "SEL" : ui_held(&ui.in, B_SAVE) ? "SAVE" : PAGE_NAME[ui.page], mid, right, alarm,
               pending_frac());
}

static void track_row(int32_t y, uint32_t t, int sel)
{
    const js_track_t *tr = &eng.pat[eng.cur_pat].t[t];
    uint8_t on[16];
    uint32_t i, base = ui.step_page * 16u;
    int play = -1;
    for (i = 0; i < 16u; i++) {
        const js_step_t *st = &tr->s[(base + i) % JS_MAXSTEPS];
        on[i] = base + i >= tr->len ? 0 : !(st->flags & JS_ON) ? 0 : st->lk_id[0] != JS_LOCK_NONE ? 3 : st->flags & JS_ACCENT ? 2 : 1;
    }
    if (eng.clock.running) {
        uint32_t p = (eng.seq.pos[t] + tr->len - 1u) % (tr->len ? tr->len : 1u);
        if (p >= base && p < base + 16u) play = (int)(p - base);
    }
    ui_steps(y, 8, on, play, sel && ui.held_step >= 0 ? ui.held_step : -1);
    if ((eng.seq.mute >> t) & 1u) cv_rect(0, y + 3, 240, 2, UI_RED);
    if (sel) cv_rect(0, y - 1, 1, 10, C_WHITE);
}

/* with SEL held: what the buttons do with it, over the bottom of the page */
static void sel_overlay(void)
{
    cv_rect(0, 82, 240, 33, C_BLACK);
    cv_rect(0, 82, 240, 1, C_HI);
    ui_fit(4, 86, 234 - (4), &FONT_T, "SEL + PLAY: PANIC    SEL + ENV: FILL 2 BARS", C_WHITE);
    ui_fit(4, 99, 234 - (4), &FONT_T, "SEL + LFO: UNDO      SEL + KNOB: FINE STEPS", C_WHITE);
}

static void draw_body(void)
{
    char t[40];
    uint32_t i;
    cv_begin(240, UI_BODY_H, C_BLACK);
    switch (ui.page) {
    case PG_PERFORM: {
        const char *perf = bs_perf_name[eng.perf];
        ui_fit(6, 2, eng.scene_pending >= 0 ? 128 : 228, &FONT_M, eng.scene_cur >= 0 ? SCENE_NAME[eng.scene_cur] : "-", C_HI);
        if (eng.scene_pending >= 0) { t[0] = 0; ui_cat(t, "> "); ui_cat(t, SCENE_NAME[eng.scene_pending]); ui_fit(138, 6, 98, &FONT_B, t, C_WHITE); }
        ui_fit(6, 30, 234 - (6), &FONT_S, "RHYTHM", C_GRAY);
        ui_fit(70, 30, 234 - (70), &FONT_B, eng.rhythm.node[eng.rv.near].name, C_AMB);
        ui_fit(6, 46, 234 - (6), &FONT_S, "KIT", C_GRAY);
        ui_fit(70, 46, 234 - (70), &FONT_B, eng.kit.node[eng.kv.near].name, C_AMB);
        ui_fit(6, 62, 234 - (6), &FONT_S, "MODE", C_GRAY);
        ui_fit(70, 62, 76, &FONT_B, eng.fill_on ? "FILL" : perf, eng.perf || eng.fill_on ? C_WHITE : C_AMB);
        t[0] = 0; ui_cat(t, PATTERN_NAME[eng.cur_pat]);
        ui_fit(150, 62, 234 - (150), &FONT_S, t, C_GRAY);
        for (i = 0; i < BS_NTRACKS; i++) { /* the mutes */
            int32_t x = 2 + (int32_t)i * 34;
            int m = (eng.seq.mute >> i) & 1u;
            cv_rect(x, 82, 33, 14, m ? C_LINE : C_DIM);
            if (m) cv_rect(x, 88, 33, 2, UI_RED);
            ui_fit(x + 2, 83, 30, &FONT_T, bs_track_name[i], m ? C_DIM : C_WHITE);
        }
        ui_fit(6, 100, 234 - (6), &FONT_XS, "OUT", C_GRAY);
        ui_meter(30, 105, 140, eng.peak, C_HI);
        eng.peak *= 0.6f;
        ui_fit(176, 100, 60, &FONT_XS, jl_alive(&eng.link, eng.ms) ? "LINK OK" : "SOLO", C_GRAY);
        break;
    }
    case PG_SEQ:
        for (i = 0; i < BS_NTRACKS; i++) track_row((int32_t)i * 13 + 4, i, i == ui.track);
        t[0] = 0;
        ui_cat(t, bs_track_name[ui.track]);
        ui_cat(t, ui.step_page ? "  STEPS 17-32" : "  STEPS 1-16");
        ui_fit(6, 96, 120, &FONT_XS, t, C_HI);
        if (ui.held_step >= 0) {
            const js_step_t *st = &eng.pat[eng.cur_pat].t[ui.track].s[(ui.step_page * 16 + ui.held_step) % JS_MAXSTEPS];
            char v[8];
            t[0] = 0;
            ui_cat(t, ui.track == BS_T_BREAK ? "SLICE " : "VEL ");
            ui_int(v, ui.track == BS_T_BREAK ? st->data + 1 : st->vel);
            ui_cat(t, v);
            if (st->flags & JS_REVERSE) ui_cat(t, " REV");
            if (st->flags & BS_FLAG_OPEN && ui.track == BS_T_HATS) ui_cat(t, " OPEN");
            ui_fit(130, 96, 234 - (130), &FONT_XS, t, C_WHITE);
        } else
            ui_fit(130, 96, 234 - (130), &FONT_XS, PATTERN_NAME[eng.cur_pat], C_GRAY);
        break;
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
                m = m * 22u / 32768u;
                cv_rect(6 + (int32_t)i, 89 - (int32_t)m, 1, (int32_t)(2u * m + 1u), C_DIM);
            }
            for (i = 0; i < b->nslices; i++) cv_rect(6 + (int32_t)(228.0f * (float)b->start[i] / (float)b->frames), 66, 1, 47, C_AMB);
        } else {
            static const uint8_t M[BS_NTRACKS] = {0, JD_KICK, JD_SNARE, JD_GHOST, JD_HATS, JD_PERC, JD_IMPACT};
            static const char *const PV[JD_PERC_NVAR] = {"TOM", "RIM", "SHAKER", "CLICK"};
            char v[8];
            ui_fit(6, 30, 128, &FONT_B, jd_model_name[M[ui.track]], C_AMB);
            if (ui.track == BS_T_PERC) ui_fit(140, 30, 234 - (140), &FONT_B, PV[eng.snd[BS_T_PERC].variant % JD_PERC_NVAR], C_WHITE);
            ui_fit(6, 52, 234 - (6), &FONT_S, "KIT NODE", C_GRAY);
            ui_fit(80, 52, 234 - (80), &FONT_S, eng.kit.node[eng.kv.near].name, C_AMB);
            t[0] = 0; ui_cat(t, "LEVEL "); ui_pct(v, eng.snd[ui.track].level); ui_cat(t, v);
            ui_fit(6, 70, 234 - (6), &FONT_S, t, C_GRAY);
            ui_fit(6, 92, 234 - (6), &FONT_XS, "HARDER HITS: BRIGHTER, MORE CRACK", C_DIM);
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
        break;
    }
    case PG_SCENE:
        for (i = 0; i < BS_NSCENES; i++) {
            int32_t x = 4 + (int32_t)(i % 4u) * 59, y = 4 + (int32_t)(i / 4u) * 48;
            int cur = (int)i == eng.scene_cur, pend = (int)i == eng.scene_pending;
            cv_rect(x, y, 55, 44, cur ? C_HI : pend ? C_DIM : C_LINE);
            ui_fit(x + 3, y + 4, 50, &FONT_XS, SCENE_NAME[i], cur ? C_BLACK : C_WHITE);
            ui_fit(x + 3, y + 26, 50, &FONT_XS, eng.scene[i].used ? "" : "EMPTY", C_GRAY);
        }
        break;
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
    if (ui_held(&ui.in, B_SEL)) sel_overlay();
    cv_blit(0, UI_BODY_Y);
}

/* the strip: the encoders and the keys on this page, or with SEL / SAVE held */
static void draw_strip(void)
{
    ui_kg_t g[8];
    const char *enc[3] = {0, 0, 0};
    uint32_t n = 0;
    int layer = ui_held(&ui.in, B_SAVE) ? 2 : ui_held(&ui.in, B_SEL) ? 1 : 0;
    int trackpage = ui.page == PG_SEQ || ui.page == PG_SOUND;
    if (layer == 2) {
        g[n++] = (ui_kg_t)UI_W(0, 7, "STORE THE SCENE HERE (1-8)");
    } else if (layer == 1) {
        g[n++] = (ui_kg_t)UI_W(0, 7, "QUEUE SCENE 1-8");
        if (ui.page == PG_PERFORM) g[n++] = (ui_kg_t)UI_W(8, 14, "SOLO");
        g[n++] = (ui_kg_t)UI_B(0, 6, "MUTE TRACK 1-7");
        enc[0] = trackpage ? "TEMPO" : 0;
        enc[2] = ui.page == PG_FX ? "TRIM" : 0;
    } else {
        switch (ui.page) {
        case PG_PERFORM:
            g[n++] = (ui_kg_t)UI_W(0, 6, "HOLD: PERFORM FX");
            g[n++] = (ui_kg_t)UI_W(7, 7, "FILL");
            g[n++] = (ui_kg_t)UI_W(8, 14, "MUTE TRACKS");
            g[n++] = (ui_kg_t)UI_W(15, 15, "MUTATE");
            g[n++] = (ui_kg_t)UI_B(0, 7, "SCENES 1-8");
            g[n++] = (ui_kg_t)UI_B(8, 9, "FILL 2/4");
            g[n++] = (ui_kg_t)UI_B(10, 10, "UNDO");
            enc[0] = "TEMPO"; enc[1] = "KIT NODE"; enc[2] = "SNAP";
            break;
        case PG_SEQ:
            g[n++] = (ui_kg_t)UI_W(0, 15, ui.held_step >= 0 ? "TURN K1-K4: VEL PROB RATCH MICRO" : "STEPS: TAP ON/OFF, HOLD + KNOB EDITS");
            g[n++] = (ui_kg_t)UI_B(0, 6, "TRACKS");
            g[n++] = (ui_kg_t)UI_B(7, 7, "GHOST");
            g[n++] = (ui_kg_t)UI_B(8, 9, "MUT/UNDO");
            if (ui.track == BS_T_HATS) g[n++] = (ui_kg_t)UI_B(10, 10, "OPEN");
            else if (ui.track == BS_T_BREAK) g[n++] = (ui_kg_t)UI_B(10, 10, "REV");
            enc[0] = "TRACK";
            enc[1] = ui.held_step >= 0 ? (ui.track == BS_T_BREAK ? "SLICE" : "TUNE") : 0;
            enc[2] = "PATTERN";
            break;
        case PG_SOUND:
            g[n++] = (ui_kg_t)UI_W(0, 15, ui.track == BS_T_BREAK ? "PLAY SLICES 1-16" : "PLAY IT: SOFT (LEFT) TO HARD (RIGHT)");
            g[n++] = (ui_kg_t)UI_B(0, 6, "TRACKS");
            enc[0] = "TRACK";
            enc[1] = ui.track == BS_T_PERC ? "VOICE" : 0;
            enc[2] = ui.track == BS_T_BREAK ? 0 : "LEVEL";
            break;
        case PG_FX: enc[0] = "TEMPO"; enc[1] = "PRESET"; enc[2] = "ROOM"; break;
        case PG_SCENE:
            g[n++] = (ui_kg_t)UI_W(0, 7, "QUEUE SCENE 1-8");
            enc[0] = "TEMPO";
            break;
        default: enc[0] = "TEMPO"; enc[2] = "USB HOST"; break;
        }
    }
    ui_strip(enc, g, n, layer);
}

static void draw_footer(void)
{
    const char *name[4] = {0, 0, 0, 0}, *vt[4] = {0, 0, 0, 0};
    float v[4] = {0, 0, 0, 0};
    char b[4][12];
    uint32_t k;
    js_pattern_t *pt = &eng.pat[eng.cur_pat];
    switch (ui.page) {
    case PG_PERFORM:
        name[0] = "BREAK"; name[1] = "KIT"; name[2] = "PRESSURE"; name[3] = "FILL AMT";
        v[0] = eng.rhythm_t; v[1] = eng.kit_t; v[2] = eng.bus.m.pressure; v[3] = eng.energy;
        break;
    case PG_SEQ:
        if (ui.held_step >= 0) {
            const js_step_t *st = &pt->t[ui.track].s[(ui.step_page * 16 + ui.held_step) % JS_MAXSTEPS];
            name[0] = "VEL"; name[1] = "PROB"; name[2] = "RATCH"; name[3] = "MICRO";
            v[0] = (float)st->vel / 127.0f; v[1] = (float)st->prob / 100.0f; v[2] = (float)st->ratch / 7.0f; v[3] = ((float)st->micro + 64.0f) / 127.0f;
            ui_int(b[2], st->ratch + 1); vt[2] = b[2];
            ui_int(b[3], st->micro); vt[3] = b[3];
        } else {
            name[0] = "LENGTH"; name[1] = "SWING"; name[2] = "GHOSTS"; name[3] = "MUTATE";
            v[0] = (float)pt->t[ui.track].len / 32.0f; v[1] = ((float)pt->swing - 50.0f) / 25.0f; v[2] = ui.ghost_density; v[3] = ui.mutate_amt;
            ui_int(b[0], pt->t[ui.track].len); vt[0] = b[0];
            ui_int(b[1], pt->swing); ui_cat(b[1], "%"); vt[1] = b[1];
        }
        break;
    case PG_SOUND:
        if (ui.track == BS_T_BREAK) {
            name[0] = "PITCH"; name[1] = "LEVEL"; name[2] = "SLICES"; name[3] = "GATE";
            v[0] = (eng.player.pitch + 12.0f) / 24.0f; v[1] = eng.break_level; v[2] = (float)eng.slices / 32.0f; v[3] = eng.player.gate;
            ui_fix1(b[0], eng.player.pitch); vt[0] = b[0];
            ui_int(b[2], eng.slices); vt[2] = b[2];
        } else {
            static const uint8_t M[BS_NTRACKS] = {0, JD_KICK, JD_SNARE, JD_GHOST, JD_HATS, JD_PERC, JD_IMPACT};
            for (k = 0; k < 4u; k++) { name[k] = jd_macro_name[M[ui.track]][k]; v[k] = eng.snd[ui.track].m[k]; }
        }
        break;
    case PG_FX:
        name[0] = "PUNCH"; name[1] = "GRIT"; name[2] = "GLUE"; name[3] = "PRESSURE";
        v[0] = eng.bus.m.punch; v[1] = eng.bus.m.crush; v[2] = eng.bus.m.glue; v[3] = eng.bus.m.pressure;
        break;
    case PG_SCENE:
        name[0] = "QUANT"; name[1] = "BRK LOCK"; name[2] = "ANCHORS"; name[3] = "LINK KICK";
        v[0] = (float)set.scene_quant / 4.0f; v[1] = (float)eng.breaklock / 2.0f; v[2] = eng.anchors; v[3] = eng.send_kicks;
        vt[0] = QUANT_NAME[set.scene_quant]; vt[1] = LOCK_NAME[eng.breaklock]; vt[2] = eng.anchors ? "ON" : "OFF"; vt[3] = eng.send_kicks ? "ON" : "OFF";
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
    ui_macros(name, v, vt, ui.hi_until && plat_ms() < ui.hi_until ? ui.hi_macro : -1);
}

static void lights(void)
{
    uint32_t b = 0, keys = 0, dimk = 0, k;
    static const uint8_t PB[NPAGES] = {B_HOME, B_SEQ, B_EDIT, B_FX, B_ARP, B_GLO};
    b |= 1u << PB[ui.page];
    if (eng.clock.running) b |= 1u << B_PLAY;
    if (dirty) b |= 1u << B_SAVE;
    if (eng.fill_on) b |= 1u << B_ENV;
    if (eng.return_pending) b |= 1u << B_REC;
    if (ui.page == PG_SEQ) {
        const js_track_t *tr = &eng.pat[eng.cur_pat].t[ui.track];
        for (k = 0; k < 16u; k++)
            if (tr->s[(ui.step_page * 16u + k) % JS_MAXSTEPS].flags & JS_ON) keys |= ui_white_bit((int)k);
        keys |= ui_black_bit(ui.track);
        if (eng.clock.running) dimk |= ui_white_bit((int)((eng.clock.abs_step) & 15u));
    } else if (ui.page == PG_PERFORM) {
        for (k = 0; k < BS_NTRACKS; k++) if (!((eng.seq.mute >> k) & 1u)) keys |= ui_white_bit(8 + (int)k);
        if (eng.scene_cur >= 0) keys |= ui_black_bit(eng.scene_cur);
        if (eng.scene_pending >= 0) dimk |= ui_black_bit(eng.scene_pending);
        if (eng.perf) keys |= ui_white_bit(eng.perf == BS_PERF_ROLL ? (eng.roll_div == 3 ? 1 : 0) : eng.perf);
    } else if (ui.page == PG_SCENE) {
        for (k = 0; k < BS_NSCENES; k++) if (eng.scene[k].used) dimk |= ui_white_bit((int)k);
        if (eng.scene_cur >= 0) keys |= ui_white_bit(eng.scene_cur);
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
