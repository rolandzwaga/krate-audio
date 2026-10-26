#!/usr/bin/env bash
# Profundum Phase 6, task T019: the three calibration harvests (plan S8.1 / S8.2 / S8.3; tasks.md deviation 6).
#
# Run from the main loop at the Group 8 checkpoint (Git Bash, from anywhere):
#   bash specs/profundum-phase6-mass-dynamics/evidence/close/harvest.sh
# Steps (each to its own log under evidence/run-log/close/; one status line per step, with exit code and wall clock,
# to evidence/run-log/close/harvest-status.txt):
#   1. One rebuild of dsp_processors_tests + dsp_systems_tests (warning count must be 0).
#   2. CONCURRENTLY: TransientShaper_DurationTableHarvest (processors) and MassCompressor_AmountTableHarvest (systems).
#   3. paste-harvest.js duration-table amount-table, then one rebuild of both exes.
#   4. MassLaw_TableHarvest (systems; needs the pasted amount table, plan S8.3 target 2).
#   5. paste-harvest.js mass-law-rows, then one rebuild of both exes.
#   6. The harvest-dependent per-push cases, concurrently, collected into one log; one summary line per case.
# Exit codes: 0 green; 1 a build, harvest, paste or step-6 case failed; 2 environment; 3 STOP AND SURFACE (the line is
# printed; take it to the user before anything else runs: S8.2 R-6 / FR-021 ruling, S8.3 harvest targets).
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase6-mass-dynamics"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
BUILD_DIR="build/windows-x64-release"
BIN="$BUILD_DIR/bin/Release"
PROC_EXE="$BIN/dsp_processors_tests.exe"
SYS_EXE="$BIN/dsp_systems_tests.exe"
PASTE="$SPEC/evidence/close/paste-harvest.js"
EVIDENCE="$SPEC/evidence"
LOGDIR="$SPEC/evidence/run-log/close"
STATUS="$LOGDIR/harvest-status.txt"

if [ ! -x "$CMAKE" ] && [ ! -f "$CMAKE" ]; then
    echo "harvest: $CMAKE not found" >&2
    exit 2
fi
mkdir -p "$LOGDIR"

now() { date -u +%Y-%m-%dT%H:%M:%SZ; }
status_line() { echo "$*" >> "$STATUS"; }
run_start="$(date +%s)"
echo "START $(now) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown) nproc $(nproc 2>/dev/null || echo ?)" > "$STATUS"

finish() {
    local code="$1"
    local msg="$2"
    status_line "DONE $(now) exit=$code wall=$(( $(date +%s) - run_start ))s $msg"
    echo "harvest: $msg (exit $code; status $STATUS)"
    exit "$code"
}

# Prints any STOP AND SURFACE line of the given files and exits 3.
check_stop() {
    local lines
    lines="$(grep -h "STOP AND SURFACE" "$@" 2>/dev/null | tr -d '\r' | sort -u)"
    if [ -n "$lines" ]; then
        echo "harvest: STOP AND SURFACE -- return to the user before anything else runs; do NOT paste" >&2
        echo "$lines" >&2
        status_line "STOP AND SURFACE: $(echo "$lines" | head -n 5 | tr '\n' ' ')"
        finish 3 "STOP AND SURFACE (see the lines above)"
    fi
}

# One rebuild of both exes; $1 = step label. Red on a build failure or any warning.
rebuild() {
    local label="$1"
    local log="$LOGDIR/harvest-$label.log"
    local t0 t1 rc warnings
    t0="$(date +%s)"
    "$CMAKE" --build "$BUILD_DIR" --config Release --target dsp_processors_tests dsp_systems_tests > "$log" 2>&1
    rc=$?
    t1="$(date +%s)"
    warnings="$(grep -ciE "warning C[0-9]|warning:" "$log" || true)"
    echo "exit: $rc wall: $((t1 - t0)) s warnings: ${warnings:-0}" >> "$log"
    status_line "$label exit=$rc wall=$((t1 - t0))s warnings=${warnings:-0} log=$log"
    if [ "$rc" -ne 0 ]; then
        tail -n 30 "$log" >&2
        finish 1 "$label: build failed"
    fi
    if [ "${warnings:-0}" -ne 0 ]; then
        grep -iE "warning C[0-9]|warning:" "$log" | sort -u | head -n 20 >&2
        finish 1 "$label: ${warnings} build warning(s); must be 0"
    fi
    for exe in "$PROC_EXE" "$SYS_EXE"; do
        [ -f "$exe" ] || finish 1 "$label: $exe missing after the build"
    done
}

# Runs one test exe on one case name to a log, appends exit + wall, writes a status line; returns the exit code.
run_case() {
    local label="$1" exe="$2" name="$3" log="$4"
    local s0 s1 rc
    s0="$(date +%s)"
    "$exe" "$name" > "$log" 2>&1
    rc=$?
    s1="$(date +%s)"
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    status_line "$label exit=$rc wall=$((s1 - s0))s log=$log"
    return "$rc"
}

# The evidence file must have been (re)written by this step, not left over from an earlier run.
require_fresh() {
    local file="$1" since="$2"
    if [ ! -f "$file" ] || [ "$(stat -c %Y "$file")" -lt "$since" ]; then
        finish 1 "$file was not written by this run"
    fi
}

paste_tables() {
    local label="$1"
    shift
    local log="$LOGDIR/harvest-$label.log"
    local t0 rc
    t0="$(date +%s)"
    node "$PASTE" "$@" > "$log" 2>&1
    rc=$?
    echo "exit: $rc wall: $(( $(date +%s) - t0 )) s" >> "$log"
    status_line "$label exit=$rc wall=$(( $(date +%s) - t0 ))s log=$log"
    cat "$log"
    [ "$rc" -eq 0 ] || finish 1 "$label: paste-harvest.js $* failed"
}

