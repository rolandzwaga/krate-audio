#!/usr/bin/env bash
# Profundum Phase 14, task T031 close lane: change-set scope, one build, the per-push lanes of every consumer of the
# modified headers, the consumers' [long] cases, the SC-008 pre-phase fingerprint comparison, pluginval, the generator
# determinism check and the SC case listing (SC-008, SC-021, spec C-16). Structure copied from
# specs/profundum-phase13-presets-release/evidence/close/suites.sh.
#
# Lane scope (encoded 2026-10-18, Phase 6 ruling G9-4; spec C-16; tasks.md "Gate scope note"): this phase MODIFIES
# profundum_engine.h, profundum_engine_observer.h and the Phase 9 driver profundum_voice_engine_test_helpers.h. Their
# consumers are Profundum TUs only, and every TEST_CASE in them carries [profundum], so the [profundum] per-push set is
# the consumers' per-push set. Their [long] cases are run per TU (lane c), never every [long] case of an executable.
#
# Run from the main loop DETACHED (Git Bash, from the Bash tool), concurrently with tidy.sh (T034):
#   nohup bash specs/profundum-phase14-morph-modulation/evidence/close/suites.sh \
#       > specs/profundum-phase14-morph-modulation/evidence/run-log/close/suites.out 2>&1 & disown
# Watch evidence/run-log/close/status.txt; its last line is "DONE <time> GREEN|RED".
# FORCE_LONG=1 runs the Phase 13 library [long] names even when T026's record is GREEN and fresh.
# Widths (override by env): LANE_SHARE (default 4) is run-gate's --share for each of the two concurrent per-push gates;
# LONG_JOBS (default (cores - 2) / 2) is the [long] pool; WALL_WORKERS (default 4) is the quiet [wallclock] pass.
#
# Steps:
#   1. Change set. git diff --name-only 3dfc5b7d -- dsp/include dsp/src plugins/shared tests/test_helpers dsp/tests
#      (plus untracked files under the same paths) must stay inside the T030 allow-list's dsp/ fence (tasks.md
#      "Rules -> dsp/ fence"); anything else is RED and stops the script: no build, no lane.
#      [Deviation, the same as T030's scope-fence.js: the diff against the base also lists the test files of the
#      user's two non-phase commits after the base (4fb6c248, e501b577: [wallclock] tags in processors / Seraphis
#      TUs). A path reached ONLY through those commits (in git diff 3dfc5b7d HEAD, not in git diff HEAD, not
#      untracked) is listed, not fenced; any other post-base commit is RED.]
#   2. One build of dsp_systems_tests dsp_ieee_tests profundum_tests Profundum profundum_preset_generator
#      (-- /nr:false) to build.log; warning count (warning C[0-9]|warning:) must be 0.
#   3. Concurrently, each to its own log with its wall clock:
#      (a) profundum_tests "~[long]~[.]~[performance]~[perf]~[benchmark]~[!benchmark]~[wallclock]" through
#          tools/run-gate.js (sharded), the phase executable's per-push lane;
#      (b) dsp_systems_tests and dsp_ieee_tests "[profundum]~[long]~[.]~[performance]~[perf]~[benchmark]~[!benchmark]
#          ~[wallclock]" through tools/run-gate.js, the consumers' per-push set;
#          [Deviations from tasks.md T031 3(a)(b), which name single serial exe runs and dsp_ieee_tests "[profundum]":
#          root CLAUDE.md (user, 2026-10-26) puts every non-timing multi-case run through run-gate.js, which refuses a
#          filter without the timing exclusions; dsp_ieee_tests' [long] cases are in lane (c), whose TU list holds the
#          IEEE TUs that include the engine header. "~[.]" is explicit (a hand-run filter of exclusions only must not
#          reach hidden cases). The [wallclock] cases run in step 3h, after every lane, as run-gate's quiet pass would.]
#      (c) the consumers' [long] cases: TU list = grep -rlE '#include .*(<modified headers and the Phase 9 / plugin
#          test helpers that include them>)' over dsp/tests/unit/systems and plugins/profundum/tests, closed
#          transitively (a header in the result adds its own includers, until nothing changes); per TU,
#          <exe> -# --list-tests "[#<tu>][long]~[.]~[performance]~[perf]~[benchmark]~[!benchmark]" for the
#          executables the TU can be in (dsp_systems_tests + dsp_ieee_tests for dsp TUs, profundum_tests for plugin
#          TUs); one process per case name, LONG_JOBS wide. The Phase 13 library [long] names (LIBRARY_SET, the names
#          T026's run-library-lanes.sh ran, except this phase's Profundum_MorphModHarnessFidelity) are SKIPPED when
#          evidence/library/status.txt ends "verdict GREEN" and is newer than every modified / untracked file under
#          plugins/profundum and tools/;
#      (d) profundum_tests "Profundum_MorphModPrePhaseFingerprints_<k>of4" x 4 (SC-008 close gate, 0 failures; the
#          "# Profundum_MorphModPrePhaseFingerprints_<k>of4: compared= ... worstMetric= ... worstSample=" lines are
#          copied into the status file);
#      (e) tools/pluginval.exe --strictness-level 5 --validate build/windows-x64-release/VST3/Release/Profundum.vst3;
#      (f) node tools/check-preset-generator-determinism.js --plugin profundum -> exit 0;
#      (g) --list-tests shows every SC case name of spec.md (visible ones in the default listing; the hidden ones under
#          their hidden tag and absent from the default listing);
#      (h) after (a)-(g) drain: the [wallclock] cases of the (a) / (b) filters through run-gate.js, WALL_WORKERS wide.
#   4. Summary block with every lane's verdict and wall clock. node tools/clean-test-output.js --apply is NOT run here
#      (close procedure step 10).
# Status file evidence/run-log/close/status.txt: one line per lane: name, exit code, wall clock (s).
# Exit code: 0 only if every lane is green.
#
# A timing-budget assertion of an untouched case that goes red under the concurrent lanes is recorded as load, not
# re-run. A Phase 14 red is owned: fix it, re-run only the affected case.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase14-morph-modulation"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
BUILD_DIR="build/windows-x64-release"
BIN="$BUILD_DIR/bin/Release"
EXE="profundum_tests"
SYS_EXE="dsp_systems_tests"
IEEE_EXE="dsp_ieee_tests"
GEN="profundum_preset_generator"
PROFUNDUM_BUNDLE="$BUILD_DIR/VST3/Release/Profundum.vst3"
TIMING_EXCL="~[performance]~[perf]~[benchmark]~[!benchmark]"
PP_FILTER="~[long]~[.]$TIMING_EXCL"
DSP_FILTER="[profundum]~[long]~[.]$TIMING_EXCL"
BASE_COMMIT="3dfc5b7d"
# The user's non-phase commits on the branch after the base (evidence/close/scope-fence.js NON_PHASE_COMMITS).
NON_PHASE_COMMITS="4fb6c248 e501b577"
FENCE_PATHS="dsp/include dsp/src plugins/shared tests/test_helpers dsp/tests"
# tasks.md "Rules for every file of this phase" -> dsp/ fence (FR-019 as amended by P-8): new files and append-only edits.
FENCE_ALLOWED="dsp/include/krate/dsp/systems/profundum_morph_modulator.h
dsp/include/krate/dsp/systems/profundum_engine.h
dsp/include/krate/dsp/systems/profundum_engine_observer.h
dsp/tests/unit/systems/profundum_morph_modulator_test.cpp
dsp/tests/unit/systems/profundum_morph_mod_nonfinite_test.cpp
dsp/include/krate/dsp/primitives/tanh_adaa.h
dsp/include/krate/dsp/core/log_cosh_table.h
dsp/include/krate/dsp/processors/tube_stage.h
dsp/include/krate/dsp/systems/bass_character_stage.h
dsp/include/krate/dsp/systems/shape_morph.h
dsp/tests/unit/primitives/tanh_adaa_test.cpp
dsp/tests/unit/systems/profundum_engine_morphmod_test.cpp
dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h
dsp/tests/CMakeLists.txt
dsp/tests/unit/systems/profundum_engine_perf_test.cpp
dsp/tests/unit/systems/profundum_engine_display_test.cpp
dsp/tests/unit/systems/profundum_voice_engine_helpers_test.cpp"
# Lane (c) seed: the modified headers and the test helpers that reach them (tasks.md T031 3(c)).
CONSUMER_SEED='profundum_engine[.]h|profundum_engine_observer[.]h|profundum_voice_engine_test_helpers[.]h|profundum_audibility_readers[.]h|preset_render_support[.]h|param_path_audibility_helpers[.]h'
CONSUMER_DIRS="dsp/tests/unit/systems plugins/profundum/tests"
T026_STATUS="$SPEC/evidence/library/status.txt"
T026_INPUTS="plugins/profundum tools"
LOGDIR="$SPEC/evidence/run-log/close"
LONGDIR="$LOGDIR/long"
FPDIR="$LOGDIR/fingerprints"
STATUS="$LOGDIR/status.txt"
GATE_ROOT="f:/tmp/gate/profundum-p14-close-$(date +%Y%m%dT%H%M%S)"

