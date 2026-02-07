# Tasks: Trance Gate (039)

**Input**: Design documents from `/specs/039-trance-gate/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase 8.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality
3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create files, register test, set up minimal compilable skeleton

- [X] T001 Create header file with GateStep struct, TranceGateParams struct, and empty TranceGate class skeleton (constructor, prepare, reset, setParams, setTempo, setStep, setPattern, setEuclidean, process, processBlock mono, processBlock stereo, getGateValue, getCurrentStep -- all noexcept stubs returning defaults) in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T002 Create test file with `#include <krate/dsp/processors/trance_gate.h>` and a single placeholder TEST_CASE("TranceGate - compiles") that constructs a TranceGate in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T003 Register `unit/processors/trance_gate_test.cpp` in the `add_executable(dsp_tests ...)` target list (under Layer 2: Processors section) in `dsp/tests/CMakeLists.txt`
- [X] T004 Build dsp_tests target and verify compilation succeeds with zero warnings
- [X] T005 Run the placeholder test and verify it passes

**Checkpoint**: Skeleton compiles, test infrastructure works, ready for user story implementation

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core internal timing and smoother mechanics that ALL user stories depend on

**CRITICAL**: No user story work can begin until this phase is complete. These are the internal engine components.

- [X] T006 Write failing tests for internal step advancement logic: construct TranceGate, prepare at 44100 Hz, set a 16-step alternating pattern (1.0/0.0), set tempo 120 BPM, process enough samples for one step (5512 samples at 1/16 note, 120 BPM), verify getCurrentStep() advances from 0 to 1 in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T007 Implement standalone minimal timing engine: sample counter increment, step boundary detection (sampleCounter >= samplesPerStep), step advancement (currentStep = (currentStep + 1) % numSteps), samplesPerStep calculation from BPM/noteValue/sampleRate using getBeatsForNote(), all in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T008 Write failing tests for asymmetric smoother integration: construct TranceGate with alternating 0.0/1.0 pattern, process samples across a step boundary, verify that the gain value transitions smoothly (not instantaneously) in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T009 Implement asymmetric smoother: two OnePoleSmoother instances (attack/release), direction detection (target > current = attack, target < current = release), state synchronization of inactive smoother via snapTo(), in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T010 Verify all foundational tests pass, build with zero warnings

**Checkpoint**: Foundation ready -- timing engine advances steps correctly, smoother produces smooth transitions. User story implementation can now begin.

---

## Phase 3: User Story 1 - Pattern-Driven Rhythmic Gating (Priority: P1) -- MVP

**Goal**: Apply a repeating float-level step pattern as multiplicative gain to audio. Steps hold levels 0.0-1.0 for silence, full volume, ghost notes, and accents. The pattern loops in sync with tempo at a configurable note value.

**Independent Test**: Create a TranceGate at 44100 Hz / 120 BPM / 1/16 note, set alternating 1.0/0.0 pattern, process constant 1.0 signal, verify output alternates between near-1.0 and near-0.0 at ~5512-sample intervals with smooth transitions.

**Requirements covered**: FR-001, FR-002, FR-005 (partial: tempo-sync step duration), FR-012 (partial: mono process and mono processBlock), FR-014

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T011 [US1] Write test "TranceGate - alternating pattern produces rhythmic gating at correct step duration" (SC-001): prepare at 44100 Hz, tempo 120 BPM, 1/16 note, 16-step alternating 1.0/0.0 pattern, process constant 1.0 input, verify output is near-1.0 during step 0 (after ramp) and near-0.0 during step 1 (after ramp), with step boundaries at ~5512 samples in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T012 [US1] Write test "TranceGate - ghost notes and accents produce float-level gain" (FR-001): set pattern with levels 0.3 (ghost) and 1.0 (accent), process constant signal, verify ghost step output is approximately 30% amplitude and accent step is approximately 100% in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T013 [US1] Write test "TranceGate - all-open pattern is transparent" (FR-002): set 8 steps all at 1.0, process signal, verify output equals input within tolerance in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T014 [US1] Write test "TranceGate - setStep modifies only addressed step" (FR-001): set initial pattern, call setStep(3, 0.5), verify step 3 changed and all others unchanged in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T015 [US1] Write test "TranceGate - default state without prepare is passthrough" (FR-014): construct TranceGate without calling prepare(), process signal, verify output equals input (unity gain) in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T016 [US1] Write test "TranceGate - processBlock mono produces same result as per-sample process" (FR-012): process same signal both ways, compare outputs in `dsp/tests/unit/processors/trance_gate_test.cpp`

