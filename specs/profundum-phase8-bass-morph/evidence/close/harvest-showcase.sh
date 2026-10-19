#!/usr/bin/env bash
# Profundum Phase 8, B7-2 showcase-B harvest (main loop, 2026-10-19). 96 candidates sharded over N concurrent
# dsp_systems_tests processes, each running the hidden case BassMorph_ShowcaseHarvestGrid with BM_HARVEST_SHARD=i/N.
# Output: evidence/harvest/showcase-<i>.txt (ROW / CAND lines); aggregate with evidence/close/aggregate-showcase.js.
set -euo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO"
SPEC="specs/profundum-phase8-bass-morph"
EXE="build/windows-x64-release/bin/Release/dsp_systems_tests.exe"
N="${1:-12}"
mkdir -p "$SPEC/evidence/harvest" "$SPEC/evidence/run-log/close"
start=$(date +%s)
pids=()
for ((i = 0; i < N; i++)); do
    BM_HARVEST_SHARD="$i/$N" "$EXE" "BassMorph_ShowcaseHarvestGrid" > "$SPEC/evidence/run-log/close/harvest-showcase-$i.log" 2>&1 &
    pids+=($!)
done
fail=0
for p in "${pids[@]}"; do
    wait "$p" || fail=1
done
wall=$(( $(date +%s) - start ))
echo "harvest-showcase: shards=$N fail=$fail wall=${wall}s" | tee -a "$SPEC/evidence/run-log/T008.txt"
exit $fail
