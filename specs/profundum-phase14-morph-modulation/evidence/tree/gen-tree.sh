#!/usr/bin/env bash
# ==============================================================================
# gen-tree.sh -- Profundum Phase 14 T015: regenerate the factory tree at 610 bytes (FR-061;
#                Phase 13 Profundum_CommittedPresetTreeMatches, Profundum_FactoryPresetStateFormat)
# ==============================================================================
# Run by the MAIN LOOP (Git Bash, from anywhere):
#   bash specs/profundum-phase14-morph-modulation/evidence/tree/gen-tree.sh
# Profundum only: builds and runs nothing but profundum_preset_generator and profundum_tests.
# 1. ONE build of profundum_preset_generator + profundum_tests (-- /nr:false); exit 0, 0 warnings.
#    Log -> evidence/run-log/T015/build.log.
# 2. The generate_profundum_presets target (root CMakeLists.txt): runs the generator into
#    plugins/profundum/resources/presets (its dependency is already built, so this is the run only).
# 3. Tree check: exactly EXPECTED *.vstpreset files and no other file.
# 4. "[profundum][preset]~[long]~[.]" over profundum_tests, sharded across the machine by tools/run-gate.js
#    (the runner also requires the timing-test exclusions; none of these cases carries those tags).
#    Non-vacuity: Profundum_FactoryPresetStateFormat (every Comp v3, 610 bytes) and
#    Profundum_CommittedPresetTreeMatches must be among the listed cases.
# 5. git status --short -- plugins/profundum/resources/presets and the file count.
# Last line: "TREE GREEN files=<n>" or "TREE RED <reason>".
# Quote the last line (and the wall clocks) into evidence/run-log/T015.txt.
# ==============================================================================
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$ROOT" || exit 2

EV="specs/profundum-phase14-morph-modulation/evidence"
LOGS="$EV/run-log/T015"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
BIN="build/windows-x64-release/bin/Release"
EXE="$BIN/profundum_tests.exe"
TREE="plugins/profundum/resources/presets"
EXPECTED=60   # T025: 58 + the two rule (ii) new presets (profundum_preset_defs.h definitions 59-60)
FILTER='[profundum][preset]~[long]~[.]~[performance]~[perf]~[benchmark]~[!benchmark]'

red() {
    echo "TREE RED $*"
    exit 1
}

secs_since() {
    awk -v a="$1" -v b="$(date +%s%N)" 'BEGIN { printf "%.1f", (b - a) / 1e9 }'
}

count_warnings() {
    local w
    w=$(grep -cE "warning C[0-9]|warning:" "$1" || true)
    echo "${w:-0}"
}

mkdir -p "$LOGS" || red "cannot create $LOGS"
rm -f "$LOGS"/*.log

# ---- 1. one build of both targets -------------------------------------------------
echo "[tree] building profundum_preset_generator profundum_tests -> $LOGS/build.log"
b0=$(date +%s%N)
"$CMAKE" --build build/windows-x64-release --config Release \
    --target profundum_preset_generator profundum_tests -- /nr:false > "$LOGS/build.log" 2>&1
brc=$?
warnings=$(count_warnings "$LOGS/build.log")
echo "[tree] build exit=$brc warnings=$warnings wall=$(secs_since "$b0")s"
[[ $brc -eq 0 ]] || red "build exit=$brc: $(grep -m1 -iE "error|failed" "$LOGS/build.log" | tr -d '\r')"
[[ "$warnings" -eq 0 ]] || red "build warnings=$warnings: $(grep -m1 -E "warning C[0-9]|warning:" "$LOGS/build.log" | tr -d '\r')"
[[ -x "$EXE" ]] || red "missing $EXE"

# ---- 2. generate ------------------------------------------------------------------
echo "[tree] generate_profundum_presets -> $LOGS/generate.log"
g0=$(date +%s%N)
"$CMAKE" --build build/windows-x64-release --config Release \
    --target generate_profundum_presets -- /nr:false > "$LOGS/generate.log" 2>&1
grc=$?
warnings=$(count_warnings "$LOGS/generate.log")
echo "[tree] generate exit=$grc warnings=$warnings wall=$(secs_since "$g0")s"
grep -iE "wrote|written|preset" "$LOGS/generate.log" | tr -d '\r' | tail -5 | sed 's/^/[tree]   /'
[[ $grc -eq 0 ]] || red "generate exit=$grc: $(grep -m1 -iE "error|failed" "$LOGS/generate.log" | tr -d '\r')"
[[ "$warnings" -eq 0 ]] || red "generate warnings=$warnings"

# ---- 3. tree check ----------------------------------------------------------------
[[ -d "$TREE" ]] || red "missing $TREE"
files="$(find "$TREE" -type f | sort)"
n_all=$(printf '%s\n' "$files" | grep -c . || true)
n_preset=$(printf '%s\n' "$files" | grep -c '\.vstpreset$' || true)
others="$(printf '%s\n' "$files" | grep -v '\.vstpreset$' | grep . || true)"
echo "[tree] files=$n_all vstpreset=$n_preset"
for d in "$TREE"/*/; do
    [[ -d "$d" ]] || continue
    c=$(find "$d" -type f -name '*.vstpreset' | grep -c . || true)
    echo "[tree]   $(basename "$d"): $c"
done
[[ -z "$others" ]] || red "non-preset files present: $(printf '%s' "$others" | tr '\n' ' ')"
[[ "$n_preset" -eq "$EXPECTED" ]] || red "n=$n_preset (expected $EXPECTED)"

# ---- 4. preset cases, sharded -----------------------------------------------------
listed="$("$EXE" --list-tests --verbosity quiet "$FILTER" 2>&1 | tr -d '\r')"
n_cases=$(printf '%s\n' "$listed" | grep -c '^Profundum_' || true)
echo "[tree] preset cases listed=$n_cases"
for must in Profundum_FactoryPresetStateFormat Profundum_CommittedPresetTreeMatches; do
    printf '%s\n' "$listed" | grep -qx "$must" || red "filter does not select $must (vacuous run)"
done
echo "[tree] run-gate -> $LOGS/gate.log"
t0=$(date +%s%N)
node tools/run-gate.js --filter "$FILTER" --out "f:/tmp/gate/profundum-p14-T015" profundum_tests > "$LOGS/gate.log" 2>&1
trc=$?
tr -d '\r' < "$LOGS/gate.log" | sed 's/^/[tree]   /'
echo "[tree] gate exit=$trc wall=$(secs_since "$t0")s"
gate_last="$(tr -d '\r' < "$LOGS/gate.log" | grep -E '^GATE (GREEN|RED)' | tail -1)"
[[ $trc -eq 0 && "$gate_last" == GATE\ GREEN* ]] || red "preset cases: ${gate_last:-no GATE line (exit $trc)}"

# ---- 5. git status ----------------------------------------------------------------
changed="$(git status --short -- "$TREE")"
n_changed=$(printf '%s\n' "$changed" | grep -c . || true)
echo "[tree] git status --short -- $TREE: $n_changed entries"
printf '%s\n' "$changed" | sed 's/^/[tree]   /'

echo "TREE GREEN files=$n_preset"
