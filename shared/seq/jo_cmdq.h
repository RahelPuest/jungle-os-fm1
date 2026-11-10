/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* A bounded single-producer / single-consumer command ring: the main loop (UI, MIDI parsing) pushes, the audio
 * interrupt drains at the start of each block. No lock: each index has one writer, and on the FM-1's single
 * core an aligned 32-bit store is atomic with respect to the interrupt. A full ring refuses the command (the
 * caller shows it); it never blocks. */
#pragma once
#include <stdint.h>

#define JO_CMDQ_N 32u /* a power of two */
typedef struct { uint8_t op, a, b, c; float f; } jo_cmd_t;
typedef struct {
    jo_cmd_t c[JO_CMDQ_N];
    volatile uint32_t head; /* written by the producer */
    volatile uint32_t tail; /* written by the consumer */
    uint32_t refused;
} jo_cmdq_t;

static inline int jo_cmdq_push(jo_cmdq_t *q, uint8_t op, uint8_t a, uint8_t b, uint8_t c, float f)
{
    uint32_t h = q->head;
    if (h - q->tail >= JO_CMDQ_N) {
        q->refused++;
        return -1;
    }
    q->c[h & (JO_CMDQ_N - 1u)].op = op;
    q->c[h & (JO_CMDQ_N - 1u)].a = a;
    q->c[h & (JO_CMDQ_N - 1u)].b = b;
    q->c[h & (JO_CMDQ_N - 1u)].c = c;
    q->c[h & (JO_CMDQ_N - 1u)].f = f;
    __asm__ volatile("" ::: "memory"); /* the command is whole before the head moves */
    q->head = h + 1u;
    return 0;
}

static inline int jo_cmdq_pop(jo_cmdq_t *q, jo_cmd_t *out)
{
    uint32_t t = q->tail;
    if (t == q->head)
        return 0;
    *out = q->c[t & (JO_CMDQ_N - 1u)];
    __asm__ volatile("" ::: "memory");
    q->tail = t + 1u;
    return 1;
}
