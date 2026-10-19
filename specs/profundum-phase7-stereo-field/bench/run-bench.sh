#!/usr/bin/env bash
# Profundum Phase 7, task T001 lane script (plan R-1, S12.6, S16 step 1): the stereo-field CPU bench.
#
# Run ALONE from anywhere (Git Bash), nothing else executing on the machine:
#   bash specs/profundum-phase7-stereo-field/bench/run-bench.sh [--quick]
# Compiles one binary, runs it 3 times, SETTLE_S (default 60 s) apart, each pinned through tools/pin-perf-cores.ps1.
# Prints the median per cell and the T001 decision line; everything is appended to
# specs/profundum-phase7-stereo-field/evidence/run-log/T001-bench.txt (BENCH_LOG overrides the path, e.g. for a
# smoke run that must not touch the evidence log).
# --quick: one 0.2 s-per-cell run, no settle (smoke only; no timing decision may be taken from it).
# Decision rule (tasks.md T001): per voice (law_1v - base_1v) <= 0.25 % AND four voices (law_4v - base_4v) <= 1.0 %
# AND stage_static <= 0.5 % AND stage_sweep <= 0.5 % -> proceed with the plan's design. Otherwise return to the user
# with the table and R-1's options (the only lever is the FR-016 handoff cadence; a CPU ceiling is never relaxed).
# stage_sweep_192k is recorded only.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$REPO"
SRC="specs/profundum-phase7-stereo-field/bench/stereo-field-bench.cpp"
LOG="${BENCH_LOG:-specs/profundum-phase7-stereo-field/evidence/run-log/T001-bench.txt}"
OUT="${BENCH_DIR:-${TMPDIR:-/tmp}/p7-bench}"
CXX="/c/Program Files/LLVM/bin/clang++.exe"
SETTLE_S="${SETTLE_S:-60}"
QUICK=0
for arg in "$@"; do
    case "$arg" in
        --quick) QUICK=1 ;;
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

# ProfundumCore pulls in HarmonicOscillatorBank::processStereo, which calls processMcfBatchSIMD from the Highway TU
# harmonic_oscillator_bank_simd.cpp (not header-only). The bench compiles that TU with the same flags and links the
# Release hwy.lib of build/windows-x64-release; hwy.lib is /MD, so the bench uses the DLL CRT (-fms-runtime-lib=dll,
# /NODEFAULTLIB:libcmt). The SIMD render is identical in the baseline and law cells, so it cancels in the deltas.
HWY_SRC="build/windows-x64-release/_deps/highway-src"
HWY_LIB="build/windows-x64-release/lib/Release/hwy.lib"
SIMD_SRC="dsp/include/krate/dsp/processors/harmonic_oscillator_bank_simd.cpp"
for f in "$HWY_SRC/hwy/highway.h" "$HWY_LIB"; do
    [ -e "$f" ] || { echo "missing $f (configure + build build/windows-x64-release first)" >&2; exit 4; }
done
FLAGS=(-std=c++20 -O2 -ffast-math -march=x86-64-v2 -DNDEBUG -fms-runtime-lib=dll -Idsp/include -I"$HWY_SRC")
EXE="$OUT/p7-bench.exe"
echo "compiling $EXE" >&2
"$CXX" "${FLAGS[@]}" "$SRC" "$SIMD_SRC" "$HWY_LIB" -Xlinker /NODEFAULTLIB:libcmt -o "$EXE"
EXE_WIN="$(cygpath -w "$EXE")"

RUNS=3
ARGS=()
if [ "$QUICK" -eq 1 ]; then RUNS=1; ARGS=(-ExeArgs "--quick"); fi

{
    echo "=== T001 bench $(date -u +%Y-%m-%dT%H:%M:%SZ) runs=$RUNS settle=${SETTLE_S}s quick=$QUICK"
    echo "compile: \"$CXX\" ${FLAGS[*]} $SRC $SIMD_SRC $HWY_LIB -Xlinker /NODEFAULTLIB:libcmt -o $EXE"
    echo "commit: $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
} | tee -a "$LOG"

for r in $(seq 1 "$RUNS"); do
    if [ "$r" -gt 1 ]; then sleep "$SETTLE_S"; fi
    echo "--- run $r" | tee -a "$LOG"
    pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe "$EXE_WIN" "${ARGS[@]}" | tr -d '\r' | tee "$OUT/run$r.txt" | tee -a "$LOG"
done

FILES=()
for r in $(seq 1 "$RUNS"); do FILES+=("$OUT/run$r.txt"); done
cat "${FILES[@]}" | awk -v runs="$RUNS" -v quick="$QUICK" '
    $1 == "cell" { if (!(($2) in v)) name[++n] = $2; v[$2] = v[$2] " " $6 }
    END {
        print "--- medians over " runs " run(s) (% of one core; stage_sweep_192k at 192 kHz)"
        for (i = 1; i <= n; ++i) {
            k = name[i]; m = split(substr(v[k], 2), a, " ")
            for (x = 1; x <= m; ++x) for (y = x + 1; y <= m; ++y) if (a[y] + 0 < a[x] + 0) { t = a[x]; a[x] = a[y]; a[y] = t }
            med[k] = (m % 2) ? a[(m + 1) / 2] : (a[m / 2] + a[m / 2 + 1]) / 2
            printf "median %-18s %.4f\n", k, med[k]
        }
        need = "base_1v law_1v base_4v law_4v stage_static stage_sweep"
        c = split(need, req, " ")
        for (i = 1; i <= c; ++i) if (!(req[i] in med)) { print "DECISION: NONE (cell " req[i] " not parsed)"; exit }
        pv = med["law_1v"] - med["base_1v"]
        fv = med["law_4v"] - med["base_4v"]
        st = med["stage_static"]; sw = med["stage_sweep"]
        printf "per_voice %.4f (<= 0.25) four_voice %.4f (<= 1.0) stage_static %.4f (<= 0.5) stage_sweep %.4f (<= 0.5)\n", pv, fv, st, sw
        if (quick == 1) { print "DECISION: NONE (--quick smoke; not a timing reading)"; exit }
        if (pv <= 0.25 && fv <= 1.0 && st <= 0.5 && sw <= 0.5) d = "PROCEED with the plan design"
        else d = "OVER BUDGET -> return to the user with the table and R-1 options (FR-016 cadence; never relax a ceiling)"
        printf "DECISION: %s\n", d
        if ("stage_sweep_192k" in med) printf "RECORD: stage_sweep_192k %.4f (recorded, not gated)\n", med["stage_sweep_192k"]
    }' | tee -a "$LOG"
