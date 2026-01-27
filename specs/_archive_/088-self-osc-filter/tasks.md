# Tasks: Self-Oscillating Filter

**Input**: Design documents from `/specs/088-self-osc-filter/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

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
4. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/path/to/your_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create foundational MIDI utility functions (Layer 0) needed by all user stories

- [X] T001 Create `dsp/include/krate/dsp/core/midi_utils.h` header file with namespace and guards
- [X] T002 Create `dsp/tests/core/midi_utils_tests.cpp` test file with Catch2 includes

**Checkpoint**: Layer 0 file structure ready

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core MIDI utilities that MUST be complete before ANY user story can be implemented

CRITICAL: No user story work can begin until this phase is complete

### 2.1 MIDI Utilities Tests (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T003 [P] Write failing tests for `midiNoteToFrequency()` in `dsp/tests/core/midi_utils_tests.cpp`
  - Test MIDI note 69 (A4) = 440 Hz
  - Test MIDI note 60 (C4) = 261.63 Hz +/- 0.01 Hz
  - Test MIDI note 72 (C5) = 523.25 Hz +/- 0.01 Hz
  - Test boundary: note 0 and note 127
  - Test custom A4 frequency parameter

- [X] T004 [P] Write failing tests for `velocityToGain()` in `dsp/tests/core/midi_utils_tests.cpp`
  - Test velocity 127 = 1.0 gain (0 dB)
  - Test velocity 64 = approximately 0.504 gain (-5.95 dB, verify within 0.1 dB of -6 dB)
  - Test velocity 0 = 0.0 gain
  - Test velocity 1 = minimum non-zero gain

### 2.2 MIDI Utilities Implementation

- [X] T005 Implement `midiNoteToFrequency()` in `dsp/include/krate/dsp/core/midi_utils.h`
  - Use 12-TET formula: `440 * pow(2, (note - 69) / 12)`
  - Use `detail::constexprExp` for constexpr compatibility
  - Mark as `[[nodiscard]] constexpr` and `noexcept`
  - Default A4 = 440 Hz parameter

- [X] T006 Implement `velocityToGain()` in `dsp/include/krate/dsp/core/midi_utils.h`
  - Linear mapping: `velocity / 127.0f`
  - Clamp velocity to [0, 127] range
  - Mark as `[[nodiscard]] constexpr` and `noexcept`

### 2.3 Verify MIDI Utilities

- [X] T007 Build dsp_tests target and verify all MIDI utility tests pass
- [X] T008 Verify cross-platform: check if MIDI tests use IEEE 754 functions (likely not needed for these tests)

### 2.4 Commit Foundational Work

- [ ] T009 Commit completed MIDI utilities (Layer 0)

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Pure Sine Wave Oscillator (Priority: P1) MVP

**Goal**: Enable the filter to produce stable self-oscillation as a sine wave generator without external input.

**Independent Test**: Set resonance to 1.0, frequency to 440 Hz, process with zero input, verify stable sine-like waveform at 440 Hz (+/- 1 cent).

**Requirements Coverage**: FR-001, FR-002, FR-003, FR-004, FR-018, FR-019, FR-020, FR-021, FR-022, FR-024, SC-001, SC-002, SC-005

### 3.1 Create SelfOscillatingFilter Skeleton

- [X] T010 Create `dsp/include/krate/dsp/processors/self_oscillating_filter.h` header file
  - Class declaration with namespace `Krate::DSP`
  - Include guards
  - Forward declarations and includes for LadderFilter, DCBlocker2, LinearRamp, OnePoleSmoother
  - Public constants (kMinFrequency, kMaxFrequency, kSelfOscResonance, etc.)
  - EnvelopeState enum (Idle, Attack, Sustain, Release)

- [X] T011 Create `dsp/tests/processors/self_oscillating_filter_tests.cpp` test file with Catch2 includes

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T012 [P] [US1] Write failing tests for basic lifecycle in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test prepare() sets internal state
  - Test reset() clears state but preserves config
  - Test process() returns 0 before prepare()
  - Test prepare() with valid sample rates (44100, 48000, 96000)

- [X] T013 [P] [US1] Write failing tests for frequency control in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test setFrequency() clamps to valid range [20, 20000] Hz
  - Test frequency above Nyquist/2 is clamped to sampleRate * 0.45
  - Test getFrequency() returns set value

- [X] T014 [P] [US1] Write failing tests for resonance control in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test setResonance() clamps to [0.0, 1.0]
  - Test resonance = 1.0 maps to internal filter resonance >= 3.9
  - Test getResonance() returns normalized value

- [X] T015 [US1] Write failing test for stable self-oscillation (FR-001, SC-001, SC-002) in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test: resonance 1.0, frequency 440 Hz, zero input, process 1 second
  - Verify FFT shows dominant peak at 440 Hz +/- 10 cents
  - Verify output remains bounded (no runaway gain) for entire duration
  - Verify peak output does not exceed +6 dBFS

- [X] T016 [US1] Write failing test for DC offset removal (FR-019, SC-005) in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test: oscillate for 1 second, measure DC component
  - Verify DC offset < 0.001 linear

- [X] T017 [US1] Write failing test for frequency accuracy (FR-004, SC-001) in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test frequencies: 100 Hz, 440 Hz, 1000 Hz, 5000 Hz
  - Verify FFT fundamental within +/- 10 cents of target

- [X] T017b [US1] Write failing test for per-sample cutoff update (FR-004) in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test: Start glide from 440 Hz to 880 Hz over 100ms
  - Verify frequency changes continuously (not stepped) by measuring instantaneous frequency at multiple points
  - Verify no frequency "staircase" artifacts that would indicate block-rate updates
  - This ensures the requirement "cutoff frequency MUST be updated every sample" is met

### 3.3 Implementation for User Story 1

- [X] T018 [US1] Implement private member fields in `self_oscillating_filter.h`
  - Components: filter_, dcBlocker_, frequencyRamp_, levelSmoother_, mixSmoother_, attackEnvelope_, releaseEnvelope_
  - State: envelopeState_, currentEnvelopeLevel_, targetVelocityGain_
  - Parameters: frequency_, resonance_, glideMs_, attackMs_, releaseMs_, externalMix_, waveShapeAmount_, levelDb_
  - Runtime: sampleRate_, prepared_

- [X] T019 [US1] Implement `prepare()` method in `self_oscillating_filter.h`
  - Store sample rate and max block size
  - Call prepare() on all components (filter, dcBlocker, smoothers, ramps)
  - Configure smoothers with default times
  - Set prepared_ = true

- [X] T020 [US1] Implement `reset()` method in `self_oscillating_filter.h`
  - Call reset() on all components
  - Reset envelope state to Idle
  - Reset currentEnvelopeLevel_ to 0
  - Preserve all parameter values

- [X] T021 [US1] Implement `setFrequency()` and `getFrequency()` in `self_oscillating_filter.h`
  - Clamp to [kMinFrequency, min(kMaxFrequency, sampleRate * 0.45)]
  - Update frequencyRamp_ target

- [X] T022 [US1] Implement `setResonance()` and `getResonance()` in `self_oscillating_filter.h`
  - Clamp to [0.0, 1.0]
  - Implement mapResonanceToFilter() helper: map 0→0, 0.95→3.9, 1.0→3.95
  - Call filter_.setResonance() with mapped value

- [X] T023 [US1] Implement basic `process(float externalInput)` method in `self_oscillating_filter.h`
  - Early return 0.0f if not prepared
  - Get current frequency from frequencyRamp_.process()
  - Update filter_.setCutoff() every sample (FR-004)
  - Process external input through filter
  - Pass through dcBlocker_
  - Return output

- [X] T024 [US1] Implement `processBlock()` method in `self_oscillating_filter.h`
  - Loop calling process() for each sample

### 3.4 Verify User Story 1

- [X] T025 [US1] Build dsp_tests and run SelfOscillatingFilter tests
- [X] T026 [US1] Verify all US1 tests pass (lifecycle, frequency, resonance, oscillation stability, DC removal, accuracy)
- [ ] T027 [US1] Manual verification: Create test program that generates 1 second of 440 Hz oscillation, save to WAV, verify audibly

### 3.5 Cross-Platform Verification (MANDATORY)

- [X] T028 [US1] Verify IEEE 754 compliance: If self_oscillating_filter_tests.cpp uses `std::isnan`/`std::isfinite`/`std::isinf`, add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 3.6 Commit (MANDATORY)

- [ ] T029 [US1] Commit completed User Story 1 work (basic self-oscillation capability)

**Checkpoint**: User Story 1 should be fully functional - the filter can produce stable sine-like oscillation at a set frequency with no external input.

---

## Phase 4: User Story 2 - Melodic MIDI Control (Priority: P1) MVP

**Goal**: Enable playing the filter melodically using MIDI notes with velocity control and proper envelope behavior.

**Independent Test**: Send noteOn(60, 127) and verify output at 261.63 Hz. Send noteOff() and verify exponential decay.

**Requirements Coverage**: FR-005, FR-006, FR-006b, FR-007, FR-008, FR-008b, FR-009, FR-010, FR-011, FR-023, SC-003, SC-009, SC-010

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T030 [P] [US2] Write failing tests for noteOn/noteOff behavior in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test noteOn(60, 127) produces oscillation at 261.63 Hz +/- 1 Hz (SC-003)
  - Test velocity 127 = full level, velocity 64 = approx -6 dB (FR-007)
  - Test velocity 0 treated as noteOff (FR-008)
  - Test noteOff() initiates exponential decay, not instant cutoff (FR-006)

- [X] T031 [P] [US2] Write failing tests for attack time in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test setAttack() clamps to [0, 20] ms (FR-006b)
  - Test attack 0 ms = instant full amplitude
  - Test attack 10 ms = smooth ramp to target over approximately 10 ms
  - Verify no transients > 3 dB during attack (SC-009)

- [X] T032 [P] [US2] Write failing tests for release time in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test setRelease() clamps to [10, 2000] ms (FR-006)
  - Test release 500 ms decays to -60 dB over approximately 500 ms
  - Verify exponential decay curve
  - Verify no transients > 3 dB during release (SC-009)

- [X] T033 [P] [US2] Write failing tests for note retriggering in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test noteOn() during active note restarts attack from current level (FR-008b)
  - Verify no clicks/discontinuities during retrigger (SC-010)
  - Test rapid note sequences for smooth amplitude transitions

- [X] T034 [P] [US2] Write failing tests for glide/portamento in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test setGlide() clamps to [0, 5000] ms (FR-009)
  - Test glide 0 ms = instant frequency change (FR-011)
  - Test glide 100 ms: noteOn(440Hz) then noteOn(880Hz), verify linear frequency ramp over 100ms (FR-010)
  - Verify no clicks during glide (SC-004)
  - Verify smooth interpolation with no audible discontinuities

### 4.2 Implementation for User Story 2

- [X] T035 [US2] Implement `setAttack()` and `getAttack()` in `self_oscillating_filter.h`
  - Clamp to [kMinAttackMs, kMaxAttackMs] (0-20 ms)
  - Reconfigure attackEnvelope_ smoother with attack time

- [X] T036 [US2] Implement `setRelease()` and `getRelease()` in `self_oscillating_filter.h`
  - Clamp to [kMinReleaseMs, kMaxReleaseMs] (10-2000 ms)
  - Reconfigure releaseEnvelope_ smoother with release time

- [X] T037 [US2] Implement `setGlide()` and `getGlide()` in `self_oscillating_filter.h`
  - Clamp to [kMinGlideMs, kMaxGlideMs] (0-5000 ms)
  - Store glideMs_ for use in noteOn()

- [X] T038 [US2] Implement envelope state machine helper `processEnvelope()` in `self_oscillating_filter.h`
  - State: Idle → return 0.0
  - State: Attack → process attackEnvelope_, check if >= 99% target, transition to Sustain
  - State: Sustain → return targetVelocityGain_
  - State: Release → process releaseEnvelope_, check if < threshold (-60dB), transition to Idle
  - Return current envelope level

- [X] T039 [US2] Implement `noteOn(int midiNote, int velocity)` in `self_oscillating_filter.h`
  - Handle velocity 0 as noteOff() (FR-008)
  - Clamp midiNote to [0, 127] and velocity to [1, 127]
  - Convert MIDI note to frequency using midiNoteToFrequency()
  - Convert velocity to gain using velocityToGain()
  - Update targetVelocityGain_
  - Configure frequencyRamp_ with glide time, set target frequency
  - If glide is 0, snapTo() new frequency immediately (FR-011)
  - Configure attack envelope and set target
  - If retriggering (state not Idle), restart attack from currentEnvelopeLevel_ (FR-008b)
  - Transition to Attack state

- [X] T040 [US2] Implement `noteOff()` in `self_oscillating_filter.h`
  - If state is Idle, do nothing
  - Configure release envelope
  - Transition to Release state

- [X] T041 [US2] Update `process()` to include envelope processing in `self_oscillating_filter.h`
  - Call processEnvelope() to get current envelope level
  - Apply envelope as gain multiplier to output

### 4.3 Verify User Story 2

- [X] T042 [US2] Build dsp_tests and run all SelfOscillatingFilter tests
- [X] T043 [US2] Verify all US2 tests pass (noteOn/noteOff, attack, release, retriggering, glide)
- [ ] T044 [US2] Manual verification: Play melody with glide and attack/release, verify smooth transitions

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T045 [US2] Verify IEEE 754 compliance: Confirm test file settings from US1 still apply

### 4.5 Commit (MANDATORY)

- [ ] T046 [US2] Commit completed User Story 2 work (melodic MIDI control with envelope and glide)

**Checkpoint**: User Stories 1 AND 2 should both work - the filter can self-oscillate and be played melodically with MIDI.

---

## Phase 5: User Story 3 - Filter Ping Effect (Priority: P2)

**Goal**: Enable using external audio to "ping" the filter at high resonance, creating resonant bell-like tones triggered by transients.

**Independent Test**: Set resonance to 0.95, frequency to 1000 Hz, send impulse, verify resonant ringing at 1000 Hz with exponential decay.

**Requirements Coverage**: FR-012, FR-013, FR-020, SC-007

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T047 [P] [US3] Write failing tests for external input mixing in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test setExternalMix() clamps to [0.0, 1.0] (FR-012)
  - Test mix 0.0 = pure oscillation, no external signal contribution
  - Test mix 1.0 = external signal only, minimal oscillation contribution
  - Test mix 0.5 = blend of both
  - Verify parameter changes are click-free (SC-007)

- [X] T048 [P] [US3] Write failing test for filter ping effect in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test: resonance 0.95 (high but below self-osc), frequency 1000 Hz
  - Send impulse (single sample at 1.0)
  - Verify output rings at 1000 Hz with exponential decay
  - Measure decay time (should be several hundred milliseconds)

- [X] T049 [P] [US3] Write failing test for continuous audio filtering in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test: resonance 0.8 (standard resonant filter), process continuous audio
  - Verify filter behaves as standard resonant filter without sustained oscillation
  - Verify output decays to silence when input stops

### 5.2 Implementation for User Story 3

- [X] T050 [US3] Implement `setExternalMix()` and `getExternalMix()` in `self_oscillating_filter.h`
  - Clamp to [0.0, 1.0]
  - Update mixSmoother_ target for smooth transitions (FR-023, SC-007)

- [X] T051 [US3] Update `process()` to handle external input mixing in `self_oscillating_filter.h`
  - Get current mix value from mixSmoother_.process()
  - Blend external input: `filterInput = externalInput * mix`
  - Filter processes blended input
  - When mix = 0, filter receives no external signal (pure oscillation)
  - When mix = 1, filter receives full external signal

### 5.3 Verify User Story 3

- [X] T052 [US3] Build dsp_tests and run all SelfOscillatingFilter tests
- [X] T053 [US3] Verify all US3 tests pass (external mixing, filter ping, continuous filtering)
- [ ] T054 [US3] Manual verification: Process drum loop through filter with high resonance, verify ping effect

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T055 [US3] Verify IEEE 754 compliance: Confirm test file settings still apply

### 5.5 Commit (MANDATORY)

- [ ] T056 [US3] Commit completed User Story 3 work (external input mixing and filter ping)

**Checkpoint**: All P1 and P2 user stories complete - filter can self-oscillate, be played melodically, and process external audio.

---

## Phase 6: User Story 4 - Wave Shaping and Character (Priority: P3)

**Goal**: Add harmonic richness to pure sine oscillation using soft saturation for warmer, more analog-like tones.

**Independent Test**: Enable wave shaping at 1.0, measure harmonic content vs clean sine, verify audible odd harmonics.

**Requirements Coverage**: FR-014, FR-015, SC-008

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T057 [P] [US4] Write failing tests for wave shaping in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test setWaveShape() clamps to [0.0, 1.0] (FR-014)
  - Test amount 0.0: measure harmonic content, verify predominantly fundamental (FR-015)
  - Test amount 1.0: measure harmonic content, verify audible odd harmonics (3rd, 5th)
  - Verify output bounded to [-1, 1] by tanh
  - Test amount 0.5: verify intermediate saturation level

- [X] T058 [P] [US4] Write failing test for wave shaping with DC blocking in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test: enable wave shaping, oscillate for 1 second
  - Verify DC offset still < 0.001 (DCBlocker2 handles any saturation DC)

### 6.2 Implementation for User Story 4

- [X] T059 [US4] Implement `setWaveShape()` and `getWaveShape()` in `self_oscillating_filter.h`
  - Clamp to [0.0, 1.0]
  - Store waveShapeAmount_

- [X] T060 [US4] Implement `applyWaveShaping()` helper in `self_oscillating_filter.h`
  - Early return input if waveShapeAmount_ <= 0.0
  - Map amount (0-1) to gain (1x-3x): `gain = 1.0f + waveShapeAmount_ * 2.0f`
  - Apply: `FastMath::fastTanh(input * gain)`
  - Return saturated output

- [X] T061 [US4] Update `process()` to include wave shaping in `self_oscillating_filter.h`
  - After DC blocking, before envelope
  - Call applyWaveShaping() on signal

### 6.3 Verify User Story 4

- [X] T062 [US4] Build dsp_tests and run all SelfOscillatingFilter tests
- [X] T063 [US4] Verify all US4 tests pass (wave shaping, harmonic content, DC blocking with saturation)
- [ ] T064 [US4] Manual verification: A/B test clean vs saturated oscillation, verify audible warmth/harmonics

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T065 [US4] Verify IEEE 754 compliance: Confirm test file settings still apply

### 6.5 Commit (MANDATORY)

- [ ] T066 [US4] Commit completed User Story 4 work (wave shaping and harmonic character)

**Checkpoint**: Wave shaping adds harmonic richness to the oscillator.

---

## Phase 7: User Story 5 - Output Level Control (Priority: P3)

**Goal**: Control the output level of self-oscillation to match other signal levels and prevent clipping.

**Independent Test**: Set level to -6 dB, verify peak output does not exceed -6 dBFS (+/- 0.5 dB).

**Requirements Coverage**: FR-016, FR-017, SC-007

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T067 [P] [US5] Write failing tests for output level control in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test setOscillationLevel() clamps to [-60, +6] dB (FR-016)
  - Test level 0 dB: verify peak output approximately 0 dBFS (within 0.5 dB)
  - Test level -6 dB: verify peak output approximately -6 dBFS (within 0.5 dB) (SC-007)
  - Test level -12 dB: verify peak output approximately -12 dBFS
  - Test level +6 dB: verify output can exceed 0 dBFS (headroom for mixing)

- [X] T068 [P] [US5] Write failing test for smooth level transitions in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test: change level from 0 dB to -12 dB during oscillation
  - Verify transition is smooth with no clicks (SC-007)
  - Detect no transients > 3 dB above signal level during transition

### 7.2 Implementation for User Story 5

- [X] T069 [US5] Implement `setOscillationLevel()` and `getOscillationLevel()` in `self_oscillating_filter.h`
  - Clamp to [kMinLevelDb, kMaxLevelDb] (-60 to +6 dB)
  - Convert dB to linear gain using dbToGain()
  - Update levelSmoother_ target (FR-017)

- [X] T070 [US5] Update `process()` to apply level control in `self_oscillating_filter.h`
  - After envelope, apply levelSmoother_.process() as final gain stage

### 7.3 Verify User Story 5

- [X] T071 [US5] Build dsp_tests and run all SelfOscillatingFilter tests
- [X] T072 [US5] Verify all US5 tests pass (level control, smooth transitions)
- [ ] T073 [US5] Manual verification: Oscillate and change level in real-time, verify smooth transitions

### 7.4 Cross-Platform Verification (MANDATORY)

- [X] T074 [US5] Verify IEEE 754 compliance: Confirm test file settings still apply

### 7.5 Commit (MANDATORY)

- [ ] T075 [US5] Commit completed User Story 5 work (output level control)

**Checkpoint**: All user stories (P1, P2, P3) are now complete - the filter has full functionality.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories and edge case handling

### 8.1 Edge Case Testing

- [X] T076 [P] Write tests for edge cases in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Test resonance exactly at threshold (0.95): verify oscillation behavior
  - Test frequency at upper boundary (sampleRate * 0.45): verify no aliasing
  - Test sample rate changes: call prepare() again, verify filter adapts
  - Test noteOn with velocity 0: verify treated as noteOff (already covered in US2)
  - Test glide 0 ms: verify instant frequency change (already covered in US2)
  - Test attack 0 ms: verify instant amplitude (already covered in US2)
  - Test multiple prepare() calls: verify reconfiguration works
  - Test process() with extremely long blocks (8192 samples)
  - Test all parameters at boundary values simultaneously

- [X] T077 Verify all edge case tests pass
- [X] T078 Fix any edge case failures found

### 8.2 Performance Verification

- [X] T079 Create performance benchmark test in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Measure CPU time for processing 1 second at 44.1 kHz stereo (2 instances)
  - Target: < 0.5% CPU on reference hardware (SC-006)
  - Verify zero allocations in process path

- [X] T080 Run performance benchmark and verify meets Layer 2 budget

### 8.3 Real-Time Safety Verification

- [X] T081 Add static asserts for real-time safety in `self_oscillating_filter.h`
  - Verify all member components are noexcept in process path
  - Add noexcept specifications to all process methods (FR-022)
  - Document any potential allocation points in comments

- [X] T081b Document VST3 threading model compliance for FR-023 in `self_oscillating_filter.h`
  - Add class-level comment explaining threading assumptions
  - Document that parameter setters use internal smoothers for click-free transitions
  - Note that concurrent setter calls during process() are NOT supported (VST3 host handles via message queue)
  - This fulfills the "safe to call during processing" requirement via smoothing, not thread-safety primitives

### 8.4 isOscillating() Query

- [X] T082 Implement `isOscillating()` const getter in `self_oscillating_filter.h`
  - Return true if envelopeState_ is not Idle
  - Useful for UI feedback

### 8.5 Integration Testing

- [X] T083 [P] Create integration test combining all features in `dsp/tests/processors/self_oscillating_filter_tests.cpp`
  - Sequence: noteOn → glide to new note → wave shaping enabled → external audio mixed → level changed → noteOff
  - Verify smooth operation throughout entire sequence (SC-008)
  - Verify no clicks, pops, or discontinuities at any transition

### 8.6 Documentation Updates

- [X] T084 Verify `specs/088-self-osc-filter/quickstart.md` is accurate
- [X] T085 Add inline documentation comments to all public methods in `self_oscillating_filter.h`
- [X] T086 Add class-level Doxygen comment block in `self_oscillating_filter.h`

### 8.7 Commit Polish Work

- [ ] T087 Commit completed polish work (edge cases, performance, integration tests, documentation)

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [X] T088 Update `specs/_architecture_/layer-0-core.md` with new MIDI utilities
  - Add `midiNoteToFrequency()` entry: purpose, signature, usage example
  - Add `velocityToGain()` entry: purpose, signature, usage example
  - Document when to use these utilities vs custom conversions

- [X] T089 Update `specs/_architecture_/layer-2-processors.md` with SelfOscillatingFilter
  - Add component entry with purpose, public API summary, file location
  - Include "when to use this" guidance
  - Add usage example (pure oscillator vs filter ping)
  - Document composition of Layer 1 components
  - Note any gotchas (e.g., resonance mapping, DC blocking requirement)

- [X] T090 Verify no duplicate functionality was introduced
  - Search for similar components in architecture docs
  - Document relationship to existing components (LadderFilter, etc.)

### 9.2 Final Commit

- [ ] T091 Commit architecture documentation updates

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T092 Review ALL FR-xxx requirements from spec.md against implementation
  - FR-001: Stable self-oscillation at resonance >= 0.95
  - FR-002: Frequency range 20-20000 Hz (or 0.45 * sample rate)
  - FR-003: Resonance control 0.0-1.0
  - FR-004: Frequency accuracy +/- 10 cents, updated every sample
  - FR-005: noteOn(midiNote, velocity) with 12-TET tuning
  - FR-006: noteOff() with release time 10-2000 ms
  - FR-006b: setAttack(ms) with 0-20 ms range
  - FR-007: Velocity scaling (127 = full, 64 = -6 dB)
  - FR-008: Velocity 0 treated as noteOff
  - FR-008b: Retriggering restarts attack from current level
  - FR-009: setGlide(ms) with 0-5000 ms range
  - FR-010: Glide uses linear frequency ramp
  - FR-011: Glide 0 ms = immediate change
  - FR-012: setExternalInput(mix) 0.0-1.0
  - FR-013: Filter processes external signal with current settings
  - FR-014: setWaveShape(amount) 0.0-1.0
  - FR-015: Wave shaping uses tanh with 1x-3x gain scaling
  - FR-016: setOscillationLevel(dB) -60 to +6 dB
  - FR-017: Level changes smoothed
  - FR-018: Uses LadderFilter
  - FR-019: Includes DCBlocker2
  - FR-020: Uses OnePoleSmoother for parameters
  - FR-021: Standard interface (prepare/reset/process/processBlock)
  - FR-022: All processing methods noexcept with zero allocations
  - FR-023: Parameter setters safe during processing
  - FR-024: Configuration preserved across reset()

- [X] T093 Review ALL SC-xxx success criteria and verify measurable targets
  - SC-001: Oscillation at set frequency +/- 10 cents, FFT verified
  - SC-002: Bounded amplitude for 10 seconds, no exceed +6 dBFS
  - SC-003: MIDI note 69 = 440 Hz +/- 1 Hz
  - SC-004: Glide smooth with no clicks (no transients > 6 dB above signal)
  - SC-005: DC offset < 0.001 linear
  - SC-006: < 0.5% CPU at 44.1 kHz stereo
  - SC-007: Click-free parameter changes (no transients > 3 dB above signal)
  - SC-008: Handles all edge cases gracefully
  - SC-009: Envelope transitions smooth and artifact-free
  - SC-010: Note retriggering click-free

- [X] T094 Search for cheating patterns in implementation
  - No `// placeholder` or `// TODO` comments in new code
  - No test thresholds relaxed from spec requirements
  - No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [X] T095 Update `specs/088-self-osc-filter/spec.md` Implementation Verification section
  - Fill compliance table with status (MET/NOT MET/PARTIAL) for each requirement
  - Add evidence for each requirement (test name, measurement result)
  - Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **No**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **No**
