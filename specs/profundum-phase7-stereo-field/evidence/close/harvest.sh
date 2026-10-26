#!/usr/bin/env bash
# Profundum Phase 7, task T015: the wide-floor calibration harvest (plan S9.1 / S9.2; C-13; tasks.md deviations 4, 7).
#
# Run from the main loop at the Group 8 checkpoint (Git Bash, from anywhere):
#   bash specs/profundum-phase7-stereo-field/evidence/close/harvest.sh
# Steps (each to its own log under evidence/run-log/close/; one status line per step, with exit code and wall clock,
# to evidence/run-log/close/harvest-status.txt):
#   1. One rebuild of dsp_systems_tests (warning count must be 0).
#   2. BassStereoField_WideScaleHarvest (K_wide in {1, 0.75} at g_floor 0.5; G8-1: chosen on SC-007 + the SC-010 lever).
#      A STOP AND SURFACE line -> exit 3.
#   3. aggregate-harvest.js wide-scale -> evidence/harvest-wide-floor.txt (chosen K_wide + the committed floor);
#      paste-harvest.js wide-floor; one rebuild.
#   4. CONCURRENTLY (xargs -P 15): the BassStereoField_SideFloorHarvest_<Arm> shards and
#      BassStereoField_SideFloorLeverHarvest, each to its own log; the names come from --list-tests
#      "[stereo-field-harvest]", never hand-kept.
#   5. aggregate-harvest.js side-floor: the largest g_floor PASS in every shard and in the lever case; none >= 0.03 ->
#      STOP AND SURFACE, exit 3. Otherwise the evidence file is rewritten (both tables, both values, commit, date);
#      paste-harvest.js wide-floor; one rebuild.
#   6. The harvest-dependent per-push cases by name, then the phase's per-push filter, concurrently; collected into
#      one log; one summary line per run.
# Exit codes: 0 green; 1 a build, harvest, aggregate, paste or step-6 run failed; 2 environment; 3 STOP AND SURFACE (the
# line is printed; take it to the user before anything else runs: C-13's options are lower K_wide further, accept a
# lower floor, or re-read SC-002 on the WIDE arm).
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase7-stereo-field"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
BUILD_DIR="build/windows-x64-release"
BIN="$BUILD_DIR/bin/Release"
SYS_EXE="$BIN/dsp_systems_tests.exe"
PASTE="$SPEC/evidence/close/paste-harvest.js"
AGG="$SPEC/evidence/close/aggregate-harvest.js"
EVIDENCE="$SPEC/evidence"
WIDE_FLOOR="$EVIDENCE/harvest-wide-floor.txt"
LOGDIR="$SPEC/evidence/run-log/close"
STATUS="$LOGDIR/harvest-status.txt"
JOBS=15

if [ ! -x "$CMAKE" ] && [ ! -f "$CMAKE" ]; then
    echo "harvest: $CMAKE not found" >&2
    exit 2
fi
command -v node > /dev/null 2>&1 || { echo "harvest: node not found" >&2; exit 2; }
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

# One rebuild of dsp_systems_tests; $1 = step label. Red on a build failure or any warning.
rebuild() {
    local label="$1"
    local log="$LOGDIR/harvest-$label.log"
    local t0 t1 rc warnings
    t0="$(date +%s)"
    "$CMAKE" --build "$BUILD_DIR" --config Release --target dsp_systems_tests > "$log" 2>&1
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
    [ -f "$SYS_EXE" ] || finish 1 "$label: $SYS_EXE missing after the build"
}

