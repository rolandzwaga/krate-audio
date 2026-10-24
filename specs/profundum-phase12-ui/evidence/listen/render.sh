#!/usr/bin/env bash
# Profundum Phase 12 listening renders (T010; FR-006, SC-006 (b); Phase 9 FR-097 renders, plan 3.4).
# Landed before ratification under the user's P-1 ruling (spec.md FR-001 row).
#
# Run by the MAIN LOOP (Git Bash, from anywhere):
#   bash specs/profundum-phase12-ui/evidence/listen/render.sh
# Steps (each step's wall clock is printed and logged):
#   1. one build (-- /nr:false) of profundum_tests, plus dsp_systems_tests in the same invocation because step 3's
#      Phase 9 script runs that exe and refuses to run without it -> evidence/listen/build.log
#   2. profundum_tests.exe "Profundum_MainPageRenderSet" (hidden [.listen] case; writes the WAVs and manifest.txt
#      into this folder) -> evidence/listen/render.log
#   3. bash specs/profundum-phase9-voice-engine/evidence/close/listen.sh (Phase 9's renders for the FR-097 verdicts)
# Not timing-sensitive: it may run beside other lanes.
# Last line: LISTEN RENDERS DONE | LISTEN RENDERS FAILED
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
BUILD_DIR="build/windows-x64-release"
EXE="$BUILD_DIR/bin/Release/profundum_tests.exe"
CASE="Profundum_MainPageRenderSet"
OUT="specs/profundum-phase12-ui/evidence/listen"
BUILD_LOG="$OUT/build.log"
RENDER_LOG="$OUT/render.log"
PHASE9="specs/profundum-phase9-voice-engine/evidence/close/listen.sh"
mkdir -p "$OUT"

failed=0
commit="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"

# ---- 1. build ----
start=$(date +%s)
{
    echo "=== build $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $commit"
    echo "cmd: \"$CMAKE\" --build $BUILD_DIR --config Release --target profundum_tests dsp_systems_tests -- /nr:false"
} > "$BUILD_LOG"
"$CMAKE" --build "$BUILD_DIR" --config Release --target profundum_tests dsp_systems_tests -- /nr:false \
    2>&1 | tr -d '\r' >> "$BUILD_LOG"
status=${PIPESTATUS[0]}
wall=$(( $(date +%s) - start ))
echo "exit: $status wall: ${wall} s" >> "$BUILD_LOG"
echo "build: exit $status wall ${wall} s (log $BUILD_LOG)"
if [ "$status" -ne 0 ] || [ ! -f "$EXE" ]; then
    tail -n 20 "$BUILD_LOG"
    echo "LISTEN RENDERS FAILED"
    exit 1
fi

# ---- 2. Phase 12 render set ----
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
    failed=1
fi
wavs=$(find "$OUT" -maxdepth 1 -name '*.wav' | wc -l)
echo "render: $wavs WAV files in $OUT"

# ---- 3. Phase 9 renders (FR-097) ----
start=$(date +%s)
bash "$PHASE9"
status=$?
wall=$(( $(date +%s) - start ))
echo "phase9 listen: exit $status wall ${wall} s"
if [ "$status" -ne 0 ]; then
    failed=1
fi

if [ "$failed" -ne 0 ]; then
    echo "LISTEN RENDERS FAILED"
    exit 1
fi
echo "LISTEN RENDERS DONE"
