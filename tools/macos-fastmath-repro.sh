#!/usr/bin/env bash
# =============================================================================
# macos-fastmath-repro.sh - run DSP test TUs under the macOS leg's FP flags, on x86 Linux / WSL, in minutes
# =============================================================================
# The Xcode leg compiles everything with -ffast-math -ffp-contract=fast (extern/vst3sdk/cmake/modules/
# SMTG_PlatformToolset.cmake:46) on a CPU that always has FMA. Windows uses the much milder /fp:fast and Linux has no
# fast-math at all, so a green Windows + Linux run says nothing about macOS floating point. This script builds the
# named dsp/tests/unit TUs with clang and those flags (+ -mfma -mavx2 so contraction can emit FMA, as on arm64), links
# them with every KrateDSP .cpp built the same way, Catch2, pffft and the allocation-detector overrides, and runs them.
#
#   bash tools/macos-fastmath-repro.sh [--ieee] <tu> [<tu> ...] [-- <catch2 args>]
#     <tu>     path under dsp/tests/unit, e.g. systems/profundum_engine_test.cpp
#     --ieee   dsp_ieee_tests' flags, -fno-fast-math -fno-finite-math-only -ffp-contract=off (the control: a case red
#              only without --ieee is a fast-math red). EXTRA_FP=<flags> adds flags to the code under test.
#
#   e.g. (from Windows)  wsl -e bash -lc 'bash /mnt/f/projects/iterum/tools/macos-fastmath-repro.sh \
#                          systems/mass_law_test.cpp -- "MassLaw_SlewBounded"'
#
# Needs clang++ (18 tested) and the FetchContent sources of a Windows configure (DEPS, default
# build/windows-x64-release/_deps: catch2-src, pffft-src, highway-src). Objects are cached per mode in
# $HOME/.krate-fastmath-repro; delete it after a KrateDSP .cpp changes. Test evidence goes to a scratch dir there, not
# into specs/.
#
# LIMITS, measured 2026-10-25: this is x86 upstream clang, not Apple clang on arm64. It reproduced the reassociation,
# contraction and pow-domain reds of that day (HarmonicFold, MassLaw, MonoHandler, MassSplit, AnchorJoin, the
# Fold-100 aliasing grid) and missed one arm64-only red (C1CheckerCrossCheck). Green here is evidence, not proof.
# Plugin tests (VST SDK, VSTGUI) are out of scope: see dsp/CLAUDE.md "Reproducing macOS fast-math reds locally".
# =============================================================================
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DEPS=${DEPS:-$ROOT/build/windows-x64-release/_deps}
MODE=mac
TUS=()
CATCH_ARGS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --ieee) MODE=ieee ;;
    --) shift; CATCH_ARGS=("$@"); break ;;
    *) TUS+=("$1") ;;
  esac
  shift
