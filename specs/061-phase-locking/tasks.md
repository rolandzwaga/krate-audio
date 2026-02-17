# Tasks: Identity Phase Locking for PhaseVocoderPitchShifter

**Feature**: 061-phase-locking
**Input**: Design documents from `/specs/061-phase-locking/`
**Branch**: `061-phase-locking`

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Scope**: Modification to `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (header-only, no new source files) and creation of `dsp/tests/unit/processors/phase_locking_test.cpp`.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements (Constitution Principle XIII).

### Required Steps for EVERY User Story

1. **Write Failing Tests**: Write tests in `phase_locking_test.cpp` that FAIL (no implementation yet)
2. **Implement**: Write code in `pitch_shift_processor.h` to make tests pass
3. **Build**: Run `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
4. **Verify**: Run `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "Phase Locking*"` and confirm tests pass
5. **Cross-Platform Check**: Verify IEEE 754 compliance for test files using `std::isnan`/`std::isfinite`
6. **Commit**: Commit completed work

**DO NOT** skip the build or commit steps.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Prepare build system and file structure before any user story work begins.

- [X] T001 Register `dsp/tests/unit/processors/phase_locking_test.cpp` in `dsp/tests/CMakeLists.txt` (add to `dsp_tests` source list under `# Layer 2: Processors`)
- [X] T002 Add `phase_locking_test.cpp` to the `-fno-fast-math` `set_source_files_properties(...)` block in `dsp/tests/CMakeLists.txt` (required because tests use `std::isnan`/`std::isfinite`)
- [X] T003 Add `#include <array>` to the existing standard library includes section in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (currently missing; required for `std::array<bool, N>` and `std::array<uint16_t, N>`). Also fix the pre-existing incorrect comment at the `kHopSize` constant (currently reads `// 25% overlap (4x)`; correct value is `// 75% overlap (4x)` since hop = 1024 / 4096 = 25% of the window, meaning 75% overlap).
- [X] T004 Create empty test file `dsp/tests/unit/processors/phase_locking_test.cpp` with Catch2 include, namespace, and placeholder TEST_CASE to confirm build succeeds
- [X] T005 Build target `dsp_tests` and confirm zero compilation errors before writing any test or implementation code

**Checkpoint**: Build passes with empty test file - ready to begin user story work.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Add new member variables, constants, and API stubs to `PhaseVocoderPitchShifter`. These are required by ALL user stories and must be complete before any story-specific work begins.

**CRITICAL**: No user story work can begin until this phase is complete.

