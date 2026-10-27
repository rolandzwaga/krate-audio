#!/usr/bin/env bash
# ==============================================================================
# run-probe.sh -- Profundum Phase 14 T022 Morph Mod probe (FR-044, FR-046; SC-017 (a) input; plan 5.7 steps 1-4)
# ==============================================================================
# Run by the MAIN LOOP (Git Bash, from anywhere):
#   bash specs/profundum-phase14-morph-modulation/evidence/probe/run-probe.sh
# 1. ONE build of profundum_tests (-- /nr:false); exit 0 and 0 warnings. Log -> evidence/run-log/T022/build.log.
# 2. The 32 hidden cases Profundum_MorphModProbe_<k>of32 as 32 CONCURRENT processes (one per core; each is a single
#    named case, run directly), stdout+stderr -> evidence/run-log/T022/<k>.log, wall clock appended as
#    "WALL <k> <s> s exit=<rc>". Shard k writes evidence/probe/shard-<k>.csv.
# 3. node evidence/probe/merge.js  -> evidence/probe-morphmod.csv (run id = git short hash + UTC stamp).
# 4. node evidence/probe/select.js -> evidence/conversions.md under the ruled rule (R4 = rule (ii)); prints
#    "SELECTION <n> conversions, <m> new". Exit 3 = a column with no passing candidate in any preset (FR-023 stop).
# Last line: "PROBE GREEN <SELECTION line>" or "PROBE RED <reason>".
# Quote the per-shard WALL lines, the SELECTION line and any NEW PRESET NEEDED row into evidence/run-log/T022.txt.
# ==============================================================================
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$ROOT" || exit 2

EV="specs/profundum-phase14-morph-modulation/evidence"
PROBE="$EV/probe"
LOGS="$EV/run-log/T022"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
EXE="build/windows-x64-release/bin/Release/profundum_tests.exe"
NSHARDS=32

red() {
    echo "PROBE RED $*"
    exit 1
}

secs_since() {
    awk -v a="$1" -v b="$(date +%s%N)" 'BEGIN { printf "%.1f", (b - a) / 1e9 }'
}

mkdir -p "$LOGS" "$PROBE" || red "cannot create $LOGS / $PROBE"
rm -f "$LOGS"/*.log "$PROBE"/shard-*.csv "$EV/probe-morphmod.csv" "$EV/conversions.md"

# ---- 1. one build ------------------------------------------------------------------------------
echo "[probe] building profundum_tests -> $LOGS/build.log"
b0=$(date +%s%N)
"$CMAKE" --build build/windows-x64-release --config Release --target profundum_tests -- /nr:false > "$LOGS/build.log" 2>&1
brc=$?
warnings=$(grep -cE "warning C[0-9]|warning:" "$LOGS/build.log" || true)
warnings=${warnings:-0}
echo "[probe] build exit=$brc warnings=$warnings wall=$(secs_since "$b0")s"
[[ $brc -eq 0 ]] || red "build exit=$brc (see $LOGS/build.log)"
[[ "$warnings" -eq 0 ]] || red "build warnings=$warnings (see $LOGS/build.log): $(grep -m1 -E "warning C[0-9]|warning:" "$LOGS/build.log")"
[[ -x "$EXE" ]] || red "missing $EXE"

# ---- 2. the 32 hidden shards, concurrently -------------------------------------------------------
echo "[probe] cores=$(nproc 2>/dev/null || echo '?') shards=$NSHARDS"
t0=$(date +%s%N)
pids=()
for ((k = 0; k < NSHARDS; ++k)); do
    (
        log="$LOGS/$k.log"
        s0=$(date +%s%N)
        "$EXE" "Profundum_MorphModProbe_${k}of${NSHARDS}" > "$log" 2>&1
        rc=$?
        secs=$(secs_since "$s0")
        echo "WALL $k $secs s exit=$rc" >> "$log"
        echo "WALL $k $secs s exit=$rc"
        exit $rc
    ) &
    pids+=($!)
done

failed=()
for i in "${!pids[@]}"; do
    wait "${pids[$i]}" || failed+=("$i")
done
echo "[probe] shards done wall=$(secs_since "$t0")s failed=${#failed[@]}"

if [[ ${#failed[@]} -gt 0 ]]; then
    k="${failed[0]}"
    line="$(grep -m1 -E "FAILED|failed|error|No test cases matched" "$LOGS/$k.log" || true)"
    [[ -n "$line" ]] || line="$(tail -n 2 "$LOGS/$k.log" | head -n 1)"
    red "failed shards=${failed[*]}; first: shard $k: $line"
fi
# Non-vacuity: every shard ran its case and printed its summary line.
for ((k = 0; k < NSHARDS; ++k)); do
    grep -q "^MORPHMOD PROBE SHARD $k " "$LOGS/$k.log" || red "shard $k printed no MORPHMOD PROBE SHARD line (see $LOGS/$k.log)"
done

# ---- 3. merge ------------------------------------------------------------------------------------
merge_out="$(node "$PROBE/merge.js" 2>&1)"
mrc=$?
echo "$merge_out"
[[ $mrc -eq 0 ]] || red "merge exit=$mrc: $(echo "$merge_out" | tail -n 1)"
echo "$merge_out" | grep -q "^PROBE MERGED" || red "merge printed no PROBE MERGED line"

# ---- 4. select -----------------------------------------------------------------------------------
select_out="$(node "$PROBE/select.js" 2>&1)"
src=$?
echo "$select_out"
selection="$(echo "$select_out" | grep -m1 "^SELECTION ")"
if [[ $src -eq 3 ]]; then
    red "FR-023 stop: $(echo "$select_out" | grep -m1 "^NO PASSING CANDIDATE") ($selection)"
fi
[[ $src -eq 0 ]] || red "select exit=$src: $(echo "$select_out" | tail -n 1)"
[[ -n "$selection" ]] || red "select printed no SELECTION line"
[[ -f "$EV/conversions.md" ]] || red "missing $EV/conversions.md"

echo "PROBE GREEN $selection"