# ---------------------------------------------------------------------------------------------------------------------
# 1. One rebuild of both exes.
rebuild "step1-build"

# ---------------------------------------------------------------------------------------------------------------------
# 2. Duration and amount harvests, concurrently.
DUR_LOG="$LOGDIR/harvest-duration.log"
AMT_LOG="$LOGDIR/harvest-amount.log"
step2_start="$(date +%s)"
run_case "step2-duration-harvest" "$PROC_EXE" "TransientShaper_DurationTableHarvest" "$DUR_LOG" &
dur_pid=$!
run_case "step2-amount-harvest" "$SYS_EXE" "MassCompressor_AmountTableHarvest" "$AMT_LOG" &
amt_pid=$!
wait "$dur_pid"
dur_rc=$?
wait "$amt_pid"
amt_rc=$?
status_line "step2 wall=$(( $(date +%s) - step2_start ))s duration=$dur_rc amount=$amt_rc"
check_stop "$DUR_LOG" "$AMT_LOG" "$EVIDENCE/duration-table.txt" "$EVIDENCE/amount-table.txt"
if [ "$dur_rc" -ne 0 ] || [ "$amt_rc" -ne 0 ]; then
    tail -n 12 "$DUR_LOG" "$AMT_LOG" >&2
    finish 1 "step2: a harvest case failed (duration $dur_rc, amount $amt_rc)"
fi
require_fresh "$EVIDENCE/duration-table.txt" "$step2_start"
require_fresh "$EVIDENCE/amount-table.txt" "$step2_start"

# ---------------------------------------------------------------------------------------------------------------------
# 3. Paste duration + amount tables, one rebuild.
paste_tables "step3-paste" duration-table amount-table
rebuild "step3-build"

# ---------------------------------------------------------------------------------------------------------------------
# 4. Mass law harvest (12-cell std::async fan-out inside the case).
LAW_LOG="$LOGDIR/harvest-mass-law.log"
step4_start="$(date +%s)"
run_case "step4-mass-law-harvest" "$SYS_EXE" "MassLaw_TableHarvest" "$LAW_LOG"
law_rc=$?
check_stop "$LAW_LOG" "$EVIDENCE/mass-law-table.txt"
if [ "$law_rc" -ne 0 ]; then
    tail -n 12 "$LAW_LOG" >&2
    finish 1 "step4: MassLaw_TableHarvest failed (exit $law_rc)"
fi
require_fresh "$EVIDENCE/mass-law-table.txt" "$step4_start"

# ---------------------------------------------------------------------------------------------------------------------
# 5. Paste the law rows, one rebuild.
paste_tables "step5-paste" mass-law-rows
rebuild "step5-build"

# ---------------------------------------------------------------------------------------------------------------------
# 6. Harvest-dependent per-push cases, concurrently; collected into one log.
STEP6_LOG="$LOGDIR/harvest-step6-cases.log"
STEP6_TMP="$LOGDIR/harvest-step6"
rm -rf "$STEP6_TMP"
mkdir -p "$STEP6_TMP"
SYS_CASES="MassCompressor_MakeupMatchesGr MassCompressor_NoSubPumping MassCompressor_RectifiedFeedNegativeControl
MassChain_LimiterIdleSteadyState MassLaw_AnchorPoints MassLaw_Audibility MassLaw_FundamentalStrengthJointSweepSmoke
MassLaw_LevelSlopeBoundedSmoke"
PROC_CASES="TransientShaper_DurationMatches TransientShaper_DurationShortestCell"
step6_start="$(date +%s)"
pids=""
for c in $SYS_CASES; do
    run_case "step6-$c" "$SYS_EXE" "$c" "$STEP6_TMP/$c.log" &
    pids="$pids $!"
done
for c in $PROC_CASES; do
    run_case "step6-$c" "$PROC_EXE" "$c" "$STEP6_TMP/$c.log" &
    pids="$pids $!"
done
for p in $pids; do
    wait "$p"
done

red=0
{
    echo "=== T019 step 6 harvest-dependent per-push cases $(now) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    for c in $SYS_CASES $PROC_CASES; do
        echo
        echo "=== $c"
        tr -d '\r' < "$STEP6_TMP/$c.log"
    done
} > "$STEP6_LOG"
echo "harvest: step 6 summaries"
for c in $SYS_CASES $PROC_CASES; do
    log="$STEP6_TMP/$c.log"
    rc="$(tr -d '\r' < "$log" | sed -n 's/^exit: \([0-9-]*\) .*/\1/p' | tail -n 1)"
    summary="$(tr -d '\r' < "$log" | grep -E "^(All tests passed|test cases:|No test cases matched)" | tail -n 1)"
    [ -n "$summary" ] || summary="(no Catch2 summary line)"
    if [ "${rc:-1}" != "0" ] || echo "$summary" | grep -q "No test cases matched"; then
        red=$((red + 1))
        verdict="RED"
    else
        verdict="green"
    fi
    line="step6 $verdict $c exit=${rc:-?} | $summary"
    echo "$line"
    echo "$line" >> "$STEP6_LOG"
done
rm -rf "$STEP6_TMP"
status_line "step6 wall=$(( $(date +%s) - step6_start ))s red=$red log=$STEP6_LOG"

if [ "$red" -ne 0 ]; then
    finish 1 "step6: $red harvest-dependent case(s) red (see $STEP6_LOG)"
fi
finish 0 "green: three tables harvested and pasted, step 6 green"
