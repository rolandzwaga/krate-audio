#!/usr/bin/env bash
# =============================================================================
# macos-fastmath-repro.sh - run DSP test TUs under the macOS leg's FP flags, on x86 Linux / WSL, in minutes
# =============================================================================
# The Xcode leg compiles everything with -ffast-math -ffp-contract=fast (extern/vst3sdk/cmake/modules/
# SMTG_PlatformToolset.cmake:46) on a CPU that always has FMA. Windows uses the much milder /fp:fast and Linux has no
# fast-math at all, so a green Windows + Linux run says nothing about macOS floating point. This script builds the
# named dsp/tests/unit TUs with clang and those flags (+ -mfma -mavx2 so contraction can emit FMA, as on arm64), links
# them with every KrateDSP .cpp built the same way, the project's test main (dsp_test_main.cpp: FTZ / DAZ on), Catch2,
# pffft, Highway and the allocation-detector overrides, and runs them.
#
#   bash tools/macos-fastmath-repro.sh [--ieee] <tu> [<tu> ...] [-- <catch2 args>]
#     <tu>     path under dsp/tests/unit, e.g. systems/profundum_engine_test.cpp
#     --ieee   the control build: -fno-fast-math -fno-finite-math-only -ffp-contract=off everywhere, KrateDSP included
#              (a case red only without --ieee is a fast-math red).
#     EXTRA_FP=<flags>  extra flags for the code under test. They come AFTER the CMake flags of the TUs under test, so
#              they can override them (EXTRA_FP="-ffast-math -ffp-contract=fast" builds an IEEE-listed TU fast-math, the
#              script's behaviour before 2026-11-11), and before KrateDSP's per-source IEEE exceptions.
#     NODE=<path>  node binary for the CMake reader (default: node on PATH, else the newest ~/.nvm one).
#
#   e.g. (from Windows)  wsl -e bash -lc 'bash /mnt/f/projects/iterum/tools/macos-fastmath-repro.sh \
#                          systems/mass_law_test.cpp -- "MassLaw_SlewBounded"'
#
# WHAT CMAKE DECIDES (read by tools/macos-fastmath-repro-cmake.js, whose header lists what it cannot model):
#   - per-TU flags: the TU's executable's target options (dsp_ieee_tests is IEEE as a whole), then its own
#     set_source_files_properties COMPILE_FLAGS (the IEEE list, the -O2 caps), after the global fast-math flags, in
#     CMake's command-line order; KrateDSP's sources get dsp/CMakeLists.txt's per-source flags the same way.
#   - per-TU definitions and -I for Highway, from the executable's target_compile_definitions / target_link_libraries.
#   - sibling TUs: when the link misses a symbol (vorago_lever_bounded_part*_test.cpp calls into
#     vorago_ecosystem_lever_test.cpp), the executable's other sources that name it are compiled until one defines it;
#     their own test cases are excluded with Catch2's -# filename tags (~[#<sibling>] appended to the catch2 args, so a
#     comma-OR test spec only excludes them from its last alternative).
#   - the allocation overrides: one owner per image, as in CMake. A TU that defines them (brownian_drift_test.cpp,
#     selectable_oscillator_test.cpp, aether_reverb_test.cpp) is the owner, else the script's alloc_owner.o. Two
#     owners (TUs of different executables in one run) stop the script; run them separately.
#   - KRATE_DSP_TESTS_DIR points at $HOME/.krate-fastmath-repro/tree/dsp/tests, a mirror of the real dsp/tests (its
#     entries and ../include are symlinks, so committed fixtures and header scans read the real files) whose
#     ../../specs/*/evidence is an rsync copy refreshed every run: tests read committed evidence, and the evidence
#     they write stays in the mirror, never in specs/. VORAGO_P1_HARNESS_DIR goes to the mirror's build/ too.
#
# Needs clang++ (18 tested), nm, rsync, node, and the FetchContent sources of a Windows configure (DEPS, default
# build/windows-x64-release/_deps: catch2-src, pffft-src, highway-src). Objects are cached in $HOME/.krate-fastmath-repro
# under a hash of their recipe and rebuilt when a dependency (clang's -MMD list) is newer; the TUs under test and their
# siblings are always rebuilt.
#
# LIMITS, measured 2026-10-25: this is x86 upstream clang, not Apple clang on arm64. It reproduced the reassociation,
# contraction and pow-domain reds of that day (HarmonicFold, MassLaw, MonoHandler, MassSplit, AnchorJoin, the
# Fold-100 aliasing grid) and missed one arm64-only red (C1CheckerCrossCheck). Green here is evidence, not proof.
# Each TU is linked alone (with any sibling), not with the rest of its executable, so a COMDAT copy chosen from another
# TU of the CMake image is not modelled. Plugin tests (VST SDK, VSTGUI) are out of scope: see dsp/CLAUDE.md
# "Reproducing macOS fast-math reds locally".
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
IEEE_FP="-fno-fast-math -fno-finite-math-only -ffp-contract=off"
if [ "$MODE" = mac ]; then FP="-ffast-math -ffp-contract=fast"; else FP="$IEEE_FP"; fi
EXTRA=${EXTRA_FP:-}   # EXTRA_FP: extra flags for the code under test, e.g. -ffp-contract=on to see contraction
OUT=$HOME/.krate-fastmath-repro
OBJ=$OUT/$MODE
TREE=$OUT/tree
mkdir -p "$OBJ" "$OUT/shim" "$TREE/dsp/tests" "$TREE/specs" "$TREE/build/vorago_p1"

