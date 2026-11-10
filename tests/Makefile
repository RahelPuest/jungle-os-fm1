# SPDX-License-Identifier: MPL-2.0
# Host-native tests for JUNGLE OS's shared engines: no FM-1, no toolchain.
#   make -C tests          build and run every suite
#   make -C tests renders  the audio-regression renders (WAV) and their hashes
CC ?= cc
# -Wdouble-promotion: the FM-1's FPU is single precision; a double in the engines is a bug
WARN = -Wall -Wextra -Wno-unused-parameter -Wdouble-promotion -Werror=double-promotion -Wno-unused-function
CFLAGS = -std=c99 -O2 -ffp-contract=off $(WARN) -I../shared
# shared/ui is part of the firmwares' unity builds (it draws with fm1-base's canvas): not built here
SHARED = $(filter-out ../shared/ui/%,$(wildcard ../shared/*/*.c)) $(wildcard ../firmware/*/engine/*.c)
UNIT = $(wildcard unit/*.c property/*.c integration/*.c)
BUILD = build

all: test
$(BUILD)/run_tests: run_all.c suites.h jo_test.h $(UNIT) $(SHARED) $(wildcard ../shared/*/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ run_all.c $(UNIT) $(SHARED) -lm
test: $(BUILD)/run_tests
	./$(BUILD)/run_tests
$(BUILD)/render: audio-regression/render.c $(SHARED) $(wildcard ../shared/*/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ audio-regression/render.c $(SHARED) -lm
renders: $(BUILD)/render
	./$(BUILD)/render $(BUILD)/renders audio-regression/golden.txt
clean:
	rm -rf $(BUILD)
.PHONY: all test renders clean
