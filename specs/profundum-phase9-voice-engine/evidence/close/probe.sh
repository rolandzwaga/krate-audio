#!/usr/bin/env bash
# Profundum Phase 9 probes (plan S10.4). Structure copied from specs/profundum-phase8-bass-morph/evidence/close/probe.sh.
#
# Run by the MAIN LOOP after the gate build of dsp_systems_tests (Git Bash, from anywhere):
#   bash specs/profundum-phase9-voice-engine/evidence/close/probe.sh <p3|p1|p2|p4|h13|all-but-p1>
# Refuses to start while a build (cl / link / MSBuild / ninja / cmake), a dsp_*_tests.exe or clang-tidy is running.
#   p3  (T006, Group 4; plan R-C, C-3): dsp_systems_tests.exe "MassDynamics_OversamplingAliasProbe"
#       -> evidence/run-log/close/p3.log (the case writes evidence/probes/p3-output-alias.txt;
#          last line "P3 VERDICT GREEN|RED <worst on dBFS>")
#   p1  (T013, Group 9; plan R-A): run ALONE, pinned to the performance cores:
#       pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe <exe> -ExeArgs "VoiceEngine_CpuProbe"
#       -> evidence/run-log/close/p1.log (writes evidence/probes/p1-cpu.txt; "P1 VERDICT GREEN|RED ...")
#   p2  (T013; plan R-B): "VoiceEngine_DeclickReferenceProbe" -> evidence/run-log/close/p2.log
#       (writes evidence/probes/p2-declick.txt; "P2 VERDICT GREEN|RED ...")
#   p4  (T013): "VoiceEngine_FollowFlipProbe" -> evidence/run-log/close/p4.log
#       (writes evidence/probes/p4-follow-flip.txt; "P4 VERDICT FLIPS|FOLLOW INERT")
#   h13 (T013; FR-070 H-13 Record): "VoiceEngine_CharacterRecords" -> evidence/run-log/close/h13.log
#       (writes evidence/h13-character-records.txt; no verdict: the last "H13 GROWL_SWING" line is logged)
#   all-but-p1: p2, p4 and h13 concurrently (none is timing-sensitive), each to its own log.
# Each verdict line, with its wall clock, is appended to evidence/run-log/<Tnnn>.txt.
# Decision rules:
#   P3 GREEN -> Group 5. P3 RED -> STOP AND SURFACE to the user with the table (R-C; Q4's -80 dBFS bar is not moved).
#   T013 checkpoint (Group 9): run `all-but-p1`, then `p1` ALONE (nothing else running).
#   P1 RED -> STOP AND SURFACE with the breakdown (R-A: L3 with a user ruling, an append-only cost fix in a Phase 2 / 5
#   component with a ruling, or another ruling; a ceiling is never relaxed). P2 RED -> STOP AND SURFACE with the readings
#   (R-B; RetriggerDeclick's 1.5 ms is not changed). P4 "FOLLOW INERT" is recorded and carried to T016 / T017.
#   No Group 11 gated render is trusted until P-1 and P-2 read GREEN.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO"
SPEC="specs/profundum-phase9-voice-engine"
EXE="build/windows-x64-release/bin/Release/dsp_systems_tests.exe"

usage() {
    echo "usage: bash $SPEC/evidence/close/probe.sh <p3|p1|p2|p4|h13|all-but-p1>" >&2
    exit 64
}
[ "$#" -eq 1 ] || usage