CATCH=$DEPS/catch2-src
PFFFT=$DEPS/pffft-src
HWY=$DEPS/highway-src
for d in "$CATCH" "$PFFFT" "$HWY"; do
  [ -d "$d" ] || { echo "missing $d (configure a Windows build first, or set DEPS)" >&2; exit 2; }
done
NODE=${NODE:-$(command -v node || ls -1d "$HOME"/.nvm/versions/node/*/bin/node 2>/dev/null | sort -V | tail -1)}
[ -n "$NODE" ] && [ -x "$NODE" ] || { echo "no node found (set NODE=<path>)" >&2; exit 2; }
for t in clang++ nm rsync; do command -v $t > /dev/null || { echo "missing $t" >&2; exit 2; }; done

# ---- the KRATE_DSP_TESTS_DIR mirror (see the header) ----------------------------------------------------------------
# Links are replaced only when wrong: ln -sf unlinks first, and a parallel invocation's test may be reading through it.
relink() { [ "$(readlink "$2")" = "$1" ] || ln -sfn "$1" "$2"; }
relink "$ROOT/dsp/include" "$TREE/dsp/include"
for e in "$ROOT"/dsp/tests/*; do relink "$e" "$TREE/dsp/tests/$(basename "$e")"; done
(cd "$ROOT/specs" && rsync -aR ./*/evidence "$TREE/specs/") || { echo "evidence sync into $TREE/specs failed" >&2; exit 2; }

# ---- the CMake recipe ------------------------------------------------------------------------------------------------
CM="$ROOT/tools/macos-fastmath-repro-cmake.js"
LIBREC=$("$NODE" "$CM" lib) || exit 2
TUREC=$("$NODE" "$CM" tu "${TUS[@]}" --var "CMAKE_CURRENT_SOURCE_DIR=$TREE/dsp/tests" --var "CMAKE_BINARY_DIR=$TREE/build") || exit 2
declare -A EXE_OF TU_OPTS TU_DEFS TU_HWY SIB_OPTS
declare -A SIBS_OF   # exe -> newline-separated sibling candidates
while IFS=$'\t' read -r kind key val; do
  case "$kind" in
    exe) EXE_OF[$key]=$val ;;
    opt) TU_OPTS[$key]+="$val"$'\n' ;;
    def) TU_DEFS[$key]+="-D$val"$'\n' ;;
    lib) [ "$val" = hwy ] && TU_HWY[$key]=1 ;;
    sib) [ -n "${EXE_OF[$key]:-}" ] && [ "${EXE_OF[$key]}" != - ] && SIBS_OF[${EXE_OF[$key]}]+="$val"$'\n' ;;
    sibopt) SIB_OPTS[$key]+="$val"$'\n' ;;
    warn) echo "!! $key: $val" >&2 ;;
  esac
done <<< "$TUREC"
EXES=$(for tu in "${TUS[@]}"; do echo "${EXE_OF[$tu]}"; done | sort -u)
[ "$(echo "$EXES" | wc -l)" -gt 1 ] && echo "!! the TUs come from $(echo $EXES | wc -w) executables ($(echo $EXES)); CMake never links them together" >&2

