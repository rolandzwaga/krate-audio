#!/usr/bin/env bash
# Profundum Phase 9, task T026 close lane: portability + lints (cross-cutting portability; FR-084).
# Structure copied from specs/profundum-phase8-bass-morph/evidence/close/portability.sh.
#
# Run from the main loop (Git Bash, from anywhere), concurrently with suites.sh / tidy.sh (T025):
#   bash specs/profundum-phase9-voice-engine/evidence/close/portability.sh
# Lanes (all concurrently, each with a 300 s ceiling; arguments per each tool's usage line):
#   1. node tools/check-portability.js <the fourteen Phase 9 TUs>   (g++ -fsyntax-only via WSL; the five new headers,
#      the modified mass_dynamics.h and profundum_voice_engine_test_helpers.h are checked through the TUs). It needs
#      WSL to see the repo drive (/mnt/f); a sandboxed run fails with "wsl: Failed to translate 'F:\projects\iterum'"
#      (only C: mounted), so then run it unsandboxed.
#   2. node tools/lint-apple-globals.js <five new headers> <helpers header> <fourteen TUs>
#   3. node tools/lint-layers.js                   (no arguments: whole dsp/ tree)
#   4. node tools/lint-odr.js                      (no arguments)
#   5. node tools/lint-nonfinite-symbols.js        (no arguments: the GUARDED list)
#   6. node tools/lint-float-bit-goldens.js        (no arguments)
#   7. node tools/lint-simd-aligned-loadstore.js   (no arguments; no new SIMD is expected, a 0 is the evidence)
#   then wsl --shutdown (check-portability boots WSL; WSLg traces otherwise keep writing to C:).
# Output: evidence/run-log/close/portability.log (every lane's output, exit code and wall clock in a fixed order;
# last line "PORTABILITY GREEN|RED"). Quote the log into evidence/run-log/T026.txt.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase9-voice-engine"
LOGDIR="$SPEC/evidence/run-log/close"
LOG="$LOGDIR/portability.log"
TMPDIR_P="$LOGDIR/portability-tmp"
CEILING_S=300
mkdir -p "$LOGDIR" "$TMPDIR_P"
rm -f "$TMPDIR_P"/*.tmp

HEADERS=(
    dsp/include/krate/dsp/primitives/lowpass_biquad_d.h
    dsp/include/krate/dsp/systems/profundum_motion.h
    dsp/include/krate/dsp/systems/profundum_macro_matrix.h
    dsp/include/krate/dsp/systems/profundum_engine_observer.h
    dsp/include/krate/dsp/systems/profundum_voice.h
    dsp/include/krate/dsp/systems/profundum_engine.h
    dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h
)
# The fourteen Phase 9 TUs (same list as tidy.sh / tasks.md File map), including mass_dynamics_oversampling_test.cpp.
TUS=(
    dsp/tests/unit/systems/profundum_voice_engine_helpers_test.cpp
    dsp/tests/unit/systems/profundum_motion_test.cpp
    dsp/tests/unit/systems/profundum_macro_matrix_test.cpp
    dsp/tests/unit/systems/profundum_voice_test.cpp
    dsp/tests/unit/systems/profundum_engine_test.cpp
    dsp/tests/unit/systems/profundum_engine_nonfinite_test.cpp
    dsp/tests/unit/systems/profundum_engine_click_test.cpp
    dsp/tests/unit/systems/profundum_engine_audibility_test.cpp
    dsp/tests/unit/systems/profundum_engine_chain_test.cpp
    dsp/tests/unit/systems/profundum_engine_aliasing_test.cpp
    dsp/tests/unit/systems/profundum_engine_soak_test.cpp
    dsp/tests/unit/systems/profundum_engine_probe_test.cpp
    dsp/tests/unit/systems/profundum_engine_perf_test.cpp
    dsp/tests/unit/systems/mass_dynamics_oversampling_test.cpp
)

for f in "${HEADERS[@]}" "${TUS[@]}"; do
    [[ -f "$f" ]] || { echo "missing file: $f" | tee "$LOG"; echo "PORTABILITY RED (missing input)" >> "$LOG"; exit 2; }
done

start=$SECONDS
lane() { # name, command...
    local name="$1"; shift
    local out="$TMPDIR_P/$name.tmp"
    local t0=$SECONDS
    timeout "$CEILING_S" "$@" > "$out" 2>&1
    local rc=$?
    echo "$name exit=$rc wall=$((SECONDS - t0))s" >> "$out"
    echo "$rc" > "$TMPDIR_P/$name.rc"
}

NAMES=(check-portability lint-apple-globals lint-layers lint-odr lint-nonfinite-symbols lint-float-bit-goldens
       lint-simd-aligned-loadstore)
lane check-portability node tools/check-portability.js "${TUS[@]}" &
lane lint-apple-globals node tools/lint-apple-globals.js "${HEADERS[@]}" "${TUS[@]}" &
lane lint-layers node tools/lint-layers.js &
lane lint-odr node tools/lint-odr.js &
lane lint-nonfinite-symbols node tools/lint-nonfinite-symbols.js &
lane lint-float-bit-goldens node tools/lint-float-bit-goldens.js &
lane lint-simd-aligned-loadstore node tools/lint-simd-aligned-loadstore.js &
wait

wsl --shutdown > /dev/null 2>&1

RED=""
{
    echo "=== T026 portability $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    for n in "${NAMES[@]}"; do
        echo
        echo "== $n =="
        cat "$TMPDIR_P/$n.tmp" 2>/dev/null || echo "(no output file)"
    done
    echo
    echo "wsl --shutdown done; total wall $((SECONDS - start))s"
} > "$LOG"

for n in "${NAMES[@]}"; do
    rc="$(cat "$TMPDIR_P/$n.rc" 2>/dev/null || echo missing)"
    [[ "$rc" == "0" ]] || RED="$RED $n=$rc"
done
# check-portability exits 0 when WSL is unavailable or the drive is not mounted; neither is a green.
if grep -q "Failed to translate" "$TMPDIR_P/check-portability.tmp"; then RED="$RED check-portability=no-drive(sandbox)"; fi
if grep -q "WSL with g++ not available" "$TMPDIR_P/check-portability.tmp"; then RED="$RED check-portability=no-wsl"; fi
if [[ -z "$RED" ]]; then
    echo "PORTABILITY GREEN" >> "$LOG"
else
    echo "PORTABILITY RED ($RED ; 124 = ${CEILING_S} s ceiling hit)" >> "$LOG"
fi
rm -rf "$TMPDIR_P"

cat "$LOG"
[[ -z "$RED" ]]
