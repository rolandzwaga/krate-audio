#!/usr/bin/env bash
# Profundum Phase 10, task T029 close lane: change-set scope, one build, per-push lane, the phase's own [long] lane,
# case listing, pluginval, check-bundle and the clean-tree check (SC-001 Windows, SC-002, SC-003, SC-020, SC-021).
# Structure copied from specs/profundum-phase9-voice-engine/evidence/close/suites.sh, with the lane scope of
# 2026-10-18 (Phase 6 ruling G9-4): the only executable is profundum_tests, every TU of it is a Phase 10 TU, and no
# shared header is modified (FR-090), so there is NO consumer suite.
#
# Run from the main loop DETACHED (Git Bash, from the Bash tool), with tidy.sh and asan.sh concurrently:
#   nohup bash specs/profundum-phase10-plugin-scaffold/evidence/close/suites.sh \
#       > specs/profundum-phase10-plugin-scaffold/evidence/run-log/close/suites.out 2>&1 & disown
# Watch evidence/run-log/close/status.txt; its last line is "DONE ...".
#
# Steps:
#   1. Change-set scope: git diff --name-only 285613c5 -- dsp/include dsp/src plugins/shared tests/test_helpers must
#      print nothing (and no untracked file may sit there). A path is RED (FR-090) and is written to the status file
#      with the consumer executables it would require; those consumers are NOT run: the stage stops for a user ruling.
#   2. One build of Profundum profundum_tests (-- /nr:false) to build.log; warning count must be 0 (SC-001).
#   3. Concurrently:
#      (a) per-push lane: profundum_tests.exe "~[long]~[performance]~[perf]~[benchmark]~[!benchmark]" -D 15
#          -> perpush-profundum_tests.log, last line "All tests passed" (SC-002.2); wall clock recorded (P-7);
#      (b) the [long] lane of the phase's own TUs (zero cases expected; any listed run one shard per case through
#          xargs -P min(shards, cores - 4)); never every [long] case of another executable;
#      (c) --list-tests holds the eight non-hidden FR-066 names; "[.perf]" --list-tests holds Profundum_ProcessorCpu
#          (SC-002.1);
#      (d) pluginval strictness 5 on Profundum.vst3, exit 0 (SC-003);
#      (e) node tools/check-bundle.js on Profundum.vst3 (SC-020);
#      (f) git status --porcelain plugins/profundum shows no generated file after the build (SC-021).
#   4. Summary block with every lane's verdict and wall clock.
# Status file evidence/run-log/close/status.txt: one line per lane: name, exit code, wall clock (s).
# Exit code: 0 only if every lane is green.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase10-plugin-scaffold"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
BUILD_DIR="build/windows-x64-release"
BIN="$BUILD_DIR/bin/Release"
EXE="profundum_tests"
BUNDLE="$BUILD_DIR/VST3/Release/Profundum.vst3"
PERPUSH_FILTER="~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"
LONG_FILTER="[profundum][long]~[performance]~[perf]~[benchmark]~[!benchmark]"
BASE_COMMIT="285613c5"
SCOPE_PATHS="dsp/include dsp/src plugins/shared tests/test_helpers"
LOGDIR="$SPEC/evidence/run-log/close"
LONGDIR="$LOGDIR/long"
STATUS="$LOGDIR/status.txt"
PERPUSH_LOG="$LOGDIR/perpush-$EXE.log"

# FR-066: the eight non-hidden TEST_CASE names, and the hidden one.
CASES="Profundum_ProcessorBusSetup
Profundum_ParamDenormRoundTrip
Profundum_StateRoundTrip
Profundum_MidiEventTranslation
Profundum_ProcessorLifecycle
Profundum_EditorLifecycle
Profundum_ProcessorRendersHeldNote
Profundum_ParamFlowReachesEngine"
HIDDEN_CASE="Profundum_ProcessorCpu"

# FR-007: the generated files, which must never show in git status (ignored by .gitignore).
GENERATED="plugins/profundum/src/version.h
plugins/profundum/resources/win32resource.rc
plugins/profundum/resources/auv3/audiounitconfig.h"