# The Phase 13 library [long] names T026 (evidence/library/run-library-lanes.sh) ran and recorded.
LIBRARY_SET="Profundum_PresetShowcase_0of8
Profundum_PresetShowcase_1of8
Profundum_PresetShowcase_2of8
Profundum_PresetShowcase_3of8
Profundum_PresetShowcase_4of8
Profundum_PresetShowcase_5of8
Profundum_PresetShowcase_6of8
Profundum_PresetShowcase_7of8
Profundum_PresetMorphEndpoints_0of8
Profundum_PresetMorphEndpoints_1of8
Profundum_PresetMorphEndpoints_2of8
Profundum_PresetMorphEndpoints_3of8
Profundum_PresetMorphEndpoints_4of8
Profundum_PresetMorphEndpoints_5of8
Profundum_PresetMorphEndpoints_6of8
Profundum_PresetMorphEndpoints_7of8
Profundum_PresetMorphSweep
Profundum_PresetSweep
Profundum_PresetSweepRates
Profundum_PresetTruePeakMaxMass
Profundum_PresetMonoSum
Profundum_PresetDeterminism
Profundum_PresetLoudnessMatch
Profundum_PresetDistinctness"

# Spec SC case names (spec.md SC-001 .. SC-024), "<exe>|<name>", visible in the default listing. Sharded families are
# listed shard by shard (the transparency family is generated below).
CASES="$SYS_EXE|ProfundumMorphModulator_ApiAndRt
$SYS_EXE|ProfundumMorphModulator_Envelope
$SYS_EXE|ProfundumMorphModulator_Cycle
$SYS_EXE|ProfundumEngine_MorphModNoAlloc
$SYS_EXE|ProfundumEngine_MorphModVelocity
$SYS_EXE|ProfundumEngine_MorphModComposition
$SYS_EXE|ProfundumEngine_MorphModTransparentAtZero
$SYS_EXE|ProfundumEngine_MorphModRetrigger
$SYS_EXE|ProfundumEngine_MorphModClickFree_1of8
$SYS_EXE|ProfundumEngine_MorphModClickFree_2of8
$SYS_EXE|ProfundumEngine_MorphModClickFree_3of8
$SYS_EXE|ProfundumEngine_MorphModClickFree_4of8
$SYS_EXE|ProfundumEngine_MorphModClickFree_5of8
$SYS_EXE|ProfundumEngine_MorphModClickFree_6of8
$SYS_EXE|ProfundumEngine_MorphModClickFree_7of8
$SYS_EXE|ProfundumEngine_MorphModClickFree_8of8
$SYS_EXE|ProfundumEngine_MorphModLifecycle
$SYS_EXE|ProfundumEngine_MorphModAnchorMono
$SYS_EXE|Profundum_EngineDisplayAccessorNeutral
$SYS_EXE|VoiceEngineHelpers_DriverSplitsAtEvents
$SYS_EXE|VoiceEngineHelpers_MorphModFields
$EXE|Profundum_Phase11PathsDoNotAllocate
$EXE|Profundum_FactoryPresetStateFormat
$EXE|Profundum_MorphModWheelRouting
$EXE|Profundum_MassPerformanceOffset
$EXE|Profundum_MorphModPedalRestrike
$EXE|Profundum_MidiEventTranslation
$EXE|Profundum_MorphModCompareBypass
$EXE|Profundum_StateV3MorphMod
$EXE|Profundum_EndpointNames
$EXE|Profundum_StateVersions
$EXE|Profundum_StateRoundTrip
$EXE|Profundum_ParamTableConformance
$EXE|Profundum_SoundParamTableRouting
$EXE|Profundum_UidescBindsEverySurfaceId
$EXE|Profundum_UidescTagIntegrity
$EXE|Profundum_UiLayout
$EXE|Profundum_UiPlacement
$EXE|Profundum_UiZoom
$EXE|Profundum_UiLabels
$EXE|Profundum_UiClassRule
$EXE|Profundum_EditorLifecycle
$EXE|Profundum_MorphIndicatorFrame
$EXE|Profundum_UiMorphIndicator
$EXE|Profundum_SpectrumFrameDoesNotChangeAudio
$EXE|Profundum_SpectrumFrameAllocationFree
$EXE|Profundum_PresetShowcase_0of8
$EXE|Profundum_PresetShowcase_1of8
$EXE|Profundum_PresetShowcase_2of8
$EXE|Profundum_PresetShowcase_3of8
$EXE|Profundum_PresetShowcase_4of8
$EXE|Profundum_PresetShowcase_5of8
$EXE|Profundum_PresetShowcase_6of8
$EXE|Profundum_PresetShowcase_7of8
$EXE|Profundum_MainPageAudibility
$EXE|Profundum_PresetMonoSum
$EXE|Profundum_ManualCoversSurface
$EXE|Profundum_MorphModOutsideEndpoints
$EXE|Profundum_CopyEndpoint
$EXE|Profundum_MorphModHarnessFidelity"
for k in $(seq 0 63); do
    CASES="$CASES
