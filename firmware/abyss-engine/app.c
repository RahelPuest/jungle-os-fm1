/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* ABYSS ENGINE: the app (screens, controls, storage, MIDI and the link). The engine is engine/ab_engine.c
 * (-O2 unit dsp_o2.c). Part of fm1-base's unity build. The control map is firmware/abyss-engine/docs/MANUAL.md. */
#include "engine/ab_engine.h"
#include "../../shared/store/jo_store.h"
#include "../../shared/ui/jo_ui.c"

static ab_engine_t eng PLAT_POOL;
static int16_t fx_pool[AB_POOL_SAMPLES] PLAT_POOL; /* the delay's lines and the reverb: 96 KiB */

#define SET_MAGIC 0x31534241u /* "ABS1" */
typedef struct {
    float bpm, morph, motion, bite, space, duck_depth;
    jf_neuro_macros_t fx;
    jn_stab_patch_t stab;
    jn_pad_patch_t pad;
    float lfo_shape, tension;
    int8_t root, octave;
    uint8_t scale, chord, inversion, link_ch, follow_link, voice_mode;
    uint8_t duck_internal, scene_quant, sub_octave, link_usb_host;
} settings_t;
static settings_t set;
#define PAT_MAGIC 0x31544150u
#define SCN_MAGIC 0x314E4353u
static struct { jo_rec_hdr_t h; uint8_t p[sizeof(js_pattern_t) > sizeof(ab_scene_t) * AB_NSCENES ? sizeof(js_pattern_t)
                                                                                    : sizeof(ab_scene_t) * AB_NSCENES]; } io;
_Static_assert(sizeof io <= 3840u, "a store object holds 3,840 bytes");
static uint8_t dirty, load_report;

enum { PG_PERFORM, PG_SEQ, PG_SOUND, PG_FX, PG_SCENE, PG_SYSTEM, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"PERFORM", "SEQUENCE", "SOUND", "FX", "SCENE", "SYSTEM"};
static const char *const PART_NAME[AB_NPARTS] = {"BASS", "STAB", "PAD"};
static const char *const SCENE_NAME[AB_NSCENES] = {"INTRO", "BUILD", "DROP", "MUTATION", "VOID", "IMPACT", "SCENE 7", "SCENE 8"};
static const char *const QUANT_NAME[5] = {"NOW", "BEAT", "BAR", "2 BAR", "4 BAR"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const SHAPE_NAME[5] = {"SINE", "EXP", "PULSE", "ASYM", "STEP"};
static const char *const VMODE_NAME[3] = {"FULL", "NO PAD", "BASS ONLY"};

static struct {
    uint8_t page, part, track, redraw;
    int8_t held_step;
    uint32_t key_used;
    int8_t hi_macro;
    uint32_t hi_until;
    char msg[2][24];
    uint32_t msg_until;
    uint32_t mutate_seed;
    float mutate_radius;
    int8_t key_note[NKEYS]; /* the note each held key started (its release ends that one) */
    ui_in_t in;
    uint32_t splash_until;
} ui;

/* ---------------------------------------------------------------- audio --- */
/* a message's arrival on the 24 MHz clock: the device's stamp (fm1.toml midi_stamp). The host simulator stamps
 * with the wall clock while it runs far faster than real time, so there the simulated time stands in */
static uint32_t midi_t24(void)
{
#ifdef PLAT_HOST
    return eng.ms * 24000u + (eng.ms_rem * 24000u) / 44118u;
#else
    return plat_midi_in_t24();
#endif
}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) ab_link_packet(&eng, pkt, midi_t24());
    ab_render(&eng, lr, n, (float)master_q12 * (1.0f / 4096.0f));
}

void app_panic(void)
{
    js_clock_stop(&eng.clock);
    js_queue_clear(&eng.q);
    jn_bass_off(&eng.bass);
    jn_sub_off(&eng.sub);
    jn_stab_release(&eng.stab);
    jn_pad_release(&eng.pad);
}

/* -------------------------------------------------------------- storage --- */
static void settings_defaults(void)
{
    set.bpm = 174.0f;
    set.morph = 0.4f, set.motion = 0.5f, set.bite = 0.3f, set.space = 0.4f;
    set.duck_depth = 0.6f;
    set.fx.weight = 0.5f, set.fx.dirt = 0.3f, set.fx.width = 0.4f, set.fx.pressure = 0.4f, set.fx.trim_db = 0.0f;
    set.stab.model = JN_STAB_RAVE, set.stab.chord = 0.3f, set.stab.attack = 0.1f, set.stab.bite = 0.5f, set.stab.tail = 0.4f;
    set.pad.darkness = 0.5f, set.pad.evolve = 0.4f, set.pad.tension = 0.3f, set.pad.depth = 0.6f;
    set.pad.attack_s = 1.5f, set.pad.release_s = 3.0f;
    set.lfo_shape = 0.0f;
    set.tension = 0.2f;
    set.root = 38;
    set.octave = 0;
    set.scale = JH_MINOR;
    set.chord = JH_CH_MIN;
    set.inversion = 0;
    set.link_ch = 15;
    set.follow_link = 1;
    set.voice_mode = AB_VOICE_FULL;
    set.duck_internal = 0;
    set.scene_quant = JL_Q_BAR;
    set.sub_octave = 1; /* 1: an octave below (-1), 0: unison */
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
    js_clock_set_bpm(&eng.clock, set.bpm);
    eng.morph = eng.morph_s = set.morph, eng.motion = set.motion, eng.bite = set.bite, eng.space = set.space;
    eng.duck_depth = set.duck_depth;
    eng.master.m = set.fx;
    jf_neuro_update(&eng.master);
    eng.stab_p = set.stab;
    eng.pad_p = set.pad;
    eng.patch.lfo_shape = set.lfo_shape;
    eng.tension = set.tension;
    eng.root = set.root;
    eng.scale = set.scale;
    eng.chord = set.chord;
    eng.inversion = set.inversion;
    jl_init(&eng.link, set.link_ch);
    eng.follow_link = set.follow_link;
    eng.voice_mode = set.voice_mode;
    eng.duck_internal = set.duck_internal;
    eng.sub.octave = set.sub_octave ? -1 : 0;
}

