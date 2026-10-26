#!/usr/bin/env bash
# ==============================================================================
# Profundum Phase 12 - D3 / D4 table regeneration (T033; FR-005, FR-007; plan 3.2)
# ==============================================================================
# 1. Builds profundum_tests once (Release, MSBuild node reuse off).
# 2. Runs the hidden probe Profundum_UiContractTables ([.][probe]) to a log.
# 3. regen-tables.js (next to this script) splits the two CSVs out of the log and
#    replaces the text between the GENERATED-BEGIN / GENERATED-END markers of
#    design/macro-mapping.md (macro-rows) and design/morph-laws.md (law-table).
# 4. Prints git diff --stat of the two docs.
# Last line: TABLES IDENTICAL | TABLES CHANGED. A CHANGED result is a review item,
# not an automatic accept.
#
# Usage (from anywhere inside the repository):
#   bash specs/profundum-phase12-ui/evidence/close/regen-tables.sh
# ==============================================================================
set -euo pipefail

ROOT="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
CMAKE="${CMAKE:-/c/Program Files/CMake/bin/cmake.exe}"
BUILD_DIR="$ROOT/build/windows-x64-release"
EXE="$BUILD_DIR/bin/Release/profundum_tests.exe"
SPEC="$ROOT/specs/profundum-phase12-ui"
HELPER="$SPEC/evidence/close/regen-tables.js"
LOG_DIR="$SPEC/evidence/run-log/close"
BUILD_LOG="$LOG_DIR/regen-tables-build.log"
PROBE_LOG="$LOG_DIR/regen-tables-probe.log"
D3_DOC="$SPEC/design/macro-mapping.md"
D4_DOC="$SPEC/design/morph-laws.md"

mkdir -p "$LOG_DIR"

echo "[regen-tables] building profundum_tests -> $BUILD_LOG"
start=$(date +%s)
if ! "$CMAKE" --build "$BUILD_DIR" --config Release --target profundum_tests -- /nr:false >"$BUILD_LOG" 2>&1; then
    tail -20 "$BUILD_LOG"
    echo "[regen-tables] build FAILED (see $BUILD_LOG)"
    exit 1
fi
echo "[regen-tables] build done in $(( $(date +%s) - start )) s"

echo "[regen-tables] running Profundum_UiContractTables -> $PROBE_LOG"
if ! "$EXE" "Profundum_UiContractTables" >"$PROBE_LOG" 2>&1; then
    tail -20 "$PROBE_LOG"
    echo "[regen-tables] probe FAILED (see $PROBE_LOG)"
    exit 1
fi

verdict="$(node "$HELPER" "$PROBE_LOG" "$LOG_DIR" "$D3_DOC" "$D4_DOC" | tee /dev/stderr | tail -1)"

echo "[regen-tables] git diff --stat of the two docs:"
git -C "$ROOT" diff --stat -- "$D3_DOC" "$D4_DOC"

case "$verdict" in
    IDENTICAL) echo "TABLES IDENTICAL" ;;
    CHANGED) echo "TABLES CHANGED" ;;
    *)
        echo "[regen-tables] unexpected helper verdict: $verdict"
        exit 1
        ;;
esac
