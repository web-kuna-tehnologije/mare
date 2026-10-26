#!/bin/bash
# Build MARE LIBERUM. Run from anywhere.
#   ./game/build.sh            release build        -> ./mare
#   ./game/build.sh -g -O0     debug build
#   ./game/build.sh test       headless tests (no GL/v3 needed) -> ./diag_test ./jobs_test ./sim_test
#
# Two translation units: the C11 unity build (game/mare.c) and the job system's C++17 unit
# (game/src/core/jobs.cpp, see game/docs/DESIGN_JOBS.md). They are linked with the C++ driver.
cd "$(dirname "$0")/.."

CC=${CC:-cc}
CXX=${CXX:-c++}

if [ "$1" = "test" ]; then
    set -e; set -x
    $CC  game/tests/diag_test.c -o diag_test -lm -O2 -Wall && ./diag_test
    $CXX -std=c++17 game/tests/jobs_test.cpp -o jobs_test -O2 -Wall -pthread && ./jobs_test
    $CXX -std=c++17 -pthread -c game/src/core/jobs.cpp -o jobs_t.o -O2
    $CC  -c game/tests/sim_test.c -o sim_t.o -lm -O2 -Wall
    $CXX sim_t.o jobs_t.o -o sim_test -lm -pthread && rm -f sim_t.o jobs_t.o && ./sim_test ${2:-3}
    exit $?
fi

FLAGS="$*"; [ -z "$FLAGS" ] && FLAGS="-O2"
set -x
$CXX -std=c++17 -pthread -c game/src/core/jobs.cpp -o mare_jobs.o $FLAGS || exit 1
if [ "$(uname)" = "Darwin" ]; then
    export SDKROOT=$(xcrun --show-sdk-path)
    $CC -ObjC -c game/mare.c -o mare_main.o -I. $FLAGS || exit 1
    $CXX mare_main.o mare_jobs.o -o mare $FLAGS -framework cocoa -framework iokit -framework audiotoolbox || exit 1
else
    # dev packages (Debian/Ubuntu): gcc g++ libx11-dev libgl1-mesa-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libasound2-dev
    $CC -c game/mare.c -o mare_main.o -I. $FLAGS || exit 1
    $CXX mare_main.o mare_jobs.o -o mare $FLAGS -lm -ldl -lpthread -lasound || exit 1
fi
rm -f mare_main.o mare_jobs.o
