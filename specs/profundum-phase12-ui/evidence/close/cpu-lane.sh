#!/usr/bin/env bash
# ==============================================================================
# Profundum Phase 12 - CPU lane, alone (T038; SC-013; FR-075)
# ==============================================================================
# Runs `node tools/run-cpu-tests.js profundum_tests` (the timing-sensitive
# [performance],[perf],[.perf],[benchmark],[!benchmark],[long] cases, pinned to
# the performance cores on Windows) and writes its console output plus the full
# suite output (which carries every arm's WARN line: producer off and on, MONO,
# POLY, MONO-MAX, POLY-MAX, automation arms) to evidence/run-log/close/cpu.log.
#
# Precondition: the T036 and T037 lanes have finished and the machine has idled.
# Run it LAST and ALONE: nothing else executing, no build, no other suite.
# It does not build; profundum_tests.exe must already be the close build.
# A red reading is re-run once after the machine has idled; still red is a
# defect reported to the user. No bar relaxed, no workload shrunk.
#
# Last line: CPU GREEN | CPU RED
#
# Usage (from anywhere inside the repository):
#   bash specs/profundum-phase12-ui/evidence/close/cpu-lane.sh
# ==============================================================================
set -uo pipefail

ROOT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
SPEC="$ROOT/specs/profundum-phase12-ui"
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
    echo "[cpu-lane] ---- arm readings (WARN lines) ----"
    if [[ -f "$SUITE_LOG" ]]; then
        grep -iE "pct|%|ratio|arm|producer" "$SUITE_LOG" || echo "[cpu-lane] no arm lines found"
    fi
} >>"$LOG" 2>&1

# Green needs the runner to pass AND the suite to have actually run (a SKIP for
# an unbuilt exe would otherwise read "0/0 suites passed" with exit 0).
if [[ $status -eq 0 ]] && grep -q "^1/1 suites passed" "$LOG"; then
    echo "CPU GREEN" | tee -a "$LOG"
    exit 0
fi
echo "CPU RED" | tee -a "$LOG"
exit 1