# run_probe <name> <catch2 case> <verdict regex> <run-log task id> [pinned]
run_probe() {
    local name="$1" testCase="$2" verdictRe="$3" task="$4" pinned="${5:-}"
    local log="$SPEC/evidence/run-log/close/$name.log"
    local runlog="$SPEC/evidence/run-log/$task.txt"
    mkdir -p "$(dirname "$log")" "$(dirname "$runlog")"
    local commit
    commit="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
    local exeWin
    exeWin="$(cygpath -w "$REPO/$EXE")"
    {
        echo "=== $name probe $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $commit"
        if [ "$pinned" = "pinned" ]; then
            echo "cmd: pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe $EXE -ExeArgs \"$testCase\""
        else
            echo "cmd: $EXE \"$testCase\""
        fi
    } > "$log"
    local start status wall
    start=$(date +%s)
    set +e
    if [ "$pinned" = "pinned" ]; then
        pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe "$exeWin" -ExeArgs "$testCase" 2>&1 | tr -d '\r' >> "$log"
        status=${PIPESTATUS[0]}
    else
        "$EXE" "$testCase" 2>&1 | tr -d '\r' >> "$log"
        status=${PIPESTATUS[0]}
    fi
    set -e
    wall=$(( $(date +%s) - start ))
    echo "exit: $status wall: ${wall} s" >> "$log"
    tail -n 12 "$log"

    local verdict
    verdict="$(grep -E "$verdictRe" "$log" | tail -n 1 || true)"
    [ -z "$verdict" ] && verdict="${name^^} VERDICT MISSING (case did not complete; exit $status; see $log)"
    echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) probe $name exit=$status wall=${wall}s | $verdict" >> "$runlog"
    echo "probe: verdict appended to $runlog" >&2
    echo "$verdict"
    return "$status"
}

busy="$(tasklist | grep -ciE "^(cl|link|msbuild|ninja|cmake|clang-tidy)\.exe|^dsp_.*_tests\.exe" || true)"
if [ "${busy:-0}" -ne 0 ]; then
    echo "probe: refusing to start: $busy competing process(es) running (build / dsp_*_tests.exe / clang-tidy)" >&2
    tasklist | grep -iE "^(cl|link|msbuild|ninja|cmake|clang-tidy)\.exe|^dsp_.*_tests\.exe" >&2 || true
    exit 3
fi
if [ ! -f "$EXE" ]; then
    echo "probe: $EXE not found; build dsp_systems_tests (Release) first" >&2
    exit 2
fi

case "$1" in
    p3)
        run_probe p3 "MassDynamics_OversamplingAliasProbe" "^P3 VERDICT" T006
        ;;
    p1)
        # R-A: timing-sensitive. The busy check above refused any build / test / tidy; run nothing else meanwhile.
        run_probe p1 "VoiceEngine_CpuProbe" "^P1 VERDICT" T013 pinned
        ;;
    p2)
        run_probe p2 "VoiceEngine_DeclickReferenceProbe" "^P2 VERDICT" T013
        ;;
    p4)
        run_probe p4 "VoiceEngine_FollowFlipProbe" "^P4 VERDICT" T013
        ;;
    h13)
        run_probe h13 "VoiceEngine_CharacterRecords" "^H13 GROWL_SWING" T013
        ;;
    all-but-p1)
        # Not timing-sensitive: concurrent, each to its own log (CLAUDE.md "Optimise every build and test run").
        run_probe p2 "VoiceEngine_DeclickReferenceProbe" "^P2 VERDICT" T013 > /dev/null & pid2=$!
        run_probe p4 "VoiceEngine_FollowFlipProbe" "^P4 VERDICT" T013 > /dev/null & pid4=$!
        run_probe h13 "VoiceEngine_CharacterRecords" "^H13 GROWL_SWING" T013 > /dev/null & pid13=$!
        rc=0
        wait "$pid2" || rc=$?
        wait "$pid4" || rc=$?
        wait "$pid13" || rc=$?
        for n in p2 p4 h13; do
            grep -E "^(P2 VERDICT|P4 VERDICT|H13 GROWL_SWING)" "$SPEC/evidence/run-log/close/$n.log" | tail -n 1 || \
                echo "${n^^}: no verdict line (see $SPEC/evidence/run-log/close/$n.log)"
        done
        exit "$rc"
        ;;
    *)
        usage
        ;;
esac