### 3.2 Implementation for User Story 1

- [X] T017 [US1] Implement pattern storage: std::array<float, 32> with all levels defaulting to 1.0, setStep() with index bounds check and level clamping to [0.0, 1.0], setPattern() to copy array and update numSteps in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T018 [US1] Implement process(float input): read current step level, set smoother target, process active smoother, sync inactive smoother, apply gain (input * smoothedGain), update currentGainValue_, return result in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T019 [US1] Implement processBlock(float*, size_t): loop calling process() per sample for mono in-place processing in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T020 [US1] Implement prepare(double sampleRate): store sample rate, recalculate samplesPerStep and smoother coefficients, handle default state (prepared_ flag) in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T021 [US1] Implement constructor: initialize all steps to 1.0, smoothers snapped to 1.0 (unity), default sampleRate 44100.0, default tempo 120 BPM in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T022 [US1] Verify all US1 tests pass, build with zero warnings

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T023 [US1] Verify IEEE 754 compliance: check if `dsp/tests/unit/processors/trance_gate_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` -- if so, add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 3.4 Commit (MANDATORY)

- [X] T024 [US1] Commit completed User Story 1 work

**Checkpoint**: User Story 1 fully functional -- pattern-driven gating with float-level steps, correct step timing, smooth transitions, mono processing. This is the MVP.

---

## Phase 4: User Story 2 - Click-Free Edge Shaping (Priority: P1)

**Goal**: Guarantee that gate transitions are always click-free by enforcing one-pole exponential smoothing bounds. Attack/release parameters control ramp speed. Hard gating (instantaneous transitions) is impossible -- minimum 1ms ramp.

**Independent Test**: Set alternating 0.0/1.0 at fast rate, process 440 Hz sine, measure max sample-to-sample gain change, verify it never exceeds one-pole coefficient bounds.

**Requirements covered**: FR-003, SC-002

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T025 [US2] Write test "TranceGate - max gain change within one-pole bounds" (SC-002): set attackMs=2.0, releaseMs=10.0, alternating 0.0/1.0 pattern, process several cycles, track max sample-to-sample gain change, verify it is less than 0.056 (1 - exp(-5000/(2.0*44100))) in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T026 [US2] Write test "TranceGate - minimum ramp time prevents instantaneous transitions" (FR-003): set attackMs=1.0, releaseMs=1.0 (minimum), verify gain ramp takes at least 44 samples (~1ms at 44100 Hz) to transition from 0 to near 1.0 in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T027 [US2] Write test "TranceGate - 99% settling time matches attack parameter" (FR-003): set attackMs=20.0, trigger 0.0-to-1.0 transition, verify envelope reaches 99% of target after approximately 882 samples (20ms at 44100 Hz) in `dsp/tests/unit/processors/trance_gate_test.cpp`

### 4.2 Implementation for User Story 2

- [X] T028 [US2] Implement configurable attack/release: setParams updates attack smoother via configure(attackMs, sampleRate) and release smoother via configure(releaseMs, sampleRate), clamp attackMs to [1.0, 20.0] and releaseMs to [1.0, 50.0] in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T029 [US2] Verify all US2 tests pass, build with zero warnings

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T030 [US2] Verify IEEE 754 compliance for any new test code added in US2

### 4.4 Commit (MANDATORY)

- [X] T031 [US2] Commit completed User Story 2 work

**Checkpoint**: User Stories 1 AND 2 complete -- pattern gating with guaranteed click-free transitions at configurable attack/release rates.

