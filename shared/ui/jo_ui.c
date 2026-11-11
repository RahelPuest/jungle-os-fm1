/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* JUNGLE OS screen and input helpers, shared by both firmwares. Part of each app's unity build (included by
 * app.c after fm1-base's gfx.c): it draws with fm1-base's canvas (cv_*), fonts and palette.
 *
 * Screen: 240 x 240, four bands drawn and sent separately (only when something changed):
 *   header  y   0..25   page name, the instrument's state (tempo, clock, scene, load); a queued change's countdown
 *   body    y  26..140  the page
 *   strip   y 141..185  what the three encoders and the 27 keys do on this page, now (with SEL or SAVE held: what
 *                       they do with it): the encoders' line, the black keys' labels, the keys, the white keys' labels
 *   footer  y 186..239  the four macro boxes (K1..K4), names and values */

#define UI_HEAD_H 26
#define UI_BODY_Y 26
#define UI_BODY_H 115
#define UI_STRIP_Y 141
#define UI_STRIP_H 45
#define UI_FOOT_Y 186
#define UI_FOOT_H 54

#ifdef PLAT_HOST
#include <stdio.h>
#endif

/* ----------------------------------------------------------- themes --- */
/* (0.3) the drum & bass look: near-black neutrals, one loud hue per instrument. BREAK SURGEON: acid lime values over
 * hazard yellow; ABYSS ENGINE: abyss cyan values over neuro violet. The red is the alarm (and PANIC). */
enum { UI_THEME_ACID, UI_THEME_ABYSS };
#define UI_RED RGB(255, 40, 70)
static void ui_theme(int which)
{
    static const uint16_t T[2][5] = {
        {RGB(30, 30, 26), RGB(74, 76, 62), RGB(152, 154, 140), RGB(255, 204, 0), RGB(206, 255, 0)},
        {RGB(24, 26, 38), RGB(64, 68, 96), RGB(142, 148, 172), RGB(178, 112, 255), RGB(0, 232, 255)},
    };
    uint32_t k;
    for (k = 0; k < 5u; k++) pal[k] = T[which & 1][k];
}

/* diagonal hazard stripes in a rectangle (canvas coordinates): c on black, 4 px bands at 45 degrees */
static void ui_stripes(int32_t x0, int32_t y0, int32_t w, int32_t h, uint16_t c)
{
    int32_t x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) cv_pset(x0 + x, y0 + y, ((x0 + x + y0 + y) >> 2) & 1 ? c : C_BLACK);
}

/* ------------------------------------------------------------ text --- */
/* (0.3) text that never runs past its box: drawn in f if it fits in maxw, else in the next smaller font (L, M, B, XS,
 * S, XS, T; L, M and T have capitals only, so lowercase text skips them), centred on the line f would take; if even the
 * smallest is too wide, it is cut (the host simulator says so on stderr: tools/build_all.sh fails on it). Returns
 * the x after the text. */
static const felucca_font_t *const UI_FONTS[6] = {&FONT_L, &FONT_M, &FONT_B, &FONT_S, &FONT_XS, &FONT_T};
static int32_t ui_fit(int32_t x, int32_t y, int32_t maxw, const felucca_font_t *f, const char *s, uint16_t c)
{
    uint32_t i = 0, k, n = 0, lower = 0;
    const felucca_font_t *g = f;
    char cut[48];
    for (k = 0; s[k]; k++) if ((uint8_t)s[k] > 95u) lower = 1;
    while (i < 6u && UI_FONTS[i] != f) i++;
    for (; i < 6u; i++) {
        if (lower && UI_FONTS[i]->last < 126u) continue;
        g = UI_FONTS[i];
        if (text_w(g, s) <= maxw) return cv_text(x, y + ((int32_t)f->h - (int32_t)g->h) / 2, g, s, c);
    }
    /* cut to fit */
    for (k = 0; s[k] && k < sizeof cut - 1u; k++) {
        cut[k] = s[k];
        cut[k + 1u] = 0;
        if (text_w(g, cut) > maxw) break;
        n = k + 1u;
    }
    cut[n] = 0;
#ifdef PLAT_HOST
    fprintf(stderr, "ui: cut \"%s\" to \"%s\" (%d px)\n", s, cut, (int)maxw);
#endif
    return cv_text(x, y + ((int32_t)f->h - (int32_t)g->h) / 2, g, cut, c);
}

