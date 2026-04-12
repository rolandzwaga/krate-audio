# Quickstart — Membrum Phase 3

Literal copy-paste commands for building, testing, and validating Phase 3. All paths assume the repo root `F:\projects\iterum`. Use the full CMake path on Windows per `CLAUDE.md`.

## 0. Environment

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
```

## 1. Configure (Release, one-time per clone)

```bash
"$CMAKE" --preset windows-x64-release
```

## 2. Build the Membrum plugin

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target Membrum
```

Artifact: `build/windows-x64-release/VST3/Release/Membrum.vst3/`. The post-build copy to `C:/Program Files/Common Files/VST3/` may fail with a permissions error — ignore it, the plugin built successfully.

## 3. Build and run all Membrum tests

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target membrum_tests
build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5
```

Expected last line: `All tests passed (N assertions in M test cases)`.

## 4. Run Phase 3-specific tests

```bash
# Voice pool (allocation, stealing, choke, fast-release)
build/windows-x64-release/bin/Release/membrum_tests.exe "VoicePool*"          2>&1 | tail -5

# All state migration tests
build/windows-x64-release/bin/Release/membrum_tests.exe "*StateMigration*"    2>&1 | tail -5
build/windows-x64-release/bin/Release/membrum_tests.exe "*StateRoundTripV3*"  2>&1 | tail -5

# Click-free steal across sample rates
build/windows-x64-release/bin/Release/membrum_tests.exe "*StealClickFree*"    2>&1 | tail -5

# Choke group behaviour
build/windows-x64-release/bin/Release/membrum_tests.exe "*ChokeGroup*"        2>&1 | tail -5

# Phase 2 regression via maxPolyphony=1
build/windows-x64-release/bin/Release/membrum_tests.exe "*Phase2RegressionMaxPoly1*" 2>&1 | tail -5

# Allocation-detector on the full 16-voice fuzz
build/windows-x64-release/bin/Release/membrum_tests.exe "*PolyphonyAllocationMatrix*" 2>&1 | tail -5
```

## 5. Run the [.perf] benchmarks (opt-in)

```bash
build/windows-x64-release/bin/Release/membrum_tests.exe "[.perf]" 2>&1 | tail -30
```

Output written to `build/windows-x64-release/membrum_phase3_benchmark.csv`. The 8-voice benchmark asserts ≤ 12 % per combination (≤ 18 % for the single Phase 2 waived Feedback+NoiseBody+TS+UN cell). The 16-voice stress test asserts zero xruns for 10 s of continuous processing.

## 6. Manually verify an 8-voice polyphony session

Run the exploratory smoke test that triggers 8 overlapping MIDI notes and records the mix:

```bash
build/windows-x64-release/bin/Release/membrum_tests.exe "*VoicePoolAllocate*eight voices*" 2>&1 | tail -5
```

For an actual DAW session:
1. Load `build/windows-x64-release/VST3/Release/Membrum.vst3` in the DAW.
2. Set `Max Polyphony` = 8, `Voice Stealing` = Oldest, `Choke Group` = 0.
3. Program a 1/32 at 140 BPM drum roll of alternating MIDI notes 36..43 for 4 bars.
4. Bounce and listen — you should hear overlapping tails, no cut-offs for the first 8 hits, a click-free steal on the 9th hit, and no audible artifacts on subsequent steals.

## 7. Pluginval (FR-188 / SC-029)

```bash
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"
```

Must pass with ZERO errors and ZERO warnings.

## 8. Clang-tidy (before every commit)

From a Visual Studio Developer PowerShell:

```powershell
cd F:\projects\iterum
"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja
./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja 2>&1 | tee membrum-phase3-clang-tidy.log
```

Fix ALL findings before committing — no pre-existing excuse per CLAUDE.md.

## 9. macOS / Linux

```bash
# macOS
cmake --preset macos-release
cmake --build build/macos-release --target Membrum membrum_tests
build/macos-release/bin/Release/membrum_tests | tail -5
auval -v aumu Mbrm KrAt         # FR-189 / SC-029 — must pass

# Linux
cmake --preset linux-release
cmake --build build/linux-release --target Membrum membrum_tests
build/linux-release/bin/membrum_tests | tail -5
```

## 10. Capture the Phase 2 (v2) state fixture (one-time, Phase 3.4 setup)

The v2 → v3 migration test uses a committed fixture blob captured from a real Phase 2 session. One-time generation:

```bash
# From a test helper that writes the current processor state to disk
build/windows-x64-release/bin/Release/membrum_tests.exe "[generate_v2_fixture]"
git add plugins/membrum/tests/golden/phase2_state_v2.bin
```

Subsequent v2 → v3 tests load this bin and assert bit-exact v2 body preservation.

## 11. Common issues

- **Test suite hangs on a voice-pool test**: usually the `allocation_detector` tripping on an unexpected heap hit inside `VoicePool::processBlock`. Run under a debugger with a breakpoint on `operator new`.
- **Fast-release still clicks at 22050 Hz**: 5 ms ≈ 110 samples at 22050 Hz — on the edge of the masking threshold. FR-124 allows 5 ms ± 1 ms; bump `kFastReleaseSecs` to 0.006 f for the 22050 case if measurements show clicks.
- **v3 state load rejects a corrupt blob**: should not happen — FR-144 mandates clamping, not rejection. Check `test_state_corruption_clamp.cpp`.
- **`sizeof(DrumVoice)` measurement too large for the two-array technique**: apply the fallback documented in `plan.md` Risks — single-fade per slot.
- **CPU benchmark exceeds 12 %**: check whether the scalar `ModalResonatorBank` is the bottleneck; FR-162 authorizes swapping to `ModalResonatorBankSIMD` inside `BodyBank::sharedBank_` with zero API change.

## 12. Post-implementation verification

Before filling the compliance table in `spec.md` per Principle XVI:

```bash
# Full rebuild + full test suite
"$CMAKE" --build build/windows-x64-release --config Release
ctest --test-dir build/windows-x64-release -C Release --output-on-failure

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"

# Clang-tidy
./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja

# Perf
build/windows-x64-release/bin/Release/membrum_tests.exe "[.perf]" 2>&1 | tee /tmp/membrum-phase3-perf.log
```

Only then update `plugins/membrum/CHANGELOG.md` and `plugins/membrum/version.json` (0.2.x → 0.3.0) and fill the compliance table with concrete file:line evidence.
