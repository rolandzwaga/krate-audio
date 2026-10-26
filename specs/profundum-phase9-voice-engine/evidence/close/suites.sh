#!/usr/bin/env bash
# Profundum Phase 9, task T025 close lane: change-set scope check, per-push lane, the phase's own [long] lane and the
# [long] lane of the consumers of the one modified shared header (FR-084, SC-030). Structure copied from
# specs/profundum-phase8-bass-morph/evidence/close/suites.sh, with the scope rule of 2026-10-18 (Phase 6 ruling G9-4,
# root CLAUDE.md "Run only the tests the change set can reach").
#
# Run from the main loop AFTER T022, DETACHED, so it survives the 2 h background-task ceiling (Git Bash, from the
# Bash tool):
#   nohup bash specs/profundum-phase9-voice-engine/evidence/close/suites.sh \
#       > specs/profundum-phase9-voice-engine/evidence/run-log/close/suites.out 2>&1 & disown
# and run tidy.sh concurrently. Watch evidence/run-log/close/status.txt; its last line is "DONE ...".
#
# Steps:
#   1. One rebuild of dsp_systems_tests (wall clock recorded; the count of "warning C|warning:" lines in the build
#      log must be 0).
#   2. Change-set scope (BASE_COMMIT 0ae33b87): git diff --name-only over dsp/include dsp/src plugins
#      tests/test_helpers must be EXACTLY dsp/include/krate/dsp/systems/mass_dynamics.h (T005), and the untracked
#      files under dsp/include must be exactly the five new Phase 9 headers. Any other path is RED and is written to
#      the status file with the consumer executables whose full suites it requires; those consumers are then rebuilt
#      (one build call) and their full per-push suites run in step 3 (lane c'). With the expected diff no consumer
#      executable outside dsp_systems_tests runs (mass_dynamics.h is included by no plugin and by no other dsp_*_tests
#      TU; consumer-cases.js computes its consumers).
#   3. Concurrently:
#      (a) the per-push lane of the phase's executable (also the full per-push suite of the one consumer executable):
#          dsp_systems_tests.exe "~[long]~[performance]~[perf]~[benchmark]~[!benchmark]" -D 15
#          to evidence/run-log/close/perpush-dsp_systems_tests.log (every case's duration printed);
#      (b) the [long] lane of the PHASE'S OWN TUs: one shard per TEST_CASE of
#          dsp_systems_tests.exe --list-tests "[voice-engine][long]~[performance]~[perf]~[benchmark]~[!benchmark]"
#          --verbosity quiet (generated, never hand-kept);
#      (c) the [long] cases of the CONSUMERS of mass_dynamics.h: one shard per name printed by
#          node consumer-cases.js (include-closure derived, cross-checked against the binary);
#      (b) and (c) share one xargs pool, -P min(shards, cores - 4), longest first by the previous run's wall clock
#      (status.prev.txt; unknown shards first), each shard to evidence/run-log/close/long/<case>.log.
#      Never every [long] case of the executable.
#      (c') only on a scope RED: each required consumer's full per-push suite, to
#          evidence/run-log/close/consumer-<exe>.log.
#   4. Summary block: per-shard verdict and wall clock, the worst SC-005 / SC-006 / SC-025 ratio per family, the
#      minimum audibility margin per SC-010 shard, the worst SC-021 dBFS per fs, the soak maxima (SC-013 / SC-014).
# A timing-budget assertion of an untouched (non-Phase-9) case that fails under this concurrent lane is recorded as
# load, not re-run (root CLAUDE.md).
# Status file evidence/run-log/close/status.txt: one line per lane / shard: name, exit code, wall clock (s).
# Exit code: 0 only if every lane is green.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase9-voice-engine"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
BUILD_DIR="build/windows-x64-release"
BIN="$BUILD_DIR/bin/Release"
EXE="dsp_systems_tests"
PERPUSH_FILTER="~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"
LONG_FILTER="[voice-engine][long]~[performance]~[perf]~[benchmark]~[!benchmark]"
BASE_COMMIT="0ae33b87"
SCOPE_PATHS="dsp/include dsp/src plugins tests/test_helpers"
EVID="$SPEC/evidence"
LOGDIR="$EVID/run-log/close"
LONGDIR="$LOGDIR/long"
STATUS="$LOGDIR/status.txt"
STATUS_PREV="$LOGDIR/status.prev.txt"
START_MARK="$LOGDIR/.suites-start"
PERPUSH_LOG="$LOGDIR/perpush-$EXE.log"
CONSUMER_JS="$SPEC/evidence/close/consumer-cases.js"