CORES="$(nproc 2>/dev/null || echo 8)"

mkdir -p "$LOGDIR" "$LONGDIR"
rm -f "$LONGDIR"/*.log "$LONGDIR"/*.txt

now() { date -u +%Y-%m-%dT%H:%M:%SZ; }
status_line() { echo "$*" >> "$STATUS"; }

suite_start="$(date +%s)"
echo "START $(now) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown) base $BASE_COMMIT cores $CORES" > "$STATUS"

# ---------------------------------------------------------------------------------------------------------------
# 1. Change-set scope (FR-090). Must print nothing.
# ---------------------------------------------------------------------------------------------------------------
# Consumer executables whose full suites a changed path would require (reported only, never run here).
consumers_for() {
    local p="$1" layers="" plugins="" inc out="" l pl
    case "$p" in
        dsp/include/krate/dsp/core/*)       layers="core primitives processors systems effects" ;;
        dsp/include/krate/dsp/primitives/*) layers="primitives processors systems effects" ;;
        dsp/include/krate/dsp/processors/*) layers="processors systems effects" ;;
        dsp/include/krate/dsp/systems/*)    layers="systems effects" ;;
        dsp/include/krate/dsp/effects/*)    layers="effects" ;;
        dsp/src/*|dsp/include/*|tests/test_helpers/*) layers="core primitives processors systems effects" ;;
    esac
    case "$p" in
        dsp/include/krate/dsp/*)
            inc="krate/dsp/${p#dsp/include/krate/dsp/}"
            # Direct includers only; a transitive consumer still needs a check by hand.
            plugins="$(grep -rlF "$inc" plugins 2>/dev/null | cut -d/ -f2 | sort -u | tr '\n' ' ')" ;;
        dsp/src/*|plugins/shared/*|tests/test_helpers/*)
            plugins="disrumpo gradus innexus iterum membrum profundum ruinae seraphis shared vorago" ;;
    esac
    for l in $layers; do out="$out dsp_${l}_tests"; done
    for pl in $plugins; do
        case "$pl" in
            iterum) out="$out plugin_tests approval_tests" ;;
            *)      out="$out ${pl}_tests" ;;
        esac
    done
    echo "${out# }"
}

SCOPE_LOG="$LOGDIR/scope-diff.log"
# shellcheck disable=SC2086
{
    echo "=== git diff --name-only $BASE_COMMIT -- $SCOPE_PATHS (must print nothing)"
    git diff --name-only "$BASE_COMMIT" -- $SCOPE_PATHS
    echo "=== git ls-files --others --exclude-standard -- $SCOPE_PATHS (must print nothing)"
    git ls-files --others --exclude-standard -- $SCOPE_PATHS
} > "$SCOPE_LOG" 2>&1
# shellcheck disable=SC2086
scope_changed="$( { git diff --name-only "$BASE_COMMIT" -- $SCOPE_PATHS; git ls-files --others --exclude-standard -- $SCOPE_PATHS; } 2>/dev/null | tr -d '\r' | grep -v '^$' | sort -u)"
scope_red=0
if [ -n "$scope_changed" ]; then
    scope_red=1
    while IFS= read -r p; do
        [ -z "$p" ] && continue
        status_line "scope RED path=$p requires_full_suites=[$(consumers_for "$p")] (not run: FR-090, stop for a user ruling)"
    done <<< "$scope_changed"
fi
status_line "scope $( [ "$scope_red" -eq 0 ] && echo GREEN || echo RED ) base=$BASE_COMMIT log=$SCOPE_LOG"

# SC-021 baseline: the untracked / modified set under plugins/profundum before the build.
CLEAN_PRE="$LOGDIR/clean-tree.pre.txt"
CLEAN_POST="$LOGDIR/clean-tree.post.txt"
git status --porcelain --untracked-files=all plugins/profundum 2>/dev/null | tr -d '\r' | sort -u > "$CLEAN_PRE"

# ---------------------------------------------------------------------------------------------------------------
# 2. One build of Profundum profundum_tests.
# ---------------------------------------------------------------------------------------------------------------
BUILD_LOG="$LOGDIR/build.log"
t0="$(date +%s)"
"$CMAKE" --build "$BUILD_DIR" --config Release --target Profundum "$EXE" -- /nr:false > "$BUILD_LOG" 2>&1
build_status=$?
t1="$(date +%s)"
build_wall=$((t1 - t0))
warnings="$(grep -cE "warning C[0-9]|warning:" "$BUILD_LOG" || true)"
warnings="${warnings:-0}"
compile_errors="$(grep -cE "error C[0-9]|error LNK[0-9]|fatal error" "$BUILD_LOG" || true)"
compile_errors="${compile_errors:-0}"
# The post-build copy to C:/Program Files/Common Files/VST3/ may fail on permissions (root CLAUDE.md), so a nonzero
# exit alone does not stop the lanes; a compile or link error does (the exe on disk would be stale).
status_line "build exit=$build_status wall=${build_wall}s warnings=$warnings compile_errors=$compile_errors log=$BUILD_LOG"
if [ "$compile_errors" -ne 0 ] || [ ! -f "$BIN/$EXE.exe" ] || [ ! -d "$BUNDLE" ]; then
    status_line "DONE $(now) RED: compile/link errors=$compile_errors, or $BIN/$EXE.exe / $BUNDLE missing; no test lane ran (see $BUILD_LOG)"
    tail -n 30 "$BUILD_LOG"
    exit 1
fi

# ---------------------------------------------------------------------------------------------------------------
# 3a. Per-push lane.
# ---------------------------------------------------------------------------------------------------------------
run_perpush() {
    local s0 s1 rc
    s0="$(date +%s)"
    "$BIN/$EXE.exe" "$PERPUSH_FILTER" -D 15 > "$PERPUSH_LOG" 2>&1
    rc=$?
    s1="$(date +%s)"
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$PERPUSH_LOG"
    status_line "perpush:$EXE exit=$rc wall=$((s1 - s0))s log=$PERPUSH_LOG"
}

# ---------------------------------------------------------------------------------------------------------------
# 3b. The phase's own [long] cases (zero expected: no Phase 10 case is [long]).
# ---------------------------------------------------------------------------------------------------------------
LIST_RAW="$LONGDIR/list-raw.txt"
LONG_LIST="$LONGDIR/long-cases.txt"
"$BIN/$EXE.exe" --list-tests "$LONG_FILTER" --verbosity quiet > "$LIST_RAW" 2>&1
tr -d '\r' < "$LIST_RAW" | grep -v '^[[:space:]]*$' > "$LONG_LIST"
long_count="$(wc -l < "$LONG_LIST" | tr -d ' ')"
LONG_JOBS=$(( CORES - 4 ))
[ "$LONG_JOBS" -lt 1 ] && LONG_JOBS=1
if [ "${long_count:-0}" -gt 0 ] && [ "$long_count" -lt "$LONG_JOBS" ]; then
    LONG_JOBS="$long_count"
fi
status_line "long-list cases=${long_count:-0} (0 expected) jobs=$LONG_JOBS list=$LONG_LIST"

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
# 3c. Case listing (SC-002.1).
# ---------------------------------------------------------------------------------------------------------------
run_listing() {
    local s0 s1 rc=0 missing="" leaked="" n
    local log="$LOGDIR/list-tests.log"
    local visible="$LOGDIR/list-tests.visible.txt"
    local hidden="$LOGDIR/list-tests.hidden.txt"
    s0="$(date +%s)"
    "$BIN/$EXE.exe" --list-tests --verbosity quiet 2>&1 | tr -d '\r' | sed 's/^[[:space:]]*//' > "$visible"
    "$BIN/$EXE.exe" "[.perf]" --list-tests --verbosity quiet 2>&1 | tr -d '\r' | sed 's/^[[:space:]]*//' > "$hidden"
    while IFS= read -r n; do
        [ -z "$n" ] && continue
        grep -qxF "$n" "$visible" || missing="$missing $n"
    done <<< "$CASES"
    grep -qxF "$HIDDEN_CASE" "$hidden" || missing="$missing $HIDDEN_CASE(hidden)"
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
        echo "missing=[${missing# }] leaked_hidden=[$leaked]"
    } > "$log"
    status_line "list-tests exit=$rc wall=$((s1 - s0))s missing=[${missing# }] leaked_hidden=[$leaked] log=$log"
}

