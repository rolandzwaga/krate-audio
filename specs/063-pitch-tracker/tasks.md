# Tasks: Pitch Tracking Robustness (PitchTracker)

**Input**: Design documents from `/specs/063-pitch-tracker/`
**Branch**: `063-pitch-tracker`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/pitch_tracker_api.h, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, dsp-architecture) - no manual context verification required.

### Build Commands (Windows - MUST use full CMake path)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run PitchTracker tests only
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "PitchTracker*"

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
```

### Cross-Platform Compatibility Check (MANDATORY after each user story)

After implementing tests, verify IEEE 754 compliance in `dsp/tests/CMakeLists.txt`:

```cmake
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        unit/primitives/pitch_tracker_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()
```

This is required because VST3 SDK enables `-ffast-math` globally, breaking `std::isnan`/`std::isfinite`.

---

## Phase 1: Setup (CMake Registration)

**Purpose**: Register new files in the build system so all subsequent phases compile correctly.

**Note**: No new directories are required. The header goes into the existing `dsp/include/krate/dsp/primitives/` directory and the test goes into `dsp/tests/unit/primitives/` - both already exist.

- [X] T001 Register `pitch_tracker.h` in `dsp/CMakeLists.txt` by adding it to the `KRATE_DSP_PRIMITIVES_HEADERS` list (follow existing pattern for `pitch_detector.h`)
- [X] T002 Register `pitch_tracker_test.cpp` in `dsp/tests/CMakeLists.txt` by adding it to the test sources list (follow existing pattern for `pitch_detector_test.cpp`)
- [X] T003 Add `dsp/tests/unit/primitives/pitch_tracker_test.cpp` to the `-fno-fast-math` source file properties list in `dsp/tests/CMakeLists.txt` to ensure IEEE 754 compliance on Clang/GCC
- [X] T004 Verify the build system change compiles cleanly: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests` (expect linker error for missing test file, not a CMake error)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The single header file that ALL four user stories depend on. This MUST exist before any test can be written or implementation can begin.

**CRITICAL**: No user story work can begin until this phase is complete.

- [X] T005 Create the empty class skeleton `dsp/include/krate/dsp/primitives/pitch_tracker.h` matching the API contract in `specs/063-pitch-tracker/contracts/pitch_tracker_api.h` exactly: class declaration with all public methods, all private member fields (with defaults from data-model.md), all constants, correct `#include` directives (`pitch_detector.h`, `smoother.h`, `pitch_utils.h`, `midi_utils.h`), and `namespace Krate::DSP`. All method bodies MUST return stub values (`return {};`, `return 0;`, `return 0.0f;`, `return false;`) so the code compiles but tests fail.
- [X] T006 Verify the skeleton compiles without errors or warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests`

**Checkpoint**: The empty PitchTracker class skeleton compiles cleanly. All user story test writing can now begin.

---

## Phase 3: User Story 1 - Stable Pitch Input for Diatonic Harmonizer (Priority: P1) - MVP

**Goal**: PitchTracker produces a consistent, non-warbling MIDI note from stable and near-stable pitched input by composing all five pipeline stages into a working `pushBlock()` / `getMidiNote()` / `getFrequency()` implementation.

**Independent Test**: Feed known pitch sequences (stable sine, jittered sine, clean A4-to-B4 transition) into PitchTracker and verify MIDI note output is stable and transitions cleanly. No harmonizer engine required.

**Acceptance Scenarios from spec.md**: SC-001, SC-002, SC-004, SC-007, SC-008, SC-009.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins. The skeleton from T005 ensures they compile but fail.

- [X] T007 [P] [US1] Write SC-001 test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: feed 2 seconds of 440Hz sine at 44100Hz into `PitchTracker::pushBlock()`, verify `getMidiNote()` returns 69 with zero note switches over the observation window
- [X] T008 [P] [US1] Write SC-002 test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: feed 2 seconds of 440Hz sine with +/- 20 cents random jitter (hysteresis default 50 cents), verify zero note switches
- [X] T009 [P] [US1] Write SC-004 test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: feed A4 (440Hz) then B4 (493.88Hz) transition, verify exactly one note switch (69 -> 71) occurring within 100ms of the transition point
- [X] T010 [P] [US1] Write SC-007 test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: measure incremental CPU overhead of PitchTracker beyond PitchDetector (verify it is negligible; test comment documents the budget is <0.1% at 44.1kHz)
- [X] T011 [P] [US1] Write SC-008 test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: inspect implementation that `pushBlock()` contains no allocating calls (code inspection test with assertion that allocator call count is zero - or document as inspection-only with a comment citing FR-011)
- [X] T012 [P] [US1] Write SC-009 test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: feed a 512-sample block with 256-sample window, verify tracker state reflects the SECOND detection result (second hop processed), not just the first
- [X] T013 [P] [US1] Write FR-001 pushBlock/internal-detect test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: verify `pushBlock()` with a block larger than windowSize triggers multiple pipeline executions (observable via note state changes reflecting multiple hops)
- [X] T014 [P] [US1] Write FR-006 frequency-smoother separation test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: verify `getMidiNote()` returns the committed integer note while `getFrequency()` returns a smoothed value that is NOT the exact center frequency of the note immediately after commitment (smoother lags behind)
- [X] T015 [US1] Verify ALL User Story 1 tests compile and FAIL (no implementation yet): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "PitchTracker*"`

