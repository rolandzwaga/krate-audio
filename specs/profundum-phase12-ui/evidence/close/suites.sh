#!/usr/bin/env bash
# Profundum Phase 12, task T036 close lane: change-set scope, one build, the per-push lanes, the phase's own [long]
# lane, the consumer [long] listing, case listing, pluginval and the D3 / D4 table regeneration (SC-013 listing,
# SC-020, SC-021 build, SC-022, FR-095). Structure copied from specs/profundum-phase11-parameters/evidence/close/suites.sh.
#
# Lane scope (encoded 2026-10-18, Phase 6 ruling G9-4; FR-095): the executables are profundum_tests (the phase's TUs
# and the amended Phase 10 / 11 TUs) and dsp_systems_tests (the consumer of the two modified dsp/ headers). Never every
# [long] case of an executable. The 46 consumer [long] cases of dsp_systems_tests are LISTED, not run (tasks.md
# Deviation 6; ruled "not run", spec.md Clarifications, FR-095).
#
# Run from the main loop DETACHED (Git Bash, from the Bash tool), with tidy.sh and asan.sh concurrently:
#   nohup bash specs/profundum-phase12-ui/evidence/close/suites.sh \
#       > specs/profundum-phase12-ui/evidence/run-log/close/suites.out 2>&1 & disown
# Watch evidence/run-log/close/status.txt; its last line is "DONE ...".
#
# Steps:
#   1. Change-set scope:
#      - git diff --name-only 40f6ebca -- dsp/include dsp/src plugins/shared tests/test_helpers (tracked, modified) must
#        print exactly dsp/include/krate/dsp/systems/profundum_{engine,voice}.h; anything else is RED and stops the
#        script (no build, no lane). Untracked files under those paths are reported.
#      - the consumer set, grep -rlE "profundum_engine\.h|profundum_voice\.h" dsp plugins tests, is written to the
#        status file. Expected: dsp/ (the systems headers, dsp/CMakeLists.txt, dsp/tests/unit/systems/) and
#        plugins/profundum/ only. A consumer anywhere else (Ruinae, Seraphis, Vorago, Innexus, ...) is RED.
#      - modified tracked headers under dsp/tests/unit/<layer>/ are reported with their layer executable (they are not
#        in this lane's executable set; the main loop decides).
#   2. One build of Profundum profundum_tests dsp_systems_tests (-- /nr:false) to build.log; warning count
#      (warning C[0-9]|warning:) must be 0 (SC-021). Wall clock recorded.
#   3. Concurrently, each to its own log with its wall clock:
#      (a) profundum_tests.exe "~[long]~[performance]~[perf]~[benchmark]~[!benchmark]" -> "All tests passed";
#      (b) dsp_systems_tests.exe with the same filter (the consumer's per-push set, FR-095, SC-022) -> "All tests passed";
#      (c) the [long] cases of the phase's own TUs: profundum_tests.exe --list-tests "[profundum][long]", kept when the
#          name is a Phase 12 TEST_CASE name (none is [long] at cut time; Profundum_MainPageSanity joins if T031
#          re-tagged it) or is defined in a profundum test TU this phase amended (git diff 40f6ebca; at cut time
#          Profundum_StateRestoreRender, whose getState length assertion moved to kStateV3Bytes). Every other listed
#          name is reported, not run. One process per case through xargs -P min(8, cores - 4);
#      (d) dsp_systems_tests.exe --list-tests "[profundum][long]": the consumer [long] names are written to the status
#          file (46 expected at 40f6ebca) and NOT run (Deviation 6, ruled);
#      (e) --list-tests shows every SC case name of the spec; "[.perf]" --list-tests shows both CPU cases (and they are
#          absent from the default listing); dsp_systems_tests --list-tests shows Profundum_EngineDisplayAccessorNeutral;
#      (f) tools/pluginval.exe --strictness-level 5 --validate Profundum.vst3 -> exit 0 (SC-020, editor open / close);
#      (g) bash specs/profundum-phase12-ui/evidence/close/regen-tables.sh (T033): exit 0; "TABLES CHANGED" is a review
#          item for the user, not a red.
#   4. Summary block with every lane's verdict and wall clock.
# Status file evidence/run-log/close/status.txt: one line per lane: name, exit code, wall clock (s).
# Exit code: 0 only if every lane is green.
#
# A timing-budget assertion of an untouched case that goes red under the concurrent lanes is recorded as load, not
# re-run. A Phase 12 red is owned: fix it, re-run only the affected case or lane.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase12-ui"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
BUILD_DIR="build/windows-x64-release"
BIN="$BUILD_DIR/bin/Release"
EXE="profundum_tests"
SYS_EXE="dsp_systems_tests"
PROFUNDUM_BUNDLE="$BUILD_DIR/VST3/Release/Profundum.vst3"
PERPUSH_FILTER="~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"
LONG_FILTER="[profundum][long]"
BASE_COMMIT="40f6ebca"
FENCE_PATHS="dsp/include dsp/src plugins/shared tests/test_helpers"
FENCE_EXPECTED="dsp/include/krate/dsp/systems/profundum_engine.h
dsp/include/krate/dsp/systems/profundum_voice.h"
CONSUMER_LONG_EXPECTED=46
LOGDIR="$SPEC/evidence/run-log/close"
LONGDIR="$LOGDIR/long"
STATUS="$LOGDIR/status.txt"
REGEN="$SPEC/evidence/close/regen-tables.sh"

