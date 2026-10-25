#!/usr/bin/env bash
# Profundum Phase 13, task T032 close lane: change-set scope, one build, the per-push lanes, the phase's own [long]
# lane, the case listing, pluginval, the generator determinism check and the installed-tree case (SC-013 lifecycle,
# SC-025 build / tests / pluginval, SC-011, SC-014 installed arm). Structure copied from
# specs/profundum-phase12-ui/evidence/close/suites.sh.
#
# Lane scope (encoded 2026-10-18, Phase 6 ruling G9-4; FR-058): the executables are profundum_tests (every Phase 13 TU
# and the edited Phase 12 TUs) and dsp_systems_tests (the consumer of the modified Phase 9 TU; FR-058 limits it to that
# TU's cases -- never the full suite, never its [long] set).
#
# Run from the main loop DETACHED (Git Bash, from the Bash tool), concurrently with tidy.sh (T034):
#   nohup bash specs/profundum-phase13-presets-release/evidence/close/suites.sh \
#       > specs/profundum-phase13-presets-release/evidence/run-log/close/suites.out 2>&1 & disown
# Watch evidence/run-log/close/status.txt; its last line is "DONE <time> GREEN|RED ...".
# FORCE_LONG=1 runs lane 3c even when T027's record is GREEN and fresh.
#
# Steps:
#   1. Change-set scope. git diff --name-only e6095b24 -- dsp/include dsp/src plugins/shared tests/test_helpers must
#      print nothing beyond the FR-065 allow-list (user ruling 2026-10-24: plugins/shared/src/preset/preset_manager.cpp,
#      plugins/shared/src/ui/preset_browser_view.{h,cpp}, plugins/shared/tests/CMakeLists.txt). Those paths are
#      reported and their consumers are the FR-065 gate's lane (evidence/close/fr065-gate.sh, whose last line is quoted
#      here, not re-run). Anything else printed is RED and stops the script: no build, no lane.
#      [Deviation from tasks.md T032 step 1, which reads "must print nothing": FR-065 (T037) was ruled after the task
#      was cut and modifies plugins/shared; T031's allow-list was amended for it, this step follows that amendment.]
#   2. One build of Profundum profundum_tests profundum_preset_generator dsp_systems_tests (-- /nr:false) to build.log;
#      warning count (warning C[0-9]|warning:) must be 0.
#   3. Concurrently, each to its own log with its wall clock:
#      (a) profundum_tests.exe "~[long]~[performance]~[perf]~[benchmark]~[!benchmark]" -> "All tests passed";
#      (b) dsp_systems_tests.exe -# "[#profundum_engine_audibility_test]~[long]" -> "All tests passed".
#          [Deviation: tasks.md writes "[voice-engine]~[long]", but [voice-engine] tags cases in ten systems TUs
#          (grep -rlE 'TEST_CASE\(.*\[voice-engine\]' dsp/tests/unit/systems/), which is the full Phase 9 suite FR-058
#          forbids. The file tag selects the modified TU's per-push smoke cases only, the stated intent.]
#      (c) the [long] cases of the phase's own TUs: profundum_tests.exe --list-tests "[profundum][long]" kept when the
#          name is in LONG_SET (Profundum_PresetShowcase_<k>of8, Profundum_PresetSweep, Profundum_PresetSweepRates,
#          Profundum_PresetTruePeakMaxMass, Profundum_PresetMonoSum, Profundum_PresetDeterminism,
#          Profundum_PresetLoudnessMatch, Profundum_PresetDistinctness); one process per name. A listed [long] name
#          outside the set is reported, not run. SKIPPED when T027's record (evidence/library/status.txt) names the
#          current HEAD, ends "verdict GREEN", and is newer than every modified / untracked file under the paths those
#          cases compile from (the tree is uncommitted, so the commit alone does not prove the record current).
#      (d) --list-tests shows every SC case name of spec.md (visible set); the hidden ones are listed under their
#          hidden tag ([.perf], [.install], [.probe]) and absent from the default listing;
#      (e) tools/pluginval.exe --strictness-level 5 --validate build/windows-x64-release/VST3/Release/Profundum.vst3;
#      (f) node tools/check-preset-generator-determinism.js --plugin profundum -> exit 0 (SC-011);
#      (g) profundum_tests.exe "Profundum_InstalledPresetTree" (Windows only; the Profundum build's post-build step
#          krate_plugin_install_presets, plugins/profundum/CMakeLists.txt:128, installed the tree in step 2) (SC-014).
#   4. Summary block with every lane's verdict and wall clock.
# Status file evidence/run-log/close/status.txt: one line per lane: name, exit code, wall clock (s).
# Exit code: 0 only if every lane is green.
#
# A timing-budget assertion of an untouched case that goes red under the concurrent lanes is recorded as load, not
# re-run. A Phase 13 red is owned: fix it, re-run only the affected case.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase13-presets-release"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
BUILD_DIR="build/windows-x64-release"
BIN="$BUILD_DIR/bin/Release"
EXE="profundum_tests"
SYS_EXE="dsp_systems_tests"
GEN="profundum_preset_generator"
PROFUNDUM_BUNDLE="$BUILD_DIR/VST3/Release/Profundum.vst3"
PERPUSH_FILTER="~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"
SYS_FILTER="[#profundum_engine_audibility_test]~[long]"
LONG_FILTER="[profundum][long]"
BASE_COMMIT="e6095b24"
FENCE_PATHS="dsp/include dsp/src plugins/shared tests/test_helpers"
FR065_ALLOWED="plugins/shared/src/preset/preset_manager.cpp
plugins/shared/src/ui/preset_browser_view.h
plugins/shared/src/ui/preset_browser_view.cpp
plugins/shared/tests/CMakeLists.txt"
FR065_OUT="$SPEC/evidence/run-log/close/fr065-gate.out"
T027_STATUS="$SPEC/evidence/library/status.txt"
# Paths the Phase 13 [long] cases compile from or read; a change newer than T027's record makes the record stale.
T027_INPUTS="plugins/profundum/src plugins/profundum/tests plugins/profundum/resources/presets plugins/profundum/CMakeLists.txt tools/profundum_preset_defs.h tools/profundum_preset_generator.cpp dsp/include dsp/src dsp/tests/unit/systems plugins/shared/src"
LOGDIR="$SPEC/evidence/run-log/close"
LONGDIR="$LOGDIR/long"
STATUS="$LOGDIR/status.txt"

