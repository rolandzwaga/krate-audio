#!/usr/bin/env bash
# Profundum Phase 10, task T029 close lane: clang-tidy over the Profundum plugin (SC-018.4).
# Reconfigure step copied from specs/profundum-phase9-voice-engine/evidence/close/tidy.sh.
#
# Run from the main loop CONCURRENTLY with suites.sh and asan.sh (Git Bash, from anywhere):
#   bash specs/profundum-phase10-plugin-scaffold/evidence/close/tidy.sh
# Steps:
#   1. Reconfigure build/windows-ninja ("$CMAKE" --preset windows-ninja) inside the VS x64 environment (vcvars64; the
#      preset needs it), because the test CMake changed and a stale compile database gives phantom or missing
#      diagnostics. Then check every one of the 14 Profundum TUs is in compile_commands.json (a TU absent from the
#      database would be a false green).
#   2. ./tools/run-clang-tidy.ps1 -Target profundum -BuildDir build/windows-ninja under a 900 s wall-clock ceiling.
#      Not -Target all.
# Output: evidence/run-log/close/tidy.log; last line "TIDY GREEN|RED ...". Green means the script finished inside the
# ceiling and reported 0 warnings and 0 errors over all 14 files. Exit 0 only when green.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$REPO" || exit 2

SPEC="specs/profundum-phase10-plugin-scaffold"
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
NINJA_DIR="build/windows-ninja"
CEILING_S=900
LOGDIR="$SPEC/evidence/run-log/close"
TIDYDIR="$LOGDIR/tidy"
LOG="$LOGDIR/tidy.log"

# The 14 Profundum TUs (tasks.md T030 list; plugins/profundum/CMakeLists.txt and tests/CMakeLists.txt).
TUS="plugins/profundum/src/entry.cpp
plugins/profundum/src/processor/processor.cpp
plugins/profundum/src/controller/controller.cpp
plugins/profundum/tests/vstgui_test_stubs.cpp
plugins/profundum/tests/unit/test_main.cpp
plugins/profundum/tests/unit/processor_bus_test.cpp
plugins/profundum/tests/unit/param_denorm_test.cpp
plugins/profundum/tests/unit/state_roundtrip_test.cpp
plugins/profundum/tests/unit/midi_event_test.cpp
plugins/profundum/tests/unit/lifecycle_test.cpp
plugins/profundum/tests/unit/controller/editor_lifecycle_test.cpp
plugins/profundum/tests/integration/processor_audio_test.cpp
plugins/profundum/tests/integration/param_flow_test.cpp
plugins/profundum/tests/integration/processor_cpu_test.cpp"
TU_COUNT="$(printf '%s\n' "$TUS" | grep -c .)"

mkdir -p "$LOGDIR" "$TIDYDIR"
start="$(date +%s)"
echo "=== T029 tidy $(date -u +%Y-%m-%dT%H:%M:%SZ) commit $(git rev-parse --short HEAD 2>/dev/null || echo unknown)" > "$LOG"

PWSH="$(command -v pwsh || command -v powershell.exe || true)"
for f in "$VSWHERE" "$PWSH"; do
    if [ -z "$f" ] || [ ! -f "$f" ]; then
        echo "TIDY RED: '$f' not found" | tee -a "$LOG"
        exit 2
    fi
done

