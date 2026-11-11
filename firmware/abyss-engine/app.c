/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* ABYSS ENGINE: the app (screens, controls, storage, MIDI and the link). The engine is engine/ab_engine.c
 * (-O2 unit dsp_o2.c). Part of fm1-base's unity build. The control map (0.5: a sequencer first, the live tools on
 * held layers) is docs/architecture/control-maps.md and firmware/abyss-engine/docs/MANUAL.md. */
#include "engine/ab_engine.h"
#include "../../shared/store/jo_store.h"
#include "../../shared/seq/jo_undo.h"
#include "../../shared/ui/jo_ui.c"

static ab_engine_t eng PLAT_POOL;
static int16_t fx_pool[AB_POOL_SAMPLES] PLAT_POOL; /* the delay's lines and the reverb: 96 KiB */
static ju_stack_t undo PLAT_POOL;                 /* UNDO / REDO: 8 patterns and their sound states, 23 KiB */

#define SET_MAGIC 0x35534241u /* "ABS5" (0.7: the new sounds and styles) */
typedef struct {
    float bpm, morph, motion, bite, space, duck_depth;
    jf_neuro_macros_t fx;
    jn_stab_patch_t stab;
    jn_pad_patch_t pad;
    float lfo_shape, tension, glide;
    int8_t root, octave;
    uint8_t scale, chord, inversion, link_ch, follow_link, voice_mode;
    uint8_t duck_internal, scene_quant, sub_octave, link_usb_host;
    uint8_t form, phrase, lock_kick, fill_answers, fill_bars, fill_type;
    int8_t var_line[AB_NVAR - 1], var_node[AB_NVAR - 1]; /* B, C, D */
    float energy, pocket_amt, decay_amt;                  /* (0.6) */
    uint8_t duet_xform, gesture, decay_on;
} settings_t;
static settings_t set;
#define PAT_MAGIC 0x33544150u /* (0.6: the steps' ENERGY levels, the tracks' routes) */
#define SCN_MAGIC 0x314E4353u
static struct { jo_rec_hdr_t h; uint8_t p[sizeof(js_pattern_t) > sizeof(ab_scene_t) * AB_NSCENES ? sizeof(js_pattern_t)
                                                                                    : sizeof(ab_scene_t) * AB_NSCENES]; } io;
_Static_assert(sizeof io <= 3840u, "a store object holds 3,840 bytes");
_Static_assert(sizeof(ab_sound_state_t) <= JU_EXTRA, "the sound state fits UNDO's extra blob");
static uint8_t dirty, load_report;