# Spec SC-002..SC-032 TEST_CASE names in profundum_tests (non-hidden), plus the tasks-level extra cases (tasks.md
# deviations 2, 3). SC-004 names the four Phase 10 / 11 cases that must pass with no edit to their v1 / v2 expectations.
CASES="Profundum_UidescBindsEverySurfaceId
Profundum_UidescTagIntegrity
Profundum_UiClassRule
Profundum_ParamTableConformance
Profundum_ParamDisplayFormats
Profundum_StateVersions
Profundum_StateRoundTripV2
Profundum_UiLabels
Profundum_MainPageSanity
Profundum_MorphComponentUi
Profundum_MorphLockDimming
Profundum_MacroHighlight
Profundum_SpectrumFrameMatchesSignal
Profundum_SpectrumFrameDoesNotChangeAudio
Profundum_SpectrumFrameAllocationFree
Profundum_SpreadCurveMatchesField
Profundum_EditorSyncTimer
Profundum_EditorLifecycle
Profundum_Editor_PageSwitch
Profundum_UiPortamentoUnitFollowsGlideMode
Profundum_UiLayout
Profundum_SpectrumFrameHandlerLifecycle
Profundum_UiCustomViewLaws
Profundum_SpectrumFrameFocusVoice
Profundum_SpectrumFrameSanitise
Profundum_EndpointNames
Profundum_MacroModulationRing
Profundum_MidiLearnHostLevel
Profundum_SpectrumFrameCadence
Profundum_UiPlacement
Profundum_UiZoom
Profundum_SpectrumProducerMath
Profundum_UiViewsUnit
Profundum_MacroUiMap"
# SC-013: the two CPU cases, hidden behind [.perf].
HIDDEN_CASES="Profundum_ProcessorCpu
Profundum_ProcessorCpuFullSurface"
# SC-022: the accessor's bit-identity case in the consumer executable.
SYS_CASE="Profundum_EngineDisplayAccessorNeutral"

CORES="$(nproc 2>/dev/null || echo 8)"

