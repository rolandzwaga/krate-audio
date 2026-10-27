#!/usr/bin/env bash
# Profundum Phase 14, task T033 close lane: macOS fast-math repro of the phase's DSP TUs (FR-054; root CLAUDE.md
# "Fast-math (the macOS leg)"; dsp/CLAUDE.md "The macOS leg builds with -ffast-math").
#
# Run from the main loop (Git Bash on Windows, UNSANDBOXED: WSL must see /mnt/f; or from inside WSL itself):
#   bash specs/profundum-phase14-morph-modulation/evidence/close/fastmath-repro.sh
# It drives tools/macos-fastmath-repro.sh (read its header, LIMITS included: x86 upstream clang, not Apple clang on
# arm64; green is evidence, not proof; plugin tests are out of its scope) over the five phase TUs
#   systems/profundum_morph_modulator_test.cpp systems/profundum_engine_morphmod_test.cpp
#   systems/profundum_voice_engine_helpers_test.cpp systems/profundum_engine_display_test.cpp
#   systems/profundum_engine_test.cpp
# with the Catch2 filter "[profundum]~[long]~[.]", twice:
#   mac  (-ffast-math -ffp-contract=fast)                          -> evidence/run-log/close/fastmath.log
#   ieee (--ieee: -fno-fast-math -fno-finite-math-only -ffp-contract=off, the control) -> .../fastmath-ieee.log
# A case red in mac and green in ieee is a macOS red to fix before push. A case red in both is a red on every
# platform (or a harness difference) and an ieee-only red is a control red; both are RED too. A repo-path
# -Wnan-infinity-disabled hit in a TU under test (infinity / NaN constant in fast-math code) is RED.
#
# Scheduling: the repro caches objects per mode under $HOME/.krate-fastmath-repro/{mac,ieee}, but the Catch2 / pffft
# / shim / alloc-owner support objects are shared and built on first use without a lock. When they all exist (warm)
# the two modes run CONCURRENTLY; on a cold cache mac runs first (it builds the shared objects), then ieee.
# Before either run, a cached KrateDSP library object older than its .cpp (dsp/CMakeLists.txt list) is deleted so it
# rebuilds (the repro itself never invalidates; no lib .cpp changed in this phase, so this is a guard only).
# Catch2 writes a JUnit file per mode next to the log (parsed for the failing case names, then deleted). Each Linux
# command goes through a transient .fastmath-<name>.cmd.sh in the log dir (deleted after it runs).
# Each run has a CEILING_S wall-clock ceiling (default 3600 s). Afterwards `wsl --shutdown` (WSLg traces otherwise
# keep writing to C:), skipped with a note while another wsl.exe is still running (a concurrent portability lane).
# Output: the two logs; fastmath.log ends with the summary block, whose last line is "FASTMATH GREEN|RED ...".
# Exit 0 only when green.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase14-morph-modulation"
LOGDIR="$SPEC/evidence/run-log/close"
LOG_MAC="$LOGDIR/fastmath.log"
LOG_IEEE="$LOGDIR/fastmath-ieee.log"
CEILING_S="${CEILING_S:-3600}"
FILTER='[profundum]~[long]~[.]'
TUS=(
    systems/profundum_morph_modulator_test.cpp
    systems/profundum_engine_morphmod_test.cpp
    systems/profundum_voice_engine_helpers_test.cpp
    systems/profundum_engine_display_test.cpp
    systems/profundum_engine_test.cpp
)
mkdir -p "$LOGDIR"

fail_early() { # message
    { echo "$1"; echo "FASTMATH RED ($1)"; } | tee -a "$LOG_MAC"
    exit 2
}
: > "$LOG_MAC"
: > "$LOG_IEEE"
[[ -f tools/macos-fastmath-repro.sh ]] || fail_early "missing tools/macos-fastmath-repro.sh"
for tu in "${TUS[@]}"; do
    [[ -f "dsp/tests/unit/$tu" ]] || fail_early "missing TU dsp/tests/unit/$tu"
done

# Where the repro runs: directly when this shell is Linux (WSL), else through wsl.exe with the /mnt path.
if [[ "$(uname -s)" == Linux ]]; then
    IN_WSL=1
    WREPO="$REPO"
