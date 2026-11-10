/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
#include "jo_link.h"

/* USB-MIDI event packet: byte 0 cable/CIN, then status, data 1, data 2 (fm1-base plat.h) */
static uint32_t pkt3(uint8_t cin, uint8_t st, uint8_t d1, uint8_t d2)
{
    return (uint32_t)cin | (uint32_t)st << 8 | (uint32_t)(d1 & 0x7Fu) << 16 | (uint32_t)(d2 & 0x7Fu) << 24;
}

void jl_init(jl_link_t *l, uint8_t channel)
{
    uint8_t *p = (uint8_t *)l;
    uint32_t i;
    for (i = 0; i < sizeof *l; i++) p[i] = 0;
    l->channel = channel & 15u;
    l->prepared_scene = -1;
}

int jl_alive(const jl_link_t *l, uint32_t now_ms) { return l->heard && now_ms - l->last_heard_ms < JL_LOST_MS; }

int jl_decode(jl_link_t *l, uint32_t pkt, uint32_t now_ms, jl_event_t *ev)
{
    uint8_t st = (uint8_t)(pkt >> 8), d1 = (uint8_t)(pkt >> 16) & 0x7Fu, d2 = (uint8_t)(pkt >> 24) & 0x7Fu;
    ev->type = JL_EV_NONE;
    ev->a = ev->b = 0;
    switch (st) {
    case 0xF8: ev->type = JL_EV_CLOCK; return 1;
    case 0xFA: ev->type = JL_EV_START; return 1;
    case 0xFB: ev->type = JL_EV_CONTINUE; return 1;
    case 0xFC: ev->type = JL_EV_STOP; return 1;
    default: break;
    }
    if ((st & 0x0Fu) != l->channel)
        return 0;
    if ((st & 0xF0u) == 0x90u && d2) {
        if (d1 != JL_NOTE_KICK && d1 != JL_NOTE_SNARE) return 0;
        ev->type = d1 == JL_NOTE_KICK ? JL_EV_KICK : JL_EV_SNARE;
        ev->a = d2;
        l->last_heard_ms = now_ms;
        l->heard = 1;
        return 1;
    }
    if ((st & 0xF0u) != 0xB0u || d1 < JL_CC_HELLO || d1 > JL_CC_SCALE)
        return 0;
    l->last_heard_ms = now_ms;
    l->heard = 1;
    switch (d1) {
    case JL_CC_HELLO:
        l->peer_version = d2;
        ev->type = d2 == JL_VERSION ? JL_EV_HELLO : JL_EV_VERSION_MISMATCH;
        ev->a = d2;
        return 1;
    case JL_CC_PREPARE:
        l->prepared_scene = (int8_t)(d2 & 15u);
        l->prepared_quant = (uint8_t)(d2 >> 4);
        return 0;
    case JL_CC_COMMIT:
        if (l->prepared_scene != (int8_t)(d2 & 15u))
            return 0;  /* a commit without its prepare (lost, or a repeat): nothing changes */
        ev->type = JL_EV_SCENE;
        ev->a = (uint8_t)l->prepared_scene;
        ev->b = l->prepared_quant;
        l->prepared_scene = -1; /* the redundant second commit is then ignored */
        return 1;
    case JL_CC_FILL_BEGIN: ev->type = JL_EV_FILL_BEGIN; ev->a = d2 ? d2 : 1u; return 1;
    case JL_CC_FILL_END: ev->type = JL_EV_FILL_END; return 1;
    case JL_CC_DROP: ev->type = JL_EV_DROP; return 1;
    case JL_CC_RESET: ev->type = JL_EV_RESET; return 1;
    case JL_CC_ENERGY: ev->type = JL_EV_ENERGY; ev->a = d2; return 1;
    case JL_CC_ROOT: ev->type = JL_EV_ROOT; ev->a = (uint8_t)(d2 % 12u); return 1;
    default: ev->type = JL_EV_SCALE; ev->a = d2; return 1;
    }
}

uint32_t jl_cc(const jl_link_t *l, uint8_t cc, uint8_t value) { return pkt3(0x0Bu, (uint8_t)(0xB0u | l->channel), cc, value); }

uint32_t jl_hello(jl_link_t *l, uint32_t now_ms, uint32_t *out)
{
    if (l->sent && now_ms - l->last_hello_ms < JL_HEARTBEAT_MS)
        return 0;
    l->last_hello_ms = now_ms;
    l->sent++;
    out[0] = jl_cc(l, JL_CC_HELLO, JL_VERSION);
    return 1;
}

uint32_t jl_scene(const jl_link_t *l, uint8_t scene, uint8_t quant, uint32_t *out)
{
    out[0] = jl_cc(l, JL_CC_PREPARE, (uint8_t)((scene & 15u) | (quant & 7u) << 4));
    out[1] = jl_cc(l, JL_CC_COMMIT, scene & 15u);
    out[2] = out[1]; /* sent twice: a lost commit is covered, a doubled one is ignored */
    return 3;
}

uint32_t jl_trigger(jl_link_t *l, uint8_t note, uint8_t vel, uint32_t now_ms, uint32_t *out)
{
    uint32_t *last = note == JL_NOTE_KICK ? &l->last_kick_ms : &l->last_snare_ms;
    if (*last && now_ms - *last < JL_TRIG_MIN_MS) {
        l->rate_limited++;
        return 0;
    }
    *last = now_ms ? now_ms : 1u;
    out[0] = pkt3(0x09u, (uint8_t)(0x90u | l->channel), note, vel ? vel : 1u);
    out[1] = pkt3(0x08u, (uint8_t)(0x80u | l->channel), note, 0);
    return 2;
}
