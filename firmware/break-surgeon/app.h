/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* BREAK SURGEON's half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "BREAK SURGEON"
#define APP_SPLASH_RGB RGB(232, 136, 30)
#define APP_BLOCK 128u                   /* frames per audio block: 2.9 ms at 44.1 kHz */
#define APP_SYSEX                        /* the sample bank's loader (docs/development/sample-format.md) */
#define APP_CPU_KHZ                      /* the CPU clock, for the SYSTEM page's load figures */

/* flash map (the data area 0x97000..0xDFFFF): the store objects as A/B sector pairs, and the sample bank between
 * them. The panel calibration sits where fm1-base's template keeps it */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_PAT0, OBJ_PAT1, OBJ_PAT2, OBJ_PAT3, OBJ_SCENES, OBJ_NOBJ };
#define APP_STORE_MAP {0xDE000u, 0xDC000u, 0x97000u, 0x99000u, 0x9B000u, 0x9D000u, 0x9F000u}
#define BS_BANK_LO 0xA1000u              /* the sample bank: 0xA1000..0xDBFFF, 241,664 bytes */
#define BS_BANK_HI 0xDC000u

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
int app_sysex(const uint8_t *p, uint32_t n);