- [X] T006 Add `static constexpr std::size_t kMaxBins = 4097` and `static constexpr std::size_t kMaxPeaks = 512` constants inside the `PhaseVocoderPitchShifter` class body (adjacent to existing `kFFTSize`/`kHopSize`) in `dsp/include/krate/dsp/processors/pitch_shift_processor.h`
- [X] T007 Add all six new member variables to the private section of `PhaseVocoderPitchShifter` in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` after existing formant-preservation members: `isPeak_` (`std::array<bool, kMaxBins>{}`), `peakIndices_` (`std::array<uint16_t, kMaxPeaks>{}`), `numPeaks_` (`std::size_t = 0`), `regionPeak_` (`std::array<uint16_t, kMaxBins>{}`), `phaseLockingEnabled_` (`bool = true`), `wasLocked_` (`bool = false`)
- [X] T008 Add `setPhaseLocking(bool enabled) noexcept` and `[[nodiscard]] bool getPhaseLocking() const noexcept` public methods to `PhaseVocoderPitchShifter` in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (near existing `setFormantPreserve`/`getFormantPreserve`)
- [X] T009 Extend the `reset()` method in `PhaseVocoderPitchShifter` (`dsp/include/krate/dsp/processors/pitch_shift_processor.h`) to clear all new phase locking state: `isPeak_.fill(false)`, `peakIndices_.fill(0)`, `numPeaks_ = 0`, `regionPeak_.fill(0)`, `wasLocked_ = false`
- [X] T010 Build target `dsp_tests` and confirm zero compilation errors and zero warnings after adding the foundational members and methods

**Checkpoint**: All new members and API stubs compile cleanly - user story implementation can now begin.

---

## Phase 3: User Story 1 - Reduce Phasiness in Phase Vocoder Pitch Shifting (Priority: P1) -- MVP

**Goal**: Implement the three-stage identity phase locking algorithm (peak detection, region assignment, two-pass synthesis) so that pitch-shifted tonal signals concentrate spectral energy at harmonics rather than spreading it across neighboring bins.

**Independent Test**: Process a 440 Hz sine wave with +3 semitone shift; measure energy in a 3-bin window centered on the target frequency. Phase-locked output must achieve >= 90% concentration vs < 70% for basic path. Run `dsp_tests.exe "Phase Locking - Spectral Quality*"`.

**Covers**: FR-001, FR-002, FR-003, FR-004, FR-005, FR-011, FR-012, FR-013, FR-014, SC-001, SC-002, SC-008

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T011 [US1] Write spectral quality test in `dsp/tests/unit/processors/phase_locking_test.cpp`: single 440 Hz sine shifted +3 semitones, measure energy concentration in 3-bin window, assert >= 90% with locking enabled and < 70% with locking disabled (SC-001)
- [X] T012 [US1] Write multi-harmonic quality test in `dsp/tests/unit/processors/phase_locking_test.cpp`: sawtooth wave with harmonics pitched up, verify >= 95% of harmonics remain detectable as local maxima in output (SC-002)
- [X] T013 [US1] Write extended stability test in `dsp/tests/unit/processors/phase_locking_test.cpp`: process 10 continuous seconds at 44.1 kHz for each pitch shift amount (-12, -7, -3, +3, +7, +12 semitones), verify no NaN/inf/crash (SC-008). Note (G2): this test is written at Phase 3 test-write time (T013) but only becomes meaningful after the two-pass implementation in T015-T019 exists. Before T015-T019, the test will fail because the locked path is not yet implemented, not because stability is actually broken. This is an acceptable test-first sequencing artifact; the test's purpose is to provide a regression guard once the algorithm is complete.
- [X] T014 [US1] Build `dsp_tests` and confirm all US1 tests compile but FAIL (linking succeeds, assertions fail -- implementation not yet written)

### 3.2 Implementation for User Story 1

- [X] T015 [US1] Implement Stage A (peak detection) in `processFrame()` in `dsp/include/krate/dsp/processors/pitch_shift_processor.h`: after Step 1's analysis loop, when `phaseLockingEnabled_`, scan `magnitude_[1..numBins-2]` for 3-point local maxima, populate `isPeak_[]`, `peakIndices_[]`, and `numPeaks_`; stop at `kMaxPeaks` (FR-002, FR-012)
- [X] T016 [US1] Implement Stage B (region-of-influence assignment) in `processFrame()` in `dsp/include/krate/dsp/processors/pitch_shift_processor.h`: after Stage A, when `phaseLockingEnabled_ && numPeaks_ > 0`, assign every analysis bin 0..numBins-1 to its nearest peak via midpoint boundaries using a forward scan; handle single-peak case (FR-003)
- [X] T017 [US1] Implement Pass 1 of the two-pass synthesis loop in `processFrame()` in `dsp/include/krate/dsp/processors/pitch_shift_processor.h`: when `phaseLockingEnabled_ && numPeaks_ > 0`, first pass iterates synthesis bins 0..numBins-1 and processes only bins where `isPeak_[srcBinRounded]` is true; applies standard phase accumulation with `wrapPhase` and computes Cartesian output for peak bins (FR-004, FR-013)
- [X] T018 [US1] Implement Pass 2 of the two-pass synthesis loop in `processFrame()` in `dsp/include/krate/dsp/processors/pitch_shift_processor.h`: second pass processes only non-peak bins; looks up `regionPeak_[srcBinRounded]` to find `analysisPeak`, computes `synthPeakBin = round(analysisPeak * pitchRatio)`, computes `rotationAngle = synthPhase_[synthPeakBin] - prevPhase_[analysisPeak]`, applies `phaseForOutput = analysisPhaseAtSrc + rotationAngle`, stores locked phase in `synthPhase_[k]` for formant compatibility, computes Cartesian output (FR-005, FR-013)
- [X] T019 [US1] Implement the zero-peaks fallback: when `phaseLockingEnabled_ == true && numPeaks_ == 0`, fall through to the existing basic per-bin phase accumulation path in `processFrame()` (`dsp/include/krate/dsp/processors/pitch_shift_processor.h`) (FR-011)
- [X] T019b [US1] Add a formant-compatibility smoke test to Phase 3 (not deferred to Phase 4): with both phase locking and formant preservation enabled, process one frame of audio and verify no NaN/inf in the output. This is a minimal guard to catch broken formant step interactions before Phase 4's deeper formant tests (FR-015, G1 coverage gap). A comprehensive formant test is still done in T027 but this ensures the core two-pass synthesis does not break the formant step immediately after T015-T018.
- [X] T020 [US1] Build `dsp_tests` and confirm zero compilation errors and zero warnings
- [X] T021 [US1] Run `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "Phase Locking*"` and verify all US1 tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T022 [US1] Confirm `phase_locking_test.cpp` is in the `-fno-fast-math` `set_source_files_properties` block in `dsp/tests/CMakeLists.txt` (required because tests use `std::isnan`/`std::isfinite` for NaN/inf checks in SC-008); rebuild to verify no new warnings on cross-platform paths

### 3.4 Commit (MANDATORY)

- [X] T023 [US1] Commit completed User Story 1 work (peak detection, region assignment, two-pass synthesis, spectral quality tests) to feature branch `061-phase-locking`

**Checkpoint**: User Story 1 fully functional, tested, and committed. Phase-locked output demonstrably cleaner than basic output on tonal signals.

---

## Phase 4: User Story 2 - Backward-Compatible Toggle for Phase Locking (Priority: P2)

**Goal**: Ensure `setPhaseLocking(false)` produces output bit-identical to the pre-modification code path, and that toggling during continuous processing does not introduce audible clicks.

**Independent Test**: Process identical input with locking disabled; compare sample-by-sample with reference output from pre-modification code. Toggle locking on/off during processing and check amplitude discontinuity at toggle frame boundary does not exceed normal processing discontinuity. Run `dsp_tests.exe "Phase Locking - Toggle*"` and `dsp_tests.exe "Phase Locking - Backward*"`.

**Covers**: FR-006, FR-007, FR-008, FR-014, FR-015, FR-016, SC-005, SC-006, SC-007

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T024 [US2] Write backward compatibility test in `dsp/tests/unit/processors/phase_locking_test.cpp`: instantiate two `PhaseVocoderPitchShifter` objects; call `setPhaseLocking(false)` on both before any processing (since `true` is the default, both instances must explicitly disable locking to compare the same code path); process identical input frames through both instances and assert their sample outputs are equal within `Approx().margin(1e-6f)`. This verifies that a freshly toggled-off instance produces the same output as one that was never enabled, confirming backward compatibility (SC-005). Do NOT use exact float equality -- use `Approx().margin(1e-6f)` per SC-005 cross-platform definition.
- [X] T025 [US2] Write toggle click test in `dsp/tests/unit/processors/phase_locking_test.cpp`: process continuous audio for at least 5 frames with locking enabled (record the 99th-percentile sample-to-sample amplitude change across those frames as `normalDiscontinuity`), then call `setPhaseLocking(false)` and measure the maximum sample-to-sample amplitude change in the toggle frame; assert that this toggle-frame maximum does not exceed `normalDiscontinuity`. This matches the SC-006 definition (SC-006).
- [X] T026 [US2] Write API state test in `dsp/tests/unit/processors/phase_locking_test.cpp`: verify `getPhaseLocking()` returns `true` after default construction, returns `false` after `setPhaseLocking(false)`, and returns `true` again after `setPhaseLocking(true)` (FR-007)
- [X] T027 [US2] Write formant compatibility test in `dsp/tests/unit/processors/phase_locking_test.cpp`: enable both phase locking and formant preservation, process audio, verify no NaN/inf artifacts and that formant correction operates on phase-locked magnitudes (FR-015)
- [X] T028 [US2] Write noexcept / real-time safety test in `dsp/tests/unit/processors/phase_locking_test.cpp`: static_assert that `setPhaseLocking` and `getPhaseLocking` are noexcept; code inspection assert that processFrame contains no `new`, `delete`, `malloc`, `std::vector::push_back`, or dynamic allocation (SC-007, FR-016)
- [X] T029 [US2] Build `dsp_tests` and confirm all US2 tests compile but FAIL

### 4.2 Implementation for User Story 2

- [X] T030 [US2] Implement the toggle-to-basic re-initialization in `processFrame()` in `dsp/include/krate/dsp/processors/pitch_shift_processor.h`: check `wasLocked_ && !phaseLockingEnabled_` before the synthesis loop; if true, re-initialize `synthPhase_[k] = prevPhase_[k]` for all `k` in `[0, numBins-1]`; then update `wasLocked_ = phaseLockingEnabled_` (FR-008)
- [X] T031 [US2] Verify the basic path (when `phaseLockingEnabled_ == false`) in `processFrame()` in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` is the original single-pass per-bin phase accumulation loop, unchanged from pre-modification behavior, ensuring sample-accurate backward compatibility (FR-006, SC-005)
- [X] T032 [US2] Build `dsp_tests` and confirm zero compilation errors and zero warnings
- [X] T033 [US2] Run `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "Phase Locking*"` and verify all US1 and US2 tests pass (no regressions)

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T034 [US2] Confirm floating-point comparisons in backward-compatibility tests use `Approx().margin(1e-6f)` not exact equality (MSVC/Clang can differ at 7th-8th decimal place for intermediate float operations). This is the cross-platform definition of "sample-accurate" in SC-005 -- bit-identical on the same platform, within `1e-6f` margin across platforms. Any test using exact `==` comparison on float output samples must be updated to use this margin.