mkdir -p "$LOGDIR" "$LONGDIR"
rm -f "$LONGDIR"/*.log "$LONGDIR"/*.txt

now() { date -u +%Y-%m-%dT%H:%M:%SZ; }
status_line() { echo "$*" >> "$STATUS"; }

suite_start="$(date +%s)"
echo "START $(now) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown) base $BASE_COMMIT cores $CORES" > "$STATUS"

# ---------------------------------------------------------------------------------------------------------------
# 1. Change-set scope.
# ---------------------------------------------------------------------------------------------------------------
SCOPE_LOG="$LOGDIR/scope-diff.log"
CONSUMERS_TXT="$LOGDIR/scope-consumers.txt"
# shellcheck disable=SC2086
{
    echo "=== git diff --name-only $BASE_COMMIT -- $FENCE_PATHS (must print exactly the two profundum_{engine,voice}.h)"
    git diff --name-only "$BASE_COMMIT" -- $FENCE_PATHS
    echo "=== git ls-files --others --exclude-standard -- $FENCE_PATHS (new files; reported)"
    git ls-files --others --exclude-standard -- $FENCE_PATHS
    echo "=== git diff --name-only $BASE_COMMIT -- dsp/tests (modified dsp test files; reported)"
    git diff --name-only "$BASE_COMMIT" -- dsp/tests
} > "$SCOPE_LOG" 2>&1

# shellcheck disable=SC2086
fence_changed="$(git diff --name-only "$BASE_COMMIT" -- $FENCE_PATHS 2>/dev/null | tr -d '\r' | grep -v '^$' | sort -u)"
fence_expected_sorted="$(printf '%s\n' "$FENCE_EXPECTED" | sort -u)"
# shellcheck disable=SC2086
fence_new="$(git ls-files --others --exclude-standard -- $FENCE_PATHS 2>/dev/null | tr -d '\r' | grep -v '^$' | sort -u | tr '\n' ' ')"
if [ "$fence_changed" != "$fence_expected_sorted" ]; then
    while IFS= read -r p; do
        [ -z "$p" ] && continue
        printf '%s\n' "$FENCE_EXPECTED" | grep -qxF "$p" || status_line "scope RED unexpected-modified path=$p"
    done <<< "$fence_changed"
    while IFS= read -r p; do
        [ -z "$p" ] && continue
        printf '%s\n' "$fence_changed" | grep -qxF "$p" || status_line "scope RED expected-but-unmodified path=$p"
    done <<< "$FENCE_EXPECTED"
    status_line "scope RED base=$BASE_COMMIT log=$SCOPE_LOG"
    status_line "DONE $(now) RED: change set is not exactly the two profundum_{engine,voice}.h headers; no build, no lane ran"
    tr -d '\r' < "$STATUS" | tail -n 12
    exit 1
fi

grep -rlE "profundum_engine\.h|profundum_voice\.h" dsp plugins tests 2>/dev/null | tr -d '\r' | sort -u > "$CONSUMERS_TXT"
consumer_count="$(grep -c . "$CONSUMERS_TXT" || true)"
consumers_foreign=""
while IFS= read -r p; do
    [ -z "$p" ] && continue
    case "$p" in
        dsp/CMakeLists.txt|dsp/include/krate/dsp/systems/*|dsp/tests/unit/systems/*|plugins/profundum/*) ;;
        *) consumers_foreign="$consumers_foreign $p" ;;
    esac
done < "$CONSUMERS_TXT"
while IFS= read -r p; do
    [ -z "$p" ] && continue
    status_line "scope consumer $p"
done < "$CONSUMERS_TXT"
if [ -n "$consumers_foreign" ]; then
    status_line "scope RED consumers outside dsp/systems and plugins/profundum:${consumers_foreign} (their per-push suites are not in this lane)"
    status_line "DONE $(now) RED: unexpected consumer of the modified headers; no build, no lane ran"
    tr -d '\r' < "$STATUS" | tail -n 12
    exit 1
fi

# Modified dsp test helpers / TUs: reported with the layer executable that compiles them (not run by this lane).
dsp_tests_modified="$(git diff --name-only "$BASE_COMMIT" -- dsp/tests 2>/dev/null | tr -d '\r' | grep -v '^$' | sort -u)"
outside_lane_layers=""
while IFS= read -r p; do
    [ -z "$p" ] && continue
    layer="$(printf '%s' "$p" | sed -nE 's#^dsp/tests/unit/([a-z]+)/.*#\1#p')"
    if [ -n "$layer" ]; then
        status_line "scope dsp-test-modified path=$p exe=dsp_${layer}_tests"
        [ "$layer" = "systems" ] || outside_lane_layers="$outside_lane_layers dsp_${layer}_tests"
    else
        status_line "scope dsp-test-modified path=$p"
    fi
done <<< "$dsp_tests_modified"
# shellcheck disable=SC2086
outside_lane_layers="$(printf '%s\n' $outside_lane_layers | grep -v '^$' | sort -u | tr '\n' ' ')"
outside_lane_layers="${outside_lane_layers% }"
status_line "scope GREEN base=$BASE_COMMIT fence=[$(printf '%s' "$fence_changed" | tr '\n' ' ')] new_under_fence=[${fence_new% }] consumers=$consumer_count foreign=[] modified_helpers_outside_lane=[$outside_lane_layers] log=$SCOPE_LOG"
if [ -n "$outside_lane_layers" ]; then
    status_line "scope NOTE modified dsp test files are compiled by [$outside_lane_layers]: rebuilt to 0 warnings, suite not run (FR-095 2026-10-24)"
fi

# ---------------------------------------------------------------------------------------------------------------
# 2. One build.
# ---------------------------------------------------------------------------------------------------------------
# FR-095 (2026-10-24): every executable that compiles a modified dsp test helper is rebuilt to the zero-warning bar.
BUILD_TARGETS="Profundum $EXE $SYS_EXE $outside_lane_layers"
BUILD_LOG="$LOGDIR/build.log"
t0="$(date +%s)"
# shellcheck disable=SC2086
"$CMAKE" --build "$BUILD_DIR" --config Release --target $BUILD_TARGETS -- /nr:false > "$BUILD_LOG" 2>&1
build_status=$?
t1="$(date +%s)"
build_wall=$((t1 - t0))
warnings="$(grep -cE "warning C[0-9]|warning:" "$BUILD_LOG" || true)"
warnings="${warnings:-0}"
compile_errors="$(grep -cE "error C[0-9]|error LNK[0-9]|fatal error" "$BUILD_LOG" || true)"
compile_errors="${compile_errors:-0}"
# The post-build copy to C:/Program Files/Common Files/VST3/ may fail on permissions (root CLAUDE.md), so a nonzero
# exit alone does not stop the lanes; a compile or link error does (the exes on disk would be stale).
status_line "build targets=[$BUILD_TARGETS] exit=$build_status wall=${build_wall}s warnings=$warnings compile_errors=$compile_errors log=$BUILD_LOG"
missing_art=""
for f in "$BIN/$EXE.exe" "$BIN/$SYS_EXE.exe"; do
    [ -f "$f" ] || missing_art="$missing_art $f"
done
[ -d "$PROFUNDUM_BUNDLE" ] || missing_art="$missing_art $PROFUNDUM_BUNDLE"
if [ "$compile_errors" -ne 0 ] || [ -n "$missing_art" ]; then
    status_line "DONE $(now) RED: compile/link errors=$compile_errors missing=[${missing_art# }]; no test lane ran (see $BUILD_LOG)"
    tail -n 30 "$BUILD_LOG"
    exit 1
fi

# ---------------------------------------------------------------------------------------------------------------
# 3a / 3b. Per-push lanes.
# ---------------------------------------------------------------------------------------------------------------
# run_suite <lane-name> <exe> <filter>
run_suite() {
    local name="$1" exe="$2" filter="$3" s0 s1 rc
    local log="$LOGDIR/perpush-$1.log"
    s0="$(date +%s)"
    "$BIN/$exe.exe" "$filter" -D 15 > "$log" 2>&1
    rc=$?
    s1="$(date +%s)"
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    status_line "perpush:$name exit=$rc wall=$((s1 - s0))s log=$log"
}

# ---------------------------------------------------------------------------------------------------------------
# 3c. The [long] cases of the phase's own TUs (Phase 12 names + cases of the amended profundum test TUs).
# ---------------------------------------------------------------------------------------------------------------
LIST_RAW="$LONGDIR/list-raw.txt"
LONG_LISTED="$LONGDIR/long-listed.txt"
LONG_RUN="$LONGDIR/long-run.txt"
AMENDED_LONG="$LONGDIR/amended-tu-long-names.txt"
"$BIN/$EXE.exe" --list-tests "$LONG_FILTER" --verbosity quiet > "$LIST_RAW" 2>&1
tr -d '\r' < "$LIST_RAW" | sed 's/^[[:space:]]*//' | grep -v '^$' > "$LONG_LISTED"

