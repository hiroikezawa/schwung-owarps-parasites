#!/usr/bin/env bash
# Where src/mi came from, as a command.
#
#   scripts/fetch_warps.sh --verify   download the pinned upstream commits,
#                                     apply patches/, and diff against the
#                                     vendored tree (exit 1 on any difference)
#   scripts/fetch_warps.sh --update   the same, but overwrite the vendored tree
#   scripts/fetch_warps.sh --pristine DIR
#                                     write the UNPATCHED upstream files to DIR
#                                     (tests/firmware_compare.cpp builds the
#                                     original code from there)
#
# Taken: the Warps DSP of Matthias Puech's Parasites firmware (warps/dsp/**,
# warps/resources.*) and the stmlib files it includes, at the stmlib commit
# Parasites pins as its submodule. No drivers, UI, CV scaling, settings,
# bootloader or hardware files (the CV-scaling arithmetic the module needs is
# transcribed in src/wrapper/cv_reader.h and compared against the original
# cv_scaler.cc by the tests). Every file is MIT, "Copyright Emilie Gillet"
# (Parasites' Warps changes are by Matthias Puech in the same files, under the
# same notice). See THIRD_PARTY_LICENSES.md.
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
PARASITES_COMMIT=32fa66f5acce1bff2f0a7bdd041e29bad3222557
STMLIB_COMMIT=8ab2aaee77cbacb47b646d46d22ee5d358effe2d   # parasites' submodule pin
WARPS_FILES=(
  dsp/filter_bank.cc dsp/filter_bank.h dsp/limiter.h dsp/modulator.cc
  dsp/modulator.h dsp/oscillator.cc dsp/oscillator.h dsp/parameters.h
  dsp/quadrature_oscillator.h dsp/quadrature_transform.h
  dsp/sample_rate_conversion_filters.h dsp/sample_rate_converter.h
  dsp/vocoder.cc dsp/vocoder.h resources.cc resources.h)
# Only for the firmware comparison test (never compiled into the module).
WARPS_TEST_FILES=(cv_scaler.cc cv_scaler.h settings.h)
STMLIB_FILES=(stmlib.h dsp/dsp.h dsp/filter.h dsp/parameter_interpolator.h
  dsp/units.h dsp/units.cc dsp/cosine_oscillator.h utils/random.h
  utils/random.cc LICENSE)

mode="${1:---verify}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

git -C "$work" init -q parasites
git -C "$work/parasites" fetch -q --depth 1 https://github.com/mqtthiqs/parasites "$PARASITES_COMMIT"
git -C "$work/parasites" checkout -q FETCH_HEAD
git -C "$work" init -q stmlib
git -C "$work/stmlib" fetch -q --depth 1 https://github.com/pichenettes/stmlib "$STMLIB_COMMIT"
git -C "$work/stmlib" checkout -q FETCH_HEAD

out="$work/mi"
for f in "${WARPS_FILES[@]}"; do
  mkdir -p "$(dirname "$out/warps/$f")"
  cp "$work/parasites/warps/$f" "$out/warps/$f"
done
for f in "${STMLIB_FILES[@]}"; do
  mkdir -p "$(dirname "$out/stmlib/$f")"
  cp "$work/stmlib/$f" "$out/stmlib/$f"
done

if [[ "$mode" == "--pristine" ]]; then
  dest="${2:?--pristine needs a directory}"
  rm -rf "$dest"
  mkdir -p "$dest"
  cp -r "$out/." "$dest/"
  for f in "${WARPS_TEST_FILES[@]}"; do cp "$work/parasites/warps/$f" "$dest/warps/$f"; done
  exit 0
fi

for p in "$MODULE"/patches/*.patch; do
  [[ -e "$p" ]] || continue
  patch -s -p1 -d "$out" < "$p"
done
find "$out" -name '*.orig' -delete

case "$mode" in
  --update)
    rm -rf "$MODULE/src/mi"
    cp -r "$out" "$MODULE/src/mi"
    echo "src/mi updated from parasites $PARASITES_COMMIT + stmlib $STMLIB_COMMIT + patches/"
    ;;
  --verify)
    if diff -r "$out" "$MODULE/src/mi"; then
      echo "PASS: src/mi == parasites $PARASITES_COMMIT + stmlib $STMLIB_COMMIT + $(ls "$MODULE"/patches/*.patch 2>/dev/null | wc -l) patches"
    else
      echo "FAIL: src/mi differs from upstream + patches/" >&2
      exit 1
    fi
    ;;
  *) echo "Usage: $0 [--verify|--update|--pristine DIR]" >&2; exit 2 ;;
esac