### 4.4 Commit (MANDATORY)

- [X] T035 [US2] Commit completed User Story 2 work (toggle re-initialization, backward compatibility verification, toggle click tests) to feature branch `061-phase-locking`

**Checkpoint**: User Stories 1 and 2 both committed. Phase locking toggle works cleanly; disabled mode is bit-identical to pre-modification behavior.

---

## Phase 5: User Story 3 - Peak Detection Produces Correct Spectral Peaks (Priority: P3)

**Goal**: Verify that Stage A (peak detection) correctly identifies spectral peaks for known signals: exactly 1 peak for a single sinusoid, approximately 220 peaks for a 100 Hz sawtooth at 44.1 kHz / 4096-pt FFT.

**Independent Test**: Feed known synthetic signals directly to the `PhaseVocoderPitchShifter` and expose peak count/positions via test accessor or indirect measurement. Run `dsp_tests.exe "Phase Locking - Peak Detection*"`.

**Covers**: FR-002, FR-012, SC-003

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T036 [US3] Write single sinusoid peak detection test in `dsp/tests/unit/processors/phase_locking_test.cpp`: feed a 440 Hz sinusoid into the pitch shifter and verify the output has exactly 1 spectral peak near bin 40-41 (bin index = 440 * 4096 / 44100 ~ 40.8); use 3-point local maximum check on output spectrum (SC-003)
- [X] T037 [US3] Write multi-harmonic peak detection test in `dsp/tests/unit/processors/phase_locking_test.cpp`: feed a 100 Hz sawtooth wave and verify peak count is approximately 220 (harmonics below Nyquist = floor(22050/100)), within +/- 5% tolerance (SC-003). Use a steady-state buffer of sufficient length (at least 4 * kFFTSize samples) to ensure the STFT analysis reaches stable magnitude readings. Note that Hann windowing produces ~3-bin spectral leakage per harmonic; at high harmonic indices where harmonics are separated by only a few bins, some adjacent harmonics may not satisfy the strict 3-point local maximum condition. Document the actual measured peak count from the test run in the SC-003 evidence row of the compliance table.
- [X] T038 [US3] Write silence/zero-signal test in `dsp/tests/unit/processors/phase_locking_test.cpp`: feed all-zero input and verify zero peaks are detected and the basic path is used (FR-011)
- [X] T039 [US3] Write maximum peaks limit test in `dsp/tests/unit/processors/phase_locking_test.cpp`: feed a signal designed to produce more than 512 peaks (e.g., white noise or dense sinusoidal synthesis); verify peak count is clamped to `kMaxPeaks` (512) without buffer overflow (FR-012)
- [X] T039b [US3] Write equal-magnitude plateau test in `dsp/tests/unit/processors/phase_locking_test.cpp`: synthesize a spectrum where two adjacent bins (e.g., bins 100 and 101) have identical magnitude and both are surrounded by lower-magnitude bins; verify that neither bin 100 nor bin 101 is detected as a peak, confirming that the strict inequality condition (`magnitude[k] > magnitude[k-1] AND magnitude[k] > magnitude[k+1]`) is enforced and not relaxed to `>=` (FR-002, spec Edge Cases).
- [X] T040 [US3] Build `dsp_tests` and confirm all US3 tests compile but FAIL