---

## Phase 5: User Story 3 - Euclidean Pattern Generation (Priority: P2)

**Goal**: Generate complex polyrhythmic patterns via Euclidean algorithm (Bjorklund/Toussaint). Hits distributed as evenly as possible across steps. Active steps get 1.0, inactive get 0.0. Optional rotation shifts the downbeat.

**Independent Test**: Call setEuclidean(3, 8, 0), verify pattern matches tresillo [1,0,0,1,0,0,1,0]. Process audio and confirm only hit positions produce output.

**Requirements covered**: FR-007, SC-004

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T032 [US3] Write test "TranceGate - Euclidean E(3,8) matches tresillo" (SC-004): call setEuclidean(3, 8, 0), verify pattern is [1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0] by querying step levels or processing audio in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T033 [US3] Write test "TranceGate - Euclidean E(5,8) matches cinquillo" (SC-004): call setEuclidean(5, 8, 0), verify pattern [1.0, 0.0, 1.0, 1.0, 0.0, 1.0, 1.0, 0.0] in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T034 [US3] Write test "TranceGate - Euclidean E(5,12) reference pattern" (SC-004): call setEuclidean(5, 12, 0), verify pattern matches [1,0,0,1,0,1,0,0,1,0,1,0] in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T035 [US3] Write test "TranceGate - Euclidean rotation shifts pattern" (FR-007): call setEuclidean(4, 16, 2), verify pattern is rotated by 2 positions vs setEuclidean(4, 16, 0) in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T036 [US3] Write test "TranceGate - Euclidean edge cases" (FR-007): setEuclidean(0, 16, 0) produces all 0.0, setEuclidean(16, 16, 0) produces all 1.0 in `dsp/tests/unit/processors/trance_gate_test.cpp`

### 5.2 Implementation for User Story 3

- [X] T037 [US3] Implement setEuclidean(int hits, int steps, int rotation): call EuclideanPattern::generate(hits, steps, rotation), iterate steps, use EuclideanPattern::isHit() to set each step level to 1.0 or 0.0, update numSteps_ and recalculate samplesPerStep_ in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T038 [US3] Verify all US3 tests pass, build with zero warnings

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T039 [US3] Verify IEEE 754 compliance for any new test code added in US3

### 5.4 Commit (MANDATORY)

- [X] T040 [US3] Commit completed User Story 3 work

**Checkpoint**: Euclidean pattern generation works with known reference outputs, rotation, and edge cases.

---

## Phase 6: User Story 4+5 - Depth Control & Tempo Sync (Priority: P2)

**Goal**: (US4) Depth parameter blends between unprocessed and gated signal: g_final = lerp(1.0, g_pattern, depth). Depth 0.0 = bypass, 1.0 = full effect. (US5) Step timing locks to host BPM via musical note values. Free-run mode uses Hz-based step rate. Tempo changes adjust step duration.

**Independent Test (US4)**: Process same signal at depth 0.0 (output=input), 0.5 (half effect), 1.0 (full effect). Verify lerp relationship. **Independent Test (US5)**: Set 120 BPM, 1/16 note, verify ~5512 samples/step. Change to 140 BPM, verify step duration changes proportionally.

**Requirements covered**: FR-004, FR-005, FR-006, SC-001, SC-005

### 6.1 Tests for User Story 4 - Depth Control (Write FIRST - Must FAIL)

- [X] T041 [US4] Write test "TranceGate - depth 0.0 bypasses gate entirely" (SC-005): set depth=0.0, any pattern with 0.0 steps, verify output equals input in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T042 [US4] Write test "TranceGate - depth 1.0 applies full pattern effect" (FR-004): set depth=1.0, pattern alternating 0.0/1.0, verify silent steps produce near-zero output in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T043 [US4] Write test "TranceGate - depth 0.5 halves the effect" (SC-005): set depth=0.5, pattern with step level 0.0, verify output amplitude is approximately 50% of input (within 1% tolerance per SC-005) in `dsp/tests/unit/processors/trance_gate_test.cpp`