LONG_SET="Profundum_PresetShowcase_0of8
Profundum_PresetShowcase_1of8
Profundum_PresetShowcase_2of8
Profundum_PresetShowcase_3of8
Profundum_PresetShowcase_4of8
Profundum_PresetShowcase_5of8
Profundum_PresetShowcase_6of8
Profundum_PresetShowcase_7of8
Profundum_PresetSweep
Profundum_PresetSweepRates
Profundum_PresetTruePeakMaxMass
Profundum_PresetMonoSum
Profundum_PresetDeterminism
Profundum_PresetLoudnessMatch
Profundum_PresetDistinctness"

# Spec SC case names (spec.md), visible in the default listing. The Profundum_PresetShowcase family is its 8 shards.
CASES="Profundum_CommittedPresetTreeMatches
Profundum_CoverageMatrixComplete
Profundum_EditorLifecycle
Profundum_FactoryLibraryShape
Profundum_FactoryPresetCeiling
Profundum_FactoryPresetContainer
Profundum_FactoryPresetLoadRtSafe
Profundum_FactoryPresetRoundTrip
Profundum_FactoryPresetScan
Profundum_FactoryPresetStateFormat
Profundum_GeneratorSmoke
Profundum_ManualCoversSurface
Profundum_PresetBrowserLoad
Profundum_PresetCategoriesMatchFilesystem
Profundum_PresetDescriptions
Profundum_PresetDeterminism
Profundum_PresetDistinctness
Profundum_PresetLoadUnderHeldNote
Profundum_PresetLoudnessMatch
Profundum_PresetMonoSum
Profundum_PresetRenderGuard_0of4
Profundum_PresetRenderGuard_1of4
Profundum_PresetRenderGuard_2of4
Profundum_PresetRenderGuard_3of4
Profundum_PresetShowcase_0of8
Profundum_PresetShowcase_1of8
Profundum_PresetShowcase_2of8
Profundum_PresetShowcase_3of8
Profundum_PresetShowcase_4of8
Profundum_PresetShowcase_5of8
Profundum_PresetShowcase_6of8
Profundum_PresetShowcase_7of8
Profundum_PresetSweep
Profundum_PresetSweepRates
Profundum_PresetTruePeakMaxMass
Profundum_ReferenceColumns
Profundum_UiLayout"
# Hidden SC cases: "<hidden tag>|<name>". Profundum_PresetAudibilityProbe is its eleven group shards.
HIDDEN_CASES="[.perf]|Profundum_PresetCpu
[.install]|Profundum_InstalledPresetTree
[.probe]|Profundum_PresetAudibilityProbe_Core
[.probe]|Profundum_PresetAudibilityProbe_Character
[.probe]|Profundum_PresetAudibilityProbe_Anchor
[.probe]|Profundum_PresetAudibilityProbe_Pitch
[.probe]|Profundum_PresetAudibilityProbe_Envelope
[.probe]|Profundum_PresetAudibilityProbe_Mass
[.probe]|Profundum_PresetAudibilityProbe_Field
[.probe]|Profundum_PresetAudibilityProbe_Motion
[.probe]|Profundum_PresetAudibilityProbe_Morph
[.probe]|Profundum_PresetAudibilityProbe_NoteMemory
[.probe]|Profundum_PresetAudibilityProbe_Macros"

