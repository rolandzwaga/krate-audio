#!/usr/bin/env bash
# ==============================================================================
# Profundum Phase 14 - CPU lane, alone (T035; FR-053; SC-014)
# ==============================================================================
# Runs, strictly one after the other with a cooldown between them:
#   1. node tools/run-cpu-tests.js profundum_tests
#        Profundum_ProcessorCpuMorphMod (SC-014 (a): MONO / 4-voice % of a core,
#        control S2 / S ratio), Profundum_PresetCpu (SC-014 (c): every preset's
#        SC-022 MONO / POLY4 reading as stored), plus the Phase 9-13 arms.
#   2. node tools/run-cpu-tests.js dsp_systems_tests
#        VoiceEngine_WorstIntervalMorphMod (SC-014 (b): worst 64-sample block
#        ratio vs bar 1.0) and the SC-014 (d) advanceInterval ns-per-call lines.
# run-cpu-tests.js runs the timing-sensitive tags ([performance],[perf],[.perf],
# [benchmark],[!benchmark],[long]) pinned to the performance cores on Windows and
# writes each suite's full output to f:/tmp/cpu_<target>.log; this script copies
# both into evidence/run-log/close/cpu.log and extracts the SC-014 / SC-022 lines.
#
# Precondition: T031 (suites.sh), T032 (portability.sh), T033 (fastmath-repro.sh)
# and T034 (tidy.sh) have finished and the machine has idled. NEVER run straight
# after the clang-tidy lane (Phase 12 handoff 13.5: one such run read every arm
# 10-19 % slow). Run it LAST and ALONE: nothing else executing, no build, no other
# suite. It does not build; both exes must already be the close build.
# The dsp_systems_tests timing lane is multi-hour (a full run measured 5 h 17 min):
# start this script detached (Start-Process + this log), never as a Bash
# background task (2 h ceiling).
# A red reading is re-run once after the machine has idled; still red is a defect
# reported to the user. No budget relaxed, no workload shrunk.
#
# Last line: CPU GREEN | CPU RED
#
# Usage (from anywhere inside the repository):
#   bash specs/profundum-phase14-morph-modulation/evidence/close/cpu-lane.sh
#   SETTLE_S=120 bash ...   (longer cooldown between the two suites; default 60)
# ==============================================================================
set -uo pipefail

ROOT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
SPEC="$ROOT/specs/profundum-phase14-morph-modulation"
LOG_DIR="$SPEC/evidence/run-log/close"
LOG="$LOG_DIR/cpu.log"
BIN="$ROOT/build/windows-x64-release/bin/Release"
TARGETS=(profundum_tests dsp_systems_tests)
# Cooldown between the two runner invocations: run-cpu-tests.js only settles
# between suites of ONE invocation, and back-to-back timing runs measure the
# previous run's thermal cooldown (tools/run-cpu-tests.js "SECOND CLAUSE").
SETTLE_S="${SETTLE_S:-60}"

mkdir -p "$LOG_DIR"
: >"$LOG"

missing=0
for t in "${TARGETS[@]}"; do
    if [[ ! -f "$BIN/$t.exe" ]]; then
        echo "[cpu-lane] $t.exe not built: $BIN/$t.exe" | tee -a "$LOG"
        missing=1
    fi
done
if [[ $missing -ne 0 ]]; then
    echo "CPU RED" | tee -a "$LOG"
    exit 1
fi

# run-cpu-tests.js resolves its BIN path relative to the working directory.
cd "$ROOT" || exit 1

lane_start=$(date +%s)
echo "[cpu-lane] started $(date '+%Y-%m-%d %H:%M:%S')" | tee -a "$LOG"

declare -A STATUS WALL
first=1
for t in "${TARGETS[@]}"; do
    suite_log="f:/tmp/cpu_${t}.log"
    # A stale suite log from an earlier lane must not be quoted as this run's output.
    rm -f "$suite_log"
    if [[ $first -eq 0 && $SETTLE_S -gt 0 ]]; then
        echo "[cpu-lane] settle ${SETTLE_S} s before $t" | tee -a "$LOG"
        sleep "$SETTLE_S"
    fi
    first=0
    echo "[cpu-lane] node tools/run-cpu-tests.js $t  ($(date '+%H:%M:%S'))" | tee -a "$LOG"
    start=$(date +%s)
    node tools/run-cpu-tests.js "$t" 2>&1 | tee -a "$LOG"
    STATUS[$t]=${PIPESTATUS[0]}
    WALL[$t]=$(( $(date +%s) - start ))
    echo "[cpu-lane] $t runner exit ${STATUS[$t]}, wall clock ${WALL[$t]} s" | tee -a "$LOG"
