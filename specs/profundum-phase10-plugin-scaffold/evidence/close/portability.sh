#!/usr/bin/env bash
# Profundum Phase 10, task T030 close lane: portability (cross-cutting; SC-001).
# Structure copied from specs/profundum-phase9-voice-engine/evidence/close/portability.sh.
#
# Run from the main loop (Git Bash, from anywhere), concurrently with suites.sh / tidy.sh / asan.sh (T029):
#   bash specs/profundum-phase10-plugin-scaffold/evidence/close/portability.sh
# Lanes (concurrently, each with a 300 s ceiling):
#   1. node tools/check-portability.js <the fourteen Phase 10 .cpp files>   (g++ -fsyntax-only via WSL). Required line:
#      "check-portability: all clear -- 14 compiled." with no ", M skipped" suffix. It needs WSL to see the repo drive
#      (/mnt/f); a sandboxed run fails with "wsl: Failed to translate 'F:\projects\iterum'", so then run it unsandboxed.
#      Cross-check: git status --porcelain --untracked-files=all plugins/profundum | grep -c '\.cpp$' prints 14.
#   2. node tools/lint-apple-globals.js <plugins/profundum/src headers> <fourteen TUs>   (tasks.md deviation 6;
#      usage line: node tools/lint-apple-globals.js [dir-or-file ...]).
#   then wsl --shutdown (check-portability boots WSL; WSLg traces otherwise keep writing to C:).
# Output: evidence/run-log/close/portability.log (each lane's output, exit code and wall clock; last line
# "PORTABILITY GREEN|RED"). Quote the log into evidence/run-log/T030.txt.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase10-plugin-scaffold"
LOGDIR="$SPEC/evidence/run-log/close"
LOG="$LOGDIR/portability.log"
TMPDIR_P="$LOGDIR/portability-tmp"
CEILING_S=300
EXPECTED_TUS=14
mkdir -p "$LOGDIR" "$TMPDIR_P"
rm -f "$TMPDIR_P"/*.tmp "$TMPDIR_P"/*.rc

# The fourteen Phase 10 .cpp files (plan §5.5 list).
TUS=(
    plugins/profundum/src/entry.cpp
    plugins/profundum/src/processor/processor.cpp
    plugins/profundum/src/controller/controller.cpp
    plugins/profundum/tests/vstgui_test_stubs.cpp
    plugins/profundum/tests/unit/test_main.cpp
    plugins/profundum/tests/unit/lifecycle_test.cpp
    plugins/profundum/tests/unit/midi_event_test.cpp
    plugins/profundum/tests/unit/param_denorm_test.cpp
    plugins/profundum/tests/unit/processor_bus_test.cpp
    plugins/profundum/tests/unit/state_roundtrip_test.cpp
    plugins/profundum/tests/unit/controller/editor_lifecycle_test.cpp
    plugins/profundum/tests/integration/param_flow_test.cpp
    plugins/profundum/tests/integration/processor_audio_test.cpp
    plugins/profundum/tests/integration/processor_cpu_test.cpp
)
# The plugin's src/ headers (the generated version.h included when present).
mapfile -t HEADERS < <(find plugins/profundum/src -name '*.h' | sort)

for f in "${TUS[@]}"; do
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

NAMES=(check-portability lint-apple-globals)
lane check-portability node tools/check-portability.js "${TUS[@]}" &
lane lint-apple-globals node tools/lint-apple-globals.js "${HEADERS[@]}" "${TUS[@]}" &
wait

wsl --shutdown > /dev/null 2>&1

CPP_COUNT="$(git status --porcelain --untracked-files=all plugins/profundum | grep -c '\.cpp$')"

RED=""
{
    echo "=== T030 portability $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    for n in "${NAMES[@]}"; do
        echo
        echo "== $n =="
        cat "$TMPDIR_P/$n.tmp" 2>/dev/null || echo "(no output file)"
    done
    echo
    echo "== cross-check =="
    echo "git status .cpp count under plugins/profundum: $CPP_COUNT (expected $EXPECTED_TUS)"
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
# The exact required line: all fourteen compiled, no skipped suffix.
if ! grep -qx "check-portability: all clear -- ${EXPECTED_TUS} compiled\." "$TMPDIR_P/check-portability.tmp"; then
    RED="$RED check-portability=not-all-clear-${EXPECTED_TUS}"
fi
[[ "$CPP_COUNT" == "$EXPECTED_TUS" ]] || RED="$RED cpp-count=$CPP_COUNT"

if [[ -z "$RED" ]]; then
    echo "PORTABILITY GREEN" >> "$LOG"
else
    echo "PORTABILITY RED ($RED ; 124 = ${CEILING_S} s ceiling hit)" >> "$LOG"
fi
rm -rf "$TMPDIR_P"

cat "$LOG"
[[ -z "$RED" ]]
