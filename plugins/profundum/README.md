# Profundum

**A mass bass instrument for VST3 / AU**

Profundum is a Krate Audio bass instrument built around mass rather than layers: one physically coherent bass
voice that moves from pure sub through resonant and harmonic bass to a distorted speaker-grabber while never
losing its low-frequency centre, steered by four Personality macros (Weight, Dark/Bright, Tight/Loose,
Clean/Violent). It is a thin plugin wrapper around the `ProfundumEngine` / `ProfundumMacroMatrix` DSP in the
[KrateDSP library](../../dsp/); no DSP lives in this plugin. Unreleased at `0.1.0` (the plugin scaffold); see
[CHANGELOG.md](CHANGELOG.md) and `specs/Profundum-roadmap.md`.

## Build

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --preset windows-x64-release
"$CMAKE" --build build/windows-x64-release --config Release --target Profundum profundum_tests
```

## Test

```bash
build/windows-x64-release/bin/Release/profundum_tests.exe 2>&1 | tail -5
node tools/run-cpu-tests.js profundum_tests    # timed CPU case; run it alone
```

## Validate

```bash
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Profundum.vst3"
```
