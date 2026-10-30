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
    $CXX -std=c++20 -I. -Igame -c game/tests/stream_test.cpp -o stream_t.o -O2 -Wall
    $CXX stream_t.o diag_t.o jobs_t.o -o stream_test -lm -pthread && ./stream_test
    $CXX -std=c++20 -I. -Igame -c game/tests/sim_test.cpp -o sim_t.o -O2 -Wall
    $CXX sim_t.o diag_t.o jobs_t.o -o sim_test -lm -pthread && ./sim_test ${2:-3}
    $CXX -std=c++20 -pthread game/tests/entity_test.cpp game/src/engine/entity_manager.cc diag_t.o jobs_t.o -o entity_test -O2 -Wall && rm -f sim_t.o stream_t.o jobs_t.o diag_t.o diag_test.o && ./entity_test
    exit $?
fi

FLAGS="$*"; [ -z "$FLAGS" ] && FLAGS="-O2"
set -x
# every module under game/src compiles as its own C++20 TU (docs/CPP_CONVERSION.md)
rm -rf ccobj && mkdir -p ccobj
for f in $(find game/src -name '*.cc'); do
    $CXX -std=c++20 -pthread -I. -Igame -c "$f" -o "ccobj/$(basename "${f%.cc}").o" $FLAGS || exit 1
done
if [ "$(uname)" = "Darwin" ]; then
    export SDKROOT=$(xcrun --show-sdk-path)
    $CC -ObjC -c game/src/platform/v3_impl.c -o ccobj/v3_impl.o -I. $FLAGS || exit 1
    $CXX -std=c++20 -c game/mare.cc -o ccobj/mare_main.o -I. -Igame $FLAGS || exit 1
    $CXX ccobj/*.o -o mare $FLAGS -framework cocoa -framework iokit -framework audiotoolbox || exit 1
else
    # dev packages (Debian/Ubuntu): gcc g++ libx11-dev libgl1-mesa-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libasound2-dev
    $CC -c game/src/platform/v3_impl.c -o ccobj/v3_impl.o -I. $FLAGS || exit 1
    $CXX -std=c++20 -c game/mare.cc -o ccobj/mare_main.o -I. -Igame $FLAGS || exit 1
    $CXX ccobj/*.o -o mare $FLAGS -lm -ldl -lpthread -lasound || exit 1
fi
rm -rf ccobj
# every successful build is archived under tmp/builds/<root>-<datetime>
STAMP="$(basename "$PWD")-$(date +%Y%m%d-%H%M%S)"
mkdir -p "tmp/builds/$STAMP" && cp mare "tmp/builds/$STAMP/mare" && echo "build archived: tmp/builds/$STAMP/mare"
