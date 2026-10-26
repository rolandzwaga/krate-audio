#!/usr/bin/env bash
# Profundum Phase 11, task T031 close lane: clang-tidy over the change set's targets (SC-023: profundum, shared,
# vorago; 0 warnings). Reconfigure step copied from specs/profundum-phase10-plugin-scaffold/evidence/close/tidy.sh.
#
# Run from the main loop CONCURRENTLY with suites.sh (Git Bash, from anywhere):
#   bash specs/profundum-phase11-parameters/evidence/close/tidy.sh
# Steps:
#   1. Reconfigure build/windows-ninja ("$CMAKE" --preset windows-ninja) inside the VS x64 environment (vcvars64; the
#      preset needs it), because the test CMake lists changed (profundum + shared) and a stale compile database gives
#      phantom or missing diagnostics. Then check every Profundum TU (14 Phase 10 + 12 Phase 11), the new shared TU
#      and the Vorago sustain TU are in compile_commands.json (a TU absent from the database would be a false green).
#   2. Three lanes CONCURRENTLY, each to its own log under a 900 s wall-clock ceiling:
#      - profundum: ./tools/run-clang-tidy.ps1 -Target profundum -BuildDir build/windows-ninja
#      - vorago:    ./tools/run-clang-tidy.ps1 -Target vorago    -BuildDir build/windows-ninja
#      - shared:    run-clang-tidy.ps1 has NO `shared` target (its ValidateSet, tools/run-clang-tidy.ps1:66, lists
#                   none; only run-clang-tidy.sh has one, and the .sh is never used on Windows). This lane therefore
#                   runs clang-tidy -p build/windows-ninja directly over the .cpp files of the .sh's `shared)` dirs
#                   (plugins/shared/src, plugins/shared/tests; .cpp only, as the .ps1 does), with the .ps1's filter
#                   (Process-TidyOutput: lines under plugins/ or dsp/, the Catch2 false positives dropped in test
#                   files, clang-diagnostic-error not counted) and the .ps1's 600 s per-TU ceiling.
#      Each lane's job count is max(2, cores / 3) so the three together do not oversubscribe the machine while
#      suites.sh runs.
# Output: evidence/run-log/close/tidy.log (+ tidy/<lane>.out); last line "TIDY GREEN|RED ...". Green means every lane
# finished inside its ceiling and reported 0 warnings and 0 errors. Exit 0 only when green.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase11-parameters"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
CLANG_TIDY="/c/Program Files/LLVM/bin/clang-tidy.exe"
NINJA_DIR="build/windows-ninja"
CEILING_S=900
FILE_CEILING_S=600
LOGDIR="$SPEC/evidence/run-log/close"
TIDYDIR="$LOGDIR/tidy"
LOG="$LOGDIR/tidy.log"

# Every TU that must be in the compile database (plugins/profundum/CMakeLists.txt, tests/CMakeLists.txt; the new
# shared TU; the Vorago consumer TU of the moved SustainLatch).
TUS="plugins/profundum/src/entry.cpp
plugins/profundum/src/processor/processor.cpp
plugins/profundum/src/controller/controller.cpp
plugins/profundum/tests/vstgui_test_stubs.cpp
plugins/profundum/tests/unit/test_main.cpp
plugins/profundum/tests/unit/processor_bus_test.cpp
plugins/profundum/tests/unit/param_denorm_test.cpp
plugins/profundum/tests/unit/state_roundtrip_test.cpp
plugins/profundum/tests/unit/midi_event_test.cpp
plugins/profundum/tests/unit/lifecycle_test.cpp
plugins/profundum/tests/unit/controller/editor_lifecycle_test.cpp
plugins/profundum/tests/integration/processor_audio_test.cpp
plugins/profundum/tests/integration/param_flow_test.cpp
plugins/profundum/tests/integration/processor_cpu_test.cpp
plugins/profundum/tests/unit/param_table_test.cpp
plugins/profundum/tests/unit/state_v2_test.cpp
plugins/profundum/tests/unit/midi_mapping_test.cpp
plugins/profundum/tests/unit/phase11_alloc_test.cpp
plugins/profundum/tests/integration/param_wiring_test.cpp
plugins/profundum/tests/integration/state_restore_render_test.cpp
plugins/profundum/tests/integration/copy_compare_test.cpp
plugins/profundum/tests/integration/tail_test.cpp
plugins/profundum/tests/integration/param_path_audibility_test.cpp
plugins/profundum/tests/integration/mono_sum_params_test.cpp
plugins/profundum/tests/integration/determinism_test.cpp
plugins/profundum/tests/integration/band_level_metric_test.cpp
plugins/shared/tests/test_sustain_latch.cpp
plugins/vorago/tests/integration/sustain_test.cpp"
TU_COUNT="$(printf '%s\n' "$TUS" | grep -c .)"
PROFUNDUM_TU_COUNT="$(printf '%s\n' "$TUS" | grep -c '^plugins/profundum/')"