$EXE|Profundum_MorphModPresetTransparency_${k}of64"
done
# Hidden SC cases: "<exe>|<hidden tag>|<name>".
HIDDEN_CASES="$EXE|[.harvest]|Profundum_MorphModPrePhaseFingerprints_0of4
$EXE|[.harvest]|Profundum_MorphModPrePhaseFingerprints_1of4
$EXE|[.harvest]|Profundum_MorphModPrePhaseFingerprints_2of4
$EXE|[.harvest]|Profundum_MorphModPrePhaseFingerprints_3of4
$EXE|[.perf]|Profundum_ProcessorCpuMorphMod
$EXE|[.perf]|Profundum_PresetCpu
$SYS_EXE|[.perf]|VoiceEngine_WorstIntervalMorphMod"

CORES="$(nproc 2>/dev/null || echo 8)"
LANE_SHARE="${LANE_SHARE:-4}"
LONG_JOBS="${LONG_JOBS:-$(( (CORES - 2) / 2 ))}"
[ "$LONG_JOBS" -lt 1 ] && LONG_JOBS=1
WALL_WORKERS="${WALL_WORKERS:-4}"

mkdir -p "$LOGDIR" "$LONGDIR" "$FPDIR"
rm -f "$LONGDIR"/*.log "$LONGDIR"/*.txt "$FPDIR"/*.log

now() { date -u +%Y-%m-%dT%H:%M:%SZ; }
status_line() { echo "$*" >> "$STATUS"; }
HEAD_SHORT="$(git rev-parse --short=8 HEAD 2>/dev/null || echo unknown)"

suite_start="$(date +%s)"
echo "START $(now) commit $HEAD_SHORT base $BASE_COMMIT cores $CORES lane_share $LANE_SHARE long_jobs $LONG_JOBS" > "$STATUS"

# ---------------------------------------------------------------------------------------------------------------
# 1. Change set.
# ---------------------------------------------------------------------------------------------------------------
SCOPE_LOG="$LOGDIR/scope-diff.log"
# shellcheck disable=SC2086
{
    echo "=== git diff --name-only $BASE_COMMIT -- $FENCE_PATHS (must stay inside the dsp/ fence)"
    git diff --name-only "$BASE_COMMIT" -- $FENCE_PATHS
    echo "=== git ls-files --others --exclude-standard -- $FENCE_PATHS (new files; must stay inside the dsp/ fence)"
    git ls-files --others --exclude-standard -- $FENCE_PATHS
} > "$SCOPE_LOG" 2>&1

# shellcheck disable=SC2086
fence_changed="$({ git diff --name-only "$BASE_COMMIT" -- $FENCE_PATHS; git ls-files --others --exclude-standard -- $FENCE_PATHS; } \
                     2>/dev/null | tr -d '\r' | grep -v '^$' | sort -u)"
fence_red=""
fence_ok=""
fence_nonphase=""
for c in $(git rev-list --abbrev-commit --abbrev=8 "$BASE_COMMIT..HEAD" 2>/dev/null | tr -d '\r'); do
    known=0
    for k in $NON_PHASE_COMMITS; do
        case "$c" in "$k"*) known=1 ;; esac
    done
    [ "$known" -eq 1 ] || { fence_red="$fence_red commit:$c"; status_line "scope RED post-base commit $c is not a named non-phase commit"; }
done
# shellcheck disable=SC2086
committed_only="$(git diff --name-only "$BASE_COMMIT" HEAD -- $FENCE_PATHS 2>/dev/null | tr -d '\r' | sort -u)"
# shellcheck disable=SC2086
working_vs_head="$({ git diff --name-only HEAD -- $FENCE_PATHS; git ls-files --others --exclude-standard -- $FENCE_PATHS; } \
                     2>/dev/null | tr -d '\r' | sort -u)"
while IFS= read -r p; do
    [ -z "$p" ] && continue
    if printf '%s\n' "$FENCE_ALLOWED" | grep -qxF "$p"; then
        fence_ok="$fence_ok $p"
    elif printf '%s\n' "$committed_only" | grep -qxF "$p" && ! printf '%s\n' "$working_vs_head" | grep -qxF "$p"; then
        fence_nonphase="$fence_nonphase $p"
    else
        fence_red="$fence_red $p"
        status_line "scope RED unexpected path=$p"
    fi
done <<< "$fence_changed"
if [ -n "$fence_red" ]; then
    status_line "scope RED base=$BASE_COMMIT log=$SCOPE_LOG (a shared file outside the T030 dsp/ fence is changed: its consumers' suites are not in this lane)"
    status_line "DONE $(now) RED: change set reaches shared code outside the dsp/ fence; no build, no lane ran"
    tr -d '\r' < "$STATUS" | tail -n 12
    exit 1
fi
status_line "scope GREEN base=$BASE_COMMIT changed=[${fence_ok# }] non_phase_commits_only=[${fence_nonphase# }] log=$SCOPE_LOG"

# ---------------------------------------------------------------------------------------------------------------
# 2. One build.
# ---------------------------------------------------------------------------------------------------------------
BUILD_TARGETS="$SYS_EXE $IEEE_EXE $EXE Profundum $GEN"
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
for f in "$BIN/$EXE.exe" "$BIN/$SYS_EXE.exe" "$BIN/$IEEE_EXE.exe" "$BIN/$GEN.exe"; do
    [ -f "$f" ] || missing_art="$missing_art $f"
done
[ -d "$PROFUNDUM_BUNDLE" ] || missing_art="$missing_art $PROFUNDUM_BUNDLE"
if [ "$compile_errors" -ne 0 ] || [ -n "$missing_art" ]; then
    status_line "DONE $(now) RED: compile/link errors=$compile_errors missing=[${missing_art# }]; no test lane ran (see $BUILD_LOG)"
    tail -n 30 "$BUILD_LOG"
    exit 1
fi

# list_names <exe> <filter> [extra args...]: case names, one per line.
list_names() {
    local exe="$1" filter="$2"
    shift 2
    "$BIN/$exe.exe" "$@" --list-tests "$filter" --verbosity quiet 2>/dev/null | tr -d '\r' | sed 's/^[[:space:]]*//' \
        | grep -v '^$' | grep -viE '^(no test cases matched|filters:)'
}

