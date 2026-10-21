#!/usr/bin/env bash
# Every test that runs on this machine: upstream provenance, the comparison
# against the original Parasites code, native engine tests (plain and
# ASan/UBSan), UI tests, the Move build with its ABI check, and the licence
# scan. The ARM tests on the Move itself: scripts/bench_move.sh.
#
#   scripts/test.sh            everything
#   scripts/test.sh --renders  also write WAV renders to build/renders
#
# Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
API="$MODULE/third_party/schwung"
RENDERS=""
[[ "${1:-}" == "--renders" ]] && RENDERS="$MODULE/build/renders" && mkdir -p "$RENDERS"

echo "== provenance: src/mi == pinned upstream + patches/"
if ! bash "$MODULE/scripts/fetch_warps.sh" --verify; then
  if git ls-remote https://github.com/mqtthiqs/parasites HEAD >/dev/null 2>&1; then
    exit 1
  fi
  echo "SKIP: GitHub unreachable, provenance not verified" >&2
fi

echo "== the port against the original Parasites code (900 scenarios)"
bash "$MODULE/scripts/build_compare.sh" native
"$MODULE/build/compare-native/firmware-compare" | grep -v "^  MISMATCH"

echo "== native engine tests"
bash "$MODULE/scripts/build.sh" native
"${CXX:-g++}" -std=gnu++17 -O2 -g -Wall -Wextra -I"$API" "$MODULE/tests/test_engine.cpp" \
  -o "$MODULE/build/native/test-engine" -ldl
"$MODULE/build/native/test-engine" "$MODULE/build/native/owarps_parasites.so" $RENDERS

echo "== the same under AddressSanitizer + UBSan"
bash "$MODULE/scripts/build.sh" asan
"${CXX:-g++}" -std=gnu++17 -O1 -g -Wall -Wextra -DOWARPS_NO_ALLOC_HOOK \
  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
  -I"$API" "$MODULE/tests/test_engine.cpp" -o "$MODULE/build/asan/test-engine" -ldl
ASAN_OPTIONS=detect_leaks=1 "$MODULE/build/asan/test-engine" "$MODULE/build/asan/owarps_parasites.so" \
  | grep -E "FAIL|PASSED|failures"

echo "== UI tests (Schwung's real page engine)"
# The page engine: a mirror of the device's shared/ in build/device-shared if
# there is one (scp -r ableton@move.local:/data/UserData/schwung/shared
# build/device-shared/), else the ../schwung checkout. The framebuffer harness
# always comes from ../schwung (or SCHWUNG_ROOT).
python3 "$MODULE/tests/dump_contract.py" "$MODULE/build/native/owarps_parasites.so" \
  "$MODULE/build/native/contract.json"
ROOT="${SCHWUNG_ROOT:-$MODULE/../schwung}"
SHARED="${OW_SHARED:-}"
[[ -z "$SHARED" && -d "$MODULE/build/device-shared/shared" ]] && SHARED="$MODULE/build/device-shared/shared"
if [[ ! -f "$ROOT/tools/param-pages/harness.mjs" ]]; then
  echo "SKIP: no Schwung checkout at $ROOT (set SCHWUNG_ROOT)" >&2
elif command -v node >/dev/null 2>&1; then
  OW_SHARED="$SHARED" SCHWUNG_ROOT="$ROOT" node "$MODULE/tests/test_ui.mjs"
else
  NODE=node.exe; command -v node.exe >/dev/null 2>&1 || NODE="/mnt/c/Program Files/nodejs/node.exe"
  OW_SHARED="${SHARED:+$(wslpath -w "$SHARED")}" SCHWUNG_ROOT="$(wslpath -w "$ROOT")" \
    WSLENV="OW_SHARED:SCHWUNG_ROOT" "$NODE" "$(wslpath -w "$MODULE/tests/test_ui.mjs")"
fi

echo "== Move build (arm64) + ABI"
if command -v "${CROSS_PREFIX:-aarch64-linux-gnu-}g++" >/dev/null 2>&1; then
  bash "$MODULE/scripts/build.sh" arm64
  bash "$MODULE/scripts/build_compare.sh" arm64
else
  echo "SKIP: no aarch64 cross compiler; ABI not checked" >&2
fi

echo "== licences"
python3 "$MODULE/scripts/check_licenses.py"
echo "ALL TESTS PASSED"