static void engine_to_settings(void)
{
    set.bpm = eng.clock.bpm;
    set.morph = eng.morph, set.motion = eng.motion, set.bite = eng.bite, set.space = eng.space;
    set.duck_depth = eng.duck_depth;
    set.fx = eng.master.m;
    set.stab = eng.stab_p;
    set.pad = eng.pad_p;
    set.lfo_shape = eng.patch.lfo_shape;
    set.tension = eng.tension;
    set.root = eng.root, set.scale = eng.scale, set.chord = eng.chord, set.inversion = eng.inversion;
    set.follow_link = eng.follow_link, set.voice_mode = eng.voice_mode, set.duck_internal = eng.duck_internal;
}

static void load_all(void)
{
    uint32_t p;
    load_report = 0;
    if (load_rec(OBJ_SETTINGS, SET_MAGIC, &set, sizeof set) || !(set.bpm >= 40.0f && set.bpm <= 300.0f) ||
        set.scale >= JH_NSCALES || set.chord >= JH_NCHORDS || set.stab.model >= JN_STAB_NMODELS || set.voice_mode > 2u ||
        set.root < 24 || set.root > 60) {
        settings_defaults();
        load_report |= 1u;
    }
    for (p = 0; p < AB_NPATTERNS; p++) {
        js_pattern_t tmp;
        if (load_rec(OBJ_PAT0 + p, PAT_MAGIC, &tmp, sizeof tmp) == 0 && tmp.ntracks == AB_NTRACKS) {
            uint32_t t, ok = 1;
            for (t = 0; t < AB_NTRACKS; t++)
                if (tmp.t[t].len < 1u || tmp.t[t].len > JS_MAXSTEPS || tmp.t[t].div > 2u) ok = 0;
            if (ok) { eng.pat[p] = tmp; continue; }
        }
        load_report |= 2u;
    }
    if (load_rec(OBJ_SCENES, SCN_MAGIC, eng.scene, sizeof eng.scene)) load_report |= 4u;
    apply_settings();
}

static void save_all(void)
{
    uint32_t p;
    int rc = 0;
    if (eng.clock.running) { app_say("STOP FIRST", "SAVING STALLS SOUND"); return; }
    engine_to_settings();
    rc |= save_rec(OBJ_SETTINGS, SET_MAGIC, &set, sizeof set);
    for (p = 0; p < AB_NPATTERNS; p++) rc |= save_rec(OBJ_PAT0 + p, PAT_MAGIC, &eng.pat[p], sizeof eng.pat[p]);
    rc |= save_rec(OBJ_SCENES, SCN_MAGIC, eng.scene, sizeof eng.scene);
    if (rc) app_say("SAVE", "FAILED");
    else { dirty = 0; app_say("SAVED", "PATTERNS + SCENES"); }
}