# ---------------------------------------------------------------------------------------------------------------
# 3d. pluginval strictness 5 (SC-003).
# ---------------------------------------------------------------------------------------------------------------
run_pluginval() {
    local s0 s1 rc log="$LOGDIR/pluginval.log"
    s0="$(date +%s)"
    tools/pluginval.exe --strictness-level 5 --validate "$BUNDLE" > "$log" 2>&1
    rc=$?
    s1="$(date +%s)"
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    status_line "pluginval exit=$rc wall=$((s1 - s0))s log=$log"
}

# ---------------------------------------------------------------------------------------------------------------
# 3e. check-bundle (SC-020): exit 0 and both required lines.
# ---------------------------------------------------------------------------------------------------------------
run_bundle() {
    local s0 s1 rc log="$LOGDIR/check-bundle.log"
    s0="$(date +%s)"
    node tools/check-bundle.js "$BUNDLE" > "$log" 2>&1
    rc=$?
    s1="$(date +%s)"
    if [ "$rc" -eq 0 ] && ! tr -d '\r' < "$log" | grep -qF "OK   Profundum.vst3: editor.uidesc + moduleinfo.json present"; then
        rc=97
    fi
    if [ "$rc" -eq 0 ] && ! tr -d '\r' < "$log" | grep -qF "check-bundle: all 1 bundle(s) OK"; then
        rc=96
    fi
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    status_line "check-bundle exit=$rc wall=$((s1 - s0))s log=$log"
}

