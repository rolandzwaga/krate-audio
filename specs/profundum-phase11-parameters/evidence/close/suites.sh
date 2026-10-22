#!/usr/bin/env bash
# Profundum Phase 11, task T031 close lane: change-set scope, one build, the per-push lanes, the phase's own [long]
# lane, case listing and pluginval (SC-001 Windows, SC-002, SC-003, SC-011, SC-017 renders, SC-018, SC-019, SC-023).
# Structure copied from specs/profundum-phase10-plugin-scaffold/evidence/close/suites.sh, with the lane scope of
# 2026-10-18 (Phase 6 ruling G9-4) and SC-023: the executables are profundum_tests, shared_tests and the two
# sustain_test.cpp cases of vorago_tests (consumers of the promoted SustainLatch, FR-061; a pure code move, user
# ruling 2026-10-22: the full vorago_tests suite and its [long] lanes are NOT run). Never every [long] case of an
# executable: only the five Phase 11 [long] cases.
#
# Run from the main loop DETACHED (Git Bash, from the Bash tool), with tidy.sh concurrently:
#   nohup bash specs/profundum-phase11-parameters/evidence/close/suites.sh \
#       > specs/profundum-phase11-parameters/evidence/run-log/close/suites.out 2>&1 & disown
# Watch evidence/run-log/close/status.txt; its last line is "DONE ...".
#
# Steps:
#   1. Change-set scope:
#      - git diff --name-only 565f195d -- dsp/include dsp/src tests/test_helpers (and untracked files there) must print
#        nothing (FR-070). A path is RED and the stage stops: no build, no lane.
#      - git diff --name-only 565f195d -- plugins/shared (tracked, modified) is expected to be exactly
#        plugins/shared/CMakeLists.txt and plugins/shared/tests/CMakeLists.txt. If an existing plugins/shared/src file
#        is modified, its consumers are written to the status file and their per-push suites run too (expected set:
#        shared + Vorago only, i.e. no extra suite).
#   2. One build of Profundum Vorago profundum_tests shared_tests vorago_tests (-- /nr:false) to build.log; warning
#      count (warning C[0-9]|warning:) must be 0 (SC-001). Wall clock recorded.
#   3. Concurrently, each to its own log with its wall clock:
#      (a) profundum_tests.exe "~[long]~[performance]~[perf]~[benchmark]~[!benchmark]" -> "All tests passed" (SC-002);
#      (b) shared_tests.exe with the same filter, and vorago_tests.exe "Vorago_SustainLatchUnit,Vorago_SustainLatch"
#          (SC-002 / SC-023); plus the per-push suite of any extra consumer found in step 1;
#      (c) the [long] cases of the phase's own TUs: profundum_tests.exe --list-tests "[profundum][long]", keep exactly
#          the five Phase 11 names (Profundum_TailCoversRelease first, R3); a listed name outside that set is reported,
#          not run. One process per case through xargs -P min(5, cores - 4);
#      (d) --list-tests shows every SC case name of spec SC-005..SC-025 (SC-002) and "[.perf]" --list-tests shows
#          Profundum_ProcessorCpuFullSurface; shared_tests --list-tests shows SustainLatch_SharedSemantics;
#      (e) tools/pluginval.exe --strictness-level 5 on Profundum.vst3 and on Vorago.vst3, exit 0 (SC-003).
#   4. Summary block with every lane's verdict and wall clock.
# Status file evidence/run-log/close/status.txt: one line per lane: name, exit code, wall clock (s).
# Exit code: 0 only if every lane is green.
#
# R3 (plan): a non-zero sample in Profundum_TailCoversRelease (SC-017) goes to the user; the 1250 ms term is never
# re-fitted. A timing-budget assertion of an untouched case that goes red under the concurrent lanes is recorded as
# load, not re-run.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase11-parameters"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
BUILD_DIR="build/windows-x64-release"
BIN="$BUILD_DIR/bin/Release"
EXE="profundum_tests"
PROFUNDUM_BUNDLE="$BUILD_DIR/VST3/Release/Profundum.vst3"
VORAGO_BUNDLE="$BUILD_DIR/VST3/Release/Vorago.vst3"
PERPUSH_FILTER="~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"
LONG_FILTER="[profundum][long]"
VORAGO_FILTER="Vorago_SustainLatchUnit,Vorago_SustainLatch"
BASE_COMMIT="565f195d"
FENCE_PATHS="dsp/include dsp/src tests/test_helpers"
SHARED_EXPECTED="plugins/shared/CMakeLists.txt
plugins/shared/tests/CMakeLists.txt"
LOGDIR="$SPEC/evidence/run-log/close"
LONGDIR="$LOGDIR/long"
STATUS="$LOGDIR/status.txt"

