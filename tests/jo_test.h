/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 JUNGLE OS contributors */
/* A minimal test harness: CHECK keeps going, the runner's exit code is the failure count. */
#pragma once
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
extern int jt_fail, jt_pass;
#define CHECK(c) do { if (c) jt_pass++; else { jt_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKF(c, ...) do { if (c) jt_pass++; else { jt_fail++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define TEST(name) static void name(void)
#define RUN(name) do { int f0 = jt_fail; name(); printf("%s %s\n", jt_fail == f0 ? "ok  " : "FAIL", #name); } while (0)
