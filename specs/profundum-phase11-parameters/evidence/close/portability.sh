#!/usr/bin/env bash
# Profundum Phase 11, task T032 close lane: portability (SC-001).
# Structure copied from specs/profundum-phase10-plugin-scaffold/evidence/close/portability.sh.
#
# Run (Git Bash, from anywhere), concurrently with suites.sh / tidy.sh (T031):
#   bash specs/profundum-phase11-parameters/evidence/close/portability.sh
# Lanes (concurrently, each with a 300 s ceiling):
#   1. node tools/check-portability.js <the changed .cpp files>  (g++ -fsyntax-only via WSL). The list is
#      git diff --name-only 565f195d  UNION  untracked (git ls-files --others --exclude-standard), filtered to \.cpp$;
#      it must equal the 20-file list of tasks.md T032 step 1. Required line:
#      "check-portability: all clear -- 20 compiled." with no ", M skipped" suffix. WSL must see the repo drive
#      (/mnt/f); a sandboxed run fails with "wsl: Failed to translate 'F:\projects\iterum'" -> run unsandboxed.
#   2. node tools/lint-apple-globals.js <changed/new .h files> <the .cpp list>
#      (usage line, tools/lint-apple-globals.js:20: node tools/lint-apple-globals.js [dir-or-file ...]).
#   then wsl --shutdown (check-portability boots WSL; WSLg traces otherwise keep writing to C:).
# Output: evidence/run-log/close/portability.log (each lane's output, exit code and wall clock; last line
# "PORTABILITY GREEN|RED"). Quote the log into evidence/run-log/T032.txt.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

BASE=565f195d
SPEC="specs/profundum-phase11-parameters"
LOGDIR="$SPEC/evidence/run-log/close"
LOG="$LOGDIR/portability.log"
TMPDIR_P="$LOGDIR/portability-tmp"
CEILING_S=300
mkdir -p "$LOGDIR" "$TMPDIR_P"
rm -f "$TMPDIR_P"/*.tmp "$TMPDIR_P"/*.rc

# Expected .cpp list (tasks.md T032 step 1), sorted.
EXPECTED=(
    plugins/profundum/src/controller/controller.cpp
    plugins/profundum/src/processor/processor.cpp
    plugins/profundum/tests/integration/band_level_metric_test.cpp
    plugins/profundum/tests/integration/copy_compare_test.cpp
    plugins/profundum/tests/integration/determinism_test.cpp
    plugins/profundum/tests/integration/mono_sum_params_test.cpp
    plugins/profundum/tests/integration/param_flow_test.cpp
    plugins/profundum/tests/integration/param_path_audibility_test.cpp
    plugins/profundum/tests/integration/param_wiring_test.cpp
    plugins/profundum/tests/integration/processor_cpu_test.cpp
    plugins/profundum/tests/integration/state_restore_render_test.cpp
    plugins/profundum/tests/integration/tail_test.cpp
    plugins/profundum/tests/unit/lifecycle_test.cpp
    plugins/profundum/tests/unit/midi_mapping_test.cpp
    plugins/profundum/tests/unit/param_denorm_test.cpp
    plugins/profundum/tests/unit/param_table_test.cpp
    plugins/profundum/tests/unit/phase11_alloc_test.cpp
    plugins/profundum/tests/unit/state_roundtrip_test.cpp
    plugins/profundum/tests/unit/state_v2_test.cpp
    plugins/shared/tests/test_sustain_latch.cpp
)

changed() { { git diff --name-only "$BASE" 2>/dev/null; git ls-files --others --exclude-standard 2>/dev/null; } | sort -u; }
mapfile -t TUS < <(changed | grep '\.cpp$')
mapfile -t HEADERS < <(changed | grep '\.h$')

for f in "${TUS[@]}" "${HEADERS[@]}"; do
    [[ -f "$f" ]] || { echo "missing file: $f" | tee "$LOG"; echo "PORTABILITY RED (missing input)" >> "$LOG"; exit 2; }
done

LIST_DIFF="$(diff <(printf '%s\n' "${EXPECTED[@]}" | sort) <(printf '%s\n' "${TUS[@]}" | sort))"
EXPECTED_TUS=${#EXPECTED[@]}

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

NAMES=(check-portability lint-apple-globals)
lane check-portability node tools/check-portability.js "${TUS[@]}" &
lane lint-apple-globals node tools/lint-apple-globals.js "${HEADERS[@]}" "${TUS[@]}" &
wait

wsl --shutdown > /dev/null 2>&1

RED=""
{
    echo "=== T032 portability $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "TUs (${#TUS[@]}):"; printf '  %s\n' "${TUS[@]}"
    echo "headers (${#HEADERS[@]}):"; printf '  %s\n' "${HEADERS[@]}"
    for n in "${NAMES[@]}"; do
        echo
        echo "== $n =="
        cat "$TMPDIR_P/$n.tmp" 2>/dev/null || echo "(no output file)"
    done
    echo
    echo "== list cross-check (expected vs computed) =="
    if [[ -z "$LIST_DIFF" ]]; then echo "identical ($EXPECTED_TUS files)"; else echo "$LIST_DIFF"; fi
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
if ! grep -qx "check-portability: all clear -- ${EXPECTED_TUS} compiled\." "$TMPDIR_P/check-portability.tmp"; then
    RED="$RED check-portability=not-all-clear-${EXPECTED_TUS}"
fi
[[ -z "$LIST_DIFF" ]] || RED="$RED cpp-list-mismatch"

if [[ -z "$RED" ]]; then
    echo "PORTABILITY GREEN" >> "$LOG"
else
    echo "PORTABILITY RED ($RED ; 124 = ${CEILING_S} s ceiling hit)" >> "$LOG"
fi
rm -rf "$TMPDIR_P"

cat "$LOG"
[[ -z "$RED" ]]