# The one modified shared header (T005; tasks.md "Untouchable") and the five new Phase 9 headers (untracked), plus
# the G14-12 L1 primitive (2026-10-21, lowpass_biquad_d.h). The 13 further modified headers of the CPU optimisation
# group and the limiter look-ahead are reported as scope RED and drive the consumer lanes (by design).
EXPECTED_TRACKED="dsp/include/krate/dsp/systems/mass_dynamics.h"
ALLOWED_UNTRACKED="dsp/include/krate/dsp/primitives/lowpass_biquad_d.h
dsp/include/krate/dsp/systems/profundum_engine.h
dsp/include/krate/dsp/systems/profundum_engine_observer.h
dsp/include/krate/dsp/systems/profundum_macro_matrix.h
dsp/include/krate/dsp/systems/profundum_motion.h
dsp/include/krate/dsp/systems/profundum_voice.h"

CORES="$(nproc 2>/dev/null || echo 8)"

mkdir -p "$LOGDIR" "$LONGDIR"
# Keep the previous run's shard wall clocks for the longest-first order.
[ -f "$STATUS" ] && cp -f "$STATUS" "$STATUS_PREV"
rm -f "$LONGDIR"/*.log "$LONGDIR"/*.txt "$LOGDIR"/consumer-*.log
touch "$START_MARK"

now() { date -u +%Y-%m-%dT%H:%M:%SZ; }
status_line() { echo "$*" >> "$STATUS"; }

suite_start="$(date +%s)"
echo "START $(now) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown) base $BASE_COMMIT cores $CORES" > "$STATUS"

# ---------------------------------------------------------------------------------------------------------------
# 1. One rebuild of dsp_systems_tests.
# ---------------------------------------------------------------------------------------------------------------
BUILD_LOG="$LOGDIR/build.log"
t0="$(date +%s)"
"$CMAKE" --build "$BUILD_DIR" --config Release --target "$EXE" > "$BUILD_LOG" 2>&1
build_status=$?
t1="$(date +%s)"
warnings="$(grep -ciE "warning C|warning:" "$BUILD_LOG" || true)"
status_line "build exit=$build_status wall=$((t1 - t0))s warnings=${warnings:-0} log=$BUILD_LOG"
if [ "$build_status" -ne 0 ]; then
    status_line "DONE $(now) RED: build failed; no test lane ran (see $BUILD_LOG)"
    tail -n 30 "$BUILD_LOG"
    exit 1
fi
if [ ! -f "$BIN/$EXE.exe" ]; then
    status_line "DONE $(now) RED: $BIN/$EXE.exe missing after the build"
    exit 1
fi

# ---------------------------------------------------------------------------------------------------------------
# 2. Change-set scope (FR-084, SC-030). Recorded before the lanes so a RED is visible while they run.
# ---------------------------------------------------------------------------------------------------------------
# Consumer executables whose full suites a changed path requires.
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
            plugins="disrumpo gradus innexus iterum membrum ruinae seraphis shared vorago" ;;
        plugins/*)
            plugins="$(printf '%s' "$p" | cut -d/ -f2)" ;;
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
    echo "=== git diff --name-only $BASE_COMMIT -- $SCOPE_PATHS (must be exactly $EXPECTED_TRACKED)"
    git diff --name-only "$BASE_COMMIT" -- $SCOPE_PATHS
    echo "=== untracked under dsp/include (must be exactly the five Phase 9 headers)"
    git ls-files --others --exclude-standard -- dsp/include
} > "$SCOPE_LOG" 2>&1
# shellcheck disable=SC2086
tracked_changed="$(git diff --name-only "$BASE_COMMIT" -- $SCOPE_PATHS 2>/dev/null | tr -d '\r' | grep -v '^$' | sort -u)"
untracked="$(git ls-files --others --exclude-standard -- dsp/include | tr -d '\r' | grep -v '^$' | sort -u)"
allowed_sorted="$(printf '%s\n' "$ALLOWED_UNTRACKED" | sort -u)"
unexpected_tracked="$(comm -23 <(printf '%s\n' "$tracked_changed" | grep -v '^$') <(printf '%s\n' "$EXPECTED_TRACKED"))"
missing_tracked="$(comm -13 <(printf '%s\n' "$tracked_changed" | grep -v '^$') <(printf '%s\n' "$EXPECTED_TRACKED"))"
unexpected_untracked="$(comm -23 <(printf '%s\n' "$untracked" | grep -v '^$') <(printf '%s\n' "$allowed_sorted"))"
missing_untracked="$(comm -13 <(printf '%s\n' "$untracked" | grep -v '^$') <(printf '%s\n' "$allowed_sorted"))"
unexpected="$(printf '%s\n%s\n' "$unexpected_tracked" "$unexpected_untracked" | grep -v '^$' | sort -u)"
scope_red=0
if [ -n "$unexpected" ] || [ -n "$missing_tracked" ] || [ -n "$missing_untracked" ]; then
    scope_red=1
fi
# shellcheck disable=SC2086
status_line "scope $( [ "$scope_red" -eq 0 ] && echo GREEN || echo RED ) base=$BASE_COMMIT tracked_changed=[$(echo $tracked_changed)] unexpected=[$(echo $unexpected)] missing_tracked=[$(echo $missing_tracked)] missing_untracked=[$(echo $missing_untracked)] log=$SCOPE_LOG"

CONSUMERS=""
if [ -n "$unexpected" ]; then
    while IFS= read -r p; do
        [ -z "$p" ] && continue
        c="$(consumers_for "$p")"
        status_line "scope RED path=$p requires_full_suites=[$c]"
        CONSUMERS="$CONSUMERS $c"
    done <<< "$unexpected"
fi
# Unique consumer executables, minus the phase's own (lane a is already its full per-push suite).
# shellcheck disable=SC2086
CONSUMERS="$(printf '%s\n' $CONSUMERS | grep -v '^$' | grep -vx "$EXE" | sort -u | tr '\n' ' ')"
CONSUMERS="${CONSUMERS% }"
consumer_build_red=0
if [ -n "$CONSUMERS" ]; then
    CBUILD_LOG="$LOGDIR/build-consumers.log"
    b0="$(date +%s)"
    # shellcheck disable=SC2086
    "$CMAKE" --build "$BUILD_DIR" --config Release --target $CONSUMERS > "$CBUILD_LOG" 2>&1
    cb_status=$?
    b1="$(date +%s)"
    cwarn="$(grep -ciE "warning C|warning:" "$CBUILD_LOG" || true)"
    status_line "build-consumers targets=[$CONSUMERS] exit=$cb_status wall=$((b1 - b0))s warnings=${cwarn:-0} log=$CBUILD_LOG"
    if [ "$cb_status" -ne 0 ] || [ "${cwarn:-0}" -ne 0 ]; then
        consumer_build_red=1
    fi
fi

# ---------------------------------------------------------------------------------------------------------------
# 3a. Per-push lane of the phase's executable.
# ---------------------------------------------------------------------------------------------------------------
run_perpush() {
    local s0 s1 rc
    s0="$(date +%s)"
    "$BIN/$EXE.exe" "$PERPUSH_FILTER" -D 15 > "$PERPUSH_LOG" 2>&1
    rc=$?
    s1="$(date +%s)"
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$PERPUSH_LOG"
    echo "perpush:$EXE exit=$rc wall=$((s1 - s0))s log=$PERPUSH_LOG" >> "$STATUS"
    return "$rc"
}

# ---------------------------------------------------------------------------------------------------------------
# 3b / 3c. Shard lists: the phase's own [long] cases (from the exe) and the consumer [long] cases (consumer-cases.js).
# ---------------------------------------------------------------------------------------------------------------
PHASE_LIST="$LONGDIR/phase-cases.txt"
CONSUMER_LIST="$LONGDIR/consumer-cases.txt"
CONSUMER_ERR="$LONGDIR/consumer-cases.stderr.txt"
LONG_LIST="$LONGDIR/long-cases.txt"
LIST_RAW="$LONGDIR/list-raw.txt"
list_fail=0
if ! "$BIN/$EXE.exe" --list-tests "$LONG_FILTER" --verbosity quiet > "$LIST_RAW" 2>&1; then
    # Catch2 may return the listed count as the exit code; the content check below decides.
    status_line "phase-list exit=nonzero (see $LIST_RAW)"
fi
tr -d '\r' < "$LIST_RAW" | grep -v '^[[:space:]]*$' > "$PHASE_LIST"
phase_count="$(wc -l < "$PHASE_LIST" | tr -d ' ')"
if [ "${phase_count:-0}" -eq 0 ]; then
    # The phase has [long] cases; an empty list is a broken filter or build, never a green.
    list_fail=1
    status_line "phase-list RED: no [voice-engine][long] case listed"
fi

if node "$CONSUMER_JS" --exe "$BIN/$EXE.exe" > "$CONSUMER_LIST" 2> "$CONSUMER_ERR"; then
    :
else
    list_fail=1
    status_line "consumer-list RED: consumer-cases.js exit=nonzero (see $CONSUMER_ERR)"
fi
tr -d '\r' < "$CONSUMER_LIST" | grep -v '^[[:space:]]*$' > "$CONSUMER_LIST.tmp" && mv -f "$CONSUMER_LIST.tmp" "$CONSUMER_LIST"
consumer_case_count="$(wc -l < "$CONSUMER_LIST" | tr -d ' ')"
status_line "consumer-list cases=${consumer_case_count:-0} $(grep -E '^consumer-cases: (include closure|cross-check)' "$CONSUMER_ERR" | tr -d '\r' | tr '\n' ' ')"

# Union, longest first by the previous run's wall clock (unknown = first).
UNION="$LONGDIR/union.txt"
sort -u "$PHASE_LIST" "$CONSUMER_LIST" | grep -v '^$' > "$UNION"
PREV_WALL="$LONGDIR/prev-wall.txt"
if [ -f "$STATUS_PREV" ]; then
    tr -d '\r' < "$STATUS_PREV" | sed -nE 's/^long:(.+) exit=[^ ]+ wall=([0-9]+)s log=.*$/\2\t\1/p' > "$PREV_WALL"
else
    : > "$PREV_WALL"
fi
# FILENAME, not NR == FNR: with an empty prev-wall file the NR == FNR idiom swallows the union (0 shards, 2026-10-21).
awk -F'\t' 'FILENAME == ARGV[1] { if (NF >= 2) w[$2] = $1; next } { print ((($0 in w) ? w[$0] : 999999) "\t" $0) }' "$PREV_WALL" "$UNION" \
    | sort -t "$(printf '\t')" -k1,1nr -k2,2 | cut -f2- > "$LONG_LIST"
long_count="$(wc -l < "$LONG_LIST" | tr -d ' ')"
union_count="$(wc -l < "$UNION" | tr -d ' ')"
if [ "${long_count:-0}" -ne "${union_count:-0}" ]; then
    list_fail=1
    status_line "long-list RED: $long_count scheduled of $union_count in the union (ordering step dropped shards)"
fi

# xargs -P min(shards, cores - 4).
LONG_JOBS=$(( CORES - 4 ))
[ "$LONG_JOBS" -lt 1 ] && LONG_JOBS=1
if [ "${long_count:-0}" -gt 0 ] && [ "$long_count" -lt "$LONG_JOBS" ]; then
    LONG_JOBS="$long_count"
fi
status_line "long-list cases=$long_count (phase $phase_count, consumers ${consumer_case_count:-0}) jobs=$LONG_JOBS list=$LONG_LIST"

run_long_shard() {
    # $1 = exact TEST_CASE name, or "<exe>::<name>" for a consumer case of another executable (T025-3)
    local token="$1"
    local name="$1"
    local exe="$EXE"
    case "$token" in
        *::*) exe="${token%%::*}"; name="${token#*::}" ;;
    esac
    local safe spec log s0 s1 rc
    safe="$(printf '%s' "$token" | tr -c 'A-Za-z0-9_.-' '_' | cut -c1-120)"
    log="$LONGDIR/${safe}.log"
    # Catch2 test-spec escaping: backslash, comma and brackets are special; '*' would be a wildcard.
    spec="$(printf '%s' "$name" | sed -e 's/\\/\\\\/g' -e 's/,/\\,/g' -e 's/\[/\\[/g' -e 's/\]/\\]/g' -e 's/\*/\\*/g')"
    s0="$(date +%s)"
    "$BIN/$exe.exe" "$spec" -d yes > "$log" 2>&1
    rc=$?
    s1="$(date +%s)"
    if grep -qi "no test cases matched" "$log"; then
        rc=99
    fi
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    echo "long:$token exit=$rc wall=$((s1 - s0))s log=$log" >> "$STATUS"
    return 0
}
export -f run_long_shard
export BIN EXE LONGDIR STATUS