### 6.2 Tests for User Story 5 - Tempo Sync (Write FIRST - Must FAIL)

- [X] T044 [US5] Write test "TranceGate - step duration matches tempo and note value" (SC-001): set 120 BPM, 1/16 note at 44100 Hz, verify step boundary at sample 5512 (within 1 sample per SC-001) in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T045 [US5] Write test "TranceGate - tempo change adjusts step duration" (FR-005): start at 120 BPM, process one step, change to 140 BPM via setTempo(), verify next step has shorter duration proportional to BPM ratio in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T046 [US5] Write test "TranceGate - free-run mode uses Hz rate" (FR-006): set tempoSync=false, rateHz=8.0, verify step lasts ~5512 samples (44100/8) in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T047 [US5] Write test "TranceGate - dotted and triplet note modifiers" (FR-005): set 1/16 dotted and 1/16 triplet, verify step durations match getBeatsForNote() calculations in `dsp/tests/unit/processors/trance_gate_test.cpp`

### 6.3 Implementation for User Story 4 - Depth Control

- [X] T048 [US4] Implement depth application in process(): compute finalGain = 1.0f + (smoothedGain - 1.0f) * depth (equivalent to lerp(1.0, smoothedGain, depth)), clamp depth to [0.0, 1.0] in setParams() in `dsp/include/krate/dsp/processors/trance_gate.h`

### 6.4 Implementation for User Story 5 - Tempo Sync

- [X] T049 [US5] Implement setTempo(double bpm): clamp to [20.0, 300.0], store tempoBPM_, recalculate samplesPerStep_ in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T050 [US5] Implement free-run mode: when tempoSync=false, calculate samplesPerStep_ = sampleRate_ / rateHz, clamp rateHz to [0.1, 100.0] in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T051 [US5] Implement updateStepDuration() private method: if tempoSync, use (60.0 / tempoBPM_) * getBeatsForNote(noteValue, noteModifier) * sampleRate_; else use sampleRate_ / rateHz; called from setParams(), setTempo(), prepare() in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T052 [US4] [US5] Verify all US4 and US5 tests pass, build with zero warnings

### 6.5 Cross-Platform Verification (MANDATORY)

- [X] T053 [US4] [US5] Verify IEEE 754 compliance for any new test code added in US4/US5

### 6.6 Commit (MANDATORY)

- [X] T054 [US4] [US5] Commit completed User Story 4 and 5 work

**Checkpoint**: Depth control and tempo synchronization work. Gate can blend subtly or dramatically, and locks to host tempo or runs free.

---

## Phase 7: User Story 6+7 - Modulation Output & Voice Modes (Priority: P3)

**Goal**: (US6) Expose current smoothed, depth-adjusted gate envelope as a readable modulation output via getGateValue(). (US7) Per-voice mode resets pattern on note-on; global mode continues uninterrupted.

**Independent Test (US6)**: Process audio, call getGateValue() each sample, verify it matches the gain applied to audio. **Independent Test (US7)**: Two instances -- per-voice resets on reset(), global does not.

**Requirements covered**: FR-008, FR-009, FR-010, FR-011, FR-012 (stereo processBlock), FR-013, FR-015, SC-006, SC-007

### 7.1 Tests for User Story 6 - Modulation Output (Write FIRST - Must FAIL)

- [X] T055 [US6] Write test "TranceGate - getGateValue matches applied gain" (SC-006): process audio sample-by-sample, compute expected gain from output/input ratio, compare against getGateValue(), verify within 0.001 tolerance in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T056 [US6] Write test "TranceGate - getGateValue reflects depth adjustment" (FR-008): set depth=0.5, pattern with step 0.0, verify getGateValue() returns approximately 0.5 (not 0.0 raw pattern level) in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T057 [US6] Write test "TranceGate - getGateValue is 1.0 for all-open pattern" (FR-008): all steps at 1.0, verify getGateValue() returns 1.0 in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T058 [US6] Write test "TranceGate - getCurrentStep returns correct index" (FR-009): process through multiple steps, verify getCurrentStep() returns expected index at each step in `dsp/tests/unit/processors/trance_gate_test.cpp`

