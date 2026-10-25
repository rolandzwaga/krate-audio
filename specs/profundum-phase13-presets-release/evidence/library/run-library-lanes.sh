#!/usr/bin/env bash
# ==============================================================================
# run-library-lanes.sh -- Profundum Phase 13 T027: the [long] library lanes
#   (SC-007, SC-016 - SC-021, SC-023, SC-027 wall clock, SC-031; plan 10)
# ==============================================================================
# Run by the MAIN LOOP, DETACHED (Git Bash, from anywhere):
#   bash specs/profundum-phase13-presets-release/evidence/library/run-library-lanes.sh
# Status file: evidence/library/status.txt, one line per lane: "<lane> exit=<rc> wall=<s>s".
# 1. One build of profundum_tests (-- /nr:false), log -> evidence/library/build.log; must show 0 warnings.
# 2. CONCURRENTLY (none is timing-sensitive), each to its own evidence/library/<lane>.log:
#      showcase-0 .. showcase-7  "Profundum_PresetShowcase_<k>of8"   (write evidence/coverage-matrix/shard-<k>.csv)
#      sweep-set                 "Profundum_PresetSweep*" "Profundum_PresetTruePeakMaxMass"
#                                "Profundum_PresetMonoSum" "Profundum_PresetLoudnessMatch"  (one process: shared
#                                render cache)
#      determinism               "Profundum_PresetDeterminism"
#      distinctness              "Profundum_PresetDistinctness"
#      listen                    bash evidence/listen/render.sh (its build step is a no-op after step 1)
# 3. node evidence/merge-csv.js evidence/coverage-matrix evidence/coverage-matrix.csv <claims> --group-min (SC-007:
#    a group column plus one group-min record per C-2.1 group), claims = the sum of
#    the shards' "arms=" summary counts (every gated cell of every def's primary + secondaries).
# 4. git checkout -- specs/profundum-phase9-voice-engine/evidence/ (the lanes rewrite Phase 9 evidence files).
# 5. "Profundum_PresetRenderGuard" alone, AFTER the concurrent lanes (it asserts its own wall clock, SC-027):
#    the per-push case with the full library. Above 15 s (R-11) it is RED with "split needed": the case is split
#    by preset index in preset_rt_test.cpp (_0of4 .. _3of4 since 2026-10-25; never moved to [long], never a relaxed bar).
# Last line: "LIBRARY LANES GREEN" or "LIBRARY LANES RED <lane>: <first FAILED line>".
# Expected cost (plan 10): showcase ~10-15 min wall on 8 shards, sweep set ~10 min.
# ==============================================================================
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$ROOT" || exit 2

EV="specs/profundum-phase13-presets-release/evidence"
OUT="$EV/library"
STATUS="$OUT/status.txt"
COV_DIR="$EV/coverage-matrix"
COV_CSV="$EV/coverage-matrix.csv"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
EXE="build/windows-x64-release/bin/Release/profundum_tests.exe"
GUARD_BAR_S=15

mkdir -p "$OUT" || { echo "LIBRARY LANES RED setup: cannot create $OUT"; exit 1; }
rm -f "$OUT"/*.log "$STATUS"
commit="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
echo "# run-library-lanes $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $commit" > "$STATUS"

red() {
    echo "LIBRARY LANES RED $*"
    echo "verdict RED $*" >> "$STATUS"
    exit 1
}

secs_since() {
    awk -v a="$1" -v b="$(date +%s%N)" 'BEGIN { printf "%.1f", (b - a) / 1e9 }'
}

first_failed() {
    grep -m1 -E "FAILED|FR-023 STOP|LISTEN RED|error" "$1" | tr -d '\r'
}

# ---- 1. build -------------------------------------------------------------------
echo "[library] building profundum_tests -> $OUT/build.log"
b0=$(date +%s%N)
"$CMAKE" --build build/windows-x64-release --config Release --target profundum_tests -- /nr:false > "$OUT/build.log" 2>&1
brc=$?
build_s=$(secs_since "$b0")
warnings=$(grep -cE "warning C[0-9]|warning:" "$OUT/build.log" || true)
warnings=${warnings:-0}
echo "build exit=$brc wall=${build_s}s warnings=$warnings" >> "$STATUS"
echo "[library] build exit=$brc warnings=$warnings wall=${build_s}s"
[[ $brc -eq 0 ]] || red "build: exit=$brc: $(grep -m1 -iE "error" "$OUT/build.log" | tr -d '\r')"
[[ "$warnings" -eq 0 ]] || red "build: warnings=$warnings: $(grep -m1 -E "warning C[0-9]|warning:" "$OUT/build.log" | tr -d '\r')"
[[ -x "$EXE" ]] || red "build: missing $EXE"

# ---- 2. the lanes, concurrently ---------------------------------------------------------
rm -f "$COV_DIR"/*.csv "$COV_CSV"

# run_lane <lane> <cmd...>: runs one lane to $OUT/<lane>.log and records its exit and wall clock.
run_lane() {
    local lane="$1"
    shift
    local log="$OUT/$lane.log"
    local s0 rc s
    s0=$(date +%s%N)
    {
        echo "=== $lane $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $commit"
        echo "cmd: $*"
    } > "$log"
    "$@" 2>&1 | tr -d '\r' >> "$log"
    rc=${PIPESTATUS[0]}
    s=$(secs_since "$s0")
    echo "WALL $lane ${s} s exit=$rc" >> "$log"
    echo "$lane exit=$rc wall=${s}s" >> "$STATUS"
    echo "[library] $lane exit=$rc wall=${s}s"
    return "$rc"
}

LANES=()
pids=()
t0=$(date +%s%N)
for k in 0 1 2 3 4 5 6 7; do
    LANES+=("showcase-$k")
    run_lane "showcase-$k" "$EXE" "Profundum_PresetShowcase_${k}of8" &
    pids+=($!)
done
LANES+=(sweep-set)
# One comma-joined spec: Catch2 ANDs space-separated positional filters (the first run matched nothing).
run_lane sweep-set "$EXE" "Profundum_PresetSweep*,Profundum_PresetTruePeakMaxMass,Profundum_PresetMonoSum,Profundum_PresetLoudnessMatch" &
pids+=($!)
LANES+=(determinism)
run_lane determinism "$EXE" "Profundum_PresetDeterminism" &
pids+=($!)
LANES+=(distinctness)
run_lane distinctness "$EXE" "Profundum_PresetDistinctness" &
pids+=($!)
LANES+=(listen)
run_lane listen bash "$EV/listen/render.sh" &
pids+=($!)

failed=()
for i in "${!pids[@]}"; do
    wait "${pids[$i]}" || failed+=("${LANES[$i]}")
done
lanes_s=$(secs_since "$t0")
echo "lanes wall=${lanes_s}s failed=${#failed[@]}" >> "$STATUS"
echo "[library] lanes done wall=${lanes_s}s failed=${#failed[@]}"

for lane in "${LANES[@]}"; do
    echo "----- $lane (last lines) -----"
    tail -n 4 "$OUT/$lane.log"
done

# ---- 3. coverage matrix -----------------------------------------------------------------
claims=0
shards_seen=0
for k in 0 1 2 3 4 5 6 7; do
    line="$(grep -E "^# Profundum_PresetShowcase_${k}of8: presets=[0-9]+ arms=[0-9]+" "$OUT/showcase-$k.log" | tail -n 1)"
    if [[ -n "$line" ]]; then
        arms="$(echo "$line" | sed -E 's/.* arms=([0-9]+).*/\1/')"
        claims=$(( claims + arms ))
        shards_seen=$(( shards_seen + 1 ))
    fi