done
[ ${#TUS[@]} -gt 0 ] || { echo "usage: $0 [--ieee] <tu under dsp/tests/unit> ... [-- <catch2 args>]" >&2; exit 2; }

ARCH="-mfma -mavx2"
if [ "$MODE" = mac ]; then FP="-ffast-math -ffp-contract=fast"; else FP="-fno-fast-math -fno-finite-math-only -ffp-contract=off"; fi
FP="$FP ${EXTRA_FP:-}"   # EXTRA_FP: extra flags for the code under test, e.g. -ffp-contract=on to see contraction
IEEE_FP="-fno-fast-math -fno-finite-math-only -ffp-contract=off"
OUT=$HOME/.krate-fastmath-repro
OBJ=$OUT/$MODE
mkdir -p "$OBJ" "$OUT/shim/catch2/matchers" "$OUT/shim/catch2/generators" "$OUT/evidence/a/b"

CATCH=$DEPS/catch2-src/extras
PFFFT=$DEPS/pffft-src
HWY=$DEPS/highway-src
for d in "$CATCH" "$PFFFT" "$HWY"; do
  [ -d "$d" ] || { echo "missing $d (configure a Windows build first, or set DEPS)" >&2; exit 2; }
done

# Catch2 through its amalgamated header: every <catch2/...> include the tests use resolves to it.
cp -n "$CATCH/catch_amalgamated.hpp" "$OUT/shim/" 2>/dev/null || true
for h in catch_test_macros.hpp catch_message.hpp catch_approx.hpp catch_template_test_macros.hpp \
         matchers/catch_matchers_floating_point.hpp matchers/catch_matchers.hpp generators/catch_generators.hpp; do
  [ -f "$OUT/shim/catch2/$h" ] || printf '#include "catch_amalgamated.hpp"\n' > "$OUT/shim/catch2/$h"
done

INC="-I $OUT/shim -I $PFFFT/include -I $ROOT -I $ROOT/dsp/include -I $ROOT/dsp/tests -I $ROOT/dsp/tests/test_helpers \
     -I $ROOT/dsp/tests/unit/systems -I $ROOT/dsp/tests/unit/processors -I $ROOT/tests -I $ROOT/tests/test_helpers"
# An array, so the string macros keep their quotes.
DEF=(-DNDEBUG -DHWY_COMPILE_ONLY_STATIC=1 -DKRATE_DSP_AETHER_TEST_HOOKS
     "-DKRATE_DSP_TESTS_DIR=\"$OUT/evidence/a/b\"" "-DVORAGO_P1_HARNESS_DIR=\"$OUT/evidence/\"")

build() {  # build <object> <flags...> -- <source>; skips an existing object
  local o=$1; shift
  [ -f "$o" ] && return 0
  local flags=()
  while [ "$1" != -- ]; do flags+=("$1"); shift; done
  shift
  if ! clang++ -std=c++20 -O3 -w "${flags[@]}" "${DEF[@]}" -c "$1" -o "$o" > "$o.log" 2>&1; then
    echo "compile failed: $1 (see $o.log)" >&2; grep -m5 error "$o.log" >&2; rm -f "$o"; return 1
  fi
}

# Support objects. Catch2 and pffft are not code under test: no fast-math.
[ -f "$OUT/catch.o" ] || clang++ -std=c++20 -O2 -w -I "$OUT/shim" -c "$CATCH/catch_amalgamated.cpp" -o "$OUT/catch.o" || exit 1
for c in pffft pffft_common; do
  [ -f "$OUT/$c.o" ] || clang -O2 -w -I "$PFFFT/include" -I "$PFFFT/include/pffft" -I "$PFFFT/src" \
                              -c "$PFFFT/src/$c.c" -o "$OUT/$c.o" || exit 1
done
# The image's one owner of the operator new/delete replacements, so allocation checks count (allocation_detector.h).
printf '#include <allocation_detector.h>\n#include <allocation_operator_overrides.h>\n' > "$OUT/alloc_owner.cpp"
build "$OBJ/alloc_owner.o" $FP $ARCH $INC -- "$OUT/alloc_owner.cpp" || exit 1

# KrateDSP's out-of-line sources (dsp/CMakeLists.txt), with the two CMake -fno-fast-math exceptions kept.
LIBOBJS=()
for s in $(sed -n 's#^\s*\(include/krate/dsp/.*\.cpp\)\s*$#\1#p' "$ROOT/dsp/CMakeLists.txt" | sort -u); do
  o="$OBJ/lib_$(basename "$s" .cpp).o"
  case "$s" in
    */spectral_simd.cpp|*/spectral_state.cpp) build "$o" $IEEE_FP $ARCH -I "$HWY" $INC -- "$ROOT/dsp/$s" || exit 1 ;;
    *) build "$o" $FP $ARCH -I "$HWY" $INC -- "$ROOT/dsp/$s" || exit 1 ;;
  esac
  LIBOBJS+=("$o")
done

# The TUs under test: always rebuilt (they are what changes).
TUOBJS=()
NAME=""
for tu in "${TUS[@]}"; do
  o="$OBJ/tu_$(basename "$tu" .cpp).o"
  rm -f "$o"
  build "$o" $FP $ARCH $INC -- "$ROOT/dsp/tests/unit/$tu" || exit 1
  TUOBJS+=("$o")
  NAME="${NAME}_$(basename "$tu" .cpp)"
done
EXE="$OBJ/run$NAME"
clang++ "${TUOBJS[@]}" "${LIBOBJS[@]}" "$OBJ/alloc_owner.o" "$OUT/catch.o" "$OUT/pffft.o" "$OUT/pffft_common.o" \
        -o "$EXE" > "$EXE.link.log" 2>&1 || { echo "link failed (see $EXE.link.log)" >&2; grep -m5 undefined "$EXE.link.log" >&2; exit 1; }

echo "== $MODE [$FP $ARCH] ${TUS[*]}"
"$EXE" "${CATCH_ARGS[@]}"