void app_init(void)
{
    ab_init(&eng, fx_pool);
    load_all();
    ui.held_step = -1;
    ui.hi_macro = -1;
    ui.mutate_seed = 1;
    ui.mutate_radius = 0.35f;
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

/* ---------------------------------------------------------------- knobs --- */
static float *page_macro(int k)
{
    switch (ui.page) {
    case PG_PERFORM: return k == 0 ? &eng.morph : k == 1 ? &eng.motion : k == 2 ? &eng.bite : &eng.space;
    case PG_SOUND:
        if (ui.part == AB_PART_STAB) return k == 0 ? &eng.stab_p.chord : k == 1 ? &eng.stab_p.attack : k == 2 ? &eng.stab_p.bite : &eng.stab_p.tail;
        if (ui.part == AB_PART_PAD) return k == 0 ? &eng.pad_p.darkness : k == 1 ? &eng.pad_p.evolve : k == 2 ? &eng.pad_p.tension : &eng.pad_p.depth;
        return k == 3 ? &eng.tension : 0;
    case PG_FX: return k == 0 ? &eng.master.m.weight : k == 1 ? &eng.master.m.dirt : k == 2 ? &eng.master.m.width : &eng.master.m.pressure;
    default: return 0;
    }
}

static void turn_k(int k, int32_t d)
{
    float *v = page_macro(k), step = ui_held(&ui.in, B_SEL) ? 0.0025f : 0.01f;
    js_track_t *tr = &eng.pat[eng.cur_pat].t[ui.track];
    ui.hi_macro = (int8_t)k;
    ui.hi_until = plat_ms() + 1200u;
    ui.redraw = 1;
    dirty = 1;
    if (ui.page == PG_SEQ && ui.held_step >= 0) {
        js_step_t *st = &tr->s[ui.held_step % JS_MAXSTEPS];
        ui.key_used |= ui_white_bit(ui.held_step);
        st->flags |= JS_ON;
        if (k == 0) { /* the note, in scale degrees around the root (bass) or the chord degree (stab, pad) */
            if (ui.track == AB_T_BASS) {
                int note = (int)st->data - 64 + eng.root;
                note = jh_snap(eng.root, eng.scale, note + (d > 0 ? 1 : -1) * (eng.scale == JH_CHROMATIC ? 1 : 1));
                if (note == (int)st->data - 64 + eng.root) note += d > 0 ? 2 : -2;
                note = jh_snap(eng.root, eng.scale, note);
                st->data = (uint8_t)jo_clampi(note - eng.root + 64, 40, 100);
            } else st->data = (uint8_t)jo_clampi((int)st->data + (d > 0 ? 1 : -1), 0, 13);
        } else if (k == 1) st->vel = (uint8_t)jo_clampi(st->vel + d * 2, 1, 127);
        else {
            uint8_t id = k == 2 ? AB_LK_MORPH : AB_LK_BITE;
            int cur = js_step_lock(st, id);
            if (cur < 0) cur = (int)((k == 2 ? eng.morph : eng.bite) * 255.0f);
            if (js_step_set_lock(st, id, jo_clampi(cur + d * 4, 0, 255))) app_say("LOCKS", "TWO PER STEP");
        }
        return;
    }
    switch (ui.page) {
    case PG_SEQ:
        if (k == 0) tr->len = (uint8_t)jo_clampi(tr->len + (d > 0 ? 1 : -1), 1, JS_MAXSTEPS);
        else if (k == 1) eng.pat[eng.cur_pat].swing = (uint8_t)jo_clampi(eng.pat[eng.cur_pat].swing + d, 50, 75);
        else if (k == 2) ui.mutate_radius = jo_clampf(ui.mutate_radius + 0.02f * (float)d, 0.0f, 1.0f);
        else tr->div = (uint8_t)jo_clampi(tr->div + (d > 0 ? 1 : -1), 0, 2);
        return;
    case PG_SOUND:
        if (ui.part == AB_PART_BASS) {
            if (k == 0) eng.patch.lfo_shape = (float)jo_clampi((int32_t)eng.patch.lfo_shape + (d > 0 ? 1 : -1), 0, 4);
            else if (k == 1) eng.sub.sat = jo_clampf(eng.sub.sat + 0.02f * (float)d, 0.0f, 1.0f);
            else if (k == 2) { set.sub_octave = d < 0; eng.sub.octave = set.sub_octave ? -1 : 0; }
        }
        break;
    case PG_SCENE:
        if (k == 0) set.scene_quant = (uint8_t)jo_clampi(set.scene_quant + (d > 0 ? 1 : -1), 0, 4);
        else if (k == 1) eng.duck_depth = jo_clampf(eng.duck_depth + 0.02f * (float)d, 0.0f, 1.0f);
        else if (k == 2) eng.duck_internal = d > 0;
        else eng.follow_link = d > 0;
        return;
    case PG_SYSTEM:
        if (k == 0) ab_cmd_bpm(&eng, jo_clampf(eng.clock.bpm + 0.5f * (float)d, 60.0f, 220.0f));
        else if (k == 1) eng.voice_mode = (uint8_t)jo_clampi(eng.voice_mode + (d > 0 ? 1 : -1), 0, 2);
        else if (k == 2) { set.link_ch = (uint8_t)jo_clampi(set.link_ch + (d > 0 ? 1 : -1), 0, 15); jl_init(&eng.link, set.link_ch); }
        else eng.follow_link = d > 0;
        return;
    default: break;
    }
    if (v) {
        *v = jo_clampf(*v + step * (float)d, 0.0f, 1.0f);
        if (ui.page == PG_FX) jf_neuro_update(&eng.master);
        if (ui.page == PG_PERFORM && k == 0) eng.explore_node = -1, eng.mutated = 0;
    }
}

/* --------------------------------------------------------------- input --- */
static int key_note(uint32_t k)
{
    int base = ui.part == AB_PART_BASS ? 29 : ui.part == AB_PART_STAB ? 53 : 41; /* F1, F3, F2 at the lowest key */
    return base + (int)k + 12 * set.octave;
}

static void play_key(uint32_t k, int down)
{
    if (down) {
        uint32_t v = touch_vel(k);
        int note = key_note(k);
        ui.key_note[k] = (int8_t)note;
        ab_cmd_note(&eng, ui.part, note, (float)(v ? v : 100u) * (1.0f / 127.0f), 1);
    } else
        ab_cmd_note(&eng, ui.part, ui.key_note[k], 0.0f, 0);
}

static void input(void)
{
    ui_in_t *in = &ui.in;
    uint32_t k;
    int32_t d;
    ui_poll(in);
    if (ui_tapped(in, B_HOME)) ui.page = PG_PERFORM, ui.redraw = 1;
    if (ui_tapped(in, B_SEQ)) ui.page = PG_SEQ, ui.redraw = 1;
    if (ui_tapped(in, B_EDIT)) ui.page = PG_SOUND, ui.redraw = 1;
    if (ui_tapped(in, B_FX)) ui.page = PG_FX, ui.redraw = 1;
    if (ui_tapped(in, B_ARP)) ui.page = PG_SCENE, ui.redraw = 1;
    if (ui_tapped(in, B_GLO)) ui.page = PG_SYSTEM, ui.redraw = 1;
    if (ui_pressed(in, B_PLAY)) {
        if (ui_held(in, B_SEL)) { ui_use(in, B_SEL); ab_cmd_panic(&eng); ab_cmd_play(&eng, 0); app_say("PANIC", "ALL NOTES OFF"); }
        else ab_cmd_play(&eng, !eng.clock.running);
    }
    /* (0.2) the buttons as on Break Surgeon: REC RETURN, ENV FILL, LFO MUTATE (SEL + LFO: UNDO); PAD HOLD on SEL + REC */
    if (ui_tapped(in, B_ENV)) {
        int two = ui_held(in, B_SEL);
        if (two) ui_use(in, B_SEL);
        ab_cmd_fill(&eng, two ? 2 : 1);
        app_say(two ? "FILL 2 BARS" : "FILL 1 BAR", "BASS ROLLS");
    }
    if (ui_tapped(in, B_REC)) {
        if (ui_held(in, B_SEL)) { ui_use(in, B_SEL); ab_cmd_pad(&eng, !eng.pad_on); app_say("PAD", eng.pad_on ? "RELEASE" : "HOLD"); }
        else { ab_cmd_return(&eng); app_say("RETURN", "SNAP, MUTATION OFF"); }
    }
    if (ui_tapped(in, B_LFO)) {
        if (ui_held(in, B_SEL)) { ui_use(in, B_SEL); app_say("UNDO", ab_revert(&eng) ? "NOTHING TO UNDO" : "LAST MUTATION UNDONE"); }
        else { ab_mutate(&eng, ui.mutate_radius, ui.mutate_seed++); app_say("MUTATE", "SEL+LFO: UNDO"); }
    }
    if (ui_tapped(in, B_SAVE)) save_all();
    if (ui_pressed(in, B_SEL) || ui_pressed(in, B_SAVE) || (in->btn_up & (1u << B_SEL | 1u << B_SAVE))) ui.redraw = 1;
    if (ui_pressed(in, B_OCTDN)) set.octave = (int8_t)jo_clampi(set.octave - 1, -2, 2), ui.redraw = 1;
    if (ui_pressed(in, B_OCTUP)) set.octave = (int8_t)jo_clampi(set.octave + 1, -2, 2), ui.redraw = 1;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        int w = ui_white(k), b = ui_black(k);
        int down = (in->key_down & bit) != 0, up = (in->key_up & bit) != 0;
        if (!down && !up) continue;
        ui.redraw = 1;
        /* on every page, as on Break Surgeon: SEL + white 1-8 queues a scene, SAVE + white 1-8 stores one,
         * SEL + black 1-3 mutes BASS, STAB, PAD */
        if (down && w >= 0 && w < AB_NSCENES && ui_held(in, B_SAVE)) {
            ui_use(in, B_SAVE);
            ab_scene_store(&eng, w);
            dirty = 1;
            app_say("SCENE STORED", SCENE_NAME[w]);
            ui.key_used |= bit;
            continue;
        }
        if (down && b >= 0 && b < AB_NTRACKS && ui_held(in, B_SEL)) {
            ui_use(in, B_SEL);
            ab_cmd_mute(&eng, b);
            app_say((eng.seq.mute >> b) & 1u ? "UNMUTE" : "MUTE", ab_track_name[b]);
            ui.key_used |= bit;
            continue;
        }
        if (up && (ui.key_used & bit) && ui.page != PG_SEQ) { ui.key_used &= ~bit; continue; }
        if (ui.page == PG_SEQ && !(ui_held(in, B_SEL) && down && w >= 0 && w < AB_NSCENES)) {
            if (w >= 0) {
                if (down) { ui.held_step = (int8_t)w; ui.key_used &= ~bit; }
                else {
                    if (!(ui.key_used & bit)) { eng.pat[eng.cur_pat].t[ui.track].s[w].flags ^= JS_ON; dirty = 1; }
                    if (ui.held_step == w) ui.held_step = -1;
                }
            } else if (down) {
                if (b < AB_NTRACKS) ui.track = (uint8_t)b;
                else if (ui.held_step >= 0 && (b == 3 || b == 4)) {
                    js_step_t *st = &eng.pat[eng.cur_pat].t[ui.track].s[ui.held_step];
                    st->flags ^= b == 3 ? JS_ACCENT : JS_SLIDE;
                    st->flags |= JS_ON;
                    ui.key_used |= ui_white_bit(ui.held_step);
                    dirty = 1;
                } else if (b == 10) ab_cmd_mute(&eng, ui.track);
            }
        } else if (ui.page == PG_SCENE) {
            if (down && w >= 0 && w < AB_NSCENES) {
                if (ui_held(in, B_SAVE)) { ui_use(in, B_SAVE); ab_scene_store(&eng, w); dirty = 1; app_say("SCENE STORED", SCENE_NAME[w]); }
                else { ab_cmd_scene(&eng, w, set.scene_quant); app_say(SCENE_NAME[w], QUANT_NAME[set.scene_quant]); }
            }
        } else if (ui_held(in, B_SEL) && down && w >= 0 && w < AB_NSCENES) { /* SEL + a white key: a scene, anywhere */
            ui_use(in, B_SEL);
            ab_cmd_scene(&eng, w, set.scene_quant);
            app_say(SCENE_NAME[w], QUANT_NAME[set.scene_quant]);
            ui.key_used |= bit;
        } else
            play_key(k, down);
    }
    if ((d = plat_enc(EN_SELECT)) != 0) {
        /* SELECT: the track (SEQUENCE) or the part the keys play (PERFORM, SOUND); SEL + SELECT and elsewhere: tempo */
        int sel = ui_held(in, B_SEL);
        if (sel) ui_use(in, B_SEL);
        if (ui.page == PG_SEQ && !sel) ui.track = (uint8_t)jo_clampi(ui.track + (d > 0 ? 1 : -1), 0, AB_NTRACKS - 1);
        else if ((ui.page == PG_PERFORM || ui.page == PG_SOUND) && !sel) {
            ui.part = (uint8_t)jo_clampi(ui.part + (d > 0 ? 1 : -1), 0, AB_NPARTS - 1);
            app_say("KEYS PLAY", PART_NAME[ui.part]);
        } else ab_cmd_bpm(&eng, jo_clampf(eng.clock.bpm + 0.5f * (float)d, 60.0f, 220.0f));
        dirty = 1;
        ui.redraw = 1;
    }
    if ((d = plat_enc(EN_ALGO)) != 0) {
        if (ui.page == PG_PERFORM) { ab_explore(&eng, d > 0 ? 1 : -1); app_say("EXPLORE", ab_node_name(&eng)); }
        else if (ui.page == PG_SOUND && ui.part == AB_PART_STAB) eng.stab_p.model = (uint8_t)((eng.stab_p.model + (d > 0 ? 1u : JN_STAB_NMODELS - 1u)) % JN_STAB_NMODELS);
        else if (ui.page == PG_SOUND) eng.chord = (uint8_t)jo_clampi(eng.chord + (d > 0 ? 1 : -1), 0, JH_NCHORDS - 1);
        else if (ui.page == PG_SEQ && ui.held_step >= 0) {
            js_step_t *st = &eng.pat[eng.cur_pat].t[ui.track].s[ui.held_step];
            int cur = js_step_lock(st, AB_LK_OCT);
            ui.key_used |= ui_white_bit(ui.held_step);
            js_step_set_lock(st, AB_LK_OCT, jo_clampi((cur < 0 ? 2 : cur) + (d > 0 ? 1 : -1), 0, 4));
        }
        dirty = 1;
        ui.redraw = 1;
    }
    if ((d = plat_enc(EN_PRESET)) != 0) {
        if (ui.page == PG_SYSTEM) {
            set.link_usb_host = d > 0;
            plat_usb_host(set.link_usb_host);
            app_say("USB", set.link_usb_host ? "HOST: LOOKING" : "DEVICE");
        } else if (ui.page == PG_PERFORM) { int n = ab_snap(&eng); app_say("SNAP", eng.fm.node[n].name); }
        else if (ui.page == PG_SOUND && ui_held(in, B_SEL)) {
            ui_use(in, B_SEL);
            eng.scale = (uint8_t)jo_clampi(eng.scale + (d > 0 ? 1 : -1), 0, JH_NSCALES - 1);
            app_say("SCALE", jh_scale_name[eng.scale]);
        } else if (ui.page == PG_SOUND) eng.root = (int8_t)jo_clampi(eng.root + (d > 0 ? 1 : -1), 28, 52);
        else if (ui.page == PG_FX) { eng.master.m.trim_db = jo_clampf(eng.master.m.trim_db + 0.5f * (float)d, -12.0f, 6.0f); jf_neuro_update(&eng.master); }
        else if (ui.page == PG_SEQ) {
            static const char *const PN[4] = {"P1", "P2", "P3", "P4"};
            int p = (eng.next_pat + AB_NPATTERNS + (d > 0 ? 1 : -1)) % AB_NPATTERNS;
            ab_cmd_pattern(&eng, p);
            app_say(PN[p % 4], eng.clock.running ? "AT THE NEXT BAR" : "");
        }
        dirty = 1;
        ui.redraw = 1;
    }
    for (k = 0; k < 4u; k++)
        if ((d = plat_enc(EN_K1 + (int)k)) != 0) turn_k((int)k, d);
}

