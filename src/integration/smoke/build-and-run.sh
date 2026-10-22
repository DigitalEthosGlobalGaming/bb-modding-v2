#!/bin/sh
# Build and run the Windows integration smoke test from Linux (mingw-w64 + wine64).
#   integration/smoke/build-and-run.sh <bbport checkout with the patch applied> <imgui dir> <SDL3 dir> <zydis-mingw prefix>
# Needs build-win/ from the main README (test DLLs and libbbmod_host.a).
set -e
BBPORT=$1; IMGUI=$2; SDL=$3; ZYDIS=$4
HERE=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$HERE/../../.." && pwd)
OUT=$ROOT/build-win/smoke; mkdir -p "$OUT"; cd "$OUT"
CC=x86_64-w64-mingw32-gcc; CXX=x86_64-w64-mingw32-g++
INC="-I$BBPORT/src -I$BBPORT -I$BBPORT/gpu/shim -I$ROOT/src/host/include -I$ROOT/sdk/include -I$IMGUI -I$SDL/include"
$CC -std=c11 -c "$BBPORT/src/compat_win.c" -I"$BBPORT/src" -o compat_win.o
$CC -std=c11 -Wall -Wextra -Werror -DBB_MODHOST=1 $INC -c "$BBPORT/src/runtime_modhost.c" -o runtime_modhost.o
$CC -std=c11 -Wall -Wextra -Werror $INC -c "$BBPORT/src/runtime_text_input.c" -o runtime_text_input.o
$CXX -std=c++20 -DBB_MODHOST=1 $INC -c "$BBPORT/gpu/shim/bbport_modui.cpp" -o modui.o
for f in imgui imgui_draw imgui_tables imgui_widgets; do $CXX -std=c++20 -I"$IMGUI" -c "$IMGUI/$f.cpp" -o $f.o; done
$CXX -std=c++20 -DBB_MODHOST=1 $INC -c "$HERE/smoke.cpp" -o smoke.o
$CXX -o smoke.exe smoke.o runtime_modhost.o runtime_text_input.o modui.o compat_win.o imgui*.o \
    "$ROOT/build-win/libbbmod_host.a" "$ZYDIS/lib/libZydis.a" "$ZYDIS/lib/libZycore.a" -lbcrypt -pthread -static
WINEPREFIX=${WINEPREFIX:-/tmp/wp} WINEDEBUG=-all ${WINE:-/usr/lib/wine/wine64} smoke.exe \
    "$ROOT/build-win/tests/test_mod.dll" "$ROOT/build-win/tests/test_crash_mod.dll"