CORES="$(nproc 2>/dev/null || echo 8)"

mkdir -p "$LOGDIR" "$LONGDIR"
rm -f "$LONGDIR"/*.log "$LONGDIR"/*.txt

now() { date -u +%Y-%m-%dT%H:%M:%SZ; }
status_line() { echo "$*" >> "$STATUS"; }
HEAD_SHORT="$(git rev-parse --short=8 HEAD 2>/dev/null || echo unknown)"

suite_start="$(date +%s)"
echo "START $(now) commit $HEAD_SHORT base $BASE_COMMIT cores $CORES" > "$STATUS"

# ---------------------------------------------------------------------------------------------------------------
# 1. Change-set scope.
# ---------------------------------------------------------------------------------------------------------------
SCOPE_LOG="$LOGDIR/scope-diff.log"
# shellcheck disable=SC2086
{
    echo "=== git diff --name-only $BASE_COMMIT -- $FENCE_PATHS (must print nothing beyond the FR-065 allow-list)"
    git diff --name-only "$BASE_COMMIT" -- $FENCE_PATHS
    echo "=== git ls-files --others --exclude-standard -- $FENCE_PATHS (new files; reported)"
    git ls-files --others --exclude-standard -- $FENCE_PATHS
    echo "=== git diff --name-only $BASE_COMMIT -- dsp/tests (modified dsp test files; reported)"
    git diff --name-only "$BASE_COMMIT" -- dsp/tests
} > "$SCOPE_LOG" 2>&1

# shellcheck disable=SC2086
fence_changed="$(git diff --name-only "$BASE_COMMIT" -- $FENCE_PATHS 2>/dev/null | tr -d '\r' | grep -v '^$' | sort -u)"
# shellcheck disable=SC2086
fence_new="$(git ls-files --others --exclude-standard -- $FENCE_PATHS 2>/dev/null | tr -d '\r' | grep -v '^$' | sort -u | tr '\n' ' ')"
fence_red=""
fence_fr065=""
while IFS= read -r p; do
    [ -z "$p" ] && continue
    if printf '%s\n' "$FR065_ALLOWED" | grep -qxF "$p"; then
        fence_fr065="$fence_fr065 $p"
    else
        fence_red="$fence_red $p"
        status_line "scope RED unexpected-modified path=$p"
    fi
done <<< "$fence_changed"
if [ -n "$fence_red" ]; then
    status_line "scope RED base=$BASE_COMMIT log=$SCOPE_LOG (a shared header / source outside FR-065 is modified: its consumers' suites are not in this lane)"
    status_line "DONE $(now) RED: change set reaches shared code outside the FR-065 allow-list; no build, no lane ran"
    tr -d '\r' < "$STATUS" | tail -n 12
    exit 1
fi
fr065_last="(no fr065-gate.out)"
if [ -f "$FR065_OUT" ]; then
    fr065_last="$(tr -d '\r\000' < "$FR065_OUT" | grep -v '^[[:space:]]*$' | tail -n 1)"
fi
dsp_tests_modified="$(git diff --name-only "$BASE_COMMIT" -- dsp/tests 2>/dev/null | tr -d '\r' | grep -v '^$' | sort -u | tr '\n' ' ')"
status_line "scope GREEN base=$BASE_COMMIT fr065_paths=[${fence_fr065# }] new_under_fence=[${fence_new% }] dsp_tests_modified=[${dsp_tests_modified% }] log=$SCOPE_LOG"
status_line "scope NOTE FR-065 consumers are the fr065-gate.sh lane (not re-run here); its last line: \"$fr065_last\""

# ---------------------------------------------------------------------------------------------------------------
# 2. One build.
# ---------------------------------------------------------------------------------------------------------------
BUILD_TARGETS="Profundum $EXE $GEN $SYS_EXE"
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
for f in "$BIN/$EXE.exe" "$BIN/$SYS_EXE.exe" "$BIN/$GEN.exe"; do
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
# run_suite <lane-name> <exe> <filter> [extra args...]
run_suite() {
    local name="$1" exe="$2" filter="$3" s0 s1 rc
    shift 3
    local log="$LOGDIR/perpush-$name.log"
    s0="$(date +%s)"
    "$BIN/$exe.exe" "$@" "$filter" -D 15 > "$log" 2>&1
    rc=$?
    s1="$(date +%s)"
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    status_line "perpush:$name exit=$rc wall=$((s1 - s0))s log=$log"
}

# ---------------------------------------------------------------------------------------------------------------
# 3c. The [long] cases of the phase's own TUs.
# ---------------------------------------------------------------------------------------------------------------
LIST_RAW="$LONGDIR/list-raw.txt"
LONG_LISTED="$LONGDIR/long-listed.txt"
LONG_RUN="$LONGDIR/long-run.txt"
"$BIN/$EXE.exe" --list-tests "$LONG_FILTER" --verbosity quiet > "$LIST_RAW" 2>&1
tr -d '\r' < "$LIST_RAW" | sed 's/^[[:space:]]*//' | grep -v '^$' > "$LONG_LISTED"