# The Phase 11 [long] cases, in run order (R3: SC-017 first).
LONG_CASES="Profundum_TailCoversRelease
Profundum_ParamPathAudibility
Profundum_MassOffsetAudibility
Profundum_MonoSumViaParams
Profundum_StateRestoreRender"
LONG_EXPECTED=5

# Spec SC-005..SC-025 TEST_CASE names (non-hidden), plus the tasks-level split / extra cases (tasks.md deviations 2, 3).
CASES="Profundum_ParamTableConformance
Profundum_ParamDenormRoundTrip
Profundum_DefaultsMatchEngine
Profundum_EveryParamReachesEngine
Profundum_EditTargetAddressing
Profundum_StateRoundTripV2
Profundum_StateRestoreRender
Profundum_StateVersions
Profundum_CopyEndpoint
Profundum_CompareDoesNotWriteState
Profundum_MidiMapping
Profundum_PortamentoLabelFollowsGlideMode
Profundum_TailCoversRelease
Profundum_TailFormula
Profundum_ParamPathAudibility
Profundum_MonoSumViaParams
Profundum_Phase11PathsDoNotAllocate
Profundum_Determinism
Profundum_MassPerformanceOffset
Profundum_ProcessorControllerSync
Profundum_SoundParamTableRouting"
HIDDEN_CASE="Profundum_ProcessorCpuFullSurface"
SHARED_CASE="SustainLatch_SharedSemantics"

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
# shellcheck disable=SC2086
{
    echo "=== git diff --name-only $BASE_COMMIT -- $FENCE_PATHS (must print nothing, FR-070)"
    git diff --name-only "$BASE_COMMIT" -- $FENCE_PATHS
    echo "=== git ls-files --others --exclude-standard -- $FENCE_PATHS (must print nothing)"
    git ls-files --others --exclude-standard -- $FENCE_PATHS
    echo "=== git diff --name-only $BASE_COMMIT -- plugins/shared (expected: the two CMake lists only)"
    git diff --name-only "$BASE_COMMIT" -- plugins/shared
    echo "=== git ls-files --others --exclude-standard -- plugins/shared (new files; do not count)"
    git ls-files --others --exclude-standard -- plugins/shared
} > "$SCOPE_LOG" 2>&1

# shellcheck disable=SC2086
fence_changed="$( { git diff --name-only "$BASE_COMMIT" -- $FENCE_PATHS; git ls-files --others --exclude-standard -- $FENCE_PATHS; } 2>/dev/null | tr -d '\r' | grep -v '^$' | sort -u)"
if [ -n "$fence_changed" ]; then
    while IFS= read -r p; do
        [ -z "$p" ] && continue
        status_line "scope RED path=$p (FR-070: no dsp/ or tests/test_helpers edit; stop for a user ruling)"
    done <<< "$fence_changed"
    status_line "scope RED base=$BASE_COMMIT log=$SCOPE_LOG"
    status_line "DONE $(now) RED: FR-070 fence crossed; no build, no lane ran"
    tr -d '\r' < "$STATUS" | tail -n 12
    exit 1
fi