static void ui_cat(char *d, const char *s)
{
    while (*d) d++;
    while (*s) *d++ = *s++;
    *d = 0;
}
static void ui_int(char *b, int32_t v)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do t[n++] = (char)('0' + u % 10u); while ((u /= 10u) && n < 11);
    if (v < 0) *b++ = '-';
    while (n) *b++ = t[--n];
    *b = 0;
}
/* "174.0" */
static void ui_fix1(char *b, float v)
{
    int32_t i = (int32_t)(v * 10.0f + (v < 0 ? -0.5f : 0.5f));
    ui_int(b, i / 10);
    ui_cat(b, ".");
    {
        char d[2] = {(char)('0' + (i < 0 ? -i : i) % 10), 0};
        ui_cat(b, d);
    }
}
static void ui_pct(char *b, float v01)
{
    ui_int(b, (int32_t)(jo_clampf(v01, 0.0f, 1.0f) * 100.0f + 0.5f));
}

/* ----------------------------------------------------------- bands --- */
/* header: the page on the left (inverted), text on the right; pending 0..1: how far a queued change (a pattern, a
 * scene, a fill, RETURN) is from landing, drawn as a bar along the bottom (< 0: nothing queued) */
static void ui_header2(const char *page, const char *mid, const char *right, int alarm, float pending)
{
    int32_t w, y, xr, xm;
    cv_begin(240, UI_HEAD_H, C_BLACK);
    /* the page's tab: a slanted block (its right edge leans), the name in black */
    w = text_w(&FONT_B, page) + 10;
    if (w > 112) w = 112;
    for (y = 0; y < UI_HEAD_H - 4; y++) cv_rect(0, y, w + (UI_HEAD_H - 4 - y) / 2, 1, alarm ? UI_RED : C_HI);
    ui_fit(5, 3, w - 6, &FONT_B, page, C_BLACK);
    xm = w + UI_HEAD_H / 2 + 2;
    xr = 236 - (right ? text_w(&FONT_S, right) : 0);
    if (right && xr < xm + 60) xr = xm + 60;            /* the right text gives way first */
    if (mid) ui_fit(xm, 5, xr - xm - 4, &FONT_S, mid, C_AMB);
    if (right) ui_fit(xr, 5, 236 - xr, &FONT_S, right, alarm ? C_WHITE : C_GRAY);
    /* the bottom edge: hazard stripes, red ones on an alarm, a white countdown over them while a change waits */
    ui_stripes(0, UI_HEAD_H - 3, 240, 3, alarm ? UI_RED : C_DIM);
    if (pending >= 0.0f) cv_rect(0, UI_HEAD_H - 3, (int32_t)(240.0f * jo_clampf(pending, 0.0f, 1.0f)), 3, C_WHITE);
    cv_blit(0, 0);
}
static void ui_header(const char *page, const char *mid, const char *right, int alarm)
{
    ui_header2(page, mid, right, alarm, -1.0f);
}

/* the strip: what the encoders and the keys do. A key group is a run of white keys (0..15) or of black keys (0..10)
 * and its label; groups next to each other alternate their colour so the runs read apart */
typedef struct {
    uint8_t black, lo, hi;
    const char *label;
} ui_kg_t;
#define UI_W(lo, hi, label) {0, (lo), (hi), (label)}
#define UI_B(lo, hi, label) {1, (lo), (hi), (label)}
/* the white key each black key sits right of (F3 .. G5: F# G# A# C# D# F# G# A# C# D# F#) */
static const uint8_t UI_BLACK_AT[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};

/* a run's label, centred over it; it may reach 12 px past either end of the run (a one-key run's label is wider than
 * the key), never past the screen */
static void ui_label(int32_t x0, int32_t x1, int32_t y, const char *t, uint16_t c)
{
    int32_t room = x1 - x0 + 24, w = text_w(&FONT_T, t), x;
    if (room > 238) room = 238;
    if (w > room) w = room;
    x = (x0 + x1 - w) / 2;
    if (x < 1) x = 1;
    if (x + w > 239) x = 239 - w;
    ui_fit(x, y, w, &FONT_T, t, c);
}

