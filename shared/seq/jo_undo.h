/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* (0.5) UNDO / REDO for both firmwares: a stack of whole-pattern snapshots (and a small blob of the instrument's own
 * state, e.g. Abyss's sound mutation). ju_push records the state BEFORE a change; ju_undo swaps the newest snapshot
 * with the live state (so the snapshot now holds the redo), ju_redo swaps it back. Main loop only; the caller locks
 * the render around ju_undo / ju_redo (they write the live pattern). Header-only. */
#pragma once
#include "jo_seq.h"

#define JU_DEPTH 8
#define JU_EXTRA 80
typedef struct {
    js_pattern_t pat;
    uint8_t slot;                 /* which of the instrument's patterns */
    uint8_t extra[JU_EXTRA];
} ju_entry_t;
typedef struct {
    ju_entry_t e[JU_DEPTH];
    uint8_t cur, top;             /* e[0..cur) can be undone, e[cur..top) redone */
} ju_stack_t;

static void ju_copy(void *d, const void *s, uint32_t n)
{
    uint8_t *a = (uint8_t *)d;
    const uint8_t *b = (const uint8_t *)s;
    while (n--) *a++ = *b++;
}
static void ju_swap(void *x, void *y, uint32_t n)
{
    uint8_t *a = (uint8_t *)x, *b = (uint8_t *)y, t;
    while (n--) t = *a, *a++ = *b, *b++ = t;
}

static void ju_init(ju_stack_t *u) { u->cur = u->top = 0; }

/* the state before a change: pattern `slot` as it is now, and the instrument's extra state (or 0) */
static void ju_push(ju_stack_t *u, uint8_t slot, const js_pattern_t *before, const void *extra, uint32_t extra_len)
{
    ju_entry_t *e;
    if (u->cur == JU_DEPTH) { /* full: the oldest goes */
        uint32_t i;
        for (i = 1; i < JU_DEPTH; i++) ju_copy(&u->e[i - 1], &u->e[i], sizeof u->e[0]);
        u->cur--;
    }
    e = &u->e[u->cur];
    ju_copy(&e->pat, before, sizeof *before);
    e->slot = slot;
    if (extra && extra_len) ju_copy(e->extra, extra, extra_len > JU_EXTRA ? JU_EXTRA : extra_len);
    u->cur++;
    u->top = u->cur; /* a new change: no redo past it */
}

/* pats: the instrument's patterns (indexed by slot); extra: its live extra state. 0 = done, -1 = nothing to do.
 * *slot_out: the pattern that changed */
static int ju_undo(ju_stack_t *u, js_pattern_t *pats, void *extra, uint32_t extra_len, uint8_t *slot_out)
{
    ju_entry_t *e;
    if (!u->cur) return -1;
    e = &u->e[--u->cur];
    ju_swap(&e->pat, &pats[e->slot], sizeof e->pat);
    if (extra && extra_len) ju_swap(e->extra, extra, extra_len > JU_EXTRA ? JU_EXTRA : extra_len);
    if (slot_out) *slot_out = e->slot;
    return 0;
}
static int ju_redo(ju_stack_t *u, js_pattern_t *pats, void *extra, uint32_t extra_len, uint8_t *slot_out)
{
    ju_entry_t *e;
    if (u->cur == u->top) return -1;
    e = &u->e[u->cur++];
    ju_swap(&e->pat, &pats[e->slot], sizeof e->pat);
    if (extra && extra_len) ju_swap(e->extra, extra, extra_len > JU_EXTRA ? JU_EXTRA : extra_len);
    if (slot_out) *slot_out = e->slot;
    return 0;
}
static int ju_can_undo(const ju_stack_t *u) { return u->cur != 0; }
static int ju_can_redo(const ju_stack_t *u) { return u->cur != u->top; }