done
merge_rc=1
if [[ $shards_seen -eq 8 ]]; then
    node "$EV/merge-csv.js" "$COV_DIR" "$COV_CSV" "$claims" --group-min > "$OUT/merge.log" 2>&1
    merge_rc=$?
    tail -n 2 "$OUT/merge.log"
else
    echo "merge-csv: skipped, $shards_seen of 8 shard summary lines" > "$OUT/merge.log"
    cat "$OUT/merge.log"
fi
echo "coverage-merge exit=$merge_rc claims=$claims shards=$shards_seen" >> "$STATUS"

# ---- 4. restore Phase 9 evidence ----------------------------------------------------------
git checkout -- specs/profundum-phase9-voice-engine/evidence/
echo "phase9-evidence-restore exit=$?" >> "$STATUS"

# ---- 5. render guard, alone (SC-027 wall clock) ------------------------------------------
guard_rc=1
guard_case_s=""
if [[ ${#failed[@]} -eq 0 ]]; then
    g0=$(date +%s%N)
    "$EXE" "Profundum_PresetRenderGuard_*" 2>&1 | tr -d '\r' > "$OUT/render-guard.log"   # R-11 split: _0of2, _1of2
    guard_rc=${PIPESTATUS[0]}
    g_s=$(secs_since "$g0")
    guard_case_s="$(grep -E "^render-guard worst truePeak=.* wall clock [0-9.e+-]+ s over [0-9]+ presets" \
        "$OUT/render-guard.log" | sed -E 's/.* wall clock ([0-9.e+-]+) s over.*/\1/' | sort -g | tail -n 1)"   # the slower shard
    echo "WALL render-guard ${g_s} s exit=$guard_rc case_seconds=${guard_case_s:-none}" >> "$OUT/render-guard.log"
    echo "render-guard exit=$guard_rc wall=${g_s}s case_seconds=${guard_case_s:-none} bar=${GUARD_BAR_S}s" >> "$STATUS"
    echo "[library] render-guard exit=$guard_rc wall=${g_s}s case_seconds=${guard_case_s:-none}"
    tail -n 3 "$OUT/render-guard.log"
else
    echo "render-guard skipped (concurrent lanes red)" >> "$STATUS"
fi

# ---- verdict ------------------------------------------------------------------------------
if [[ ${#failed[@]} -gt 0 ]]; then
    lane="${failed[0]}"
    red "$lane: $(first_failed "$OUT/$lane.log") (failed lanes: ${failed[*]})"
fi
[[ $merge_rc -eq 0 ]] || red "coverage-merge: $(grep -m1 'merge-csv:' "$OUT/merge.log")"
if [[ -z "$guard_case_s" ]]; then
    red "render-guard: no wall-clock line; $(first_failed "$OUT/render-guard.log")"
fi
if awk -v s="$guard_case_s" -v b="$GUARD_BAR_S" 'BEGIN { exit !(s > b) }'; then
    red "render-guard: wall clock ${guard_case_s} s > ${GUARD_BAR_S} s with the full library: split needed (more shards, R-11)"
fi
[[ $guard_rc -eq 0 ]] || red "render-guard: $(first_failed "$OUT/render-guard.log")"
echo "verdict GREEN claims=$claims" >> "$STATUS"
echo "LIBRARY LANES GREEN"