### 3.2 Implementation for User Story 1

- [X] T016 [US1] Implement `prepare()` in `dsp/include/krate/dsp/primitives/pitch_tracker.h`: call `detector_.prepare(sampleRate, windowSize)`, compute `hopSize_ = windowSize / 4`, compute `minNoteDurationSamples_ = static_cast<std::size_t>(minNoteDurationMs_ / 1000.0 * sampleRate)`, configure `frequencySmoother_.configure(kDefaultFrequencySmoothingMs, static_cast<float>(sampleRate))`, store `sampleRate_` and `windowSize_`, then call `reset()`
- [X] T017 [US1] Implement `reset()` in `dsp/include/krate/dsp/primitives/pitch_tracker.h`: zero `pitchHistory_`, reset `historyIndex_`, `historyCount_`, `currentNote_ = -1`, `candidateNote_ = -1`, `noteHoldTimer_ = 0`, `samplesSinceLastHop_ = 0`, `pitchValid_ = false`, `smoothedFrequency_ = 0.0f`, call `detector_.reset()` and `frequencySmoother_.reset()`
- [X] T018 [US1] Implement `computeMedian()` const helper in `dsp/include/krate/dsp/primitives/pitch_tracker.h`: copy `historyCount_` entries from `pitchHistory_` ring buffer into a scratch `std::array<float, kMaxMedianSize>`, perform insertion sort on the scratch array, return the middle element. Handle the `historyCount_ == 0` edge case by returning 0.0f. Handle `medianSize_ == 1` by returning `pitchHistory_[historyIndex_ == 0 ? 0 : historyIndex_ - 1]` (last written value) as an optimization.
- [X] T019 [US1] Implement `runPipeline()` private method in `dsp/include/krate/dsp/primitives/pitch_tracker.h` covering all five stages in the fixed order from spec.md:
  - Stage 1 (Confidence Gate): read `detector_.getConfidence()`; if below `confidenceThreshold_`, set `pitchValid_ = false` and return early
  - Stage 2 (Median Filter): set `pitchValid_ = true`, write `detector_.getDetectedFrequency()` into `pitchHistory_[historyIndex_]`, increment `historyIndex_` mod `medianSize_`, increment `historyCount_` capped at `medianSize_`, compute `medianFreq = computeMedian()`
  - Stage 3 (Hysteresis): if `currentNote_ == -1`, proceed to stage 4 unconditionally; else compute cents distance as `std::abs(frequencyToMidiNote(medianFreq) - static_cast<float>(currentNote_)) * 100.0f`; if distance <= `hysteresisThreshold_`, clear `candidateNote_` and return; else set `candidateNote_ = static_cast<int>(std::round(frequencyToMidiNote(medianFreq)))`
  - Stage 4 (Min Note Duration): if `currentNote_ == -1`, commit immediately: set `currentNote_ = static_cast<int>(std::round(frequencyToMidiNote(medianFreq)))`, set `candidateNote_ = -1`, `noteHoldTimer_ = 0`, call `frequencySmoother_.snapTo(midiNoteToFrequency(currentNote_))`; else if `candidateNote_` unchanged, increment `noteHoldTimer_` by `hopSize_`; if `noteHoldTimer_ >= minNoteDurationSamples_`, commit candidate; if `candidateNote_` changed, reset `noteHoldTimer_ = 0`
  - Stage 5 (Frequency Smoother): call `frequencySmoother_.setTarget(midiNoteToFrequency(currentNote_))` when note committed; advance smoother with `frequencySmoother_.advanceSamples(hopSize_)`, update `smoothedFrequency_ = frequencySmoother_.getCurrentValue()`
