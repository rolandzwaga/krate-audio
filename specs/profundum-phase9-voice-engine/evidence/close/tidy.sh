#!/usr/bin/env bash
# Profundum Phase 9, task T025 close lane: clang-tidy over the fourteen Phase 9 TUs (FR-084, SC-030).
# Structure copied from specs/profundum-phase8-bass-morph/evidence/close/tidy.sh.
#
# Run from the main loop CONCURRENTLY with suites.sh (Git Bash, from anywhere):
#   bash specs/profundum-phase9-voice-engine/evidence/close/tidy.sh
# Steps:
#   1. Reconfigure build/windows-ninja ("$CMAKE" --preset windows-ninja) inside the VS x64 environment (vcvars64;
#      the preset needs it), because the test CMake list changed and a stale compile database gives phantom or
#      missing diagnostics. Then check every one of the fourteen TUs is in compile_commands.json (a TU absent from the
#      database would be a false green).
#   2. "/c/Program Files/LLVM/bin/clang-tidy.exe" -p build/windows-ninja <TU>, one process per TU, concurrently,
#      each with a 600 s ceiling (timeout). The five new headers (systems/profundum_motion.h, profundum_macro_matrix.h,
#      profundum_engine_observer.h, profundum_voice.h, profundum_engine.h), the modified systems/mass_dynamics.h and
#      the test header profundum_voice_engine_test_helpers.h are checked through the TUs (root
#      .clang-tidy HeaderFilterRegex). NOT the whole-dsp sweep (a 15 h hang on an unrelated TU is on record).
# Output: evidence/run-log/close/tidy.log (all TU logs concatenated in a fixed order, then a summary; last line
# "TIDY GREEN|RED ..."). Exit 0 only when every TU finished inside the ceiling with 0 warnings and 0 errors in
# project files.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase9-voice-engine"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
TIDY="/c/Program Files/LLVM/bin/clang-tidy.exe"
VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
NINJA_DIR="build/windows-ninja"
CEILING_S=600
LOGDIR="$SPEC/evidence/run-log/close"
TIDYDIR="$LOGDIR/tidy"
LOG="$LOGDIR/tidy.log"

# The fourteen Phase 9 TUs (tasks.md "File map"; dsp/tests/CMakeLists.txt systems list; run-log/T024.txt), including
# mass_dynamics_oversampling_test.cpp (T005, the opt-in of the modified shared header).
TUS="dsp/tests/unit/systems/profundum_voice_engine_helpers_test.cpp
dsp/tests/unit/systems/profundum_motion_test.cpp
dsp/tests/unit/systems/profundum_macro_matrix_test.cpp
dsp/tests/unit/systems/profundum_voice_test.cpp
dsp/tests/unit/systems/profundum_engine_test.cpp
dsp/tests/unit/systems/profundum_engine_nonfinite_test.cpp
dsp/tests/unit/systems/profundum_engine_click_test.cpp
dsp/tests/unit/systems/profundum_engine_audibility_test.cpp
dsp/tests/unit/systems/profundum_engine_chain_test.cpp
dsp/tests/unit/systems/profundum_engine_aliasing_test.cpp
dsp/tests/unit/systems/profundum_engine_soak_test.cpp
dsp/tests/unit/systems/profundum_engine_probe_test.cpp
dsp/tests/unit/systems/profundum_engine_perf_test.cpp
dsp/tests/unit/systems/mass_dynamics_oversampling_test.cpp"
TU_COUNT="$(printf '%s\n' "$TUS" | grep -c .)"

