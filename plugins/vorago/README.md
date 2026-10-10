# Vorago

**A dark-ambient drone instrument for VST3 / AU**

Vorago is a Krate Audio instrument for slow, heavy, evolving drones: deep harmonic clouds, sub tones,
spectral smear and a cavernous space engine, animated by autonomous life modulators and steered by twelve
macros. It is a thin plugin wrapper around the `VoragoEngine` / `VoragoMacroMatrix` / `CavernVerb` DSP in
the [KrateDSP library](../../dsp/); no DSP lives in this plugin. Released as `1.0.0` with 42 factory
presets in seven categories; see [CHANGELOG.md](CHANGELOG.md), the
[website](https://krateaudio.com/vorago/) and `specs/Vorago-roadmap.md`.

## Build

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --preset windows-x64-release
"$CMAKE" --build build/windows-x64-release --config Release --target Vorago vorago_tests
```

## Test

```bash
build/windows-x64-release/bin/Release/vorago_tests.exe 2>&1 | tail -5
node tools/run-cpu-tests.js vorago_tests    # timed CPU case; run it alone
```

## Validate

```bash
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Vorago.vst3"
```