/* -------------------------------------------------------------- screens --- */
/* how far a queued pattern or scene is from landing (0..1), or -1 */
static float pending_frac(void)
{
    uint32_t now = eng.clock.abs_step, rem, span;
    if (!eng.clock.running || !(eng.next_pat != eng.cur_pat || eng.scene_pending >= 0)) return -1.0f;
    if (!js_before(now, eng.pending_at)) return -1.0f;
    rem = eng.pending_at - now;
    span = rem > 32u ? 64u : rem > 16u ? 32u : 16u;
    return 1.0f - (float)rem / (float)span;
}

static void draw_header(void)
{
    char mid[24] = "", right[28] = "", t[8];
    if (ui.msg_until && plat_ms() < ui.msg_until) { ui_header(ui.msg[0], ui.msg[1], 0, 0); return; }
    ui_fix1(mid, eng.clock.bpm);
    ui_cat(mid, eng.clock.source == JS_CLK_MIDI ? " LINK" : "");
    ui_cat(mid, eng.clock.running ? " >" : " #");
    ui_cat(right, NOTE_NAME[(eng.root % 12 + 12) % 12]);
    ui_cat(right, " ");
    ui_cat(right, jh_scale_name[eng.scale]);
    ui_cat(right, " ");
    ui_int(t, (int32_t)plat_cpu_pct());
    ui_cat(right, t);
    ui_cat(right, "%");
    ui_header2(ui_held(&ui.in, B_SEL) ? "SEL" : ui_held(&ui.in, B_SAVE) ? "SAVE" : PAGE_NAME[ui.page], mid, right,
               plat_cpu_pct() > 85u || plat_xruns(), pending_frac());
}