### 7.2 Tests for User Story 7 - Voice Modes (Write FIRST - Must FAIL)

- [X] T059 [US7] Write test "TranceGate - per-voice mode resets on reset()" (FR-010): set perVoice=true, advance to step 5, call reset(), verify getCurrentStep() returns 0 in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T060 [US7] Write test "TranceGate - global mode does not reset on reset()" (FR-010): set perVoice=false, advance to step 5, call reset(), verify getCurrentStep() remains at 5 in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T061 [US7] Write test "TranceGate - two per-voice instances produce different phasing when reset at different times" (FR-010): two instances with same pattern, reset at different times, verify different outputs in `dsp/tests/unit/processors/trance_gate_test.cpp`

### 7.3 Tests for Stereo Processing and Phase Offset

- [X] T062 [US7] Write test "TranceGate - stereo processBlock applies identical gain to both channels" (SC-007, FR-012): process stereo block with identical L/R input, verify L/R output are identical at every sample in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T063 [US7] Write test "TranceGate - phaseOffset rotates pattern start position" (FR-011): set phaseOffset=0.5 on a 16-step pattern, verify pattern playback starts from step 8 in `dsp/tests/unit/processors/trance_gate_test.cpp`

### 7.4 Implementation for User Story 6 - Modulation Output

- [X] T064 [US6] Implement getGateValue(): return currentGainValue_ (already stored in process()) in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T065 [US6] Implement getCurrentStep(): return currentStep_ in `dsp/include/krate/dsp/processors/trance_gate.h`

### 7.5 Implementation for User Story 7 - Voice Modes

- [X] T066 [US7] Implement reset(): if perVoice, reset sampleCounter_=0, currentStep_=0, snap both smoothers to pattern_[0] level; if not perVoice, no-op in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T067 [US7] Implement stereo processBlock(float*, float*, size_t): loop calling process() per sample, apply same gain to both channels in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T068 [US7] Implement phaseOffset: in process(), compute rotationOffset_ = static_cast<int>(phaseOffset * numSteps_), compute effectiveStep = (currentStep_ + rotationOffset_) % numSteps_ for step level lookup in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T069 [US6] [US7] Verify all US6 and US7 tests pass, build with zero warnings

### 7.6 Cross-Platform Verification (MANDATORY)

- [X] T070 [US6] [US7] Verify IEEE 754 compliance for any new test code added in US6/US7

### 7.7 Commit (MANDATORY)

- [X] T071 [US6] [US7] Commit completed User Story 6 and 7 work

**Checkpoint**: All 7 user stories complete. Modulation output matches applied gain. Per-voice/global modes work correctly. Stereo processing is coherent.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Edge cases, performance, real-time safety verification, and optimizations

### 8.1 Edge Case Tests

- [X] T072 [P] Write test "TranceGate - minimum two steps loops correctly" (edge case): set numSteps=2 with levels 0.5 and 1.0, verify gain alternates between the two levels in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T073 [P] Write test "TranceGate - all-zero pattern produces depth-modulated silence" (edge case): all steps 0.0, depth=1.0, verify output is near-zero; depth=0.5, verify output is ~50% in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T074 [P] Write test "TranceGate - extreme tempos clamped to [20, 300] BPM" (edge case): call setTempo(5.0) and setTempo(500.0), verify clamped behavior in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T075 [P] Write test "TranceGate - pattern update mid-processing is click-free" (edge case): change pattern via setStep() during processing, verify smoother handles transition without discontinuity in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T076 [P] Write test "TranceGate - ramp time exceeding step duration produces triangular envelope" (edge case): set long releaseMs with fast step rate, verify no errors and envelope has expected shape in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T077 [P] Write test "TranceGate - prepare recalculates coefficients with new sample rate" (edge case): prepare at 44100, then prepare at 96000, verify step duration and smoother coefficients change in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T077b [P] Write test "TranceGate - gate does not affect voice lifetime" (FR-015): verify gate is purely a gain modifier -- process audio through gate with 0.0 step levels, confirm gate produces output (attenuated by depth) and does not signal note-off, voice stealing, or envelope interactions. The gate output at gain 0.0 with depth 1.0 should be near-zero but the gate itself has no mechanism to end a voice in `dsp/tests/unit/processors/trance_gate_test.cpp`

