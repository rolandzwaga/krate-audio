#!/usr/bin/env bash
# T028 (SC-018.2, plan §5.5): registration greps, SC-018 extra greps and the FR inspection checklist.
# Prints `<pattern> <count> <expected> PASS|FAIL` per row; last line `REGISTRATION GREEN|RED`.
set -u
cd "$(dirname "$0")/../../../.." || exit 2
FAILS=0

# row <label> <count> <op> <n>   op: ge | eq
row() {
  local label="$1" count="$2" op="$3" n="$4" verdict=FAIL exp
  if [ "$op" = ge ]; then exp=">=$n"; [ "$count" -ge "$n" ] && verdict=PASS
  else exp="$n"; [ "$count" -eq "$n" ] && verdict=PASS; fi
  [ "$verdict" = FAIL ] && FAILS=$((FAILS + 1))
  echo "$label $count $exp $verdict"
}
# fixed-string line count in one file
fc() { grep -cF -- "$2" "$1" 2>/dev/null || true; }

echo "== SC-018.2 =="
row "ci.yml:regex'for area in .*profundum'" "$(grep -cE 'for area in .*profundum' .github/workflows/ci.yml || true)" ge 1
row "ci.yml:'auval -v aumu Prfd KrAt'" "$(fc .github/workflows/ci.yml 'auval -v aumu Prfd KrAt')" ge 1
for p in 'Profundum-Windows-x64' 'Profundum-macOS' 'Profundum-Linux-x64' 'Profundum AUv3.app'; do
  row "ci.yml:'$p'" "$(fc .github/workflows/ci.yml "$p")" ge 1
done
for p in '/plugins/profundum/src/version.h' '/plugins/profundum/resources/win32resource.rc' '/plugins/profundum/resources/auv3/audiounitconfig.h'; do
  row ".gitignore:'$p'" "$(grep -cxF -- "$p" .gitignore || true)" eq 1
done
row "run-cpu-tests.js:\"'profundum_tests'\"" "$(fc tools/run-cpu-tests.js "'profundum_tests'")" ge 1
row "run-close-lanes.js:\"'profundum_tests'\"" "$(fc tools/run-close-lanes.js "'profundum_tests'")" ge 1
row "ci-test-shard.js:\"['profundum', 'Profundum']\"" "$(fc tools/ci-test-shard.js "['profundum', 'Profundum']")" eq 1
row "CLAUDE.md:'Profundum.vst3'" "$(fc CLAUDE.md 'Profundum.vst3')" ge 2
row "CLAUDE.md:'--target profundum_tests'" "$(fc CLAUDE.md '--target profundum_tests')" ge 1
row "CLAUDE.md:'plugins/profundum/CLAUDE.md'" "$(fc CLAUDE.md 'plugins/profundum/CLAUDE.md')" ge 1
row "CLAUDE.md:'|profundum'" "$(fc CLAUDE.md '|profundum')" ge 1
for p in 'Add Profundum parameter' 'Add Profundum test' 'Change Profundum UI'; do
  row "CLAUDE.md:'$p'" "$(fc CLAUDE.md "$p")" eq 1
done
row "CLAUDE.md:'**Profundum**'" "$(fc CLAUDE.md '**Profundum**')" ge 1

echo "== SC-018 extra greps =="
row "CLAUDE.md:'Plugins today:.*profundum'" "$(tr '\n' ' ' < CLAUDE.md | grep -c 'Plugins today:.*profundum' || true)" ge 1
row "run-clang-tidy.ps1:all-block:plugins/profundum/(src|tests)" "$(awk '/"all" *\{/,/^ *\}/' tools/run-clang-tidy.ps1 | grep -c 'plugins/profundum/\(src\|tests\)' || true)" ge 2
row "run-clang-tidy.sh:all-block:plugins/profundum/(src|tests)" "$(awk '/^ *all\)/,/;;/' tools/run-clang-tidy.sh | grep -c 'plugins/profundum/\(src\|tests\)' || true)" ge 2
row "INDEX.md:'^## Profundum'" "$(grep -c '^## Profundum' specs/INDEX.md || true)" eq 1
row "INDEX.md:Profundum-section:profundum-phase10-plugin-scaffold" "$(awk '/^## Profundum/,/^## [^P]/' specs/INDEX.md | grep -c profundum-phase10-plugin-scaffold || true)" ge 1

