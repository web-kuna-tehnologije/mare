#!/bin/bash
# Build MARE LIBERUM. Run from anywhere.
#   ./game/build.sh            release build        -> ./mare
#   ./game/build.sh -g -O0     debug build
#   ./game/build.sh test       headless simulation tests (no GL/v3 needed) -> ./sim_test
cd "$(dirname "$0")/.."

if [ "$1" = "test" ]; then
    set -e; set -x
    cc game/tests/diag_test.c -o diag_test -lm -O2 -Wall && ./diag_test
    cc game/tests/sim_test.c -o sim_test -lm -O2 -Wall && ./sim_test ${2:-3}
    exit $?
fi

FLAGS="$*"; [ -z "$FLAGS" ] && FLAGS="-O2"
if [ "$(uname)" = "Darwin" ]; then
    export SDKROOT=$(xcrun --show-sdk-path)
    set -x
    cc -ObjC game/mare.c -o mare -I. $FLAGS -framework cocoa -framework iokit -framework audiotoolbox
else
    # dev packages (Debian/Ubuntu): gcc libx11-dev libgl1-mesa-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libasound2-dev
    set -x
    cc game/mare.c -o mare -I. $FLAGS -lm -ldl -lpthread -lasound
fi
