#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SOURCE=${1:-"$ROOT"}
OUT=${2:?usage: run.sh source-root output-directory}
mkdir -p "$OUT"
SDK="$SOURCE/4D-Plugin-SDK/4D Plugin API"
SODIUM="$SOURCE/libsodium-cmake/libsodium/src/libsodium/include"
test -f "$SDK/4DPluginAPI.c"
test -f "$SODIUM/sodium/version.h.in"
mkdir -p "$OUT/include/sodium"
sed -e 's/@VERSION@/1.0.20/g' \
  -e 's/@SODIUM_LIBRARY_VERSION_MAJOR@/26/g' \
  -e 's/@SODIUM_LIBRARY_VERSION_MINOR@/2/g' \
  -e 's/@SODIUM_LIBRARY_MINIMAL_DEF@//g' \
  "$SODIUM/sodium/version.h.in" > "$OUT/include/sodium/version.h"
clang++ -std=c++20 -fsyntax-only -DSODIUM_STATIC -I"$SDK" \
  -I"$SODIUM" -I"$SODIUM/sodium" -I"$OUT/include" "$SOURCE/sodium/sodium-4dplugin.cpp"
clang++ -std=c++20 -fsyntax-only -I"$SDK" \
  "$SOURCE/4D-Plugin-SDK/4D Plugin Wizard/4D Plugin Wizard.4dbase/Resources/Extras/4DPlugin.cpp"
# Boundaries precede all SDK/system include paths. No project build is invoked.
clang++ -std=c++20 -g -O1 -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Wno-int-conversion -Wno-multichar \
  -DSODIUM_STATIC -I"$ROOT/tests/ownership/boundary" -I"$SDK" \
  -I"$SODIUM" -I"$SOURCE/sodium" \
  -x c++ "$SDK/4DPluginAPI.c" "$ROOT/tests/ownership/ownership.cpp" \
  -o "$OUT/ownership"
# The only external imports may be the C/C++ runtime and sanitizer runtimes.
nm -u "$OUT/ownership" > "$OUT/imports.txt"
if grep -E '(_CGContext|_crypto_|_sodium_|_Sec|_CF|_dlopen|_system$|_popen|_socket|_connect|_open$|_fopen)' "$OUT/imports.txt"; then
  echo "Unexpected platform/crypto import" >&2
  exit 1
fi
failures=0
: > "$OUT/results.txt"
for mode in 0 1; do
  for case in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18; do
    log="$OUT/case-$case-empty-$mode.txt"
    if ASAN_OPTIONS=detect_stack_use_after_return=1 UBSAN_OPTIONS=halt_on_error=1 \
      "$OUT/ownership" "$case" "$mode" > "$log" 2>&1; then
      cat "$log" >> "$OUT/results.txt"
    else
      failures=$((failures + 1))
      echo "FAILED case=$case allocated-empty=$mode" >> "$OUT/results.txt"
      cat "$log" >> "$OUT/results.txt"
    fi
  done
done
cat "$OUT/results.txt"
echo "38 cases; $failures failed"
test "$failures" -eq 0
