#!/usr/bin/env bash
# ==============================================================================
# Profundum Phase 13 - CPU lane, alone (T035; FR-045, FR-062; SC-022, SC-032)
# ==============================================================================
# Runs `node tools/run-cpu-tests.js profundum_tests` (the timing-sensitive
# [performance],[perf],[.perf],[benchmark],[!benchmark],[long] cases, pinned to
# the performance cores on Windows) and writes its console output plus the full
# suite output to evidence/run-log/close/cpu.log. The full output carries every
# WARN line: the SC-022 per-preset MONO / POLY4 readings with their control
# D' / D ratio, the SC-022 max MONO / max POLY4 line, and the Phase 9-12 arms
# (SC-013 / SC-015 / SC-020) that the same suite still measures.
#
# Precondition: T032 (suites.sh), T033 (portability.sh) and T034 (tidy.sh) have
# finished and the machine has idled. NEVER run straight after the clang-tidy
# lane (roadmap handoff 13.5: one such run read every arm 10-19 % slow).
# Run it LAST and ALONE: nothing else executing, no build, no other suite.
# It does not build; profundum_tests.exe must already be the close build.
# A red reading is re-run once after the machine has idled; still red is a
# defect reported to the user. No budget relaxed, no workload shrunk.
#
# Last line: CPU GREEN | CPU RED
#
# Usage (from anywhere inside the repository):
#   bash specs/profundum-phase13-presets-release/evidence/close/cpu-lane.sh
# ==============================================================================
set -uo pipefail

ROOT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
SPEC="$ROOT/specs/profundum-phase13-presets-release"
LOG_DIR="$SPEC/evidence/run-log/close"
LOG="$LOG_DIR/cpu.log"
EXE="$ROOT/build/windows-x64-release/bin/Release/profundum_tests.exe"
# run-cpu-tests.js writes the full suite output here (tools/run-cpu-tests.js).
SUITE_LOG="f:/tmp/cpu_profundum_tests.log"

mkdir -p "$LOG_DIR"

if [[ ! -f "$EXE" ]]; then
    echo "[cpu-lane] profundum_tests.exe not built: $EXE" | tee "$LOG"
    echo "CPU RED" | tee -a "$LOG"
    exit 1
fi

# run-cpu-tests.js resolves its BIN path relative to the working directory.
cd "$ROOT" || exit 1

# A stale suite log from an earlier lane must not be quoted as this run's output.
rm -f "$SUITE_LOG"

start=$(date +%s)
{
    echo "[cpu-lane] started $(date '+%Y-%m-%d %H:%M:%S')"
    echo "[cpu-lane] node tools/run-cpu-tests.js profundum_tests"
} | tee "$LOG"

node tools/run-cpu-tests.js profundum_tests 2>&1 | tee -a "$LOG"
status=${PIPESTATUS[0]}
wall=$(( $(date +%s) - start ))

{
    echo
    echo "[cpu-lane] runner exit $status, wall clock ${wall} s"
    echo "[cpu-lane] ---- full suite output ($SUITE_LOG) ----"
    if [[ -f "$SUITE_LOG" ]]; then
        cat "$SUITE_LOG"
    else
        echo "[cpu-lane] suite log missing"
    fi
    echo "[cpu-lane] ---- SC-022 preset readings (MONO / POLY4 / control D' / D / max) ----"
    if [[ -f "$SUITE_LOG" ]]; then
        grep -E "SC-022" "$SUITE_LOG" || echo "[cpu-lane] no SC-022 lines found"
    fi
    echo "[cpu-lane] ---- other arm readings (SC-013 / SC-015 / SC-020) ----"
    if [[ -f "$SUITE_LOG" ]]; then
        grep -E "SC-013|SC-015|SC-020" "$SUITE_LOG" || echo "[cpu-lane] no arm lines found"
    fi
} >>"$LOG" 2>&1

# Green needs the runner to pass, the suite to have actually run (a SKIP for an
# unbuilt exe would otherwise read "0/0 suites passed" with exit 0), the SC-022
# max line to be present (the preset case ran), and no control ratio INVALID.
red=()
[[ $status -eq 0 ]] || red+=("runner-exit-$status")
grep -q "^1/1 suites passed" "$LOG" || red+=("suite-not-passed")
grep -q "SC-022 max MONO" "$LOG" || red+=("no-SC-022-max-line")
if grep -E "SC-022 .*control D' / D = .*INVALID" "$LOG" >/dev/null; then red+=("control-ratio-INVALID"); fi

if [[ ${#red[@]} -eq 0 ]]; then
    echo "CPU GREEN" | tee -a "$LOG"
    exit 0
fi
echo "[cpu-lane] red: ${red[*]}" | tee -a "$LOG"
echo "CPU RED" | tee -a "$LOG"
exit 1