# ---------------------------------------------------------------------------------------------------------------
# 3a / 3b / 3h. Per-push lanes through tools/run-gate.js.
# ---------------------------------------------------------------------------------------------------------------
# run_gate_lane <lane-name> <filter> <run-gate options...> -- <exe> [<exe>...]
run_gate_lane() {
    local name="$1" filter="$2" s0 s1 rc last opts=() exes=()
    shift 2
    while [ "$#" -gt 0 ] && [ "$1" != "--" ]; do opts+=("$1"); shift; done
    [ "$#" -gt 0 ] && shift
    exes=("$@")
    local log="$LOGDIR/gate-$name.log"
    s0="$(date +%s)"
    node tools/run-gate.js --filter "$filter" --out "$GATE_ROOT/$name" "${opts[@]}" "${exes[@]}" > "$log" 2>&1
    rc=$?
    s1="$(date +%s)"
    last="$(tr -d '\r' < "$log" | grep -v '^[[:space:]]*$' | tail -n 1)"
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    status_line "gate:$name exit=$rc wall=$((s1 - s0))s last=\"$last\" log=$log"
}

# ---------------------------------------------------------------------------------------------------------------
# 3c. The consumers' [long] cases.
# ---------------------------------------------------------------------------------------------------------------
# TU list, closed over the helper headers: a header that includes the pattern adds its own includers.
pat="$CONSUMER_SEED"
while :; do
    # shellcheck disable=SC2086
    consumers="$(grep -rlE "#include .*($pat)" $CONSUMER_DIRS 2>/dev/null | tr -d '\r' | sort -u)"
    hdrs="$(printf '%s\n' "$consumers" | grep -E '\.h$' | xargs -r -n 1 basename | sed 's/[.]/[.]/g' | sort -u | tr '\n' '|' | sed 's/|$//')"
    newpat="$CONSUMER_SEED${hdrs:+|$hdrs}"
    [ "$newpat" = "$pat" ] && break
    pat="$newpat"