### 5.2 Implementation for User Story 3

Note: Stage A implementation was done in T015. This phase focuses on verifying correctness of the peak detection step specifically and fixing any issues discovered by the dedicated tests.

- [X] T041 [US3] Review and adjust Stage A (peak detection) implementation in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` if peak count or position does not match expected values from SC-003 tests (boundary conditions: confirm DC bin 0 and Nyquist bin numBins-1 are excluded from peak detection per FR-002)
- [X] T042 [US3] Build `dsp_tests` and confirm zero compilation errors and zero warnings
- [X] T043 [US3] Run `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "Phase Locking*"` and verify all US1, US2, and US3 tests pass

### 5.3 Commit (MANDATORY)

- [X] T044 [US3] Commit completed User Story 3 work (peak detection correctness verification and any boundary fixes) to feature branch `061-phase-locking`

**Checkpoint**: Peak detection verified correct for known signals. All prior user stories still pass.

---

## Phase 6: User Story 4 - Region-of-Influence Assignment Covers All Bins (Priority: P4)

**Goal**: Verify that Stage B (region assignment) achieves 100% bin coverage for all test signals including edge cases (silence, single peak, maximum peaks), and that midpoint boundaries are placed correctly between adjacent peaks.

**Independent Test**: After region assignment, verify every bin index from 0 to numBins-1 maps to a valid peak index. Run `dsp_tests.exe "Phase Locking - Region*"`.

**Covers**: FR-003, SC-004

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T045 [US4] Write complete bin coverage test in `dsp/tests/unit/processors/phase_locking_test.cpp`: after processing a multi-peak signal, verify that every bin in [0, numBins-1] has received a phase assignment via one of the following direct approaches: (a) add a `friend class PhaseLockingTest` or a `verifyRegionCoverage() const noexcept -> bool` method to `PhaseVocoderPitchShifter` that asserts `regionPeak_[k]` is a valid peak index for all k, or (b) assert that the sum of bins claimed by each detected peak equals numBins exactly. Do NOT rely solely on observing non-zero output energy — zero-magnitude bins produce zero output regardless of phase assignment and would not reveal missing coverage (SC-004).
- [X] T046 [US4] Write midpoint boundary test in `dsp/tests/unit/processors/phase_locking_test.cpp`: synthesize a two-tone signal with known peaks at bins 50 and 80; verify that output bins 0-64 belong to the region of peak 50 and bins 65-numBins-1 belong to peak 80 (midpoint = (50+80)/2 = 65), observable via phase coherence in the output spectrum (FR-003)
- [X] T047 [US4] Write single-peak coverage test in `dsp/tests/unit/processors/phase_locking_test.cpp`: feed a pure single sinusoid (produces 1 peak), verify all bins in output spectrum receive valid phase assignments (no bin defaults to zero/undefined) (FR-003, SC-004)
- [X] T048 [US4] Build `dsp_tests` and confirm all US4 tests compile but FAIL

### 6.2 Implementation for User Story 4

Note: Stage B implementation was done in T016. This phase focuses on verifying correctness of region assignment specifically.

- [X] T049 [US4] Review and adjust Stage B (region assignment) implementation in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` if coverage or boundary tests fail (verify single-peak path uses `regionPeak_.fill(peakIndices_[0])` for all bins; verify midpoint calculation uses integer division matching the spec: `midpoint = (peakIndices_[i] + peakIndices_[i+1]) / 2`)
- [X] T050 [US4] Build `dsp_tests` and confirm zero compilation errors and zero warnings
- [X] T051 [US4] Run `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "Phase Locking*"` and verify all US1, US2, US3, and US4 tests pass