### 8.2 Performance & Real-Time Safety

- [X] T078 Write performance test "TranceGate - processing overhead < 0.1% CPU" (SC-003): benchmark process() for 1 second of 44100 Hz mono audio, verify CPU percentage is below 0.1% in `dsp/tests/unit/processors/trance_gate_test.cpp`
- [X] T079 Verify all methods are marked noexcept (FR-013): audit trance_gate.h for any method missing noexcept in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T080 Verify no allocation in process path (FR-013): audit that process(), processBlock(), reset() use no heap allocation (no new, no vector resize, no string operations) in `dsp/include/krate/dsp/processors/trance_gate.h`
- [X] T081 Implement early-out optimizations: skip processing when depth == 0.0 (bypass), skip smoother when already at target in `dsp/include/krate/dsp/processors/trance_gate.h`

### 8.3 Verify & Commit

- [X] T082 Verify ALL tests pass (all user stories + edge cases + performance), build with zero warnings
- [X] T083 Commit polish and edge case work

**Checkpoint**: All edge cases handled, performance verified, real-time safety confirmed.

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIV**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [X] T084 Update `specs/_architecture_/layer-2-processors.md` with TranceGate entry: purpose, public API summary (prepare, reset, setParams, setTempo, setStep, setPattern, setEuclidean, process, processBlock, getGateValue, getCurrentStep), file location, "when to use this" section

### 9.2 Final Commit

- [X] T085 Commit architecture documentation updates
- [X] T086 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects TranceGate functionality.

---

## Phase 10: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 10.1 Run Clang-Tidy Analysis

- [X] T087 Run clang-tidy on new source files: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`

### 10.2 Address Findings

- [X] T088 Fix all errors reported by clang-tidy (blocking issues)
- [X] T089 Review warnings and fix where appropriate (use judgment for DSP code)
- [X] T090 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean -- ready for completion verification.

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed.

### 11.1 Requirements Verification

- [X] T091 Review ALL FR-xxx requirements (FR-001 through FR-015) from spec.md against implementation
- [X] T092 Review ALL SC-xxx success criteria (SC-001 through SC-007) and verify measurable targets are achieved
- [X] T093 Search for cheating patterns in implementation:
  - No `// placeholder` or `// TODO` comments in new code
  - No test thresholds relaxed from spec requirements
  - No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [X] T094 Update spec.md "Implementation Verification" section with compliance status for each requirement
- [X] T095 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

- [X] T096 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete -- ready for final phase.

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Commit

- [X] T097 Commit all spec work to feature branch
- [X] T098 Verify all tests pass

### 12.2 Completion Claim

- [X] T099 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies -- can start immediately
- **Foundational (Phase 2)**: Depends on Phase 1 (skeleton must compile)
- **US1 (Phase 3)**: Depends on Phase 2 (timing + smoother must work)
- **US2 (Phase 4)**: Depends on Phase 3 (pattern gating must work to test edge shaping)
- **US3 (Phase 5)**: Depends on Phase 3 (Euclidean sets pattern, requires US1 process() to test via audio processing)
- **US4+US5 (Phase 6)**: Depends on Phase 3 (depth modifies gain computation, tempo sync extends timing)
- **US6+US7 (Phase 7)**: Depends on Phase 3 (modulation reads gain, voice modes use reset/timing)
- **Polish (Phase 8)**: Depends on all user stories being complete
- **Documentation (Phase 9)**: Depends on Phase 8
- **Static Analysis (Phase 10)**: Depends on Phase 8
- **Verification (Phase 11)**: Depends on Phases 9 and 10
- **Completion (Phase 12)**: Depends on Phase 11

