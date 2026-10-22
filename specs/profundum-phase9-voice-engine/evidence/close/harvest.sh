#!/usr/bin/env bash
# Profundum Phase 9, task T014: the macro-row and SC-011 fingerprint harvests (plan S10.5; tasks.md T014, deviations 6
# and 12). Structure after specs/profundum-phase7-stereo-field/evidence/close/harvest.sh.
#
# Run by the MAIN LOOP after the Group 10 build of dsp_systems_tests (Git Bash, from anywhere):
#   bash specs/profundum-phase9-voice-engine/evidence/close/harvest.sh
# Then rebuild dsp_systems_tests ONCE (the paste changed profundum_macro_matrix.h and profundum_engine_test.cpp) and
# run the Group 10 gate. This script does not build.
#
# Steps (one status line per step, with exit code and wall clock, to evidence/run-log/close/harvest-status.txt):
#   1. CONCURRENTLY (neither is timing-sensitive; both single-threaded):
#        dsp_systems_tests.exe "VoiceEngine_MacroHarvest"       -> evidence/run-log/close/harvest-macro.log
#          (writes evidence/macro-rows.txt, and evidence/macro-rows.inc only when every macro is green)
#        dsp_systems_tests.exe "VoiceEngine_FingerprintHarvest" -> evidence/run-log/close/harvest-fingerprint.log
#          (writes evidence/fingerprint.inc)
#      The fingerprint is independent of the macro rows: the SC-011 schedule runs every macro at the neutral, where
#      each contribution is +-0 and apply() is skipped field by field (profundum_macro_matrix.h), so the paste
#      cannot move it beyond codegen noise (compared at kSampleTolerance).
#   2. A "HARVEST RED <macro>" line in the log or in evidence/macro-rows.txt -> STOP AND SURFACE, exit 3, nothing pasted.
#   3. Both .inc files must have been written by this run; then
#        node paste-harvest.js macro-rows fingerprint
#      (all-or-nothing; it refuses on a missing marker pair, a sign change or a malformed row, and prints the diff).
# Exit codes: 0 green (pasted; rebuild next); 1 a harvest case, a fresh-file check or the paste failed; 2 environment;
# 3 STOP AND SURFACE (HARVEST RED: take the table to the user; no bar is changed).
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase9-voice-engine"
EXE="build/windows-x64-release/bin/Release/dsp_systems_tests.exe"
EVIDENCE="$SPEC/evidence"
LOGDIR="$EVIDENCE/run-log/close"
STATUS="$LOGDIR/harvest-status.txt"
PASTE="$EVIDENCE/close/paste-harvest.js"
MACRO_LOG="$LOGDIR/harvest-macro.log"
FP_LOG="$LOGDIR/harvest-fingerprint.log"
MACRO_TXT="$EVIDENCE/macro-rows.txt"
MACRO_INC="$EVIDENCE/macro-rows.inc"
FP_INC="$EVIDENCE/fingerprint.inc"

[ -f "$EXE" ] || { echo "harvest: $EXE not found (build dsp_systems_tests first)" >&2; exit 2; }
command -v node > /dev/null 2>&1 || { echo "harvest: node not found" >&2; exit 2; }
mkdir -p "$LOGDIR"

now() { date -u +%Y-%m-%dT%H:%M:%SZ; }
status_line() { echo "$*" >> "$STATUS"; }
run_start="$(date +%s)"
echo "START $(now) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown) nproc $(nproc 2>/dev/null || echo ?)" > "$STATUS"

finish() {
    status_line "DONE $(now) exit=$1 wall=$(( $(date +%s) - run_start ))s $2"
    echo "harvest: $2 (exit $1; status $STATUS)"
    exit "$1"
}