static void draw_body(void)
{
    char t[40], v[12];
    uint32_t i;
    cv_begin(240, UI_BODY_H, C_BLACK);
    switch (ui.page) {
    case PG_PERFORM: {
        const char *nn = eng.explore_node >= 0 ? eng.fm.node[eng.explore_node].name : ab_node_name(&eng);
        ui_fit(6, 2, 234 - (6), &FONT_M, nn, C_HI);
        t[0] = 0;
        if (eng.mutated) ui_cat(t, "MUTATED (SEL+LFO: UNDO)  ");
        if (eng.explore_node >= 0) ui_cat(t, "EXPLORING");
        if (eng.fill_on) ui_cat(t, "  FILL");
        ui_fit(6, 28, 234 - (6), &FONT_XS, t, C_GRAY);
        /* the path, nodes as ticks, the morph as a marker */
        for (i = 0; i < eng.fm.npath; i++) {
            int32_t x = 10 + (int32_t)(220.0f * jg_path_t(&eng.fm, i));
            cv_rect(x - 1, 46, 3, 10, eng.fm.path[i] == eng.fv.near ? C_WHITE : C_DIM);
        }
        cv_rect(10, 50, 220, 2, C_LINE);
        cv_rect(10 + (int32_t)(220.0f * eng.morph_s) - 2, 42, 5, 18, C_HI);
        t[0] = 0; ui_cat(t, "KEYS PLAY "); ui_cat(t, PART_NAME[ui.part]); ui_cat(t, "  OCT ");
        ui_int(v, set.octave); ui_cat(t, v);
        ui_fit(6, 64, 234 - (6), &FONT_B, t, C_AMB);
        t[0] = 0; ui_cat(t, eng.pad_on ? "PAD HOLD  " : "PAD OFF  "); ui_cat(t, eng.scene_cur >= 0 ? SCENE_NAME[eng.scene_cur] : "");
        if (eng.scene_pending >= 0) { ui_cat(t, " > "); ui_cat(t, SCENE_NAME[eng.scene_pending]); }
        ui_fit(6, 82, 234 - (6), &FONT_S, t, C_GRAY);
        ui_fit(6, 99, 30, &FONT_XS, "DUCK", C_GRAY);
        ui_meter(36, 104, 60, 1.0f - eng.b_duck[0], C_AMB);
        ui_fit(104, 99, 22, &FONT_XS, "OUT", C_GRAY);
        ui_meter(126, 104, 70, eng.peak, C_HI);
        eng.peak *= 0.6f;
        ui_fit(202, 99, 234 - (202), &FONT_XS, jl_alive(&eng.link, eng.ms) ? "LINK" : "SOLO", jl_alive(&eng.link, eng.ms) ? C_HI : C_GRAY);
        break;
    }
    case PG_SEQ:
        for (i = 0; i < AB_NTRACKS; i++) {
            const js_track_t *tr = &eng.pat[eng.cur_pat].t[i];
            uint8_t on[16];
            uint32_t s;
            int play = -1;
            for (s = 0; s < 16u; s++) {
                const js_step_t *st = &tr->s[s];
                on[s] = s >= tr->len ? 0 : !(st->flags & JS_ON) ? 0 : st->flags & JS_SLIDE ? 3 : st->flags & JS_ACCENT ? 2 : 1;
            }
            if (eng.clock.running) play = (int)((eng.seq.pos[i] + tr->len - 1u) % (tr->len ? tr->len : 1u));
            ui_fit(2, 0 + (int32_t)i * 30, 56, &FONT_XS, ab_track_name[i], i == ui.track ? C_WHITE : (eng.seq.mute >> i) & 1u ? C_DIM : C_GRAY);
            if ((eng.seq.mute >> i) & 1u) ui_fit(60, (int32_t)i * 30, 234 - (60), &FONT_XS, "MUTED", UI_RED);
            ui_steps(16 + (int32_t)i * 30, 9, on, play < 16 ? play : -1, i == ui.track ? ui.held_step : -1);
        }
        if (ui.held_step >= 0) {
            const js_step_t *st = &eng.pat[eng.cur_pat].t[ui.track].s[ui.held_step];
            int note = (int)st->data - 64 + eng.root;
            t[0] = 0;
            if (ui.track == AB_T_BASS) { ui_cat(t, NOTE_NAME[(note % 12 + 12) % 12]); ui_int(v, note / 12 - 1); ui_cat(t, v); }
            else { ui_cat(t, "DEGREE "); ui_int(v, st->data + 1); ui_cat(t, v); }
            if (st->flags & JS_SLIDE) ui_cat(t, " SLIDE");
            if (st->flags & JS_ACCENT) ui_cat(t, " ACC");
            ui_fit(6, 96, 234 - (6), &FONT_B, t, C_WHITE);
        } else {
            t[0] = 0; ui_cat(t, "PATTERN "); ui_int(v, eng.cur_pat + 1); ui_cat(t, v);
            if (eng.next_pat != eng.cur_pat) { ui_cat(t, " > "); ui_int(v, eng.next_pat + 1); ui_cat(t, v); }
            ui_fit(6, 96, 234 - (6), &FONT_S, t, C_GRAY);
        }
        break;
    case PG_SOUND:
        ui_fit(6, 2, 80, &FONT_M, PART_NAME[ui.part], C_HI);
        if (ui.part == AB_PART_STAB) ui_fit(90, 8, 234 - (90), &FONT_B, jn_stab_name[eng.stab_p.model], C_AMB);
        else if (ui.part == AB_PART_BASS) ui_fit(90, 8, 234 - (90), &FONT_B, eng.fv.phys[JN_P_MODEL] > 0.5f ? "REESE MATRIX" : "FM CREATURE", C_AMB);
        t[0] = 0; ui_cat(t, "KEY "); ui_cat(t, NOTE_NAME[(eng.root % 12 + 12) % 12]); ui_cat(t, " "); ui_cat(t, jh_scale_name[eng.scale]);
        ui_cat(t, "  CHORD "); ui_cat(t, jh_chord_name[eng.chord]);
        ui_fit(6, 30, 234 - (6), &FONT_S, t, C_GRAY);
        if (ui.part == AB_PART_BASS)
            for (i = 0; i < JN_NPARAMS; i++) { /* the patch as the graph gives it now */
                int32_t x = 6 + (int32_t)(i % 3u) * 78, y = 50 + (int32_t)(i / 3u) * 15;
                ui_fit(x, y, 40, &FONT_XS, jn_param_name[i], C_DIM);
                ui_meter(x + 42, y + 5, 30, eng.fv.norm[i], C_AMB);
            }
        break;
    case PG_FX:
        ui_fit(6, 2, 234 - (6), &FONT_M, "NEURO PRESSURE", C_HI);
        ui_fit(6, 40, 234 - (6), &FONT_S, "PEAK", C_GRAY);
        ui_meter(70, 44, 160, eng.master.peak, C_HI);
        eng.master.peak *= 0.6f;
        ui_fit(6, 58, 234 - (6), &FONT_S, "GLUE GR", C_GRAY);
        ui_meter(70, 62, 160, -eng.master.comp.gr_db / 12.0f, C_AMB);
        t[0] = 0; ui_cat(t, "GUARD HITS "); ui_int(v, (int32_t)eng.master.guard.hits); ui_cat(t, v);
        ui_cat(t, "  TRIM "); ui_fix1(v, eng.master.m.trim_db); ui_cat(t, v);
        ui_fit(6, 80, 234 - (6), &FONT_S, t, C_GRAY);
        ui_fit(6, 100, 234 - (6), &FONT_XS, "THE SUB STAYS MONO AND CLEAN", C_DIM);
        break;
    case PG_SCENE:
        for (i = 0; i < AB_NSCENES; i++) {
            int32_t x = 4 + (int32_t)(i % 4u) * 59, y = 4 + (int32_t)(i / 4u) * 48;
            int cur = (int)i == eng.scene_cur, pend = (int)i == eng.scene_pending;
            cv_rect(x, y, 55, 44, cur ? C_HI : pend ? C_DIM : C_LINE);
            ui_fit(x + 3, y + 4, 50, &FONT_XS, SCENE_NAME[i], cur ? C_BLACK : C_WHITE);
            ui_fit(x + 3, y + 26, 50, &FONT_XS, eng.scene[i].used ? "" : "EMPTY", C_GRAY);
        }
        break;
    default:
        t[0] = 0; ui_cat(t, "CPU "); ui_int(v, (int32_t)plat_cpu_pct()); ui_cat(t, v); ui_cat(t, "%  XRUNS "); ui_int(v, (int32_t)plat_xruns()); ui_cat(t, v);
        ui_fit(6, 2, 234 - (6), &FONT_S, t, C_HI);
        t[0] = 0; ui_cat(t, "LINK CH "); ui_int(v, eng.link.channel + 1); ui_cat(t, v);
        ui_cat(t, jl_alive(&eng.link, eng.ms) ? "  PEER OK" : "  NO PEER");
        ui_cat(t, "  KICKS "); ui_int(v, (int32_t)eng.link_kicks); ui_cat(t, v);
        ui_fit(6, 18, 234 - (6), &FONT_S, t, C_GRAY);
        t[0] = 0; ui_cat(t, "STORE "); ui_cat(t, load_report ? "DEFAULTS USED" : "OK"); ui_cat(t, dirty ? "  UNSAVED" : "");
        ui_fit(6, 34, 234 - (6), &FONT_S, t, load_report ? C_WHITE : C_GRAY);
        t[0] = 0; ui_cat(t, "QUEUE DROPS "); ui_int(v, (int32_t)eng.q.dropped); ui_cat(t, v);
        ui_fit(6, 50, 234 - (6), &FONT_S, t, C_GRAY);
        {
            static const char *const UH[3] = {"DEVICE", "HOST: LOOKING", "HOST: CONNECTED"};
            int st = plat_usb_host_state();
            t[0] = 0; ui_cat(t, "USB "); ui_cat(t, UH[st >= 0 && st <= 2 ? st : 0]);
            ui_fit(6, 66, 234 - (6), &FONT_S, t, st == USBH_CONNECTED ? C_HI : C_GRAY);
        }
        { /* the sweetspots' state (docs/development/sweetspots.md): none is certified by listening yet */
            uint32_t un = 0;
            for (i = 0; i < eng.fm.nnodes; i++) un += eng.fm.node[i].review == JG_UNREVIEWED;
            t[0] = 0; ui_cat(t, "SWEETSPOTS "); ui_int(v, (int32_t)un); ui_cat(t, v); ui_cat(t, " UNREVIEWED");
            ui_fit(6, 82, 234 - (6), &FONT_S, t, C_GRAY);
        }
        break;
    }
    if (ui_held(&ui.in, B_SEL)) {
        cv_rect(0, 82, 240, 33, C_BLACK);
        cv_rect(0, 82, 240, 1, C_HI);
        ui_fit(4, 86, 234 - (4), &FONT_T, "SEL + PLAY: PANIC    SEL + ENV: FILL 2 BARS", C_WHITE);
        ui_fit(4, 99, 234 - (4), &FONT_T, "SEL + LFO: UNDO   SEL + REC: PAD HOLD", C_WHITE);
    }
    cv_blit(0, UI_BODY_Y);
}

