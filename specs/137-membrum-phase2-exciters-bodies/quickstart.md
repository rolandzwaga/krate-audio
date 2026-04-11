# Quickstart — Membrum Phase 2

Literal copy-paste commands for building, testing, and validating Phase 2. All paths assume the repo root `F:\projects\iterum`. Use the full CMake path on Windows per `CLAUDE.md`.

## Environment setup

```bash
# Recommended alias (bash on Windows MSYS2 / Git Bash)
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
```

## 1. Configure (Release, one-time per clone)

```bash
"$CMAKE" --preset windows-x64-release
```

This generates `build/windows-x64-release/` with the Visual Studio solution, builds the Steinberg SDK, pffft, Google Highway, and configures all plugin targets including `membrum`.

## 2. Build the Membrum plugin

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target Membrum
```

The plugin artifact is at `build/windows-x64-release/VST3/Release/Membrum.vst3/`.

The build may warn/fail on the post-build copy to `C:/Program Files/Common Files/VST3/`; that is a permissions quirk, not a compilation failure.

## 3. Build and run the Membrum tests

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target membrum_tests
build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5
```

The last line should read `All tests passed (N assertions in M test cases)`. If any test fails, re-read CLAUDE.md's "Running Tests Efficiently" section — never re-run the full suite just to grep differently.

### Run specific Phase 2 test groups

```bash
# Exciter variant dispatch + per-exciter behavior
build/windows-x64-release/bin/Release/membrum_tests.exe "ExciterBank*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/membrum_tests.exe "*Exciter*velocity*" 2>&1 | tail -5

# Body model spectral verification
build/windows-x64-release/bin/Release/membrum_tests.exe "BodyBank*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/membrum_tests.exe "*BodyModes*" 2>&1 | tail -5

# Tone Shaper
build/windows-x64-release/bin/Release/membrum_tests.exe "ToneShaper*" 2>&1 | tail -5

# Unnatural Zone
build/windows-x64-release/bin/Release/membrum_tests.exe "UnnaturalZone*" 2>&1 | tail -5

# State round-trip
build/windows-x64-release/bin/Release/membrum_tests.exe "*StateRoundTrip*" 2>&1 | tail -5

# Phase 1 regression
build/windows-x64-release/bin/Release/membrum_tests.exe "Phase1Regression*" 2>&1 | tail -5

# Allocation-detector tests (SC-011)
build/windows-x64-release/bin/Release/membrum_tests.exe "*Allocation*" 2>&1 | tail -5

# The 36-combination matrix (non-NaN, in-range, allocation-free)
build/windows-x64-release/bin/Release/membrum_tests.exe "*ExciterBodyMatrix*" 2>&1 | tail -5
```

## 4. Run the CPU benchmark (144 combinations, opt-in)

The benchmark is tagged `[.perf]` and skipped by default:

```bash
build/windows-x64-release/bin/Release/membrum_tests.exe "[.perf]" 2>&1 | tail -20
```

Output is appended to `build/windows-x64-release/membrum_benchmark_results.csv` for historical tracking. Each row:

```
timestamp,exciter,body,toneShaper,unnatural,cpu_percent
2026-04-10T14:30:12Z,Impulse,Membrane,off,off,0.21
...
```

The benchmark asserts `cpu_percent ≤ 1.25` for every combination. Failures print the combination and measured value before the test aborts.

## 5. Trigger exciter × body combinations manually (smoke test)

Inside `membrum_tests.exe` there is a `MembrumSmokeTest_{Exciter}_{Body}` pattern parameterized via Catch2. Run a single combination:

```bash
build/windows-x64-release/bin/Release/membrum_tests.exe "MembrumSmokeTest_FMImpulse_Bell*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/membrum_tests.exe "MembrumSmokeTest_Feedback_String*" 2>&1 | tail -5
```

Each smoke test configures the voice for the combination, triggers velocity 100 MIDI note 36, processes 500 ms, and asserts (a) non-silent, (b) peak in (-30, 0) dBFS, (c) no NaN/Inf.

## 6. Run the 808-kick pitch envelope test (SC-009)

```bash
build/windows-x64-release/bin/Release/membrum_tests.exe "*808Kick*" 2>&1 | tail -5
```

The test sets Pitch Envelope Start=160 Hz, End=50 Hz, Time=20 ms, Impulse + Membrane, and asserts the measured fundamental at t=20 ms is within ±10% of 50 Hz via short-time FFT.

## 7. Pluginval (FR-096, SC-010)

```bash
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"
```

Phase 2 MUST pass with ZERO errors and ZERO warnings at strictness 5. Any warning is a spec violation.

## 8. Allocation detector smoke test

The allocation detector is the primary SC-011 enforcement mechanism:

```bash
build/windows-x64-release/bin/Release/membrum_tests.exe "*Allocation_DrumVoice_AllCombinations*" 2>&1 | tail -20
```

This iterates all 36 exciter × body pairs and asserts zero heap activity from `DrumVoice::noteOn()`, `DrumVoice::noteOff()`, `DrumVoice::process()`, and every exciter/body backend's `process()`.

## 9. Clang-tidy (before commit)

From a Visual Studio Developer PowerShell:

```powershell
cd F:\projects\iterum
"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja
./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja 2>&1 | tee membrum-clang-tidy.log
```

Fix ALL findings before committing. Do not ignore pre-existing warnings per Constitution Principle VIII.

## 10. macOS / Linux

From a macOS or Linux machine:

```bash
# macOS
cmake --preset macos-release
cmake --build build/macos-release --target Membrum membrum_tests
build/macos-release/bin/Release/membrum_tests | tail -5
auval -v aumu Mbrm KrAt     # SC-010: must pass

# Linux
cmake --preset linux-release
cmake --build build/linux-release --target Membrum membrum_tests
build/linux-release/bin/membrum_tests | tail -5
```

## 11. Generate the Phase 1 golden reference (one-time, first implementation pass)

The Phase 1 regression test (SC-005) compares Phase 2 output to a golden `.bin` file. Generate it once on the canonical reference machine:

```bash
# Inside a test helper target (membrum_tests.exe has a "generate_golden" flag)
build/windows-x64-release/bin/Release/membrum_tests.exe "[generate_golden]"
# Commit the generated file
git add plugins/membrum/tests/golden/phase1_default.bin
```

Subsequent regression runs compare live output against the committed binary at a −90 dBFS RMS tolerance.

## Common issues

- **Test suite hangs**: likely an allocation-detector trap firing from a path that silently allocates. Run under debugger to catch.
- **Pluginval fails at strictness 5**: check pluginval output for the failing assertion — typically a parameter not round-tripping state, a bus config mismatch, or a NaN in audio output during bypass testing.
- **CPU benchmark reports > 1.25% for Noise Body**: reduce `NoiseBody::kModeCount` from 40 to 30, rebuild, re-measure, document the final value in `plan.md`.
- **Phase 1 regression fails at −90 dBFS tolerance**: either the MembraneMapper has drifted from Phase 1's inline code (fix the mapper), or the signal chain order got disturbed (audit `DrumVoice::process`).

## Post-implementation verification

Before marking the spec complete per Constitution Principle XVI:

```bash
# Full build + full test run
"$CMAKE" --build build/windows-x64-release --config Release
ctest --test-dir build/windows-x64-release -C Release --output-on-failure

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"

# Clang-tidy full pass
./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja

# CPU benchmark
build/windows-x64-release/bin/Release/membrum_tests.exe "[.perf]"
```

Only fill the compliance table in `spec.md` AFTER re-reading each FR/SC against the actual code and test output. No entries from memory.
