#!/usr/bin/env bash
# Profundum Phase 8, task T008 checkpoint (Group 7): the hidden C-9 coherence probe and the R-2 early CPU timing.
#
# Run by the MAIN LOOP, ALONE, after the Group 7 gate has built dsp_systems_tests (Git Bash, from anywhere):
#   bash specs/profundum-phase8-bass-morph/evidence/close/probe.sh
# Refuses to start while a compiler, a dsp_*_tests.exe or clang-tidy is running (tasks.md T008).
#   1. dsp_systems_tests.exe "BassMorph_SumTapCoherenceProbe"   -> evidence/run-log/close/probe-coherence.log
#      (the case writes evidence/coherence-probe.txt; last line "C9 VERDICT GREEN|RED ...")
#   2. pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe <exe> -ExeArgs "BassMorph_CpuEarlyProbe"
#                                                               -> evidence/run-log/close/probe-cpu.log
#      (the case writes evidence/cpu-early.txt; last line "R2 EARLY GREEN|RED <ns> vs 333.3")
# Both verdict lines, with wall clocks, are appended to evidence/run-log/T008.txt.
# Decision rule (tasks.md T008): C9 GREEN and R2 GREEN -> Group 8. C9 RED -> STOP AND ASK the user (remediation only
# in the showcase pair's B or the laws, spec Q7; no bar relaxed). R2 RED -> STOP AND SURFACE (R-2; budget unchanged).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO"
SPEC="specs/profundum-phase8-bass-morph"
EXE="build/windows-x64-release/bin/Release/dsp_systems_tests.exe"
LOG_C9="$SPEC/evidence/run-log/close/probe-coherence.log"
LOG_R2="$SPEC/evidence/run-log/close/probe-cpu.log"
RUNLOG="$SPEC/evidence/run-log/T008.txt"

busy="$(tasklist | grep -ciE "cl.exe|dsp_.*_tests.exe|clang-tidy" || true)"
if [ "${busy:-0}" -ne 0 ]; then
    echo "probe: refusing to start: $busy competing process(es) running (cl.exe / dsp_*_tests.exe / clang-tidy)" >&2
    tasklist | grep -iE "cl.exe|dsp_.*_tests.exe|clang-tidy" >&2 || true
    exit 3
fi
if [ ! -f "$EXE" ]; then
    echo "probe: $EXE not found; build dsp_systems_tests (Release) first" >&2
    exit 2
fi

mkdir -p "$(dirname "$LOG_C9")" "$(dirname "$RUNLOG")"
EXE_WIN="$(cygpath -w "$REPO/$EXE")"
COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

# ---- 1. C-9 coherence probe ----
{
    echo "=== C-9 coherence probe $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $COMMIT"
    echo "cmd: $EXE \"BassMorph_SumTapCoherenceProbe\""
} > "$LOG_C9"
start=$(date +%s)
set +e
"$EXE" "BassMorph_SumTapCoherenceProbe" 2>&1 | tr -d '\r' >> "$LOG_C9"
status_c9=${PIPESTATUS[0]}
set -e
wall_c9=$(( $(date +%s) - start ))
echo "exit: $status_c9 wall: ${wall_c9} s" >> "$LOG_C9"
tail -n 8 "$LOG_C9"

# ---- 2. R-2 early CPU timing, pinned to the performance cores ----
{
    echo "=== R-2 early CPU probe $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $COMMIT"
    echo "cmd: pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe $EXE -ExeArgs \"BassMorph_CpuEarlyProbe\""
} > "$LOG_R2"
start=$(date +%s)
set +e
pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe "$EXE_WIN" -ExeArgs "BassMorph_CpuEarlyProbe" 2>&1 \
    | tr -d '\r' >> "$LOG_R2"
status_r2=${PIPESTATUS[0]}
set -e
wall_r2=$(( $(date +%s) - start ))
echo "exit: $status_r2 wall: ${wall_r2} s" >> "$LOG_R2"
tail -n 8 "$LOG_R2"

# ---- verdicts ----
v_c9="$(grep -E "^C9 VERDICT" "$LOG_C9" | tail -n 1 || true)"
v_r2="$(grep -E "^R2 EARLY" "$LOG_R2" | tail -n 1 || true)"
[ -z "$v_c9" ] && v_c9="C9 VERDICT MISSING (case did not complete; exit $status_c9; see $LOG_C9)"
[ -z "$v_r2" ] && v_r2="R2 EARLY MISSING (case did not complete; exit $status_r2; see $LOG_R2)"
now="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
{
    echo "$now probe C9 exit=$status_c9 wall=${wall_c9}s | $v_c9"
    echo "$now probe R2 exit=$status_r2 wall=${wall_r2}s | $v_r2"
} >> "$RUNLOG"
echo "probe: verdicts appended to $RUNLOG" >&2
echo "$v_c9"
echo "$v_r2"

if [ "$status_c9" -ne 0 ]; then
    exit "$status_c9"
fi
exit "$status_r2"