# ---------------------------------------------------------------------------------------------------------------
# 1. Reconfigure the Ninja tree inside vcvars64.
# ---------------------------------------------------------------------------------------------------------------
# Use the VS install whose cl.exe the Ninja cache already holds (several installs exist on this machine; mixing one
# install's environment with another's cached compiler poisons the configure checks). vswhere only as a fallback.
VS_PATH=""
CACHED_CXX="$(grep -E '^CMAKE_CXX_COMPILER:' "$NINJA_DIR/CMakeCache.txt" 2>/dev/null | head -n 1 | cut -d= -f2- | tr -d '\r')"
case "$CACHED_CXX" in
    */VC/Tools/*) VS_PATH="${CACHED_CXX%%/VC/Tools/*}" ;;
esac
if [ -z "$VS_PATH" ]; then
    VS_PATH="$("$VSWHERE" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 \
        -property installationPath | tr -d '\r' | head -n 1)"
fi
VCVARS_U="$(cygpath -u "$VS_PATH")/VC/Auxiliary/Build/vcvars64.bat"
if [ -z "$VS_PATH" ] || [ ! -f "$VCVARS_U" ]; then
    echo "TIDY RED: vcvars64.bat not found (VS root '$VS_PATH')" | tee -a "$LOG"
    exit 2
fi
VCVARS="$(cygpath -w "$VCVARS_U")"
echo "--- vcvars: $VCVARS" >> "$LOG"
BAT_DIR="$(mktemp -d)"
BAT="$BAT_DIR/reconfigure-ninja.bat"
cat > "$BAT" <<EOF
@echo off
call "$VCVARS" >nul
if errorlevel 1 exit /b 9
cd /d "$(cygpath -w "$REPO")"
"$(cygpath -w "$CMAKE")" --preset windows-ninja
exit /b %ERRORLEVEL%
EOF
c0="$(date +%s)"
cmd //c "$(cygpath -w "$BAT")" > "$TIDYDIR/reconfigure.out" 2>&1
cfg_status=$?
c1="$(date +%s)"
rm -rf "$BAT_DIR"
{
    echo "--- reconfigure (cmake --preset windows-ninja in vcvars64) exit=$cfg_status wall=$((c1 - c0))s"
    tr -d '\r' < "$TIDYDIR/reconfigure.out" | tail -n 5
} >> "$LOG"
if [ "$cfg_status" -ne 0 ]; then
    echo "TIDY RED: reconfigure failed (exit $cfg_status); see $TIDYDIR/reconfigure.out" | tee -a "$LOG"
    exit 1
fi

DB="$NINJA_DIR/compile_commands.json"
# The database holds JSON-escaped Windows paths (F:\\projects\\...); squeeze every run of backslashes (octal 134)
# into one '/' before matching.
DB_NORM="$TIDYDIR/compile_commands.norm"
tr -s '\134' '/' < "$DB" > "$DB_NORM"
missing_db=""
while IFS= read -r tu; do
    [ -z "$tu" ] && continue
    if ! grep -qF "/$tu\"" "$DB_NORM"; then
        missing_db="$missing_db $tu"
    fi
done <<< "$TUS"
if [ -n "$missing_db" ]; then
    echo "TIDY RED: not in $DB (would be a false green):$missing_db" | tee -a "$LOG"
    exit 1
fi
echo "--- compile database: all $TU_COUNT Profundum TUs present" >> "$LOG"

# ---------------------------------------------------------------------------------------------------------------
# 2. run-clang-tidy.ps1 -Target profundum, 900 s ceiling.
# ---------------------------------------------------------------------------------------------------------------
TIDY_OUT="$TIDYDIR/run-clang-tidy.out"
t0="$(date +%s)"
timeout --kill-after=10 "$CEILING_S" "$PWSH" -NoProfile -ExecutionPolicy Bypass -File ./tools/run-clang-tidy.ps1 \
    -Target profundum -BuildDir "$NINJA_DIR" > "$TIDY_OUT" 2>&1
rc=$?
t1="$(date +%s)"
tr -d '\r' < "$TIDY_OUT" >> "$LOG"

files="$(tr -d '\r' < "$TIDY_OUT" | sed -nE 's/.*Files analyzed: ([0-9]+).*/\1/p' | tail -n 1)"
warns="$(tr -d '\r' < "$TIDY_OUT" | sed -nE 's/.*[[:space:]]Warnings: ([0-9]+).*/\1/p' | tail -n 1)"
errs="$(tr -d '\r' < "$TIDY_OUT" | sed -nE 's/.*[[:space:]]Errors: ([0-9]+).*/\1/p' | tail -n 1)"
killed="$(tr -d '\r' < "$TIDY_OUT" | grep -c 'exceeded .* and was killed' || true)"

verdict="GREEN"
reason=""
if [ "$rc" = "124" ] || [ "$rc" = "137" ]; then
    verdict="RED"; reason="$reason ceiling(${CEILING_S}s)"
fi
if [ "$rc" != "0" ]; then
    verdict="RED"; reason="$reason exit=$rc"
fi
if [ -z "$warns" ] || [ -z "$errs" ] || [ -z "$files" ]; then
    verdict="RED"; reason="$reason summary-missing"
fi
if [ "${warns:-1}" != "0" ] || [ "${errs:-1}" != "0" ]; then
    verdict="RED"; reason="$reason diagnostics"
fi
if [ "${killed:-0}" -ne 0 ]; then
    verdict="RED"; reason="$reason hung-TU=$killed"
fi
# run-clang-tidy.ps1 may also pick up non-TU sources under the two dirs; fewer than the 14 is a false green.
if [ -n "$files" ] && [ "$files" -lt "$TU_COUNT" ]; then
    verdict="RED"; reason="$reason files=$files<$TU_COUNT"
fi

end="$(date +%s)"
echo "TIDY $verdict files=${files:-?} warnings=${warns:-?} errors=${errs:-?} exit=$rc tidy_wall=$((t1 - t0))s total_wall=$((end - start))s${reason:+ reason=[${reason# }]}" >> "$LOG"
tail -n 12 "$LOG"
[ "$verdict" = "GREEN" ]