# ---------------------------------------------------------------------------------------------------------------
# 3f. Clean tree after the build (SC-021). While plugins/profundum is uncommitted its sources show as untracked, so
# a generated file is (i) any FR-007 path listed at all, or (ii) any path new since the pre-build snapshot. The
# literal SC-021 command is logged too (it prints nothing once the tree is committed).
# ---------------------------------------------------------------------------------------------------------------
run_clean_tree() {
    local s0 s1 rc=0 gen="" g new_paths
    local log="$LOGDIR/clean-tree.log"
    s0="$(date +%s)"
    git status --porcelain --untracked-files=all plugins/profundum 2>/dev/null | tr -d '\r' | sort -u > "$CLEAN_POST"
    while IFS= read -r g; do
        [ -z "$g" ] && continue
        if sed 's/^...//' "$CLEAN_POST" | tr -d '"' | grep -qxF "$g"; then
            gen="$gen $g"
        fi
    done <<< "$GENERATED"
    new_paths="$(comm -13 "$CLEAN_PRE" "$CLEAN_POST" | grep -v '^$' | sed 's/^...//' | tr '\n' ' ')"
    new_paths="${new_paths% }"
    if [ -n "$gen" ] || [ -n "$new_paths" ]; then
        rc=1
    fi
    {
        echo "=== git status --porcelain --untracked-files=all plugins/profundum (after the build)"
        cat "$CLEAN_POST"
        echo "=== FR-007 generated paths listed: [${gen# }]"
        echo "=== paths new since the pre-build snapshot: [$new_paths]"
        echo "=== git status --porcelain plugins/profundum (literal SC-021 command; empty once committed)"
        git status --porcelain plugins/profundum
    } > "$log" 2>&1
    s1="$(date +%s)"
    status_line "clean-tree exit=$rc wall=$((s1 - s0))s generated=[${gen# }] new=[$new_paths] log=$log"
}

lane_start="$(date +%s)"
run_perpush &
perpush_pid=$!
(
    l0="$(date +%s)"
    if [ "${long_count:-0}" -gt 0 ]; then
        tr '\n' '\0' < "$LONG_LIST" | xargs -0 -n 1 -P "$LONG_JOBS" bash -c 'run_long_shard "$1"' _
    fi
    l1="$(date +%s)"
    echo "long-lane shards=${long_count:-0} wall=$((l1 - l0))s" >> "$STATUS"
) &
long_pid=$!
run_listing &
list_pid=$!
run_pluginval &
pv_pid=$!
run_bundle &
bundle_pid=$!
run_clean_tree &
clean_pid=$!