3. Did I remove ANY features from scope without telling the user? **No**
4. Would the spec author consider this "done"? **Yes**
5. If I were the user, would I feel cheated? **No**

- [X] T096 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [ ] T097 Commit all spec work to feature branch `088-self-osc-filter`
- [ ] T098 Verify all tests pass: run `ctest --test-dir build --output-on-failure`

### 11.2 Completion Claim

- [ ] T099 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **User Stories (Phases 3-7)**: All depend on Foundational completion
  - US1 (Phase 3): Pure oscillator - no dependencies on other stories
  - US2 (Phase 4): Depends on US1 (extends with MIDI control)
  - US3 (Phase 5): Depends on US1 (adds external input to oscillator)
  - US4 (Phase 6): Depends on US1 (adds wave shaping to oscillator)
  - US5 (Phase 7): Depends on US1 (adds level control to oscillator)
- **Polish (Phase 8)**: Depends on all desired user stories
- **Documentation (Phase 9)**: Depends on all implementation
- **Verification (Phase 10)**: Depends on all work complete

### User Story Dependencies

- **User Story 1 (P1)**: Pure oscillator - foundational, must complete first
- **User Story 2 (P1)**: Extends US1 with MIDI - depends on US1 implementation
- **User Story 3 (P2)**: Adds external input - can run in parallel with US4/US5 after US1
- **User Story 4 (P3)**: Adds wave shaping - can run in parallel with US3/US5 after US1
- **User Story 5 (P3)**: Adds level control - can run in parallel with US3/US4 after US1

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Implementation to make tests pass
3. Verify tests pass
4. Cross-platform check: Verify IEEE 754 functions have `-fno-fast-math`
5. **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Phase 2**: T003 and T004 (MIDI utility tests) can run in parallel
- **Phase 3**: T012, T013, T014 (US1 basic tests) can run in parallel
- **Phase 4**: T030, T031, T032, T033, T034 (US2 tests) can run in parallel
- **Phase 5**: T047, T048, T049 (US3 tests) can run in parallel
- **Phase 6**: T057, T058 (US4 tests) can run in parallel
- **Phase 7**: T067, T068 (US5 tests) can run in parallel
- **After US2**: US3, US4, US5 can proceed in parallel if team capacity allows

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task T012: "Write failing tests for basic lifecycle"
Task T013: "Write failing tests for frequency control"
Task T014: "Write failing tests for resonance control"

