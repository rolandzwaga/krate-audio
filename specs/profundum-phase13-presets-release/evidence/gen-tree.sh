#!/usr/bin/env bash
# ==============================================================================
# gen-tree.sh -- Profundum Phase 13 T026: generate the factory tree (FR-002, FR-024, FR-025)
# ==============================================================================
# Run by the MAIN LOOP (Git Bash, from anywhere):
#   bash specs/profundum-phase13-presets-release/evidence/gen-tree.sh
# 1. Builds generate_profundum_presets (-- /nr:false): this builds profundum_preset_generator and runs it
#    into plugins/profundum/resources/presets. The build must exit 0 with 0 warnings.
# 2. find plugins/profundum/resources/presets -type f | sort must list exactly 53 *.vstpreset files and
#    no other file (no .gitkeep, no stray output).
# 3. Last line: "TREE GREEN n=53" or "TREE RED <reason>".
# Quote the output into evidence/run-log/T026.txt.
# ==============================================================================
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT" || exit 2

CMAKE="/c/Program Files/CMake/bin/cmake.exe"
TREE="plugins/profundum/resources/presets"
EXPECTED=58
BUILD_LOG="$(mktemp)"
trap 'rm -f "$BUILD_LOG"' EXIT

red() {
    echo "TREE RED $*"
    exit 1
}

# ---- 1. build + generate --------------------------------------------------------
echo "[tree] building generate_profundum_presets"
b0=$(date +%s%N)
"$CMAKE" --build build/windows-x64-release --config Release --target generate_profundum_presets -- /nr:false > "$BUILD_LOG" 2>&1
brc=$?
b1=$(date +%s%N)
warnings=$(grep -cE "warning C[0-9]|warning:" "$BUILD_LOG" || true)
warnings=${warnings:-0}
build_s=$(awk -v a="$b0" -v b="$b1" 'BEGIN { printf "%.1f", (b - a) / 1e9 }')
echo "[tree] build exit=$brc warnings=$warnings wall=${build_s}s"
grep -iE "wrote|written|preset" "$BUILD_LOG" | tr -d '\r' | tail -5 | sed 's/^/[tree]   /'
[[ $brc -eq 0 ]] || red "build exit=$brc: $(grep -m1 -iE "error|failed" "$BUILD_LOG" | tr -d '\r')"
[[ "$warnings" -eq 0 ]] || red "build warnings=$warnings: $(grep -m1 -E "warning C[0-9]|warning:" "$BUILD_LOG" | tr -d '\r')"

# ---- 2. tree check ----------------------------------------------------------------
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
printf '%s\n' "$files" | sed 's/^/[tree]   /'

[[ -z "$others" ]] || red "non-preset files present: $(printf '%s' "$others" | tr '\n' ' ')"
[[ "$n_preset" -eq "$EXPECTED" ]] || red "n=$n_preset (expected $EXPECTED)"
echo "TREE GREEN n=$n_preset"