for p in "$perpush_pid" "$long_pid" "$list_pid" "$pv_pid" "$bundle_pid" "$clean_pid"; do
    wait "$p"
done
lane_end="$(date +%s)"

# ---------------------------------------------------------------------------------------------------------------
# 4. Summary.
# ---------------------------------------------------------------------------------------------------------------
st() { tr -d '\r' < "$STATUS"; }
lane_exit() { st | grep -F "$1 exit=" | grep -E "^$2" | tail -n 1 | sed -nE 's/.* exit=([0-9]+).*/\1/p'; }
lane_wall() { st | grep -F "$1 exit=" | grep -E "^$2" | tail -n 1 | sed -nE 's/.* wall=([0-9]+s).*/\1/p'; }
verdict_of() { [ "${1:-1}" = "0" ] && echo GREEN || echo RED; }

perpush_last="$(tr -d '\r' < "$PERPUSH_LOG" | grep -v '^exit: ' | grep -v '^[[:space:]]*$' | tail -n 1)"
perpush_rc="$(lane_exit "perpush:$EXE" "perpush:")"
perpush_ok=1
case "$perpush_last" in
    "All tests passed"*) [ "${perpush_rc:-1}" = "0" ] && perpush_ok=0 ;;
esac

long_red="$(st | grep -E '^long:' | grep -vE ' exit=0 ' | sed -nE 's/^long:(.+) exit=.*/\1/p' | tr '\n' ' ')"
long_ok=0
if [ -n "$long_red" ] || [ "${long_count:-0}" -ne 0 ]; then
    # Zero [long] cases are expected in Phase 10; any listed case is reported RED for triage even if it passed.
    long_ok=1
fi
build_ok=1
if [ "$warnings" -eq 0 ]; then
    build_ok=0
fi

list_rc="$(lane_exit list-tests list-tests)"
pv_rc="$(lane_exit pluginval pluginval)"
bundle_rc="$(lane_exit check-bundle check-bundle)"
clean_rc="$(lane_exit clean-tree clean-tree)"

overall=GREEN
for v in "$scope_red" "$build_ok" "$perpush_ok" "$long_ok" "${list_rc:-1}" "${pv_rc:-1}" "${bundle_rc:-1}" "${clean_rc:-1}"; do
    [ "$v" = "0" ] || overall=RED
done

suite_end="$(date +%s)"
{
    echo "=== summary"
    echo "scope (FR-090)          $(verdict_of "$scope_red") base=$BASE_COMMIT"
    echo "build (SC-001)          $(verdict_of "$build_ok") exit=$build_status warnings=$warnings compile_errors=$compile_errors wall=${build_wall}s"
    echo "perpush (SC-002.2)      $(verdict_of "$perpush_ok") exit=${perpush_rc:-?} last=\"$perpush_last\" wall=$(lane_wall "perpush:$EXE" "perpush:") (P-7: for a later ci-test-times.json refresh)"
    echo "long (phase TUs)        $(verdict_of "$long_ok") cases=${long_count:-0} (0 expected) red=[${long_red% }] $(st | grep -E '^long-lane ' | tail -n 1)"
    echo "list-tests (SC-002.1)   $(verdict_of "${list_rc:-1}") wall=$(lane_wall list-tests list-tests)"
    echo "pluginval (SC-003)      $(verdict_of "${pv_rc:-1}") exit=${pv_rc:-?} wall=$(lane_wall pluginval pluginval)"
    echo "check-bundle (SC-020)   $(verdict_of "${bundle_rc:-1}") exit=${bundle_rc:-?} wall=$(lane_wall check-bundle check-bundle)"
    echo "clean-tree (SC-021)     $(verdict_of "${clean_rc:-1}") wall=$(lane_wall clean-tree clean-tree)"
    echo "lanes wall=$((lane_end - lane_start))s total wall=$((suite_end - suite_start))s"
} >> "$STATUS"
status_line "DONE $(now) $overall"
st | tail -n 12
[ "$overall" = "GREEN" ]