shared_modified="$(git diff --name-only "$BASE_COMMIT" -- plugins/shared 2>/dev/null | tr -d '\r' | grep -v '^$' | sort -u)"
shared_unexpected=""
EXTRA_SUITES=""
while IFS= read -r p; do
    [ -z "$p" ] && continue
    if printf '%s\n' "$SHARED_EXPECTED" | grep -qxF "$p"; then
        continue
    fi
    shared_unexpected="$shared_unexpected $p"
    case "$p" in
        plugins/shared/src/*)
            # Direct includers of the modified file: their plugins' per-push suites run (step 3b).
            inc="${p#plugins/shared/src/}"
            consumers="$(grep -rlF "$inc" plugins --include='*.h' --include='*.cpp' 2>/dev/null \
                | grep -v '^plugins/shared/' | cut -d/ -f2 | sort -u | tr '\n' ' ')"
            status_line "scope shared-src-modified path=$p consumers=[${consumers% }] (their per-push suites run)"
            for c in $consumers; do
                case "$c" in
                    profundum|vorago) ;;   # already in the lane (vorago: the moved class's own cases)
                    iterum) EXTRA_SUITES="$EXTRA_SUITES plugin_tests approval_tests" ;;
                    *)      EXTRA_SUITES="$EXTRA_SUITES ${c}_tests" ;;
                esac
            done
            ;;
        *)
            status_line "scope shared-unexpected path=$p (not a src file; reported)"
            ;;
    esac
done <<< "$shared_modified"
# shellcheck disable=SC2086
EXTRA_SUITES="$(printf '%s\n' $EXTRA_SUITES | grep -v '^$' | sort -u | tr '\n' ' ')"
EXTRA_SUITES="${EXTRA_SUITES% }"
status_line "scope GREEN base=$BASE_COMMIT fence=clean shared_unexpected=[${shared_unexpected# }] extra_suites=[$EXTRA_SUITES] log=$SCOPE_LOG"

# ---------------------------------------------------------------------------------------------------------------
# 2. One build.
# ---------------------------------------------------------------------------------------------------------------
BUILD_TARGETS="Profundum Vorago profundum_tests shared_tests vorago_tests $EXTRA_SUITES"
BUILD_TARGETS="${BUILD_TARGETS% }"
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
for f in "$BIN/$EXE.exe" "$BIN/shared_tests.exe" "$BIN/vorago_tests.exe"; do
    [ -f "$f" ] || missing_art="$missing_art $f"
done
for s in $EXTRA_SUITES; do
    [ -f "$BIN/$s.exe" ] || missing_art="$missing_art $BIN/$s.exe"
done
for d in "$PROFUNDUM_BUNDLE" "$VORAGO_BUNDLE"; do
    [ -d "$d" ] || missing_art="$missing_art $d"
done
if [ "$compile_errors" -ne 0 ] || [ -n "$missing_art" ]; then
    status_line "DONE $(now) RED: compile/link errors=$compile_errors missing=[${missing_art# }]; no test lane ran (see $BUILD_LOG)"
    tail -n 30 "$BUILD_LOG"
    exit 1
fi

# ---------------------------------------------------------------------------------------------------------------
# 3a / 3b. Per-push lanes: profundum_tests, shared_tests, the two Vorago cases, any extra consumer suite.
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
# 3c. The phase's own [long] cases.
# ---------------------------------------------------------------------------------------------------------------
LIST_RAW="$LONGDIR/list-raw.txt"
LONG_LISTED="$LONGDIR/long-listed.txt"
LONG_RUN="$LONGDIR/long-run.txt"
"$BIN/$EXE.exe" --list-tests "$LONG_FILTER" --verbosity quiet > "$LIST_RAW" 2>&1
tr -d '\r' < "$LIST_RAW" | sed 's/^[[:space:]]*//' | grep -v '^$' > "$LONG_LISTED"
# Keep the Phase 11 names in LONG_CASES order (TailCoversRelease first); report the rest.
: > "$LONG_RUN"
long_missing=""
while IFS= read -r n; do
    [ -z "$n" ] && continue
    if grep -qxF "$n" "$LONG_LISTED"; then
        echo "$n" >> "$LONG_RUN"
    else
        long_missing="$long_missing $n"
    fi
done <<< "$LONG_CASES"
long_foreign=""
while IFS= read -r n; do
    [ -z "$n" ] && continue
    printf '%s\n' "$LONG_CASES" | grep -qxF "$n" || long_foreign="$long_foreign $n"
