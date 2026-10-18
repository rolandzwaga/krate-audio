#!/usr/bin/env bash
# Profundum Phase 6, task T001 lane script (plan R-1, S12.4, S16 step 1): the Mass dynamics CPU bench.
#
# Run ALONE from anywhere (Git Bash), nothing else executing on the machine:
#   bash specs/profundum-phase6-mass-dynamics/bench/run-bench.sh [base|tick4|tick4-tp2x] [--quick]
# variant: base (default) | tick4 (-DP6_DYN_TICK4, R-1 fallback 2) | tick4-tp2x (+ -DP6_TP2X, R-1 fallback 3)
# All three binaries are compiled; only the selected one is run, 3 times, SETTLE_S (default 60 s) apart, each
# pinned through tools/pin-perf-cores.ps1. Prints the median per cell and the T001 decision line; everything is
# appended to specs/profundum-phase6-mass-dynamics/evidence/run-log/T001-bench.txt (BENCH_LOG overrides the path,
# e.g. for a smoke run that must not touch the evidence log).
# --quick: one 0.2 s-per-cell run, no settle (smoke only; no timing decision may be taken from it).
# Decision rule (tasks.md T001): median Total (cell 3) <= 1.0 % of one core -> proceed; else re-run with tick4, then
# tick4-tp2x; still > 1.0 % -> return to the user with the table. The bar (FR-045) is never relaxed.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$REPO"
SRC="specs/profundum-phase6-mass-dynamics/bench/mass-dynamics-bench.cpp"
LOG="${BENCH_LOG:-specs/profundum-phase6-mass-dynamics/evidence/run-log/T001-bench.txt}"
OUT="${BENCH_DIR:-${TMPDIR:-/tmp}/p6-bench}"
CXX="/c/Program Files/LLVM/bin/clang++.exe"
SETTLE_S="${SETTLE_S:-60}"
VARIANT="base"
QUICK=0
for arg in "$@"; do
    case "$arg" in
        --quick) QUICK=1 ;;
        base|tick4|tick4-tp2x) VARIANT="$arg" ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done
mkdir -p "$OUT" "$(dirname "$LOG")"

# Isolation precondition: refuse to start while a compiler, a test suite or clang-tidy is running.
BUSY="$(tasklist | grep -ciE "cl.exe|dsp_.*_tests.exe|clang-tidy" || true)"
if [ "$BUSY" != "0" ]; then
    echo "refusing to start: $BUSY competing process(es) (cl.exe / dsp_*_tests.exe / clang-tidy) are running" >&2
    tasklist | grep -iE "cl.exe|dsp_.*_tests.exe|clang-tidy" >&2 || true
    exit 3
fi

FLAGS=(-std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG -Idsp/include)
echo "compiling 3 binaries into $OUT" >&2
"$CXX" "${FLAGS[@]}" "$SRC" -o "$OUT/p6-bench.exe"
"$CXX" "${FLAGS[@]}" -DP6_DYN_TICK4 "$SRC" -o "$OUT/p6-bench-tick4.exe"
"$CXX" "${FLAGS[@]}" -DP6_DYN_TICK4 -DP6_TP2X "$SRC" -o "$OUT/p6-bench-tick4-tp2x.exe"

case "$VARIANT" in
    base) EXE="$OUT/p6-bench.exe" ;;
    tick4) EXE="$OUT/p6-bench-tick4.exe" ;;
    tick4-tp2x) EXE="$OUT/p6-bench-tick4-tp2x.exe" ;;
esac
EXE_WIN="$(cygpath -w "$EXE")"

RUNS=3
ARGS=()
if [ "$QUICK" -eq 1 ]; then RUNS=1; ARGS=(-ExeArgs "--quick"); fi

{
    echo "=== T001 bench $(date -u +%Y-%m-%dT%H:%M:%SZ) variant=$VARIANT runs=$RUNS settle=${SETTLE_S}s quick=$QUICK"
    echo "compile: \"$CXX\" ${FLAGS[*]} <variant defines> $SRC"
    echo "commit: $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
} | tee -a "$LOG"

for r in $(seq 1 "$RUNS"); do
    if [ "$r" -gt 1 ]; then sleep "$SETTLE_S"; fi
    echo "--- run $r" | tee -a "$LOG"
    pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe "$EXE_WIN" "${ARGS[@]}" | tr -d '\r' | tee "$OUT/run$r.txt" | tee -a "$LOG"
done

FILES=()
for r in $(seq 1 "$RUNS"); do FILES+=("$OUT/run$r.txt"); done
cat "${FILES[@]}" | awk -v runs="$RUNS" -v variant="$VARIANT" -v quick="$QUICK" '
    $1 == "cell" { if (!(($2) in v)) name[++n] = $2; v[$2] = v[$2] " " $6 }
    END {
        print "--- medians over " runs " run(s) (% of one core, 48 kHz, variant " variant ")"
        total = -1
        for (i = 1; i <= n; ++i) {
            k = name[i]; m = split(substr(v[k], 2), a, " ")
            for (x = 1; x <= m; ++x) for (y = x + 1; y <= m; ++y) if (a[y] + 0 < a[x] + 0) { t = a[x]; a[x] = a[y]; a[y] = t }
            med = (m % 2) ? a[(m + 1) / 2] : (a[m / 2] + a[m / 2 + 1]) / 2
            printf "median %-12s %.4f\n", k, med
            if (k == "total") total = med
        }
        if (total < 0) { print "DECISION: NONE (no total cell parsed)"; exit }
        if (quick == 1) { printf "DECISION: NONE (--quick smoke; total %.4f is not a timing reading)\n", total; exit }
        if (total <= 1.0) d = "PROCEED (median Total <= 1.0 %) with variant " variant
        else if (variant == "base") d = "Total > 1.0 % -> re-run with tick4 (-DP6_DYN_TICK4)"
        else if (variant == "tick4") d = "Total > 1.0 % -> re-run with tick4-tp2x (-DP6_DYN_TICK4 -DP6_TP2X)"
        else d = "Total > 1.0 % with every R-1 fallback -> return to the user with the table (never relax FR-045)"
        printf "DECISION: median_total %.4f -> %s\n", total, d
        print "RECORD: shaper / compressor / dc_limiter / linked / silence are recorded, not gated"
    }' | tee -a "$LOG"