done
TU_LIST="$LONGDIR/consumer-tus.txt"
printf '%s\n' "$consumers" | grep -E '\.cpp$' > "$TU_LIST"
tu_count="$(grep -c . "$TU_LIST" || true)"

LONG_ALL="$LONGDIR/long-listed.txt"
LONG_RUN="$LONGDIR/long-run.txt"
: > "$LONG_ALL"
while IFS= read -r tu; do
    [ -z "$tu" ] && continue
    stem="$(basename "$tu" .cpp)"
    case "$tu" in
        dsp/*) tu_exes="$SYS_EXE $IEEE_EXE" ;;
        *)     tu_exes="$EXE" ;;
    esac
    for e in $tu_exes; do
        list_names "$e" "[#$stem][long]~[.]$TIMING_EXCL" -# | sed "s#^#$e|#" >> "$LONG_ALL"
    done
done < "$TU_LIST"
sort -u -o "$LONG_ALL" "$LONG_ALL"

# T026 record: GREEN and newer than every modified / untracked file under plugins/profundum and tools/.
lib_skip=0
lib_skip_why=""
if [ "${FORCE_LONG:-0}" = "1" ]; then
    lib_skip_why="FORCE_LONG=1"
elif [ ! -f "$T026_STATUS" ]; then
    lib_skip_why="no T026 record at $T026_STATUS"
else
    t026_verdict="$(tr -d '\r' < "$T026_STATUS" | grep -v '^[[:space:]]*$' | tail -n 1)"
    stale=""
    # shellcheck disable=SC2086
    while IFS= read -r f; do
        [ -z "$f" ] && continue
        [ -f "$f" ] || continue
        [ "$f" -nt "$T026_STATUS" ] && stale="$stale $f"
    done < <({ git diff --name-only HEAD -- $T026_INPUTS; git ls-files --others --exclude-standard -- $T026_INPUTS; } \
                 2>/dev/null | tr -d '\r' | sort -u)
    if [ "${t026_verdict#verdict GREEN}" != "$t026_verdict" ] && [ -z "$stale" ]; then
        lib_skip=1
        lib_skip_why="T026 record \"$t026_verdict\" newer than every changed file under $T026_INPUTS"
    else
        lib_skip_why="T026 record not reusable: last=\"$t026_verdict\" newer_files=[${stale# }]"
    fi
fi

: > "$LONG_RUN"
lib_skipped=0
while IFS= read -r entry; do
    [ -z "$entry" ] && continue
    n="${entry#*|}"
    if [ "$lib_skip" -eq 1 ] && printf '%s\n' "$LIBRARY_SET" | grep -qxF "$n"; then
        lib_skipped=$((lib_skipped + 1))
        continue
    fi
    echo "$entry" >> "$LONG_RUN"
done < "$LONG_ALL"
long_listed="$(grep -c . "$LONG_ALL" || true)"
long_count="$(grep -c . "$LONG_RUN" || true)"
long_count="${long_count:-0}"
status_line "long-list tus=$tu_count listed=${long_listed:-0} run=$long_count library_skipped=$lib_skipped ($lib_skip_why) jobs=$LONG_JOBS tus=$TU_LIST list=$LONG_RUN"

# run_one_case <log-dir> <status-prefix> "<exe>|<name>": one process for one case; exit 98 when it did not run
# exactly one case (non-vacuity), 99 when no case matched.
run_one_case() {
    local dir="$1" prefix="$2" entry="$3" exe name safe spec log s0 s1 rc
    exe="${entry%%|*}"
    name="${entry#*|}"
    safe="$(printf '%s' "$exe-$name" | tr -c 'A-Za-z0-9_.-' '_' | cut -c1-140)"
    log="$dir/${safe}.log"
    # Catch2 test-spec escaping: backslash, comma, brackets and '*' are special.
    spec="$(printf '%s' "$name" | sed -e 's/\\/\\\\/g' -e 's/,/\\,/g' -e 's/\[/\\[/g' -e 's/\]/\\]/g' -e 's/\*/\\*/g')"
    s0="$(date +%s)"
    "$BIN/$exe.exe" "$spec" -d yes > "$log" 2>&1
    rc=$?
    s1="$(date +%s)"
    if grep -qiE "no test cases matched|No tests ran" "$log"; then
        rc=99
    elif [ "$rc" -eq 0 ] && ! grep -qE "in 1 test case\)|test cases: +1 \|" "$log"; then
        rc=98
    fi
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    echo "$prefix:$exe|$name exit=$rc wall=$((s1 - s0))s log=$log" >> "$STATUS"
    return 0
}
export -f run_one_case
export BIN STATUS