### 6.3 Commit (MANDATORY)

- [X] T052 [US4] Commit completed User Story 4 work (region assignment correctness verification and any boundary fixes) to feature branch `061-phase-locking`

**Checkpoint**: Region assignment verified correct: 100% bin coverage, correct midpoint boundaries. All prior user stories still pass.

---

## Phase 7: User Story 5 - Simplified Phase Arithmetic via Shared Rotation Angle (Priority: P5)

**Goal**: Verify that non-peak bins use the shared rotation angle formula (`phi_out[k] = phi_in[srcBin] + rotationAngle`) rather than independent phase accumulation, and that peak bins continue to use standard accumulation. This is a correctness verification of the per-bin arithmetic, not a new feature.

**Independent Test**: Inspect the two-pass synthesis implementation; verify that Pass 2 computes `rotationAngle = synthPhase_[synthPeakBin] - prevPhase_[analysisPeak]` once per region and adds it to `phi_in[srcBin]` (not to `synthPhase_[k]` accumulatively). Run `dsp_tests.exe "Phase Locking - Rotation*"`.

**Covers**: FR-005, FR-013, SC-001 (indirect)

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T053 [US5] Write rotation angle correctness test in `dsp/tests/unit/processors/phase_locking_test.cpp` using an observable behavioral approach (do NOT access `synthPhase_[]` or `prevPhase_[]` directly — these are private): process a two-tone signal with known peaks at bins P1 and P2; then for two non-peak synthesis bins in the same region, extract their output phases via `std::atan2(imag, real)` from the output `synthesisSpectrum_` Cartesian values; verify that the phase difference between the two non-peak output bins equals the phase difference between the corresponding analysis input bins (this is the invariant that identity phase locking preserves: `phi_out[k1] - phi_out[k2] == phi_in[srcBin1] - phi_in[srcBin2]` for bins in the same region). If a test accessor for `synthesisSpectrum_` is needed, add a `const SpectralBuffer& getSynthesisSpectrum() const noexcept` method or declare the test class as a friend (FR-005).
- [X] T054 [US5] Write disabled-path accumulation test in `dsp/tests/unit/processors/phase_locking_test.cpp`: with phase locking disabled, verify all bins use independent phase accumulation (`synthPhase_[k] += freq; synthPhase_[k] = wrapPhase(...)`) and NOT the rotation angle formula (FR-006, FR-013)
- [X] T055 [US5] Build `dsp_tests` and confirm all US5 tests compile but FAIL

