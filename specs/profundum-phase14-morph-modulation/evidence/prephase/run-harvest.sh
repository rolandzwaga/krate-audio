#!/usr/bin/env bash
# ==============================================================================
# run-harvest.sh -- Profundum Phase 14 T005 pre-phase harvest (FR-062, FR-063; SC-008 reference; plan 5.8)
# ==============================================================================
# Run by the main loop at the pre-phase tree (before any engine, generator, plugin or preset edit).
# 1. Builds profundum_tests once (-- /nr:false), log -> evidence/run-log/T005/build.log; must show 0 warnings.
# 2. Launches the four hidden cases Profundum_MorphModPrePhaseHarvest_<k>of4 as four CONCURRENT processes,
#    stdout+stderr -> evidence/run-log/T005/<k>.log, wall clock appended as "WALL <k> <s> s exit=<rc>".
#    Shard k writes evidence/prephase/fingerprints-<k>.csv and decoded-<k>.csv; shard 0 also writes
#    evidence/prephase-claims.csv.
# 3. node evidence/prephase/merge.js -> evidence/prephase-fingerprints.csv, evidence/prephase-decoded.csv.
# 4. Checks prephase-claims.csv names as many presets as the merge.
# 5. Last line: "HARVEST GREEN <merge summary>" when all four exit 0 and the merge passes, else "HARVEST RED <why>".
# ==============================================================================
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$ROOT" || exit 2

EV="specs/profundum-phase14-morph-modulation/evidence"
PRE="$EV/prephase"
LOGS="$EV/run-log/T005"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
EXE="build/windows-x64-release/bin/Release/profundum_tests.exe"
SHARDS=(0 1 2 3)

red() {
    echo "HARVEST RED $*"
    exit 1
}

mkdir -p "$LOGS" || red "cannot create $LOGS"
rm -f "$LOGS"/*.log "$PRE"/fingerprints-*.csv "$PRE"/decoded-*.csv \
      "$EV/prephase-fingerprints.csv" "$EV/prephase-decoded.csv" "$EV/prephase-claims.csv"

# ---- 1. build -------------------------------------------------------------------
echo "[harvest] building profundum_tests -> $LOGS/build.log"
b0=$(date +%s%N)
"$CMAKE" --build build/windows-x64-release --config Release --target profundum_tests -- /nr:false > "$LOGS/build.log" 2>&1
brc=$?
b1=$(date +%s%N)
warnings=$(grep -cE "warning C[0-9]|warning:" "$LOGS/build.log" || true)
warnings=${warnings:-0}
build_s=$(awk -v a="$b0" -v b="$b1" 'BEGIN { printf "%.1f", (b - a) / 1e9 }')
echo "[harvest] build exit=$brc warnings=$warnings wall=${build_s}s"
[[ $brc -eq 0 ]] || red "build exit=$brc (see $LOGS/build.log)"
[[ "$warnings" -eq 0 ]] || red "build warnings=$warnings (see $LOGS/build.log): $(grep -m1 -E "warning C[0-9]|warning:" "$LOGS/build.log")"
[[ -x "$EXE" ]] || red "missing $EXE"

# ---- 2. the four hidden shards, concurrently --------------------------------------------
t0=$(date +%s%N)
pids=()
for k in "${SHARDS[@]}"; do
    (
        log="$LOGS/$k.log"
        s0=$(date +%s%N)
        "$EXE" "Profundum_MorphModPrePhaseHarvest_${k}of4" > "$log" 2>&1
        rc=$?
        s1=$(date +%s%N)
        secs=$(awk -v a="$s0" -v b="$s1" 'BEGIN { printf "%.1f", (b - a) / 1e9 }')
        echo "WALL $k $secs s exit=$rc" >> "$log"
        echo "WALL $k $secs s exit=$rc"
        exit $rc
    ) &
    pids+=($!)
done

failed=()
for i in "${!pids[@]}"; do
    wait "${pids[$i]}" || failed+=("${SHARDS[$i]}")
done
t1=$(date +%s%N)
total_s=$(awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.1f", (b - a) / 1e9 }')
echo "[harvest] shards done wall=${total_s}s failed=${#failed[@]}"

if [[ ${#failed[@]} -gt 0 ]]; then
    k="${failed[0]}"
    line="$(grep -m1 -E "FAILED|failed|error" "$LOGS/$k.log" || true)"
    [[ -n "$line" ]] || line="$(tail -n 2 "$LOGS/$k.log" | head -n 1)"
    red "failed shards=${failed[*]}; first: shard $k: $line"
fi

# ---- 3. merge -------------------------------------------------------------------------------
merge_out="$(node "$PRE/merge.js" 2>&1)"
mrc=$?
echo "$merge_out"
[[ $mrc -eq 0 ]] || red "merge exit=$mrc: $(echo "$merge_out" | tail -n 1)"
summary="$(echo "$merge_out" | grep -m1 "^PREPHASE MERGED")"
[[ -n "$summary" ]] || red "merge printed no PREPHASE MERGED line"

# ---- 4. the claims file names every merged preset ----------------------------------------------
[[ -f "$EV/prephase-claims.csv" ]] || red "missing $EV/prephase-claims.csv"
merged=$(echo "$summary" | sed -E 's/.*presets=([0-9]+).*/\1/')
claims=$(node -e '
const t = require("fs").readFileSync(process.argv[1], "utf8").replace(/\r/g, "");
let q = false, n = 0, cur = 0;
for (const c of t) { if (c === "\"") q = !q; else if (c === "\n" && !q) { if (cur) n++; cur = 0; continue; } cur++; }
if (cur) n++;
console.log(n - 1);' "$EV/prephase-claims.csv")
[[ "$claims" -eq "$merged" ]] || red "prephase-claims.csv has $claims rows, the merge has $merged presets"

echo "HARVEST GREEN $summary claims=$claims"
