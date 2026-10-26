#!/usr/bin/env bash
# Profundum Phase 8, task T014 / T020 close lane: SC-018 (BassMorph_Cpu) and the FR-083 worst-interval record
# (BassMorph_WorstTransitionIntervalRecord), pinned and isolated.
#
# Run LAST, ALONE, after the machine has idled (Git Bash, from anywhere):
#   bash specs/profundum-phase8-bass-morph/evidence/close/cpu-lane.sh
# Refuses to start while a compiler, a dsp_*_tests.exe or clang-tidy is running (tasks.md T014).
# Runs each case pinned to the performance cores, one after the other:
#   pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe build/windows-x64-release/bin/Release/dsp_systems_tests.exe
#        -ExeArgs "BassMorph_Cpu"
#   pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe build/windows-x64-release/bin/Release/dsp_systems_tests.exe
#        -ExeArgs "BassMorph_WorstTransitionIntervalRecord"
# Output: evidence/run-log/close/cpu-lane.log (overwritten); the cases write evidence/sc018-cpu.txt and
# evidence/worst-interval.txt themselves. The verdict line (SC018 VERDICT + FR083 RECORD) is appended to
# evidence/run-log/T020.txt.
# A red SC018 VERDICT is re-run once alone after the machine has idled; only then is it a defect. Never relax the bar.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO"
SPEC="specs/profundum-phase8-bass-morph"
EXE="build/windows-x64-release/bin/Release/dsp_systems_tests.exe"
LOG="$SPEC/evidence/run-log/close/cpu-lane.log"
RUNLOG="$SPEC/evidence/run-log/T020.txt"

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
echo "=== SC-018 / FR-083 cpu lane $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)" > "$LOG"

# run_case <case name>: one pinned run appended to the log; sets case_status and case_wall.
run_case() {
    local name="$1"
    echo "--- cmd: pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe $EXE -ExeArgs \"$name\"" >> "$LOG"
    local start
    start=$(date +%s)
    set +e
    pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe "$EXE_WIN" -ExeArgs "$name" 2>&1 \
        | tr -d '\r' >> "$LOG"
    case_status=${PIPESTATUS[0]}
    set -e
    case_wall=$(( $(date +%s) - start ))
    echo "--- $name exit: $case_status wall: ${case_wall} s" >> "$LOG"
}

run_case "BassMorph_Cpu"
cpu_status=$case_status
cpu_wall=$case_wall
run_case "BassMorph_WorstTransitionIntervalRecord"
rec_status=$case_status
rec_wall=$case_wall

tail -n 24 "$LOG"

verdict="$(grep -E "^SC018 VERDICT" "$LOG" | tail -n 1 || true)"
record="$(grep -E "^FR083 RECORD" "$LOG" | tail -n 1 || true)"
if [ -z "$verdict" ]; then
    verdict="SC018 VERDICT MISSING (case did not complete; exit $cpu_status; see $LOG)"
fi
if [ -z "$record" ]; then
    record="FR083 RECORD MISSING (case did not complete; exit $rec_status; see $LOG)"
fi
echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) cpu-lane cpu_exit=$cpu_status cpu_wall=${cpu_wall}s rec_exit=$rec_status rec_wall=${rec_wall}s | $verdict | $record" >> "$RUNLOG"
echo "cpu-lane: verdict appended to $RUNLOG" >&2

status=0
if [ "$cpu_status" -ne 0 ]; then status=$cpu_status; elif [ "$rec_status" -ne 0 ]; then status=$rec_status; fi
exit "$status"