# ---------------------------------------------------------------------------------------------------------------
# 3d. SC-008 pre-phase fingerprint comparison (4 shards).
# ---------------------------------------------------------------------------------------------------------------
run_fingerprints() {
    local k
    for k in 0 1 2 3; do
        run_one_case "$FPDIR" "fingerprint" "$EXE|Profundum_MorphModPrePhaseFingerprints_${k}of4" &
    done
    wait
}

# ---------------------------------------------------------------------------------------------------------------
# 3e. pluginval strictness 5 (SC-021).
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
# 3f. Generator determinism.
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
# 3g. SC case listing.
# ---------------------------------------------------------------------------------------------------------------
run_listing() {
    local s0 s1 rc=0 missing="" leaked="" entry e n tag rest
    local log="$LOGDIR/list-tests.log"
    local visible="$LOGDIR/list-tests.visible.txt"
    local hidden="$LOGDIR/list-tests.hidden.txt"
    s0="$(date +%s)"
    : > "$visible"
    : > "$hidden"
    for e in "$EXE" "$SYS_EXE"; do
        "$BIN/$e.exe" --list-tests --verbosity quiet 2>&1 | tr -d '\r' | sed 's/^[[:space:]]*//' | grep -v '^$' \
            | sed "s#^#$e|#" >> "$visible"
        for tag in "[.harvest]" "[.perf]"; do
            "$BIN/$e.exe" "$tag" --list-tests --verbosity quiet 2>&1 | tr -d '\r' | sed 's/^[[:space:]]*//' \
                | grep -v '^$' | sed "s#^#$e|$tag|#" >> "$hidden"
        done
    done
    while IFS= read -r entry; do
        [ -z "$entry" ] && continue
        grep -qxF "$entry" "$visible" || missing="$missing $entry"
    done <<< "$CASES"
    while IFS= read -r entry; do
        [ -z "$entry" ] && continue
        grep -qxF "$entry" "$hidden" || missing="$missing $entry"
        e="${entry%%|*}"
        rest="${entry#*|}"
        n="${rest#*|}"
        # A hidden case must not be in the default listing.
        if grep -qxF "$e|$n" "$visible"; then
            leaked="$leaked $e|$n"
        fi
    done <<< "$HIDDEN_CASES"
    if [ -n "$missing" ] || [ -n "$leaked" ]; then
        rc=1
    fi
    s1="$(date +%s)"
    {
        echo "=== <exe>.exe --list-tests --verbosity quiet (prefixed with the exe)"
        cat "$visible"
        echo "=== <exe>.exe <hidden tag> --list-tests --verbosity quiet (prefixed with the exe and the tag)"
        cat "$hidden"
        echo "missing=[${missing# }] leaked_hidden=[${leaked# }]"
    } > "$log"
    status_line "list-tests exit=$rc wall=$((s1 - s0))s missing=[${missing# }] leaked_hidden=[${leaked# }] log=$log"
}