/* enc: what SELECT, ALGO and PRESET do (0: nothing); layer: the held layer's name ("SEL", "FX", ...), 0: none */
static void ui_strip(const char *const enc[3], const ui_kg_t *g, uint32_t n, const char *layer)
{
    static const char *const EN[3] = {"SELECT", "ALGO", "PRESET"};
    uint16_t wc[16], bc[11];
    uint32_t i, k;
    cv_begin(240, UI_STRIP_H, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    /* the encoders */
    for (i = 0; i < 3u; i++) {
        int32_t x = 2 + (int32_t)i * 80;
        int32_t x1 = 2 + (int32_t)(i + 1u) * 80 - 4;
        x = cv_text(x, 2, &FONT_T, EN[i], C_DIM) + 3;
        ui_fit(x, 2, x1 - x, &FONT_T, enc[i] ? enc[i] : "-", enc[i] ? (layer ? C_WHITE : C_AMB) : C_DIM);
    }
    if (layer) { /* the layer's name, so it is clear why the strip changed */
        char t[16] = "";
        int32_t w;
        ui_cat(t, layer);
        ui_cat(t, " HELD");
        w = text_w(&FONT_T, t) + 6;
        cv_rect(238 - w, 13, w, 11, C_HI);
        cv_text(241 - w, 13, &FONT_T, t, C_BLACK);
    }
    /* the keys' colours: a run's, alternating; none: dark */
    for (i = 0; i < 16u; i++) wc[i] = C_LINE;
    for (i = 0; i < 11u; i++) bc[i] = C_BLACK;
    {
        uint32_t nw = 0, nb = 0;
        for (k = 0; k < n; k++) {
            uint16_t c = (g[k].black ? nb++ : nw++) & 1u ? C_GRAY : C_AMB;
            for (i = g[k].lo; i <= g[k].hi && i < (g[k].black ? 11u : 16u); i++) {
                if (g[k].black) bc[i] = c;
                else wc[i] = c;
            }
        }
    }
    /* the keys: whites 15 px apart at y 25..33, blacks over their gaps at y 25..30 */
    for (i = 0; i < 16u; i++) cv_rect((int32_t)i * 15 + 1, 25, 13, 9, wc[i]);
    for (i = 0; i < 11u; i++) {
        int32_t x = (int32_t)(UI_BLACK_AT[i] + 1u) * 15 - 5;
        cv_rect(x - 1, 24, 11, 8, C_BLACK);
        cv_rect(x, 25, 9, 6, bc[i] == C_BLACK ? C_LINE : bc[i]);
    }
    /* the labels: the black runs' above the keys, the white runs' below */
    {
        uint32_t nw = 0, nb = 0;
        for (k = 0; k < n; k++) {
            uint16_t c = (g[k].black ? nb++ : nw++) & 1u ? C_GRAY : C_AMB;
            if (!g[k].label) continue;
            if (g[k].black)
                ui_label((int32_t)(UI_BLACK_AT[g[k].lo] + 1u) * 15 - 6, (int32_t)(UI_BLACK_AT[g[k].hi] + 1u) * 15 + 6, 13,
                         g[k].label, c);
            else
                ui_label((int32_t)g[k].lo * 15, (int32_t)(g[k].hi + 1u) * 15, 35, g[k].label, c);
        }
    }
    cv_blit(0, UI_STRIP_Y);
}

/* the four macro boxes: names, a value bar, the value; hi = the box being turned now */
static void ui_macros(const char *const name[4], const float v[4], const char *const vtext[4], int hi)
{
    uint32_t i;
    cv_begin(240, UI_FOOT_H, C_BLACK);
    for (i = 0; i < 4u; i++) {
        int32_t x = 2 + (int32_t)i * 60, w = 56, bar;
        int lit = (int)i == hi;
        char t[16];
        int32_t k;
        cv_rect(x, 0, w, UI_FOOT_H - 3, lit ? C_LINE : C_BLACK);
        cv_rect(x, 0, w, 2, lit ? C_WHITE : C_DIM);           /* the top edge, lit while turned */
        for (k = 0; k < 6; k++) cv_rect(x + w - 6 + k, k, 6 - k, 1, C_BLACK); /* the cut corner */
        ui_fit(x + 3, 3, w - 9, &FONT_XS, name[i] ? name[i] : "", lit ? C_WHITE : C_GRAY);
        /* the value, big */
        if (vtext && vtext[i]) ui_fit(x + 3, 18, w - 6, &FONT_M, vtext[i], lit ? C_WHITE : C_HI);
        else {
            t[0] = 0;
            ui_pct(t, v[i]);
            ui_fit(x + 3, 18, w - 6, &FONT_M, t, lit ? C_WHITE : C_HI);
        }
        /* the bar */
        cv_rect(x + 3, 44, w - 6, 5, C_LINE);
        bar = (int32_t)((float)(w - 6) * jo_clampf(v[i], 0.0f, 1.0f) + 0.5f);
        cv_rect(x + 3, 44, bar, 5, lit ? C_WHITE : C_AMB);
    }
    cv_blit(0, UI_FOOT_Y);
}

/* a 16-step row at y (canvas coordinates): on[i] 0 off, 1 on, 2 accent, 3 locked; play = the playhead (-1 none);
 * sel = a held step (-1) */
static void ui_steps(int32_t y, int32_t h, const uint8_t on[16], int play, int sel)
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        int32_t x = 2 + (int32_t)i * 15;
        uint16_t c = on[i] == 0 ? C_LINE : on[i] == 2 ? C_WHITE : on[i] == 3 ? RGB(150, 225, 255) : C_HI;
        cv_rect(x, y, 13, h, c);
        if ((i & 3u) == 0u) cv_rect(x, y + h + 1, 13, 2, C_DIM); /* the beats */
        if ((int)i == play) cv_rect(x, y - 4, 13, 3, C_WHITE);
        if ((int)i == sel) {
            cv_rect(x - 1, y - 1, 15, 1, C_WHITE);
            cv_rect(x - 1, y + h, 15, 1, C_WHITE);
        }
    }
}

