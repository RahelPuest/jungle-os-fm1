/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* ABYSS ENGINE's half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "ABYSS ENGINE"
#define APP_SPLASH_RGB RGB(60, 180, 235)
#define APP_BLOCK 128u                   /* 2.9 ms */
#define APP_CPU_KHZ
#define APP_TOUCH                        /* velocity from the keys: the keyboard plays the parts */

enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_PAT0, OBJ_PAT1, OBJ_PAT2, OBJ_PAT3, OBJ_SCENES, OBJ_NOBJ };
#define APP_STORE_MAP {0xDE000u, 0xDC000u, 0x97000u, 0x99000u, 0x9B000u, 0x9D000u, 0x9F000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