- [X] T020 [US1] Implement `pushBlock()` in `dsp/include/krate/dsp/primitives/pitch_tracker.h`: iterate over all `numSamples` input samples; for each sample call `detector_.push(samples[i])` and increment `samplesSinceLastHop_`; when `samplesSinceLastHop_ >= hopSize_`, call `runPipeline()` and reset `samplesSinceLastHop_ = 0`
- [X] T021 [US1] Implement query methods in `dsp/include/krate/dsp/primitives/pitch_tracker.h`: `getFrequency()` returns `smoothedFrequency_`, `getMidiNote()` returns `currentNote_`, `getConfidence()` returns `detector_.getConfidence()`, `isPitchValid()` returns `pitchValid_`
- [X] T022 [US1] Build DSP tests and verify zero compiler warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T023 [US1] Run User Story 1 tests and verify they pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "PitchTracker*"`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T024 [US1] Verify `dsp/tests/unit/primitives/pitch_tracker_test.cpp` is listed in the `-fno-fast-math` source file properties in `dsp/tests/CMakeLists.txt` (added in T003). Re-verify the CMakeLists block is syntactically correct by doing a clean build.

### 3.4 Commit (MANDATORY)

- [X] T025 [US1] Commit User Story 1 work: `git commit` with message "Implement PitchTracker core pipeline (US1: stable pitch input)"

**Checkpoint**: PitchTracker core pipeline is fully functional and committed. SC-001, SC-002, SC-004, SC-007, SC-008, SC-009 verified.

---

## Phase 4: User Story 2 - Graceful Handling of Unvoiced Segments (Priority: P2)

**Goal**: PitchTracker holds the last valid note during silence, noise, and unvoiced segments, correctly reporting `isPitchValid() == false` during those segments and resuming tracking when voiced signal returns.

**Independent Test**: Feed alternating pitched and silent/noise segments, verify `isPitchValid()` correctly reflects confidence gate state and `getMidiNote()` holds last valid note during unvoiced segments.

**Acceptance Scenarios from spec.md**: SC-005.

**Note**: US2 depends on the US1 implementation (same class, same file). US2 tests extend the existing test file with new test cases that exercise the hold-last-valid-note behavior.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Write new test cases that FAIL with default confidence threshold behavior before any changes.

- [ ] T026 [P] [US2] Write SC-005 voiced/silent alternating test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: feed 500ms of 440Hz sine (voiced), then 500ms of silence (unvoiced, confidence drops below 0.5), verify `isPitchValid()` is false during silence and `getMidiNote()` returns 69 throughout
- [ ] T027 [P] [US2] Write FR-004 confidence-gate hold-state test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: feed pitched A4 to establish committed note, then feed white noise (low confidence), verify `isPitchValid() == false`, `getMidiNote() == 69` (held), `getFrequency()` is non-zero (smoother holds last valid value), and `getConfidence()` returns the raw confidence value from the underlying PitchDetector (not 0 or -1 -- the delegation to `detector_.getConfidence()` must be verified to be a direct pass-through, not filtered)
- [ ] T028 [P] [US2] Write FR-004 resume-after-silence test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: after a silent segment where `isPitchValid() == false`, feed C5 (523.25Hz), verify tracker eventually transitions to MIDI note 72 and `isPitchValid()` returns true
- [ ] T029 [US2] Verify new User Story 2 tests compile and FAIL as expected: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "PitchTracker*"`

### 4.2 Implementation for User Story 2

