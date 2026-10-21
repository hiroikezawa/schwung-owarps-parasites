#!/usr/bin/env bash
# Build tests/firmware_compare (native, or arm64 for running on the Move).
#   scripts/build_compare.sh [native|arm64]   -> build/compare-<target>
# build/reference = the pinned upstream Parasites/stmlib files (fetched with
# scripts/fetch_warps.sh --pristine, cached in build/pristine) with the edits
# below, each of which makes it compute what the MODULE computes rather than
# what this build machine would (see tests/firmware_compare.cpp).
#
# Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
TARGET="${1:-native}"
case "$TARGET" in
  native) CXX="${CXX:-g++}"; STATIC=() ;;
  arm64) CXX="${CROSS_PREFIX:-aarch64-linux-gnu-}g++"; STATIC=(-static) ;;
  *) echo "Usage: $0 [native|arm64]" >&2; exit 2 ;;
esac
[[ -d "$MODULE/build/pristine/warps" ]] || bash "$MODULE/scripts/fetch_warps.sh" --pristine "$MODULE/build/pristine"
REF="$MODULE/build/reference"
rm -rf "$REF"
cp -r "$MODULE/build/pristine" "$REF"
python3 - "$REF" "$TARGET" <<'PY'
import sys
ref, target = sys.argv[1], sys.argv[2]
def edit(path, old, new, count=1):
    s = open(path).read()
    assert s.count(old) == count, (path, old)
    open(path, "w").write(s.replace(old, new))
mod = ref + "/warps/dsp/modulator.cc"
# 1. The bit mangler's negative shift count, as the Cortex-M4 answers it.
edit(mod, "  ops[3] = static_cast<float>(x_1_mod << (x_2_mod >> 12)) / 32768.0f;",
     "  { int32_t c_ = (x_2_mod >> 12) & 0xff;  /* Cortex-M4 LSL by register */\n"
     "    ops[3] = static_cast<float>(c_ >= 32 ? 0 : (int32_t)((uint32_t)x_1_mod << c_)) / 32768.0f; }")
# 2. The delay's read position past the end of the tape: the firmware reads
#    whatever memory follows its buffer, which no two builds share.
edit(mod, "    while (index < 0) {\n      index += DELAY_SIZE;\n    }\n",
     "    while (index < 0) {\n      index += DELAY_SIZE;\n    }\n"
     "    while (index >= DELAY_SIZE) {\n      index -= DELAY_SIZE;\n    }\n")
# 3. On a desktop CPU only: float -> int32 conversions of NaN and of
#    out-of-range values as both ARM targets (the module's Cortex-M4, the
#    Move's AArch64) perform them - saturating, NaN -> 0 - instead of x86's
#    INT32_MIN. On arm64 the reference keeps its plain casts.
if target != "arm64":
    helper = ("static inline int RefArmCvt(float x) { if (x != x) return 0; "
              "if (x >= 2147483648.0f) return 2147483647; "
              "if (x <= -2147483648.0f) return -2147483647 - 1; return (int)x; }\n")
    s = open(mod).read()
    s = s.replace("Clip16(static_cast<int32_t>(", "Clip16(RefArmCvt(")
    for a in ["(s.l) * 32768.0f", "(s.r) * 32768.0f",
              "(fade_out * in.l + fade_in * in.r) * 32768.0f", "wet.r * 32768.0f",
              "(fade_out * in.l + fade_in * wet.l) * 32768.0f",
              "(fade_out * in.r + fade_in * wet.r) * 32768.0f"]:
        assert s.count("Clip16(" + a + ")") == 1, a
        s = s.replace("Clip16(" + a + ")", "Clip16(RefArmCvt(" + a + "))")
    open(mod, "w").write(s)
    osc = ref + "/warps/dsp/oscillator.cc"
    s = open(osc).read()
    s = s.replace("static_cast<int32_t>(*modulation++ * kToUint32) * 4",
                  "RefArmCvt(*modulation++ * kToUint32) * 4")
    open(osc, "w").write(s)
    dsp = ref + "/stmlib/dsp/dsp.h"
    s = open(dsp).read()
    s = s.replace("namespace stmlib {", helper + "namespace stmlib {", 1)
    s = s.replace("return Clip16(static_cast<int32_t>(SoftLimit(x * 0.5f) * 32768.0f));",
                  "return Clip16(RefArmCvt(SoftLimit(x * 0.5f) * 32768.0f));")
    open(dsp, "w").write(s)
PY
OUT="$MODULE/build/compare-$TARGET"
rm -rf "$OUT"
mkdir -p "$OUT/ref" "$OUT/port"
cd "$OUT/ref"
FLAGS=(-O2 -DTEST -fwrapv -ffp-contract=off -fno-strict-aliasing -w)
"$CXX" -std=gnu++11 "${FLAGS[@]}" -Dwarps=warps_ref -Dstmlib=stmlib_ref \
  -I"$MODULE/tests/firmware_stubs" -I"$REF" \
  "$MODULE/tests/compare_reference.cpp" "$REF"/warps/cv_scaler.cc "$REF"/warps/dsp/*.cc \
  "$REF"/warps/resources.cc "$REF"/stmlib/dsp/units.cc "$REF"/stmlib/utils/random.cc -c
cd "$OUT/port"
"$CXX" -std=gnu++11 "${FLAGS[@]}" -I"$MODULE/src" -I"$MODULE/src/shim" -isystem "$MODULE/src/mi" \
  -I"$MODULE/build/generated" \
  "$MODULE/tests/compare_port.cpp" "$MODULE"/src/mi/warps/dsp/*.cc "$MODULE"/src/mi/warps/resources.cc \
  "$MODULE"/src/mi/stmlib/dsp/units.cc "$MODULE"/src/mi/stmlib/utils/random.cc -c
"$CXX" -std=gnu++11 -O2 "${STATIC[@]}" "$MODULE/tests/firmware_compare.cpp" \
  "$OUT"/ref/*.o "$OUT"/port/*.o -o "$OUT/firmware-compare" -lm
echo "built $OUT/firmware-compare"
