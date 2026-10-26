#!/usr/bin/env bash
# Profundum Phase 5, task T026 close lane: portability + Apple globals (FR-071; SC-023).
#
# Run from the main loop (Git Bash, from anywhere), concurrently with suites.sh / tidy.sh:
#   bash specs/profundum-phase5-shape-character/evidence/close/portability.sh
# Steps (both lanes concurrently, each with a 300 s ceiling):
#   1. node tools/lint-apple-globals.js over the six new headers, the modified dc_blocker.h, the four test
#      headers and the 14 Phase 5 TUs.
#   2. node tools/check-portability.js over the 14 Phase 5 TUs (g++ -fsyntax-only via WSL; the headers are
#      checked through the TUs). It needs WSL to see the repo drive (/mnt/f): the executor's sandboxed run
#      failed with "wsl: Failed to translate 'F:\projects\iterum'" for every TU (only C: was mounted), so it
#      must run unsandboxed from the main loop.
#   3. wsl --shutdown (check-portability boots WSL; WSLg traces otherwise keep writing to C:).
# Output: evidence/run-log/close/portability.log (both outputs, exit codes, wall clocks; last line
# "PORTABILITY GREEN|RED"). Both must report 0 findings; quote the log into evidence/run-log/T026.txt.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase5-shape-character"
LOGDIR="$SPEC/evidence/run-log/close"
LOG="$LOGDIR/portability.log"
CEILING_S=300
mkdir -p "$LOGDIR"

HEADERS=(
    dsp/include/krate/dsp/core/closed_form_adaa2.h
    dsp/include/krate/dsp/processors/keytracked_tilt.h
    dsp/include/krate/dsp/processors/oversampled_residual_path.h
    dsp/include/krate/dsp/processors/rectifier.h
    dsp/include/krate/dsp/systems/bass_character_stage.h
    dsp/include/krate/dsp/systems/shape_morph.h
    dsp/include/krate/dsp/primitives/dc_blocker.h
    dsp/tests/unit/processors/shape_character_metric_helpers.h
    dsp/tests/unit/systems/bass_character_stage_probe.h
    dsp/tests/unit/systems/shape_character_test_helpers.h
    dsp/tests/unit/systems/shape_morph_probe.h
)
TUS=(
    dsp/tests/unit/core/closed_form_adaa2_test.cpp
    dsp/tests/unit/processors/keytracked_tilt_test.cpp
    dsp/tests/unit/processors/oversampled_residual_path_test.cpp
    dsp/tests/unit/processors/rectifier_nonfinite_test.cpp
    dsp/tests/unit/processors/rectifier_test.cpp
    dsp/tests/unit/processors/shape_character_metric_helpers_test.cpp
    dsp/tests/unit/systems/bass_character_rlaw_test.cpp
    dsp/tests/unit/systems/bass_character_spectral_test.cpp
    dsp/tests/unit/systems/bass_character_stage_test.cpp
    dsp/tests/unit/systems/shape_character_chain_test.cpp
    dsp/tests/unit/systems/shape_character_nonfinite_test.cpp
    dsp/tests/unit/systems/shape_character_perf_test.cpp
    dsp/tests/unit/systems/shape_morph_spectral_test.cpp
    dsp/tests/unit/systems/shape_morph_test.cpp
)

for f in "${HEADERS[@]}" "${TUS[@]}"; do
    [[ -f "$f" ]] || { echo "missing file: $f" | tee "$LOG"; exit 2; }
done

lane() { # name, log, command...
    local name="$1" out="$2"; shift 2
    local t0=$SECONDS
    timeout "$CEILING_S" "$@" > "$out" 2>&1
    local rc=$?
    echo "$name exit=$rc wall=$((SECONDS - t0))s" >> "$out"
    return $rc
}

lane lint "$LOGDIR/portability-lint.tmp" node tools/lint-apple-globals.js "${HEADERS[@]}" "${TUS[@]}" &
PID_LINT=$!
lane port "$LOGDIR/portability-gxx.tmp" node tools/check-portability.js "${TUS[@]}" &
PID_PORT=$!
wait "$PID_LINT"; RC_LINT=$?
wait "$PID_PORT"; RC_PORT=$?

wsl --shutdown > /dev/null 2>&1

{
    echo "== lint-apple-globals =="
    cat "$LOGDIR/portability-lint.tmp"
    echo
    echo "== check-portability =="
    cat "$LOGDIR/portability-gxx.tmp"
    echo
    echo "wsl --shutdown done"
    if [[ $RC_LINT -eq 0 && $RC_PORT -eq 0 ]] \
        && ! grep -q "Failed to translate" "$LOGDIR/portability-gxx.tmp"; then
        echo "PORTABILITY GREEN"
    else
        echo "PORTABILITY RED (lint=$RC_LINT port=$RC_PORT; 124 = ${CEILING_S} s ceiling hit)"
    fi
} > "$LOG"
rm -f "$LOGDIR/portability-lint.tmp" "$LOGDIR/portability-gxx.tmp"

cat "$LOG"
[[ $RC_LINT -eq 0 && $RC_PORT -eq 0 ]]
