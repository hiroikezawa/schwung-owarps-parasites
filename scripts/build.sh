#!/usr/bin/env bash
# Build the oWarps Parasites module.
#   scripts/build.sh native   desktop owarps_parasites.so + objects for the tests (build/native)
#   scripts/build.sh asan     the same with AddressSanitizer + UBSan (build/asan)
#   scripts/build.sh arm64    Move owarps_parasites.so + install tarball (dist/)
set -euo pipefail
ID=owarps_parasites
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
# Schwung's plugin API headers ship in third_party/schwung (MIT, see its
# LICENSE), unmodified.
API="$MODULE/third_party/schwung"
TARGET="${1:-arm64}"

SAN=()
case "$TARGET" in
  native) CXX="${CXX:-g++}"; CC="${CC:-gcc}"; OUT="$MODULE/build/native" ;;
  asan) CXX="${CXX:-g++}"; CC="${CC:-gcc}"; OUT="$MODULE/build/asan"
        SAN=(-g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer) ;;
  arm64) CXX="${CROSS_PREFIX:-aarch64-linux-gnu-}g++"; CC="${CROSS_PREFIX:-aarch64-linux-gnu-}gcc"; OUT="$MODULE/dist/$ID" ;;
  *) echo "Usage: $0 [native|asan|arm64]" >&2; exit 2 ;;
esac

GEN="$MODULE/build/generated"
OBJ="$MODULE/build/obj-$TARGET"
rm -rf "$OBJ"
# A fresh dist/<id> every time: a file dropped from the package must not ride
# along from an older build.
[[ "$TARGET" == arm64 ]] && rm -rf "$OUT" "$MODULE/dist/$ID-module.tar.gz"
mkdir -p "$OUT" "$GEN" "$OBJ"
python3 "$MODULE/scripts/gen_tables.py" "$GEN/owarps_src_tables.h"
python3 "$MODULE/scripts/metadata.py" "$GEN"

# -DTEST: stmlib's switch that turns off the MCU's ".ramtext" placement.
# -fvisibility=hidden + -Bsymbolic: only move_audio_fx_init_v2 is exported and
#   every internal call binds locally, so another Mutable-based module in the
#   same process can never interpose its warps::/stmlib:: symbols on ours.
# No exceptions/RTTI and linked with the C driver: no libstdc++ at runtime.
# No -ffast-math: the finite guards must survive. -ffp-contract=off keeps the
#   float arithmetic what the source says on both targets.
COMMON=(-O2 -fPIC -fno-strict-aliasing -fno-math-errno -ffp-contract=off -DTEST
        -fvisibility=hidden -fvisibility-inlines-hidden
        -fno-exceptions -fno-rtti -fno-threadsafe-statics
        -I"$MODULE/src" -I"$MODULE/src/shim" -isystem "$MODULE/src/mi" -I"$GEN" -I"$API")
OURS=(-std=gnu++17 -Wall -Wextra -Werror)
# Third-party, compiled as shipped. -fwrapv: the Warps code was written for a
# Cortex-M4, where signed arithmetic wraps (the oscillator's phase-modulation
# sum relies on it).
VENDOR=(-std=gnu++11 -w -fwrapv)
# The sanitizer build checks the vendored code too, minus "shift-base" (the
# bit mangler shifts negative samples left, as the firmware does) and
# "float-cast-overflow" (the Doppler panner stores float sums straight into
# int16 frames; wrapping there is the firmware's behaviour on both ARM
# targets). Shift COUNTS are still checked: that check is what patches/0002's
# Cortex-M4 shift exists for.
[[ "$TARGET" == asan ]] && VENDOR+=(-fno-sanitize=shift-base,float-cast-overflow)

objs=()
compile() {  # compile <flags-array-name> <source>
  local -n flags=$1
  local src=$2 obj
  obj="$OBJ/$(echo "${src#$MODULE/src/}" | tr '/' '_').o"
  "$CXX" "${COMMON[@]}" "${flags[@]}" "${SAN[@]}" -c "$src" -o "$obj"
  objs+=("$obj")
}
for f in "$MODULE"/src/mi/warps/dsp/*.cc "$MODULE"/src/mi/warps/resources.cc \
         "$MODULE"/src/mi/stmlib/dsp/*.cc "$MODULE"/src/mi/stmlib/utils/*.cc; do
  compile VENDOR "$f"
done
for f in "$MODULE"/src/wrapper/*.cpp "$MODULE"/src/plugin.cpp; do
  compile OURS "$f"
done
if [[ "$TARGET" == asan ]]; then   # the sanitizer runtimes come from the test executable
  "$CXX" -shared "${SAN[@]}" -o "$OUT/$ID.so" "${objs[@]}" -Wl,-Bsymbolic -lm
else
  "$CC" -shared -o "$OUT/$ID.so" "${objs[@]}" -Wl,-z,defs -Wl,-Bsymbolic -Wl,--gc-sections -lm
fi

cp "$MODULE/module.json" "$OUT/"
if [[ "$TARGET" == arm64 ]]; then
  python3 "$MODULE/scripts/check_abi.py" "$OUT/$ID.so"
  for f in help.json README.md LICENSE THIRD_PARTY_LICENSES.md; do
    cp "$MODULE/$f" "$OUT/"
  done
  tar -czf "$MODULE/dist/$ID-module.tar.gz" -C "$MODULE/dist" "$ID"
fi
file "$OUT/$ID.so"