: > "$LONG_RUN"
long_foreign=""
while IFS= read -r n; do
    [ -z "$n" ] && continue
    if printf '%s\n' "$LONG_SET" | grep -qxF "$n"; then
        echo "$n" >> "$LONG_RUN"
    else
        long_foreign="$long_foreign $n"
    fi
done < "$LONG_LISTED"
# A set name the executable does not list is a registration defect.
long_missing=""
while IFS= read -r n; do
    [ -z "$n" ] && continue
    grep -qxF "$n" "$LONG_LISTED" || long_missing="$long_missing $n"
done <<< "$LONG_SET"

# T027 record: same HEAD, GREEN, and newer than every modified / untracked input file.
long_skip=0
long_skip_why=""
if [ "${FORCE_LONG:-0}" != "1" ] && [ -f "$T027_STATUS" ]; then
    t027_commit="$(tr -d '\r' < "$T027_STATUS" | head -n 1 | sed -nE 's/.* commit ([0-9a-f]+).*/\1/p')"
    t027_verdict="$(tr -d '\r' < "$T027_STATUS" | grep -v '^[[:space:]]*$' | tail -n 1)"
    stale=""
    # shellcheck disable=SC2086
    while IFS= read -r f; do
        [ -z "$f" ] && continue
        [ -f "$f" ] || continue
        [ "$f" -nt "$T027_STATUS" ] && stale="$stale $f"
    done < <({ git diff --name-only HEAD -- $T027_INPUTS; git ls-files --others --exclude-standard -- $T027_INPUTS; } \
                 2>/dev/null | tr -d '\r' | sort -u)
    if [ -n "$t027_commit" ] && [ "${HEAD_SHORT#"$t027_commit"}" != "$HEAD_SHORT" ] \
        && [ "${t027_verdict#verdict GREEN}" != "$t027_verdict" ] && [ -z "$stale" ]; then
        long_skip=1
        long_skip_why="T027 record commit=$t027_commit \"$t027_verdict\" newer than every changed input"
    else
        long_skip_why="T027 record not reusable: commit=${t027_commit:-?} last=\"$t027_verdict\" newer_inputs=[${stale# }]"
    fi
elif [ "${FORCE_LONG:-0}" = "1" ]; then
    long_skip_why="FORCE_LONG=1"
else
    long_skip_why="no T027 record at $T027_STATUS"
fi