/* (0.5) the sequencer's main view: one track's 16 steps big, two rows of 8 at canvas y (each box 28 x 22): on[i]
 * 0 off, 1 on, 2 accent; vel[i] 0..127 as a bar in the box; mark[i] bits: 1 a lock, 2 a condition, 4 probability
 * under 100 %, 8 a slide / open hat; play = the playhead (-1), held = a held step (-1). The beats' boxes have a tick */
#define UI_GRID_H 50
static void ui_grid(int32_t y, const uint8_t on[16], const uint8_t vel[16], const uint8_t mark[16], int play, int held)
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        int32_t x = 4 + (int32_t)(i & 7u) * 29, yy = y + (int32_t)(i >> 3) * 26, h;
        uint16_t c = on[i] == 0 ? C_LINE : on[i] == 2 ? C_WHITE : C_HI;
        cv_rect(x, yy, 27, 22, (int)i == held ? C_WHITE : C_BLACK);
        cv_rect(x + 1, yy + 1, 25, 20, on[i] ? C_BLACK : C_LINE);
        if (on[i]) {
            h = 2 + (int32_t)(vel[i] * 16u / 127u);
            cv_rect(x + 3, yy + 19 - h, 21, h, c);
        }
        if (mark[i] & 1u) cv_rect(x + 2, yy + 2, 4, 4, RGB(150, 225, 255)); /* lock */
        if (mark[i] & 2u) cv_rect(x + 21, yy + 2, 4, 4, UI_RED);            /* condition */
        if (mark[i] & 4u) cv_rect(x + 12, yy + 2, 3, 3, C_GRAY);            /* probability */
        if (mark[i] & 8u) cv_rect(x + 3, yy + 20, 21, 1, C_WHITE);          /* slide / open */
        if ((i & 3u) == 0u) cv_rect(x, yy + 23, 27, 1, C_DIM);              /* the beat */
        if ((int)i == play) cv_rect(x, yy + 23, 27, 2, C_WHITE);
    }
}

/* (0.5) a box over the page: a title line and a value line (a held step's edit, a held layer's state) */
static void ui_popup(int32_t y, const char *title, const char *line)
{
    cv_rect(10, y, 220, 34, C_BLACK);
    cv_rect(10, y, 220, 1, C_HI);
    cv_rect(10, y + 33, 220, 1, C_HI);
    cv_rect(10, y, 1, 34, C_HI);
    cv_rect(229, y, 1, 34, C_HI);
    ui_fit(16, y + 3, 208, &FONT_XS, title ? title : "", C_GRAY);
    ui_fit(16, y + 15, 208, &FONT_S, line ? line : "", C_WHITE);
}

/* (0.5) a held layer's banner across the page's top: the name between hazard stripes */
static void ui_banner(const char *name, const char *hint)
{
    int32_t w;
    cv_rect(0, 0, 240, 18, C_BLACK);
    ui_stripes(0, 0, 240, 18, C_HI);
    w = text_w(&FONT_B, name) + 12;
    if (w > 150) w = 150;
    cv_rect(4, 1, w, 16, C_BLACK);
    ui_fit(10, 2, w - 10, &FONT_B, name, C_HI);
    if (hint) {
        int32_t hw = text_w(&FONT_T, hint) + 8;
        if (hw > 230 - w) hw = 230 - w;
        cv_rect(236 - hw, 3, hw, 12, C_BLACK);
        ui_fit(240 - hw, 4, hw - 6, &FONT_T, hint, C_WHITE);
    }
}

