/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* JUNGLE OS screen and input helpers, shared by both firmwares. Part of each app's unity build (included by
 * app.c after fm1-base's gfx.c): it draws with fm1-base's canvas (cv_*), fonts and palette.
 *
 * Screen: 240 x 240, three bands drawn and sent separately (only when something changed):
 *   header  y   0..25   page name, the instrument's state (tempo, clock, scene, load)
 *   body    y  26..169  the page
 *   footer  y 170..239  the four macro boxes (K1..K4), names and values */

#define UI_HEAD_H 26
#define UI_BODY_Y 26
#define UI_BODY_H 144
#define UI_FOOT_Y 170
#define UI_FOOT_H 70

/* ------------------------------------------------------------ text --- */
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
/* header: the page on the left (inverted), text on the right */
static void ui_header(const char *page, const char *mid, const char *right, int alarm)
{
    int32_t w;
    cv_begin(240, UI_HEAD_H, C_BLACK);
    w = text_w(&FONT_B, page) + 10;
    cv_rect(0, 0, w, UI_HEAD_H - 2, C_HI);
    cv_text(5, 4, &FONT_B, page, C_BLACK);
    if (mid) cv_text(w + 6, 6, &FONT_S, mid, C_AMB);
    if (right) cv_text(236 - text_w(&FONT_S, right), 6, &FONT_S, right, alarm ? C_WHITE : C_GRAY);
    if (alarm) cv_rect(0, UI_HEAD_H - 2, 240, 2, RGB(232, 76, 60));
    else cv_rect(0, UI_HEAD_H - 1, 240, 1, C_LINE);
    cv_blit(0, 0);
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
        cv_rect(x, 0, w, UI_FOOT_H - 4, lit ? C_LINE : C_BLACK);
        cv_rect(x, 0, w, 1, C_DIM);
        cv_text(x + 3, 4, &FONT_XS, name[i] ? name[i] : "", lit ? C_WHITE : C_GRAY);
        /* the bar */
        cv_rect(x + 3, 24, w - 6, 8, C_LINE);
        bar = (int32_t)((float)(w - 6) * jo_clampf(v[i], 0.0f, 1.0f) + 0.5f);
        cv_rect(x + 3, 24, bar, 8, lit ? C_WHITE : C_HI);
        if (vtext && vtext[i]) {
            cv_text(x + 3, 38, &FONT_B, vtext[i], lit ? C_WHITE : C_AMB);
        } else {
            t[0] = 0;
            ui_pct(t, v[i]);
            cv_text(x + 3, 38, &FONT_B, t, lit ? C_WHITE : C_AMB);
        }
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

/* a horizontal meter 0..1 with a mark at the ceiling */
static void ui_meter(int32_t x, int32_t y, int32_t w, float v, uint16_t c)
{
    int32_t b = (int32_t)((float)w * jo_clampf(v, 0.0f, 1.0f));
    cv_rect(x, y, w, 5, C_LINE);
    cv_rect(x, y, b, 5, v >= 0.94f ? RGB(232, 76, 60) : c);
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