echo "== FR inspection checklist =="
fr002="$(node -e "const j=require('./plugins/profundum/version.json');console.log(Object.keys(j).length, 'preset_subdir' in j)")"
if [ "$fr002" = "6 false" ]; then echo "FR-002:version.json '$fr002' '6 false' PASS"; else echo "FR-002:version.json '$fr002' '6 false' FAIL"; FAILS=$((FAILS + 1)); fi
grep -rnE "/wd[0-9]+|pragma warning *\( *disable|-Wno-" plugins/profundum
row "FR-006:warning-suppressions" "$(grep -rnE "/wd[0-9]+|pragma warning *\( *disable|-Wno-" plugins/profundum | wc -l)" eq 0
fr008="$(node -e "
const fs=require('fs');const b='plugins/profundum/';
const p=['CMakeLists.txt','CLAUDE.md','CHANGELOG.md','README.md','version.json',
'src/entry.cpp','src/plugin_ids.h','src/processor/processor.h','src/processor/processor.cpp',
'src/controller/controller.h','src/controller/controller.cpp','src/parameters/global_params.h','src/parameters/macro_params.h',
'src/engine/profundum_engine_config.h','src/preset/profundum_preset_config.h','src/update/profundum_update_config.h','src/ui/.gitkeep',
'resources/editor.uidesc','resources/au-info.plist','resources/auv3/audiounitconfig.h.in','resources/auv3/macOS/Profundum.entitlements',
'resources/presets/Sub/.gitkeep','tests/CMakeLists.txt','tests/vstgui_test_stubs.cpp','tests/profundum_test_fixture.h',
'tests/unit/test_main.cpp','tests/unit/processor_bus_test.cpp','tests/unit/param_denorm_test.cpp','tests/unit/state_roundtrip_test.cpp',
'tests/unit/midi_event_test.cpp','tests/unit/lifecycle_test.cpp','tests/unit/controller/editor_lifecycle_test.cpp',
'tests/integration/processor_audio_test.cpp','tests/integration/param_flow_test.cpp','tests/integration/processor_cpu_test.cpp',
'docs/.gitkeep','installers/windows/setup.iss','installers/linux/README.txt'];
const m=p.filter(x=>!fs.existsSync(b+x));m.forEach(x=>console.error('MISSING '+b+x));console.log(p.length+' '+m.length);")"
row "FR-008:skeleton-missing(of ${fr008% *})" "${fr008#* }" eq 0
for a in '64 samples' 'after the limiter' 'Polyphony' 'Sub' 'IMidiMapping' 'Mass and Morph' 'ProfundumPluginTest' '22050' '120 BPM'; do
  row "FR-009:CLAUDE.md:'$a'" "$(fc plugins/profundum/CLAUDE.md "$a")" ge 1
done
grep -n 'kSubCategories = "Instrument|Synth"' plugins/profundum/src/plugin_ids.h
row "FR-014:kSubCategories" "$(grep -c 'kSubCategories = "Instrument|Synth"' plugins/profundum/src/plugin_ids.h || true)" eq 1
row "FR-018:DEF_CLASS2" "$(grep -c DEF_CLASS2 plugins/profundum/src/entry.cpp || true)" eq 2
grep -n '"ui/' plugins/profundum/src/entry.cpp
row "FR-018:'\"ui/'" "$(grep -n '"ui/' plugins/profundum/src/entry.cpp | wc -l)" eq 0
row "FR-053:'return Krate::DSP::ProfundumEngineConfig{}'" "$(fc plugins/profundum/src/engine/profundum_engine_config.h 'return Krate::DSP::ProfundumEngineConfig{}')" eq 1
fr056="$(ls -A plugins/profundum/src/ui | tr '\n' ' ' | sed 's/ $//')"
if [ "$fr056" = ".gitkeep" ]; then echo "FR-056:ls-A-src/ui '$fr056' '.gitkeep' PASS"; else echo "FR-056:ls-A-src/ui '$fr056' '.gitkeep' FAIL"; FAILS=$((FAILS + 1)); fi

echo "failures=$FAILS"
if [ "$FAILS" -eq 0 ]; then echo "REGISTRATION GREEN"; exit 0; else echo "REGISTRATION RED"; exit 1; fi