CORES="$(nproc 2>/dev/null || echo 8)"
JOBS=$(( CORES / 3 ))
[ "$JOBS" -lt 2 ] && JOBS=2

mkdir -p "$LOGDIR" "$TIDYDIR"
start="$(date +%s)"
echo "=== T031 tidy $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown) cores=$CORES jobs/lane=$JOBS" > "$LOG"

PWSH="$(command -v pwsh || command -v powershell.exe || true)"
for f in "$VSWHERE" "$PWSH" "$CLANG_TIDY"; do
    if [ -z "$f" ] || [ ! -f "$f" ]; then
        echo "TIDY RED: '$f' not found" | tee -a "$LOG"
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
echo "--- compile database: all $TU_COUNT TUs present ($PROFUNDUM_TU_COUNT Profundum, 1 shared, 1 Vorago)" >> "$LOG"

# ---------------------------------------------------------------------------------------------------------------
# 2. Three lanes, concurrently, each under the 900 s ceiling.
# ---------------------------------------------------------------------------------------------------------------
# ps1_lane <target>: run-clang-tidy.ps1 -Target <target>; writes tidy/<target>.out and tidy/<target>.rc.
ps1_lane() {
    local target="$1" out="$TIDYDIR/$1.out" t0 t1 rc
    t0="$(date +%s)"
    timeout --kill-after=10 "$CEILING_S" "$PWSH" -NoProfile -ExecutionPolicy Bypass -File ./tools/run-clang-tidy.ps1 \
        -Target "$target" -BuildDir "$NINJA_DIR" -Jobs "$JOBS" > "$out" 2>&1
    rc=$?
    t1="$(date +%s)"
    echo "rc=$rc wall=$((t1 - t0))" > "$TIDYDIR/$target.rc"
}