long_count="$(grep -c . "$LONG_RUN" || true)"
long_count="${long_count:-0}"
LONG_JOBS=$(( CORES - 4 ))
[ "$LONG_JOBS" -gt 8 ] && LONG_JOBS=8
[ "$LONG_JOBS" -lt 1 ] && LONG_JOBS=1
status_line "long-list set=$long_count skip=$long_skip ($long_skip_why) missing=[${long_missing# }] foreign_not_run=[${long_foreign# }] jobs=$LONG_JOBS list=$LONG_RUN"

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
    if grep -qiE "no test cases matched|No tests ran" "$log"; then
        rc=99
    fi
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    echo "long:$name exit=$rc wall=$((s1 - s0))s log=$log" >> "$STATUS"
    return 0
}
export -f run_long_shard
export BIN EXE LONGDIR STATUS

# ---------------------------------------------------------------------------------------------------------------
# 3d. Case listing.
# ---------------------------------------------------------------------------------------------------------------
run_listing() {
    local s0 s1 rc=0 missing="" leaked="" n tag entry
    local log="$LOGDIR/list-tests.log"
    local visible="$LOGDIR/list-tests.visible.txt"
    local hidden="$LOGDIR/list-tests.hidden.txt"
    s0="$(date +%s)"
    "$BIN/$EXE.exe" --list-tests --verbosity quiet 2>&1 | tr -d '\r' | sed 's/^[[:space:]]*//' > "$visible"
    : > "$hidden"
    for tag in "[.perf]" "[.install]" "[.probe]"; do
        "$BIN/$EXE.exe" "$tag" --list-tests --verbosity quiet 2>&1 | tr -d '\r' | sed 's/^[[:space:]]*//' \
            | sed "s#^#$tag|#" >> "$hidden"
    done
    while IFS= read -r n; do
        [ -z "$n" ] && continue
        grep -qxF "$n" "$visible" || missing="$missing $n"
    done <<< "$CASES"
    while IFS= read -r entry; do
        [ -z "$entry" ] && continue
        grep -qxF "$entry" "$hidden" || missing="$missing $entry"
        n="${entry#*|}"
        # A hidden case must not be in the default listing.
        if grep -qxF "$n" "$visible"; then
            leaked="$leaked $n"
        fi
    done <<< "$HIDDEN_CASES"
    if [ -n "$missing" ] || [ -n "$leaked" ]; then
        rc=1
    fi
    s1="$(date +%s)"
    {
        echo "=== $EXE.exe --list-tests --verbosity quiet"
        cat "$visible"
        echo "=== $EXE.exe <hidden tag> --list-tests --verbosity quiet (prefixed with the tag)"
        cat "$hidden"
        echo "missing=[${missing# }] leaked_hidden=[${leaked# }]"
    } > "$log"
    status_line "list-tests exit=$rc wall=$((s1 - s0))s missing=[${missing# }] leaked_hidden=[${leaked# }] log=$log"
}

# ---------------------------------------------------------------------------------------------------------------
# 3e. pluginval strictness 5 (SC-025; editor open / close covers SC-013's host-level lifecycle).
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
# 3f. Generator determinism (SC-011).
# ---------------------------------------------------------------------------------------------------------------
run_determinism() {
    local s0 s1 rc last
    local log="$LOGDIR/determinism.log"
    s0="$(date +%s)"
    node tools/check-preset-generator-determinism.js --plugin profundum > "$log" 2>&1
    rc=$?
    s1="$(date +%s)"
    last="$(tr -d '\r\000' < "$log" | grep -v '^[[:space:]]*$' | tail -n 1)"
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    status_line "determinism exit=$rc wall=$((s1 - s0))s last=\"$last\" log=$log"
}

# ---------------------------------------------------------------------------------------------------------------
# 3g. Installed tree (SC-014 installed arm; Windows only).
# ---------------------------------------------------------------------------------------------------------------
run_installed() {
    local s0 s1 rc
    local log="$LOGDIR/installed-tree.log"
    s0="$(date +%s)"
    case "$(uname -s 2>/dev/null)" in
        MINGW*|MSYS*|CYGWIN*)
            "$BIN/$EXE.exe" "Profundum_InstalledPresetTree" > "$log" 2>&1
            rc=$?
            if grep -qiE "no test cases matched|No tests ran" "$log"; then
                rc=99
            fi
            ;;
        *)
            echo "not Windows: SC-014 installed arm is Windows-only (preset_browser_test.cpp #if defined(_WIN32))" > "$log"
            rc=0
            ;;
    esac
    s1="$(date +%s)"
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    status_line "installed-tree exit=$rc wall=$((s1 - s0))s log=$log"
}