lane_start="$(date +%s)"
pids=""
# The [long] lane starts first (longest per-process wall clock).
(
    l0="$(date +%s)"
    if [ "$long_count" -gt 0 ]; then
        tr '\n' '\0' < "$LONG_RUN" | xargs -0 -n 1 -P "$LONG_JOBS" bash -c 'run_one_case "$1" long "$2"' _ "$LONGDIR"
    fi
    l1="$(date +%s)"
    echo "long-lane cases=$long_count wall=$((l1 - l0))s" >> "$STATUS"
) &
pids="$pids $!"
run_fingerprints &
pids="$pids $!"
run_gate_lane "perpush-$EXE" "$PP_FILTER~[wallclock]" --share "$LANE_SHARE" -- "$EXE" &
pids="$pids $!"
run_gate_lane "perpush-dsp" "$DSP_FILTER~[wallclock]" --share "$LANE_SHARE" -- "$SYS_EXE" "$IEEE_EXE" &
pids="$pids $!"
run_listing &
pids="$pids $!"
run_pluginval &
pids="$pids $!"
run_determinism &
pids="$pids $!"

for p in $pids; do
    wait "$p"
done
lane_end="$(date +%s)"

# ---------------------------------------------------------------------------------------------------------------
# 3h. Quiet [wallclock] pass, after every concurrent lane has drained (tidy.sh may still be running: a red wall-clock
#     reading of an untouched case under that load is recorded as load).
# ---------------------------------------------------------------------------------------------------------------
wall_pp_n="$(list_names "$EXE" "$PP_FILTER[wallclock]" | grep -c . || true)"
wall_dsp_exes=()
for e in "$SYS_EXE" "$IEEE_EXE"; do
    n="$(list_names "$e" "$DSP_FILTER[wallclock]" | grep -c . || true)"
    [ "${n:-0}" -gt 0 ] && wall_dsp_exes+=("$e")
done
status_line "wallclock-list $EXE=${wall_pp_n:-0} dsp_exes=[${wall_dsp_exes[*]:-}]"
if [ "${wall_pp_n:-0}" -gt 0 ]; then
    run_gate_lane "wallclock-$EXE" "$PP_FILTER[wallclock]" --workers "$WALL_WORKERS" --shards-per-exe "$((WALL_WORKERS * 2))" -- "$EXE"
fi
if [ "${#wall_dsp_exes[@]}" -gt 0 ]; then
    run_gate_lane "wallclock-dsp" "$DSP_FILTER[wallclock]" --workers "$WALL_WORKERS" --shards-per-exe "$((WALL_WORKERS * 2))" -- "${wall_dsp_exes[@]}"
fi
wall_end="$(date +%s)"

# ---------------------------------------------------------------------------------------------------------------
# 4. Summary.
# ---------------------------------------------------------------------------------------------------------------
st() { tr -d '\r' < "$STATUS"; }
lane_exit() { st | grep -E "^$1 exit=" | tail -n 1 | sed -nE 's/.* exit=([0-9]+).*/\1/p'; }
lane_wall() { st | grep -E "^$1 exit=" | tail -n 1 | sed -nE 's/.* wall=([0-9]+s).*/\1/p'; }
lane_last() { st | grep -E "^$1 exit=" | tail -n 1 | sed -nE 's/.* last="([^"]*)".*/\1/p'; }
verdict_of() { [ "${1:-1}" = "0" ] && echo GREEN || echo RED; }

# gate_ok <lane>: 0 when run-gate exited 0 and printed GATE GREEN; a lane that did not run (no [wallclock] case) is 0.
gate_ok() {
    local rc last
    rc="$(lane_exit "gate:$1")"
    last="$(lane_last "gate:$1")"
    if [ -z "$rc" ]; then
        echo "${2:-1}"
        return
    fi
    case "$last" in
        "GATE GREEN"*) [ "$rc" = "0" ] && { echo 0; return; } ;;
    esac
    echo 1
}

