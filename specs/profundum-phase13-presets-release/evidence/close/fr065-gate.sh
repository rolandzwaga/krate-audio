#!/usr/bin/env bash
# Profundum Phase 13 T037, FR-065 scoped gate (user ruling 2026-10-24): preset descriptions touch plugins/shared
# (preset_manager.cpp, preset_browser_view.{h,cpp}). Gate = full shared_tests + in every plugin exe, the cases of each
# test file that references PresetManager / scanPresets / PresetBrowserView / readMetadata (Catch2 -# file tags) and
# every [lifecycle] case (the editor builds the browser view) + pluginval strictness 5 on all 9 plugins.
# Never the other plugins' full suites. clang-tidy on the changed shared TUs runs separately (clang-tidy -p
# build/windows-ninja; run-clang-tidy.ps1 has no shared target).
# Revision 2 (2026-10-25): run 1 used "[preset],[browser],[lifecycle] ~[perf]...". Catch2 ORs the comma terms and the
# space-separated exclusions bind to the last term only, so every [preset] case ran with no exclusion (Vorago's
# [long] 420 s factory sweep, which loads its defs directly and never reaches the changed code; stopped at 2 h 13 min).
# Each term now carries its own exclusions, and the selection is by file, not by tag.
# Prerequisite: one build of the 9 plugins, their test exes and shared_tests (main loop).
# Run from the repo root. Last line: FR065 GATE GREEN | FR065 GATE RED <reasons>.
set -u
BIN="build/windows-x64-release/bin/Release"
VST="build/windows-x64-release/VST3/Release"
OUT="specs/profundum-phase13-presets-release/evidence/run-log/close/fr065"
mkdir -p "$OUT"
EXCL="~[.]~[performance]~[perf]~[benchmark]~[!benchmark]"   # ~[.]: a file tag also matches hidden cases ([.install] writes ProgramData)
declare -A EXES=([iterum]=plugin_tests [disrumpo]=disrumpo_tests [ruinae]=ruinae_tests [innexus]=innexus_tests
                 [gradus]=gradus_tests [membrum]=membrum_tests [seraphis]=seraphis_tests [vorago]=vorago_tests
                 [profundum]=profundum_tests)
PLUGINS=(Iterum Disrumpo Ruinae Innexus Gradus Membrum Seraphis Vorago Profundum)

# The spec for plugin $1: "[#file]EXCL,...,[lifecycle]EXCL" over the files that reach the changed shared code.
spec_for() {
    local terms=() f
    while IFS= read -r f; do
        terms+=("[#$(basename "$f" .cpp)]$EXCL")
    done < <(grep -rlE "PresetManager|scanPresets|PresetBrowserView|readMetadata" "plugins/$1/tests" --include='*.cpp' | sort)
    terms+=("[lifecycle]$EXCL")
    local IFS=,
    echo "${terms[*]}"
}

t0=$(date +%s)
pids=()
names=()
"$BIN/shared_tests.exe" > "$OUT/shared_tests.log" 2>&1 &
pids+=($!); names+=("shared_tests")
for p in "${!EXES[@]}"; do
    spec="$(spec_for "$p")"
    { echo "spec: $spec"; "$BIN/${EXES[$p]}.exe" -# "$spec"; } > "$OUT/$p.log" 2>&1 &
    pids+=($!); names+=("$p")
done
for pl in "${PLUGINS[@]}"; do
    tools/pluginval.exe --strictness-level 5 --validate "$VST/$pl.vst3" > "$OUT/pluginval-$pl.log" 2>&1 &
    pids+=($!); names+=("pluginval-$pl")
done

red=()
for i in "${!pids[@]}"; do
    wait "${pids[$i]}"
    rc=$?
    n="${names[$i]}"
    last="$(tr -d '\r' < "$OUT/$n.log" | grep -v '^[[:space:]]*$' | tail -n 1)"
    echo "$n exit=$rc last=\"$last\""
    if [[ $rc -ne 0 ]]; then red+=("$n"); fi
    if [[ "$n" != pluginval-* && "$n" != shared_tests ]] && grep -q 'No tests ran' "$OUT/$n.log"; then red+=("$n(no cases)"); fi
done
echo "wall=$(( $(date +%s) - t0 ))s"
if [[ ${#red[@]} -eq 0 ]]; then echo "FR065 GATE GREEN"; else echo "FR065 GATE RED ${red[*]}"; fi