/* the strip: the encoders and the keys on this page, or with SEL / SAVE held */
static void draw_strip(void)
{
    ui_kg_t g[6];
    const char *enc[3] = {0, 0, 0};
    uint32_t n = 0;
    int layer = ui_held(&ui.in, B_SAVE) ? 2 : ui_held(&ui.in, B_SEL) ? 1 : 0;
    static const char *const PLAYS[AB_NPARTS] = {"PLAY THE BASS (AND ITS SUB)", "PLAY STAB CHORDS", "PLAY PAD CHORDS"};
    if (layer == 2) {
        g[n++] = (ui_kg_t)UI_W(0, 7, "STORE THE SCENE HERE (1-8)");
    } else if (layer == 1) {
        g[n++] = (ui_kg_t)UI_W(0, 7, "QUEUE SCENE 1-8");
        g[n++] = (ui_kg_t)UI_B(0, 2, "MUTE B S P");
        enc[0] = ui.page == PG_PERFORM || ui.page == PG_SOUND || ui.page == PG_SEQ ? "TEMPO" : 0;
        enc[2] = ui.page == PG_SOUND ? "SCALE" : 0;
    } else {
        switch (ui.page) {
        case PG_SEQ:
            g[n++] = (ui_kg_t)UI_W(0, 15, ui.held_step >= 0 ? "TURN K1-K4: NOTE VEL MORPH BITE" : "STEPS: TAP ON/OFF, HOLD + KNOB EDITS");
            g[n++] = (ui_kg_t)UI_B(0, 2, "TRACKS");
            if (ui.held_step >= 0) g[n++] = (ui_kg_t)UI_B(3, 4, "ACC/SLIDE");
            g[n++] = (ui_kg_t)UI_B(10, 10, "MUTE");
            enc[0] = "TRACK"; enc[1] = ui.held_step >= 0 ? "OCTAVE" : 0; enc[2] = "PATTERN";
            break;
        case PG_SCENE:
            g[n++] = (ui_kg_t)UI_W(0, 7, "QUEUE SCENE 1-8");
            enc[0] = "TEMPO";
            break;
        default:
            g[n++] = (ui_kg_t)UI_W(0, 15, PLAYS[ui.part % AB_NPARTS]);
            g[n++] = (ui_kg_t)UI_B(0, 10, "(BLACK KEYS PLAY TOO)");
            if (ui.page == PG_PERFORM) { enc[0] = "PART"; enc[1] = "EXPLORE"; enc[2] = "SNAP"; }
            else if (ui.page == PG_SOUND) { enc[0] = "PART"; enc[1] = ui.part == AB_PART_STAB ? "MODEL" : "CHORD"; enc[2] = "ROOT"; }
            else if (ui.page == PG_FX) { enc[0] = "TEMPO"; enc[2] = "TRIM"; }
            else { enc[0] = "TEMPO"; enc[2] = "USB HOST"; }
            break;
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
    switch (ui.page) {
    case PG_PERFORM:
        name[0] = "MORPH"; name[1] = "MOTION"; name[2] = "BITE"; name[3] = "SPACE";
        v[0] = eng.morph; v[1] = eng.motion; v[2] = eng.bite; v[3] = eng.space;
        break;
    case PG_SEQ:
        if (ui.held_step >= 0) {
            const js_step_t *st = &eng.pat[eng.cur_pat].t[ui.track].s[ui.held_step];
            int m = js_step_lock(st, AB_LK_MORPH), bi = js_step_lock(st, AB_LK_BITE);
            name[0] = "NOTE"; name[1] = "VEL"; name[2] = "MORPH LK"; name[3] = "BITE LK";
            v[0] = (float)st->data / 127.0f; v[1] = (float)st->vel / 127.0f; v[2] = m < 0 ? 0.0f : (float)m / 255.0f; v[3] = bi < 0 ? 0.0f : (float)bi / 255.0f;
            vt[2] = m < 0 ? "-" : 0; vt[3] = bi < 0 ? "-" : 0;
            ui_int(b[0], (int32_t)st->data - 64); vt[0] = b[0];
        } else {
            static const char *const DIV[3] = {"1/16", "1/32", "1/8"};
            const js_track_t *tr = &eng.pat[eng.cur_pat].t[ui.track];
            name[0] = "LENGTH"; name[1] = "SWING"; name[2] = "MUT RAD"; name[3] = "SPEED";
            v[0] = (float)tr->len / 32.0f; v[1] = ((float)eng.pat[eng.cur_pat].swing - 50.0f) / 25.0f; v[2] = ui.mutate_radius; v[3] = (float)tr->div / 2.0f;
            ui_int(b[0], tr->len); vt[0] = b[0];
            ui_int(b[1], eng.pat[eng.cur_pat].swing); ui_cat(b[1], "%"); vt[1] = b[1];
            vt[3] = DIV[tr->div % 3u];
        }
        break;
    case PG_SOUND:
        if (ui.part == AB_PART_BASS) {
            name[0] = "MOD SHAPE"; name[1] = "SUB SAT"; name[2] = "SUB OCT"; name[3] = "TENSION";
            v[0] = eng.patch.lfo_shape / 4.0f; v[1] = eng.sub.sat; v[2] = eng.sub.octave < 0 ? 0.0f : 1.0f; v[3] = eng.tension;
            vt[0] = SHAPE_NAME[(int)eng.patch.lfo_shape % 5]; vt[2] = eng.sub.octave < 0 ? "-1" : "0";
        } else if (ui.part == AB_PART_STAB) {
            name[0] = "CHORD"; name[1] = "ATTACK"; name[2] = "BITE"; name[3] = "TAIL";
            v[0] = eng.stab_p.chord; v[1] = eng.stab_p.attack; v[2] = eng.stab_p.bite; v[3] = eng.stab_p.tail;
        } else {
            name[0] = "DARKNESS"; name[1] = "EVOLVE"; name[2] = "TENSION"; name[3] = "DEPTH";
            v[0] = eng.pad_p.darkness; v[1] = eng.pad_p.evolve; v[2] = eng.pad_p.tension; v[3] = eng.pad_p.depth;
        }
        break;
    case PG_FX:
        name[0] = "WEIGHT"; name[1] = "DIRT"; name[2] = "WIDTH"; name[3] = "PRESSURE";
        v[0] = eng.master.m.weight; v[1] = eng.master.m.dirt; v[2] = eng.master.m.width; v[3] = eng.master.m.pressure;
        break;
    case PG_SCENE:
        name[0] = "QUANT"; name[1] = "DUCK"; name[2] = "SOLO DUCK"; name[3] = "FOLLOW";
        v[0] = (float)set.scene_quant / 4.0f; v[1] = eng.duck_depth; v[2] = eng.duck_internal; v[3] = eng.follow_link;
        vt[0] = QUANT_NAME[set.scene_quant]; vt[2] = eng.duck_internal ? "ON" : "OFF"; vt[3] = eng.follow_link ? "LINK" : "OFF";
        break;
    default:
        name[0] = "BPM"; name[1] = "VOICES"; name[2] = "LINK CH"; name[3] = "FOLLOW";
        v[0] = (eng.clock.bpm - 60.0f) / 160.0f; v[1] = (float)eng.voice_mode / 2.0f; v[2] = (float)eng.link.channel / 15.0f; v[3] = eng.follow_link;
        ui_fix1(b[0], eng.clock.bpm); vt[0] = b[0];
        vt[1] = VMODE_NAME[eng.voice_mode % 3u];
        ui_int(b[2], eng.link.channel + 1); vt[2] = b[2];
        vt[3] = eng.follow_link ? "LINK" : "OFF";
        break;
    }
    for (k = 0; k < 4u; k++) (void)k;
    ui_macros(name, v, vt, ui.hi_until && plat_ms() < ui.hi_until ? ui.hi_macro : -1);
}

static void lights(void)
{
    uint32_t b = 0, keys = 0, dimk = 0, k;
    static const uint8_t PB[NPAGES] = {B_HOME, B_SEQ, B_EDIT, B_FX, B_ARP, B_GLO};
    b |= 1u << PB[ui.page];
    if (eng.clock.running) b |= 1u << B_PLAY;
    if (dirty) b |= 1u << B_SAVE;
    if (eng.pad_on) b |= 1u << B_REC;
    if (eng.mutated) b |= 1u << B_LFO;
    if (ui.page == PG_SEQ) {
        const js_track_t *tr = &eng.pat[eng.cur_pat].t[ui.track];
        for (k = 0; k < 16u; k++) if (tr->s[k].flags & JS_ON) keys |= ui_white_bit((int)k);
        keys |= ui_black_bit(ui.track);
    } else if (ui.page == PG_SCENE) {
        for (k = 0; k < AB_NSCENES; k++) if (eng.scene[k].used) dimk |= ui_white_bit((int)k);
        if (eng.scene_cur >= 0) keys |= ui_white_bit(eng.scene_cur);
    } else {
        /* the keyboard: the root's keys lit dim, so the key is visible in the dark */
        for (k = 0; k < NKEYS; k++) if (((key_note(k) - eng.root) % 12 + 12) % 12 == 0) dimk |= 1u << k;
        keys = ui.in.keys;
    }
    plat_lights(b, keys, 0, dimk, 0);
}

void app_ui_init(void)
{
    touch_on(1);
    ui_theme(UI_THEME_ABYSS);
    if (set.link_usb_host) plat_usb_host(1); /* (main loop only: plat.h) */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui_splash("ABYSS", "ENGINE", "JUNGLE OS " APP_VERSION "  -  NEURO BASS AND SUB");
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
    if (now - last_t > 120u) { last_t = now; ui.redraw = 1; }
    if (!ui.redraw) return;
    ui.redraw = 0;
    draw_header();
    draw_body();
    draw_strip();
    draw_footer();
}