# TEST_CASE names tagged [long] inside the profundum test TUs this phase modified or added since the base commit.
: > "$AMENDED_LONG"
{
    git diff --name-only "$BASE_COMMIT" -- plugins/profundum/tests
    git ls-files --others --exclude-standard -- plugins/profundum/tests
} 2>/dev/null | tr -d '\r' | grep -E '\.cpp$' | sort -u | while IFS= read -r tu; do
    [ -f "$tu" ] || continue
    tr -d '\r' < "$tu" | grep -E '^[[:space:]]*TEST_CASE\("[^"]+",[[:space:]]*"[^"]*\[long\]' \
        | sed -nE 's/^[[:space:]]*TEST_CASE\("([^"]+)".*/\1/p' >> "$AMENDED_LONG"
done

: > "$LONG_RUN"
long_foreign=""
while IFS= read -r n; do
    [ -z "$n" ] && continue
    if printf '%s\n' "$CASES" | grep -qxF "$n" || grep -qxF "$n" "$AMENDED_LONG"; then
        echo "$n" >> "$LONG_RUN"
    else
        long_foreign="$long_foreign $n"
    fi
done < "$LONG_LISTED"
# A [long] name of an amended TU that the executable does not list is a registration defect.
long_missing=""
while IFS= read -r n; do
    [ -z "$n" ] && continue
    grep -qxF "$n" "$LONG_LISTED" || long_missing="$long_missing $n"