The confidence gate behavior (stage 1 of `runPipeline()`) was already implemented in T019. US2 tests verify that behavior is correct for unvoiced hold-last-note semantics. If any US2 test fails, fix the gap in `runPipeline()` stage 1:

- [ ] T030 [US2] Verify stage 1 of `runPipeline()` in `dsp/include/krate/dsp/primitives/pitch_tracker.h` correctly sets `pitchValid_ = false` and returns WITHOUT modifying `currentNote_`, `candidateNote_`, or `noteHoldTimer_` when confidence is below `confidenceThreshold_`. Fix if incorrect.
- [ ] T031 [US2] Verify that `getFrequency()` returns the last smoothed frequency (not 0) during unvoiced segments (smoother is not reset on low-confidence frames). Fix `runPipeline()` stage 1 early-return if it incorrectly resets smoother state.
- [ ] T032 [US2] Build DSP tests and verify zero compiler warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T033 [US2] Run all PitchTracker tests and verify they pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "PitchTracker*"`

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T034 [US2] Confirm that no new source files were added (US2 tests extend the existing `pitch_tracker_test.cpp`); no additional `-fno-fast-math` changes needed beyond T003.

### 4.4 Commit (MANDATORY)

- [ ] T035 [US2] Commit User Story 2 work: `git commit` with message "Add confidence gating hold-state tests (US2: unvoiced segment handling)"

**Checkpoint**: Confidence gate hold-state behavior verified and committed. SC-005 verified.

---

## Phase 5: User Story 4 - Elimination of Single-Frame Outliers (Priority: P2)

**Note**: US4 is listed as P2 in spec.md, same priority as US2. US4 is placed here because it exercises the median filter in isolation, which is simpler than US2 and serves as an independent correctness verification of the ring buffer logic.

**Goal**: PitchTracker rejects single-frame and two-frame octave-jump outliers (high-confidence) via the median filter, preventing momentary pitch glitches from reaching the committed note output.

**Independent Test**: Inject known outlier frequency values with high confidence into an otherwise stable pitch sequence and verify the median filter rejects them from affecting the committed note.

**Acceptance Scenarios from spec.md**: SC-003.

### 5.1 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T036 [P] [US4] Write SC-003 single-frame-outlier test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: synthesize a sequence of confident pitch detections [440, 440, 880, 440, 440] (one octave-jump outlier with high confidence), process with median size 5, verify committed note NEVER switches to A5 (880Hz = MIDI 81) during the outlier frame
- [ ] T037 [P] [US4] Write two-consecutive-outliers test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: synthesize [440, 880, 880, 440, 440] with high confidence, verify output note is still A4 (median of sorted [440, 440, 440, 880, 880] = 440)
- [ ] T038 [P] [US4] Write FR-013 ring-buffer-not-full test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: when fewer than `medianSize_` confident frames have arrived (historyCount_ < medianSize_), verify `computeMedian()` uses only the available frames (no uninitialized zero values skewing the result)
- [ ] T039 [US4] Verify new User Story 4 tests compile and FAIL: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "PitchTracker*"`

### 5.2 Implementation for User Story 4

The median filter was implemented in T018 and T019. US4 tests verify the ring buffer ring-buffer-not-full edge case and the outlier rejection math. Fix any gaps:

- [ ] T040 [US4] Verify `computeMedian()` in `dsp/include/krate/dsp/primitives/pitch_tracker.h` uses `historyCount_` (not `medianSize_`) to determine how many entries to copy to scratch array, so partial-buffer median works correctly. Fix if it uses `medianSize_` unconditionally.
- [ ] T041 [US4] Verify ring buffer insertion in `runPipeline()` stage 2 wraps `historyIndex_` modulo `medianSize_` (not `kMaxMedianSize`), so changing `medianSize_` via `setMedianFilterSize()` correctly uses the new window without reading stale entries outside the current window. Fix if wrapping is incorrect.
- [ ] T042 [US4] Build DSP tests and verify zero compiler warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T043 [US4] Run all PitchTracker tests and verify they pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "PitchTracker*"`

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T044 [US4] No new source files added; confirm the existing `-fno-fast-math` entry for `pitch_tracker_test.cpp` in `dsp/tests/CMakeLists.txt` covers these new test cases.

### 5.4 Commit (MANDATORY)

- [ ] T045 [US4] Commit User Story 4 work: `git commit` with message "Add median filter outlier rejection tests (US4: single-frame outlier elimination)"

**Checkpoint**: Median filter ring buffer logic verified correct. SC-003 verified.

---

## Phase 6: User Story 3 - Configurable Tracking Behavior (Priority: P3)

**Goal**: Each configuration setter (`setMedianFilterSize()`, `setHysteresisThreshold()`, `setConfidenceThreshold()`, `setMinNoteDuration()`) independently and measurably affects tracking behavior so that users can tune the tracker for different musical contexts.

**Independent Test**: Test each setter in isolation by verifying that changing its value produces measurably different behavior on a controlled input sequence, with all other parameters held at defaults.

**Acceptance Scenarios from spec.md**: Spec US3 acceptance scenarios 1, 2, 3.

### 6.1 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T046 [P] [US3] Write `setMinNoteDuration()` effect test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: feed 5 rapid note changes per second (200ms per note), verify that with default 50ms min duration the tracker suppresses SOME transitions (fewer output switches than input), then set duration to 20ms and verify MORE transitions pass through
- [ ] T047 [P] [US3] Write `setHysteresisThreshold()` effect test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: feed a signal hovering at the boundary between two notes (within 50 cents); with default 50 cent hysteresis verify no switching; reduce to 10 cents and verify the tracker may switch
- [ ] T048 [P] [US3] Write `setConfidenceThreshold()` effect test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: feed a signal with medium confidence (e.g. 0.35); with default threshold 0.5 verify frames are rejected (isPitchValid() == false); lower threshold to 0.2 and verify frames are accepted (isPitchValid() == true)
- [ ] T049 [P] [US3] Write `setMedianFilterSize()` validation test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: set size to 1 and verify median of single value is that value; set size to 11 and verify maximum window operates correctly; set size to 0 or 12 and verify it is clamped to valid range [1, kMaxMedianSize]
- [ ] T050 [P] [US3] Write `setMedianFilterSize()` history-reset test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: establish 5 history entries, then call `setMedianFilterSize(3)`, verify `historyCount_` is reset to 0 (history cleared on size change, per contract doc)
- [ ] T051 [P] [US3] Write `setMinNoteDuration(0ms)` edge case test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: verify immediate note transitions (no hold timer delay); also test `setHysteresisThreshold(0)` produces a tracker that proposes a candidate on any pitch change
- [ ] T052 [US3] Verify new User Story 3 tests compile and FAIL: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "PitchTracker*"`

### 6.2 Implementation for User Story 3