enum { PG_MAIN, PG_SEQ, PG_SOUND, PG_FX, PG_SONG, PG_SYSTEM, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"HOME", "SEQUENCE", "SOUND", "FX", "SONG", "SYSTEM"};
/* the held layers, in priority (the first held one is the layer) */
enum { L_NONE, L_SEL, L_STORE, L_FX, L_SONG, L_FILL, L_VARY, L_ERASE, L_ENERGY, L_DUET, NLAYERS };
static const char *const LAYER_NAME[NLAYERS] = {"", "SEL", "STORE", "FX", "SONG", "FILL", "VARY", "ERASE", "ENERGY", "DUET"};
static const char *const SCENE_NAME[AB_NSCENES] = {"INTRO", "BUILD", "DROP", "MUTATION", "VOID", "IMPACT", "SCENE 7", "SCENE 8"};
static const char *const QUANT_NAME[6] = {"NOW", "BEAT", "BAR", "2 BAR", "4 BAR", "PHRASE"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const SHAPE_NAME[5] = {"SINE", "EXP", "PULSE", "ASYM", "STEP"};
static const char *const VMODE_NAME[3] = {"FULL", "NO PAD", "BASS ONLY"};
static const char *const VAR_NAME[AB_NVAR] = {"A", "B", "C", "D"};
static const char *const PAT_NAME[AB_NPATTERNS] = {"P1", "P2", "P3", "P4"};

static struct {
    uint8_t page, track, step_page, redraw, rec;
    int8_t held_step;
    uint8_t step_pushed;               /* the held step's first edit has pushed UNDO */
    uint8_t layer_pushed;              /* this layer hold has pushed UNDO (DENSITY) */
    uint32_t key_used;
    int8_t hi_macro;
    uint32_t hi_until;
    char msg[2][24];
    uint32_t msg_until;
    uint32_t mutate_seed;
    float mutate_radius;
    int8_t key_note[NKEYS];            /* the note each held key started (its release ends that one) */
    int8_t fx_key;                     /* the white key holding a performance FX (-1) */
    uint8_t vary;                      /* VARY: the variation picked last (its LINE / SOUND on ALGO / K4) */
    uint8_t lib_line, lib_stab, lib_pad;
    uint8_t snd;                      /* (0.7) the SOUND page's sound browser: the last sound picked */
    uint32_t hint_until;              /* (0.7) a style's name and hint shown over the page until then */
    uint32_t stop_ms;                  /* when the transport stopped (autosave 3 s later) */
    uint8_t song_page;                 /* (0.6) SONG's knobs: 0 the form and the link, 1 DECAY, POCKET, ECHO */
    uint8_t burned;                    /* (0.6) the held step was burned in (TAPE DECAY) */
    uint8_t was_running;
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
    uint32_t i;
    set.bpm = 174.0f;
    set.morph = 2.0f / 7.0f, set.motion = 0.5f, set.bite = 0.3f, set.space = 0.4f;
    set.duck_depth = 0.6f;
    set.fx.weight = 0.5f, set.fx.dirt = 0.3f, set.fx.width = 0.4f, set.fx.pressure = 0.4f, set.fx.trim_db = 0.0f;
    set.stab.model = JN_STAB_RAVE, set.stab.chord = 0.3f, set.stab.attack = 0.1f, set.stab.bite = 0.5f, set.stab.tail = 0.4f;
    set.pad.darkness = 0.5f, set.pad.evolve = 0.4f, set.pad.tension = 0.3f, set.pad.depth = 0.6f;
    set.pad.attack_s = 1.5f, set.pad.release_s = 3.0f;
    set.lfo_shape = 0.0f;
    set.tension = 0.2f;
    set.glide = 0.5f;
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
    set.form = AB_FORM_OFF;
    set.phrase = 4;
    set.lock_kick = 0;
    set.fill_answers = 0;
    set.fill_bars = 1;
    set.fill_type = AB_FILL_ROLLS;
    for (i = 0; i < AB_NVAR - 1u; i++) set.var_line[i] = eng.cr[i].line, set.var_node[i] = eng.cr[i].node;
    set.energy = 1.0f;
    set.pocket_amt = 0.0f;
    set.decay_amt = 0.3f;
    set.duet_xform = AB_ANS_ECHO;
    set.gesture = 0;
    set.decay_on = 0;
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
    uint32_t i;
    js_clock_set_bpm(&eng.clock, set.bpm);
    eng.morph = eng.morph_s = set.morph, eng.motion = set.motion, eng.bite = set.bite, eng.space = set.space;
    eng.duck_depth = set.duck_depth;
    eng.master.m = set.fx;
    jf_neuro_update(&eng.master);
    eng.stab_p = set.stab;
    eng.pad_p = set.pad;
    eng.patch.lfo_shape = set.lfo_shape;
    eng.tension = set.tension;
    eng.glide_amt = set.glide;
    eng.root = set.root;
    eng.scale = set.scale;
    eng.chord = set.chord;
    eng.inversion = set.inversion;
    jl_init(&eng.link, set.link_ch);
    eng.follow_link = set.follow_link;
    eng.voice_mode = set.voice_mode;
    eng.duck_internal = set.duck_internal;
    eng.sub.octave = set.sub_octave ? -1 : 0;
    for (i = 0; i < AB_NVAR - 1u; i++) eng.cr[i].line = set.var_line[i], eng.cr[i].node = set.var_node[i];
    ab_set_form(&eng, set.form);
    ab_set_phrase(&eng, set.phrase);
    eng.quant = set.scene_quant == 5;
    eng.lock_kick = set.lock_kick;
    eng.fill_answers = set.fill_answers;
    eng.en.knob = eng.en.value = set.energy;
    eng.seq.energy = je_level(set.energy);
    eng.pocket_amt = set.pocket_amt;
    eng.decay_amt = set.decay_amt;
    eng.duet_xform = set.duet_xform;
    eng.gesture = set.gesture;
    eng.decay_on = set.decay_on;
}

static void engine_to_settings(void)
{
    uint32_t i;
    set.bpm = eng.clock.bpm;
    set.morph = eng.morph, set.motion = eng.motion, set.bite = eng.bite, set.space = eng.space;
    set.duck_depth = eng.duck_depth;
    set.fx = eng.master.m;
    set.stab = eng.stab_p;
    set.pad = eng.pad_p;
    set.lfo_shape = eng.patch.lfo_shape;
    set.tension = eng.tension;
    set.glide = eng.glide_amt;
    set.root = eng.root, set.scale = eng.scale, set.chord = eng.chord, set.inversion = eng.inversion;
    set.follow_link = eng.follow_link, set.voice_mode = eng.voice_mode, set.duck_internal = eng.duck_internal;
    set.form = eng.form, set.phrase = eng.phrase, set.lock_kick = eng.lock_kick, set.fill_answers = eng.fill_answers;
    for (i = 0; i < AB_NVAR - 1u; i++) set.var_line[i] = eng.cr[i].line, set.var_node[i] = eng.cr[i].node;
    set.energy = eng.en.knob, set.pocket_amt = eng.pocket_amt, set.decay_amt = eng.decay_amt;
    set.duet_xform = eng.duet_xform, set.gesture = eng.gesture, set.decay_on = eng.decay_on;
}

static int settings_ok(void)
{
    uint32_t i;
    if (!(set.bpm >= 40.0f && set.bpm <= 300.0f) || set.scale >= JH_NSCALES || set.chord >= JH_NCHORDS ||
        set.stab.model >= JN_STAB_NMODELS || set.voice_mode > 2u || set.root < 24 || set.root > 60 ||
        set.form >= AB_NFORMS || set.scene_quant > 5u || set.fill_type >= AB_NFILLS || set.duet_xform >= AB_NANS ||
        !(set.energy >= 0.0f && set.energy <= 1.0f) || !(set.pocket_amt >= 0.0f && set.pocket_amt <= 1.0f) ||
        !(set.decay_amt >= 0.0f && set.decay_amt <= 1.0f))
        return 0;
    for (i = 0; i < AB_NVAR - 1u; i++)
        if (set.var_line[i] < -1 || set.var_line[i] >= AB_NPATTERNS || set.var_node[i] < -1 || set.var_node[i] >= (int)eng.fm.nnodes)
            return 0;
    return 1;
}

static void load_all(void)
{
    uint32_t p;
    load_report = 0;
    if (load_rec(OBJ_SETTINGS, SET_MAGIC, &set, sizeof set) || !settings_ok()) {
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

static void save_all(int quiet)
{
    uint32_t p;
    int rc = 0;
    if (eng.clock.running) { app_say("STOP FIRST", "SAVING STALLS SOUND"); return; }
    engine_to_settings();
    rc |= save_rec(OBJ_SETTINGS, SET_MAGIC, &set, sizeof set);
    for (p = 0; p < AB_NPATTERNS; p++) rc |= save_rec(OBJ_PAT0 + p, PAT_MAGIC, &eng.pat[p], sizeof eng.pat[p]);
    rc |= save_rec(OBJ_SCENES, SCN_MAGIC, eng.scene, sizeof eng.scene);
    if (rc) app_say("SAVE", "FAILED");
    else { dirty = 0; app_say(quiet ? "AUTOSAVED" : "SAVED", "PATTERNS + SCENES"); }
}

static void link_out(uint32_t pkt) { plat_midi_send(pkt, TO_USB); } /* (0.6) ENERGY goes to Break Surgeon */

void app_init(void)
{
    uint32_t i;
    ab_init(&eng, fx_pool);
    eng.midi_out = link_out;
    ju_init(&undo);
    load_all();
    ui.lib_line = 0xFFu; /* (0.7) no style loaded yet: ALGO starts at the first */
    ui.held_step = -1;
    ui.hi_macro = -1;
    ui.fx_key = -1;
    ui.mutate_seed = 1;
    ui.mutate_radius = 0.35f;
    ui.vary = 1;
    for (i = 0; i < NKEYS; i++) ui.key_note[i] = -1;
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

/* ----------------------------------------------------------------- undo --- */
/* the state before a change: the playing pattern and the bass's sound */
static void undo_push(void)
{
    ab_sound_state_t s;
    ab_sound_get(&eng, &s);
    ju_push(&undo, eng.cur_pat, &eng.pat[eng.cur_pat], &s, sizeof s);
    dirty = 1;
}
static int undo_do(int redo)
{
    ab_sound_state_t s;
    uint8_t slot;
    uint32_t f;
    int rc;
    ab_sound_get(&eng, &s);
    f = plat_lock();
    rc = redo ? ju_redo(&undo, eng.pat, &s, sizeof s, &slot) : ju_undo(&undo, eng.pat, &s, sizeof s, &slot);
    if (!rc) ab_sound_set(&eng, &s);
    plat_unlock(f);
    if (!rc) dirty = 1;
    return rc;
}

/* ---------------------------------------------------------------- names --- */
static js_track_t *cur_track(void) { return &eng.pat[eng.cur_pat].t[ui.track % AB_NTRACKS]; }
static js_step_t *held_step(void) { return &cur_track()->s[(ui.step_page * 16u + (uint32_t)ui.held_step) % JS_MAXSTEPS]; }

static void note_name(char *t, int note)
{
    char v[6];
    t[0] = 0;
    ui_cat(t, NOTE_NAME[(note % 12 + 12) % 12]);
    ui_int(v, note / 12 - 1);
    ui_cat(t, v);
}
/* a variation's line ("A" or "P1".."P4") and sound ("A" or the node) */
static void var_line_name(char *t, int line)
{
    t[0] = 0;
    if (line < 0) { ui_cat(t, "A"); return; }
    ui_cat(t, PAT_NAME[line % AB_NPATTERNS]);
}
static const char *var_node_name(int node) { return node < 0 ? "A" : eng.fm.node[node].name; }
/* (0.7) A's sound (the one the SOUND page's browser sets; a FORM's answers may be playing their own) */
static uint32_t a_sound(void)
{
    jg_eval_t ev;
    if (eng.explore_node >= 0) return (uint32_t)eng.explore_node;
    jg_eval(&eng.fm, eng.morph, &ev);
    return ev.near;
}

static const char *lib_name(void)
{
    if (ui.track == AB_T_BASS) return ab_line_name[ui.lib_line % AB_NLINES];
    if (ui.track == AB_T_STAB) return ab_stab_rhythm_name[ui.lib_stab % AB_NRHYTHMS];
    return ab_pad_rhythm_name[ui.lib_pad % AB_NRHYTHMS];
}

/* the layer held now (the first of SEL, SAVE, FX, ARP, ENV, LFO, REC, HOME, SEQ held). (0.6) HOME and SEQ count
 * as layers once held past a tap (a tap still opens their page) */
static int layer(void)
{
    static const uint8_t B[9] = {B_SEL, B_SAVE, B_FX, B_ARP, B_ENV, B_LFO, B_REC, B_HOME, B_SEQ};
    uint32_t i;
    for (i = 0; i < 9u; i++)
        if (ui_held(&ui.in, B[i])) return (int)i + 1;
    return L_NONE;
}
static void use_layer(int l)
{
    static const uint8_t B[NLAYERS] = {0, B_SEL, B_SAVE, B_FX, B_ARP, B_ENV, B_LFO, B_REC, B_HOME, B_SEQ};
    if (l > 0 && l < NLAYERS) ui_use(&ui.in, B[l]);
}

/* ---------------------------------------------------------------- knobs --- */
/* the track's four sound controls (HOME, SEQUENCE, the FX layer has the bus) */
static float *track_knob(int k)
{
    switch (ui.track) {
    case AB_T_BASS: return k == 0 ? &eng.morph : k == 1 ? &eng.motion : k == 2 ? &eng.bite : &eng.glide_amt;
    case AB_T_STAB: return k == 0 ? &eng.stab_p.chord : k == 1 ? &eng.stab_p.attack : k == 2 ? &eng.stab_p.bite : &eng.stab_p.tail;
    default: return k == 0 ? &eng.pad_p.darkness : k == 1 ? &eng.pad_p.evolve : k == 2 ? &eng.pad_p.tension : &eng.pad_p.depth;
    }
}
static float *bus_knob(int k)
{
    return k == 0 ? &eng.master.m.weight : k == 1 ? &eng.master.m.dirt : k == 2 ? &eng.master.m.width : &eng.master.m.pressure;
}

static void nudge(float *v, int32_t d, float step) { *v = jo_clampf(*v + step * (float)d, 0.0f, 1.0f); }

static void turn_k(int k, int32_t d)
{
    float step = ui_held(&ui.in, B_SEL) ? 0.0025f : 0.01f;
    int l = layer();
    js_track_t *tr = cur_track();
    if (ui_held(&ui.in, B_SEL)) ui_use(&ui.in, B_SEL);
    ui.hi_macro = (int8_t)k;
    ui.hi_until = plat_ms() + 1200u;
    ui.redraw = 1;
    dirty = 1;
    if (ui.held_step >= 0) { /* the held step: NOTE, VELOCITY, a CHARACTER lock, a BITE lock */
        js_step_t *st = held_step();
        if (!ui.step_pushed) undo_push(), ui.step_pushed = 1;
        ui.key_used |= ui_white_bit(ui.held_step);
        st->flags |= JS_ON;
        if (k == 0) {
            if (ui.track == AB_T_BASS) {
                int note = (int)st->data - 64 + eng.root, n2 = jh_snap(eng.root, eng.scale, note + (d > 0 ? 1 : -1));
                if (n2 == note) n2 = jh_snap(eng.root, eng.scale, note + (d > 0 ? 2 : -2));
                st->data = (uint8_t)jo_clampi(n2 - eng.root + 64, 40, 100);
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
    switch (l) {
    case L_ENERGY: /* (0.6) K1 ENERGY, K2 the curve, K3 AUTO LEVELS */
        ui_use(&ui.in, B_HOME);
        if (k == 0) ab_energy_set(&eng, eng.en.knob + step * 2.0f * (float)d);
        else if (k == 1) {
            int m = jo_clampi(ab_energy_curve_mode(&eng) + (d > 0 ? 1 : -1), 0, AB_NCURVES - 1);
            ab_energy_curve(&eng, m);
            app_say("ENERGY CURVE", m == AB_CURVE_REC ? "FROM THE PHRASE START" : ab_curve_name[m]);
        } else if (k == 2) {
            undo_push();
            ab_auto_levels(&eng, ui.track);
            app_say("AUTO LEVELS", ab_track_name[ui.track]);
        }
        return;
    case L_DUET: /* (0.6) K1 ANSWER ME, K2 its transform, K3 POCKET, K4 GESTURE ECHO */
        ui_use(&ui.in, B_SEQ);
        if (k == 0) { ab_set_answer(&eng, d > 0); app_say("ANSWER ME", eng.duet_answer ? "PLAY A BAR" : "OFF"); }
        else if (k == 1) eng.duet_xform = (uint8_t)jo_clampi(eng.duet_xform + (d > 0 ? 1 : -1), 0, AB_NANS - 1);
        else if (k == 2) nudge(&eng.pocket_amt, d, 0.02f);
        else eng.gesture = d > 0;
        return;
    case L_FX: ui_use(&ui.in, B_FX); nudge(bus_knob(k), d, step); jf_neuro_update(&eng.master); return;
    case L_FILL:
        ui_use(&ui.in, B_ENV);
        if (k == 0) set.fill_bars = d > 0 ? 2 : 1;
        return;
    case L_VARY:
        ui_use(&ui.in, B_LFO);
        if (k == 0) { /* DENSITY: one step more or less, by the ranking */
            if (!ui.layer_pushed) undo_push(), ui.layer_pushed = 1;
            ab_density(&eng, ui.track, d > 0 ? 1 : -1);
        } else if (k == 1) nudge(&eng.humanize, d, 0.02f);
        else if (k == 2) ui.mutate_radius = jo_clampf(ui.mutate_radius + 0.02f * (float)d, 0.0f, 1.0f);
        else if (ui.vary == 0) { nudge(&eng.morph, d, step); eng.explore_node = -1, eng.mutated = 0; }
        else {
            ab_resp_t *r = &eng.cr[(ui.vary - 1u) % (AB_NVAR - 1u)];
            r->node = (int8_t)jo_clampi(r->node + (d > 0 ? 1 : -1), -1, (int)eng.fm.nnodes - 1);
        }
        return;
    default: break;
    }
    switch (ui.page) {
    case PG_SEQ: /* the track's shape */
        if (k == 0) tr->len = (uint8_t)jo_clampi(tr->len + (d > 0 ? 1 : -1), 1, JS_MAXSTEPS);
        else if (k == 1) eng.pat[eng.cur_pat].swing = (uint8_t)jo_clampi(eng.pat[eng.cur_pat].swing + d, 50, 75);
        else if (k == 2) tr->div = (uint8_t)jo_clampi(tr->div + (d > 0 ? 1 : -1), 0, 2);
        else nudge(&eng.space, d, step);
        return;
    case PG_SOUND: /* the depth */
        if (ui.track == AB_T_BASS) {
            if (k == 0) eng.patch.lfo_shape = (float)jo_clampi((int32_t)eng.patch.lfo_shape + (d > 0 ? 1 : -1), 0, 4);
            else if (k == 1) nudge(&eng.sub.sat, d, 0.02f);
            else if (k == 2) { set.sub_octave = d < 0; eng.sub.octave = set.sub_octave ? -1 : 0; }
            else nudge(&eng.tension, d, step);
        } else {
            if (k == 0) nudge(&eng.space, d, step);
            else if (k == 1) nudge(&eng.tension, d, step);
            else if (k == 2 && ui.track == AB_T_STAB) eng.inversion = (uint8_t)jo_clampi(eng.inversion + (d > 0 ? 1 : -1), 0, 2);
            else if (k == 2) eng.pad_p.attack_s = jo_clampf(eng.pad_p.attack_s + 0.1f * (float)d, 0.05f, 6.0f);
            else if (ui.track == AB_T_PAD) eng.pad_p.release_s = jo_clampf(eng.pad_p.release_s + 0.1f * (float)d, 0.2f, 8.0f);
        }
        return;
    case PG_FX: nudge(bus_knob(k), d, step); jf_neuro_update(&eng.master); return;
    case PG_SONG:
        if (ui.song_page) { /* (0.6) TAPE DECAY, its amount, POCKET, GESTURE ECHO */
            if (k == 0) eng.decay_on = d > 0;
            else if (k == 1) nudge(&eng.decay_amt, d, 0.02f);
            else if (k == 2) nudge(&eng.pocket_amt, d, 0.02f);
            else eng.gesture = d > 0;
            return;
        }
        if (k == 0) ab_set_phrase(&eng, d > 0 ? (eng.phrase == 4 ? 8 : 16) : (eng.phrase == 16 ? 8 : 4));
        else if (k == 1) { set.scene_quant = (uint8_t)jo_clampi(set.scene_quant + (d > 0 ? 1 : -1), 0, 5); eng.quant = set.scene_quant == 5; }
        else if (k == 2) eng.lock_kick = d > 0;
        else eng.fill_answers = d > 0;
        return;
    case PG_SYSTEM:
        if (k == 0) ab_cmd_bpm(&eng, jo_clampf(eng.clock.bpm + 0.5f * (float)d, 60.0f, 220.0f));
        else if (k == 1) eng.voice_mode = (uint8_t)jo_clampi(eng.voice_mode + (d > 0 ? 1 : -1), 0, 2);
        else if (k == 2) { set.link_ch = (uint8_t)jo_clampi(set.link_ch + (d > 0 ? 1 : -1), 0, 15); jl_init(&eng.link, set.link_ch); }
        else eng.follow_link = d > 0;
        return;
    default: /* HOME: the track's sound */
        nudge(track_knob(k), d, step);
        if (ui.track == AB_T_BASS && k == 0) eng.explore_node = -1, eng.mutated = 0;
        if (ui.track == AB_T_BASS && eng.gesture) ab_gesture_knob(&eng, k, *track_knob(k)); /* (0.6) GESTURE ECHO */
        return;
    }
}

/* --------------------------------------------------------------- input --- */
/* the part the keys play: the track, or (0.6) the bass while ANSWER ME listens */
static int key_part(void) { return eng.duet_answer && !ui.rec && ui.page != PG_SOUND ? AB_T_BASS : ui.track; }
static int key_note(uint32_t k)
{
    int part = key_part(), base = part == AB_T_BASS ? 29 : part == AB_T_STAB ? 53 : 41; /* F1, F3, F2 at the lowest key */
    return base + (int)k + 12 * set.octave;
}

static void play_key(uint32_t k, int down)
{
    if (down) {
        uint32_t v = touch_vel(k);
        int note = key_note(k);
        float vel = (float)(v ? v : 100u) * (1.0f / 127.0f);
        ui.key_note[k] = (int8_t)note;
        ab_cmd_note(&eng, key_part(), note, vel, 1);
        if (ui.rec && ui.page != PG_SOUND) {
            int s = ab_record(&eng, ui.track, note, vel);
            if (s >= 0) dirty = 1;
        }
    } else if (ui.key_note[k] >= 0) {
        ab_cmd_note(&eng, key_part(), ui.key_note[k], 0.0f, 0);
        ui.key_note[k] = -1;
    }
}

static void pattern_queue(int p)
{
    ab_cmd_pattern(&eng, p);
    app_say(PAT_NAME[p % AB_NPATTERNS], eng.clock.running ? (eng.quant ? "AT THE PHRASE END" : "AT THE NEXT BAR") : "");
}

/* a key in a held layer; returns 1 if the layer took it */
static int layer_key(int l, int w, int b, int down, int up)
{
    if (l == L_NONE || l == L_SEL) return 0;
    if (l == L_DUET) { use_layer(l); return 0; } /* (0.6) DUET: the keys as outside it (they play the bass with ANSWER ME) */
    use_layer(l);
    switch (l) {
    case L_ENERGY: /* (0.6) a white key: its step's level 1 > 2 > 3 > 4 > 1 */
        if (down && w >= 0) {
            js_step_t *st = &cur_track()->s[(ui.step_page * 16u + (uint32_t)w) % JS_MAXSTEPS];
            char t[16] = "STEP ", v[8];
            undo_push();
            st->lvl = (uint8_t)((st->lvl ? st->lvl : 1u) % 4u + 1u);
            ui_int(v, w + 1); ui_cat(t, v);
            ui_cat(t, " LEVEL "); ui_int(v, st->lvl); ui_cat(t, v);
            app_say("ENERGY", t);
        }
        return 1;
    case L_FX: /* the performance FX, while the key is held */
        if (down && w >= 0 && w < AB_NFX - 1) { ab_set_perf(&eng, w + 1); ui.fx_key = (int8_t)w; }
        if (up && w >= 0 && w == ui.fx_key) { ab_set_perf(&eng, AB_FX_NONE); ui.fx_key = -1; }
        return 1;
    case L_SONG:
        if (!down) return 1;
        if (w >= 0 && w < AB_NSCENES) {
            int q = set.scene_quant == 5 ? 5 : set.scene_quant;
            ab_cmd_scene(&eng, w, q);
            app_say(SCENE_NAME[w], QUANT_NAME[set.scene_quant % 6u]);
        } else if (w >= 8 && w < 8 + AB_NPATTERNS) pattern_queue(w - 8);
        else if (b >= 0 && b < AB_NTRACKS) {
            ab_cmd_mute(&eng, b);
            app_say((eng.seq.mute >> b) & 1u ? "UNMUTE" : "MUTE", ab_track_name[b]);
        }
        return 1;
    case L_FILL:
        if (down && w >= 0 && w < AB_NFILLS) {
            set.fill_type = (uint8_t)w;
            ab_cmd_fill_type(&eng, w, set.fill_bars, 0);
            app_say(ab_fill_name[w], set.fill_bars == 2 ? "2 BARS, NEXT BAR" : "1 BAR, NEXT BAR");
        }
        return 1;
    case L_VARY:
        if (!down) return 1;
        if (w >= 0 && w < AB_NVAR) {
            ui.vary = (uint8_t)w;
            ab_cmd_variation(&eng, w);
            app_say("VARIATION", VAR_NAME[w]);
        } else if (w >= 8 && w < 8 + AB_NFORMS) {
            ab_set_form(&eng, w - 8);
            dirty = 1;
            app_say("FORM", ab_form_name[w - 8]);
        }
        return 1;
    case L_STORE:
        if (down && w >= 0 && w < AB_NSCENES) {
            ab_scene_store(&eng, w);
            dirty = 1;
            app_say("SCENE STORED", SCENE_NAME[w]);
        }
        return 1;
    case L_ERASE:
        if (!down) return 1;
        if (w >= 0) {
            js_step_t *st = &cur_track()->s[(ui.step_page * 16u + (uint32_t)w) % JS_MAXSTEPS];
            undo_push();
            st->flags &= (uint8_t)~JS_ON;
            st->cond = JS_C_ALWAYS;
            st->lk_id[0] = st->lk_id[1] = JS_LOCK_NONE;
        } else if (b >= 0 && b < AB_NTRACKS) {
            js_track_t *t = &eng.pat[eng.cur_pat].t[b];
            uint32_t i;
            undo_push();
            for (i = 0; i < JS_MAXSTEPS; i++) t->s[i].flags &= (uint8_t)~JS_ON;
            app_say("TRACK CLEARED", ab_track_name[b]);
        }
        return 1;
    default: return 0;
    }
}

static void input(void)
{
    ui_in_t *in = &ui.in;
    uint32_t k;
    int32_t d;
    int l, sel;
    ui_poll(in);
    l = layer();
    sel = ui_held(in, B_SEL);
    /* the SEL layer's buttons: UNDO, REDO, PANIC, RETURN */
    if (sel && ui_pressed(in, B_LFO)) { ui_use(in, B_SEL); ui_use(in, B_LFO); app_say("UNDO", undo_do(0) ? "NOTHING TO UNDO" : "DONE"); }
    if (sel && ui_pressed(in, B_ENV)) { ui_use(in, B_SEL); ui_use(in, B_ENV); app_say("REDO", undo_do(1) ? "NOTHING TO REDO" : "DONE"); }
    if (sel && ui_pressed(in, B_REC)) {
        ui_use(in, B_SEL); ui_use(in, B_REC);
        ab_cmd_return(&eng);
        if (eng.scene_cur >= 0) ab_cmd_scene(&eng, eng.scene_cur, set.scene_quant);
        app_say("RETURN", eng.scene_cur >= 0 ? SCENE_NAME[eng.scene_cur] : "SNAP");
    }
    if (ui_pressed(in, B_PLAY)) {
        if (sel) { ui_use(in, B_SEL); ab_cmd_panic(&eng); ab_cmd_play(&eng, 0); app_say("PANIC", "ALL NOTES OFF"); }
        else ab_cmd_play(&eng, !eng.clock.running);
    }
    /* taps: the pages, and the buttons' own actions */
    if (ui_tapped(in, B_HOME)) ui.page = PG_MAIN, ui.redraw = 1;
    if (ui_tapped(in, B_SEQ)) ui.page = PG_SEQ, ui.redraw = 1;
    if (ui_tapped(in, B_EDIT)) ui.page = PG_SOUND, ui.redraw = 1;
    if (ui_tapped(in, B_FX)) ui.page = PG_FX, ui.redraw = 1;
    if (ui_tapped(in, B_ARP)) ui.page = PG_SONG, ui.redraw = 1;
    if (ui_tapped(in, B_GLO)) ui.page = PG_SYSTEM, ui.redraw = 1;
    if (ui_tapped(in, B_ENV)) {
        ab_cmd_fill_type(&eng, set.fill_type, set.fill_bars, 1);
        app_say(ab_fill_name[set.fill_type % AB_NFILLS], "AT THE PHRASE END");
    }
    if (ui_tapped(in, B_LFO)) {
        undo_push();
        ab_mutate(&eng, ui.mutate_radius, ui.mutate_seed++);
        app_say("MUTATE", "SEL+LFO: UNDO");
    }
    if (ui_tapped(in, B_REC)) {
        ui.rec = !ui.rec;
        if (ui.rec) undo_push(); /* a record pass is one UNDO */
        app_say(ui.rec ? "RECORD" : "RECORD OFF", ui.rec ? (eng.clock.running ? "PLAY THE KEYS" : "PRESS PLAY") : "");
    }
    if (ui_tapped(in, B_SAVE)) save_all(0);
    if (in->btn_down || in->btn_up) ui.redraw = 1;
    if (in->btn_up & (1u << B_LFO)) ui.layer_pushed = 0;
    if ((in->btn_up & (1u << B_FX)) && ui.fx_key >= 0) { ab_set_perf(&eng, AB_FX_NONE); ui.fx_key = -1; }
    /* OCT- / OCT+: the step page, or the keyboard's octave while it plays (REC, SOUND) */
    if (ui_pressed(in, B_OCTDN) || ui_pressed(in, B_OCTUP)) {
        int up = ui_pressed(in, B_OCTUP);
        if (ui.rec || ui.page == PG_SOUND || eng.duet_answer) set.octave = (int8_t)jo_clampi(set.octave + (up ? 1 : -1), -2, 2);
        else if (ui.page == PG_SONG && l == L_NONE) ui.song_page = (uint8_t)up; /* (0.6) SONG's two knob pages */
        else ui.step_page = (uint8_t)(up && cur_track()->len > 16u);
        ui.redraw = 1;
    }
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        int w = ui_white(k), b = ui_black(k);
        int down = (in->key_down & bit) != 0, up = (in->key_up & bit) != 0;
        if (!down && !up) continue;
        ui.redraw = 1;
        if (up && ui.key_note[k] >= 0) { play_key(k, 0); ui.key_used &= ~bit; continue; } /* a note started: it ends */
        if (layer_key(l, w, b, down, up)) { if (down) ui.key_used |= bit; else ui.key_used &= ~bit; continue; }
        if (up && (ui.key_used & bit) && !(w >= 0 && w == ui.held_step)) { ui.key_used &= ~bit; continue; }
        if (ui.rec || ui.page == PG_SOUND || eng.duet_answer) { if (down) play_key(k, 1); continue; } /* the keys play (and REC records) */
        if (w >= 0) { /* a step: tap toggles, hold edits */
            if (down) { ui.held_step = (int8_t)w; ui.step_pushed = 0; ui.burned = 0; ui.key_used &= ~bit; }
            else {
                if (!(ui.key_used & bit)) {
                    undo_push();
                    cur_track()->s[(ui.step_page * 16u + (uint32_t)w) % JS_MAXSTEPS].flags ^= JS_ON;
                }
                ui.key_used &= ~bit;
                if (ui.held_step == w) ui.held_step = -1;
            }
        } else if (down && b >= 0 && b < AB_NTRACKS) {
            ui.track = (uint8_t)b;
            if (cur_track()->len <= 16u) ui.step_page = 0;
            app_say("TRACK", ab_track_name[b]);
        } else if (down && b >= 3 && b <= 4 && ui.held_step >= 0) { /* black 4 / 5: ACCENT / SLIDE of the held step */
            js_step_t *st = held_step();
            if (!ui.step_pushed) undo_push(), ui.step_pushed = 1;
            st->flags ^= b == 3 ? JS_ACCENT : JS_SLIDE;
            st->flags |= JS_ON;
            ui.key_used |= ui_white_bit(ui.held_step);
        }
    }
    /* (0.6) TAPE DECAY: a step held half a second without a knob turned is burned in */
    if (ui.held_step >= 0 && !ui.burned && !(ui.key_used & ui_white_bit(ui.held_step)) && eng.decay_on) {
        uint32_t kk;
        for (kk = 0; kk < NKEYS; kk++)
            if (ui_white(kk) == ui.held_step && (in->keys >> kk & 1u) && plat_ms() - in->key_t[kk] >= 500u) {
                js_step_t *st = held_step();
                if (st->flags & JS_ON) {
                    undo_push();
                    st->flags = (uint8_t)((st->flags | JS_BURN) & ~JS_DECAY);
                    ui.key_used |= ui_white_bit(ui.held_step);
                    ui.burned = 1;
                    app_say("BURNED IN", "IT NO LONGER FADES");
                }
                break;
            }
    }
    if ((d = plat_enc(EN_SELECT)) != 0 && l == L_ENERGY) { /* (0.6) ENERGY on SELECT too */
        ui_use(in, B_HOME);
        ab_energy_set(&eng, eng.en.knob + 0.02f * (float)d);
        dirty = 1;
        ui.redraw = 1;
        d = 0;
    }
    if (d != 0) {
        if (sel) { ui_use(in, B_SEL); ab_cmd_bpm(&eng, jo_clampf(eng.clock.bpm + 0.5f * (float)d, 60.0f, 220.0f)); }
        else if (ui.page == PG_SYSTEM) eng.duck_depth = jo_clampf(eng.duck_depth + 0.02f * (float)d, 0.0f, 1.0f);
        else {
            ui.track = (uint8_t)jo_clampi(ui.track + (d > 0 ? 1 : -1), 0, AB_NTRACKS - 1);
            if (cur_track()->len <= 16u) ui.step_page = 0;
        }
        dirty = 1;
        ui.redraw = 1;
    }
    if ((d = plat_enc(EN_ALGO)) != 0) {
        if (ui.held_step >= 0) { /* the held step's trig condition */
            js_step_t *st = held_step();
            if (!ui.step_pushed) undo_push(), ui.step_pushed = 1;
            st->cond = (uint8_t)jo_clampi(st->cond + (d > 0 ? 1 : -1), 0, JS_NCOND - 1);
            st->flags |= JS_ON;
            ui.key_used |= ui_white_bit(ui.held_step);
        } else if (l == L_VARY) { /* the variation's LINE */
            ui_use(in, B_LFO);
            if (ui.vary > 0) {
                ab_resp_t *r = &eng.cr[(ui.vary - 1u) % (AB_NVAR - 1u)];
                r->line = (int8_t)jo_clampi(r->line + (d > 0 ? 1 : -1), -1, AB_NPATTERNS - 1);
            }
        } else if (ui.page == PG_SONG) {
            ab_set_form(&eng, jo_clampi(eng.form + (d > 0 ? 1 : -1), 0, AB_NFORMS - 1));
            app_say("FORM", ab_form_name[eng.form]);
        } else if (ui.page == PG_SYSTEM) eng.duck_internal = d > 0;
        else if (ui.page == PG_SOUND && ui.track == AB_T_BASS) { /* (0.7) every sound by name */
            undo_push();
            ui.snd = (uint8_t)a_sound(); /* from A's sound */
            ui.snd = (uint8_t)((ui.snd + (d > 0 ? 1u : eng.fm.nnodes - 1u)) % eng.fm.nnodes);
            {
                uint32_t i;
                eng.mutated = 0;
                eng.explore_node = -1;
                for (i = 0; i < eng.fm.npath; i++)
                    if (eng.fm.path[i] == ui.snd) break;
                if (i < eng.fm.npath) eng.morph = jg_path_t(&eng.fm, i);
                else eng.explore_node = (int8_t)ui.snd;
            }
            app_say("SOUND", eng.fm.node[ui.snd].name);
        } else if (ui.page == PG_SOUND && ui.track == AB_T_STAB)
            eng.stab_p.model = (uint8_t)((eng.stab_p.model + (d > 0 ? 1u : JN_STAB_NMODELS - 1u)) % JN_STAB_NMODELS);
        else if (ui.page == PG_SOUND) eng.chord = (uint8_t)jo_clampi(eng.chord + (d > 0 ? 1 : -1), 0, JH_NCHORDS - 1);
        else { /* the library: the next / previous starting point for the track */
            undo_push();
            if (ui.track == AB_T_BASS) {
                ui.lib_line = ui.lib_line >= AB_NLINES ? (uint8_t)(d > 0 ? 0u : AB_NLINES - 1u) /* the first: from the top */
                                                       : (uint8_t)((ui.lib_line + (d > 0 ? 1u : AB_NLINES - 1u)) % AB_NLINES);
                ab_load_line(&eng, ui.lib_line);
            } else if (ui.track == AB_T_STAB) {
                ui.lib_stab = (uint8_t)((ui.lib_stab + (d > 0 ? 1u : AB_NRHYTHMS - 1u)) % AB_NRHYTHMS);
                ab_load_rhythm(&eng, AB_T_STAB, ui.lib_stab);
            } else {
                ui.lib_pad = (uint8_t)((ui.lib_pad + (d > 0 ? 1u : AB_NRHYTHMS - 1u)) % AB_NRHYTHMS);
                ab_load_rhythm(&eng, AB_T_PAD, ui.lib_pad);
            }
            app_say(lib_name(), "SEL+LFO: UNDO");
            if (ui.track == AB_T_BASS) ui.hint_until = plat_ms() + 2200u; /* (0.7) the style's how-to */
        }
        dirty = 1;
        ui.redraw = 1;
    }
    if ((d = plat_enc(EN_PRESET)) != 0) {
        if (ui.held_step >= 0) { /* the held step's octave */
            js_step_t *st = held_step();
            int cur = js_step_lock(st, AB_LK_OCT);
            if (!ui.step_pushed) undo_push(), ui.step_pushed = 1;
            ui.key_used |= ui_white_bit(ui.held_step);
            js_step_set_lock(st, AB_LK_OCT, jo_clampi((cur < 0 ? 2 : cur) + (d > 0 ? 1 : -1), 0, 4));
        } else if (ui.page == PG_SYSTEM) {
            set.link_usb_host = d > 0;
            plat_usb_host(set.link_usb_host);
            app_say("USB", set.link_usb_host ? "HOST: LOOKING" : "DEVICE");
        } else if (ui.page == PG_SOUND && sel) {
            ui_use(in, B_SEL);
            eng.scale = (uint8_t)jo_clampi(eng.scale + (d > 0 ? 1 : -1), 0, JH_NSCALES - 1);
            app_say("SCALE", jh_scale_name[eng.scale]);
        } else if (ui.page == PG_SOUND) eng.root = (int8_t)jo_clampi(eng.root + (d > 0 ? 1 : -1), 28, 52);
        else if (ui.page == PG_FX) { eng.master.m.trim_db = jo_clampf(eng.master.m.trim_db + 0.5f * (float)d, -12.0f, 6.0f); jf_neuro_update(&eng.master); }
        else pattern_queue((eng.next_pat + AB_NPATTERNS + (d > 0 ? 1 : -1)) % AB_NPATTERNS);
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
    int l = layer();
    if (ui.msg_until && plat_ms() < ui.msg_until) { ui_header(ui.msg[0], ui.msg[1], 0, 0); return; }
    ui_fix1(mid, eng.clock.bpm);
    ui_cat(mid, eng.clock.source == JS_CLK_MIDI ? " LINK" : "");
    ui_cat(mid, eng.clock.running ? " >" : " #");
    ui_cat(right, PAT_NAME[eng.cur_pat % AB_NPATTERNS]);
    ui_cat(right, " ");
    ui_cat(right, VAR_NAME[eng.cr_play % AB_NVAR]);
    ui_cat(right, "  BAR ");
    ui_int(t, eng.clock.running ? (int32_t)((eng.clock.abs_step >> 4) % eng.phrase) + 1 : 1);
    ui_cat(right, t);
    ui_cat(right, "/");
    ui_int(t, eng.phrase);
    ui_cat(right, t);
    ui_header2(l ? LAYER_NAME[l] : ui.rec ? "REC" : PAGE_NAME[ui.page], mid, right,
               ui.rec || plat_cpu_pct() > 85u || plat_xruns(), pending_frac());
    /* (0.6) ENERGY: a thin bar under the right text */
    lcd_fill(176, 19, 60, 2, C_LINE);
    lcd_fill(176, 19, (int32_t)(60.0f * jo_clampf(eng.en.value, 0.0f, 1.0f) + 0.5f), 2, eng.en.playing ? C_WHITE : C_HI);
}

/* the step view of track t (16 at the step page): on, velocity, marks */
static void track_view(uint32_t t, uint8_t on[16], uint8_t vel[16], uint8_t mark[16], int *play)
{
    const js_track_t *tr = &eng.pat[eng.cur_pat].t[t];
    uint32_t s, base = t == ui.track ? ui.step_page * 16u : 0u, len = tr->len ? tr->len : 1u;
    for (s = 0; s < 16u; s++) {
        const js_step_t *st = &tr->s[(base + s) % JS_MAXSTEPS];
        int in = base + s < tr->len;
        on[s] = !in || !(st->flags & JS_ON) ? 0 : st->flags & JS_ACCENT ? 2 : 1;
        vel[s] = st->vel;
        mark[s] = (uint8_t)((st->lk_id[0] != JS_LOCK_NONE || st->lk_id[1] != JS_LOCK_NONE) | (st->cond ? 2 : 0) |
                            (st->prob < 100u ? 4 : 0) | (st->flags & JS_SLIDE ? 8 : 0));
    }
    *play = -1;
    if (eng.clock.running) {
        uint32_t p = (eng.seq.pos[t] + len - 1u) % len;
        if (p >= base && p < base + 16u) *play = (int)(p - base);
    }
}

/* the held step's popup */
static void draw_step_popup(int32_t y)
{
    const js_step_t *st = held_step();
    char t[48], v[12];
    int oct = js_step_lock(st, AB_LK_OCT), m = js_step_lock(st, AB_LK_MORPH), bi = js_step_lock(st, AB_LK_BITE);
    char title[32] = "STEP ";
    ui_int(v, (int32_t)(ui.step_page * 16u + (uint32_t)ui.held_step) + 1);
    ui_cat(title, v);
    ui_cat(title, "  COND ");
    ui_cat(title, js_cond_name[st->cond % JS_NCOND]);
    t[0] = 0;
    if (ui.track == AB_T_BASS) { note_name(v, (int)st->data - 64 + eng.root + (oct >= 0 ? 12 * (oct - 2) : 0)); ui_cat(t, v); }
    else { ui_cat(t, "DEG "); ui_int(v, st->data + 1); ui_cat(t, v); }
    ui_cat(t, "  VEL "); ui_int(v, st->vel); ui_cat(t, v);
    if (oct >= 0 && oct != 2) { ui_cat(t, "  OCT "); ui_int(v, oct - 2); ui_cat(t, v); }
    if (m >= 0) { ui_cat(t, "  CHR "); ui_int(v, m * 100 / 255); ui_cat(t, v); }
    if (bi >= 0) { ui_cat(t, "  BITE "); ui_int(v, bi * 100 / 255); ui_cat(t, v); }
    if (st->flags & JS_SLIDE) ui_cat(t, "  SLIDE");
    if (st->flags & JS_ACCENT) ui_cat(t, "  ACC");
    ui_popup(y, title, t);
}

/* the held layers' pages: what each key does now */
static void draw_layer(int l)
{
    char t[40], v[12];
    uint32_t i;
    static const char *const HINT[NLAYERS] = {"", "", "WHITE 1-8: STORE", "HOLD A KEY", "SCENE PATTERN MUTE", "WHITE 1-4: NOW",
                                              "WHITE 1-4: A-D  9-14: FORM", "CLEAR STEP / TRACK", "WHITE: STEP LEVEL",
                                              "YOU AND THE MACHINE"};
    ui_banner(LAYER_NAME[l], HINT[l]);
    switch (l) {
    case L_FX:
        for (i = 0; i < AB_NFX - 1u; i++) {
            int32_t x = 6 + (int32_t)(i / 4u) * 116, y = 24 + (int32_t)(i % 4u) * 22;
            int on = eng.perf == i + 1u;
            t[0] = 0; ui_int(v, (int32_t)i + 1); ui_cat(t, v); ui_cat(t, " "); ui_cat(t, ab_fx_name[i + 1u]);
            if (on) cv_rect(x - 2, y - 1, 112, 19, C_HI);
            ui_fit(x, y, 106, &FONT_S, t, on ? C_BLACK : C_WHITE);
        }
        break;
    case L_SONG:
    case L_STORE:
        for (i = 0; i < AB_NSCENES; i++) {
            int32_t x = 4 + (int32_t)(i % 4u) * 59, y = 22 + (int32_t)(i / 4u) * 30;
            int cur = (int)i == eng.scene_cur, pend = (int)i == eng.scene_pending;
            cv_rect(x, y, 55, 27, cur ? C_HI : pend ? C_DIM : C_LINE);
            ui_fit(x + 3, y + 3, 50, &FONT_XS, SCENE_NAME[i], cur ? C_BLACK : C_WHITE);
            ui_fit(x + 3, y + 15, 50, &FONT_T, eng.scene[i].used ? "" : "EMPTY", C_GRAY);
        }
        if (l == L_SONG) {
            for (i = 0; i < AB_NPATTERNS; i++) {
                int32_t x = 4 + (int32_t)i * 30;
                int cur = (int)i == eng.cur_pat, nx = (int)i == eng.next_pat && !cur;
                cv_rect(x, 86, 27, 16, cur ? C_HI : nx ? C_DIM : C_LINE);
                ui_fit(x + 5, 88, 20, &FONT_XS, PAT_NAME[i], cur ? C_BLACK : C_WHITE);
            }
            t[0] = 0;
            for (i = 0; i < AB_NTRACKS; i++) { ui_cat(t, (eng.seq.mute >> i) & 1u ? "-" : "+"); ui_cat(t, ab_track_name[i]); ui_cat(t, " "); }
            ui_fit(128, 89, 234 - 128, &FONT_T, t, C_GRAY);
        }
        break;
    case L_FILL:
        for (i = 0; i < AB_NFILLS; i++) {
            int32_t y = 24 + (int32_t)i * 20;
            int on = set.fill_type == i;
            t[0] = 0; ui_int(v, (int32_t)i + 1); ui_cat(t, v); ui_cat(t, " "); ui_cat(t, ab_fill_name[i]);
            if (on) cv_rect(4, y - 1, 140, 18, C_HI);
            ui_fit(8, y, 134, &FONT_S, t, on ? C_BLACK : C_WHITE);
        }
        t[0] = 0; ui_cat(t, set.fill_bars == 2 ? "2 BARS" : "1 BAR");
        ui_fit(156, 26, 234 - 156, &FONT_B, t, C_HI);
        ui_fit(156, 48, 234 - 156, &FONT_T, eng.fill_on ? "FILL QUEUED" : "", C_AMB);
        ui_fit(156, 62, 234 - 156, &FONT_T, "TAP ENV:", C_GRAY);
        ui_fit(156, 74, 234 - 156, &FONT_T, "PHRASE END", C_GRAY);
        break;
    case L_VARY:
        for (i = 0; i < AB_NVAR; i++) {
            int32_t y = 22 + (int32_t)i * 17;
            int playing = eng.cr_play == i, picked = ui.vary == i;
            cv_rect(4, y, 18, 15, playing ? C_HI : C_LINE);
            ui_fit(9, y + 2, 12, &FONT_XS, VAR_NAME[i], playing ? C_BLACK : C_WHITE);
            t[0] = 0;
            if (i == 0) { ui_cat(t, "THE PATTERN, "); ui_cat(t, ab_node_name(&eng)); }
            else {
                const ab_resp_t *r = &eng.cr[i - 1u];
                ui_cat(t, "LINE "); var_line_name(v, r->line); ui_cat(t, v);
                ui_cat(t, "  SOUND "); ui_cat(t, var_node_name(r->node));
            }
            ui_fit(28, y + 3, 132, &FONT_T, t, picked ? C_WHITE : C_GRAY);
        }
        for (i = 0; i < AB_NFORMS; i++) {
            int32_t y = 22 + (int32_t)i * 15;
            int on = eng.form == i;
            if (on) cv_rect(164, y - 1, 74, 14, C_HI);
            ui_fit(168, y, 68, &FONT_T, ab_form_name[i], on ? C_BLACK : C_GRAY);
        }
        break;
    case L_ENERGY: { /* (0.6) the ENERGY, its curve, the selected track's levels */
        const js_track_t *tr = cur_track();
        uint32_t lvl = je_level(eng.en.value), beats = eng.en.beats;
        t[0] = 0; ui_int(v, (int32_t)(eng.en.value * 100.0f + 0.5f)); ui_cat(t, v); ui_cat(t, "%");
        ui_fit(6, 21, 80, &FONT_M, t, C_HI);
        t[0] = 0; ui_cat(t, "LEVEL "); ui_int(v, (int32_t)lvl); ui_cat(t, v); ui_cat(t, " OF 4  CURVE ");
        ui_cat(t, ab_curve_name[ab_energy_curve_mode(&eng)]);
        ui_fit(88, 24, 234 - 88, &FONT_XS, t, C_WHITE);
        ui_fit(88, 38, 234 - 88, &FONT_T, eng.en_armed ? "REC STARTS AT THE PHRASE START" : eng.en.recording ? "RECORDING THE KNOB" : "K1 ENERGY K2 CURVE K3 AUTO", C_GRAY);
        /* the curve, one bar a beat */
        cv_rect(4, 50, 232, 18, C_LINE);
        for (i = 0; i < beats && i < JE_BEATS; i++) {
            int32_t x = 4 + (int32_t)(i * 232u / beats), w = (int32_t)(232u / beats) - 1, h = (int32_t)(eng.en.lane[i] * 16u / 255u);
            cv_rect(x, 67 - h, w < 1 ? 1 : w, h, C_AMB);
        }
        if (eng.clock.running && beats) {
            uint32_t b = (eng.clock.abs_step >> 2) % beats;
            cv_rect(4 + (int32_t)(b * 232u / beats), 50, 2, 18, C_WHITE);
        }
        if (!beats) ui_fit(10, 54, 220, &FONT_T, "NO CURVE: K2 REC RECORDS ONE PHRASE", C_DIM);
        /* the steps' levels: the box's height, dim above the ENERGY's level */
        for (i = 0; i < 16u; i++) {
            const js_step_t *st = &tr->s[(ui.step_page * 16u + i) % JS_MAXSTEPS];
            int32_t x = 4 + (int32_t)i * 15, lv = st->lvl ? st->lvl : 1, on = (st->flags & JS_ON) && ui.step_page * 16u + i < tr->len;
            cv_rect(x, 74, 13, 24, C_LINE);
            if (on) cv_rect(x + 1, 98 - lv * 6, 11, lv * 6, (uint32_t)lv <= lvl ? C_HI : C_DIM);
        }
        t[0] = 0; ui_cat(t, ab_track_name[ui.track]); ui_cat(t, ": TAP A STEP FOR ITS LEVEL 1-4");
        ui_fit(6, 101, 228, &FONT_T, t, C_GRAY);
        break;
    }
    case L_DUET: { /* (0.6) ANSWER ME, POCKET, GESTURE ECHO */
        uint16_t km = ab_pocket_mask(&eng, eng.clock.abs_step >> 4);
        int link = jl_alive(&eng.link, eng.ms);
        t[0] = 0; ui_cat(t, "ANSWER ME "); ui_cat(t, eng.duet_answer ? "ON  " : "OFF  "); ui_cat(t, ab_answer_name[eng.duet_xform % AB_NANS]);
        ui_fit(6, 22, 228, &FONT_XS, t, eng.duet_answer ? C_HI : C_GRAY);
        ui_fit(6, 34, 228, &FONT_T, !eng.duet_answer ? "K1 ON: THE KEYS PLAY, THE MACHINE ANSWERS" :
                                  eng.ans_valid ? "THE MACHINE ANSWERS (IN C'S SOUND)" : eng.cap_n ? "LISTENING TO YOUR BAR" : "PLAY A BAR", C_WHITE);
        t[0] = 0; ui_cat(t, "POCKET ");
        if (eng.pocket_amt <= 0.0f) ui_cat(t, "OFF");
        else { ui_int(v, (int32_t)(eng.pocket_amt * 100.0f + 0.5f)); ui_cat(t, v); ui_cat(t, "%"); }
        if (!link) ui_cat(t, "  NO LINK");
        else { ui_cat(t, "  DRUMS "); ui_int(v, (int32_t)(eng.drum_density * 100.0f + 0.5f)); ui_cat(t, v); ui_cat(t, "%"); }
        ui_fit(6, 48, 228, &FONT_XS, t, eng.pocket_amt > 0.0f ? (link ? C_HI : UI_RED) : C_GRAY);
        for (i = 0; i < 16u; i++) { /* the kicks POCKET heard in the last bar */
            int32_t x = 4 + (int32_t)i * 15;
            cv_rect(x, 62, 13, 9, (km >> i) & 1u ? C_WHITE : C_LINE);
        }
        ui_fit(6, 73, 228, &FONT_T, "LAST BAR'S KICKS: THE BASS GOES AROUND THEM", C_DIM);
        t[0] = 0; ui_cat(t, "GESTURE ECHO "); ui_cat(t, eng.gesture ? "ON" : "OFF");
        if (eng.gesture) { ui_cat(t, "  "); ui_int(v, eng.g_nrec); ui_cat(t, v); ui_cat(t, " MOVES"); if (eng.perf_echo) { ui_cat(t, "  "); ui_cat(t, ab_fx_name[eng.perf_echo % AB_NFX]); } }
        ui_fit(6, 86, 228, &FONT_XS, t, eng.gesture ? C_HI : C_GRAY);
        ui_fit(6, 100, 228, &FONT_T, "FX AND KNOBS COME BACK NEXT PHRASE, HALF DEEP", C_DIM);
        break;
    }
    case L_ERASE:
        ui_fit(6, 30, 228, &FONT_S, "WHITE KEY: CLEAR THAT STEP", C_WHITE);
        ui_fit(6, 50, 228, &FONT_S, "BLACK 1-3: CLEAR BASS STAB PAD", C_WHITE);
        ui_fit(6, 74, 228, &FONT_XS, "SEL + LFO UNDOES IT", C_GRAY);
        break;
    default: /* SEL */
        ui_fit(6, 26, 228, &FONT_XS, "SEL + LFO: UNDO   SEL + ENV: REDO", C_WHITE);
        ui_fit(6, 42, 228, &FONT_XS, "SEL + PLAY: PANIC  SEL + REC: RETURN", C_WHITE);
        ui_fit(6, 58, 228, &FONT_XS, "SEL + SELECT: TEMPO  SEL + KNOB: FINE", C_WHITE);
        t[0] = 0; ui_cat(t, "UNDO "); ui_int(v, undo.cur); ui_cat(t, v); ui_cat(t, "  REDO "); ui_int(v, undo.top - undo.cur); ui_cat(t, v);
        ui_fit(6, 80, 228, &FONT_XS, t, C_GRAY);
        break;
    }
}

static void draw_main(void)
{
    uint8_t on[16], vel[16], mark[16];
    int play;
    uint32_t i, s;
    char t[40], v[12];
    const js_track_t *tr = cur_track();
    ui_fit(4, 1, 70, &FONT_B, ab_track_name[ui.track], C_HI);
    t[0] = 0;
    if (ui.track == AB_T_BASS) {
        ui_cat(t, eng.explore_node >= 0 ? eng.fm.node[eng.explore_node].name : ab_node_name(&eng));
        if (eng.mutated) ui_cat(t, " MUT");
    } else if (ui.track == AB_T_STAB) ui_cat(t, jn_stab_name[eng.stab_p.model]);
    else { ui_cat(t, eng.pad_on ? "HOLD " : ""); ui_cat(t, jh_chord_name[eng.chord]); }
    if (eng.form) { ui_cat(t, "  "); ui_cat(t, ab_form_name[eng.form]); }
    if (eng.perf) { ui_cat(t, "  "); ui_cat(t, ab_fx_name[eng.perf]); }
    if (tr->len > 16u) { ui_cat(t, ui.step_page ? "  17-32" : "  1-16"); }
    ui_fit(76, 5, 234 - 76, &FONT_T, t, C_GRAY);
    track_view(ui.track, on, vel, mark, &play);
    ui_grid(19, on, vel, mark, play, ui.held_step);
    for (s = 0; s < 16u; s++) { /* (0.6) TAPE DECAY: burned steps get a solid corner, fading ones a thin one */
        const js_step_t *st = &tr->s[(ui.step_page * 16u + s) % JS_MAXSTEPS];
        int32_t x = 4 + (int32_t)(s & 7u) * 29, y = 19 + (int32_t)(s >> 3) * 26;
        if (!on[s]) continue;
        if (st->flags & JS_BURN) cv_rect(x + 21, y + 15, 4, 4, C_WHITE);
        else if (st->flags & JS_DECAY) cv_rect(x + 21, y + 18, 4, 1, C_GRAY);
    }
    if (ui.track == AB_T_BASS) /* the notes over the boxes */
        for (s = 0; s < 16u; s++)
            if (on[s]) {
                const js_step_t *st = &tr->s[(ui.step_page * 16u + s) % JS_MAXSTEPS];
                int32_t x = 4 + (int32_t)(s & 7u) * 29, y = 19 + (int32_t)(s >> 3) * 26;
                note_name(v, (int)st->data - 64 + eng.root);
                cv_rect(x + 2, y + 6, 23, 8, C_BLACK);
                ui_fit(x + 3, y + 6, 22, &FONT_T, v, C_WHITE);
            }
    /* every track as a mini row */
    for (i = 0; i < AB_NTRACKS; i++) {
        uint8_t o2[16], v2[16], m2[16];
        int p2;
        track_view(i, o2, v2, m2, &p2);
        ui_steps(77 + (int32_t)i * 13, 6, o2, p2, -1);
        if ((eng.seq.mute >> i) & 1u) cv_rect(2, 80 + (int32_t)i * 13, 238, 1, UI_RED);
    }
    if (ui.held_step >= 0) draw_step_popup(76);
    else if (ui.rec) ui_popup(76, eng.clock.running ? "RECORD: THE KEYS PLAY AND WRITE" : "RECORD: PRESS PLAY",
                              ab_track_name[ui.track]);
}

static void draw_seq(void)
{
    uint32_t i;
    char t[40], v[12];
    for (i = 0; i < AB_NTRACKS; i++) {
        uint8_t on[16], vel[16], mark[16];
        int play;
        const js_track_t *tr = &eng.pat[eng.cur_pat].t[i];
        static const char *const DIV[3] = {"1/16", "1/32", "1/8"};
        track_view(i, on, vel, mark, &play);
        t[0] = 0;
        ui_cat(t, ab_track_name[i]);
        ui_cat(t, "  LEN "); ui_int(v, tr->len); ui_cat(t, v);
        ui_cat(t, " "); ui_cat(t, DIV[tr->div % 3u]);
        ui_fit(4, (int32_t)i * 31, 150, &FONT_XS, t, i == ui.track ? C_WHITE : C_GRAY);
        if ((eng.seq.mute >> i) & 1u) ui_fit(160, (int32_t)i * 31, 234 - 160, &FONT_XS, "MUTED", UI_RED);
        ui_steps(13 + (int32_t)i * 31, 11, on, play, i == ui.track ? ui.held_step : -1);
    }
    t[0] = 0; ui_cat(t, PAT_NAME[eng.cur_pat % AB_NPATTERNS]); ui_cat(t, "  SWING "); ui_int(v, eng.pat[eng.cur_pat].swing); ui_cat(t, v); ui_cat(t, "%");
    if (eng.next_pat != eng.cur_pat) { ui_cat(t, "  > "); ui_cat(t, PAT_NAME[eng.next_pat % AB_NPATTERNS]); }
    ui_fit(4, 98, 230, &FONT_XS, t, C_GRAY);
    if (ui.held_step >= 0) draw_step_popup(60);
}

static void draw_sound(void)
{
    char t[40];
    uint32_t i;
    ui_fit(6, 2, 80, &FONT_M, ab_track_name[ui.track], C_HI);
    if (ui.track == AB_T_STAB) ui_fit(90, 8, 234 - 90, &FONT_B, jn_stab_name[eng.stab_p.model], C_AMB);
    else if (ui.track == AB_T_BASS) ui_fit(90, 8, 234 - 90, &FONT_B, eng.fm.node[a_sound()].name, C_AMB);
    t[0] = 0;
    if (ui.track == AB_T_BASS) { ui_cat(t, jn_model_name[jo_clampi((int32_t)eng.fv.phys[JN_P_MODEL], 0, JN_NMODELS - 1)]); ui_cat(t, "  "); }
    ui_cat(t, "KEY "); ui_cat(t, NOTE_NAME[(eng.root % 12 + 12) % 12]); ui_cat(t, " "); ui_cat(t, jh_scale_name[eng.scale]);
    if (ui.track != AB_T_BASS) { ui_cat(t, "  CHORD "); ui_cat(t, jh_chord_name[eng.chord]); }
    ui_fit(6, 30, 234 - 6, &FONT_S, t, C_GRAY);
    if (ui.track == AB_T_BASS)
        for (i = 0; i < JN_NPARAMS; i++) { /* the patch as the graph gives it now */
            int32_t x = 6 + (int32_t)(i % 3u) * 78, y = 48 + (int32_t)(i / 3u) * 11;
            ui_fit(x, y, 40, &FONT_XS, jn_param_name[i], C_DIM);
            ui_meter(x + 42, y + 5, 30, eng.fv.norm[i], C_AMB);
        }
    else ui_fit(6, 60, 228, &FONT_XS, "THE KEYS PLAY IT (NOT RECORDED)", C_GRAY);
}

static void draw_fx(void)
{
    char t[40], v[12];
    ui_fit(6, 2, 234 - 6, &FONT_M, "NEURO PRESSURE", C_HI);
    ui_fit(6, 40, 234 - 6, &FONT_S, "PEAK", C_GRAY);
    ui_meter(70, 44, 160, eng.master.peak, C_HI);
    eng.master.peak *= 0.6f;
    ui_fit(6, 58, 234 - 6, &FONT_S, "GLUE GR", C_GRAY);
    ui_meter(70, 62, 160, -eng.master.comp.gr_db / 12.0f, C_AMB);
    t[0] = 0; ui_cat(t, "GUARD HITS "); ui_int(v, (int32_t)eng.master.guard.hits); ui_cat(t, v);
    ui_cat(t, "  TRIM "); ui_fix1(v, eng.master.m.trim_db); ui_cat(t, v);
    ui_fit(6, 80, 234 - 6, &FONT_S, t, C_GRAY);
    ui_fit(6, 100, 234 - 6, &FONT_XS, "HOLD FX: THE KEYS ARE PERFORMANCE FX", C_DIM);
}

static void draw_song(void)
{
    char t[48], v[12];
    uint32_t i;
    ui_fit(6, 2, 120, &FONT_B, "FORM", C_GRAY);
    ui_fit(60, 0, 174, &FONT_M, ab_form_name[eng.form], C_HI);
    for (i = 0; i < AB_CR_BARS; i++) {
        int32_t x = 6 + (int32_t)i * 26;
        int bar = eng.clock.running ? (int)((eng.clock.abs_step >> 4) % AB_CR_BARS) : -1, lit = (int)i == bar;
        cv_rect(x, 28, 23, 20, lit ? C_HI : C_LINE);
        ui_fit(x + 6, 30, 14, &FONT_B, VAR_NAME[AB_FORMS[eng.form][i]], lit ? C_BLACK : C_WHITE);
    }
    for (i = 0; i < AB_NVAR - 1u; i++) {
        t[0] = 0; ui_cat(t, VAR_NAME[i + 1u]); ui_cat(t, " ");
        var_line_name(v, eng.cr[i].line); ui_cat(t, v); ui_cat(t, " "); ui_cat(t, var_node_name(eng.cr[i].node));
        ui_fit(116, 27 + (int32_t)i * 9, 234 - 116, &FONT_T, t, C_GRAY);
    }
    t[0] = 0; ui_cat(t, "PHRASE "); ui_int(v, eng.phrase); ui_cat(t, v); ui_cat(t, " BARS  QUANT "); ui_cat(t, QUANT_NAME[set.scene_quant % 6u]);
    ui_fit(6, 56, 228, &FONT_XS, t, C_WHITE);
    t[0] = 0; ui_cat(t, "LOCK TO KICK "); ui_cat(t, eng.lock_kick ? (jl_alive(&eng.link, eng.ms) ? "ON" : "ON (NEEDS LINK)") : "OFF");
    ui_fit(6, 72, 228, &FONT_XS, t, eng.lock_kick ? C_HI : C_GRAY);
    t[0] = 0; ui_cat(t, "FILL ANSWERS "); ui_cat(t, eng.fill_answers ? "ON: A LINK FILL PLAYS C" : "OFF");
    ui_fit(6, 86, 228, &FONT_XS, t, eng.fill_answers ? C_HI : C_GRAY);
    if (ui.song_page) { /* (0.6) TAPE DECAY and DUET's link part */
        cv_rect(0, 54, 240, 46, C_BLACK);
        t[0] = 0; ui_cat(t, "TAPE DECAY "); ui_cat(t, eng.decay_on ? "ON  AMOUNT " : "OFF");
        if (eng.decay_on) { ui_int(v, (int32_t)(eng.decay_amt * 100.0f + 0.5f)); ui_cat(t, v); ui_cat(t, "%"); }
        ui_fit(6, 56, 228, &FONT_XS, t, eng.decay_on ? C_HI : C_GRAY);
        ui_fit(6, 70, 228, &FONT_T, "REC'S NOTES FADE EACH BAR; HOLD A STEP TO BURN IT IN", C_DIM);
        t[0] = 0; ui_cat(t, "POCKET ");
        if (eng.pocket_amt <= 0.0f) ui_cat(t, "OFF");
        else { ui_int(v, (int32_t)(eng.pocket_amt * 100.0f + 0.5f)); ui_cat(t, v); ui_cat(t, "%"); if (!jl_alive(&eng.link, eng.ms)) ui_cat(t, " (NO LINK)"); }
        ui_cat(t, "  ECHO "); ui_cat(t, eng.gesture ? "ON" : "OFF");
        ui_fit(6, 84, 228, &FONT_XS, t, C_GRAY);
    }
    t[0] = 0; ui_cat(t, "SCENE "); ui_cat(t, eng.scene_cur >= 0 ? SCENE_NAME[eng.scene_cur] : "-");
    if (eng.scene_pending >= 0) { ui_cat(t, " > "); ui_cat(t, SCENE_NAME[eng.scene_pending]); }
    ui_cat(t, jl_alive(&eng.link, eng.ms) ? "  LINK" : "  SOLO");
    ui_cat(t, ui.song_page ? "  OCT-: PAGE 1" : "  OCT+: PAGE 2");
    ui_fit(6, 100, 228, &FONT_XS, t, C_GRAY);
}

static void draw_system(void)
{
    char t[40], v[12];
    uint32_t i;
    t[0] = 0; ui_cat(t, "CPU "); ui_int(v, (int32_t)plat_cpu_pct()); ui_cat(t, v); ui_cat(t, "%  XRUNS "); ui_int(v, (int32_t)plat_xruns()); ui_cat(t, v);
    ui_fit(6, 2, 234 - 6, &FONT_S, t, C_HI);
    t[0] = 0; ui_cat(t, "LINK CH "); ui_int(v, eng.link.channel + 1); ui_cat(t, v);
    ui_cat(t, jl_alive(&eng.link, eng.ms) ? "  PEER OK" : "  NO PEER");
    ui_cat(t, "  KICKS "); ui_int(v, (int32_t)eng.link_kicks); ui_cat(t, v);
    ui_fit(6, 18, 234 - 6, &FONT_S, t, C_GRAY);
    t[0] = 0; ui_cat(t, "STORE "); ui_cat(t, load_report ? "DEFAULTS USED" : "OK"); ui_cat(t, dirty ? "  UNSAVED" : "");
    ui_fit(6, 34, 234 - 6, &FONT_S, t, load_report ? C_WHITE : C_GRAY);
    t[0] = 0; ui_cat(t, "DUCK "); ui_pct(v, eng.duck_depth); ui_cat(t, v); ui_cat(t, "  SOLO DUCK "); ui_cat(t, eng.duck_internal ? "ON" : "OFF");
    ui_fit(6, 50, 234 - 6, &FONT_S, t, C_GRAY);
    {
        static const char *const UH[3] = {"DEVICE", "HOST: LOOKING", "HOST: CONNECTED"};
        int st = plat_usb_host_state();
        t[0] = 0; ui_cat(t, "USB "); ui_cat(t, UH[st >= 0 && st <= 2 ? st : 0]);
        ui_fit(6, 66, 234 - 6, &FONT_S, t, st == USBH_CONNECTED ? C_HI : C_GRAY);
    }
    { /* the sweetspots' state (docs/development/sweetspots.md): none is certified by listening yet */
        uint32_t un = 0;
        for (i = 0; i < eng.fm.nnodes; i++) un += eng.fm.node[i].review == JG_UNREVIEWED;
        t[0] = 0; ui_cat(t, "SWEETSPOTS "); ui_int(v, (int32_t)un); ui_cat(t, v); ui_cat(t, " UNREVIEWED  Q DROPS ");
        ui_int(v, (int32_t)eng.q.dropped); ui_cat(t, v);
        ui_fit(6, 82, 234 - 6, &FONT_S, t, C_GRAY);
    }
}

static void draw_body(void)
{
    int l = layer();
    cv_begin(240, UI_BODY_H, C_BLACK);
    if (l != L_NONE && ui.held_step < 0) draw_layer(l);
    else switch (ui.page) {
        case PG_SEQ: draw_seq(); break;
        case PG_SOUND: draw_sound(); break;
        case PG_FX: draw_fx(); break;
        case PG_SONG: draw_song(); break;
        case PG_SYSTEM: draw_system(); break;
        default: draw_main(); break;
        }
    if (ui.hint_until && plat_ms() < ui.hint_until && l == L_NONE && ui.held_step < 0) /* (0.7) a style: how it is played */
        ui_popup(76, ab_line_name[ui.lib_line % AB_NLINES], ab_style_hint[ui.lib_line % AB_NLINES]);
    cv_blit(0, UI_BODY_Y);
}

/* the strip: the encoders and the keys, on this page or in the held layer */
static void draw_strip(void)
{
    ui_kg_t g[6];
    const char *enc[3] = {0, 0, 0};
    uint32_t n = 0;
    int l = layer();
    switch (l) {
    case L_SEL:
        enc[0] = "TEMPO";
        g[n++] = (ui_kg_t)UI_W(0, 15, "LFO UNDO  ENV REDO  REC RETURN");
        break;
    case L_STORE: g[n++] = (ui_kg_t)UI_W(0, 7, "STORE SCENE 1-8"); break;
    case L_FX: g[n++] = (ui_kg_t)UI_W(0, 7, "HOLD: PERFORMANCE FX"); break;
    case L_SONG:
        g[n++] = (ui_kg_t)UI_W(0, 7, "SCENE 1-8");
        g[n++] = (ui_kg_t)UI_W(8, 11, "P1-P4");
        g[n++] = (ui_kg_t)UI_B(0, 2, "MUTE B S P");
        break;
    case L_FILL: g[n++] = (ui_kg_t)UI_W(0, 3, "FILL NOW"); break;
    case L_VARY:
        enc[1] = ui.vary ? "LINE" : 0;
        g[n++] = (ui_kg_t)UI_W(0, 3, "VAR A-D");
        g[n++] = (ui_kg_t)UI_W(8, 13, "FORM");
        break;
    case L_ENERGY:
        enc[0] = "ENERGY";
        g[n++] = (ui_kg_t)UI_W(0, 15, "TAP A STEP: ITS LEVEL 1-4");
        break;
    case L_DUET:
        if (eng.duet_answer) g[n++] = (ui_kg_t)UI_W(0, 15, "PLAY THE BASS: IT ANSWERS");
        else g[n++] = (ui_kg_t)UI_W(0, 15, "K1 ANSWER K2 HOW K3 POCKET K4 ECHO");
        break;
    case L_ERASE:
        g[n++] = (ui_kg_t)UI_W(0, 15, "CLEAR STEP");
        g[n++] = (ui_kg_t)UI_B(0, 2, "CLEAR TRACK");
        break;
    default:
        if (ui.rec || ui.page == PG_SOUND || eng.duet_answer) {
            g[n++] = (ui_kg_t)UI_W(0, 15, ui.rec && ui.page != PG_SOUND ? "PLAY + RECORD THE TRACK" : eng.duet_answer && ui.page != PG_SOUND ? "PLAY THE BASS: IT ANSWERS" : "PLAY THE TRACK");
            g[n++] = (ui_kg_t)UI_B(0, 10, "(BLACK KEYS PLAY TOO)");
        } else {
            g[n++] = (ui_kg_t)UI_W(0, 15, ui.held_step >= 0 ? "HOLD + KNOBS EDIT THE STEP" : "STEPS: TAP ON/OFF, HOLD TO EDIT");
            g[n++] = (ui_kg_t)UI_B(0, 2, "TRACK");
            if (ui.held_step >= 0) g[n++] = (ui_kg_t)UI_B(3, 4, "ACC/SLIDE");
        }
        if (ui.held_step >= 0) { enc[1] = "CONDITION"; enc[2] = "OCTAVE"; }
        else if (ui.page == PG_SOUND) { enc[0] = "TRACK"; enc[1] = ui.track == AB_T_STAB ? "MODEL" : ui.track == AB_T_BASS ? "SOUND" : "CHORD"; enc[2] = "ROOT"; }
        else if (ui.page == PG_SONG) { enc[0] = "TRACK"; enc[1] = "FORM"; enc[2] = "PATTERN"; }
        else if (ui.page == PG_SYSTEM) { enc[0] = "DUCK"; enc[1] = "SOLO DUCK"; enc[2] = "USB HOST"; }
        else if (ui.page == PG_FX) { enc[0] = "TRACK"; enc[1] = "LIBRARY"; enc[2] = "TRIM"; }
        else { enc[0] = "TRACK"; enc[1] = ui.track == AB_T_BASS ? "STYLE" : "LIBRARY"; enc[2] = "PATTERN"; }
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
    if (ui.held_step >= 0) {
        const js_step_t *st = held_step();
        int m = js_step_lock(st, AB_LK_MORPH), bi = js_step_lock(st, AB_LK_BITE);
        name[0] = "NOTE"; name[1] = "VEL"; name[2] = "CHAR LK"; name[3] = "BITE LK";
        v[0] = (float)st->data / 127.0f; v[1] = (float)st->vel / 127.0f; v[2] = m < 0 ? 0.0f : (float)m / 255.0f; v[3] = bi < 0 ? 0.0f : (float)bi / 255.0f;
        vt[2] = m < 0 ? "-" : 0; vt[3] = bi < 0 ? "-" : 0;
        if (ui.track == AB_T_BASS) { note_name(b[0], (int)st->data - 64 + eng.root); vt[0] = b[0]; }
        else { ui_int(b[0], st->data + 1); vt[0] = b[0]; }
    } else if (l == L_FX || (l == L_NONE && ui.page == PG_FX)) {
        name[0] = "WEIGHT"; name[1] = "DIRT"; name[2] = "WIDTH"; name[3] = "PRESSURE";
        for (k = 0; k < 4u; k++) v[k] = *bus_knob((int)k);
    } else if (l == L_ENERGY) { /* (0.6) */
        int m = ab_energy_curve_mode(&eng);
        name[0] = "ENERGY"; name[1] = "CURVE"; name[2] = "AUTO LVL";
        v[0] = eng.en.knob; v[1] = (float)m / 2.0f; vt[1] = ab_curve_name[m]; v[2] = 0.0f; vt[2] = "TURN";
    } else if (l == L_DUET) {
        name[0] = "ANSWER"; name[1] = "HOW"; name[2] = "POCKET"; name[3] = "ECHO";
        v[0] = eng.duet_answer; vt[0] = eng.duet_answer ? "ON" : "OFF";
        v[1] = (float)eng.duet_xform / (float)(AB_NANS - 1); vt[1] = ab_answer_name[eng.duet_xform % AB_NANS];
        v[2] = eng.pocket_amt; if (eng.pocket_amt <= 0.0f) vt[2] = "OFF";
        v[3] = eng.gesture; vt[3] = eng.gesture ? "ON" : "OFF";
    } else if (l == L_FILL) {
        name[0] = "LENGTH"; v[0] = set.fill_bars == 2 ? 1.0f : 0.5f; vt[0] = set.fill_bars == 2 ? "2 BAR" : "1 BAR";
    } else if (l == L_VARY) {
        const js_track_t *tr = cur_track();
        uint32_t on = 0, i;
        for (i = 0; i < tr->len; i++) on += (tr->s[i].flags & JS_ON) != 0;
        name[0] = "DENSITY"; name[1] = "HUMANIZE"; name[2] = "MUT RAD"; name[3] = ui.vary ? "SOUND" : "CHAR";
        v[0] = (float)on / (float)(tr->len ? tr->len : 16u); ui_int(b[0], (int32_t)on); vt[0] = b[0];
        v[1] = eng.humanize; v[2] = ui.mutate_radius;
        if (ui.vary) {
            int nd = eng.cr[(ui.vary - 1u) % (AB_NVAR - 1u)].node;
            v[3] = (float)(nd + 1) / (float)eng.fm.nnodes; vt[3] = var_node_name(nd);
        } else v[3] = eng.morph;
    } else switch (ui.page) {
        case PG_SEQ: {
            static const char *const DIV[3] = {"1/16", "1/32", "1/8"};
            const js_track_t *tr = cur_track();
            name[0] = "LENGTH"; name[1] = "SWING"; name[2] = "SPEED"; name[3] = "SPACE";
            v[0] = (float)tr->len / 32.0f; v[1] = ((float)eng.pat[eng.cur_pat].swing - 50.0f) / 25.0f; v[2] = (float)tr->div / 2.0f; v[3] = eng.space;
            ui_int(b[0], tr->len); vt[0] = b[0];
            ui_int(b[1], eng.pat[eng.cur_pat].swing); ui_cat(b[1], "%"); vt[1] = b[1];
            vt[2] = DIV[tr->div % 3u];
            break;
        }
        case PG_SOUND:
            if (ui.track == AB_T_BASS) {
                name[0] = "MOD SHAPE"; name[1] = "SUB SAT"; name[2] = "SUB OCT"; name[3] = "TENSION";
                v[0] = eng.patch.lfo_shape / 4.0f; v[1] = eng.sub.sat; v[2] = eng.sub.octave < 0 ? 0.0f : 1.0f; v[3] = eng.tension;
                vt[0] = SHAPE_NAME[(int)eng.patch.lfo_shape % 5]; vt[2] = eng.sub.octave < 0 ? "-1" : "0";
            } else if (ui.track == AB_T_STAB) {
                name[0] = "SPACE"; name[1] = "TENSION"; name[2] = "INVERSION";
                v[0] = eng.space; v[1] = eng.tension; v[2] = (float)eng.inversion / 2.0f;
                ui_int(b[2], eng.inversion); vt[2] = b[2];
            } else {
                name[0] = "SPACE"; name[1] = "TENSION"; name[2] = "ATTACK"; name[3] = "RELEASE";
                v[0] = eng.space; v[1] = eng.tension; v[2] = eng.pad_p.attack_s / 6.0f; v[3] = eng.pad_p.release_s / 8.0f;
                ui_fix1(b[2], eng.pad_p.attack_s); ui_cat(b[2], "S"); vt[2] = b[2];
                ui_fix1(b[3], eng.pad_p.release_s); ui_cat(b[3], "S"); vt[3] = b[3];
            }
            break;
        case PG_SONG:
            if (ui.song_page) { /* (0.6) */
                name[0] = "DECAY"; name[1] = "AMOUNT"; name[2] = "POCKET"; name[3] = "ECHO";
                v[0] = eng.decay_on; vt[0] = eng.decay_on ? "ON" : "OFF";
                v[1] = eng.decay_amt;
                v[2] = eng.pocket_amt; if (eng.pocket_amt <= 0.0f) vt[2] = "OFF";
                v[3] = eng.gesture; vt[3] = eng.gesture ? "ON" : "OFF";
                break;
            }
            name[0] = "PHRASE"; name[1] = "QUANT"; name[2] = "LOCK KICK"; name[3] = "FILL ANS";
            v[0] = eng.phrase / 16.0f; ui_int(b[0], eng.phrase); vt[0] = b[0];
            v[1] = (float)set.scene_quant / 5.0f; vt[1] = QUANT_NAME[set.scene_quant % 6u];
            v[2] = eng.lock_kick; vt[2] = eng.lock_kick ? "ON" : "OFF";
            v[3] = eng.fill_answers; vt[3] = eng.fill_answers ? "ON" : "OFF";
            break;
        case PG_SYSTEM:
            name[0] = "BPM"; name[1] = "VOICES"; name[2] = "LINK CH"; name[3] = "FOLLOW";
            v[0] = (eng.clock.bpm - 60.0f) / 160.0f; v[1] = (float)eng.voice_mode / 2.0f; v[2] = (float)eng.link.channel / 15.0f; v[3] = eng.follow_link;
            ui_fix1(b[0], eng.clock.bpm); vt[0] = b[0];
            vt[1] = VMODE_NAME[eng.voice_mode % 3u];
            ui_int(b[2], eng.link.channel + 1); vt[2] = b[2];
            vt[3] = eng.follow_link ? "LINK" : "OFF";
            break;
        default: /* HOME: the track's sound */
            if (ui.track == AB_T_BASS) { name[0] = "CHAR"; name[1] = "MOTION"; name[2] = "BITE"; name[3] = "GLIDE"; }
            else if (ui.track == AB_T_STAB) { name[0] = "CHORD"; name[1] = "ATTACK"; name[2] = "BITE"; name[3] = "TAIL"; }
            else { name[0] = "DARKNESS"; name[1] = "EVOLVE"; name[2] = "TENSION"; name[3] = "DEPTH"; }
            for (k = 0; k < 4u; k++) v[k] = *track_knob((int)k);
            break;
        }
    for (k = 0; k < 4u; k++) if (!name[k]) vt[k] = ""; /* an unused knob: an empty box */
    ui_macros(name, v, vt, ui.hi_until && plat_ms() < ui.hi_until ? ui.hi_macro : -1);
}

static void lights(void)
{
    uint32_t b = 0, keys = 0, dimk = 0, k;
    static const uint8_t PB[NPAGES] = {B_HOME, B_SEQ, B_EDIT, B_FX, B_ARP, B_GLO};
    int l = layer();
    b |= 1u << PB[ui.page];
    if (eng.clock.running) b |= 1u << B_PLAY;
    if (dirty) b |= 1u << B_SAVE;
    if (ui.rec) b |= 1u << B_REC;
    if (eng.mutated || eng.form) b |= 1u << B_LFO;
    if (eng.fill_on) b |= 1u << B_ENV;
    if (l == L_FX) { if (eng.perf) keys |= ui_white_bit((int)eng.perf - 1); for (k = 0; k < AB_NFX - 1u; k++) dimk |= ui_white_bit((int)k); }
    else if (l == L_SONG || l == L_STORE) {
        for (k = 0; k < AB_NSCENES; k++) if (eng.scene[k].used) dimk |= ui_white_bit((int)k);
        if (eng.scene_cur >= 0) keys |= ui_white_bit(eng.scene_cur);
        if (l == L_SONG) { keys |= ui_white_bit(8 + eng.cur_pat); for (k = 0; k < AB_NTRACKS; k++) if (!((eng.seq.mute >> k) & 1u)) keys |= ui_black_bit((int)k); }
    } else if (l == L_FILL) { for (k = 0; k < AB_NFILLS; k++) dimk |= ui_white_bit((int)k); keys |= ui_white_bit(set.fill_type); }
    else if (l == L_VARY) {
        for (k = 0; k < AB_NVAR; k++) dimk |= ui_white_bit((int)k);
        keys |= ui_white_bit(eng.cr_play) | ui_white_bit(8 + eng.form);
    } else if (l == L_ENERGY) { /* (0.6) the steps that play at this ENERGY lit, the others dim */
        const js_track_t *tr = cur_track();
        uint32_t lv = je_level(eng.en.value);
        for (k = 0; k < 16u; k++) {
            const js_step_t *st = &tr->s[(ui.step_page * 16u + k) % JS_MAXSTEPS];
            if (!(st->flags & JS_ON) || ui.step_page * 16u + k >= tr->len) continue;
            if ((st->lvl ? st->lvl : 1u) <= lv) keys |= ui_white_bit((int)k); else dimk |= ui_white_bit((int)k);
        }
    } else if (ui.rec || ui.page == PG_SOUND || eng.duet_answer) {
        for (k = 0; k < NKEYS; k++) if (((key_note(k) - eng.root) % 12 + 12) % 12 == 0) dimk |= 1u << k;
        keys = ui.in.keys;
    } else {
        const js_track_t *tr = cur_track();
        int play = -1;
        for (k = 0; k < 16u; k++) if (tr->s[(ui.step_page * 16u + k) % JS_MAXSTEPS].flags & JS_ON && ui.step_page * 16u + k < tr->len) keys |= ui_white_bit((int)k);
        if (eng.clock.running) { uint32_t len = tr->len ? tr->len : 1u, p = (eng.seq.pos[ui.track] + len - 1u) % len; if (p / 16u == ui.step_page) play = (int)(p % 16u); }
        if (play >= 0) { keys ^= ui_white_bit(play); dimk |= ui_white_bit(play); }
        keys |= ui_black_bit(ui.track);
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
    { /* (0.6) TAPE DECAY: once a bar, the fading steps fade */
        uint32_t f = plat_lock();
        int ran = ab_decay_tick(&eng);
        plat_unlock(f);
        if (ran) dirty = 1, ui.redraw = 1;
    }
    /* autosave: 3 s after STOP, when something changed */
    if (ui.was_running && !eng.clock.running) ui.stop_ms = now;
    ui.was_running = eng.clock.running;
    if (!eng.clock.running && dirty && ui.stop_ms && now - ui.stop_ms > 3000u) { ui.stop_ms = 0; save_all(1); }
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
