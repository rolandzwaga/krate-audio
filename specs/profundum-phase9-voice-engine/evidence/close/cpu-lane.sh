#!/usr/bin/env bash
# Profundum Phase 9, task T020 / T027 close lane: SC-001 (VoiceEngine_CpuMono), SC-002 (VoiceEngine_CpuPoly4),
# SC-003 (VoiceEngine_WorstInterval), SC-004 (VoiceEngine_PlacementCost), then Phase 6's SC-017 CPU case
# (MassDynamics_CpuBudget: its TU consumes the modified systems/mass_dynamics.h), pinned and isolated
# (tasks.md T020, deviation 8; the Phase 8 cpu-lane.sh structure).
#
# Run LAST, ALONE, after the machine has idled (Git Bash, from anywhere):
#   bash specs/profundum-phase9-voice-engine/evidence/close/cpu-lane.sh
# Refuses to start while a compiler, a dsp_*_tests.exe or clang-tidy is running.
# Each case by name, one after the other, 20 s settle between cases (SETTLE_S to change it):
#   pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe build/windows-x64-release/bin/Release/dsp_systems_tests.exe
#        -ExeArgs "<case>"
# Output: evidence/run-log/close/cpu-lane.log (overwritten); the cases write evidence/sc001-cpu.txt,
# evidence/sc002-cpu.txt, evidence/sc003-worst-interval.txt and evidence/placement.txt themselves. The verdict
# lines (SC001 / SC002 / SC003 VERDICT, SC002 RECORD, SC003 RECORD, FR014 READING, SC017 GATED) are appended to
# evidence/run-log/T027.txt.
# A red VERDICT is re-run once alone after the machine has idled; only then is it a defect. Never relax a bar.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO"
SPEC="specs/profundum-phase9-voice-engine"
EXE="build/windows-x64-release/bin/Release/dsp_systems_tests.exe"
LOG="$SPEC/evidence/run-log/close/cpu-lane.log"
RUNLOG="$SPEC/evidence/run-log/T027.txt"
SETTLE_S="${SETTLE_S:-20}"
CASES=(
    "VoiceEngine_CpuMono"
    "VoiceEngine_CpuPoly4"
    "VoiceEngine_WorstInterval"
    "VoiceEngine_PlacementCost"
    "MassDynamics_CpuBudget"
)

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
echo "=== SC-001 .. SC-004 + SC-017 cpu lane $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)" > "$LOG"

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

status=0
summary=""
first=1
for c in "${CASES[@]}"; do
    if [ "$first" -eq 0 ]; then
        sleep "$SETTLE_S"
    fi
    first=0
    run_case "$c"
    summary="$summary $c=exit${case_status}/${case_wall}s"
    if [ "$case_status" -ne 0 ] && [ "$status" -eq 0 ]; then
        status=$case_status
    fi
done

tail -n 40 "$LOG"

lines="$(grep -E "^(SC001 VERDICT|SC002 VERDICT|SC002 RECORD|SC003 VERDICT|SC003 RECORD|SC003 BLOCK|H13 RECORD|FR014 READING|SC017 GATED)" "$LOG" || true)"
for tag in "SC001 VERDICT" "SC002 VERDICT" "SC003 VERDICT" "FR014 READING placement" "SC017 GATED"; do
    if ! printf '%s\n' "$lines" | grep -q "^$tag"; then
        lines="$lines
$tag MISSING (case did not complete; see $LOG)"
    fi
done
{
    echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) cpu-lane$summary"
    printf '%s\n' "$lines" | sed '/^$/d' | sed 's/^/  /'
} >> "$RUNLOG"
echo "cpu-lane: verdict lines appended to $RUNLOG" >&2

exit "$status"