done
lane_wall=$(( $(date +%s) - lane_start ))

PF_LOG="f:/tmp/cpu_profundum_tests.log"
SY_LOG="f:/tmp/cpu_dsp_systems_tests.log"

{
    for t in "${TARGETS[@]}"; do
        echo
        echo "[cpu-lane] ---- full suite output (f:/tmp/cpu_${t}.log) ----"
        if [[ -f "f:/tmp/cpu_${t}.log" ]]; then
            cat "f:/tmp/cpu_${t}.log"
        else
            echo "[cpu-lane] suite log missing"
        fi
    done
    echo
    echo "[cpu-lane] ---- SC-014 (a) Profundum_ProcessorCpuMorphMod (MONO / 4 voices / control S2 / S) ----"
    grep -E -A2 "SC-014 " "$PF_LOG" 2>/dev/null || echo "[cpu-lane] no SC-014 lines found"
    echo "[cpu-lane] ---- SC-014 (b) VoiceEngine_WorstIntervalMorphMod / (d) advanceInterval ns per call ----"
    grep -E "SC014B|SC014D|SC-014 \(b\)" "$SY_LOG" 2>/dev/null || echo "[cpu-lane] no SC014B / SC014D lines found"
    echo "[cpu-lane] ---- SC-014 (c) Profundum_PresetCpu (SC-022 per-preset MONO / POLY4 / control / max) ----"
    grep -E "SC-022" "$PF_LOG" 2>/dev/null || echo "[cpu-lane] no SC-022 lines found"
    echo "[cpu-lane] ---- other profundum arm readings (SC-013 / SC-015 / SC-020) ----"
    grep -E "SC-013|SC-015|SC-020" "$PF_LOG" 2>/dev/null || echo "[cpu-lane] no arm lines found"
    echo
    echo "[cpu-lane] summary:"
    for t in "${TARGETS[@]}"; do
        echo "[cpu-lane]   $t: runner exit ${STATUS[$t]}, wall clock ${WALL[$t]} s"
    done
    echo "[cpu-lane]   lane wall clock ${lane_wall} s"
} >>"$LOG" 2>&1

# Green needs both runners to pass, each suite to have actually run (a SKIP for an
# unbuilt exe reads "0/0 suites passed" with exit 0), the SC-014 (a) / (b) / (d)
# and SC-022 readings to be present (the cases ran: they are hidden [.perf] cases
# and only run because the runner names [.perf]), and no control ratio INVALID.
red=()
for t in "${TARGETS[@]}"; do
    [[ ${STATUS[$t]} -eq 0 ]] || red+=("$t-runner-exit-${STATUS[$t]}")
done
n_passed=$(grep -c "^1/1 suites passed" "$LOG")
[[ $n_passed -eq 2 ]] || red+=("suites-passed-$n_passed-of-2")
grep -q "SC-014 .*best ns P" "$PF_LOG" 2>/dev/null || red+=("no-SC-014a-reading")
grep -q "SC014B VERDICT GREEN" "$SY_LOG" 2>/dev/null || red+=("SC-014b-verdict-not-green")
grep -q "SC014D advanceInterval" "$SY_LOG" 2>/dev/null || red+=("no-SC-014d-reading")
grep -q "SC-022 max MONO" "$PF_LOG" 2>/dev/null || red+=("no-SC-022-max-line")
if grep -E "SC-014 .*INVALID|SC-022 .*INVALID" "$PF_LOG" >/dev/null 2>&1; then red+=("control-ratio-INVALID"); fi

if [[ ${#red[@]} -eq 0 ]]; then
    echo "CPU GREEN" | tee -a "$LOG"
    exit 0
fi
echo "[cpu-lane] red: ${red[*]}" | tee -a "$LOG"
echo "CPU RED" | tee -a "$LOG"
exit 1
