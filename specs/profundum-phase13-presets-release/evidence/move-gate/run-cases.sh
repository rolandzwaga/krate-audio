#!/usr/bin/env bash
# ==============================================================================
# run-cases.sh <pre|post> -- Profundum Phase 13 FR-058 / SC-029 move gate run (plan §5.1 steps 1, 3, 4)
# ==============================================================================
# 1. Builds dsp_systems_tests once (-- /nr:false), log -> move-gate/<mode>/build.log; the count of
#    "warning C[0-9]|warning:" lines -> move-gate/<mode>/warnings.txt (post must be 0, compare.js checks).
# 2. Lists the 16 TEST_CASEs of dsp/tests/unit/systems/profundum_engine_audibility_test.cpp
#    (Catch2 v3: --list-tests --verbosity quiet "[voice-engine]", filtered to VoiceEngine_AudibilityTable*,
#    VoiceEngine_MacroMonotone*, VoiceEngine_MotionMassSwell*) and runs them as 16 CONCURRENT processes,
#    one per exact case name, stdout+stderr -> move-gate/<mode>/<case>.log, wall clock appended as
#    "WALL <case> <seconds> s exit=<rc>".
# 3. After all exit: every file the run wrote under specs/profundum-phase9-voice-engine/evidence/
#    (git status --porcelain -uall, unioned with files newer than the run's start marker, so a file
#    rewritten with identical bytes is still captured) is copied to move-gate/<mode>/evidence/<same
#    relative path>; then `git checkout -- specs/profundum-phase9-voice-engine/evidence/` restores the
#    committed Phase 9 evidence (and any untracked file this run created there is removed).
# 4. Prints "RUN <mode> DONE cases=16 wall=<s>" as its last line.
# The full dsp_systems_tests suite and its other [long] cases are never run (FR-058).
# ==============================================================================
set -u

mode="${1:-}"
if [[ "$mode" != "pre" && "$mode" != "post" ]]; then
    echo "usage: run-cases.sh <pre|post>" >&2
    exit 2
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$ROOT" || exit 2

GATE="specs/profundum-phase13-presets-release/evidence/move-gate"
OUT="$GATE/$mode"
P9="specs/profundum-phase9-voice-engine/evidence"
TU="dsp/tests/unit/systems/profundum_engine_audibility_test.cpp"
HDR="dsp/tests/unit/systems/profundum_audibility_readers.h"
BASE="e6095b24"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
EXE="build/windows-x64-release/bin/Release/dsp_systems_tests.exe"
EXPECTED_CASES=16

fail() {
    echo "RUN $mode FAILED $*"
    exit 1
}

# ---- guards -------------------------------------------------------------------
# The restore step runs `git checkout` over the Phase 9 evidence: refuse to start if it holds changes
# this run did not make, so nothing of anyone else's is discarded.
dirty="$(git status --porcelain -uall -- "$P9")"
[[ -z "$dirty" ]] || fail "Phase 9 evidence is not clean before the run (restore it first): $(echo "$dirty" | head -3 | tr '\n' ' ')"

if [[ "$mode" == "pre" ]]; then
    # The baseline must be the pre-move binary: the Phase 9 TU unedited since $BASE and no readers header.
    git diff --quiet "$BASE" -- "$TU" || fail "pre: $TU differs from $BASE (the baseline must run before the move)"
    [[ ! -e "$HDR" ]] || fail "pre: $HDR exists (the baseline must run before the move)"
else
    [[ -e "$HDR" ]] || fail "post: $HDR does not exist (run the move, T003, first)"
fi

rm -rf "$OUT"
mkdir -p "$OUT/evidence" || fail "cannot create $OUT"

# ---- 1. build -----------------------------------------------------------------
echo "[$mode] building dsp_systems_tests -> $OUT/build.log"
b0=$(date +%s%N)
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests -- /nr:false > "$OUT/build.log" 2>&1
brc=$?
b1=$(date +%s%N)
warnings=$(grep -cE "warning C[0-9]|warning:" "$OUT/build.log" || true)
echo "${warnings:-0}" > "$OUT/warnings.txt"
build_s=$(awk -v a="$b0" -v b="$b1" 'BEGIN { printf "%.1f", (b - a) / 1e9 }')
echo "[$mode] build exit=$brc warnings=${warnings:-0} wall=${build_s}s"
[[ $brc -eq 0 ]] || fail "build exit=$brc (see $OUT/build.log)"
[[ -x "$EXE" ]] || fail "missing $EXE"

# ---- 2. the 16 cases, concurrently ----------------------------------------------
mapfile -t cases < <("$EXE" --list-tests --verbosity quiet "[voice-engine]" 2>/dev/null | tr -d '\r' |
    grep -E '^VoiceEngine_(AudibilityTable|MacroMonotone|MotionMassSwell)' | sort -u)
printf '%s\n' "${cases[@]}" > "$OUT/cases.txt"
[[ ${#cases[@]} -eq $EXPECTED_CASES ]] || fail "expected $EXPECTED_CASES case names, listed ${#cases[@]} (see $OUT/cases.txt)"

touch "$OUT/.start"
t0=$(date +%s%N)
echo "[$mode] launching ${#cases[@]} concurrent cases"
for c in "${cases[@]}"; do
    (
        s=$(date +%s%N)
        "$EXE" "$c" > "$OUT/$c.log" 2>&1
        rc=$?
        e=$(date +%s%N)
        w=$(awk -v a="$s" -v b="$e" 'BEGIN { printf "%.1f", (b - a) / 1e9 }')
        echo "WALL $c $w s exit=$rc" >> "$OUT/$c.log"
    ) &
done
wait
t1=$(date +%s%N)
wall=$(awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.1f", (b - a) / 1e9 }')

# ---- 3. copy the written Phase 9 evidence, then restore it ------------------------
git status --porcelain -uall -- "$P9" | tr -d '\r' > "$OUT/git-status.txt"
{
    sed -E 's/^.{3}//' "$OUT/git-status.txt"
    find "$P9" -type f -newer "$OUT/.start"
} | sed -E 's#^"(.*)"$#\1#' | sort -u > "$OUT/written.txt"

copied=0
while IFS= read -r f; do
    [[ -n "$f" && -f "$f" ]] || continue
    rel="${f#"$P9"/}"
    mkdir -p "$(dirname "$OUT/evidence/$rel")"
    cp "$f" "$OUT/evidence/$rel" && copied=$((copied + 1))
done < "$OUT/written.txt"

git checkout -- "$P9/"
# Untracked files this run created under the Phase 9 evidence (none expected: all 17 outputs are tracked).
while IFS= read -r f; do
    [[ -n "$f" ]] || continue
    if grep -qxF "$f" "$OUT/written.txt"; then rm -f "$f"; fi
done < <(git ls-files --others --exclude-standard -- "$P9" | tr -d '\r')
left="$(git status --porcelain -uall -- "$P9")"
[[ -z "$left" ]] || echo "[$mode] WARNING: Phase 9 evidence still not clean after restore: $left"
rm -f "$OUT/.start"

# ---- 4. summary ----------------------------------------------------------------------
echo "[$mode] evidence files copied: $copied (listed in $OUT/written.txt)"
failed=0
for c in "${cases[@]}"; do
    tail -n 1 "$OUT/$c.log"
    grep -q "exit=0$" <(tail -n 1 "$OUT/$c.log") || failed=$((failed + 1))
done
echo "[$mode] build wall=${build_s}s warnings=${warnings:-0}; cases with non-zero exit: $failed"
echo "RUN $mode DONE cases=${#cases[@]} wall=$wall"