- [ ] T053 [US3] Implement `setMedianFilterSize()` in `dsp/include/krate/dsp/primitives/pitch_tracker.h`: clamp `size` to `[1, kMaxMedianSize]`, update `medianSize_`, reset `historyIndex_ = 0` and `historyCount_ = 0` (clear history on size change per contract spec)
- [ ] T054 [US3] Implement `setHysteresisThreshold()` in `dsp/include/krate/dsp/primitives/pitch_tracker.h`: store `cents` in `hysteresisThreshold_` (no clamping required; 0 is valid and disables hysteresis)
- [ ] T055 [US3] Implement `setConfidenceThreshold()` in `dsp/include/krate/dsp/primitives/pitch_tracker.h`: store `threshold` in `confidenceThreshold_`
- [ ] T056 [US3] Implement `setMinNoteDuration()` in `dsp/include/krate/dsp/primitives/pitch_tracker.h`: store `ms` in `minNoteDurationMs_`, recompute `minNoteDurationSamples_ = static_cast<std::size_t>(ms / 1000.0 * sampleRate_)` (0ms -> 0 samples -> immediate commit)
- [ ] T057 [US3] Build DSP tests and verify zero compiler warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T058 [US3] Run all PitchTracker tests and verify they pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "PitchTracker*"`

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T059 [US3] No new source files added; confirm the existing `-fno-fast-math` entry covers these test cases.

### 6.4 Commit (MANDATORY)

- [ ] T060 [US3] Commit User Story 3 work: `git commit` with message "Implement and test configuration setters (US3: configurable tracking behavior)"

**Checkpoint**: All four configuration setters implemented and tested. All three US3 acceptance scenarios verified.

---

## Phase 7: Edge Cases and FR Coverage

**Purpose**: Verify all functional requirements not fully exercised by user story tests. These are cross-cutting correctness checks derived from spec.md FR requirements and edge cases section.

- [ ] T061 [P] Write FR-007 prepare() reset-state test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: call `prepare()` after establishing tracking state, verify all state is reset (currentNote_ == -1, historyCount_ == 0, smoothedFrequency_ == 0)
- [ ] T062 [P] Write FR-008 reset() preserves-config test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: configure non-default parameters, call `reset()`, verify configuration values are unchanged (medianSize_ still set, hysteresisThreshold_ still set) but state is cleared
- [ ] T063 [P] Write FR-015 first-detection-bypasses-both-stages test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: verify that with `currentNote_ == -1`, the first confident detection with high confidence commits a note IMMEDIATELY without waiting for `minNoteDurationSamples_` and without hysteresis check
- [ ] T064 [P] Write FR-016 sub-hop block accumulation test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: call `pushBlock()` with a block smaller than `hopSize` (e.g., 32 samples with 64-sample hop), verify tracker state is unchanged (no pipeline run triggered, `isPitchValid()` returns same value as before)
- [ ] T065 [P] Write FR-012 layer-boundary test as a comment/compile-time check in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: include only `<krate/dsp/primitives/pitch_tracker.h>` and verify it compiles without needing any Layer 2+ headers (documents the layer constraint)
- [ ] T066 [P] Write prepare() with non-default sample rate test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: call `prepare(48000.0, 256)`, verify `minNoteDurationSamples_` is recomputed correctly (50ms * 48000 = 2400 samples vs 2205 at 44100)
- [ ] T066b [P] Write re-prepare with sample rate change test in `dsp/tests/unit/primitives/pitch_tracker_test.cpp`: call `prepare(44100.0, 256)`, establish tracking state (feed pitched signal until `currentNote_ != -1`), then call `prepare(48000.0, 256)`, verify (a) all state is fully reset (`currentNote_ == -1`, `historyCount_ == 0`, `isPitchValid() == false`) AND (b) `minNoteDurationSamples_` is recomputed for 48000Hz (2400 samples), not left at the 44100Hz value (2205 samples). This covers the edge case documented in spec.md: "What happens when `prepare()` is called with a new sample rate while the tracker has existing state?"
- [ ] T067 Build DSP tests and verify zero warnings, run all tests: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "PitchTracker*"`
- [ ] T068 Fix any implementation gaps revealed by edge case tests in `dsp/include/krate/dsp/primitives/pitch_tracker.h`
- [ ] T069 Run full DSP test suite (not just PitchTracker) to confirm no regressions: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [ ] T070 Commit edge case test work: `git commit` with message "Add edge case and FR coverage tests for PitchTracker"

**Checkpoint**: All 16 FR requirements and 9 SC requirements are covered by at least one test.

---

## Phase 8: Polish and Cross-Cutting Concerns

**Purpose**: Code quality, documentation style, and doxygen comment completeness.

- [ ] T071 [P] Review all doxygen comments in `dsp/include/krate/dsp/primitives/pitch_tracker.h` against the contract in `specs/063-pitch-tracker/contracts/pitch_tracker_api.h`; fill in any missing `@param`, `@return`, `@post`, `@note` tags to match the contract level of detail
- [ ] T072 [P] Review `runPipeline()` implementation for the two optimization opportunities identified in plan.md SIMD section: (1) early-out when confidence < threshold (already part of stage 1 design), (2) skip median sort when `medianSize_ == 1` (return single value directly). Implement (2) if not already present.
- [ ] T073 Rebuild and run full test suite after polish changes: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [ ] T074 Commit polish work: `git commit` with message "Polish PitchTracker comments and optimizations"

---

## Phase 9: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion.

> **Constitution Principle XIV**: Every spec implementation MUST update architecture documentation as a final task.

