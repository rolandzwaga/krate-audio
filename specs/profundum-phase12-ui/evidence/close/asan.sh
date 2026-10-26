#!/usr/bin/env bash
# Profundum Phase 12, task T036 close lane: the [lifecycle] cases under the local ASan configuration (SC-016: "One ASan
# run (-DENABLE_ASAN=ON, Debug) of profundum_tests "[lifecycle]" is clean, and its log is recorded").
# Copied from specs/profundum-phase10-plugin-scaffold/evidence/close/asan.sh, with the configure step and the Phase 12
# [lifecycle] case set.
#
# Run from the main loop CONCURRENTLY with suites.sh and tidy.sh (separate build dir; Git Bash, from anywhere):
#   bash specs/profundum-phase12-ui/evidence/close/asan.sh
# Steps:
#   1. Configure build-asan: "$CMAKE" -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON (the
#      generator of an existing cache is reused; a cache that does not read ENABLE_ASAN:BOOL=ON afterwards is RED).
#   2. "$CMAKE" --build build-asan --config Debug --target profundum_tests (-- /nr:false) to asan-build.log.
#   3. build-asan/bin/Debug/profundum_tests.exe "[lifecycle]" -> exit 0, every expected [lifecycle] case listed, and no
#      "AddressSanitizer" line.
# The MSVC ASan runtime (clang_rt.asan_dynamic-x86_64.dll) is not copied next to the exe; the directory of the cl.exe
# that configured build-asan is prepended to PATH so the exe loads the matching runtime.
# Output: evidence/run-log/close/asan.log (run output in asan-run.log); last line "ASAN GREEN|RED ...". Exit 0 only
# when green.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase12-ui"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
ASAN_DIR="build-asan"
EXE="$ASAN_DIR/bin/Debug/profundum_tests.exe"
FILTER="[lifecycle]"
# The [lifecycle] cases at cut time: Phase 10's editor lifecycle (amended by Phase 12, SC-016), the Linux sync timer
# (SC-015) and the DataExchange handler lifecycle (SC-023).
CASES="Profundum_EditorLifecycle
Profundum_EditorSyncTimer
Profundum_SpectrumFrameHandlerLifecycle"
LOGDIR="$SPEC/evidence/run-log/close"
LOG="$LOGDIR/asan.log"
CFG_LOG="$LOGDIR/asan-configure.log"
BUILD_LOG="$LOGDIR/asan-build.log"
RUN_LOG="$LOGDIR/asan-run.log"

mkdir -p "$LOGDIR"
start="$(date +%s)"
echo "=== T036 asan $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)" > "$LOG"

# ---------------------------------------------------------------------------------------------------------------
# 1. Configure (Debug is chosen at build time; the VS generator is multi-config).
# ---------------------------------------------------------------------------------------------------------------
GENERATOR="$(grep -E '^CMAKE_GENERATOR:INTERNAL=' "$ASAN_DIR/CMakeCache.txt" 2>/dev/null | head -n 1 | cut -d= -f2- | tr -d '\r')"
[ -z "$GENERATOR" ] && GENERATOR="Visual Studio 17 2022"
c0="$(date +%s)"
"$CMAKE" -S . -B "$ASAN_DIR" -G "$GENERATOR" -A x64 -DENABLE_ASAN=ON > "$CFG_LOG" 2>&1
cfg_rc=$?
c1="$(date +%s)"
{
    echo "--- configure (generator \"$GENERATOR\", -DENABLE_ASAN=ON) exit=$cfg_rc wall=$((c1 - c0))s log=$CFG_LOG"
    tr -d '\r' < "$CFG_LOG" | tail -n 3
} >> "$LOG"
if [ "$cfg_rc" -ne 0 ]; then
    echo "ASAN RED: configure failed (exit $cfg_rc); see $CFG_LOG" | tee -a "$LOG"
    exit 1
