#!/usr/bin/env bash
# Phase 10 D-3 gate lane (user ruling 2026-10-22): one rebuild of the plugin, its tests and the consumers of the two renamed
# dsp/ headers, then concurrently the per-push suites of those consumers, profundum_tests, pluginval, check-bundle and
# check-portability. Writes evidence/run-log/close/d3-status.txt (last line DONE ...). Detached from the main loop
# (multi-hour: dsp_systems_tests per-push is ~2 h). The CPU lane runs afterwards, alone.
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO"
SPEC="specs/profundum-phase10-plugin-scaffold"
L="$SPEC/evidence/run-log/close"
ST="$L/d3-status.txt"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
BIN="build/windows-x64-release/bin/Release"
FILTER="~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"
mkdir -p "$L"
echo "START $(date -u +%FT%TZ) commit $(git rev-parse --short HEAD)" > "$ST"

t0=$(date +%s)
MSYS_NO_PATHCONV=1 "$CMAKE" --build build/windows-x64-release --config Release \
    --target Profundum profundum_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests --parallel 32 -- /nr:false \
    > "$L/build-d3.log" 2>&1
rc=$?
w=$(grep -ciE 'warning C|error C' "$L/build-d3.log")
echo "build exit=$rc wall=$(( $(date +%s) - t0 ))s warnings=$w log=$L/build-d3.log" >> "$ST"
if [ "$rc" -ne 0 ] || [ "$w" -ne 0 ]; then
    echo "DONE $(date -u +%FT%TZ) RED build" >> "$ST"
    exit 1
fi

run_suite() {   # name
    local n="$1" s
    s=$(date +%s)
    "$BIN/$n.exe" "$FILTER" > "$L/consumer-$n.log" 2>&1
    local r=$?
    echo "exit: $r wall: $(( $(date +%s) - s )) s" >> "$L/consumer-$n.log"
    echo "suite:$n exit=$r wall=$(( $(date +%s) - s ))s log=$L/consumer-$n.log" >> "$ST"
}
run_suite dsp_systems_tests &
run_suite dsp_processors_tests &
run_suite dsp_effects_tests &
run_suite profundum_tests &
(
    s=$(date +%s)
    tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Profundum.vst3" > "$L/pluginval-d3.log" 2>&1
    echo "pluginval exit=$? wall=$(( $(date +%s) - s ))s log=$L/pluginval-d3.log" >> "$ST"
    s=$(date +%s)
    node tools/check-bundle.js build/windows-x64-release/VST3/Release/Profundum.vst3 > "$L/check-bundle-d3.log" 2>&1
    echo "check-bundle exit=$? wall=$(( $(date +%s) - s ))s log=$L/check-bundle-d3.log" >> "$ST"
    s=$(date +%s)
    node tools/check-portability.js > "$L/check-portability-d3.log" 2>&1
    echo "check-portability exit=$? wall=$(( $(date +%s) - s ))s log=$L/check-portability-d3.log" >> "$ST"
    wsl --shutdown 2>/dev/null || true
) &
wait
red=$(grep -E '^(suite|pluginval|check-bundle|check-portability):' "$ST" | grep -vc ' exit=0 ')
echo "DONE $(date -u +%FT%TZ) $([ "$red" -eq 0 ] && echo GREEN || echo RED) red=$red total_wall=$(( $(date +%s) - t0 ))s" >> "$ST"
