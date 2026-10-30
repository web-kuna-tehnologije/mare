#!/bin/bash
# Build MARE LIBERUM. Run from anywhere.
#   ./game/build.sh            release build        -> ./mare
#   ./game/build.sh -g -O0     debug build
#   ./game/build.sh test       headless tests (no GL/v3 needed) -> ./diag_test ./jobs_test ./sim_test
#
# Translation units: the C11 unity build of the unported modules (game/mare.c) and the C++20
# core (game/src/core/*.cc - diag, worker pool; see docs/REWRITE_PLAN.md). Linked with the
# C++ driver.
cd "$(dirname "$0")/.."

CC=${CC:-cc}
CXX=${CXX:-c++}
CORE_CXX="game/src/core/diag.cc game/src/core/worker_pool.cc"

if [ "$1" = "test" ]; then
    set -e; set -x
    $CXX -std=c++20 -c game/src/core/diag.cc -o diag_t.o -O2 -Wall
    $CXX -std=c++20 -pthread -c game/src/core/worker_pool.cc -o jobs_t.o -O2 -Wall
    $CC  -c game/tests/diag_test.c -o diag_test.o -O2 -Wall
    $CXX diag_test.o diag_t.o -o diag_test -lm && ./diag_test
    $CXX -std=c++20 game/tests/jobs_test.cpp -o jobs_test -O2 -Wall -pthread && ./jobs_test
    $CC  -c game/tests/stream_test.c -o stream_t.o -O2 -Wall
    $CXX stream_t.o diag_t.o jobs_t.o -o stream_test -lm -pthread && ./stream_test
    $CC  -c game/tests/sim_test.c -o sim_t.o -O2 -Wall
    $CXX sim_t.o diag_t.o jobs_t.o -o sim_test -lm -pthread && rm -f sim_t.o stream_t.o jobs_t.o diag_t.o diag_test.o && ./sim_test ${2:-3}
    exit $?
fi

FLAGS="$*"; [ -z "$FLAGS" ] && FLAGS="-O2"
set -x
$CXX -std=c++20 -c game/src/core/diag.cc -o mare_diag.o $FLAGS || exit 1
$CXX -std=c++20 -pthread -c game/src/core/worker_pool.cc -o mare_jobs.o $FLAGS || exit 1
if [ "$(uname)" = "Darwin" ]; then
    export SDKROOT=$(xcrun --show-sdk-path)
    $CC -ObjC -c game/mare.c -o mare_main.o -I. $FLAGS || exit 1
    $CXX mare_main.o mare_diag.o mare_jobs.o -o mare $FLAGS -framework cocoa -framework iokit -framework audiotoolbox || exit 1
else
    # dev packages (Debian/Ubuntu): gcc g++ libx11-dev libgl1-mesa-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libasound2-dev
    $CC -c game/mare.c -o mare_main.o -I. $FLAGS || exit 1
    $CXX mare_main.o mare_diag.o mare_jobs.o -o mare $FLAGS -lm -ldl -lpthread -lasound || exit 1
fi
rm -f mare_main.o mare_diag.o mare_jobs.o