fi
if ! grep -qE '^ENABLE_ASAN:BOOL=ON' "$ASAN_DIR/CMakeCache.txt" 2>/dev/null; then
    echo "ASAN RED: $ASAN_DIR/CMakeCache.txt does not read ENABLE_ASAN:BOOL=ON after configure" | tee -a "$LOG"
    exit 1
fi

# ---------------------------------------------------------------------------------------------------------------
# 2. Build.
# ---------------------------------------------------------------------------------------------------------------
b0="$(date +%s)"
"$CMAKE" --build "$ASAN_DIR" --config Debug --target profundum_tests -- /nr:false > "$BUILD_LOG" 2>&1
build_rc=$?
b1="$(date +%s)"
build_warn="$(grep -cE "warning C[0-9]|warning:" "$BUILD_LOG" || true)"
{
    echo "--- build exit=$build_rc wall=$((b1 - b0))s warnings=${build_warn:-0} log=$BUILD_LOG"
    tr -d '\r' < "$BUILD_LOG" | tail -n 5
} >> "$LOG"
if [ "$build_rc" -ne 0 ] || [ ! -f "$EXE" ]; then
    echo "ASAN RED: build failed (exit $build_rc) or $EXE missing; see $BUILD_LOG" | tee -a "$LOG"
    exit 1
fi

# ---------------------------------------------------------------------------------------------------------------
# 3. Run the [lifecycle] cases with the ASan runtime on PATH.
# ---------------------------------------------------------------------------------------------------------------
CL_PATH="$(grep -hE 'set\(CMAKE_CXX_COMPILER "' "$ASAN_DIR"/CMakeFiles/*/CMakeCXXCompiler.cmake 2>/dev/null \
    | head -n 1 | sed -nE 's/.*"(.*)".*/\1/p')"
if [ -n "$CL_PATH" ]; then
    CL_DIR="$(cygpath -u "$(dirname "$CL_PATH")")"
    if [ -f "$CL_DIR/clang_rt.asan_dynamic-x86_64.dll" ]; then
        PATH="$CL_DIR:$PATH"
        export PATH
        echo "--- ASan runtime dir on PATH: $CL_DIR" >> "$LOG"
    fi
fi

listed="$("$EXE" "$FILTER" --list-tests --verbosity quiet 2>&1 | tr -d '\r' | sed 's/^[[:space:]]*//' | grep -v '^$')"
echo "--- \"$FILTER\" --list-tests: $(printf '%s' "$listed" | tr '\n' ' ')" >> "$LOG"

r0="$(date +%s)"
"$EXE" "$FILTER" > "$RUN_LOG" 2>&1
run_rc=$?
r1="$(date +%s)"
asan_lines="$(grep -c 'AddressSanitizer' "$RUN_LOG" || true)"
{
    echo "--- run \"$FILTER\" exit=$run_rc wall=$((r1 - r0))s AddressSanitizer_lines=${asan_lines:-0} log=$RUN_LOG"
    tr -d '\r' < "$RUN_LOG" | tail -n 8
} >> "$LOG"

verdict="GREEN"
reason=""
if [ "$run_rc" -ne 0 ]; then
    verdict="RED"; reason="$reason exit=$run_rc"
fi
if [ "${asan_lines:-0}" -ne 0 ]; then
    verdict="RED"; reason="$reason asan_report"
fi
while IFS= read -r c; do
    [ -z "$c" ] && continue
    if ! printf '%s\n' "$listed" | grep -qxF "$c"; then
        verdict="RED"; reason="$reason $c-not-listed"
    fi
done <<< "$CASES"
if grep -qi "no test cases matched" "$RUN_LOG"; then
    verdict="RED"; reason="$reason no-case-matched"
fi

end="$(date +%s)"
echo "ASAN $verdict exit=$run_rc asan_lines=${asan_lines:-0} cases=$(printf '%s\n' "$listed" | grep -c .) cfg_wall=$((c1 - c0))s build_wall=$((b1 - b0))s run_wall=$((r1 - r0))s total_wall=$((end - start))s${reason:+ reason=[${reason# }]}" >> "$LOG"
tail -n 12 "$LOG"
[ "$verdict" = "GREEN" ]