/* a horizontal meter 0..1 with a mark at the ceiling */
static void ui_meter(int32_t x, int32_t y, int32_t w, float v, uint16_t c)
{
    int32_t b = (int32_t)((float)w * jo_clampf(v, 0.0f, 1.0f));
    cv_rect(x, y, w, 5, C_LINE);
    cv_rect(x, y, b, 5, v >= 0.94f ? UI_RED : c);
}

/* ----------------------------------------------------------- input --- */
/* edges and holds of the 14 buttons and 27 keys; tap = pressed and released within UI_TAP_MS without anything
 * else happening in between */
#define UI_TAP_MS 350u
typedef struct {
    uint32_t btn, keys;               /* held now */
    uint32_t btn_down, btn_up, key_down, key_up; /* edges this frame */
    uint32_t btn_t[NB];               /* when each went down */
    uint32_t key_t[NKEYS];
    uint32_t btn_used;                /* a held button used as a modifier: its release is not a tap */
} ui_in_t;

static void ui_poll(ui_in_t *in)
{
    uint32_t b = plat_buttons(), now = plat_ms(), i, k;
#ifdef APP_TOUCH
    touch_poll(); /* the sensed keys (velocity): lib/touch.h */
    k = touch_keys();
#else
    k = plat_keys();
#endif
    in->btn_down = b & ~in->btn;
    in->btn_up = ~b & in->btn;
    in->key_down = k & ~in->keys;
    in->key_up = ~k & in->keys;
    for (i = 0; i < NB; i++)
        if (in->btn_down >> i & 1u) {
            in->btn_t[i] = now;
            in->btn_used &= ~(1u << i);
        }
    for (i = 0; i < NKEYS; i++)
        if (in->key_down >> i & 1u) in->key_t[i] = now;
    in->btn = b;
    in->keys = k;
}
static int ui_held(const ui_in_t *in, int b) { return (int)(in->btn >> b & 1u); }
static int ui_pressed(const ui_in_t *in, int b) { return (int)(in->btn_down >> b & 1u); }
/* released as a tap (not used as a modifier, not held long) */
static int ui_tapped(ui_in_t *in, int b)
{
    return (in->btn_up >> b & 1u) && !(in->btn_used >> b & 1u) && plat_ms() - in->btn_t[b] < 1500u;
}
static void ui_use(ui_in_t *in, int b) { in->btn_used |= 1u << b; }

/* white key index (0..15) of key k, or -1; black key index (0..10), or -1 (F3 = key 0) */
static int ui_white(uint32_t k)
{
    static const int8_t W[NKEYS] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6, 7, -1, 8, -1, 9, -1, 10, 11, -1, 12, -1, 13, 14, -1, 15};
    return k < NKEYS ? W[k] : -1;
}
static int ui_black(uint32_t k)
{
    static const int8_t B[NKEYS] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, 4, -1, -1, 5, -1, 6, -1, 7, -1, -1, 8, -1, 9, -1, -1, 10, -1};
    return k < NKEYS ? B[k] : -1;
}
static uint32_t ui_white_bit(int w)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        if (ui_white(k) == w) return 1u << k;
    return 0;
}
static uint32_t ui_black_bit(int b)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        if (ui_black(k) == b) return 1u << k;
    return 0;
}

/* ----------------------------------------------------------- splash --- */
/* (0.3) the start screen: the name in two big lines between hazard bars (drawn once at boot, held by the app) */
static void ui_splash(const char *line1, const char *line2, const char *sub)
{
    int32_t b;
    for (b = 0; b < 240; b += 48) { /* five bands (the canvas holds a part of the screen at a time) */
        cv_begin(240, 48, C_BLACK);
        if (b == 0) ui_stripes(0, 16, 240, 14, C_HI);
        if (b == 192) ui_stripes(0, 18, 240, 14, C_HI);
        if (b == 48) ui_fit(12, 10, 216, &FONT_L, line1, C_HI);
        if (b == 96) ui_fit(12, 0, 216, &FONT_L, line2, C_HI);
        if (b == 144) {
            cv_rect(12, 0, 216, 2, C_AMB);
            ui_fit(12, 10, 216, &FONT_XS, sub, C_GRAY);
        }
        cv_blit(0, (uint32_t)b);
    }
}