# Catch2 through its amalgamated header: every <catch2/...> header resolves to it.
# Written to a temporary name and renamed: a parallel invocation may be compiling against them.
put() { [ -f "$2" ] || { mkdir -p "$(dirname "$2")"; printf '%s\n' "$1" > "$2.tmp$$" && mv -f "$2.tmp$$" "$2"; }; }
[ -f "$OUT/shim/catch_amalgamated.hpp" ] || { cp "$CATCH/extras/catch_amalgamated.hpp" "$OUT/shim/c.tmp$$" && mv -f "$OUT/shim/c.tmp$$" "$OUT/shim/catch_amalgamated.hpp"; }
(cd "$CATCH/src" && find catch2 -name '*.hpp') | while read -r h; do put '#include "catch_amalgamated.hpp"' "$OUT/shim/$h"; done

INC="-I $OUT/shim -I $PFFFT/include -I $ROOT -I $ROOT/dsp/include -I $ROOT/dsp/tests -I $ROOT/dsp/tests/test_helpers \
     -I $ROOT/dsp/tests/unit/systems -I $ROOT/dsp/tests/unit/processors -I $ROOT/tests -I $ROOT/tests/test_helpers"
DEF=(-DNDEBUG -DHWY_STATIC_DEFINE)

WARN=-w   # warning flags of build(); the TUs under test get the fast-math UB warnings, see below
stale() {  # stale <object>: missing, or a dependency in its -MMD list is newer
  local o=$1 dep
  [ -f "$o" ] && [ -f "$o.d" ] || return 0
  for dep in $(sed -e 's/\\$//' -e 's/^[^:]*://' "$o.d"); do [ "$dep" -nt "$o" ] && return 0; done
  return 1
}
# build <object> <flags...> -- <source>. A cached object (CACHED=1) carries a hash of its recipe in its name and is
# rebuilt only when stale(); the result is renamed into place, so parallel invocations never link a half-written one.
build() {
  local o=$1; shift
  local flags=()
  while [ "$1" != -- ]; do flags+=("$1"); shift; done
  shift
  local src=$1 cxx=clang++ std=-std=c++20
  case "$src" in *.c) cxx=clang; std= ;; esac
  if [ "${CACHED:-0}" = 1 ]; then
    o="${o%.o}-$(printf '%s\n' "$cxx $std $WARN ${flags[*]} ${DEF[*]} $src" | md5sum | cut -c1-10).o"
    BUILT=$o
    stale "$o" || return 0
  fi
  BUILT=$o
  if ! $cxx $std -O3 $WARN "${flags[@]}" "${DEF[@]}" -MMD -MF "$o.d" -c "$src" -o "$o.tmp$$" > "$o.log" 2>&1; then
    echo "compile failed: $src (see $o.log)" >&2; grep -m5 error "$o.log" >&2; rm -f "$o.tmp$$"; return 1
  fi
  mv -f "$o.tmp$$" "$o"
}
lines() { printf '%s' "$1" | sed '/^$/d'; }   # newline list -> lines, for mapfile

# ---- support objects. Catch2, pffft and Highway are not code under test: no fast-math. --------------------------------
SUPPORT=()
CACHED=1 build "$OUT/catch_nomain.o" -O2 -I "$OUT/shim" -DCATCH_AMALGAMATED_CUSTOM_MAIN -- "$CATCH/extras/catch_amalgamated.cpp" || exit 1
SUPPORT+=("$BUILT")
for c in pffft pffft_common; do
  CACHED=1 build "$OUT/$c.o" -O2 -I "$PFFFT/include" -I "$PFFFT/include/pffft" -I "$PFFFT/src" -- "$PFFFT/src/$c.c" || exit 1
  SUPPORT+=("$BUILT")