build_ok=1
[ "$warnings" -eq 0 ] && build_ok=0

pp_prof="$(gate_ok "perpush-$EXE")"
pp_dsp="$(gate_ok "perpush-dsp")"
wc_prof="$(gate_ok "wallclock-$EXE" 0)"
wc_dsp="$(gate_ok "wallclock-dsp" 0)"

long_red="$(st | grep -E '^long:' | grep -vE ' exit=0 ' | sed -nE 's/^long:([^ ]+) exit=.*/\1/p' | tr '\n' ' ')"
long_done="$(st | grep -cE '^long:' || true)"
long_ok=0
[ -n "$long_red" ] && long_ok=1
[ "${long_done:-0}" -ne "$long_count" ] && long_ok=1

fp_red="$(st | grep -E '^fingerprint:' | grep -vE ' exit=0 ' | sed -nE 's/^fingerprint:([^ ]+) exit=.*/\1/p' | tr '\n' ' ')"
fp_done="$(st | grep -cE '^fingerprint:' || true)"
fp_ok=0
[ -n "$fp_red" ] && fp_ok=1
[ "${fp_done:-0}" -ne 4 ] && fp_ok=1
fp_lines="$(cat "$FPDIR"/*.log 2>/dev/null | tr -d '\r' | grep -E '^# Profundum_MorphModPrePhaseFingerprints_' | sort)"
fp_fail_lines="$(cat "$FPDIR"/*.log 2>/dev/null | tr -d '\r' | grep -cE '^# (FINGERPRINT FAIL|REGION MISMATCH)' || true)"

list_rc="$(lane_exit list-tests)"
pv_rc="$(lane_exit pluginval:Profundum)"
det_rc="$(lane_exit determinism)"

overall=GREEN
for v in "$build_ok" "$pp_prof" "$pp_dsp" "$long_ok" "$fp_ok" "${list_rc:-1}" "${pv_rc:-1}" "${det_rc:-1}" "$wc_prof" "$wc_dsp"; do
    [ "$v" = "0" ] || overall=RED
done

suite_end="$(date +%s)"
{
    echo "=== summary"
    echo "scope (T030 dsp/ fence)              GREEN base=$BASE_COMMIT"
    echo "build (SC-021, 0 warnings)           $(verdict_of "$build_ok") exit=$build_status warnings=$warnings compile_errors=$compile_errors wall=${build_wall}s"
    echo "(a) per-push $EXE           $(verdict_of "$pp_prof") last=\"$(lane_last "gate:perpush-$EXE")\" wall=$(lane_wall "gate:perpush-$EXE")"
    echo "(b) per-push $SYS_EXE+$IEEE_EXE $(verdict_of "$pp_dsp") last=\"$(lane_last "gate:perpush-dsp")\" wall=$(lane_wall "gate:perpush-dsp")"
    echo "(c) consumers' [long] cases          $(verdict_of "$long_ok") tus=$tu_count run=$long_count done=${long_done:-0} library_skipped=$lib_skipped red=[${long_red% }] $(st | grep -E '^long-lane ' | tail -n 1)"
    echo "(d) SC-008 fingerprints (0 failures) $(verdict_of "$fp_ok") done=${fp_done:-0}/4 red=[${fp_red% }] fail_lines=${fp_fail_lines:-0}"
    printf '%s\n' "$fp_lines" | sed 's/^/    /'
    echo "(e) pluginval Profundum (SC-021)     $(verdict_of "${pv_rc:-1}") exit=${pv_rc:-?} wall=$(lane_wall pluginval:Profundum)"
    echo "(f) generator determinism            $(verdict_of "${det_rc:-1}") exit=${det_rc:-?} last=\"$(lane_last determinism)\" wall=$(lane_wall determinism)"
    echo "(g) list-tests (SC names)            $(verdict_of "${list_rc:-1}") wall=$(lane_wall list-tests)"
    echo "(h) quiet [wallclock] $EXE      $(verdict_of "$wc_prof") cases=${wall_pp_n:-0} last=\"$(lane_last "gate:wallclock-$EXE")\" wall=$(lane_wall "gate:wallclock-$EXE")"
    echo "(h) quiet [wallclock] dsp            $(verdict_of "$wc_dsp") exes=[${wall_dsp_exes[*]:-}] last=\"$(lane_last "gate:wallclock-dsp")\" wall=$(lane_wall "gate:wallclock-dsp")"
    echo "concurrent lanes wall=$((lane_end - lane_start))s wallclock pass wall=$((wall_end - lane_end))s total wall=$((suite_end - suite_start))s"
} >> "$STATUS"
status_line "DONE $(now) $overall"
st | tail -n 22
[ "$overall" = "GREEN" ]