else
    IN_WSL=0
    WREPO="$(printf '%s' "$REPO" | sed -E 's#^/([A-Za-z])/#/mnt/\L\1/#')"
    [[ "$WREPO" == /mnt/* ]] || fail_early "cannot map $REPO to a WSL path"
fi

# Runs one command string in a Linux login shell. The string goes through a script file in LOGDIR, so no quoting has
# to survive the Windows command line; wsl.exe runs under MSYS_NO_PATHCONV so Git Bash leaves the /mnt path alone.
in_linux() { # ceiling_s name command-string
    local f="$LOGDIR/.fastmath-$2.cmd.sh" rc
    printf '%s\n' "$3" > "$f"
    if [[ "$IN_WSL" == 1 ]]; then
        timeout "$1" bash -l "$WREPO/$f"
    else
        MSYS_NO_PATHCONV=1 timeout "$1" wsl -e bash -l "$WREPO/$f"
    fi
    rc=$?
    rm -f "$f"
    return "$rc"
}

# Executed inside Linux (shipped with declare -f): drop stale cached library objects, report whether the shared
# support objects exist.
remote_prep() { # <repo path in linux>
    local R=$1 OUT=$HOME/.krate-fastmath-repro m s o f warm=1
    command -v clang++ > /dev/null || { echo "NO_CLANG"; return 3; }
    for m in mac ieee; do
        for s in $(sed -n 's#^\s*\(include/krate/dsp/.*\.cpp\)\s*$#\1#p' "$R/dsp/CMakeLists.txt" | sort -u); do
            o="$OUT/$m/lib_$(basename "$s" .cpp).o"
            if [ -f "$o" ] && [ "$R/dsp/$s" -nt "$o" ]; then
                rm -f "$o"
                echo "stale cache: $m/lib_$(basename "$s" .cpp).o removed ($s is newer)"
            fi
        done
    done
    for f in catch.o pffft.o pffft_common.o shim/catch_amalgamated.hpp mac/alloc_owner.o ieee/alloc_owner.o; do
        [ -f "$OUT/$f" ] || warm=0
    done
    echo "WARM=$warm"
}

run_mode() { # mode(mac|ieee) log junit-relative-path
    local mode=$1 log=$2 junit=$3 flag="" cmd t0=$SECONDS rc
    [[ "$mode" == ieee ]] && flag="--ieee"
    rm -f "$junit"
    cmd="bash $(printf '%q' "$WREPO/tools/macos-fastmath-repro.sh") $flag $(printf '%q ' "${TUS[@]}")-- \
$(printf '%q' "$FILTER") --reporter console --reporter $(printf '%q' "junit::out=$WREPO/$junit")"
    {
        echo "=== T033 fastmath $mode $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
        echo "\$ $cmd"
    } >> "$log"
    in_linux "$CEILING_S" "$mode" "$cmd" >> "$log" 2>&1
    rc=$?
    echo "mode=$mode exit=$rc wall=$((SECONDS - t0))s (124 = ${CEILING_S} s ceiling hit)" >> "$log"
    return "$rc"
}

# Top-level case names with a <failure> or <error> in a Catch2 JUnit file ("Case/Section" -> "Case").
failed_cases() { # junit
    [[ -f "$1" ]] || return 0
    awk '/<testcase /{ n=$0; sub(/.* name="/, "", n); sub(/".*/, "", n); sub(/\/.*/, "", n); cur=n }
         /<failure|<error/{ if (cur != "") print cur }' "$1" | sort -u
}

start=$SECONDS
JUNIT_MAC="$LOGDIR/fastmath.junit.xml"
JUNIT_IEEE="$LOGDIR/fastmath-ieee.junit.xml"

PREP_OUT="$(in_linux 300 prep "$(declare -f remote_prep); remote_prep $(printf '%q' "$WREPO")" 2>&1)"
PREP_RC=$?
echo "$PREP_OUT" >> "$LOG_MAC"
if [[ $PREP_RC -ne 0 ]]; then
    if grep -q "Failed to translate" <<< "$PREP_OUT"; then
        fail_early "wsl cannot see the repo drive (sandboxed run): re-run unsandboxed"
    fi
    fail_early "linux prep failed rc=$PREP_RC (NO_CLANG = clang++ not on the WSL PATH)"
fi

if grep -q '^WARM=1' <<< "$PREP_OUT"; then
    SCHEDULE="concurrent (warm support-object cache)"
    run_mode mac "$LOG_MAC" "$JUNIT_MAC" & PID_MAC=$!
    run_mode ieee "$LOG_IEEE" "$JUNIT_IEEE" & PID_IEEE=$!
    wait "$PID_MAC"; RC_MAC=$?
    wait "$PID_IEEE"; RC_IEEE=$?
else
    SCHEDULE="sequential (cold support-object cache: mac builds it first)"
    run_mode mac "$LOG_MAC" "$JUNIT_MAC"; RC_MAC=$?
    run_mode ieee "$LOG_IEEE" "$JUNIT_IEEE"; RC_IEEE=$?
fi

# wsl --shutdown, unless another wsl.exe (a concurrent lane) still needs the VM.
if [[ "$IN_WSL" == 1 ]]; then
    SHUTDOWN="skipped: running inside WSL; run 'wsl --shutdown' from Windows afterwards"