lane_start="$(date +%s)"
pids=""
# The [long] lane starts first (longest per-process wall clock).
(
    l0="$(date +%s)"
    if [ "$long_skip" -eq 0 ] && [ "$long_count" -gt 0 ]; then
        tr '\n' '\0' < "$LONG_RUN" | xargs -0 -n 1 -P "$LONG_JOBS" bash -c 'run_long_shard "$1"' _
    fi
    l1="$(date +%s)"
    echo "long-lane shards=$long_count skipped=$long_skip wall=$((l1 - l0))s" >> "$STATUS"
) &
pids="$pids $!"
run_suite "$EXE" "$EXE" "$PERPUSH_FILTER" &
pids="$pids $!"
run_suite "$SYS_EXE" "$SYS_EXE" "$SYS_FILTER" -# &
pids="$pids $!"
run_listing &
pids="$pids $!"
run_pluginval &
pids="$pids $!"
run_determinism &
pids="$pids $!"
run_installed &
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
if [ -n "$long_red" ] || [ -n "$long_missing" ]; then
    long_ok=1
fi
if [ "$long_skip" -eq 0 ] && [ "${long_done:-0}" -ne "$long_count" ]; then
    long_ok=1
fi

list_rc="$(lane_exit list-tests)"
pv_rc="$(lane_exit pluginval:Profundum)"
det_rc="$(lane_exit determinism)"
inst_rc="$(lane_exit installed-tree)"

overall=GREEN
for v in "$build_ok" "$pp_prof" "$pp_sys" "$long_ok" "${list_rc:-1}" "${pv_rc:-1}" "${det_rc:-1}" "${inst_rc:-1}"; do
    [ "$v" = "0" ] || overall=RED
done

suite_end="$(date +%s)"
{
    echo "=== summary"
    echo "scope (FR-058, FR-065)            GREEN base=$BASE_COMMIT fr065_paths=[${fence_fr065# }] fr065-gate last=\"$fr065_last\""
    echo "build (SC-025)                    $(verdict_of "$build_ok") exit=$build_status warnings=$warnings compile_errors=$compile_errors wall=${build_wall}s"
    echo "perpush:$EXE             $(verdict_of "$pp_prof") last=\"$(perpush_last "$EXE")\" wall=$(lane_wall "perpush:$EXE")"
    echo "perpush:$SYS_EXE (FR-058)  $(verdict_of "$pp_sys") filter=\"$SYS_FILTER\" last=\"$(perpush_last "$SYS_EXE")\" wall=$(lane_wall "perpush:$SYS_EXE")"
    if [ "$long_skip" -eq 1 ]; then
        echo "long (phase's own TUs)            $(verdict_of "$long_ok") SKIPPED ($long_skip_why) missing=[${long_missing# }] foreign_not_run=[${long_foreign# }]"
    else
        echo "long (phase's own TUs)            $(verdict_of "$long_ok") run=$long_count done=${long_done:-0} red=[${long_red% }] missing=[${long_missing# }] foreign_not_run=[${long_foreign# }] $(st | grep -E '^long-lane ' | tail -n 1)"
    fi
    echo "list-tests (SC names)             $(verdict_of "${list_rc:-1}") wall=$(lane_wall list-tests)"
    echo "pluginval Profundum (SC-025)      $(verdict_of "${pv_rc:-1}") exit=${pv_rc:-?} wall=$(lane_wall pluginval:Profundum)"
    echo "determinism (SC-011)              $(verdict_of "${det_rc:-1}") exit=${det_rc:-?} wall=$(lane_wall determinism)"
    echo "installed tree (SC-014)           $(verdict_of "${inst_rc:-1}") exit=${inst_rc:-?} wall=$(lane_wall installed-tree)"
    echo "lanes wall=$((lane_end - lane_start))s total wall=$((suite_end - suite_start))s"
} >> "$STATUS"
status_line "DONE $(now) $overall"
st | tail -n 16
[ "$overall" = "GREEN" ]