- [ ] T075 Update `specs/_architecture_/layer-1-primitives.md`: add a PitchTracker section documenting purpose ("5-stage post-processing wrapper around PitchDetector for stable harmonizer pitch input"), public API summary (all 9 public methods with brief descriptions), file location (`dsp/include/krate/dsp/primitives/pitch_tracker.h`), dependencies (PitchDetector L1, OnePoleSmoother L1, pitch_utils.h L0, midi_utils.h L0), usage example from quickstart.md, and "when to use this" guidance (Phase 4 HarmonizerEngine integration)
- [ ] T076 Commit architecture documentation: `git commit` with message "Document PitchTracker in layer-1-primitives architecture doc"

**Checkpoint**: Architecture documentation reflects the new PitchTracker component.

---

## Phase 10: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

- [ ] T077 Generate compile_commands.json if not current: run `cmake --preset windows-ninja` from VS Developer PowerShell (required for clang-tidy)
- [ ] T078 Run clang-tidy on DSP target: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`
- [ ] T079 Fix all clang-tidy errors in `dsp/include/krate/dsp/primitives/pitch_tracker.h` and `dsp/tests/unit/primitives/pitch_tracker_test.cpp` (blocking issues must be resolved)
- [ ] T080 Review clang-tidy warnings and fix where appropriate; add `// NOLINT(rule): reason` suppressions only where the warning is a false positive in DSP context
- [ ] T081 Rebuild and run full test suite after clang-tidy fixes: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [ ] T082 Commit clang-tidy fixes: `git commit` with message "Fix clang-tidy findings in PitchTracker"

**Checkpoint**: Static analysis clean. Ready for completion verification.

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion.

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

- [ ] T083 Re-read each FR-001 through FR-016 from `specs/063-pitch-tracker/spec.md` and locate the exact line(s) in `dsp/include/krate/dsp/primitives/pitch_tracker.h` that satisfy each one; record file path + line number for the compliance table
- [ ] T084 Run `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "PitchTracker*" --success` and capture the full output; verify each SC-001 through SC-009 test name appears with PASSED status; copy actual measured values for SC-007 CPU budget
- [ ] T085 Search for disqualifying patterns in `dsp/include/krate/dsp/primitives/pitch_tracker.h`: `grep -n "TODO\|FIXME\|placeholder\|stub\|new\b\|delete\b\|malloc\|free"` -- review each match; occurrences in comments, string literals, or `noexcept` are acceptable. All matches inside processing method bodies (`pushBlock`, `runPipeline`, `computeMedian`, query methods) MUST be absent.
- [ ] T086 Verify no test thresholds were relaxed from spec: compare each SC-xxx numeric bound in `dsp/tests/unit/primitives/pitch_tracker_test.cpp` against the original spec.md requirement

### 11.2 Fill Compliance Table in spec.md

- [ ] T087 Update `specs/063-pitch-tracker/spec.md` "Implementation Verification" section: fill the compliance table with MET/NOT MET status, specific file paths and line numbers from T083, and actual test output values from T084. Mark overall status as COMPLETE or NOT COMPLETE.

### 11.3 Honest Self-Check

Answer these questions before claiming completion. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T088 All self-check questions answered "no" (or gaps documented honestly in spec.md with user notification)

**Checkpoint**: Honest assessment complete. Ready for final completion claim.

---

## Dependencies and Execution Order

### Phase Dependencies

```
Phase 1 (CMake Setup)         -- No dependencies, start immediately
    |
Phase 2 (Skeleton Header)     -- Depends on Phase 1 (must compile)
    |
    +-- Phase 3 (US1: Core Pipeline)     -- Depends on Phase 2 - BLOCKS US2, US3, US4
    |       |
    |   Phase 4 (US2: Unvoiced Hold)     -- Depends on Phase 3 (extends same file/class)
    |   Phase 5 (US4: Outlier Rejection) -- Can start in parallel with Phase 4 after Phase 3
    |   Phase 6 (US3: Configurability)   -- Can start in parallel after Phase 3
    |
Phase 7 (Edge Cases)          -- Depends on Phases 3-6
Phase 8 (Polish)              -- Depends on Phase 7
Phase 9 (Architecture Docs)   -- Depends on Phase 8
Phase 10 (Clang-Tidy)         -- Depends on Phase 9
Phase 11 (Verification)       -- Depends on Phase 10
```

