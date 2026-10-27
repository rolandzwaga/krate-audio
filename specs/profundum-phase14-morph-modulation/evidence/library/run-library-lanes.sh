#!/usr/bin/env bash
# ==============================================================================
# run-library-lanes.sh -- Profundum Phase 14 T026: the library lanes after the Morph Mod conversions
#   (FR-041, FR-043, FR-051; SC-017 (b), SC-018 Phase 13 gates, SC-019 presets, SC-023 (a)(b))
# ==============================================================================
# Run by the MAIN LOOP, DETACHED (Git Bash, from anywhere):
#   bash specs/profundum-phase14-morph-modulation/evidence/library/run-library-lanes.sh
# Status file: evidence/library/status.txt, one line per step / lane: "<lane> exit=<rc> wall=<s>s".
# 1. ONE build of profundum_tests (-- /nr:false), log -> evidence/library/build.log; exit 0 and 0 warnings.
# 2. CONCURRENTLY (none is timing-sensitive), ONE PROCESS PER CASE NAME, each to its own evidence/library/<lane>.log
#    with its wall clock ("WALL <lane> <s> s exit=<rc>"), 27 processes on the 32-core box:
#      showcase-<k>   Profundum_PresetShowcase_<k>of8 (k 0-7; writes Phase 13 evidence/coverage-matrix/shard-<k>.csv,
#                     the four MorphMod columns' cells included: FR-041)
#      endpoints-<k>  Profundum_PresetMorphEndpoints_<k>of8 (k 0-7)
#      morph-sweep Profundum_PresetMorphSweep | sweep Profundum_PresetSweep | sweep-rates Profundum_PresetSweepRates
#      true-peak-max-mass Profundum_PresetTruePeakMaxMass | mono-sum Profundum_PresetMonoSum
#      determinism Profundum_PresetDeterminism | loudness-match Profundum_PresetLoudnessMatch
#      distinctness Profundum_PresetDistinctness | harness-fidelity Profundum_MorphModHarnessFidelity (SC-023 (a)(b))
#    Non-vacuity: every lane log must show exactly one test case run.
# 3. Coverage matrix: node <Phase 13>/evidence/merge-csv.js merges the 8 shards into <Phase 13>/evidence/coverage-matrix.csv
#    (expected rows = the sum of the shards' "arms=" counts). merge-csv.js --group-min refuses a column outside its
#    C-2.1 group table and has no MorphMod group (merge-csv.js:106-121, :136); that file is Phase 13 evidence outside
#    this phase's scope fence (T030), so it runs WITHOUT --group-min and the post-pass below re-adds the Phase 13
#    "group" field and group-min records in the same format, with "MorphMod" (M-*) as the twelfth group.
#    Then (SC-017 (b)): M-ENV, M-CYCLE, M-VEL, M-WHEEL each have a primary row at E1 and at C3 with value >= 3 dB;
#    each new primary's minimum margin is printed, and every group-min margin is printed beside the pre-phase one
#    (git HEAD's coverage-matrix.csv).
# 4. git checkout -- specs/profundum-phase9-voice-engine/evidence/ ; any modified Phase 13 evidence file other than
#    coverage-matrix* is restored too and named in the status file (tasks.md "Rules": only the coverage matrix may stay).
# 5. AFTER the concurrent lanes (so its quiet [wallclock] pass runs on a quiet box): the per-push
#    "[profundum][preset]" set through tools/run-gate.js (sharded; includes Profundum_PresetLoadUnderHeldNote, the
#    render guard shards and the SC-007 transparency shards), log -> evidence/library/per-push.log. The transparency
#    shards' WALL lines and the render guards' wall-clock lines are printed; a transparency shard above 9 s is
#    printed as a DEFECT line (re-shard to 64; the G17 gate records it), never a relaxed bar.
# 6. Pre-phase comparison: every lane's "# ..." summary / worst-margin lines beside the Phase 13 library run's
#    (git HEAD:specs/profundum-phase13-presets-release/evidence/library/*.log).
# Last line: "LIBRARY LANES GREEN" or "LIBRARY LANES RED <lane>: <first FAILED line>".
# Quote every lane's verdict, wall clock and worst margin vs the pre-phase run into evidence/run-log/T026.txt.
# A red Phase 13 gate on a converted preset is fixed by replacing that conversion with the next carrier from
# probe-morphmod.csv (plan 10) and re-running T025 + this lane, never by editing a bar.
# ==============================================================================
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$ROOT" || exit 2