# ---------------------------------------------------------------------------------------------------------------
# 3c'. Consumer full per-push suites (scope RED only; empty with the expected diff).
# ---------------------------------------------------------------------------------------------------------------
run_consumer() {
    local exe="$1" log s0 s1 rc
    log="$LOGDIR/consumer-$exe.log"
    s0="$(date +%s)"
    if [ ! -f "$BIN/$exe.exe" ]; then
        echo "missing $BIN/$exe.exe" > "$log"
        rc=98
    else
        "$BIN/$exe.exe" "$PERPUSH_FILTER" > "$log" 2>&1
        rc=$?
    fi
    s1="$(date +%s)"
    echo "exit: $rc wall: $((s1 - s0)) s" >> "$log"
    echo "consumer:$exe exit=$rc wall=$((s1 - s0))s log=$log" >> "$STATUS"
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
    echo "long-lane shards=$long_count wall=$((l1 - l0))s" >> "$STATUS"
) &
long_pid=$!

consumer_pids=""
consumer_count=0
for c in $CONSUMERS; do
    run_consumer "$c" &
    consumer_pids="$consumer_pids $!"
    consumer_count=$((consumer_count + 1))
done

perpush_red=0
wait "$perpush_pid" || perpush_red=1
wait "$long_pid"
for p in $consumer_pids; do
    wait "$p"
