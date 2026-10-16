#!/usr/bin/env bash
# Profundum Phase 5, task T001 lane script (plan C-20, C-23, C-24, R-1, S10.7): the Shape + Character CPU bench.
#
# Run ALONE from anywhere (Git Bash), nothing else executing on the machine:
#   bash specs/profundum-phase5-shape-character/bench/run-bench.sh [linked|unlinked] [variant] [--quick]
# mode: linked (default; the SC-015 gated cell after the 2026-10-15 user ruling: L == R, one channel processed and
#       copied, the mono link of plan S2) | unlinked (decorrelated L/R, both channels processed; recorded only)
# variant: base (default) | ctrl16 (-DP5_CTRL_INTERVAL=16) | ctrl16-rectfloat (+ -DP5_RECT_ADAA_FLOAT)
# All three binaries are compiled; only the selected one is run, 3 times, SETTLE_S (default 60 s, the
# tools/run-cpu-tests.js SETTLE_MS default) apart, each pinned through tools/pin-perf-cores.ps1.
# Prints the median per cell, the median Total and the T001 epoch-2 decision line; everything goes to
# specs/profundum-phase5-shape-character/evidence/run-log/T001-bench.txt (appended, one section per invocation;
# BENCH_LOG overrides the path, e.g. for a smoke run that must not touch the evidence log).
# --quick: one 0.2 s-per-cell run, no settle (smoke only; no timing decision may be taken from it).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$REPO"
SRC="specs/profundum-phase5-shape-character/bench/shape-character-bench.cpp"
LOG="${BENCH_LOG:-specs/profundum-phase5-shape-character/evidence/run-log/T001-bench.txt}"
OUT="${BENCH_DIR:-${TMPDIR:-/tmp}/p5-bench}"
CXX="/c/Program Files/LLVM/bin/clang++.exe"
SETTLE_S="${SETTLE_S:-60}"
VARIANT="base"
MODE="linked"
QUICK=0
for arg in "$@"; do
    case "$arg" in
        --quick) QUICK=1 ;;
        linked|unlinked) MODE="$arg" ;;
        base|ctrl16|ctrl16-rectfloat) VARIANT="$arg" ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done
mkdir -p "$OUT" "$(dirname "$LOG")"

FLAGS=(-std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG -Idsp/include)
echo "compiling 3 binaries into $OUT" >&2
"$CXX" "${FLAGS[@]}" "$SRC" -o "$OUT/p5-bench.exe"
"$CXX" "${FLAGS[@]}" -DP5_CTRL_INTERVAL=16 "$SRC" -o "$OUT/p5-bench-ctrl16.exe"
"$CXX" "${FLAGS[@]}" -DP5_CTRL_INTERVAL=16 -DP5_RECT_ADAA_FLOAT "$SRC" -o "$OUT/p5-bench-ctrl16-rectfloat.exe"

case "$VARIANT" in
    base) EXE="$OUT/p5-bench.exe" ;;
    ctrl16) EXE="$OUT/p5-bench-ctrl16.exe" ;;
    ctrl16-rectfloat) EXE="$OUT/p5-bench-ctrl16-rectfloat.exe" ;;
esac
EXE_WIN="$(cygpath -w "$EXE")"

RUNS=3
EXEARGS=""
if [ "$MODE" = "linked" ]; then EXEARGS="--linked"; fi
if [ "$QUICK" -eq 1 ]; then RUNS=1; EXEARGS="${EXEARGS:+$EXEARGS }--quick"; fi
ARGS=()
# pin-perf-cores.ps1 passes -ExeArgs to Start-Process -ArgumentList, which joins on spaces, so one string carries
# every flag.
if [ -n "$EXEARGS" ]; then ARGS=(-ExeArgs "$EXEARGS"); fi

{
    echo "=== T001 bench $(date -u +%Y-%m-%dT%H:%M:%SZ) mode=$MODE variant=$VARIANT runs=$RUNS settle=${SETTLE_S}s quick=$QUICK"
    echo "compile: \"$CXX\" ${FLAGS[*]} <variant defines> $SRC"
    echo "commit: $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
} | tee -a "$LOG"

for r in $(seq 1 "$RUNS"); do
    if [ "$r" -gt 1 ]; then sleep "$SETTLE_S"; fi
    echo "--- run $r" | tee -a "$LOG"
    pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe "$EXE_WIN" "${ARGS[@]}" | tr -d '\r' | tee "$OUT/run$r.txt" | tee -a "$LOG"
done

# Medians per cell over the runs; Total = median(worst char) + median(worst shape) per the cell medians.
cat "$OUT"/run[1-$RUNS].txt | awk -v runs="$RUNS" -v mode="$MODE" '
    $1 == "cell" { v[$2] = v[$2] " " $6; order[$2] = (order[$2] ? order[$2] : ++n); name[order[$2]] = $2 }
    END {
        print "--- medians over " runs " run(s) (% of one core, 48 kHz, " mode ")"
        wc = 0; ws = 0
        for (i = 1; i <= n; ++i) {
            k = name[i]; m = split(substr(v[k], 2), a, " ")
            for (x = 1; x <= m; ++x) for (y = x + 1; y <= m; ++y) if (a[y] < a[x]) { t = a[x]; a[x] = a[y]; a[y] = t }
            med = (m % 2) ? a[(m + 1) / 2] : (a[m / 2] + a[m / 2 + 1]) / 2
            printf "median %-14s %.4f\n", k, med
            if (k ~ /^char_/ && med > wc) { wc = med; wcn = k }
            if (k ~ /^shape_/ && med > ws) { ws = med; wsn = k }
            if (k == "ramp") ramp = med
            if (k == "drop") drop = med
        }
        total = wc + ws
        printf "median_total_%s %.4f (%s %.4f + %s %.4f)\n", mode, total, wcn, wc, wsn, ws
        # Epoch-2 decision rule (tasks.md "T001 - EPOCH 2"): only the LINKED Total gates; unlinked, ramp, drop are recorded.
        if (mode == "linked") {
            if (total <= 2.0) d = "PROCEED (linked Total <= 2.0 %); C-23 and C-24 REQUIRED in T009-T015 as prototyped"
            else d = "linked Total > 2.0 % -> return to the user with both tables (never relax, never gate another cell)"
        } else d = "unlinked Total RECORDED for Phases 7 / 9 (not gated)"
        printf "DECISION: %s\n", d
        printf "RECORD: %s ramp %.4f / drop %.4f (recorded, not gated)\n", mode, ramp, drop
    }' | tee -a "$LOG"
