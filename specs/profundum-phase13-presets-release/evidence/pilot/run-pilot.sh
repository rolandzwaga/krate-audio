#!/usr/bin/env bash
# ==============================================================================
# run-pilot.sh -- Profundum Phase 13 T023 pilot run (FR-013, FR-043; SC-008, SC-020 pilot; plan 5.4, 5.6, 5.8)
# ==============================================================================
# Run by the MAIN LOOP (Git Bash, from anywhere):
#   bash specs/profundum-phase13-presets-release/evidence/pilot/run-pilot.sh
# 1. Builds profundum_tests once (-- /nr:false), log -> evidence/pilot/build.log; must show 0 warnings.
# 2. Removes any stale evidence/distinctness.csv, then runs CONCURRENTLY (neither is timing-sensitive):
#      "Profundum_PresetDistinctnessPilot" (hidden [.pilot]; writes evidence/distinctness.csv: F, t_max, the
#                                           near-variant row and the positive-control row)
#      "Profundum_ReferenceColumns"        (per push; SC-008 reverse arms + the R-SOLID L == R short render)
#    each to its own part log; both are concatenated, with "WALL <case> <s> s exit=<rc>" lines, into
#    evidence/pilot/pilot.log.
# 3. Last line: "PILOT GREEN F=<F>" when both cases exit 0, evidence/distinctness.csv exists and records
#    near_variant_pass = 1 and control_pass = 1; otherwise "PILOT RED <reason>". A failing control is a STOP:
#    F is never lowered.
# ==============================================================================
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$ROOT" || exit 2

EV="specs/profundum-phase13-presets-release/evidence"
OUT="$EV/pilot"
CSV="$EV/distinctness.csv"
LOG="$OUT/pilot.log"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
EXE="build/windows-x64-release/bin/Release/profundum_tests.exe"
CASES=(Profundum_PresetDistinctnessPilot Profundum_ReferenceColumns)

red() {
    echo "PILOT RED $*"
    exit 1
}

mkdir -p "$OUT" || red "cannot create $OUT"
rm -f "$OUT"/*.log "$CSV"

# ---- 1. build -------------------------------------------------------------------
echo "[pilot] building profundum_tests -> $OUT/build.log"
b0=$(date +%s%N)
"$CMAKE" --build build/windows-x64-release --config Release --target profundum_tests -- /nr:false > "$OUT/build.log" 2>&1
brc=$?
b1=$(date +%s%N)
warnings=$(grep -cE "warning C[0-9]|warning:" "$OUT/build.log" || true)
warnings=${warnings:-0}
build_s=$(awk -v a="$b0" -v b="$b1" 'BEGIN { printf "%.1f", (b - a) / 1e9 }')
echo "[pilot] build exit=$brc warnings=$warnings wall=${build_s}s"
[[ $brc -eq 0 ]] || red "build exit=$brc (see $OUT/build.log)"
[[ "$warnings" -eq 0 ]] || red "build warnings=$warnings (see $OUT/build.log): $(grep -m1 -E "warning C[0-9]|warning:" "$OUT/build.log")"
[[ -x "$EXE" ]] || red "missing $EXE"

# ---- 2. the two cases, concurrently ---------------------------------------------------
t0=$(date +%s%N)
pids=()
for c in "${CASES[@]}"; do
    (
        part="$OUT/$c.part.log"
        s0=$(date +%s%N)
        "$EXE" "$c" > "$part" 2>&1
        rc=$?
        s1=$(date +%s%N)
        secs=$(awk -v a="$s0" -v b="$s1" 'BEGIN { printf "%.1f", (b - a) / 1e9 }')
        echo "WALL $c $secs s exit=$rc" >> "$part"
        echo "WALL $c $secs s exit=$rc"
        exit $rc
    ) &
    pids+=($!)
done

failed=()
for i in "${!pids[@]}"; do
    wait "${pids[$i]}" || failed+=("${CASES[$i]}")
done
t1=$(date +%s%N)
total_s=$(awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.1f", (b - a) / 1e9 }')
echo "[pilot] cases done wall=${total_s}s failed=${#failed[@]}"

{
    for c in "${CASES[@]}"; do
        echo "===== $c ====="
        tr -d '\r' < "$OUT/$c.part.log"
    done
} > "$LOG"
rm -f "$OUT"/*.part.log

# ---- 3. verdict -----------------------------------------------------------------------
csv_value() {
    # key,value rows; the value is the rest of the line after the first comma.
    tr -d '\r' < "$CSV" | awk -F',' -v k="$1" '$1 == k { sub(/^[^,]*,/, ""); print; exit }'
}

if [[ -f "$CSV" ]]; then
    echo "[pilot] distinctness.csv: F=$(csv_value F) t_max=$(csv_value t_max) t_max_twin=$(csv_value t_max_twin)"
    echo "[pilot]   near-variant: d=$(csv_value near_variant_d) masked=$(csv_value near_variant_masked) pass=$(csv_value near_variant_pass) ($(csv_value near_variant_parameter))"
    echo "[pilot]   control:      d=$(csv_value control_d) masked=$(csv_value control_masked) pass=$(csv_value control_pass)"
fi

if [[ ${#failed[@]} -gt 0 ]]; then
    line="$(grep -m1 -E "FR-023 STOP|FAILED|failed|error" "$LOG" || true)"
    red "failed cases=${failed[*]}; first: $line (see $LOG)"
fi
[[ -f "$CSV" ]] || red "missing $CSV"
F="$(csv_value F)"
[[ -n "$F" ]] || red "distinctness.csv has no F row"
[[ "$(csv_value near_variant_pass)" == "1" ]] || red "near-variant control failed (d >= F): STOP, F is never lowered"
[[ "$(csv_value control_pass)" == "1" ]] || red "positive control failed (d < F): STOP, F is never lowered"
echo "PILOT GREEN F=$F"
