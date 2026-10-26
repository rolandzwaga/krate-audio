#!/usr/bin/env bash
# Profundum Phase 9 listening renders (T021; FR-070 Listen rows, FR-071, SC-035).
#
# Run by the MAIN LOOP after the Group 11 gate build of dsp_systems_tests (Git Bash, from anywhere):
#   bash specs/profundum-phase9-voice-engine/evidence/close/listen.sh
# Runs the hidden case "VoiceEngine_ListeningRenders" ([.listen]) by name. It writes one 32-bit float stereo WAV per
# Listen row to evidence/listen/ (h01 ... h28b, about 176 s of audio in all) and evidence/listen/manifest.txt
# (segment start times). Not timing-sensitive: it may run beside other lanes. Log: evidence/run-log/close/listen.log;
# the LISTEN lines and the wall clock are appended to evidence/run-log/T021.txt.
# Afterwards: present evidence/listening-sheet.md to the user at comply; the "Verdict (user)" lines are the user's.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO"
SPEC="specs/profundum-phase9-voice-engine"
EXE="build/windows-x64-release/bin/Release/dsp_systems_tests.exe"
CASE="VoiceEngine_ListeningRenders"
LOG="$SPEC/evidence/run-log/close/listen.log"
RUNLOG="$SPEC/evidence/run-log/T021.txt"

if [ ! -f "$EXE" ]; then
    echo "listen: $EXE not found; build dsp_systems_tests (Release) first" >&2
    exit 2
fi
mkdir -p "$(dirname "$LOG")" "$SPEC/evidence/listen"

commit="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
{
    echo "=== listen $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $commit"
    echo "cmd: $EXE \"$CASE\""
} > "$LOG"
start=$(date +%s)
set +e
"$EXE" "$CASE" 2>&1 | tr -d '\r' >> "$LOG"
status=${PIPESTATUS[0]}
set -e
wall=$(( $(date +%s) - start ))
echo "exit: $status wall: ${wall} s" >> "$LOG"
tail -n 14 "$LOG"

{
    echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) listen exit=$status wall=${wall}s commit $commit"
    grep -E "^LISTEN " "$LOG" || echo "LISTEN lines MISSING (case did not complete; see $LOG)"
} >> "$RUNLOG"
echo "listen: LISTEN lines appended to $RUNLOG" >&2
exit "$status"