# run_case <label> <case name> <log>: runs the systems exe on one case, appends exit + wall to the log.
run_case() {
    local label="$1" name="$2" log="$3"
    local s0 rc
    s0="$(date +%s)"
    echo "=== $label $(now) cmd: $EXE \"$name\"" > "$log"
    "$EXE" "$name" >> "$log" 2>&1
    rc=$?
    echo "exit: $rc wall: $(( $(date +%s) - s0 )) s" >> "$log"
    status_line "$label exit=$rc wall=$(( $(date +%s) - s0 ))s log=$log"
    return "$rc"
}

exit_of() { tr -d '\r' < "$1" 2> /dev/null | sed -n 's/^exit: \([0-9-]*\) .*/\1/p' | tail -n 1; }

# The file must exist and have been (re)written since the step started.
require_fresh() {
    if [ ! -f "$1" ] || [ "$(stat -c %Y "$1")" -lt "$2" ]; then
        finish 1 "$1 was not written by this run"
    fi
}

# ---------------------------------------------------------------------------------------------------------------------
# 1. Both harvest cases, concurrently.
step1_start="$(date +%s)"
run_case "step1-macro" "VoiceEngine_MacroHarvest" "$MACRO_LOG" &
macro_pid=$!
run_case "step1-fingerprint" "VoiceEngine_FingerprintHarvest" "$FP_LOG" &
fp_pid=$!
wait "$macro_pid"
wait "$fp_pid"
status_line "step1 wall=$(( $(date +%s) - step1_start ))s"
macro_rc="$(exit_of "$MACRO_LOG")"
fp_rc="$(exit_of "$FP_LOG")"

# ---------------------------------------------------------------------------------------------------------------------
# 2. HARVEST RED -> STOP AND SURFACE (checked before the exit code: a red harvest also fails its CHECK).
red="$(grep -h "^HARVEST RED" "$MACRO_LOG" "$MACRO_TXT" 2> /dev/null | tr -d '\r' | sort -u)"
if [ -n "$red" ]; then
    echo "harvest: STOP AND SURFACE -- return to the user before anything else runs; nothing pasted" >&2
    echo "$red" >&2
    tr -d '\r' < "$MACRO_TXT" 2> /dev/null | sed -n '/^| Row /,$p' >&2
    status_line "STOP AND SURFACE: $(echo "$red" | tr '\n' ' ')"
    finish 3 "HARVEST RED (the table is above and in $MACRO_TXT; the search log is $MACRO_LOG)"
fi
if [ "${macro_rc:-1}" != "0" ]; then
    tail -n 20 "$MACRO_LOG" >&2
    finish 1 "VoiceEngine_MacroHarvest failed (exit ${macro_rc:-?}, $MACRO_LOG)"
fi
if [ "${fp_rc:-1}" != "0" ]; then
    tail -n 20 "$FP_LOG" >&2
    finish 1 "VoiceEngine_FingerprintHarvest failed (exit ${fp_rc:-?}, $FP_LOG)"
fi
for log in "$MACRO_LOG" "$FP_LOG"; do
    if tr -d '\r' < "$log" | grep -q "No test cases matched"; then
        finish 1 "$log: the case did not run (No test cases matched)"
    fi
done
grep -h "^T14 CHOSEN" "$MACRO_LOG" | tr -d '\r'

# ---------------------------------------------------------------------------------------------------------------------
# 3. Fresh files, then the paste (all or nothing).
require_fresh "$MACRO_TXT" "$step1_start"
require_fresh "$MACRO_INC" "$step1_start"
require_fresh "$FP_INC" "$step1_start"
PASTE_LOG="$LOGDIR/harvest-paste.log"
node "$PASTE" macro-rows fingerprint > "$PASTE_LOG" 2>&1
paste_rc=$?
cat "$PASTE_LOG"
status_line "step3-paste exit=$paste_rc log=$PASTE_LOG"
[ "$paste_rc" -eq 0 ] || finish 1 "paste-harvest.js failed (exit $paste_rc, $PASTE_LOG)"

finish 0 "green: macro rows and fingerprint pasted; rebuild dsp_systems_tests once, then run the Group 10 gate"
