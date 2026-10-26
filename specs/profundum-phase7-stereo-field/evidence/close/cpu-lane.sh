#!/usr/bin/env bash
# Profundum Phase 7, task T014 / T021 close lane: SC-013 (BassStereoField_Cpu) pinned and isolated.
#
# Run LAST, ALONE, after the machine has idled (Git Bash, from anywhere):
#   bash specs/profundum-phase7-stereo-field/evidence/close/cpu-lane.sh
# Refuses to start while a compiler, a dsp_*_tests.exe or clang-tidy is running (tasks.md T014).
# Runs the single case pinned to the performance cores (tasks.md deviation 9; spec SC-013):
#   pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe build/windows-x64-release/bin/Release/dsp_systems_tests.exe
#        -ExeArgs "BassStereoField_Cpu"
# Output: evidence/run-log/close/cpu-lane.log (overwritten); the case writes evidence/sc013-cpu.txt itself.
# The verdict line is appended to evidence/run-log/T014.txt.
# A red reading is re-run once alone after the machine has idled; only then is it a defect. Never relax the bar.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO"
SPEC="specs/profundum-phase7-stereo-field"
EXE="build/windows-x64-release/bin/Release/dsp_systems_tests.exe"
LOG="$SPEC/evidence/run-log/close/cpu-lane.log"
RUNLOG="$SPEC/evidence/run-log/T014.txt"

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
    echo "=== SC-013 cpu lane $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "cmd: pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe $EXE -ExeArgs \"BassStereoField_Cpu\""
} > "$LOG"

start=$(date +%s)
set +e
pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe "$EXE_WIN" -ExeArgs "BassStereoField_Cpu" 2>&1 \
    | tr -d '\r' >> "$LOG"
status=${PIPESTATUS[0]}
set -e
wall=$(( $(date +%s) - start ))
echo "exit: $status wall: ${wall} s" >> "$LOG"

tail -n 16 "$LOG"

verdict="$(grep -E "^SC013 VERDICT" "$LOG" | tail -n 1 || true)"
gated="$(grep -E "^SC013 GATED" "$LOG" | tail -n 1 || true)"
if [ -z "$verdict" ]; then
    verdict="SC013 VERDICT MISSING (case did not complete; exit $status; see $LOG)"
fi
echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) cpu-lane exit=$status wall=${wall}s | $verdict | $gated" >> "$RUNLOG"
echo "cpu-lane: verdict appended to $RUNLOG" >&2
exit "$status"