else
    OTHERS="$(MSYS_NO_PATHCONV=1 tasklist.exe /FI "IMAGENAME eq wsl.exe" /NH 2>/dev/null | grep -ci '^wsl\.exe' || true)"
    if [[ "${OTHERS:-0}" -gt 0 ]]; then
        SHUTDOWN="skipped: $OTHERS other wsl.exe still running; run 'wsl --shutdown' once they end"
    else
        wsl --shutdown > /dev/null 2>&1
        SHUTDOWN="done"
    fi
fi

mapfile -t FAIL_MAC < <(failed_cases "$JUNIT_MAC")
mapfile -t FAIL_IEEE < <(failed_cases "$JUNIT_IEEE")
FASTMATH_ONLY="$(comm -23 <(printf '%s\n' "${FAIL_MAC[@]}" | sed '/^$/d' | sort -u) <(printf '%s\n' "${FAIL_IEEE[@]}" | sed '/^$/d' | sort -u))"
BOTH="$(comm -12 <(printf '%s\n' "${FAIL_MAC[@]}" | sed '/^$/d' | sort -u) <(printf '%s\n' "${FAIL_IEEE[@]}" | sed '/^$/d' | sort -u))"
IEEE_ONLY="$(comm -13 <(printf '%s\n' "${FAIL_MAC[@]}" | sed '/^$/d' | sort -u) <(printf '%s\n' "${FAIL_IEEE[@]}" | sed '/^$/d' | sort -u))"
SUM_MAC="$(grep -E '^(All tests passed|test cases:)' "$LOG_MAC" | tail -1)"
SUM_IEEE="$(grep -E '^(All tests passed|test cases:)' "$LOG_IEEE" | tail -1)"
# The repro prints the -Wnan-infinity-disabled lines with $ROOT/ stripped: repo-path hits start with dsp|tests|plugins/.
NANINF="$(grep -E '^(dsp|tests|plugins)/[^ ]*: warning: .*nan-infinity-disabled' "$LOG_MAC" || true)"
JUNIT_MISSING=""
[[ -f "$JUNIT_MAC" ]] || JUNIT_MISSING="$JUNIT_MISSING mac"
[[ -f "$JUNIT_IEEE" ]] || JUNIT_MISSING="$JUNIT_MISSING ieee"
rm -f "$JUNIT_MAC" "$JUNIT_IEEE"

RED=""
[[ "$RC_MAC" == 0 ]] || RED="$RED mac=$RC_MAC"
[[ "$RC_IEEE" == 0 ]] || RED="$RED ieee=$RC_IEEE"
[[ -n "$SUM_MAC" ]] || RED="$RED mac=no-catch2-summary"
[[ -n "$SUM_IEEE" ]] || RED="$RED ieee=no-catch2-summary"
[[ -z "$JUNIT_MISSING" ]] || RED="$RED junit-missing:${JUNIT_MISSING# }"
[[ -z "$FASTMATH_ONLY" ]] || RED="$RED fast-math-only-reds"
[[ -z "$BOTH" ]] || RED="$RED reds-in-both-modes"
[[ -z "$IEEE_ONLY" ]] || RED="$RED ieee-only-reds"
[[ -z "$NANINF" ]] || RED="$RED nan-infinity-disabled"
if grep -q "Failed to translate" "$LOG_MAC" "$LOG_IEEE"; then RED="$RED wsl-no-drive(sandbox)"; fi

{
    echo
    echo "=== T033 fastmath summary $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "TUs (${#TUS[@]}): ${TUS[*]}"
    echo "filter: $FILTER"
    echo "schedule: $SCHEDULE"
    echo "mac  : exit=$RC_MAC  ${SUM_MAC:-(no Catch2 summary line)}"
    echo "ieee : exit=$RC_IEEE  ${SUM_IEEE:-(no Catch2 summary line)}"
    echo "fast-math-only reds (red in mac, green in ieee: macOS reds to fix before push):"
    if [[ -n "$FASTMATH_ONLY" ]]; then echo "$FASTMATH_ONLY" | sed 's/^/  /'; else echo "  none"; fi
    echo "reds in both modes:"
    if [[ -n "$BOTH" ]]; then echo "$BOTH" | sed 's/^/  /'; else echo "  none"; fi
    echo "ieee-only reds:"
    if [[ -n "$IEEE_ONLY" ]]; then echo "$IEEE_ONLY" | sed 's/^/  /'; else echo "  none"; fi
    echo "-Wnan-infinity-disabled hits in repo files:"
    if [[ -n "$NANINF" ]]; then echo "$NANINF" | sed 's/^/  /'; else echo "  none"; fi
    echo "wsl --shutdown: $SHUTDOWN"
    echo "total wall $((SECONDS - start))s"
    if [[ -z "$RED" ]]; then
        echo "FASTMATH GREEN"
    else
        echo "FASTMATH RED ($RED )"
    fi
} >> "$LOG_MAC"

sed -n '/^=== T033 fastmath summary/,$p' "$LOG_MAC"
[[ -z "$RED" ]]