done < "$AMENDED_LONG"
long_count="$(grep -c . "$LONG_RUN" || true)"
long_count="${long_count:-0}"
LONG_JOBS=$(( CORES - 4 ))
[ "$LONG_JOBS" -gt 8 ] && LONG_JOBS=8
[ "$LONG_JOBS" -lt 1 ] && LONG_JOBS=1
status_line "long-list run=$long_count [$(tr '\n' ' ' < "$LONG_RUN" | sed 's/ $//')] missing=[${long_missing# }] foreign_not_run=[${long_foreign# }] jobs=$LONG_JOBS list=$LONG_RUN"

run_long_shard() {
    local name="$1" safe spec log s0 s1 rc
    safe="$(printf '%s' "$name" | tr -c 'A-Za-z0-9_.-' '_' | cut -c1-120)"
    log="$LONGDIR/${safe}.log"
    # Catch2 test-spec escaping: backslash, comma, brackets and '*' are special.
    spec="$(printf '%s' "$name" | sed -e 's/\\/\\\\/g' -e 's/,/\\,/g' -e 's/\[/\\[/g' -e 's/\]/\\]/g' -e 's/\*/\\*/g')"
    s0="$(date +%s)"
    "$BIN/$EXE.exe" "$spec" -d yes > "$log" 2>&1
    rc=$?
    s1="$(date +%s)"
    if grep -qi "no test cases matched" "$log"; then
        rc=99
    fi
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    echo "long:$name exit=$rc wall=$((s1 - s0))s log=$log" >> "$STATUS"
    return 0
}
export -f run_long_shard
export BIN EXE LONGDIR STATUS

# ---------------------------------------------------------------------------------------------------------------
# 3d. Consumer [long] names of dsp_systems_tests: listed, not run (Deviation 6, ruled).
# ---------------------------------------------------------------------------------------------------------------
run_consumer_long_listing() {
    local s0 s1 rc=0 n count
    local list="$LOGDIR/consumer-long-listed.txt"
    s0="$(date +%s)"
    "$BIN/$SYS_EXE.exe" --list-tests "$LONG_FILTER" --verbosity quiet 2>&1 | tr -d '\r' | sed 's/^[[:space:]]*//' \
        | grep -v '^$' > "$list"
    count="$(grep -c . "$list" || true)"
    count="${count:-0}"
    [ "$count" -eq 0 ] && rc=1
    s1="$(date +%s)"
    while IFS= read -r n; do
        [ -z "$n" ] && continue
        status_line "consumer-long-listed (not run, Deviation 6): $n"
    done < "$list"
    status_line "consumer-long exit=$rc wall=$((s1 - s0))s listed=$count expected=$CONSUMER_LONG_EXPECTED run=0 list=$list"
}

# ---------------------------------------------------------------------------------------------------------------
# 3e. Case listing.
# ---------------------------------------------------------------------------------------------------------------
run_listing() {
    local s0 s1 rc=0 missing="" leaked="" n
    local log="$LOGDIR/list-tests.log"
    local visible="$LOGDIR/list-tests.visible.txt"
    local hidden="$LOGDIR/list-tests.hidden.txt"
    local sys="$LOGDIR/list-tests.dsp_systems.txt"
    s0="$(date +%s)"
    "$BIN/$EXE.exe" --list-tests --verbosity quiet 2>&1 | tr -d '\r' | sed 's/^[[:space:]]*//' > "$visible"
    "$BIN/$EXE.exe" "[.perf]" --list-tests --verbosity quiet 2>&1 | tr -d '\r' | sed 's/^[[:space:]]*//' > "$hidden"
    "$BIN/$SYS_EXE.exe" "[profundum]" --list-tests --verbosity quiet 2>&1 | tr -d '\r' | sed 's/^[[:space:]]*//' > "$sys"
    while IFS= read -r n; do
        [ -z "$n" ] && continue
        grep -qxF "$n" "$visible" || missing="$missing $n"
    done <<< "$CASES"
    while IFS= read -r n; do
        [ -z "$n" ] && continue
        grep -qxF "$n" "$hidden" || missing="$missing $n(hidden)"
        # A hidden case must not be in the default listing.
        if grep -qxF "$n" "$visible"; then
            leaked="$leaked $n"
        fi
    done <<< "$HIDDEN_CASES"
    grep -qxF "$SYS_CASE" "$sys" || missing="$missing $SYS_CASE($SYS_EXE)"
    if [ -n "$missing" ] || [ -n "$leaked" ]; then
        rc=1
    fi
    s1="$(date +%s)"
    {
        echo "=== $EXE.exe --list-tests --verbosity quiet"
        cat "$visible"
        echo "=== $EXE.exe \"[.perf]\" --list-tests --verbosity quiet"
        cat "$hidden"
        echo "=== $SYS_EXE.exe \"[profundum]\" --list-tests --verbosity quiet"
        cat "$sys"
        echo "missing=[${missing# }] leaked_hidden=[${leaked# }]"
    } > "$log"
    status_line "list-tests exit=$rc wall=$((s1 - s0))s missing=[${missing# }] leaked_hidden=[${leaked# }] log=$log"
}

