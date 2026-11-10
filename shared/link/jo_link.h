/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* JUNGLE LINK v1: Break Surgeon <-> Abyss Engine over plain MIDI (docs/architecture/link-protocol.md).
 *
 * Transport-neutral: the packets are USB-MIDI event packets (plat.h), which fm1-base sends over USB (device
 * port, or the USB host port with fm1.toml usb_host) and Bluetooth, and receives from those and the TRS jack.
 * Standard real-time messages carry the clock (F8), START (FA), CONTINUE (FB), STOP (FC). Everything else is
 * Control Change or Note On on the link channel (default 16), so a computer or a hub in between passes it.
 *
 *   CC 102 HELLO / heartbeat   value = protocol version (1), every 500 ms; 1.5 s silence = link lost
 *   CC 103 SCENE_PREPARE       value = scene (0..15) | quant << 4 (JL_Q_*)
 *   CC 104 SCENE_COMMIT        value = scene: executes only if it matches the last PREPARE (no half scenes)
 *   CC 105 FILL_BEGIN          value = bars (1, 2, 4)
 *   CC 106 FILL_END
 *   CC 107 DROP                CC 108 RESET (to the stable groove)      CC 109 ENERGY 0..127
 *   CC 110 ROOT 0..11          CC 111 SCALE id
 *   Note 36 KICK_TRIGGER, Note 38 SNARE_TRIGGER (velocity), at most one each per 15 ms */
#pragma once
#include <stdint.h>

#define JL_VERSION 1u
#define JL_CC_HELLO 102u
#define JL_CC_PREPARE 103u
#define JL_CC_COMMIT 104u
#define JL_CC_FILL_BEGIN 105u
#define JL_CC_FILL_END 106u
#define JL_CC_DROP 107u
#define JL_CC_RESET 108u
#define JL_CC_ENERGY 109u
#define JL_CC_ROOT 110u
#define JL_CC_SCALE 111u
#define JL_NOTE_KICK 36u
#define JL_NOTE_SNARE 38u
#define JL_HEARTBEAT_MS 500u
#define JL_LOST_MS 1500u
#define JL_TRIG_MIN_MS 15u

enum { JL_Q_NOW, JL_Q_BEAT, JL_Q_BAR, JL_Q_2BAR, JL_Q_4BAR };

enum {
    JL_EV_NONE, JL_EV_CLOCK, JL_EV_START, JL_EV_CONTINUE, JL_EV_STOP, JL_EV_SCENE, JL_EV_FILL_BEGIN, JL_EV_FILL_END,
    JL_EV_DROP, JL_EV_RESET, JL_EV_ENERGY, JL_EV_ROOT, JL_EV_SCALE, JL_EV_KICK, JL_EV_SNARE, JL_EV_HELLO,
    JL_EV_VERSION_MISMATCH
};
typedef struct { uint8_t type, a, b; } jl_event_t; /* SCENE: a scene, b quant; FILL: a bars; KICK: a velocity */

typedef struct {
    uint8_t channel;          /* 0..15 */
    /* receiver */
    int8_t prepared_scene;    /* -1: none */
    uint8_t prepared_quant;
    uint32_t last_heard_ms;   /* any link message */
    uint8_t peer_version;
    uint8_t heard;
    /* sender */
    uint32_t last_hello_ms, last_kick_ms, last_snare_ms;
    uint32_t sent, rate_limited;
} jl_link_t;

void jl_init(jl_link_t *l, uint8_t channel);
/* decodes one USB-MIDI packet at time now_ms; returns 1 with *ev set if it was a link event */
int jl_decode(jl_link_t *l, uint32_t pkt, uint32_t now_ms, jl_event_t *ev);
int jl_alive(const jl_link_t *l, uint32_t now_ms);

/* encoders: the packet(s) to send. Each returns the count written to out (0 = nothing to send now) */
uint32_t jl_cc(const jl_link_t *l, uint8_t cc, uint8_t value);   /* one packet */
uint32_t jl_hello(jl_link_t *l, uint32_t now_ms, uint32_t *out);  /* 0 or 1, due every JL_HEARTBEAT_MS */
uint32_t jl_scene(const jl_link_t *l, uint8_t scene, uint8_t quant, uint32_t *out); /* PREPARE + COMMIT x2: 3 */
uint32_t jl_trigger(jl_link_t *l, uint8_t note, uint8_t vel, uint32_t now_ms, uint32_t *out); /* 0 or 2 (on+off) */
#define JL_PKT_CLOCK 0xF80Fu   /* single-byte real-time: CIN 0xF, F8 */
#define JL_PKT_START 0xFA0Fu
#define JL_PKT_CONTINUE 0xFB0Fu
#define JL_PKT_STOP 0xFC0Fu