# Runs the systems exe on one case name / filter to a log, appends exit + wall, writes a status line; returns the exit.
run_case() {
    local label="$1" name="$2" log="$3"
    local s0 s1 rc
    s0="$(date +%s)"
    "$SYS_EXE" "$name" > "$log" 2>&1
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

# Runs a node helper to a log; $1 label, rest = node args. Returns the helper's exit code (0 / 1 / 3).
run_node() {
    local label="$1"
    shift
    local log="$LOGDIR/harvest-$label.log"
    local t0 rc
    t0="$(date +%s)"
    node "$@" > "$log" 2>&1
    rc=$?
    echo "exit: $rc wall: $(( $(date +%s) - t0 )) s" >> "$log"
    status_line "$label exit=$rc wall=$(( $(date +%s) - t0 ))s log=$log"
    cat "$log"
    return "$rc"
}

# aggregate (exit 3 -> STOP AND SURFACE, 1 -> red), fresh evidence, paste, one rebuild. $1 step, $2 since, rest = agg args.
aggregate_paste_rebuild() {
    local step="$1" since="$2"
    shift 2
    run_node "$step-aggregate" "$AGG" "$@"
    local rc=$?
    if [ "$rc" -eq 3 ]; then
        echo "harvest: STOP AND SURFACE -- return to the user before anything else runs; do NOT paste" >&2
        status_line "STOP AND SURFACE: $(grep -h "STOP AND SURFACE" "$LOGDIR/harvest-$step-aggregate.log" | tr -d '\r' | head -n 1)"
        finish 3 "$step: STOP AND SURFACE (the per-candidate table is above and in $LOGDIR/harvest-$step-aggregate.log)"
    fi
    [ "$rc" -eq 0 ] || finish 1 "$step: aggregate-harvest.js $* failed (exit $rc)"
    require_fresh "$WIDE_FLOOR" "$since"
    run_node "$step-paste" "$PASTE" wide-floor || finish 1 "$step: paste-harvest.js wide-floor failed"
    rebuild "$step-build"
}

# ---------------------------------------------------------------------------------------------------------------------
# 1. One rebuild.
rebuild "step1-build"

# ---------------------------------------------------------------------------------------------------------------------
# 2. K_wide harvest.
WIDE_LOG="$LOGDIR/harvest-step2-wide-scale.log"
step2_start="$(date +%s)"
run_case "step2-wide-scale-harvest" "BassStereoField_WideScaleHarvest" "$WIDE_LOG"
wide_rc=$?
check_stop "$WIDE_LOG"
if [ "$wide_rc" -ne 0 ]; then
    tail -n 12 "$WIDE_LOG" >&2
    finish 1 "step2: BassStereoField_WideScaleHarvest failed (exit $wide_rc)"
fi
grep -h "^HARVEST wide-scale" "$WIDE_LOG" | tr -d '\r'

# ---------------------------------------------------------------------------------------------------------------------
# 3. Aggregate K_wide, paste, one rebuild.
aggregate_paste_rebuild "step3" "$step2_start" wide-scale "$WIDE_LOG"

# ---------------------------------------------------------------------------------------------------------------------
# 4. The side-floor shards + the lever case, concurrently. Names from the exe's own listing.
LIST_LOG="$LOGDIR/harvest-step4-list.log"
"$SYS_EXE" --list-tests "[stereo-field-harvest]" > "$LIST_LOG" 2>&1
NAMES="$(tr -d '\r' < "$LIST_LOG" | sed -n 's/^[[:space:]]*\(BassStereoField_SideFloor[A-Za-z0-9_]*Harvest[A-Za-z0-9_]*\)[[:space:]]*$/\1/p' | sort -u)"
SHARDS="$(echo "$NAMES" | grep -c "^BassStereoField_SideFloorHarvest_" || true)"
LEVERS="$(echo "$NAMES" | grep -c "^BassStereoField_SideFloorLeverHarvest$" || true)"
status_line "step4-list shards=$SHARDS lever=$LEVERS log=$LIST_LOG"
if [ "${SHARDS:-0}" -lt 1 ] || [ "${LEVERS:-0}" -ne 1 ]; then
    cat "$LIST_LOG" >&2
    finish 1 "step4: --list-tests found $SHARDS side-floor shard(s) and $LEVERS lever case(s)"
fi
echo "harvest: step 4 runs $SHARDS shards + the lever case, $JOBS at a time"
step4_start="$(date +%s)"
echo "$NAMES" | xargs -P "$JOBS" -I{} bash -c '
    name="$1"; exe="$2"; dir="$3"; status="$4"
    log="$dir/harvest-step4-$name.log"
    s0="$(date +%s)"
    "$exe" "$name" > "$log" 2>&1
    rc=$?
    echo "exit: $rc wall: $(( $(date +%s) - s0 )) s" >> "$log"
    echo "step4-$name exit=$rc wall=$(( $(date +%s) - s0 ))s log=$log" >> "$status"
    exit 0' _ {} "$SYS_EXE" "$LOGDIR" "$STATUS"
FLOOR_LOGS=""
red=0
for name in $NAMES; do
    log="$LOGDIR/harvest-step4-$name.log"
    rc="$(tr -d '\r' < "$log" 2>/dev/null | sed -n 's/^exit: \([0-9-]*\) .*/\1/p' | tail -n 1)"
    if [ "${rc:-1}" != "0" ]; then
        red=$((red + 1))
        echo "harvest: step 4 $name exit=${rc:-?}" >&2
        tail -n 12 "$log" >&2
    fi
    FLOOR_LOGS="$FLOOR_LOGS $log"
done
status_line "step4 wall=$(( $(date +%s) - step4_start ))s red=$red"
[ "$red" -eq 0 ] || finish 1 "step4: $red side-floor harvest case(s) failed"
# shellcheck disable=SC2086
check_stop $FLOOR_LOGS

# ---------------------------------------------------------------------------------------------------------------------
# 5. Aggregate g_floor (needs every shard and the lever), paste, one rebuild.
# shellcheck disable=SC2086
aggregate_paste_rebuild "step5" "$step4_start" side-floor --wide-log "$WIDE_LOG" --expect-shards "$SHARDS" $FLOOR_LOGS

# ---------------------------------------------------------------------------------------------------------------------
# 6. Harvest-dependent per-push cases by name + the phase's per-push filter, concurrently; collected into one log.
STEP6_LOG="$LOGDIR/harvest-step6-cases.log"
STEP6_TMP="$LOGDIR/harvest-step6"
rm -rf "$STEP6_TMP"
mkdir -p "$STEP6_TMP"
CASES="BassStereoField_MonoSumPerBandSmoke BassStereoField_GravityFloorSmoke BassStereoField_LeversAudibleSmoke
BassStereoField_WideOffsetCeilings BassStereoField_ConstantsMatchPlan BassStereoField_SideGainLaw"
FILTER="[stereo-field]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]"
step6_start="$(date +%s)"
pids=""
for c in $CASES; do
    run_case "step6-$c" "$c" "$STEP6_TMP/$c.log" &
    pids="$pids $!"
done
run_case "step6-per-push-filter" "$FILTER" "$STEP6_TMP/per-push-filter.log" &
pids="$pids $!"
for p in $pids; do
    wait "$p"
done

red=0
{
    echo "=== T015 step 6 harvest-dependent per-push cases $(now) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    for c in $CASES per-push-filter; do
        echo
        if [ "$c" = "per-push-filter" ]; then echo "=== $FILTER"; else echo "=== $c"; fi
        tr -d '\r' < "$STEP6_TMP/$c.log"
    done
} > "$STEP6_LOG"
echo "harvest: step 6 summaries"
for c in $CASES per-push-filter; do
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
    label="$c"
    [ "$c" = "per-push-filter" ] && label="$FILTER"
    line="step6 $verdict $label exit=${rc:-?} | $summary"
    echo "$line"
    echo "$line" >> "$STEP6_LOG"
done
rm -rf "$STEP6_TMP"
status_line "step6 wall=$(( $(date +%s) - step6_start ))s red=$red log=$STEP6_LOG"

if [ "$red" -ne 0 ]; then
    finish 1 "step6: $red harvest-dependent run(s) red (see $STEP6_LOG)"
fi
finish 0 "green: K_wide and g_floor harvested and pasted ($WIDE_FLOOR), step 6 green"
