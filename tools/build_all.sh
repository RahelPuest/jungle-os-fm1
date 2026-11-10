#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Builds and checks everything: the engine tests, the renders, the sample-prep tests, both firmwares (host
# simulators and device packages) and the host scripts.   tools/build_all.sh [--no-device]
set -e
cd "$(dirname "$0")/.."
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}" AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
for d in "$HOME/.local/fribidi/lib" /opt/homebrew/opt/fribidi/lib; do [ -d "$d" ] && export DYLD_LIBRARY_PATH="$d" && break; done
make -C tests
make -C tests renders
python3 tests/tools/test_sample_prep.py
OUT=tests/build/host; mkdir -p "$OUT/bs" "$OUT/ab"
for fw in break-surgeon abyss-engine; do
    (cd firmware/$fw && python3 ../../base/tools/build.py . --host | grep -E '^host|error' || true)
    [ "$1" = "--no-device" ] || (cd firmware/$fw && python3 ../../base/tools/build.py . | grep -E '^  (ok|FAIL)|^package|rror')
done
# (the screens' text is fitted to its boxes; a text cut to fit is reported on stderr and fails the build)
firmware/break-surgeon/build/host/breaksurgeon_host tests/host/bs_smoke.txt "$OUT/bs" 2> "$OUT/bs/stderr.txt" || { cat "$OUT/bs/stderr.txt"; exit 1; }
firmware/abyss-engine/build/host/abyssengine_host tests/host/ab_smoke.txt "$OUT/ab" 2> "$OUT/ab/stderr.txt" || { cat "$OUT/ab/stderr.txt"; exit 1; }
if grep -h "^ui: cut" "$OUT/bs/stderr.txt" "$OUT/ab/stderr.txt"; then echo "build_all: text cut to fit (above)"; exit 1; fi
echo "all done: packages in firmware/*/build/*.fwsc, screenshots and WAVs in $OUT"