EV="specs/profundum-phase14-morph-modulation/evidence"
OUT="$EV/library"
STATUS="$OUT/status.txt"
P13="specs/profundum-phase13-presets-release/evidence"
COV_DIR="$P13/coverage-matrix"
COV_CSV="$P13/coverage-matrix.csv"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
EXE="build/windows-x64-release/bin/Release/profundum_tests.exe"
PER_PUSH_FILTER='[profundum][preset]~[long]~[.]~[performance]~[perf]~[benchmark]~[!benchmark]'
GATE_OUT="f:/tmp/gate/profundum-p14-T026-$(date +%Y%m%dT%H%M%S)"
TRANSPARENCY_TARGET_S=9

mkdir -p "$OUT" || { echo "LIBRARY LANES RED setup: cannot create $OUT"; exit 1; }
rm -f "$OUT"/*.log "$STATUS"
commit="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
echo "# run-library-lanes (Phase 14 T026) $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $commit cores=$(nproc 2>/dev/null || echo '?')" > "$STATUS"

red() {
    echo "LIBRARY LANES RED $*"
    echo "verdict RED $*" >> "$STATUS"
    exit 1
}

secs_since() {
    awk -v a="$1" -v b="$(date +%s%N)" 'BEGIN { printf "%.1f", (b - a) / 1e9 }'
}

first_failed() {
    grep -m1 -E "FAILED|No test cases matched|error" "$1" | tr -d '\r'
}

# ---- 1. one build ------------------------------------------------------------------------------
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

# ---- 2. the lanes, concurrently, one process per case name ---------------------------------------
rm -f "$COV_DIR"/*.csv

# run_lane <lane> <case name>: runs one case to $OUT/<lane>.log and records its exit and wall clock.
run_lane() {
    local lane="$1"
    local name="$2"
    local log="$OUT/$lane.log"
    local s0 rc s
    s0=$(date +%s%N)
    {
        echo "=== $lane $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $commit"
        echo "cmd: $EXE \"$name\""
    } > "$log"
    "$EXE" "$name" 2>&1 | tr -d '\r' >> "$log"
    rc=${PIPESTATUS[0]}
    s=$(secs_since "$s0")
    echo "WALL $lane ${s} s exit=$rc" >> "$log"
    echo "$lane exit=$rc wall=${s}s" >> "$STATUS"
    echo "[library] $lane exit=$rc wall=${s}s"
    return "$rc"
}

LANES=()
CASES=()
for k in 0 1 2 3 4 5 6 7; do
    LANES+=("showcase-$k");  CASES+=("Profundum_PresetShowcase_${k}of8")
    LANES+=("endpoints-$k"); CASES+=("Profundum_PresetMorphEndpoints_${k}of8")
done
LANES+=(morph-sweep);        CASES+=(Profundum_PresetMorphSweep)
LANES+=(sweep);              CASES+=(Profundum_PresetSweep)
LANES+=(sweep-rates);        CASES+=(Profundum_PresetSweepRates)
LANES+=(true-peak-max-mass); CASES+=(Profundum_PresetTruePeakMaxMass)
LANES+=(mono-sum);           CASES+=(Profundum_PresetMonoSum)
LANES+=(determinism);        CASES+=(Profundum_PresetDeterminism)
LANES+=(loudness-match);     CASES+=(Profundum_PresetLoudnessMatch)
LANES+=(distinctness);       CASES+=(Profundum_PresetDistinctness)
LANES+=(harness-fidelity);   CASES+=(Profundum_MorphModHarnessFidelity)

echo "[library] lanes=${#LANES[@]} (one process per case)"
pids=()
t0=$(date +%s%N)
for i in "${!LANES[@]}"; do
    run_lane "${LANES[$i]}" "${CASES[$i]}" &
    pids+=($!)
done

failed=()
for i in "${!pids[@]}"; do
    wait "${pids[$i]}" || failed+=("${LANES[$i]}")
done
lanes_s=$(secs_since "$t0")
echo "lanes wall=${lanes_s}s failed=${#failed[@]}" >> "$STATUS"
echo "[library] lanes done wall=${lanes_s}s failed=${#failed[@]}"

# Non-vacuity: each lane ran exactly its one case (Catch2: "... in 1 test case)" or "test cases: 1 | ...").
vacuous=()
for lane in "${LANES[@]}"; do
    grep -qE "in 1 test case\)|test cases: +1 \|" "$OUT/$lane.log" || vacuous+=("$lane")
done
echo "lanes vacuous=${#vacuous[@]} ${vacuous[*]:-}" >> "$STATUS"

for lane in "${LANES[@]}"; do
    echo "----- $lane -----"
    grep -E "^# (Profundum_|worst|ungated|contradicting)" "$OUT/$lane.log" | head -n 4
    tail -n 3 "$OUT/$lane.log"
done

# ---- 3. coverage matrix --------------------------------------------------------------------------
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
morph_rc=1
if [[ $shards_seen -eq 8 ]]; then
    rm -f "$COV_CSV"
    node "$P13/merge-csv.js" "$COV_DIR" "$COV_CSV" "$claims" > "$OUT/merge.log" 2>&1
    merge_rc=$?
    if [[ $merge_rc -eq 0 ]]; then
        prephase_csv="$OUT/prephase-coverage-matrix.csv.tmp"
        git show "HEAD:$COV_CSV" > "$prephase_csv" 2>/dev/null || : > "$prephase_csv"
        # Post-pass: Phase 13 "group" field + group-min records (MorphMod added), then the SC-017 (b) checks.
        node - "$COV_CSV" "$prephase_csv" >> "$OUT/merge.log" 2>&1 <<'JS'
'use strict';
const fs = require('fs');
const [covPath, prePath] = process.argv.slice(2);
const fail = (m) => { console.log(`coverage-post: ${m}`); process.exit(1); };
const splitFields = (r) => {
    const f = []; let cur = ''; let q = false;
    for (const c of r) { if (c === '"') q = !q; if (c === ',' && !q) { f.push(cur); cur = ''; } else { cur += c; } }
    f.push(cur); return f;
};
const unquote = (f) => (f.startsWith('"') && f.endsWith('"') ? f.slice(1, -1).replace(/""/g, '"') : f);
const lines = (t) => t.replace(/\r/g, '').split('\n').filter((l) => l.length > 0);
// The merge-csv.js:106-121 C-2.1 table, plus the Phase 14 C-14 MorphMod group (M-ENV, M-CYCLE, M-VEL, M-WHEEL).
const kGroups = [[1, 3, 'Core'], [4, 9, 'Character'], [10, 18, 'Anchor'], [19, 23, 'Pitch'], [24, 25, 'Envelope'],
    [26, 28, 'Mass'], [29, 33, 'Field'], [34, 40, 'Motion'], [41, 42, 'Morph'], [43, 46, 'NoteMemory'], [47, 50, 'Macros']];
const kRef = { 'R-LOCK': 'Anchor', 'R-SINE': 'Anchor', 'R-SOLID': 'Field' };
const kOrder = [...kGroups.map((g) => g[2]), 'MorphMod'];
const groupOf = (col) => {
    const at = /^AT-(\d+)\b/.exec(col);
    if (at) { const n = Number(at[1]); const h = kGroups.find(([lo, hi]) => n >= lo && n <= hi); if (h) return h[2]; }
    const ref = /^(R-[A-Z]+)\b/.exec(col);
    if (ref && kRef[ref[1]]) return kRef[ref[1]];
    if (/^M-(ENV|CYCLE|VEL|WHEEL)\b/.test(col)) return 'MorphMod';
    return null;
};
const [header, ...rows] = lines(fs.readFileSync(covPath, 'utf8'));
if (header.startsWith('group,')) fail('merged file already carries a group field');
const names = splitFields(header).map(unquote);
const ix = (n) => { const i = names.indexOf(n); if (i < 0) fail(`header lacks ${n}`); return i; };
const iCol = ix('column'), iRole = ix('role'), iNote = ix('note'), iVal = ix('value'), iMargin = ix('margin');
const best = new Map();
const parsed = rows.map((r) => {
    const f = splitFields(r);
    const col = unquote(f[iCol] ?? '');
    const g = groupOf(col);
    if (g === null) fail(`column "${col}" is in no group`);
    const margin = Number(f[iMargin]);
    if (!Number.isFinite(margin)) fail(`non-numeric margin in ${r}`);
    const cur = best.get(g);
    if (cur === undefined || margin < cur.margin) best.set(g, { margin, f });
    return { r, f, g, col, margin };
});
const out = [`group,${header}`, ...parsed.map((p) => `"${p.g}",${p.r}`)];
for (const g of kOrder) {
    const b = best.get(g);
    if (b === undefined) continue;
    const f = [...b.f]; f[iRole] = 'group-min';
    out.push(`"${g}",${f.join(',')}`);
}
fs.writeFileSync(covPath, out.join('\n') + '\n');
// Pre-phase group-min margins (HEAD's coverage-matrix.csv, already in the group format).
const pre = new Map();
if (fs.existsSync(prePath)) {
    const [ph, ...pr] = lines(fs.readFileSync(prePath, 'utf8'));
    if (ph) {
        const pn = splitFields(ph).map(unquote);
        const pg = pn.indexOf('group'), prl = pn.indexOf('role'), pm = pn.indexOf('margin'), pc = pn.indexOf('column');
        for (const r of pr) {
            const f = splitFields(r);
            if (unquote(f[prl] ?? '') === 'group-min') pre.set(unquote(f[pg]), `${f[pm]} (${unquote(f[pg + 1] ?? '')} / ${unquote(f[pc])})`);
        }
    }
}
for (const g of kOrder) {
    const b = best.get(g);
    if (b === undefined) continue;
    console.log(`coverage-post: group-min ${g} margin=${b.margin} (${unquote(b.f[0] ?? '')} | ${unquote(b.f[iCol])}) pre-phase=${pre.get(g) ?? 'none'}`);
}
// SC-017 (b): each MorphMod column's primary at E1 and C3 with value >= 3 dB; its minimum margin printed.
let bad = 0;
for (const tag of ['M-ENV', 'M-CYCLE', 'M-VEL', 'M-WHEEL']) {
    const prim = parsed.filter((p) => p.col.startsWith(`${tag} `) && unquote(p.f[iRole]) === 'primary');
    let minMargin = null;
    for (const note of ['E1', 'C3']) {
        const hits = prim.filter((p) => unquote(p.f[iNote]) === note);
        const ok = hits.length > 0 && hits.every((p) => Number(p.f[iVal]) >= 3);
        if (!ok) { ++bad; console.log(`coverage-post: MORPHMOD RED ${tag} ${note}: rows=${hits.length} values=[${hits.map((p) => p.f[iVal]).join(' ')}]`); }
        for (const p of hits) if (minMargin === null || p.margin < minMargin) minMargin = p.margin;
    }
    const who = [...new Set(prim.map((p) => unquote(p.f[0])))].join('; ');
    console.log(`coverage-post: ${tag} primary=${who || 'none'} rows=${prim.length} min-margin=${minMargin ?? 'none'} dB`);
}
if (bad > 0) fail(`${bad} MorphMod primary arm(s) missing or below 3 dB`);
console.log(`coverage-post: OK rows=${rows.length} groups=${best.size}`);
JS
        morph_rc=$?
        rm -f "$prephase_csv"
    fi
    tr -d '\r' < "$OUT/merge.log" | grep -E "rows=|group-min|coverage-post" | sed 's/^/[library]   /'
else
    echo "merge-csv: skipped, $shards_seen of 8 shard summary lines" > "$OUT/merge.log"
    cat "$OUT/merge.log"
fi
echo "coverage-merge exit=$merge_rc morphmod-check exit=$morph_rc claims=$claims shards=$shards_seen" >> "$STATUS"
tr -d '\r' < "$OUT/merge.log" | grep -E "^coverage-post: (M-|group-min|MORPHMOD)" >> "$STATUS"

# ---- 4. restore Phase 9 evidence and any Phase 13 evidence other than the coverage matrix -------
git checkout -- specs/profundum-phase9-voice-engine/evidence/
echo "phase9-evidence-restore exit=$?" >> "$STATUS"
p13_touched="$(git status --porcelain -- "$P13" | sed -E 's/^.. //' | grep -vE "^$P13/coverage-matrix" || true)"
if [[ -n "$p13_touched" ]]; then
    echo "phase13-evidence-restore: $(echo "$p13_touched" | tr '\n' ' ')" >> "$STATUS"
    while IFS= read -r f; do
        git ls-files --error-unmatch -- "$f" > /dev/null 2>&1 && git checkout -- "$f"
    done <<< "$p13_touched"
fi

# ---- 5. the per-push [profundum][preset] set, after the concurrent lanes ------------------------
pp_rc=1
pp_last=""
if [[ ${#failed[@]} -eq 0 ]]; then
    listed="$("$EXE" --list-tests --verbosity quiet "$PER_PUSH_FILTER" 2>&1 | tr -d '\r')"
    n_cases=$(printf '%s\n' "$listed" | grep -c '^Profundum_' || true)
    echo "per-push listed=$n_cases" >> "$STATUS"
    # R10 (user 2026-10-27): the transparency shards are [long] (nightly), so the per-push filter no longer selects them.
    for must in Profundum_PresetLoadUnderHeldNote Profundum_PresetRenderGuard_0of16; do
        printf '%s\n' "$listed" | grep -qx "$must" || red "per-push: filter does not select $must (vacuous run)"
    done
    p0=$(date +%s%N)
    node tools/run-gate.js --filter "$PER_PUSH_FILTER" --out "$GATE_OUT" profundum_tests > "$OUT/per-push.log" 2>&1
    pp_rc=$?
    pp_s=$(secs_since "$p0")
    pp_last="$(tr -d '\r' < "$OUT/per-push.log" | grep -E '^GATE (GREEN|RED)' | tail -n 1)"
    {
        echo "WALL per-push ${pp_s} s exit=$pp_rc"
        echo "--- transparency shard wall clocks (target <= ${TRANSPARENCY_TARGET_S} s) and render guards ($GATE_OUT)"
        cat "$GATE_OUT"/*.log 2>/dev/null | tr -d '\r' | grep -E "^WALL Profundum_MorphModPresetTransparency_|^render-guard worst" | sort -V
    } >> "$OUT/per-push.log"
    echo "per-push exit=$pp_rc wall=${pp_s}s ${pp_last:-no GATE line}" >> "$STATUS"
    echo "[library] per-push exit=$pp_rc wall=${pp_s}s ${pp_last:-no GATE line}"
    t_lines="$(cat "$GATE_OUT"/*.log 2>/dev/null | tr -d '\r' | grep -E "^WALL Profundum_MorphModPresetTransparency_" || true)"
    t_n=$(printf '%s\n' "$t_lines" | grep -c . || true)
    t_max="$(printf '%s\n' "$t_lines" | awk 'NF { print $3 }' | sort -g | tail -n 1)"
    echo "transparency shards=$t_n max=${t_max:-none}s target=${TRANSPARENCY_TARGET_S}s" >> "$STATUS"
    echo "[library] transparency shards=$t_n max=${t_max:-none}s"
    printf '%s\n' "$t_lines" | awk -v t="$TRANSPARENCY_TARGET_S" 'NF && $3 > t { print "DEFECT transparency shard above " t " s (re-shard to 64): " $0 }' \
        | tee -a "$STATUS"
    cat "$GATE_OUT"/*.log 2>/dev/null | tr -d '\r' | grep -E "^render-guard worst" | sed 's/^/[library]   /'
else
    echo "per-push skipped (concurrent lanes red)" >> "$STATUS"
fi

# ---- 6. pre-phase comparison (Phase 13 library run at HEAD) -------------------------------------
{
    echo "--- pre-phase (git HEAD:$P13/library/*.log) summary lines"
    for f in $(git ls-files -- "$P13/library/*.log"); do
        git show "HEAD:$f" 2>/dev/null | tr -d '\r' | grep -E "^# (Profundum_|worst|ungated|contradicting)|^WALL " \
            | sed "s|^|$(basename "$f"): |"
    done
    echo "--- this run"
    for lane in "${LANES[@]}"; do
        grep -E "^# (Profundum_|worst|ungated|contradicting)|^WALL " "$OUT/$lane.log" | sed "s|^|$lane: |"
    done
} > "$OUT/compare.log"
cat "$OUT/compare.log"

# ---- verdict -------------------------------------------------------------------------------------
if [[ ${#failed[@]} -gt 0 ]]; then
    lane="${failed[0]}"
    red "$lane: $(first_failed "$OUT/$lane.log") (failed lanes: ${failed[*]})"
fi
[[ ${#vacuous[@]} -eq 0 ]] || red "vacuous lanes (not exactly one case run): ${vacuous[*]}"
[[ $shards_seen -eq 8 ]] || red "coverage-merge: $shards_seen of 8 showcase summary lines"
[[ $merge_rc -eq 0 ]] || red "coverage-merge: $(grep -m1 'merge-csv:' "$OUT/merge.log" | tr -d '\r')"
[[ $morph_rc -eq 0 ]] || red "coverage-morphmod: $(grep -m1 -E 'MORPHMOD RED|coverage-post: [^M]' "$OUT/merge.log" | tr -d '\r')"
[[ $pp_rc -eq 0 && "$pp_last" == GATE\ GREEN* ]] || red "per-push: ${pp_last:-no GATE line (exit $pp_rc)}"
echo "verdict GREEN claims=$claims" >> "$STATUS"
echo "LIBRARY LANES GREEN"
