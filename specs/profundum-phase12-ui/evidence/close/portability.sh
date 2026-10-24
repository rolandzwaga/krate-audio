#!/usr/bin/env bash
# Profundum Phase 12, task T037 close lane: portability (SC-021).
# Structure copied from specs/profundum-phase11-parameters/evidence/close/portability.sh.
#
# Run (Git Bash, from anywhere), concurrently with suites.sh / tidy.sh / asan.sh (T036):
#   bash specs/profundum-phase12-ui/evidence/close/portability.sh
# Lanes (concurrently, each with a 300 s ceiling):
#   1. node tools/check-portability.js <the changed .cpp files>  (g++ -fsyntax-only via WSL). The list is
#      git diff --name-only 40f6ebca  UNION  untracked (git ls-files --others --exclude-standard), filtered to \.cpp$;
#      it must equal the EXPECTED list below (tasks.md T037 step 1; four src/ui/*.cpp, not three, because the
#      P-5 ruling of 2026-10-23 added offset_ring_knob.cpp). Required line:
#      "check-portability: all clear -- N compiled." with no ", M skipped" suffix, N = list length. WSL must see
#      the repo drive (/mnt/f); a sandboxed run fails with "wsl: Failed to translate 'F:\projects\iterum'" -> run
#      unsandboxed.
#   2. node tools/lint-apple-globals.js <changed/new .h files> <the .cpp list>
#      (usage line, tools/lint-apple-globals.js:20: node tools/lint-apple-globals.js [dir-or-file ...]).
#   3. wsl --shutdown (check-portability boots WSL; WSLg traces otherwise keep writing to C:).
# Output: evidence/run-log/close/portability.log (each lane's output, exit code and wall clock; last line
# "PORTABILITY GREEN|RED"). Quote the log into evidence/run-log/T037.txt.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

BASE=40f6ebca
SPEC="specs/profundum-phase12-ui"
LOGDIR="$SPEC/evidence/run-log/close"
LOG="$LOGDIR/portability.log"
TMPDIR_P="$LOGDIR/portability-tmp"
CEILING_S=300
mkdir -p "$LOGDIR" "$TMPDIR_P"
rm -f "$TMPDIR_P"/*.tmp "$TMPDIR_P"/*.rc

# Expected .cpp list (tasks.md T037 step 1), sorted.
EXPECTED=(
    dsp/tests/unit/systems/profundum_engine_display_test.cpp
    plugins/profundum/src/controller/controller.cpp
    plugins/profundum/src/entry.cpp
    plugins/profundum/src/processor/goertzel_bank_simd.cpp
    plugins/profundum/src/processor/processor.cpp
    plugins/profundum/src/ui/harmonic_bar_view.cpp
    plugins/profundum/src/ui/offset_ring_knob.cpp
    plugins/profundum/src/ui/panel_sub_controller.cpp
    plugins/profundum/src/ui/spread_curve_view.cpp
    plugins/profundum/tests/integration/determinism_test.cpp
    plugins/profundum/tests/integration/main_page_audibility_test.cpp
    plugins/profundum/tests/integration/main_page_render_set_test.cpp
    plugins/profundum/tests/integration/main_page_sanity_test.cpp
    plugins/profundum/tests/integration/processor_cpu_test.cpp
    plugins/profundum/tests/integration/spectrum_frame_test.cpp
    plugins/profundum/tests/integration/state_restore_render_test.cpp
    plugins/profundum/tests/unit/controller/custom_view_laws_test.cpp
    plugins/profundum/tests/unit/controller/editor_layout_test.cpp
    plugins/profundum/tests/unit/controller/editor_lifecycle_test.cpp
    plugins/profundum/tests/unit/controller/editor_sync_test.cpp
    plugins/profundum/tests/unit/controller/macro_ui_test.cpp
    plugins/profundum/tests/unit/controller/morph_component_test.cpp
    plugins/profundum/tests/unit/controller/spread_curve_test.cpp
    plugins/profundum/tests/unit/controller/ui_views_test.cpp
    plugins/profundum/tests/unit/lifecycle_test.cpp
    plugins/profundum/tests/unit/phase11_alloc_test.cpp
    plugins/profundum/tests/unit/spectrum_producer_test.cpp
    plugins/profundum/tests/unit/state_roundtrip_test.cpp
    plugins/profundum/tests/unit/state_v2_test.cpp
    plugins/profundum/tests/unit/state_v3_test.cpp
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
    echo "=== T037 portability $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown) base $BASE"
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
