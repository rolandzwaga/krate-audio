#!/usr/bin/env bash
# Profundum Phase 14, task T032 close lane: portability + lints (FR-054; SC-021).
# Structure copied from specs/profundum-phase13-presets-release/evidence/close/portability.sh.
#
# Run (Git Bash, from anywhere; UNSANDBOXED, WSL must see /mnt/f):
#   bash specs/profundum-phase14-morph-modulation/evidence/close/portability.sh
# Lanes (concurrently, each with a 300 s ceiling):
#   1. node tools/check-portability.js <the changed .cpp files>  (g++ -fsyntax-only via WSL). The list is
#      git diff --name-only 3dfc5b7d  UNION  untracked (git ls-files --others --exclude-standard), filtered to \.cpp$.
#      The computed list is authoritative: N in the required line "check-portability: all clear -- N compiled."
#      (no ", M skipped" suffix) must equal its length. TASK_NAMED below are the files tasks.md T032 step 1 names;
#      one MISSING from the computed list is RED (a named change was lost); extra files are compiled and listed.
#      A sandboxed run fails with "wsl: Failed to translate 'F:\projects\iterum'" -> RED, run unsandboxed.
#   2. Lints, each exit 0 (usage lines read in each tool's header):
#        node tools/lint-finite-bitmask.js              (gate; tools/lint-finite-bitmask.js:32)
#        node tools/lint-nonfinite-symbols.js           (gate the guarded files; :54)
#        node tools/lint-ieee-flag-sets.js              (:15)
#        node tools/lint-plugin-editor-baseline.js      (:31)
#        node tools/lint-apple-globals.js <changed .h> <changed .cpp>   ([dir-or-file ...]; :20)
#        node tools/lint-float-bit-goldens.js           (:45)
#        node tools/lint-simd-aligned-loadstore.js      (:31)
#   3. wsl --shutdown (check-portability boots WSL; WSLg traces otherwise keep writing to C:).
# Output: evidence/run-log/close/portability.log (each lane's output, exit code and wall clock; last line
# "PORTABILITY GREEN|RED"). Quote the log into evidence/run-log/T032.txt.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

BASE=3dfc5b7d
SPEC="specs/profundum-phase14-morph-modulation"
LOGDIR="$SPEC/evidence/run-log/close"
LOG="$LOGDIR/portability.log"
TMPDIR_P="$LOGDIR/portability-tmp"
CEILING_S=300
mkdir -p "$LOGDIR" "$TMPDIR_P"
rm -f "$TMPDIR_P"/*.tmp "$TMPDIR_P"/*.rc

# Files named in tasks.md T032 step 1 (each must appear in the computed list).
TASK_NAMED=(
    dsp/tests/unit/systems/profundum_engine_display_test.cpp
    dsp/tests/unit/systems/profundum_engine_morphmod_test.cpp
    dsp/tests/unit/systems/profundum_engine_perf_test.cpp
    dsp/tests/unit/systems/profundum_morph_modulator_test.cpp
    dsp/tests/unit/systems/profundum_voice_engine_helpers_test.cpp
    plugins/profundum/src/controller/controller.cpp
    plugins/profundum/src/processor/processor.cpp
    plugins/profundum/src/ui/morph_indicator_slider.cpp
    plugins/profundum/src/ui/panel_sub_controller.cpp
    plugins/profundum/tests/integration/morph_mod_preset_test.cpp
    plugins/profundum/tests/integration/morph_mod_processor_test.cpp
)

changed() { { git diff --name-only "$BASE" 2>/dev/null; git ls-files --others --exclude-standard 2>/dev/null; } | sort -u; }
mapfile -t TUS < <(changed | grep '\.cpp$')
mapfile -t HEADERS < <(changed | grep '\.h$')

for f in "${TUS[@]}" "${HEADERS[@]}"; do
    [[ -f "$f" ]] || { echo "missing file: $f" | tee "$LOG"; echo "PORTABILITY RED (missing input)" >> "$LOG"; exit 2; }
done

N_TUS=${#TUS[@]}
MISSING_NAMED="$(comm -23 <(printf '%s\n' "${TASK_NAMED[@]}" | sort) <(printf '%s\n' "${TUS[@]}" | sort))"
EXTRA="$(comm -13 <(printf '%s\n' "${TASK_NAMED[@]}" | sort) <(printf '%s\n' "${TUS[@]}" | sort))"

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

NAMES=(check-portability lint-finite-bitmask lint-nonfinite-symbols lint-ieee-flag-sets lint-plugin-editor-baseline
       lint-apple-globals lint-float-bit-goldens lint-simd-aligned-loadstore)
lane check-portability node tools/check-portability.js "${TUS[@]}" &
lane lint-finite-bitmask node tools/lint-finite-bitmask.js &
lane lint-nonfinite-symbols node tools/lint-nonfinite-symbols.js &
lane lint-ieee-flag-sets node tools/lint-ieee-flag-sets.js &
lane lint-plugin-editor-baseline node tools/lint-plugin-editor-baseline.js &
lane lint-apple-globals node tools/lint-apple-globals.js "${HEADERS[@]}" "${TUS[@]}" &
lane lint-float-bit-goldens node tools/lint-float-bit-goldens.js &
lane lint-simd-aligned-loadstore node tools/lint-simd-aligned-loadstore.js &
wait

wsl --shutdown > /dev/null 2>&1

RED=""
{
    echo "=== T032 portability $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown) base $BASE"
    echo "TUs ($N_TUS):"; printf '  %s\n' "${TUS[@]}"
    echo "headers (${#HEADERS[@]}):"; printf '  %s\n' "${HEADERS[@]}"
    for n in "${NAMES[@]}"; do
        echo
        echo "== $n =="
        cat "$TMPDIR_P/$n.tmp" 2>/dev/null || echo "(no output file)"
    done
    echo
    echo "== list cross-check (tasks.md T032 step 1 vs computed) =="
    if [[ -z "$MISSING_NAMED" ]]; then echo "every task-named file present (${#TASK_NAMED[@]})"; else echo "MISSING:"; echo "$MISSING_NAMED"; fi
    if [[ -n "$EXTRA" ]]; then echo "also changed (compiled, informational):"; echo "$EXTRA" | sed 's/^/  /'; fi
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
# The exact required line: every TU compiled, no skipped suffix.
if ! grep -qx "check-portability: all clear -- ${N_TUS} compiled\." "$TMPDIR_P/check-portability.tmp"; then
    RED="$RED check-portability=not-all-clear-${N_TUS}"
fi
[[ -z "$MISSING_NAMED" ]] || RED="$RED task-named-file-missing"

if [[ -z "$RED" ]]; then
    echo "PORTABILITY GREEN" >> "$LOG"
else
    echo "PORTABILITY RED ($RED ; 124 = ${CEILING_S} s ceiling hit)" >> "$LOG"
fi
rm -rf "$TMPDIR_P"

cat "$LOG"
[[ -z "$RED" ]]
