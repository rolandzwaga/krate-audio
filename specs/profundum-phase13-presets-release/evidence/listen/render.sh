#!/usr/bin/env bash
# Profundum Phase 13 audition renders (T022; FR-050, SC-023; C-9 rule 5; plan 6 rule 5, 8 "Audition").
# Copied from specs/profundum-phase12-ui/evidence/listen/render.sh and re-pointed at the factory library.
#
# Run by the MAIN LOOP (Git Bash, from anywhere):
#   bash specs/profundum-phase13-presets-release/evidence/listen/render.sh
# Steps (each step's wall clock is printed and logged):
#   1. one build (-- /nr:false) of profundum_tests -> evidence/listen/build.log
#   2. profundum_tests.exe "Profundum_PresetAuditionRenders" (hidden [.listen] case; writes <category>/*.wav,
#      manifest.txt, listening-sheet.md and fundamental-loss.csv into this folder) -> evidence/listen/render.log
#   3. checks (SC-023): manifest lines = 2N + gesture count (N E1/C3 pairs, gesture "none"); every manifest file
#      exists; the sheet holds N "Verdict (user):" lines, all empty; exactly one fundamental-loss note per flagged
#      preset (fundamental-loss.csv rows = notes = the case's flagged count)
# Not timing-sensitive: it may run beside other lanes.
# Last line: LISTEN GREEN | LISTEN RED
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
BUILD_DIR="build/windows-x64-release"
EXE="$BUILD_DIR/bin/Release/profundum_tests.exe"
CASE="Profundum_PresetAuditionRenders"
OUT="specs/profundum-phase13-presets-release/evidence/listen"
BUILD_LOG="$OUT/build.log"
RENDER_LOG="$OUT/render.log"
MANIFEST="$OUT/manifest.txt"
SHEET="$OUT/listening-sheet.md"
LOSS="$OUT/fundamental-loss.csv"
mkdir -p "$OUT"

failed=0
commit="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

red() {
    echo "check: FAIL $*"
    failed=1
}

# ---- 1. build ----
start=$(date +%s)
{
    echo "=== build $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $commit"
    echo "cmd: \"$CMAKE\" --build $BUILD_DIR --config Release --target profundum_tests -- /nr:false"
} > "$BUILD_LOG"
"$CMAKE" --build "$BUILD_DIR" --config Release --target profundum_tests -- /nr:false \
    2>&1 | tr -d '\r' >> "$BUILD_LOG"
status=${PIPESTATUS[0]}
wall=$(( $(date +%s) - start ))
echo "exit: $status wall: ${wall} s" >> "$BUILD_LOG"
echo "build: exit $status wall ${wall} s (log $BUILD_LOG)"
if [ "$status" -ne 0 ] || [ ! -f "$EXE" ]; then
    tail -n 20 "$BUILD_LOG"
    echo "LISTEN RED"
    exit 1
fi

# ---- 2. the audition render set ----
rm -f "$MANIFEST" "$LOSS"
start=$(date +%s)
{
    echo "=== render $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $commit"
    echo "cmd: $EXE \"$CASE\""
} > "$RENDER_LOG"
"$EXE" "$CASE" 2>&1 | tr -d '\r' >> "$RENDER_LOG"
status=${PIPESTATUS[0]}
wall=$(( $(date +%s) - start ))
echo "exit: $status wall: ${wall} s" >> "$RENDER_LOG"
echo "render: exit $status wall ${wall} s (log $RENDER_LOG)"
tail -n 5 "$RENDER_LOG"
if [ "$status" -ne 0 ]; then
    red "the case exited $status"
fi

# ---- 3. checks (SC-023) ----
summary="$(grep -E '^# Profundum_PresetAuditionRenders: presets=[0-9]+ wavs=[0-9]+ gestures=[0-9]+ flagged=[0-9]+$' \
    "$RENDER_LOG" | tail -n 1)"
if [ -z "$summary" ]; then
    red "no summary line in $RENDER_LOG"
    echo "LISTEN RED"
    exit 1
fi
field() { echo "$summary" | sed -E "s/.* $1=([0-9]+).*/\\1/"; }
N=$(field presets)
W=$(field wavs)
G=$(field gestures)
K=$(field flagged)
echo "case: presets=$N wavs=$W gestures=$G flagged=$K"

if [ ! -f "$MANIFEST" ] || [ ! -f "$SHEET" ] || [ ! -f "$LOSS" ]; then
    red "missing output (manifest.txt, listening-sheet.md or fundamental-loss.csv)"
    echo "LISTEN RED"
    exit 1
fi

lines=$(grep -vc '^#' "$MANIFEST")
notes=$(grep -v '^#' "$MANIFEST" | grep -c ',none$')
gest=$(( lines - notes ))
echo "manifest: $lines lines ($notes E1/C3, $gest gesture)"
[ "$N" -ge 1 ] || red "N = $N"
[ "$notes" -eq $(( 2 * N )) ] || red "E1/C3 lines $notes != 2N = $(( 2 * N ))"
[ "$gest" -eq "$G" ] || red "gesture lines $gest != the case's gesture count $G"
[ "$lines" -eq $(( 2 * N + G )) ] || red "manifest lines $lines != 2N + gestures = $(( 2 * N + G ))"
[ "$lines" -eq "$W" ] || red "manifest lines $lines != the case's WAV count $W"

missing=0
while IFS= read -r line; do
    rel="${line#\"}"
    rel="${rel%%\"*}"
    if [ ! -s "$OUT/$rel" ]; then
        echo "missing WAV: $OUT/$rel"
        missing=$(( missing + 1 ))
    fi
done < <(grep -v '^#' "$MANIFEST")
echo "manifest: $missing missing WAV files"
[ "$missing" -eq 0 ] || red "$missing manifest files missing"

verdicts=$(grep -c '^Verdict (user):' "$SHEET")
emptyVerdicts=$(grep -cE '^Verdict \(user\):[[:space:]]*$' "$SHEET")
lossNotes=$(grep -c '^Fundamental-loss note (C-9 rule 5):' "$SHEET")
lossRows=$(( $(wc -l < "$LOSS") - 1 ))
echo "sheet: $verdicts verdict lines ($emptyVerdicts empty), $lossNotes fundamental-loss notes; csv rows $lossRows"
[ "$verdicts" -eq "$N" ] || red "verdict lines $verdicts != N = $N"
[ "$emptyVerdicts" -eq "$N" ] || red "empty verdict lines $emptyVerdicts != N = $N (an agent never fills a verdict)"
[ "$lossNotes" -eq "$K" ] || red "fundamental-loss notes $lossNotes != flagged presets $K"
[ "$lossRows" -eq "$K" ] || red "fundamental-loss.csv rows $lossRows != flagged presets $K"

if [ "$failed" -ne 0 ]; then
    echo "LISTEN RED"
    exit 1
fi
echo "LISTEN GREEN"
