#!/usr/bin/env bash
# Profundum Phase 5, task T024 close lane: the r-law and loudness harvests (plan T030 / T040; C-4, C-5, C-6).
#
# Run FIRST at close, from the main loop (Git Bash, from anywhere):
#   bash specs/profundum-phase5-shape-character/evidence/close/harvest.sh
# Runs the two hidden [.calibration] cases of dsp_systems_tests CONCURRENTLY, each to its own log:
#   ShapeMorph_LoudnessTableHarvest -> evidence/run-log/close/harvest-loudness.log, writes evidence/harvest-loudness.txt
#   BassCharacter_RLawHarvest       -> evidence/run-log/close/harvest-rlaw.log,     writes evidence/harvest-rlaw.txt
# On a "STOP AND SURFACE" line (R-6 / C-5(b)) it stops, prints the line(s) and exits 4: return to the user with
# evidence/harvest-rlaw.txt before anything else runs (paste-harvest.js must NOT be run).
# Otherwise, on success: node specs/profundum-phase5-shape-character/evidence/close/paste-harvest.js, then rebuild.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2
SPEC="specs/profundum-phase5-shape-character"
EXE="build/windows-x64-release/bin/Release/dsp_systems_tests.exe"
LOGDIR="$SPEC/evidence/run-log/close"
LOUD_LOG="$LOGDIR/harvest-loudness.log"
RLAW_LOG="$LOGDIR/harvest-rlaw.log"
LOUD_TXT="$SPEC/evidence/harvest-loudness.txt"
RLAW_TXT="$SPEC/evidence/harvest-rlaw.txt"

if [ ! -f "$EXE" ]; then
    echo "harvest: $EXE not found; build dsp_systems_tests (Release) first" >&2
    exit 2
fi
mkdir -p "$LOGDIR"

start_epoch="$(date +%s)"
echo "harvest: start $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

"$EXE" "ShapeMorph_LoudnessTableHarvest" > "$LOUD_LOG" 2>&1 &
loud_pid=$!
"$EXE" "BassCharacter_RLawHarvest" > "$RLAW_LOG" 2>&1 &
rlaw_pid=$!

wait "$loud_pid"
loud_status=$?
wait "$rlaw_pid"
rlaw_status=$?
end_epoch="$(date +%s)"

echo "exit: $loud_status wall: $((end_epoch - start_epoch)) s" >> "$LOUD_LOG"
echo "exit: $rlaw_status wall: $((end_epoch - start_epoch)) s" >> "$RLAW_LOG"
echo "harvest: loudness exit $loud_status, r-law exit $rlaw_status, wall $((end_epoch - start_epoch)) s"

stop_lines="$(grep -h "STOP AND SURFACE" "$LOUD_LOG" "$RLAW_LOG" "$LOUD_TXT" "$RLAW_TXT" 2>/dev/null | tr -d '\r' | sort -u)"
if [ -n "$stop_lines" ]; then
    echo "harvest: STOP AND SURFACE -- return to the user with $RLAW_TXT; do NOT run paste-harvest.js" >&2
    echo "$stop_lines" >&2
    exit 4
fi

if [ "$loud_status" -ne 0 ] || [ "$rlaw_status" -ne 0 ]; then
    echo "harvest: a harvest case failed; see $LOUD_LOG and $RLAW_LOG" >&2
    tail -n 8 "$LOUD_LOG" "$RLAW_LOG" >&2
    exit 1
fi

# Both evidence files must have been (re)written by this run, not left over from an earlier one.
for f in "$LOUD_TXT" "$RLAW_TXT"; do
    if [ ! -f "$f" ] || [ "$(stat -c %Y "$f")" -lt "$start_epoch" ]; then
        echo "harvest: $f was not written by this run" >&2
        exit 1
    fi
done

grep -h "^HARVEST " "$LOUD_TXT" "$RLAW_TXT" | tr -d '\r' | cut -c1-160
echo "harvest: done; next: node $SPEC/evidence/close/paste-harvest.js, then rebuild dsp_systems_tests"
exit 0