### User Story Dependencies

```
Phase 1: Setup
    |
Phase 2: Foundational (timing + smoother)
    |
    +---> Phase 3: US1 - Pattern Gating (P1) [MVP]
    |         |
    |         +---> Phase 4: US2 - Click-Free Edges (P1)
    |         |
    |         +---> Phase 5: US3 - Euclidean Patterns (P2)
    |         |
    |         +---> Phase 6: US4+US5 - Depth + Tempo Sync (P2)
    |         |
    |         +---> Phase 7: US6+US7 - Mod Output + Voice Modes (P3)
    |
    +---> Phase 8: Polish (after all stories)
              |
              +---> Phase 9: Documentation
              +---> Phase 10: Static Analysis
                        |
                        +---> Phase 11: Verification
                                  |
                                  +---> Phase 12: Completion
```

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Implementation to make tests pass
3. Verify tests pass with zero warnings
4. Cross-platform IEEE 754 check
5. Commit

### Parallel Opportunities

- **Phase 5 (US3) and Phase 6 (US4+US5) and Phase 7 (US6+US7)** can proceed in parallel after Phase 3 completes, since they modify different aspects of the same file (but since it is a single header file, sequential execution is safer to avoid merge conflicts)
- Within Phase 8: All edge case tests (T072-T077) are marked [P] and can be written in parallel
- Phase 9 and Phase 10 can proceed in parallel

---

## Parallel Example: User Story 1

```bash
# All US1 tests can be written together (same file, different TEST_CASEs):
T011: "alternating pattern produces rhythmic gating"
T012: "ghost notes and accents produce float-level gain"
T013: "all-open pattern is transparent"
T014: "setStep modifies only addressed step"
T015: "default state without prepare is passthrough"
T016: "processBlock mono matches per-sample process"

# Then implementation tasks (sequential, same file):
T017 -> T018 -> T019 -> T020 -> T021

# Verify:
T022: Run all tests
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001-T005)
2. Complete Phase 2: Foundational (T006-T010)
3. Complete Phase 3: User Story 1 (T011-T024)
4. **STOP and VALIDATE**: Test US1 independently -- pattern-driven gating works
5. This delivers a usable trance gate with correct timing and smooth transitions

### Incremental Delivery

1. Setup + Foundational --> skeleton compiles
2. US1 (Phase 3) --> pattern-driven gating works (MVP!)
3. US2 (Phase 4) --> guaranteed click-free transitions
4. US3 (Phase 5) --> Euclidean pattern generation
5. US4+US5 (Phase 6) --> depth blending + host tempo sync + free-run mode
6. US6+US7 (Phase 7) --> modulation output + per-voice/global modes
7. Polish (Phase 8) --> edge cases, performance
8. Each phase adds value without breaking previous work

### File Summary

| File | Action | Phases |
|------|--------|--------|
| `dsp/include/krate/dsp/processors/trance_gate.h` | CREATE | All (1-8) |
| `dsp/tests/unit/processors/trance_gate_test.cpp` | CREATE | All (1-8) |
| `dsp/tests/CMakeLists.txt` | MODIFY | 1 (add test registration) |
| `specs/_architecture_/layer-2-processors.md` | MODIFY | 9 (add component entry) |
| `specs/039-trance-gate/spec.md` | MODIFY | 11 (compliance table) |

---

## Notes

- All implementation is header-only in a single file (`trance_gate.h`)
- All tests are in a single test file (`trance_gate_test.cpp`)
- This is a pure DSP component -- no plugin code, no UI, no VSTGUI
- Layer 2 processor depending only on Layer 0 (EuclideanPattern, NoteValue) and Layer 1 (OnePoleSmoother)
- All processing methods must be noexcept, allocation-free, and real-time safe
- FR-015: Gate does NOT affect voice lifetime -- it is purely a gain modifier
- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIV)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **NEVER claim completion if ANY requirement is not met** -- document gaps honestly instead
