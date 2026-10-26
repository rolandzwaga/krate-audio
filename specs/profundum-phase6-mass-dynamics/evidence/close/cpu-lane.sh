#!/usr/bin/env bash
# Profundum Phase 6, task T018 / T024 close lane: SC-017 (MassDynamics_CpuBudget) pinned and isolated.
#
# Run LAST, ALONE, after the machine has idled (Git Bash, from anywhere):
#   bash specs/profundum-phase6-mass-dynamics/evidence/close/cpu-lane.sh
# Refuses to start while a compiler, a dsp_*_tests.exe or clang-tidy is running (tasks.md T018).
# Runs the single case pinned to the performance cores (tasks.md deviation 9; spec SC-017):
#   pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe build/windows-x64-release/bin/Release/dsp_systems_tests.exe
#        -ExeArgs "MassDynamics_CpuBudget"
# Output: evidence/run-log/close/cpu-lane.log (overwritten); the case writes evidence/sc017-cpu.txt itself.
# The verdict line is appended to evidence/run-log/T018.txt.
# A red reading is re-run once alone after the machine has idled; only then is it a defect. Never relax the bar.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO"
SPEC="specs/profundum-phase6-mass-dynamics"
EXE="build/windows-x64-release/bin/Release/dsp_systems_tests.exe"
LOG="$SPEC/evidence/run-log/close/cpu-lane.log"
RUNLOG="$SPEC/evidence/run-log/T018.txt"

busy="$(tasklist | grep -ciE "cl.exe|dsp_.*_tests.exe|clang-tidy" || true)"
if [ "${busy:-0}" -ne 0 ]; then
    echo "cpu-lane: refusing to start: $busy competing process(es) running (cl.exe / dsp_*_tests.exe / clang-tidy)" >&2
    tasklist | grep -iE "cl.exe|dsp_.*_tests.exe|clang-tidy" >&2 || true
    exit 3
fi
if [ ! -f "$EXE" ]; then
    echo "cpu-lane: $EXE not found; build dsp_systems_tests (Release) first" >&2
    exit 2
fi

mkdir -p "$(dirname "$LOG")" "$(dirname "$RUNLOG")"
EXE_WIN="$(cygpath -w "$REPO/$EXE")"
{
    echo "=== SC-017 cpu lane $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "cmd: pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe $EXE -ExeArgs \"MassDynamics_CpuBudget\""
} > "$LOG"

set +e
pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe "$EXE_WIN" -ExeArgs "MassDynamics_CpuBudget" 2>&1 \
    | tr -d '\r' >> "$LOG"
status=${PIPESTATUS[0]}
set -e
echo "exit: $status" >> "$LOG"

tail -n 12 "$LOG"

verdict="$(grep -E "^SC017 VERDICT" "$LOG" | tail -n 1 || true)"
gated="$(grep -E "^SC017 GATED" "$LOG" | tail -n 1 || true)"
if [ -z "$verdict" ]; then
    verdict="SC017 VERDICT MISSING (case did not complete; exit $status; see $LOG)"
fi
echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) cpu-lane exit=$status | $verdict | $gated" >> "$RUNLOG"
echo "cpu-lane: verdict appended to $RUNLOG" >&2
exit "$status"