# ---------------------------------------------------------------------------------------------------------------
# 3f. pluginval strictness 5 (SC-020).
# ---------------------------------------------------------------------------------------------------------------
run_pluginval() {
    local s0 s1 rc
    local log="$LOGDIR/pluginval-Profundum.log"
    s0="$(date +%s)"
    tools/pluginval.exe --strictness-level 5 --validate "$PROFUNDUM_BUNDLE" > "$log" 2>&1
    rc=$?
    s1="$(date +%s)"
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    status_line "pluginval:Profundum exit=$rc wall=$((s1 - s0))s log=$log"
}

# ---------------------------------------------------------------------------------------------------------------
# 3g. D3 / D4 table regeneration (T033).
# ---------------------------------------------------------------------------------------------------------------
run_regen() {
    local s0 s1 rc last
    local log="$LOGDIR/regen-tables.log"
    s0="$(date +%s)"
    bash "$REGEN" > "$log" 2>&1
    rc=$?
    s1="$(date +%s)"
    last="$(tr -d '\r\000' < "$log" | grep -v '^[[:space:]]*$' | tail -n 1)"  # NULs in the log made grep say "Binary file"
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    status_line "regen-tables exit=$rc wall=$((s1 - s0))s last=\"$last\" log=$log"
}

lane_start="$(date +%s)"
pids=""
# The [long] lane starts first (longest per-process wall clock).
(
    l0="$(date +%s)"
    if [ "$long_count" -gt 0 ]; then
        tr '\n' '\0' < "$LONG_RUN" | xargs -0 -n 1 -P "$LONG_JOBS" bash -c 'run_long_shard "$1"' _
    fi
    l1="$(date +%s)"
    echo "long-lane shards=$long_count wall=$((l1 - l0))s" >> "$STATUS"
) &
pids="$pids $!"
run_suite "$EXE" "$EXE" "$PERPUSH_FILTER" &
pids="$pids $!"
run_suite "$SYS_EXE" "$SYS_EXE" "$PERPUSH_FILTER" &
pids="$pids $!"
run_consumer_long_listing &
pids="$pids $!"
run_listing &
pids="$pids $!"
run_pluginval &
pids="$pids $!"
run_regen &
pids="$pids $!"

for p in $pids; do
    wait "$p"
done
lane_end="$(date +%s)"

# ---------------------------------------------------------------------------------------------------------------
# 4. Summary.
# ---------------------------------------------------------------------------------------------------------------
st() { tr -d '\r' < "$STATUS"; }
lane_exit() { st | grep -E "^$1 exit=" | tail -n 1 | sed -nE 's/.* exit=([0-9]+).*/\1/p'; }
lane_wall() { st | grep -E "^$1 exit=" | tail -n 1 | sed -nE 's/.* wall=([0-9]+s).*/\1/p'; }
verdict_of() { [ "${1:-1}" = "0" ] && echo GREEN || echo RED; }

perpush_last() {
    tr -d '\r' < "$LOGDIR/perpush-$1.log" 2>/dev/null | grep -v '^exit: ' | grep -v '^[[:space:]]*$' | tail -n 1
}
# perpush_ok <lane-name>: prints 0 when exit 0 and the last non-empty line reads "All tests passed".
perpush_ok() {
    local last rc
    last="$(perpush_last "$1")"
    rc="$(lane_exit "perpush:$1")"
    case "$last" in
        "All tests passed"*)
            if [ "${rc:-1}" = "0" ]; then
                echo 0
                return
            fi
            ;;
    esac
    echo 1
}