# tidy_one <file>: one clang-tidy TU for the shared lane, filtered exactly as run-clang-tidy.ps1's Process-TidyOutput.
# Prints "<warnings> <errors> <file>" on its own line, and the kept diagnostics to tidy/shared-files/<file>.diag.
tidy_one() {
    local f="$1" safe raw kept w e rc
    safe="$(printf '%s' "$f" | tr -c 'A-Za-z0-9_.-' '_')"
    raw="$TIDYDIR/shared-files/$safe.raw"
    kept="$TIDYDIR/shared-files/$safe.diag"
    timeout --kill-after=10 "$FILE_CEILING_S" "$CLANG_TIDY" -p "$NINJA_DIR" "$f" > "$raw" 2>&1
    rc=$?
    tr -d '\r' < "$raw" | grep -E 'plugins[/\\]|dsp[/\\]' | grep -v 'Processing file' | grep -v 'Error while processing' > "$kept.all"
    case "$f" in
        *tests/*) grep -vE 'bugprone-chained-comparison|cppcoreguidelines-avoid-do-while' "$kept.all" > "$kept.f" ;;
        *)        cp "$kept.all" "$kept.f" ;;
    esac
    w="$(grep -cE '\bwarning:' "$kept.f" || true)"
    e="$(grep -E '\berror:' "$kept.f" | grep -vc 'clang-diagnostic-error' || true)"
    if [ "$rc" = "124" ] || [ "$rc" = "137" ]; then
        e=$(( ${e:-0} + 1 ))
        echo "$f: clang-tidy exceeded ${FILE_CEILING_S} s and was killed (hung translation unit)" >> "$kept.f"
    fi
    grep -E 'warning:|error:|note:|exceeded' "$kept.f" > "$kept" || true
    rm -f "$kept.all" "$kept.f"
    echo "${w:-0} ${e:-0} $f"
}
export -f tidy_one
export TIDYDIR NINJA_DIR CLANG_TIDY FILE_CEILING_S

shared_lane() {
    local out="$TIDYDIR/shared.out" list="$TIDYDIR/shared-files.txt" t0 t1 rc
    mkdir -p "$TIDYDIR/shared-files"
    rm -f "$TIDYDIR/shared-files"/*
    # User ruling 2026-10-22 (pure code move): the shared lane tidies the change set's shared TU only; sustain_latch.h
    # is checked through it (the filter keeps every diagnostic under plugins/). Untouched shared files are not this lane's.
    echo plugins/shared/tests/test_sustain_latch.cpp > "$list"
    t0="$(date +%s)"
    tr '\n' '\0' < "$list" | timeout --kill-after=10 "$CEILING_S" xargs -0 -n 1 -P "$JOBS" bash -c 'tidy_one "$1"' _ \
        > "$TIDYDIR/shared-counts.txt" 2>&1
    rc=$?
    t1="$(date +%s)"
    local files warns errs
    files="$(grep -cE '^[0-9]+ [0-9]+ ' "$TIDYDIR/shared-counts.txt" || true)"
    warns="$(awk '/^[0-9]+ [0-9]+ /{s+=$1} END{print s+0}' "$TIDYDIR/shared-counts.txt")"
    errs="$(awk '/^[0-9]+ [0-9]+ /{s+=$2} END{print s+0}' "$TIDYDIR/shared-counts.txt")"
    {
        echo "=== shared lane: clang-tidy -p $NINJA_DIR over $(grep -c . "$list") .cpp file(s) (the change set: test_sustain_latch.cpp)"
        cat "$TIDYDIR/shared-files"/*.diag 2>/dev/null
        echo "  Files analyzed: ${files:-0}"
        echo "  Errors: ${errs:-0}"
        echo "  Warnings: ${warns:-0}"
    } > "$out"
    # A file the ceiling cut off never printed its count line: files < listed is caught below as RED.
    echo "rc=$rc wall=$((t1 - t0)) listed=$(grep -c . "$list")" > "$TIDYDIR/shared.rc"
}

rm -f "$TIDYDIR"/*.rc
l0="$(date +%s)"
ps1_lane profundum &
p1=$!
ps1_lane vorago &
p2=$!
shared_lane &
p3=$!
wait "$p1"
wait "$p2"
wait "$p3"
l1="$(date +%s)"

# ---------------------------------------------------------------------------------------------------------------
# 3. Verdicts.
# ---------------------------------------------------------------------------------------------------------------
overall="GREEN"
summary=""
for lane in profundum vorago shared; do
    out="$TIDYDIR/$lane.out"
    rcline="$(cat "$TIDYDIR/$lane.rc" 2>/dev/null)"
    rc="$(printf '%s' "$rcline" | sed -nE 's/.*rc=([0-9]+).*/\1/p')"
    wall="$(printf '%s' "$rcline" | sed -nE 's/.*wall=([0-9]+).*/\1/p')"
    {
        echo "=================== lane $lane ($rcline)"
        tr -d '\r' < "$out"
    } >> "$LOG"
    files="$(tr -d '\r' < "$out" | sed -nE 's/.*Files analyzed: ([0-9]+).*/\1/p' | tail -n 1)"
    warns="$(tr -d '\r' < "$out" | sed -nE 's/.*[[:space:]]Warnings: ([0-9]+).*/\1/p' | tail -n 1)"
    errs="$(tr -d '\r' < "$out" | sed -nE 's/.*[[:space:]]Errors: ([0-9]+).*/\1/p' | tail -n 1)"
    killed="$(tr -d '\r' < "$out" | grep -c 'exceeded .* and was killed' || true)"
    verdict="GREEN"
    reason=""
    if [ "$rc" = "124" ] || [ "$rc" = "137" ]; then
        verdict="RED"; reason="$reason ceiling(${CEILING_S}s)"
    fi
    if [ "${rc:-1}" != "0" ]; then
        verdict="RED"; reason="$reason exit=${rc:-?}"
    fi
    if [ -z "$warns" ] || [ -z "$errs" ] || [ -z "$files" ]; then
        verdict="RED"; reason="$reason summary-missing"
    fi
    if [ "${warns:-1}" != "0" ] || [ "${errs:-1}" != "0" ]; then
        verdict="RED"; reason="$reason diagnostics"
    fi
    if [ "${killed:-0}" -ne 0 ]; then
        verdict="RED"; reason="$reason hung-TU=$killed"
    fi
    # Fewer files than expected is a false green.
    case "$lane" in
        profundum) expect="$PROFUNDUM_TU_COUNT" ;;
        vorago)    expect=1 ;;
        shared)    expect="$(printf '%s' "$rcline" | sed -nE 's/.*listed=([0-9]+).*/\1/p')" ;;
    esac
    if [ -n "$files" ] && [ -n "$expect" ] && [ "$files" -lt "$expect" ]; then
        verdict="RED"; reason="$reason files=$files<$expect"
    fi
    [ "$verdict" = "GREEN" ] || overall="RED"
    summary="$summary
lane $lane $verdict files=${files:-?} warnings=${warns:-?} errors=${errs:-?} exit=${rc:-?} wall=${wall:-?}s${reason:+ reason=[${reason# }]}"
done

end="$(date +%s)"
{
    echo "=== summary${summary}"
    echo "lanes wall=$((l1 - l0))s"
    echo "TIDY $overall total_wall=$((end - start))s"
} >> "$LOG"
tail -n 6 "$LOG"
[ "$overall" = "GREEN" ]