### 7.2 Implementation for User Story 5

Note: The rotation angle formula is implemented in T018 (Pass 2). This phase verifies it is correct.

- [X] T056 [US5] Verify Pass 2 in `processFrame()` in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` uses interpolated `analysisPhaseAtSrc = prevPhase_[srcBin0] * (1-frac) + prevPhase_[srcBin1] * frac` (not just `prevPhase_[srcBin0]`) for the source phase; correct if not (FR-005)
- [X] T057 [US5] Verify `wrapPhase` is called after `synthPhase_[k] +=` in Pass 1 for peak bins in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` and is NOT called for non-peak bins (which use the rotation angle directly without accumulation) (FR-004)
- [X] T058 [US5] Build `dsp_tests` and confirm zero compilation errors and zero warnings
- [X] T059 [US5] Run `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "Phase Locking*"` and verify all US1 through US5 tests pass

### 7.3 Commit (MANDATORY)

- [X] T060 [US5] Commit completed User Story 5 work (rotation angle arithmetic verification) to feature branch `061-phase-locking`

**Checkpoint**: All five user stories implemented, tested, and committed. Full test suite passes.

---

## Phase 8: Polish and Cross-Cutting Concerns

**Purpose**: Additional robustness tests and integration verification across all user stories.

- [X] T061 [P] Write rapid toggle stability test in `dsp/tests/unit/processors/phase_locking_test.cpp`: toggle `setPhaseLocking` true/false/true/false 100 times during continuous processing; verify no crashes, no NaN, no inf in output
- [X] T062 [P] Write unity pitch ratio edge case test in `dsp/tests/unit/processors/phase_locking_test.cpp`: process audio at pitch ratio 1.0 (should take `processUnityPitch()` bypass path, not invoking `processFrame()`); verify phase locking state is unaffected
- [X] T063 [P] Write `reset()` completeness test in `dsp/tests/unit/processors/phase_locking_test.cpp`: call `reset()` after processing with phase locking enabled (so `isPeak_` and `regionPeak_` have been populated), then verify: (a) `numPeaks_ == 0`, (b) `phaseLockingEnabled_` retains its last-set value (reset does NOT change the toggle state), (c) `wasLocked_ == false`, and (d) on the first frame processed after reset, `numPeaks_` reflects only the peaks in that new frame and carries no stale data from before the reset. If direct member access requires a test accessor, add a `getNumPeaks() const noexcept -> std::size_t` method or equivalent for test-only inspection.
- [X] T064 Run the full `dsp_tests` suite (all tests, not just Phase Locking): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe` to verify no regressions in existing pitch_shift_processor tests or any other DSP tests
- [X] T065 Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Iterum.vst3"` to confirm plugin still validates (the DSP library is used by the plugin)
- [X] T066 Build `dsp_tests` and confirm zero compilation errors and zero warnings after polish tasks
- [X] T067 Commit polish tests and any corrections to feature branch `061-phase-locking`