done < "$LONG_LISTED"
long_count="$(grep -c . "$LONG_RUN" || true)"
long_count="${long_count:-0}"
LONG_JOBS=$(( CORES - 4 ))
[ "$LONG_JOBS" -gt "$LONG_EXPECTED" ] && LONG_JOBS="$LONG_EXPECTED"
[ "$LONG_JOBS" -lt 1 ] && LONG_JOBS=1
status_line "long-list run=$long_count ($LONG_EXPECTED expected) missing=[${long_missing# }] foreign_not_run=[${long_foreign# }] jobs=$LONG_JOBS list=$LONG_RUN"

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
# 3d. Case listing (SC-002).
# ---------------------------------------------------------------------------------------------------------------
run_listing() {
    local s0 s1 rc=0 missing="" leaked="" n
    local log="$LOGDIR/list-tests.log"
    local visible="$LOGDIR/list-tests.visible.txt"
    local hidden="$LOGDIR/list-tests.hidden.txt"
    local shared="$LOGDIR/list-tests.shared.txt"
    s0="$(date +%s)"
    "$BIN/$EXE.exe" --list-tests --verbosity quiet 2>&1 | tr -d '\r' | sed 's/^[[:space:]]*//' > "$visible"
    "$BIN/$EXE.exe" "[.perf]" --list-tests --verbosity quiet 2>&1 | tr -d '\r' | sed 's/^[[:space:]]*//' > "$hidden"
    "$BIN/shared_tests.exe" --list-tests --verbosity quiet 2>&1 | tr -d '\r' | sed 's/^[[:space:]]*//' > "$shared"
    while IFS= read -r n; do
        [ -z "$n" ] && continue
        grep -qxF "$n" "$visible" || missing="$missing $n"
    done <<< "$CASES"
    grep -qxF "$HIDDEN_CASE" "$hidden" || missing="$missing $HIDDEN_CASE(hidden)"
    grep -qxF "$SHARED_CASE" "$shared" || missing="$missing $SHARED_CASE(shared_tests)"
    # The hidden case must not be in the default listing.
    if grep -qxF "$HIDDEN_CASE" "$visible"; then
        leaked="$HIDDEN_CASE"
    fi
    if [ -n "$missing" ] || [ -n "$leaked" ]; then
        rc=1
    fi
    s1="$(date +%s)"
    {
        echo "=== $EXE.exe --list-tests --verbosity quiet"
        cat "$visible"
        echo "=== $EXE.exe \"[.perf]\" --list-tests --verbosity quiet"
        cat "$hidden"
        echo "=== shared_tests.exe --list-tests --verbosity quiet"
        cat "$shared"
        echo "missing=[${missing# }] leaked_hidden=[$leaked]"
    } > "$log"
    status_line "list-tests exit=$rc wall=$((s1 - s0))s missing=[${missing# }] leaked_hidden=[$leaked] log=$log"
}

# ---------------------------------------------------------------------------------------------------------------
# 3e. pluginval strictness 5 (SC-003), Profundum and Vorago.
# ---------------------------------------------------------------------------------------------------------------
run_pluginval() {
    local name="$1" bundle="$2" s0 s1 rc
    local log="$LOGDIR/pluginval-$1.log"
    s0="$(date +%s)"
    tools/pluginval.exe --strictness-level 5 --validate "$bundle" > "$log" 2>&1
    rc=$?
    s1="$(date +%s)"
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    status_line "pluginval:$name exit=$rc wall=$((s1 - s0))s log=$log"
}

lane_start="$(date +%s)"
pids=""
# The [long] lane starts first so Profundum_TailCoversRelease (R3) is the first process launched.
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
run_suite shared_tests shared_tests "$PERPUSH_FILTER" &
pids="$pids $!"
run_suite vorago_tests-sustain vorago_tests "$VORAGO_FILTER" &
pids="$pids $!"
for s in $EXTRA_SUITES; do
    run_suite "$s" "$s" "$PERPUSH_FILTER" &
    pids="$pids $!"
done
run_listing &
pids="$pids $!"
run_pluginval Profundum "$PROFUNDUM_BUNDLE" &
pids="$pids $!"
run_pluginval Vorago "$VORAGO_BUNDLE" &
pids="$pids $!"

for p in $pids; do
    wait "$p"
done
lane_end="$(date +%s)"