done
# Highway as CMake links it (dynamic dispatch, KrateDSP links it PRIVATE): an archive, so only what is used is pulled.
HWYOBJS=()
for s in "$HWY"/hwy/*.cc; do
  case "$s" in *_test.cc) continue ;; esac
  CACHED=1 build "$OUT/hwy_$(basename "$s" .cc).o" -O2 $ARCH -I "$HWY" -- "$s" || exit 1
  HWYOBJS+=("$BUILT")
done
HWYLIB="$OUT/libhwy-$(printf '%s\n' "${HWYOBJS[@]}" | md5sum | cut -c1-10).a"
hwy_fresh=1
for x in "${HWYOBJS[@]}"; do [ -f "$HWYLIB" ] && [ ! "$x" -nt "$HWYLIB" ] || hwy_fresh=0; done
[ $hwy_fresh = 1 ] || { rm -f "$HWYLIB.tmp$$" && ar rcs "$HWYLIB.tmp$$" "${HWYOBJS[@]}" && mv -f "$HWYLIB.tmp$$" "$HWYLIB"; } || exit 1
# The project's test main: Catch2 session + FTZ / DAZ, exactly as every dsp_*_tests executable.
CACHED=1 build "$OBJ/dsp_test_main.o" $FP $ARCH $INC -- "$ROOT/dsp/tests/dsp_test_main.cpp" || exit 1
SUPPORT+=("$BUILT")
# The image's owner of the operator new/delete replacements when no linked TU owns them (allocation_detector.h).
put $'#include <allocation_detector.h>\n#include <allocation_operator_overrides.h>' "$OUT/alloc_owner.cpp"
CACHED=1 build "$OBJ/alloc_owner.o" $FP $ARCH $INC -- "$OUT/alloc_owner.cpp" || exit 1
ALLOC_OWNER=$BUILT

# ---- KrateDSP's out-of-line sources, each with its dsp/CMakeLists.txt per-source flags ------------------------------
LIBOBJS=()
declare -A LIB_OPTS
LIB_SRCS=()
while IFS=$'\t' read -r kind key val; do
  case "$kind" in src) LIB_SRCS+=("$key") ;; opt) LIB_OPTS[$key]+="$val"$'\n' ;; esac
done <<< "$LIBREC"
for s in "${LIB_SRCS[@]}"; do
  mapfile -t opts < <(lines "${LIB_OPTS[$s]:-}")
  CACHED=1 build "$OBJ/lib_$(basename "$s" .cpp).o" $FP $EXTRA $ARCH -I "$HWY" $INC "${opts[@]}" -- "$s" || exit 1
  LIBOBJS+=("$BUILT")
done

# ---- the TUs under test: always rebuilt (they are what changes). In mac mode clang reports every infinity / NaN
# constant or comparison that -ffinite-math-only makes undefined behaviour (-Wnan-infinity-disabled): each is a
# candidate macOS-only red (BassStereoField_TransitionsClickFreeSmoke's infinity() search seed, 2026-10-25).
[ "$MODE" = mac ] && WARN="-Wno-everything -Wnan-infinity-disabled"
FALLBACK_DEFS="-DKRATE_DSP_AETHER_TEST_HOOKS"$'\n'"-DKRATE_DSP_TESTS_DIR=\"$TREE/dsp/tests\""$'\n'"-DVORAGO_P1_HARNESS_DIR=\"$TREE/build/vorago_p1/\""
# compile_tu <object> <source> <exe-key tu> <extra per-source opts>: build flags of a TU of <exe-key tu>'s executable.
compile_tu() {
  local o=$1 src=$2 key=$3 srcopts=$4 opts defs hwyinc=
  mapfile -t opts < <(lines "$srcopts")
  if [ "${EXE_OF[$key]:--}" = - ]; then mapfile -t defs < <(lines "$FALLBACK_DEFS"); else mapfile -t defs < <(lines "${TU_DEFS[$key]:-}"); fi
  [ "${TU_HWY[$key]:-0}" = 1 ] || [ "${EXE_OF[$key]:--}" = - ] && hwyinc="-I $HWY"
  rm -f "$o"
  build "$o" $FP $ARCH $INC $hwyinc "${defs[@]}" "${opts[@]}" $EXTRA -- "$src"
}
report_nan() {  # report_nan <object> <source>
  local o=$1 src=$2
  if grep -q "nan-infinity-disabled" "$o.log"; then
    echo "!! ${src#$ROOT/dsp/tests/unit/}: $(grep -c "nan-infinity-disabled" "$o.log") use(s) of infinity / NaN that -ffast-math makes undefined (see $o.log):"
    grep "nan-infinity-disabled" "$o.log" | sed "s#^$ROOT/##" | head -10
  fi
}
TUOBJS=()
NAME=""
declare -A TU_SRC
for tu in "${TUS[@]}"; do
  o="$OBJ/tu_$(basename "$tu" .cpp).o"
  echo "   $tu: ${EXE_OF[$tu]:--} [$FP $ARCH $(lines "${TU_OPTS[$tu]:-}" | tr '\n' ' ')$EXTRA]"
  compile_tu "$o" "$ROOT/dsp/tests/unit/$tu" "$tu" "${TU_OPTS[$tu]:-}" || exit 1
  report_nan "$o" "$ROOT/dsp/tests/unit/$tu"
  TUOBJS+=("$o")
  TU_SRC[$ROOT/dsp/tests/unit/$tu]=1
  NAME="${NAME}_$(basename "$tu" .cpp)"
done
EXE="$OBJ/run$NAME"

# ---- link: one allocation owner, and siblings for symbols another TU of the executable defines ------------------------
SIBOBJS=()
SIBNAMES=()
link() {
  local owners=() extra=() obj
  # grep without -q: under pipefail an early exit gives nm SIGPIPE and fails the test.
  for obj in "${TUOBJS[@]}" "${SIBOBJS[@]}"; do nm --defined-only "$obj" 2>/dev/null | grep ' T _Znwm$' > /dev/null && owners+=("$obj"); done
  if [ ${#owners[@]} -gt 1 ]; then
    echo "link failed: ${#owners[@]} objects own the operator new/delete overrides (${owners[*]##*/}); CMake has one per executable, run them separately" >&2
    exit 1
  fi
  [ ${#owners[@]} -eq 0 ] && extra=("$ALLOC_OWNER")
  clang++ "${TUOBJS[@]}" "${SIBOBJS[@]}" "${LIBOBJS[@]}" "${extra[@]}" "${SUPPORT[@]}" "$HWYLIB" -o "$EXE" > "$EXE.link.log" 2>&1
}
for round in 1 2 3 4; do
  link && break
  missing=$(sed -n "s/.*undefined reference to \`\(.*\)'$/\1/p" "$EXE.link.log" | sort -u)
  if [ -z "$missing" ] || [ $round = 4 ]; then
    echo "link failed (see $EXE.link.log)" >&2; grep -m5 "undefined\|multiple definition" "$EXE.link.log" >&2; exit 1
  fi
  # Identifiers to look for: the unqualified name of each missing symbol (args and template arguments dropped).
  ids=$(printf '%s\n' "$missing" | sed -e 's/(.*//' -e 's/<.*//' -e 's/.*:://' | sort -u)
  added=0
  for exe in $EXES; do
    [ "$exe" = - ] && continue
    mapfile -t cands < <(lines "${SIBS_OF[$exe]:-}" | sort -u)
    [ ${#cands[@]} -gt 0 ] || continue
    mapfile -t hits < <(printf '%s\n' "$ids" | grep -lwF -f - -- "${cands[@]}" 2>/dev/null)
    for src in "${hits[@]}"; do
      [ -n "${TU_SRC[$src]:-}" ] && continue
      [ -n "$missing" ] || break
      TU_SRC[$src]=1
      key=$(for tu in "${TUS[@]}"; do [ "${EXE_OF[$tu]}" = "$exe" ] && { echo "$tu"; break; }; done)
      so="$OBJ/sib_$(basename "$src" .cpp).o"
      compile_tu "$so" "$src" "$key" "${SIB_OPTS[$src]:-}" || exit 1
      defined=$(nm -C --defined-only "$so" | cut -d' ' -f3-)
      if printf '%s\n' "$defined" | grep -xF -f <(printf '%s\n' "$missing") > /dev/null; then
        echo "   sibling ${src#$ROOT/dsp/tests/unit/} (defines what the link missed; its own cases are excluded)"
        report_nan "$so" "$src"
        SIBOBJS+=("$so"); SIBNAMES+=("$(basename "$src" .cpp)"); added=1
        missing=$(printf '%s\n' "$missing" | grep -vxF -f <(printf '%s\n' "$defined"))
      fi
    done
  done
  [ $added = 1 ] || { echo "link failed (see $EXE.link.log); no source of $EXES defines:" >&2; printf '  %s\n' "$missing" >&2; exit 1; }
done
if [ ${#SIBNAMES[@]} -gt 0 ]; then
  CATCH_ARGS+=(-#)
  for s in "${SIBNAMES[@]}"; do CATCH_ARGS+=("~[#$s]"); done
fi

echo "== $MODE [$FP $ARCH${EXTRA:+ $EXTRA}] ${TUS[*]}"
"$EXE" "${CATCH_ARGS[@]}"