### User Story Dependencies

- **US1 (P1) - Core Pipeline**: Depends only on Phase 2 (skeleton). No other story dependency. This is the MVP.
- **US2 (P2) - Unvoiced Hold**: Depends on US1 (confidence gate logic in `runPipeline()` must exist). Tests extend the same file.
- **US4 (P2) - Outlier Rejection**: Depends on US1 (median filter in `runPipeline()` must exist). Can run in parallel with US2.
- **US3 (P3) - Configurability**: Depends on US1 (setters update fields used by `runPipeline()`). Can run in parallel with US2 and US4.

### Within Each User Story

1. Tests FIRST (write and verify they FAIL)
2. Implementation (make tests pass)
3. Build with zero warnings
4. Run tests (verify PASS)
5. Cross-platform check (IEEE 754 / fno-fast-math)
6. Commit

---

## Parallel Opportunities

### Within Phase 1 (Setup)

T001, T002, T003 can run in parallel (different files: `dsp/CMakeLists.txt`, `dsp/tests/CMakeLists.txt`).

### Within User Story 1 Tests (Phase 3.1)

T007 through T014 are all test functions in the same file. They can be written in parallel by different developers but must be in the same file so they cannot literally be committed simultaneously (merge required). Write them sequentially or in batches.

### After Phase 3 (US1 Complete)

US2 (Phase 4), US4 (Phase 5), and US3 (Phase 6) can proceed in parallel. Each adds new test cases to `pitch_tracker_test.cpp` and potentially modifies `pitch_tracker.h`. If parallelizing, coordinate on the shared files to avoid conflicts.

### Within Phase 7 (Edge Cases)

T061 through T066 are all independent test cases and can be written in parallel.

---

## Implementation Strategy

### MVP: Phase 1 + Phase 2 + Phase 3 Only (US1)

After completing Phases 1-3, you have a fully functional PitchTracker with the complete 5-stage pipeline. This satisfies the primary consumer (Phase 4 HarmonizerEngine) and delivers:
- SC-001: Stable note from stable input
- SC-002: Stable note with jitter
- SC-004: Clean note transitions
- SC-007: <0.1% CPU budget
- SC-008: Zero allocations
- SC-009: Multi-hop processing

Stop here to validate with Phase 4 HarmonizerEngine integration before adding US2-US4.

### Incremental Delivery

1. Phases 1-3 (US1) -> Core pipeline working, test with HarmonizerEngine
2. Phase 4 (US2) -> Add unvoiced segment handling
3. Phase 5 (US4) -> Add octave-jump outlier rejection verification
4. Phase 6 (US3) -> Add configurability setters
5. Phases 7-11 -> Polish, docs, verification

---

## Summary

| Phase | Tasks | User Story | Key Deliverable |
|-------|-------|------------|-----------------|
| 1 | T001-T004 | Setup | CMake build registration |
| 2 | T005-T006 | Foundation | PitchTracker class skeleton |
| 3 | T007-T025 | US1 (P1) | Core 5-stage pipeline (MVP) |
| 4 | T026-T035 | US2 (P2) | Unvoiced segment hold |
| 5 | T036-T045 | US4 (P2) | Median filter outlier rejection |
| 6 | T046-T060 | US3 (P3) | Configuration setters |
| 7 | T061-T070 | Cross-cutting | Edge case + FR coverage |
| 8 | T071-T074 | Polish | Comments, micro-optimizations |
| 9 | T075-T076 | Arch docs | layer-1-primitives.md updated |
| 10 | T077-T082 | Clang-tidy | Static analysis clean |
| 11 | T083-T088 | Verification | Honest compliance table |

**Total tasks**: 88
**Parallel opportunities**: T001/T002/T003 (Phase 1), T007-T014 (US1 tests), T026-T028 (US2 tests), T036-T038 (US4 tests), T046-T051 (US3 tests), T061-T066 (edge cases), T071/T072 (polish)
**MVP scope**: Phases 1-3 (T001-T025, 25 tasks)
