# Tasks: Shared-Analysis FFT Refactor for PhaseVocoder Multi-Voice Harmonizer

**Input**: Design documents from `/specs/065-shared-analysis-fft-refactor/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, quickstart.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines -- they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) -- no manual context verification required.

### Example Todo List Structure

```
[ ] Write failing tests for [feature]
[ ] Implement [feature] to make tests pass
[ ] Verify all tests pass
[ ] Cross-platform check: verify -fno-fast-math for IEEE 754 functions
[ ] Commit completed work
```

**DO NOT** skip the commit step. These appear as checkboxes because they MUST be tracked.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for DSP projects**: After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/path/to/your_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Confirm build environment is green and establish a known-good baseline before any refactor changes.

- [X] T001 Build dsp_tests target to confirm clean build baseline: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T002 Run the full dsp_tests suite and record the baseline test count and results: `build/windows-x64-release/bin/Release/dsp_tests.exe`
- [X] T003 Record the pre-refactor benchmark baselines for PhaseVocoder 4-voice and PitchSync 4-voice using the KrateDSP benchmark harness (sample rate 44.1 kHz, block size 256, 4 voices, Release build, realtime priority, 2s warmup, 10s steady-state) -- both real-time CPU % and average process() time in µs/block MUST be recorded before any code changes
- [X] T003a Capture the pre-refactor HarmonizerEngine per-voice PhaseVocoder output as a golden reference fixture required for SC-002: run a 1-second, 440 Hz sine tone through HarmonizerEngine with 4 voices in PhaseVocoder mode at 44.1 kHz/256 block size and persist the per-voice output sample arrays to a test fixture (e.g., a binary file at `dsp/tests/unit/systems/fixtures/harmonizer_engine_pv_golden.bin` or a constexpr array in the test file). This MUST be captured before any code changes; the SC-002 equivalence assertion in T027 loads this fixture and computes the per-voice RMS difference against the post-refactor output.

---

## Phase 2: Foundational (Blocking Prerequisite -- processFrame Refactor)

**Purpose**: Refactor `PhaseVocoderPitchShifter::processFrame()` from a one-parameter private method that reads/writes internal members to a three-parameter private method accepting `const SpectralBuffer& analysis`, `SpectralBuffer& synthesis`, and `float pitchRatio`. This is required by FR-023 and is a prerequisite for all user story work: US1 (shared analysis), US2 (new API), US3 (OLA isolation verification) all depend on `processFrame()` accepting external spectrum parameters.

**CRITICAL**: No user story work can begin until this phase is complete and ALL existing tests pass.

- [X] T004 Write failing tests for the refactored `processFrame()` signature by calling the existing standard `process()` method and asserting output is unchanged -- these tests must pass after the refactor in `dsp/tests/unit/processors/pitch_shift_processor_test.cpp`. Build will fail until implementation is complete.
- [X] T005 Refactor `PhaseVocoderPitchShifter::processFrame()` in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` from `void processFrame(float pitchRatio) noexcept` to `void processFrame(const SpectralBuffer& analysis, SpectralBuffer& synthesis, float pitchRatio) noexcept` (FR-023 canonical signature) -- replace all reads from `analysisSpectrum_` with reads from the `analysis` parameter, replace all writes to `synthesisSpectrum_` with writes to the `synthesis` parameter
- [X] T006 Update the existing `process()` method in `PhaseVocoderPitchShifter` in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` to call the refactored `processFrame(analysisSpectrum_, synthesisSpectrum_, pitchRatio)` using internal members -- maintaining exact backward compatibility per FR-006
- [X] T007 Build dsp_tests and fix ALL compiler warnings and errors: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T008 Run the full dsp_tests suite and verify zero regressions from the baseline recorded in T002: `build/windows-x64-release/bin/Release/dsp_tests.exe`
- [X] T009 Commit the processFrame refactor with a message documenting the mechanical signature change (no behavioral change)

**Checkpoint**: processFrame refactor complete, all existing tests pass -- user story implementation can now begin

---

## Phase 3: User Story 2 - New Layer 2 API for External Analysis Injection (Priority: P1)

**Goal**: Add `processWithSharedAnalysis()`, `pullOutputSamples()`, and `outputSamplesAvailable()` to `PhaseVocoderPitchShifter`, then add delegation methods and FFT/hop accessors to `PitchShiftProcessor`. This is the enabling API that US1 (HarmonizerEngine integration) depends on.

**Why US2 before US1**: US1 (HarmonizerEngine shared-analysis path) cannot be implemented until the Layer 2 API exists. US2 is the API definition; US1 is the consumer.

**Independent Test**: Run `PhaseVocoderPitchShifter` with the standard `process()` method and with `processWithSharedAnalysis()` on the same input, capturing the analysis spectrum in between, and verify outputs are identical (max sample error < 1e-5).

### 3.1 Tests for User Story 2 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T010 [P] [US2] Write failing test: `processWithSharedAnalysis()` produces output identical to `process()` (SC-003, max error < 1e-5) -- add to `dsp/tests/unit/processors/pitch_shift_processor_test.cpp`. Test must call the non-existent method and fail to compile or link.
- [X] T011 [P] [US2] Write failing test: `processWithSharedAnalysis()` with formant preservation enabled produces identical output to standard path (FR-005) -- add to `dsp/tests/unit/processors/pitch_shift_processor_test.cpp`
- [X] T012 [P] [US2] Write failing test: `processWithSharedAnalysis()` with identity phase locking enabled produces identical output to standard path (FR-003, SC-006, max error < 1e-5) -- add to `dsp/tests/unit/processors/pitch_shift_processor_test.cpp`
- [X] T013 [P] [US2] Write failing test: `processWithSharedAnalysis()` with transient detection and phase reset enabled produces identical output to standard path (FR-004) -- add to `dsp/tests/unit/processors/pitch_shift_processor_test.cpp`
- [X] T014 [P] [US2] Write failing test: `processWithSharedAnalysis()` is a no-op when called on an unprepared `PhaseVocoderPitchShifter` -- verify by asserting that `pullOutputSamples()` returns 0 after the call (FR-008a: no frame added to OLA buffer on degenerate return) -- add to `dsp/tests/unit/processors/pitch_shift_processor_test.cpp`
- [X] T015 [P] [US2] Write failing test: `processWithSharedAnalysis()` is a no-op when given a `SpectralBuffer` with wrong `numBins()` (i.e., anything other than `kFFTSize / 2 + 1` = 2049) -- debug assert fires on mismatch, release build silently returns without OLA write, and `pullOutputSamples()` returns 0 (FR-008; note: use `kFFTSize / 2 + 1` for the check, not `kMaxBins` which is sized for maximum FFT) -- add to `dsp/tests/unit/processors/pitch_shift_processor_test.cpp`
- [X] T016 [P] [US2] Write failing test: `pullOutputSamples()` returns 0 when called before any synthesis frame has been processed (OLA priming period), confirming no garbage audio is produced (FR-008a -- the zero-output contract is enforced via `pullOutputSamples()` returning 0, not via zero-filling a parameter buffer; callers must zero-fill their own output for samples not returned by `pullOutputSamples()`) -- add to `dsp/tests/unit/processors/pitch_shift_processor_test.cpp`
- [X] T017 [P] [US2] Write failing tests for `PitchShiftProcessor` delegation: (a) in PhaseVocoder mode, `processWithSharedAnalysis()` delegates to the internal `PhaseVocoderPitchShifter` and `pullSharedAnalysisOutput()` returns processed samples; (b) in Simple/Granular/PitchSync modes, `processWithSharedAnalysis()` is a no-op -- assert `pullSharedAnalysisOutput()` returns 0 after the call (FR-009, FR-009a: the no-op has no output buffer parameter; it is observable through `pullSharedAnalysisOutput()` returning 0, not through zero-filling a buffer) -- add to `dsp/tests/unit/processors/pitch_shift_processor_test.cpp`
- [X] T018 [P] [US2] Write failing tests for `getPhaseVocoderFFTSize()` returns 4096 and `getPhaseVocoderHopSize()` returns 1024 (FR-011) -- add to `dsp/tests/unit/processors/pitch_shift_processor_test.cpp`
- [X] T018a [P] [US2] Write failing test: one call to `processWithSharedAnalysis()` adds exactly one frame to the OLA buffer -- assert `outputSamplesAvailable()` increases by `kHopSize` (1024) per call, verifying the "exactly one frame per call" contract (FR-007) -- add to `dsp/tests/unit/processors/pitch_shift_processor_test.cpp`
- [X] T018b [P] [US2] Write failing test: `processWithSharedAnalysis()` with unity pitch ratio (1.0) does NOT apply unity-pitch bypass internally -- the method processes the frame through phase rotation and OLA normally, and `pullOutputSamples()` returns samples (FR-025, spec edge cases: unity bypass is the caller's responsibility, not this method's) -- add to `dsp/tests/unit/processors/pitch_shift_processor_test.cpp`

### 3.2 Implementation for User Story 2

- [X] T019 [US2] Add `processWithSharedAnalysis(const SpectralBuffer& analysis, float pitchRatio) noexcept` to `PhaseVocoderPitchShifter` in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` per the contract in `specs/065-shared-analysis-fft-refactor/contracts/phase-vocoder-pitch-shifter-api.md`: validate `analysis.numBins() == kFFTSize / 2 + 1` (assert debug, no-op release), call `processFrame(analysis, synthesisSpectrum_, pitchRatio)`, call `ola_.synthesize(synthesisSpectrum_)`. Important: (a) FR-007 requires exactly one frame per call -- add an assertion in debug builds that `ola_.samplesAvailable()` increases by exactly `kHopSize` after one call; (b) FR-025 requires that unity-pitch bypass is NOT applied internally -- do NOT call `processUnityPitch` or check for `abs(pitchRatio - 1.0) < threshold` inside this method; the caller (HarmonizerEngine) handles unity pitch routing
- [X] T020 [US2] Add `pullOutputSamples(float* output, std::size_t maxSamples) noexcept` and `outputSamplesAvailable() const noexcept` to `PhaseVocoderPitchShifter` in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` per the contract in `specs/065-shared-analysis-fft-refactor/contracts/phase-vocoder-pitch-shifter-api.md`
- [X] T021 [US2] Add `processWithSharedAnalysis()`, `pullSharedAnalysisOutput()`, and `sharedAnalysisSamplesAvailable()` delegation methods to `PitchShiftProcessor` public API and `PitchShiftProcessor::Impl` struct in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` per the contract in `specs/065-shared-analysis-fft-refactor/contracts/pitch-shift-processor-api.md`; for non-PhaseVocoder modes `processWithSharedAnalysis` is a documented no-op (no frame pushed to OLA buffer; there is no output buffer parameter -- the no-op is observable through `pullSharedAnalysisOutput` returning 0), `pullSharedAnalysisOutput` returns 0, `sharedAnalysisSamplesAvailable` returns 0 (FR-009, FR-009a)
- [X] T022 [US2] Add `getPhaseVocoderFFTSize()` and `getPhaseVocoderHopSize()` static constexpr methods to `PitchShiftProcessor` in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (FR-011) returning `PhaseVocoderPitchShifter::kFFTSize` and `PhaseVocoderPitchShifter::kHopSize`
- [X] T023 [US2] Build dsp_tests and fix ALL compiler warnings and errors: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T024 [US2] Run all US2 tests and verify they all pass, then run the full dsp_tests suite to confirm zero regressions (SC-004): `build/windows-x64-release/bin/Release/dsp_tests.exe`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T025 [US2] Verify IEEE 754 compliance: check if `dsp/tests/unit/processors/pitch_shift_processor_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if so

### 3.4 Commit (MANDATORY)

- [X] T026 [US2] Commit completed User Story 2 work (Layer 2 shared-analysis API)

**Checkpoint**: User Story 2 fully functional and committed. Layer 2 API ready for HarmonizerEngine to consume.

---

## Phase 4: User Story 1 - Shared-Analysis FFT Reduces PhaseVocoder CPU Cost (Priority: P1)

**Goal**: Integrate the new Layer 2 API into `HarmonizerEngine`. Add `sharedStft_`, `sharedAnalysisSpectrum_`, and `pvVoiceScratch_` members. Modify `prepare()`, `reset()`, and `process()` to use the shared-analysis path when mode is PhaseVocoder. Run forward FFT once per block and share the spectrum across all active voices.

**Independent Test**: Benchmark 4-voice PhaseVocoder CPU before (recorded in T003) and after refactor. Total CPU must be measurably lower than the pre-refactor baseline (~24%). The output of HarmonizerEngine in PhaseVocoder mode must match the pre-refactor output within RMS < 1e-5 per voice over a 1-second test signal at 44.1 kHz (SC-002).

### 4.1 Tests for User Story 1 (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T027 [P] [US1] Write failing test: `HarmonizerEngine` PhaseVocoder mode with shared analysis produces per-voice output equivalent to pre-refactor output (SC-002, RMS < 1e-5 per voice over 1-second 440 Hz sine tone at 44.1 kHz) -- load the golden reference fixture captured in T003a and assert RMS difference per voice is < 1e-5 -- add to `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [X] T028 [P] [US1] Write failing test: `HarmonizerEngine` with 1 voice in PhaseVocoder mode functions correctly through the shared-analysis path (US1 acceptance scenario 3, degenerate single-voice case) -- add to `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [X] T029 [P] [US1] Write failing test: `HarmonizerEngine` sub-hop-size block handling -- use a 440 Hz sine tone test signal at 44.1 kHz; calling `process()` with 128 samples (less than `hopSize` = 1024) MUST buffer samples, NOT assert or error, and MUST zero-fill output for samples where no synthesis frame is ready (FR-013a) -- add to `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [X] T030 [P] [US1] Write failing test: `HarmonizerEngine::process()` in PhaseVocoder mode produces zero-filled output (not garbage) during the latency priming period before the first complete synthesis frame -- use a 440 Hz sine tone test signal at 44.1 kHz; assert that all output samples during the first `kFFTSize` (4096) input samples are zero (FR-013a; note: FR-008a's zero-fill contract applies at the HarmonizerEngine level, not inside `processWithSharedAnalysis()` -- HarmonizerEngine zero-fills output for any samples not returned by `pullSharedAnalysisOutput()`) -- add to `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [X] T031 [P] [US1] Write failing test: switching from PhaseVocoder mode back to Simple/Granular/PitchSync mode continues to use the standard per-voice `process()` path and produces correct output (FR-014) -- add to `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [X] T032 [P] [US1] Write benchmark test: 4-voice PhaseVocoder CPU measurement at 44.1 kHz/256 block size using the KrateDSP benchmark harness, recording both real-time CPU % and average process() time in µs/block (SC-001, SC-002) -- add to `dsp/tests/unit/systems/harmonizer_engine_test.cpp` or the benchmark target

### 4.2 Implementation for User Story 1

- [X] T033 [US1] Add `sharedStft_` (STFT), `sharedAnalysisSpectrum_` (SpectralBuffer), and `pvVoiceScratch_` (std::vector<float>) private members to `HarmonizerEngine` in `dsp/include/krate/dsp/systems/harmonizer_engine.h` per the contract in `specs/065-shared-analysis-fft-refactor/contracts/harmonizer-engine-api.md`
- [X] T034 [US1] Modify `HarmonizerEngine::prepare()` in `dsp/include/krate/dsp/systems/harmonizer_engine.h` to prepare shared resources: `sharedStft_.prepare(fftSize, hopSize, WindowType::Hann)`, `sharedAnalysisSpectrum_.prepare(fftSize)`, `pvVoiceScratch_.resize(maxBlockSize, 0.0f)` using `PitchShiftProcessor::getPhaseVocoderFFTSize()` and `getPhaseVocoderHopSize()` (FR-012, SC-005)
- [X] T035 [US1] Modify `HarmonizerEngine::reset()` in `dsp/include/krate/dsp/systems/harmonizer_engine.h` to additionally call `sharedStft_.reset()` and `sharedAnalysisSpectrum_.reset()` (FR-012)
- [X] T036 [US1] Implement the PhaseVocoder shared-analysis process path in `HarmonizerEngine::process()` in `dsp/include/krate/dsp/systems/harmonizer_engine.h`: (1) push input to `sharedStft_` once, (2) loop `while (sharedStft_.canAnalyze())`: analyze to `sharedAnalysisSpectrum_`, then for each active voice call `voice.pitchShifter.processWithSharedAnalysis(sharedAnalysisSpectrum_, pitchRatio)`, (3) pull output from each voice's OLA buffer via `pullSharedAnalysisOutput()` into `pvVoiceScratch_` (the new scratch buffer added in T033), (4) apply per-voice delay POST-pitch to the OLA output (FR-025 -- delays moved from pre-pitch to post-pitch in PhaseVocoder mode only; for motivation see research R-004), then use `voiceScratch_` (pre-existing HarmonizerEngine member) for the delay-processed intermediate, apply level, pan, and accumulate into `outputL`/`outputR` -- per the contract in `specs/065-shared-analysis-fft-refactor/contracts/harmonizer-engine-api.md` (FR-013, FR-013a, FR-017, FR-018, FR-019, FR-025). Note: `voiceScratch_` is a pre-existing member in HarmonizerEngine; verify it exists before adding a duplicate.
- [X] T037 [US1] Implement zero-fill contract in the HarmonizerEngine PhaseVocoder path: for any samples in `outputL`/`outputR` for which no synthesis frame was produced (sub-hop block or latency priming period), zero-fill those positions explicitly before returning (FR-013a). Note: this is HarmonizerEngine's responsibility -- `processWithSharedAnalysis()` has no output buffer parameter; it is the caller that must zero-fill output positions not covered by `pullSharedAnalysisOutput()` (see corrected FR-008a).
- [X] T038 [US1] Build dsp_tests and fix ALL compiler warnings and errors: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T039 [US1] Run all US1 tests and verify they all pass, then run the full dsp_tests suite to confirm zero regressions (SC-004): `build/windows-x64-release/bin/Release/dsp_tests.exe`
- [X] T040 [US1] Run the benchmark and record SC-001 results: PhaseVocoder 4-voice CPU % and µs/block using the KrateDSP benchmark harness under the Benchmark Contract from spec.md. The result MUST be < 18% CPU. Both metrics MUST be reported in the format: `PhaseVocoder 4 voices: CPU: 6.4%, process(): 374 us avg`

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T041 [US1] Verify IEEE 754 compliance: check if `dsp/tests/unit/systems/harmonizer_engine_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if so (verified: file already in -fno-fast-math list at line 390)

### 4.4 Commit (MANDATORY)

- [X] T042 [US1] Commit completed User Story 1 work (HarmonizerEngine shared-analysis integration)

**Checkpoint**: User Story 1 fully functional. PhaseVocoder 4-voice CPU < 18%, output equivalence verified. All tests pass.

---

## Phase 5: User Story 3 - Per-Voice OLA Buffer Isolation Verified (Priority: P1)

**Goal**: Verify that each voice's OLA buffer is independently writable and readable, and that processing voice N does not affect voice M's OLA output. This is a correctness requirement (FR-021 from spec 064, now addressed as FR-018/FR-019). The implementation of independent OLA buffers is inherent in the architecture (each `PhaseVocoderPitchShifter` owns its own `ola_` member), but correctness MUST be proven by test (SC-007).

**Note**: No new implementation code is required for this user story if US1 and US2 are implemented correctly. This phase is about writing the isolation verification tests that prove the invariant holds.

**Independent Test**: Process 2 voices at +7 and -5 semitones with shared analysis, verify each voice's output matches a standalone `PhaseVocoderPitchShifter` at the same ratio. Mute one voice mid-stream and verify the other is unaffected.

### 5.1 Tests for User Story 3 (Write FIRST -- Must FAIL if isolation is broken)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T043 [P] [US3] Write test: 2 voices at +7 and -5 semitones with shared analysis -- each voice's output matches a standalone `PhaseVocoderPitchShifter` at the same ratio within floating-point tolerance (SC-007, US3 acceptance scenario 1) -- add to `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [X] T044 [P] [US3] Write test: 4 voices at different ratios, mute all but one, verify the remaining voice's output is identical to a single standalone `PhaseVocoderPitchShifter` at that ratio (US3 acceptance scenario 2) -- add to `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [X] T045 [P] [US3] Write test: 2 voices processing simultaneously, mute one voice mid-stream, verify the remaining active voice's output is unaffected by the muted voice's OLA state (US3 acceptance scenario 3) -- add to `dsp/tests/unit/systems/harmonizer_engine_test.cpp`

### 5.2 Verify or Fix OLA Isolation

- [X] T046 [US3] Build dsp_tests and fix ALL compiler warnings and errors: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T047 [US3] Run all US3 OLA isolation tests. If any fail, identify the isolation violation and fix it in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` or `dsp/include/krate/dsp/systems/harmonizer_engine.h` -- OLA buffers MUST be strictly per-voice (FR-018 this spec; spec-064-FR-021 established the same requirement in the predecessor spec)
- [X] T048 [US3] Run the full dsp_tests suite and confirm zero regressions: `build/windows-x64-release/bin/Release/dsp_tests.exe`

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T049 [US3] Verify IEEE 754 compliance for the new US3 tests in `dsp/tests/unit/systems/harmonizer_engine_test.cpp` -- check for `std::isnan`/`std::isfinite`/`std::isinf` usage and add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed (verified: new T043/T044/T045 tests do not use IEEE 754 functions; file already in -fno-fast-math list at line 390 from T041)

### 5.4 Commit (MANDATORY)

- [ ] T050 [US3] Commit completed User Story 3 work (OLA isolation verification tests)

**Checkpoint**: Per-voice OLA isolation proven by automated tests. All three P1 user stories complete.

---

## Phase 6: User Story 4 - PitchShiftProcessor Public API Backward Compatibility (Priority: P2)

**Goal**: Verify that the entire existing dsp_tests suite passes without modification after the refactor. No new implementation code is required -- this user story is a verification gate proving the refactor preserved backward compatibility (FR-006, SC-004).

**Independent Test**: Run the entire dsp_tests suite and observe zero failures. All pre-existing test cases must pass unchanged.

### 6.1 Tests for User Story 4

- [ ] T051 [US4] Run the complete dsp_tests suite and record the full test output: `build/windows-x64-release/bin/Release/dsp_tests.exe` -- confirm the test count matches the baseline from T002 and all tests pass (SC-004)
- [ ] T052 [US4] Write one targeted regression test: a standalone `PitchShiftProcessor` (not used through `HarmonizerEngine`) calling `process()` in PhaseVocoder mode produces output identical to the pre-refactor baseline (US4 acceptance scenario 2) -- add to `dsp/tests/unit/processors/pitch_shift_processor_test.cpp`
- [ ] T053 [US4] Write one targeted test: calling `processWithSharedAnalysis()` on a `PitchShiftProcessor` in Simple, Granular, and PitchSync modes is a documented no-op -- assert that `pullSharedAnalysisOutput()` returns 0 after the call (FR-009a: the no-op has no output buffer parameter; it is observable through `pullSharedAnalysisOutput()` returning 0, not through zero-filling a buffer; this aligns with the corrected FR-009a wording) (US4 acceptance scenario 3) -- add to `dsp/tests/unit/processors/pitch_shift_processor_test.cpp`
- [ ] T054 [US4] Build dsp_tests and fix ALL compiler warnings and errors: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T055 [US4] Run the full dsp_tests suite and confirm zero regressions (SC-004): `build/windows-x64-release/bin/Release/dsp_tests.exe`

### 6.2 Cross-Platform Verification (MANDATORY)

- [ ] T056 [US4] Verify IEEE 754 compliance for any new test additions -- check for `std::isnan`/`std::isfinite`/`std::isinf` usage and add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 6.3 Commit (MANDATORY)

- [ ] T057 [US4] Commit completed User Story 4 work (backward compatibility verification)

**Checkpoint**: Backward compatibility proven. All four user stories (P1 + P2) complete and committed.

---

## Phase 7: User Story 5 - PitchSync Mode Investigation and Re-Benchmark (Priority: P3)

**Goal**: Re-benchmark PitchSync mode after the refactor, document the measurement, and produce a written analysis of whether shared pitch detection is architecturally feasible. This is an informational deliverable -- no PitchSync optimization is implemented in this spec.

**Independent Test**: Benchmark PitchSync 4-voice mode using the KrateDSP benchmark harness under the same Benchmark Contract used in T003. Record both real-time CPU % and µs/block. Document whether the result changed from the spec 064 baseline (~26.4% CPU vs 3% budget).

### 7.1 Benchmark and Analysis

- [ ] T058 [US5] Run PitchSync 4-voice benchmark using the KrateDSP benchmark harness under identical conditions to spec 064 (44.1 kHz, block size 256, 4 voices, Release build, realtime priority, 2s warmup, 10s steady-state) and record both real-time CPU % and average process() time in µs/block (SC-008)
- [ ] T059 [US5] Write analysis document (or inline test comments) documenting: (a) the measured PitchSync CPU values vs the spec 064 baseline (~26.4%), (b) whether the shared-analysis refactor incidentally improved PitchSync (expected: no improvement -- the refactor does not touch PitchSync code), (c) whether shared pitch detection (running YIN once per block and sharing the result across voices) is architecturally feasible and an estimate of potential CPU savings (US5 acceptance scenario 2)

### 7.2 Commit (MANDATORY)

- [ ] T060 [US5] Commit completed User Story 5 work (PitchSync re-benchmark and analysis)

**Checkpoint**: PitchSync re-benchmark documented. All five user stories complete.

---

## Phase 8: Polish and Cross-Cutting Concerns

**Purpose**: Zero-allocation verification, code inspection, and any cleanup identified during implementation.

- [ ] T061 [P] Verify SC-005 (zero heap allocations during processing): code inspection of `processWithSharedAnalysis()`, `pullOutputSamples()`, `HarmonizerEngine::process()` PhaseVocoder path -- confirm all allocations happen only in `prepare()` and that no `std::vector` resize, `new`/`delete`, or `std::make_unique` appears in any audio-path method
- [ ] T062 [P] Verify FR-024 (no spectrum pointer retention): code inspection of `PhaseVocoderPitchShifter` and `PitchShiftProcessor::Impl` -- confirm no member variable stores a pointer or reference to the externally provided `SpectralBuffer` beyond the duration of a `processFrame()` call
- [ ] T063 [P] Verify FR-019 (const-reference enforcement): confirm that `sharedAnalysisSpectrum_` is passed only as `const SpectralBuffer&` to all voice methods and that no voice method has a non-const overload path that could accept it
- [ ] T064 Review and update any inline documentation comments for the new methods in `dsp/include/krate/dsp/processors/pitch_shift_processor.h` and `dsp/include/krate/dsp/systems/harmonizer_engine.h` to match the final implementation (doxygen-style comments per existing code conventions)
- [ ] T065 Run the full dsp_tests suite one final time to confirm clean state: `build/windows-x64-release/bin/Release/dsp_tests.exe`

---

## Phase 9: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion (Constitution Principle XIV).

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task.

### 9.1 Architecture Documentation Update

- [ ] T066 Update `specs/_architecture_/layer-2-processors.md` (or equivalent architecture inventory file) to document the new `processWithSharedAnalysis()` / `pullOutputSamples()` / `outputSamplesAvailable()` public methods added to `PhaseVocoderPitchShifter`, including: purpose, full public API signatures, file location (`dsp/include/krate/dsp/processors/pitch_shift_processor.h`), "when to use this" (when a caller owns the forward FFT and wants to inject a pre-computed analysis spectrum), usage example, and note that `processFrame()` now accepts `const SpectralBuffer& analysis` and `SpectralBuffer& synthesis` parameters
- [ ] T067 Update `specs/_architecture_/layer-3-systems.md` (or equivalent architecture inventory file) to document the shared-analysis pattern added to `HarmonizerEngine`: shared STFT + SpectralBuffer members, the PhaseVocoder mode branch in `process()`, the delay-post-pitch design decision (R-004), and the "review trigger" for when a second multi-voice spectral system should prompt extracting the pattern into a reusable utility
- [ ] T068 Update `specs/_architecture_/layer-2-processors.md` to document the new `PitchShiftProcessor` shared-analysis delegation methods (`processWithSharedAnalysis`, `pullSharedAnalysisOutput`, `sharedAnalysisSamplesAvailable`, `getPhaseVocoderFFTSize`, `getPhaseVocoderHopSize`) and the FR-009a no-op contract for non-PhaseVocoder modes

### 9.2 Final Commit

- [ ] T069 Commit architecture documentation updates
- [ ] T070 Verify all spec work is committed to feature branch: `git status` should show clean working tree

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 10.1 Run Clang-Tidy Analysis

- [ ] T071 Generate compile_commands.json for clang-tidy by running `cmake --preset windows-ninja` from a VS Developer PowerShell (one-time setup if not already done)
- [ ] T072 Run clang-tidy on all modified source files: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`

### 10.2 Address Findings

- [ ] T073 Fix all errors reported by clang-tidy (blocking issues)
- [ ] T074 Review warnings and fix where appropriate; add `// NOLINT(rule-name): reason` comments for any warnings intentionally suppressed in DSP code (e.g., magic constants used for FFT sizes)
- [ ] T075 Rebuild and re-run dsp_tests after clang-tidy fixes: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe`

**Checkpoint**: Static analysis clean -- ready for completion verification

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion (Constitution Principle XVI).

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

- [ ] T076 Verify FR-001 through FR-019 and FR-023 through FR-025: for each, open the implementation file, find the code satisfying it, record the file path and line number in the spec.md compliance table -- DO NOT fill from memory. Note: FR-025 (delay-post-pitch in PhaseVocoder mode) is a new requirement added during analysis; verify the HarmonizerEngine implementation applies delays after `pullSharedAnalysisOutput()` in the PhaseVocoder path.
- [ ] T077 Verify SC-001 (PhaseVocoder 4-voice CPU < 18%): record the actual measured value from T040 vs the spec target, report both real-time CPU % and µs/block
- [ ] T078 Verify SC-002 (HarmonizerEngine output equivalence RMS < 1e-5 per voice): run the equivalence test from T027 and record the actual RMS difference
- [ ] T079 Verify SC-003 (processWithSharedAnalysis output identical to process(), max error < 1e-5): run the equivalence test from T010 and record the actual max sample error
- [ ] T080 Verify SC-004 (zero regressions in dsp_tests): record the full test count and confirm it matches T002 baseline
- [ ] T081 Verify SC-005 (zero heap allocations in audio path): record the result of the code inspection from T061
- [ ] T082 Verify SC-006 (phase locking quality preserved, error < 1e-5): run the phase locking equivalence test from T012 and record the actual max error
- [ ] T083 Verify SC-007 (per-voice OLA independence): record the results of the isolation tests from T043, T044, T045
- [ ] T084 Verify SC-008 (PitchSync re-benchmark documented): record the measured values from T058

### 11.2 Fill Compliance Table in spec.md

- [ ] T085 Update `specs/065-shared-analysis-fft-refactor/spec.md` "Implementation Verification" section with compliance status, file paths, line numbers, test names, and measured values for every FR-xxx and SC-xxx row (including the new FR-025 row added during analysis) -- no row may be filled with only "implemented" or "test passes" without specific evidence

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T086 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete -- ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim.

### 12.1 Final Commit

- [ ] T087 Commit all remaining spec work to feature branch (if any uncommitted changes remain after T085/T086)
- [ ] T088 Run the full dsp_tests suite one final time to confirm clean state: `build/windows-x64-release/bin/Release/dsp_tests.exe`

### 12.2 Completion Claim

- [ ] T089 Claim completion ONLY if all requirements in spec.md are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies and Execution Order

### Phase Dependencies

```
Phase 1 (Setup/Baseline)
    --> Phase 2 (processFrame Refactor -- BLOCKS all stories)
        --> Phase 3 (US2: Layer 2 API -- BLOCKS Phase 4)
            --> Phase 4 (US1: HarmonizerEngine Integration)  [P1 - primary deliverable]
            --> Phase 5 (US3: OLA Isolation Verification)    [P1 - can start after Phase 3]
            --> Phase 6 (US4: Backward Compat Verification)  [P2 - can start after Phase 3]
            --> Phase 7 (US5: PitchSync Re-benchmark)        [P3 - can start after Phase 4]
    --> Phase 8 (Polish)           [after all user stories]
    --> Phase 9 (Architecture Docs) [after Polish]
    --> Phase 10 (Clang-Tidy)      [after Architecture Docs]
    --> Phase 11 (Completion Verification) [after Clang-Tidy]
    --> Phase 12 (Final Completion) [after Verification]
```

### User Story Dependencies

- **Phase 2 (processFrame Refactor)**: MUST complete before any user story work. Blocks US1, US2, US3, US4, US5.
- **Phase 3 (US2: Layer 2 API)**: MUST complete before Phase 4 (US1) because HarmonizerEngine calls the new Layer 2 methods. Can start immediately after Phase 2.
- **Phase 4 (US1: HarmonizerEngine)**: Requires Phase 3 (US2). Primary performance deliverable. SC-001 measured here.
- **Phase 5 (US3: OLA Isolation)**: Requires Phase 3 (US2). Can proceed in parallel with Phase 4 once Phase 3 completes. Tests validate the invariant established by US1/US2.
- **Phase 6 (US4: Backward Compat)**: Requires Phase 3 (US2). Can proceed in parallel with Phases 4 and 5 -- it is purely a test/verification phase with no new implementation.
- **Phase 7 (US5: PitchSync Re-benchmark)**: Requires Phase 4 (US1) to be complete before benchmarking (the refactor must be in place). Information-only deliverable.

### Within Each User Story

- Tests FIRST: Tests MUST be written and FAIL before implementation (Principle XII)
- Implement to make tests pass
- Build with zero warnings
- Run full suite to confirm zero regressions
- Cross-platform check (IEEE 754 compliance)
- Commit last

### Parallel Opportunities

**Phase 3 (US2 tests)**: T010, T011, T012, T013, T014, T015, T016, T017, T018, T018a, T018b are all marked [P] -- all test files are additive to the same test file but work on independent test functions

**Phase 4 (US1 tests)**: T027, T028, T029, T030, T031, T032 are all marked [P]

**Phase 5 (US3 tests)**: T043, T044, T045 are all marked [P]

**After Phase 3 completes**: Phases 4, 5, and 6 can proceed in parallel (different files, independent voice logic vs isolation tests vs regression tests)

**Phase 8 (Polish)**: T061, T062, T063 are all marked [P] -- independent code inspection tasks

---

## Parallel Execution Example: Phase 3 (US2 Tests)

```bash
# All US2 tests can be written in parallel (same file, independent TEST_CASE blocks):
Task A: T010 -- equivalence test (standard path vs shared path)
Task B: T011 -- formant preservation with shared analysis
Task C: T012 -- phase locking with shared analysis
Task D: T013 -- transient detection with shared analysis
Task E: T014/T015/T016 -- degenerate condition tests (unprepared, size mismatch, priming)
Task F: T017/T018 -- PitchShiftProcessor delegation + FFT/hop accessor tests
Task G: T018a/T018b -- frame-timing invariant + unity pitch no-bypass tests
```

---

## Parallel Execution Example: After Phase 3 Completes

```bash
# Three workstreams can proceed simultaneously:
Developer A: Phase 4 (US1: HarmonizerEngine integration)
Developer B: Phase 5 (US3: OLA isolation tests) -- no new implementation, test-only
Developer C: Phase 6 (US4: Backward compat verification) -- no new implementation, test-only
```

---

## Implementation Strategy

### MVP First (P1 Stories Only -- Phases 1-5)

1. Complete Phase 1: Baseline
2. Complete Phase 2: processFrame Refactor (CRITICAL -- blocks all stories)
3. Complete Phase 3: US2 Layer 2 API (CRITICAL -- blocks US1)
4. Complete Phase 4: US1 HarmonizerEngine shared-analysis path
5. Complete Phase 5: US3 OLA isolation tests
6. **STOP and VALIDATE**: Benchmark SC-001 (<18% CPU), verify SC-002/SC-003/SC-007
7. If SC-001 passes, the primary deliverable is complete

### Incremental Delivery

1. Phase 1+2 complete → processFrame refactor committed, all tests green
2. Phase 3 complete → Layer 2 API ready, testable in isolation
3. Phase 4 complete → HarmonizerEngine optimization live, benchmarked
4. Phase 5 complete → OLA isolation correctness proven
5. Phase 6 complete → Backward compatibility verified
6. Phase 7 complete → PitchSync investigation documented
7. Phases 8-12 → Polish, docs, static analysis, honest completion claim

---

## Notes

- [P] tasks = different functions/independent work, no dependencies between them
- [Story] label maps task to specific user story for traceability
- Tests MUST be written before implementation (Principle XII) -- the test file MUST compile and FAIL before writing implementation code
- **MANDATORY**: Verify cross-platform IEEE 754 compliance after each user story (add test files to `-fno-fast-math` list if needed)
- **MANDATORY**: Commit at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest evidence (Principle XVI)
- **NEVER claim completion if ANY requirement is not met** -- document gaps honestly instead
- The processFrame refactor (Phase 2) is the highest-risk task: it touches a private method called by the existing `process()` path. Any signature error will break all existing PhaseVocoder tests immediately.
- The delay-post-pitch design decision (FR-025, documented in research R-004) is architecturally significant: per-voice onset delays (0-50ms) are moved from pre-pitch to post-pitch in PhaseVocoder mode only. This is audibly transparent for humanization but MUST be documented in architecture docs and in test comments. Before implementing T036, verify that `voiceScratch_` is a pre-existing member of HarmonizerEngine (not a new member to add); if absent, add it to T033.
- SC-001 measurement MUST use the KrateDSP benchmark harness under the exact conditions specified in the Benchmark Contract in spec.md. DAW CPU meters are PROHIBITED for engineering verification.
- FR-020, FR-021, FR-022 (shared peak detection, shared transient detection, shared envelope extraction) are explicitly DEFERRED. Do NOT implement them in this spec.