---

## Phase 9: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion.

> **Constitution Principle XIV**: Every spec implementation MUST update architecture documentation as a final task.

### 9.1 Architecture Documentation Update

- [X] T068 Update `specs/_architecture_/layer-2-processors.md` with the phase locking additions to `PhaseVocoderPitchShifter`: add subsection documenting the new `setPhaseLocking()`/`getPhaseLocking()` API, the identity phase locking algorithm (Laroche & Dolson 1999), new member variables and their memory footprint (~13.3 KB per instance), the two-pass synthesis structure, and the formant preservation compatibility note

### 9.2 Final Documentation Commit

- [X] T069 Commit architecture documentation update to feature branch `061-phase-locking`

**Checkpoint**: Architecture documentation reflects all new functionality.

---

## Phase 10: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 10.1 Run Clang-Tidy Analysis

- [X] T070 Generate `compile_commands.json` via Ninja preset if not current: `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja` (from VS Developer PowerShell)
- [X] T071 Run clang-tidy on DSP target: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`

### 10.2 Address Findings

- [X] T072 Fix all clang-tidy errors reported for `pitch_shift_processor.h` and `phase_locking_test.cpp` (blocking issues) - N/A -- no issues found
- [X] T073 Review clang-tidy warnings and fix where appropriate; add `// NOLINT(<check-name>): <reason>` comments for any intentional suppressions in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` or `dsp/tests/unit/processors/phase_locking_test.cpp` - N/A -- no issues found
- [X] T074 Rebuild and re-run tests after clang-tidy fixes: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests` then run full test suite

**Checkpoint**: Static analysis clean - ready for completion verification.

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion.

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

- [X] T075 Review all FR-001 through FR-016 requirements from `specs/061-phase-locking/spec.md` against the actual implementation in `dsp/include/krate/dsp/processors/pitch_shift_processor.h`; record file path and line number for each requirement
- [X] T076 Review all SC-001 through SC-008 success criteria; run relevant test cases and record actual measured values versus spec thresholds (e.g., actual energy concentration percentage for SC-001, actual peak count for SC-003)
- [X] T077 Search implementation for cheating patterns: no `// placeholder` or `// TODO` in `pitch_shift_processor.h` or `phase_locking_test.cpp`; no test thresholds relaxed below spec values; no features removed from scope

### 11.2 Fill Compliance Table in spec.md

- [X] T078 Update `specs/061-phase-locking/spec.md` "Implementation Verification" section: fill the compliance table for each FR-xxx and SC-xxx row with status (MET/NOT MET/PARTIAL/DEFERRED) and concrete evidence (file paths, line numbers, test names, actual measured values)
- [X] T079 Mark overall spec status honestly (COMPLETE / NOT COMPLETE / PARTIAL) in the "Honest Assessment" section of `specs/061-phase-locking/spec.md`

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change any test threshold from what spec.md originally required?
2. Are there any "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove any features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T080 All self-check questions answered "no" (or gaps documented honestly in spec.md)

### 11.4 Final Commit