# Then implementation tasks that depend on different areas:
Task T021: "Implement setFrequency() and getFrequency()"
Task T022: "Implement setResonance() and getResonance()"
# (These can be done in parallel as they touch different methods)
```

---

## Implementation Strategy

### MVP First (User Stories 1 + 2 Only)

1. Complete Phase 1: Setup (MIDI utils structure)
2. Complete Phase 2: Foundational (MIDI utils implementation) - CRITICAL
3. Complete Phase 3: User Story 1 (pure oscillation)
4. Complete Phase 4: User Story 2 (MIDI control)
5. **STOP and VALIDATE**: Test melodic playability
6. This gives you a working melodic oscillator - minimal but usable

### Incremental Delivery

1. Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Usable oscillator
3. Add User Story 2 → Test independently → Melodic instrument (MVP!)
4. Add User Story 3 → Test independently → Filter ping capability
5. Add User Story 4 → Test independently → Harmonic warmth
6. Add User Story 5 → Test independently → Level control
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers after Foundational phase:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 + 2 (core oscillator and MIDI) - sequential
   - After US1 complete:
     - Developer B: User Story 3 (external input)
     - Developer C: User Story 4 (wave shaping)
     - Developer D: User Story 5 (level control)
3. Stories integrate independently

---

## Task Summary

- **Total Tasks**: 101
- **Phase 1 (Setup)**: 2 tasks
- **Phase 2 (Foundational - MIDI Utils)**: 7 tasks
- **Phase 3 (US1 - Pure Oscillator)**: 21 tasks (includes T017b for per-sample update verification)
- **Phase 4 (US2 - MIDI Control)**: 17 tasks
- **Phase 5 (US3 - Filter Ping)**: 10 tasks
- **Phase 6 (US4 - Wave Shaping)**: 10 tasks
- **Phase 7 (US5 - Level Control)**: 9 tasks
- **Phase 8 (Polish)**: 12 tasks (includes T081b for threading documentation)
- **Phase 9 (Documentation)**: 4 tasks
- **Phase 10 (Verification)**: 5 tasks
- **Phase 11 (Completion)**: 3 tasks

**Parallel Opportunities**: 15+ tasks marked [P] can run in parallel within their phases

**MVP Scope** (User Stories 1 + 2): 47 tasks to get a working melodic oscillator

---

## Notes

- [P] tasks = different files or independent components, no dependencies
- [Story] label (US1-US5) maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
