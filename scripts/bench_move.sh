#!/usr/bin/env bash
# Run the ARM tests ON THE MOVE: the firmware comparison (original Parasites
# code vs the port, compiled for AArch64 - the reference keeps its plain ARM
# conversions here), then load + time the real Move build.
#   scripts/bench_move.sh [user@host] [ssh options...]
# Streams the binaries to /data/UserData/schwung/owarps-bench/ (never /tmp:
# the Move's root filesystem is full), runs them on cores 0-2 (core 3 is the
# SPI callback's) and deletes the directory afterwards. Nothing is installed.
#
# Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:-ableton@move.local}"
shift || true
bash "$MODULE/scripts/build.sh" arm64 >/dev/null
bash "$MODULE/scripts/build_compare.sh" arm64 >/dev/null
CC="${CROSS_PREFIX:-aarch64-linux-gnu-}gcc"
OUT="$MODULE/build/bench"
mkdir -p "$OUT"
"$CC" -O2 -std=gnu11 -Wall -Wextra -I"$MODULE/third_party/schwung" "$MODULE/tests/bench_move.c" \
  -o "$OUT/owarps-bench" -ldl -lm
REMOTE=/data/UserData/schwung/owarps-bench
ssh "$@" "$TARGET" "mkdir -p $REMOTE"
# Move has no sftp server: stream the files over ssh instead of scp.
ssh "$@" "$TARGET" "cat > $REMOTE/owarps_parasites.so" < "$MODULE/dist/owarps_parasites/owarps_parasites.so"
ssh "$@" "$TARGET" "cat > $REMOTE/owarps-bench && chmod +x $REMOTE/owarps-bench" < "$OUT/owarps-bench"
ssh "$@" "$TARGET" "cat > $REMOTE/firmware-compare && chmod +x $REMOTE/firmware-compare" \
  < "$MODULE/build/compare-arm64/firmware-compare"
ssh "$@" "$TARGET" "cd $REMOTE && taskset 0x7 ./firmware-compare | grep -v '^  MISMATCH' && \
  taskset 0x7 ./owarps-bench ./owarps_parasites.so; s=\$?; rm -rf $REMOTE; exit \$s"