- [X] T081 Commit compliance table update and all remaining changes to feature branch `061-phase-locking`
- [X] T082 Run full `dsp_tests` suite one final time and confirm all tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`

**Checkpoint**: Spec implementation honestly complete and committed.

---

## Dependencies and Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - start immediately
- **Foundational (Phase 2)**: Depends on Phase 1 completion - BLOCKS all user stories
- **User Stories (Phases 3-7)**: All depend on Phase 2 completion
  - Phase 3 (US1) should be implemented first as it contains the core algorithm (Stages A, B, and the two-pass synthesis)
  - Phases 4-7 (US2-US5) verify orthogonal properties of the same implementation; can proceed sequentially after Phase 3
- **Polish (Phase 8)**: Depends on all user stories being committed
- **Architecture Docs (Phase 9)**: Depends on Phase 8
- **Static Analysis (Phase 10)**: Depends on Phase 9 (all code in final state)
- **Completion Verification (Phase 11)**: Depends on all prior phases

### User Story Dependencies

- **User Story 1 (P1)**: Implements the full algorithm (Stages A, B, and two-pass synthesis). Must complete first.
- **User Story 2 (P2)**: Toggle/backward compat. Depends on US1 (tests both paths). Can start after Phase 3 commit.
- **User Story 3 (P3)**: Peak detection correctness. The Stage A code is already present from US1; this phase adds dedicated tests and may fix boundary conditions.
- **User Story 4 (P4)**: Region assignment correctness. The Stage B code is already present from US1; this phase adds dedicated tests.
- **User Story 5 (P5)**: Rotation angle arithmetic verification. Pass 2 code is from US1; this phase verifies its correctness at the formula level.

### Within Each User Story

- Tests FIRST (must FAIL before implementation)
- Build after each code change
- Verify tests pass after implementation
- Cross-platform check (IEEE 754 / `-fno-fast-math`)
- Commit at end of each user story

### Parallel Opportunities

Within Phase 1, T001, T002, T003 can run in parallel (different parts of different files).

Within each user story's test-writing step, all test cases can be written in parallel (same file but non-conflicting TEST_CASEs):
- T011, T012, T013 can be written together (all in US1 test phase)
- T024, T025, T026, T027, T028 can be written together (all in US2 test phase)
- T036, T037, T038, T039, T039b can be written together (all in US3 test phase)
- T045, T046, T047 can be written together (all in US4 test phase)
- T053, T054 can be written together (all in US5 test phase)
- T061, T062, T063 can be written together (all in polish phase)

---

## Parallel Example: User Story 1

```bash
# Write all US1 tests together:
Task: "Spectral quality test (single sinusoid) in phase_locking_test.cpp"
Task: "Multi-harmonic quality test in phase_locking_test.cpp"
Task: "Extended stability test in phase_locking_test.cpp"

# Then implement all three stages of the algorithm together:
Task: "Stage A peak detection in processFrame() in pitch_shift_processor.h"
Task: "Stage B region assignment in processFrame() in pitch_shift_processor.h"
Task: "Pass 1 (peak bins) in processFrame() in pitch_shift_processor.h"
Task: "Pass 2 (non-peak bins) in processFrame() in pitch_shift_processor.h"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (CMakeLists.txt registration, `<array>` include, empty test file)
2. Complete Phase 2: Foundational (constants, member variables, API stubs, reset() update)
3. Complete Phase 3: User Story 1 (full algorithm: peak detection, region assignment, two-pass synthesis)
4. **STOP and VALIDATE**: Run spectral quality tests; confirm locked output concentrates energy >= 90% vs basic < 70%
5. The algorithm is now functioning end-to-end

### Incremental Delivery

1. Phase 1 + Phase 2: Foundation ready (builds, APIs declared)
2. Phase 3 (US1): Core algorithm working - pitch-shifted output measurably less "phasey" (MVP)
3. Phase 4 (US2): Toggle/backward compat verified - safe for integration into HarmonizerEngine
4. Phase 5 (US3): Peak detection correctness locked down - no boundary surprises
5. Phase 6 (US4): Region assignment coverage guaranteed - no unbounded edge cases
6. Phase 7 (US5): Rotation angle formula verified - algorithm matches Laroche & Dolson exactly
7. Phases 8-11: Polish, docs, static analysis, honest completion

### Single-Developer Sequence

Because this is a modification to a single header-only file, all user story implementations touch the same file (`pitch_shift_processor.h`) and the same test file (`phase_locking_test.cpp`). The recommended execution order is strictly sequential:

Phase 1 → Phase 2 → Phase 3 → Phase 4 → Phase 5 → Phase 6 → Phase 7 → Phase 8 → Phase 9 → Phase 10 → Phase 11

---

## Notes

- `[P]` tasks = different files or non-conflicting TEST_CASEs in the same file; no sequential dependency
- `[USn]` label maps each task to its user story for traceability
- All implementation changes are in a single header: `dsp/include/krate/dsp/processors/pitch_shift_processor.h`
- All tests are in a single new file: `dsp/tests/unit/processors/phase_locking_test.cpp`
- Phase locking is **enabled by default** -- the basic path (disabled) is preserved unchanged as the fallback
- **MANDATORY**: Write tests that FAIL before implementing (Constitution Principle XIII)
- **MANDATORY**: Add `phase_locking_test.cpp` to `-fno-fast-math` list (uses `std::isnan`/`std::isfinite`)
- **MANDATORY**: Commit at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-2-processors.md` before claiming spec complete
- **MANDATORY**: Fill compliance table in `spec.md` with real evidence before claiming spec complete
- **NEVER claim completion if ANY requirement is not met** -- document gaps honestly instead
- Build command: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- Test run command: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "Phase Locking*"`