done
lane_end="$(date +%s)"
status_line "lanes wall=$((lane_end - lane_start))s"

long_red="$(grep -E '^long:' "$STATUS" | grep -cv ' exit=0 ' || true)"
long_done="$(grep -cE '^long:' "$STATUS" || true)"
if [ "${long_done:-0}" -ne "${long_count:-0}" ]; then
    long_red=$(( ${long_red:-0} + 1 ))
    status_line "long-lane RED: $long_done of $long_count shards reported"
fi
[ "$list_fail" -ne 0 ] && long_red=$(( ${long_red:-0} + 1 ))

consumer_red="$(grep -E '^consumer:' "$STATUS" | grep -cv ' exit=0 ' || true)"
consumer_red=$(( ${consumer_red:-0} + consumer_build_red ))

# ---------------------------------------------------------------------------------------------------------------
# 4. Readings for the user (recorded, not gated here; the lane verdicts above are the gate).
#    SC-005 / SC-006 / SC-025: "SC-00x <case>: N cells, worst gated (a) ratio R (<cell>; bar B), min (b) margin M
#            (<cell>), wall clock S s" (profundum_engine_click_test.cpp VeSummary::print); one line per family (case).
#    SC-010: evidence/audibility-table/<case>.csv (profundum_engine_audibility_test.cpp auReport; columns row,
#            contrast, base, note, descriptor, tap, lo, hi, value, bar, pass, gated, ...); margin = value - N for a
#            gated row whose bar reads ">= N ..."; minimum per shard, >= 0 is inside the bar.
#    SC-021: "SC-021 shard <fs> <alg>: ... worst (a) Sum X dBFS (...), worst (b) output Y dBFS (...)" and the
#            "SC-021(c): ..." line (profundum_engine_aliasing_test.cpp); the worst of (a) / (b) per fs.
#    SC-013 / SC-014: evidence/soak-{mono,poly}.txt (profundum_engine_soak_test.cpp veReport).
#    Only evidence files written by this run (newer than the start marker) are read.
#    Untagged slow cases: -D 15 prints every per-push case's duration ("<s> s: <name>"); those over 15 s are listed.
# ---------------------------------------------------------------------------------------------------------------
{
    echo "=== summary: per-shard verdict and wall clock, longest first (for the next run's sharding)"
    grep -E '^long:' "$STATUS" | sed -E 's/^long:(.+) exit=([^ ]+) wall=([0-9]+)s log=.*$/\3 s \1 exit=\2/' | sort -rn
    echo "=== summary: per-push cases over 15 s (re-tag candidates, T022 rule)"
    if [ -f "$PERPUSH_LOG" ]; then
        tr -d '\r' < "$PERPUSH_LOG" \
            | awk '/^[0-9]+(\.[0-9]+)? s: / { if ($1 + 0 > 15.0) print }' \
            | grep . || echo "(none)"
    fi

    echo "=== summary: SC-005 / SC-006 / SC-025 worst gated (a) ratio and min (b) margin per family (recorded)"
    sc005="$(cat "$LONGDIR"/VoiceEngine_*.log 2>/dev/null | tr -d '\r' | grep -E '^SC-0(05|06|25) [^:]+: [0-9]+ cells' | sort || true)"
    if [ -n "$sc005" ]; then
        printf '%s\n' "$sc005"
    else
        echo "(no SC-005 / SC-006 / SC-025 summary line found in the shard logs)"
    fi

    echo "=== summary: SC-010 minimum audibility margin per shard (recorded; value - bar, >= 0 is inside the bar)"
    au_found=0
    for f in "$EVID"/audibility-table/VoiceEngine_*.csv; do
        [ -f "$f" ] || continue
        [ "$f" -nt "$START_MARK" ] || continue
        au_found=1
        tr -d '\r' < "$f" | awk -v shard="$(basename "$f" .csv)" '
            BEGIN { FPAT = "([^,]*)|(\"([^\"]|\"\")*\")"; minM = 1e300; gated = 0; fails = 0; other = 0 }
            NR == 1 { next }
            {
                for (i = 1; i <= NF; i++) { gsub(/^"|"$/, "", $i); gsub(/""/, "\"", $i) }
                if ($12 != "gated") next
                gated++
                if ($11 != "PASS") fails++
                if (match($10, />= *(-?[0-9.]+([eE][-+]?[0-9]+)?)/, m)) {
                    margin = ($9 + 0) - (m[1] + 0)
                    if (margin < minM) { minM = margin; cell = $1 " " $4 " (" $2 "; " $5 " " $9 " vs " $10 ")" }
                } else {
                    other++
                }
            }
            END {
                if (minM < 1e300)
                    printf "SC010 %s: %d gated rows, %d FAIL, min margin %.4g at %s; %d gated rows with a non-\">=\" bar (see the CSV)\n", shard, gated, fails, minM, cell, other
                else
                    printf "SC010 %s: %d gated rows, %d FAIL, no \">=\" bar parsed; %d other\n", shard, gated, fails, other
            }'
    done
    [ "$au_found" -eq 0 ] && echo "(no audibility-table CSV written by this run)"

    echo "=== summary: SC-021 worst dBFS per fs (recorded; bar -80)"
    sc021="$(cat "$LONGDIR"/VoiceEngine_Aliasing*.log 2>/dev/null | tr -d '\r' | grep -E '^SC-021 shard ' || true)"
    if [ -n "$sc021" ]; then
        printf '%s\n' "$sc021" | awk '
            {
                fs = $3
                a = -1e300; b = -1e300
                if (match($0, /worst \(a\) Sum (-?[0-9.]+|-?inf)/, m)) a = (m[1] ~ /inf/) ? ((m[1] ~ /^-/) ? -1e300 : 1e300) : m[1] + 0
                if (match($0, /worst \(b\) output (-?[0-9.]+|-?inf)/, n)) b = (n[1] ~ /inf/) ? ((n[1] ~ /^-/) ? -1e300 : 1e300) : n[1] + 0
                w = (a > b) ? a : b
                cnt[fs]++
                sh = $4; sub(/:$/, "", sh)
                if (!(fs in worst) || w > worst[fs]) { worst[fs] = w; at[fs] = sh; wa[fs] = a; wb[fs] = b }
            }
            END { for (f in worst) printf "SC021 WORST %s %.2f dBFS (shard %s: (a) Sum %.2f, (b) output %.2f; %d shards)\n", f, worst[f], at[f], wa[f], wb[f], cnt[f] }' \
            | sort
    else
        echo "(no SC-021 shard line found in the shard logs)"
    fi
    cat "$LONGDIR"/VoiceEngine_AliasingDropTail.log 2>/dev/null | tr -d '\r' | grep -E '^SC-021\(c\): ' || true

    echo "=== summary: soak maxima (SC-013 / SC-014; recorded)"
    soak_found=0
    for half in mono poly; do
        f="$EVID/soak-$half.txt"
        [ -f "$f" ] || continue
        [ "$f" -nt "$START_MARK" ] || continue
        soak_found=1
        echo "--- soak-$half"
        tr -d '\r' < "$f" | grep -E '^\[SC-013\] true peak|^\[SC-013\] DC per|^  trend over|^  max core-tap window|^  segments analysed|^endpoint rejections|RED' || true
    done
    [ "$soak_found" -eq 0 ] && echo "(no soak evidence written by this run)"

    echo "=== summary: consumer [long] shards (mass_dynamics.h consumers; per-shard lines above)"
    c_total=0
    c_red=0
    while IFS= read -r n; do
        [ -z "$n" ] && continue
        c_total=$((c_total + 1))
        line="$(grep -F "long:$n exit=" "$STATUS" | head -n 1)"
        case "$line" in
            *" exit=0 "*) ;;
            *) c_red=$((c_red + 1)); echo "consumer shard RED or missing: $n ${line:-(no status line)}" ;;
        esac
    done < "$CONSUMER_LIST"
    echo "consumer shards: $c_total, red $c_red"

    if [ -n "$CONSUMERS" ]; then
        echo "=== summary: consumer full suites (scope RED)"
        grep -E '^consumer:|^build-consumers' "$STATUS"
    fi
} >> "$STATUS"

# ---------------------------------------------------------------------------------------------------------------
# Verdict.
# ---------------------------------------------------------------------------------------------------------------
suite_end="$(date +%s)"
overall=0
[ "${warnings:-0}" -ne 0 ] && overall=1
[ "$perpush_red" -ne 0 ] && overall=1
[ "${long_red:-0}" -ne 0 ] && overall=1
[ "$scope_red" -ne 0 ] && overall=1
[ "${consumer_red:-0}" -ne 0 ] && overall=1
echo "--- perpush-$EXE tail" >> "$STATUS"
tail -n 3 "$PERPUSH_LOG" | tr -d '\r' >> "$STATUS"
status_line "DONE $(now) $( [ "$overall" -eq 0 ] && echo GREEN || echo RED ) warnings=${warnings:-0} scope_red=$scope_red perpush_red=$perpush_red long_red=${long_red:-0}/$long_count consumer_red=${consumer_red:-0}/$consumer_count total_wall=$((suite_end - suite_start))s"
rm -f "$START_MARK"
tail -n 60 "$STATUS"
exit "$overall"
