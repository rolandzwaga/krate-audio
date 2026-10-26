#!/usr/bin/env bash
# ==============================================================================
# run-probe.sh -- Profundum Phase 13 T017 probe run (FR-020, FR-022, FR-023; SC-009; plan 5.7)
# ==============================================================================
# 1. Builds profundum_tests once (-- /nr:false), log -> evidence/probe/build.log; must show 0 warnings.
# 2. Launches the 11 hidden cases Profundum_PresetAudibilityProbe_<Group> as 11 CONCURRENT processes,
#    stdout+stderr -> evidence/probe/<Group>.log, wall clock appended as "WALL <Group> <s> s exit=<rc>".
#    Each case writes evidence/probe-table/<Group>.csv and asserts gates (a)-(c).
# 3. node evidence/merge-csv.js evidence/probe-table evidence/probe-table.csv 102
# 4. git checkout -- specs/profundum-phase9-voice-engine/evidence/ (restores anything the run touched there).
# 5. Last line: "PROBE GREEN rows=102" when all 11 exit 0 and the merge passes, else "PROBE RED <first failing line>".
# Expected cost (plan 10): Motion ~30 min wall, the others < 10 min.
# ==============================================================================
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$ROOT" || exit 2

EV="specs/profundum-phase13-presets-release/evidence"
OUT="$EV/probe"
TABLE_DIR="$EV/probe-table"
TABLE="$EV/probe-table.csv"
P9="specs/profundum-phase9-voice-engine/evidence"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
EXE="build/windows-x64-release/bin/Release/profundum_tests.exe"
EXPECTED_ROWS=102
GROUPS_LIST=(Core Character Anchor Pitch Envelope Mass Field Motion Morph NoteMemory Macros)

red() {
    echo "PROBE RED $*"
    exit 1
}

# ---- guard: the restore step is a git checkout; refuse to discard changes this run did not make ----
dirty="$(git status --porcelain -uall -- "$P9")"
[[ -z "$dirty" ]] || red "Phase 9 evidence is not clean before the run (restore it first): $(echo "$dirty" | head -3 | tr '\n' ' ')"

mkdir -p "$OUT" || red "cannot create $OUT"
rm -f "$OUT"/*.log "$TABLE"
rm -rf "$TABLE_DIR"
mkdir -p "$TABLE_DIR" || red "cannot create $TABLE_DIR"

# ---- 1. build -------------------------------------------------------------------
echo "[probe] building profundum_tests -> $OUT/build.log"
b0=$(date +%s%N)
"$CMAKE" --build build/windows-x64-release --config Release --target profundum_tests -- /nr:false > "$OUT/build.log" 2>&1
brc=$?
b1=$(date +%s%N)
warnings=$(grep -cE "warning C[0-9]|warning:" "$OUT/build.log" || true)
warnings=${warnings:-0}
build_s=$(awk -v a="$b0" -v b="$b1" 'BEGIN { printf "%.1f", (b - a) / 1e9 }')
echo "[probe] build exit=$brc warnings=$warnings wall=${build_s}s"
[[ $brc -eq 0 ]] || red "build exit=$brc (see $OUT/build.log)"
[[ "$warnings" -eq 0 ]] || red "build warnings=$warnings (see $OUT/build.log): $(grep -m1 -E "warning C[0-9]|warning:" "$OUT/build.log")"
[[ -x "$EXE" ]] || red "missing $EXE"

# ---- 2. the 11 hidden cases, concurrently ---------------------------------------------
t0=$(date +%s%N)
pids=()
for g in "${GROUPS_LIST[@]}"; do
    (
        log="$OUT/$g.log"
        s0=$(date +%s%N)
        "$EXE" "Profundum_PresetAudibilityProbe_$g" > "$log" 2>&1
        rc=$?
        s1=$(date +%s%N)
        secs=$(awk -v a="$s0" -v b="$s1" 'BEGIN { printf "%.1f", (b - a) / 1e9 }')
        echo "WALL $g $secs s exit=$rc" >> "$log"
        echo "WALL $g $secs s exit=$rc"
        exit $rc
    ) &
    pids+=($!)
done

failed=()
for i in "${!pids[@]}"; do
    wait "${pids[$i]}" || failed+=("${GROUPS_LIST[$i]}")
done
t1=$(date +%s%N)
total_s=$(awk -v a="$t0" -v b="$t1" 'BEGIN { printf "%.1f", (b - a) / 1e9 }')
echo "[probe] cases done wall=${total_s}s failed=${#failed[@]}"

# ---- 3. merge -------------------------------------------------------------------------------
merge_out="$(node "$EV/merge-csv.js" "$TABLE_DIR" "$TABLE" "$EXPECTED_ROWS" 2>&1)"
mrc=$?
echo "[probe] merge exit=$mrc: $merge_out"

# ---- 4. restore Phase 9 evidence (and drop untracked files the run created there) ---------------
git checkout -- "$P9/"
git clean -fdq -- "$P9/" > /dev/null 2>&1 || true

# ---- 5. verdict -----------------------------------------------------------------------------
if [[ ${#failed[@]} -gt 0 ]]; then
    first=""
    for g in "${failed[@]}"; do
        line="$(grep -m1 -E "FR-023 STOP|FAILED|failed|error" "$OUT/$g.log" || true)"
        [[ -n "$line" ]] || line="$(tail -n 2 "$OUT/$g.log" | head -n 1)"
        first="$g: $line"
        break
    done
    red "failed groups=${failed[*]}; first: $first"
fi
[[ $mrc -eq 0 ]] || red "merge: $merge_out"

rows=$(node -e '
const t = require("fs").readFileSync(process.argv[1], "utf8").replace(/\r/g, "");
let q = false, n = 0, cur = 0;
for (const c of t) { if (c === "\"") q = !q; else if (c === "\n" && !q) { if (cur) n++; cur = 0; continue; } cur++; }
if (cur) n++;
console.log(n - 1);' "$TABLE")
[[ "$rows" -eq $EXPECTED_ROWS ]] || red "probe-table.csv has $rows data rows, expected $EXPECTED_ROWS"
echo "PROBE GREEN rows=$rows"
