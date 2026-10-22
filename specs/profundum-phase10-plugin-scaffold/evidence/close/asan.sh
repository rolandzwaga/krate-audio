#!/usr/bin/env bash
# Profundum Phase 10, task T029 close lane: the editor-lifecycle case under the local ASan configuration (SC-013.5).
#
# Run from the main loop CONCURRENTLY with suites.sh and tidy.sh (Git Bash, from anywhere):
#   bash specs/profundum-phase10-plugin-scaffold/evidence/close/asan.sh
# Steps:
#   1. "$CMAKE" --build build-asan --config Debug --target profundum_tests (the VS generator reconfigures on the root
#      CMakeLists.txt change; build-asan is configured with -DENABLE_ASAN=ON).
#   2. build-asan/bin/Debug/profundum_tests.exe "[lifecycle]" -> exit 0, at least one case run (Profundum_EditorLifecycle
#      listed under the filter), and no "AddressSanitizer" line.
# The MSVC ASan runtime (clang_rt.asan_dynamic-x86_64.dll) is not copied next to the exe; the directory of the cl.exe
# that configured build-asan is prepended to PATH so the exe loads the matching runtime.
# Output: evidence/run-log/close/asan.log; last line "ASAN GREEN|RED ...". Exit 0 only when green.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase10-plugin-scaffold"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
ASAN_DIR="build-asan"
EXE="$ASAN_DIR/bin/Debug/profundum_tests.exe"
FILTER="[lifecycle]"
CASE="Profundum_EditorLifecycle"
LOGDIR="$SPEC/evidence/run-log/close"
LOG="$LOGDIR/asan.log"
BUILD_LOG="$LOGDIR/asan-build.log"
RUN_LOG="$LOGDIR/asan-run.log"

mkdir -p "$LOGDIR"
start="$(date +%s)"
echo "=== T029 asan $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)" > "$LOG"

if ! grep -qE '^ENABLE_ASAN:BOOL=ON' "$ASAN_DIR/CMakeCache.txt" 2>/dev/null; then
    echo "ASAN RED: $ASAN_DIR is not configured with ENABLE_ASAN=ON (configure: \"$CMAKE\" -S . -B $ASAN_DIR -G \"Visual Studio 17 2022\" -A x64 -DENABLE_ASAN=ON)" | tee -a "$LOG"
    exit 2
fi

# ---------------------------------------------------------------------------------------------------------------
# 1. Build.
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
# 2. Run the [lifecycle] cases with the ASan runtime on PATH.
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

listed="$("$EXE" "$FILTER" --list-tests --verbosity quiet 2>&1 | tr -d '\r' | sed 's/^[[:space:]]*//')"
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
if ! printf '%s\n' "$listed" | grep -qxF "$CASE"; then
    verdict="RED"; reason="$reason $CASE-not-listed"
fi
if grep -qi "no test cases matched" "$RUN_LOG"; then
    verdict="RED"; reason="$reason no-case-matched"
fi

end="$(date +%s)"
echo "ASAN $verdict exit=$run_rc asan_lines=${asan_lines:-0} build_wall=$((b1 - b0))s run_wall=$((r1 - r0))s total_wall=$((end - start))s${reason:+ reason=[${reason# }]}" >> "$LOG"
tail -n 12 "$LOG"
[ "$verdict" = "GREEN" ]