# ---------------------------------------------------------------------------------------------------------------
# 4. Summary.
# ---------------------------------------------------------------------------------------------------------------
st() { tr -d '\r' < "$STATUS"; }
lane_exit() { st | grep -F "$1 exit=" | grep -E "^$1 " | tail -n 1 | sed -nE 's/.* exit=([0-9]+).*/\1/p'; }
lane_wall() { st | grep -F "$1 exit=" | grep -E "^$1 " | tail -n 1 | sed -nE 's/.* wall=([0-9]+s).*/\1/p'; }
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
pp_shared="$(perpush_ok shared_tests)"
pp_vorago="$(perpush_ok vorago_tests-sustain)"
pp_extra=0
for s in $EXTRA_SUITES; do
    [ "$(perpush_ok "$s")" = "0" ] || pp_extra=1
done

long_red="$(st | grep -E '^long:' | grep -vE ' exit=0 ' | sed -nE 's/^long:([^ ]+) exit=.*/\1/p' | tr '\n' ' ')"
long_ok=0
if [ -n "$long_red" ] || [ -n "$long_missing" ] || [ "$long_count" -ne "$LONG_EXPECTED" ]; then
    long_ok=1
fi
tail_rc="$(st | grep -E '^long:Profundum_TailCoversRelease exit=' | tail -n 1 | sed -nE 's/.* exit=([0-9]+).*/\1/p')"

list_rc="$(lane_exit list-tests)"
pv_prof_rc="$(lane_exit pluginval:Profundum)"
pv_vor_rc="$(lane_exit pluginval:Vorago)"

overall=GREEN
for v in "$build_ok" "$pp_prof" "$pp_shared" "$pp_vorago" "$pp_extra" "$long_ok" "${list_rc:-1}" "${pv_prof_rc:-1}" "${pv_vor_rc:-1}"; do
    [ "$v" = "0" ] || overall=RED
done

suite_end="$(date +%s)"
{
    echo "=== summary"
    echo "scope (FR-070, SC-023)           GREEN base=$BASE_COMMIT shared_unexpected=[${shared_unexpected# }] extra_suites=[$EXTRA_SUITES]"
    echo "build (SC-001)                   $(verdict_of "$build_ok") exit=$build_status warnings=$warnings compile_errors=$compile_errors wall=${build_wall}s"
    echo "perpush:$EXE (SC-002)     $(verdict_of "$pp_prof") last=\"$(perpush_last "$EXE")\" wall=$(lane_wall "perpush:$EXE")"
    echo "perpush:shared_tests (SC-002)    $(verdict_of "$pp_shared") last=\"$(perpush_last shared_tests)\" wall=$(lane_wall perpush:shared_tests)"
    echo "perpush:vorago sustain (SC-023)  $(verdict_of "$pp_vorago") last=\"$(perpush_last vorago_tests-sustain)\" wall=$(lane_wall perpush:vorago_tests-sustain)"
    for s in $EXTRA_SUITES; do
        echo "perpush:$s (shared consumer) $(verdict_of "$(perpush_ok "$s")") last=\"$(perpush_last "$s")\" wall=$(lane_wall "perpush:$s")"
    done
    echo "long (SC-011/017/018/019)        $(verdict_of "$long_ok") run=$long_count/$LONG_EXPECTED red=[${long_red% }] missing=[${long_missing# }] foreign_not_run=[${long_foreign# }] $(st | grep -E '^long-lane ' | tail -n 1)"
    if [ "${tail_rc:-1}" != "0" ]; then
        echo "  R3: Profundum_TailCoversRelease is not green -> report the reading to the user; never re-fit the 1250 ms term"
    fi
    echo "list-tests (SC-002)              $(verdict_of "${list_rc:-1}") wall=$(lane_wall list-tests)"
    echo "pluginval Profundum (SC-003)     $(verdict_of "${pv_prof_rc:-1}") exit=${pv_prof_rc:-?} wall=$(lane_wall pluginval:Profundum)"
    echo "pluginval Vorago (SC-003)        $(verdict_of "${pv_vor_rc:-1}") exit=${pv_vor_rc:-?} wall=$(lane_wall pluginval:Vorago)"
    echo "lanes wall=$((lane_end - lane_start))s total wall=$((suite_end - suite_start))s"
} >> "$STATUS"
status_line "DONE $(now) $overall"
st | tail -n 16
[ "$overall" = "GREEN" ]