build_ok=1
[ "$warnings" -eq 0 ] && build_ok=0

pp_prof="$(perpush_ok "$EXE")"
pp_sys="$(perpush_ok "$SYS_EXE")"

long_red="$(st | grep -E '^long:' | grep -vE ' exit=0 ' | sed -nE 's/^long:([^ ]+) exit=.*/\1/p' | tr '\n' ' ')"
long_done="$(st | grep -cE '^long:' || true)"
long_ok=0
if [ -n "$long_red" ] || [ -n "$long_missing" ] || [ "${long_done:-0}" -ne "$long_count" ]; then
    long_ok=1
fi

consumer_rc="$(lane_exit consumer-long)"
consumer_listed="$(st | grep -E '^consumer-long exit=' | tail -n 1 | sed -nE 's/.* listed=([0-9]+).*/\1/p')"
list_rc="$(lane_exit list-tests)"
pv_rc="$(lane_exit pluginval:Profundum)"
regen_rc="$(lane_exit regen-tables)"
regen_last="$(st | grep -E '^regen-tables exit=' | tail -n 1 | sed -nE 's/.* last="([^"]*)".*/\1/p')"
regen_ok=1
case "$regen_last" in
    "TABLES IDENTICAL"|"TABLES CHANGED") [ "${regen_rc:-1}" = "0" ] && regen_ok=0 ;;
esac

overall=GREEN
for v in "$build_ok" "$pp_prof" "$pp_sys" "$long_ok" "${consumer_rc:-1}" "${list_rc:-1}" "${pv_rc:-1}" "$regen_ok"; do
    [ "$v" = "0" ] || overall=RED
done

suite_end="$(date +%s)"
{
    echo "=== summary"
    echo "scope (FR-095)                   GREEN base=$BASE_COMMIT consumers=$consumer_count modified_helpers_outside_lane=[$outside_lane_layers]"
    echo "build (SC-021)                   $(verdict_of "$build_ok") exit=$build_status warnings=$warnings compile_errors=$compile_errors wall=${build_wall}s"
    echo "perpush:$EXE            $(verdict_of "$pp_prof") last=\"$(perpush_last "$EXE")\" wall=$(lane_wall "perpush:$EXE")"
    echo "perpush:$SYS_EXE (SC-022) $(verdict_of "$pp_sys") last=\"$(perpush_last "$SYS_EXE")\" wall=$(lane_wall "perpush:$SYS_EXE")"
    echo "long (own + amended TUs)         $(verdict_of "$long_ok") run=$long_count done=${long_done:-0} red=[${long_red% }] missing=[${long_missing# }] foreign_not_run=[${long_foreign# }] $(st | grep -E '^long-lane ' | tail -n 1)"
    echo "consumer [long] (Deviation 6)    $(verdict_of "${consumer_rc:-1}") listed=${consumer_listed:-?} expected=$CONSUMER_LONG_EXPECTED run=0 wall=$(lane_wall consumer-long)"
    if [ -n "${consumer_listed:-}" ] && [ "$consumer_listed" != "$CONSUMER_LONG_EXPECTED" ]; then
        echo "  NOTE: consumer [long] count $consumer_listed differs from the $CONSUMER_LONG_EXPECTED cut-time count (report to the user)"
    fi
    echo "list-tests (SC names, SC-013)    $(verdict_of "${list_rc:-1}") wall=$(lane_wall list-tests)"
    echo "pluginval Profundum (SC-020)     $(verdict_of "${pv_rc:-1}") exit=${pv_rc:-?} wall=$(lane_wall pluginval:Profundum)"
    echo "regen-tables (T033)              $(verdict_of "$regen_ok") exit=${regen_rc:-?} last=\"$regen_last\" wall=$(lane_wall regen-tables)"
    if [ "$regen_last" = "TABLES CHANGED" ]; then
        echo "  REVIEW: D3 / D4 tables changed; a review item for the user, not an automatic accept"
    fi
    echo "lanes wall=$((lane_end - lane_start))s total wall=$((suite_end - suite_start))s"
} >> "$STATUS"
status_line "DONE $(now) $overall"
st | tail -n 16
[ "$overall" = "GREEN" ]
