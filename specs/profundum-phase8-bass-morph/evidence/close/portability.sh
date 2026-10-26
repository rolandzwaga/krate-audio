#!/usr/bin/env bash
# Profundum Phase 8, task T019 close lane: portability + Apple globals (FR-087).
#
# Run from the main loop (Git Bash, from anywhere), concurrently with suites.sh / tidy.sh:
#   bash specs/profundum-phase8-bass-morph/evidence/close/portability.sh
# Steps (both lanes concurrently, each with a 300 s ceiling):
#   1. node tools/lint-apple-globals.js over systems/morph_state.h, systems/bass_morph_engine.h, the
#      state-helpers header, the test-helpers header and the eleven Phase 8 TUs.
#   2. node tools/check-portability.js over the eleven Phase 8 TUs (g++ -fsyntax-only via WSL; the headers are
#      checked through the TUs). It needs WSL to see the repo drive (/mnt/f); a sandboxed run fails with
#      "wsl: Failed to translate 'F:\projects\iterum'" (only C: mounted), so then run it unsandboxed.
#   3. wsl --shutdown (check-portability boots WSL; WSLg traces otherwise keep writing to C:).
# Output: evidence/run-log/close/portability.log (both outputs, exit codes, wall clocks; last line
# "PORTABILITY GREEN|RED"). Both must report 0 findings; quote the log into evidence/run-log/T019.txt.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase8-bass-morph"
LOGDIR="$SPEC/evidence/run-log/close"
LOG="$LOGDIR/portability.log"
CEILING_S=300
mkdir -p "$LOGDIR"

HEADERS=(
    dsp/include/krate/dsp/systems/morph_state.h
    dsp/include/krate/dsp/systems/bass_morph_engine.h
    dsp/tests/unit/systems/bass_morph_state_helpers.h
    dsp/tests/unit/systems/bass_morph_test_helpers.h
)
TUS=(
    dsp/tests/unit/systems/morph_state_test.cpp
    dsp/tests/unit/systems/bass_morph_engine_test.cpp
    dsp/tests/unit/systems/bass_morph_helpers_test.cpp
    dsp/tests/unit/systems/bass_morph_trajectory_test.cpp
    dsp/tests/unit/systems/bass_morph_click_test.cpp
    dsp/tests/unit/systems/bass_morph_aliasing_test.cpp
    dsp/tests/unit/systems/bass_morph_monosum_test.cpp
    dsp/tests/unit/systems/bass_morph_chain_test.cpp
    dsp/tests/unit/systems/bass_morph_nonfinite_test.cpp
    dsp/tests/unit/systems/bass_morph_probe_test.cpp
    dsp/tests/unit/systems/bass_morph_perf_test.cpp
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