mkdir -p "$LOGDIR" "$TIDYDIR"
rm -f "$TIDYDIR"/*.log
start="$(date +%s)"
echo "=== T025 tidy $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)" > "$LOG"

for f in "$TIDY" "$VSWHERE"; do
    if [ ! -f "$f" ]; then
        echo "TIDY RED: $f not found" | tee -a "$LOG"
        exit 2
    fi
done

# ---------------------------------------------------------------------------------------------------------------
# 1. Reconfigure the Ninja tree inside vcvars64.
# ---------------------------------------------------------------------------------------------------------------
# Use the VS install whose cl.exe the Ninja cache already holds (several installs exist on this machine; mixing one
# install's environment with another's cached compiler poisons the configure checks). vswhere only as a fallback.
VS_PATH=""
CACHED_CXX="$(grep -E '^CMAKE_CXX_COMPILER:' "$NINJA_DIR/CMakeCache.txt" 2>/dev/null | head -n 1 | cut -d= -f2- | tr -d '\r')"
case "$CACHED_CXX" in
    */VC/Tools/*) VS_PATH="${CACHED_CXX%%/VC/Tools/*}" ;;
esac
if [ -z "$VS_PATH" ]; then
    VS_PATH="$("$VSWHERE" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
        -property installationPath | tr -d '\r' | head -n 1)"
fi
VCVARS_U="$(cygpath -u "$VS_PATH")/VC/Auxiliary/Build/vcvars64.bat"
if [ -z "$VS_PATH" ] || [ ! -f "$VCVARS_U" ]; then
    echo "TIDY RED: vcvars64.bat not found (VS root '$VS_PATH')" | tee -a "$LOG"
    exit 2
fi
VCVARS="$(cygpath -w "$VCVARS_U")"
echo "--- vcvars: $VCVARS" >> "$LOG"
BAT_DIR="$(mktemp -d)"
BAT="$BAT_DIR/reconfigure-ninja.bat"
cat > "$BAT" <<EOF
@echo off
call "$VCVARS" >nul
if errorlevel 1 exit /b 9
cd /d "$(cygpath -w "$REPO")"
"$(cygpath -w "$CMAKE")" --preset windows-ninja
exit /b %ERRORLEVEL%
EOF
c0="$(date +%s)"
cmd //c "$(cygpath -w "$BAT")" > "$TIDYDIR/reconfigure.out" 2>&1
cfg_status=$?
c1="$(date +%s)"
rm -rf "$BAT_DIR"
{
    echo "--- reconfigure (cmake --preset windows-ninja in vcvars64) exit=$cfg_status wall=$((c1 - c0))s"
    tr -d '\r' < "$TIDYDIR/reconfigure.out" | tail -n 5
} >> "$LOG"
if [ "$cfg_status" -ne 0 ]; then
    echo "TIDY RED: reconfigure failed (exit $cfg_status); see $TIDYDIR/reconfigure.out" | tee -a "$LOG"
    exit 1
fi

DB="$NINJA_DIR/compile_commands.json"
# The database holds JSON-escaped Windows paths (F:\\projects\\...); squeeze every run of backslashes (octal 134)
# into one '/' before matching.
DB_NORM="$TIDYDIR/compile_commands.norm"
tr -s '\134' '/' < "$DB" > "$DB_NORM"
missing_db=""
while IFS= read -r tu; do
    [ -z "$tu" ] && continue
    if ! grep -qF "/$tu\"" "$DB_NORM"; then
        missing_db="$missing_db $tu"
    fi
done <<< "$TUS"
if [ -n "$missing_db" ]; then
    echo "TIDY RED: not in $DB (would be a false green):$missing_db" | tee -a "$LOG"
    exit 1
fi
echo "--- compile database: all $TU_COUNT TUs present" >> "$LOG"

# ---------------------------------------------------------------------------------------------------------------
# 2. clang-tidy, one process per TU, concurrently, 600 s ceiling each.
# ---------------------------------------------------------------------------------------------------------------
run_tidy() {
    local tu="$1"
    local log="$TIDYDIR/$(basename "$tu" .cpp).log"
    local s0 s1 rc
    s0="$(date +%s)"
    timeout --kill-after=10 "$CEILING_S" "$TIDY" -p "$NINJA_DIR" "$tu" > "$log" 2>&1
    rc=$?
    s1="$(date +%s)"
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
}

t0="$(date +%s)"
pids=""
while IFS= read -r tu; do
    [ -z "$tu" ] && continue
    run_tidy "$tu" &
    pids="$pids $!"
done <<< "$TUS"
for p in $pids; do
    wait "$p"
done
t1="$(date +%s)"

# ---------------------------------------------------------------------------------------------------------------
# Collect: diagnostics in project files only (extern/ and _deps/ excluded).
# ---------------------------------------------------------------------------------------------------------------
total_warn=0
total_err=0
timeouts=""
failed=""
summary=""
while IFS= read -r tu; do
    [ -z "$tu" ] && continue
    log="$TIDYDIR/$(basename "$tu" .cpp).log"
    {
        echo "=== $tu"
        tr -d '\r' < "$log"
    } >> "$LOG"
    rc="$(tr -d '\r' < "$log" | grep -E '^exit: ' | tail -n 1 | awk '{print $2}')"
    w="$(tr -d '\r' < "$log" | grep -E '(warning|error):' | grep -vE 'extern[/\\]|_deps[/\\]' | grep -c 'warning:' || true)"
    e="$(tr -d '\r' < "$log" | grep -E '(warning|error):' | grep -vE 'extern[/\\]|_deps[/\\]' | grep -c 'error:' || true)"
    total_warn=$((total_warn + ${w:-0}))
    total_err=$((total_err + ${e:-0}))
    if [ "${rc:-1}" = "124" ] || [ "${rc:-1}" = "137" ]; then
        timeouts="$timeouts $tu"
    elif [ "${rc:-1}" != "0" ] && [ "${e:-0}" -eq 0 ]; then
        failed="$failed $tu(exit=$rc)"
    fi
    summary="$summary
$tu exit=${rc:-?} warnings=${w:-0} errors=${e:-0}"
done <<< "$TUS"

end="$(date +%s)"
verdict="GREEN"
if [ "$total_warn" -ne 0 ] || [ "$total_err" -ne 0 ] || [ -n "$timeouts" ] || [ -n "$failed" ]; then
    verdict="RED"
fi
{
    echo "=== summary${summary}"
    echo "TIDY $verdict tus=$TU_COUNT warnings=$total_warn errors=$total_err timeouts=[${timeouts# }] failed=[${failed# }] tidy_wall=$((t1 - t0))s total_wall=$((end - start))s"
} >> "$LOG"
tail -n 18 "$LOG"
[ "$verdict" = "GREEN" ]
