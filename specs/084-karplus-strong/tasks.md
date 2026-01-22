# Tasks: Karplus-Strong String Synthesizer

**Input**: Design documents from `/specs/084-karplus-strong/`
**Prerequisites**: plan.md (Layer 2 processor design), spec.md (5 user stories with priorities P1-P5)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             processors/karplus_strong_tests.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create TwoPoleLP primitive (Layer 1) needed by all Karplus-Strong excitation methods

**Requirements Covered**: None directly (supporting component for FR-014)

- [ ] T001 Create test file for TwoPoleLP primitive in dsp/tests/primitives/two_pole_lp_tests.cpp with test cases for frequency response, NaN handling, and unprepared state
- [ ] T002 Create TwoPoleLP class in dsp/include/krate/dsp/primitives/two_pole_lp.h as Butterworth lowpass filter wrapper around Biquad with 12dB/oct slope
- [ ] T003 Verify TwoPoleLP tests pass: frequency response within 0.5dB at cutoff, -12dB at one octave above
- [ ] T004 Commit TwoPoleLP primitive implementation

**Checkpoint**: TwoPoleLP primitive ready for use by KarplusStrong excitation methods

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core KarplusStrong processor structure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

**Requirements Covered**: FR-001, FR-002, FR-003, FR-004, FR-022, FR-023, FR-024, FR-025, FR-026, FR-027, FR-028, FR-029, FR-030, FR-031, FR-032

- [ ] T005 Create test file for KarplusStrong processor in dsp/tests/processors/karplus_strong_tests.cpp with lifecycle test cases (prepare/reset/unprepared)
- [ ] T006 Create KarplusStrong class shell in dsp/include/krate/dsp/processors/karplus_strong.h with prepare(), reset(), process() methods and member variables for DelayLine, OnePoleLP, Allpass1Pole, DCBlocker2, Xorshift32
- [ ] T007 Add NaN/Inf input handling test (FR-030): verify process() resets and returns 0.0f on invalid input
- [ ] T008 Implement input validation in process() using detail::isNaN() and detail::isInf() from core/db_utils.h
- [ ] T009 Add frequency clamping test (FR-031): verify frequencies below minFrequency or above Nyquist/2 are clamped
- [ ] T010 Implement setFrequency() with clamping to [minFrequency, sampleRate/2 * 0.99] and delay length calculation (delaySamples = sampleRate / frequency)
- [ ] T011 Add basic feedback loop test: verify delay line with allpass interpolation produces output at correct frequency
- [ ] T012 Implement process() with feedback loop: DelayLine (allpass mode) → OnePoleLP (damping) → Allpass1Pole (stretch) → DCBlocker2 → feedback multiplication → write back to delay line
- [ ] T013 Add denormal flushing test: verify no CPU spikes after 10 minutes with very low amplitude signal
- [ ] T014 Implement denormal flushing in feedback loop using detail::flushDenormal() from core/db_utils.h
- [ ] T015 Add DC blocking test (FR-029): verify no DC offset accumulation after 10 minutes of continuous operation (SC-005)
- [ ] T016 Verify all foundational tests pass: lifecycle, input validation, frequency clamping, feedback loop, denormals, DC blocking
- [ ] T017 **Verify IEEE 754 compliance**: Add processors/karplus_strong_tests.cpp to `-fno-fast-math` list in dsp/tests/CMakeLists.txt (uses std::isnan/std::isinf in FR-030 tests)
- [ ] T018 Commit foundational KarplusStrong processor structure

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Plucked String Sound (Priority: P1) 🎯 MVP

**Goal**: Create realistic plucked string sounds with noise burst excitation and damping control

**Independent Test**: Call pluck(), process samples, verify pitch accuracy within 1 cent and exponential decay envelope

**Requirements Covered**: FR-005, FR-006, FR-011, FR-012, FR-017, FR-018, FR-019, SC-001, SC-002, SC-003, SC-006

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T019 [P] [US1] Add pitch accuracy test (TC-KS-001, SC-001): verify setFrequency(440.0f) + pluck(1.0f) produces output at 440Hz within 1 cent using FFT-based pitch detection
- [ ] T020 [P] [US1] Add damping tone test (TC-KS-012, US1-AC2): verify setDamping(0.5f) has less high-frequency content than setDamping(0.1f) via spectral analysis
- [ ] T021 [P] [US1] Add damping decay test (TC-KS-013): verify higher damping produces faster decay time
- [ ] T022 [P] [US1] Add exponential decay test (TC-KS-002, SC-003, US1-AC3): verify decay time matches setDecay() within 10% measured as RT60
- [ ] T023 [P] [US1] Add pluck velocity test (TC-KS-007, FR-006): verify pluck velocity scales amplitude proportionally
- [ ] T024 [P] [US1] Add frequency response test (SC-006): verify frequency changes produce audible pitch changes within 1ms

### 3.2 Implementation for User Story 1

- [ ] T025 [US1] Implement pluck(velocity) method: fill delay line with filtered noise from Xorshift32, scale by velocity parameter (FR-005, FR-006)
- [ ] T026 [US1] Implement setDamping(amount) method: calculate damping filter cutoff relative to fundamental frequency (cutoff = fundamental × multiplier), configure OnePoleLP (FR-011, FR-012)
- [ ] T027 [US1] Implement setDecay(seconds) method: calculate feedback coefficient using formula `feedback = 10^(-3 * delaySamples / (decayTime * sampleRate))`, clamp to 0.9999 max (FR-017, FR-018, FR-019)
- [ ] T028 [US1] Verify all User Story 1 tests pass: pitch accuracy, damping tone/decay, exponential decay, pluck velocity, frequency response

### 3.3 Cross-Platform Verification (MANDATORY)

- [ ] T029 [US1] **Verify IEEE 754 compliance**: Confirm processors/karplus_strong_tests.cpp is in `-fno-fast-math` list (already added in T017)

### 3.4 Commit (MANDATORY)

- [ ] T030 [US1] **Commit completed User Story 1 work** - basic plucked string with pitch, damping, and decay control

**Checkpoint**: User Story 1 (MVP) should be fully functional - realistic plucked string sounds achievable within 30 seconds (SC-002)

---

## Phase 4: User Story 2 - Tone Shaping with Brightness and Pick Position (Priority: P2)

**Goal**: Shape timbre by adjusting excitation brightness and simulating pick position comb filtering

**Independent Test**: Compare spectral analysis of different brightness and pick position settings

**Requirements Covered**: FR-013, FR-014, FR-015, FR-016

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T031 [P] [US2] Add brightness full spectrum test (TC-KS-014, US2-AC1): verify setBrightness(1.0f) + pluck() produces full-spectrum noise resulting in bright, metallic tone
- [ ] T032 [P] [US2] Add brightness low spectrum test (TC-KS-014, US2-AC2): verify setBrightness(0.2f) + pluck() produces low-pass filtered excitation resulting in warmer, softer tone
- [ ] T033 [P] [US2] Add pick position middle test (TC-KS-015, US2-AC3): verify setPickPosition(0.5f) emphasizes fundamental and odd harmonics (even harmonics attenuated) via spectral analysis
- [ ] T034 [P] [US2] Add pick position bridge test (TC-KS-016, US2-AC4): verify setPickPosition(0.1f) produces more harmonics creating brighter, thinner sound

### 4.2 Implementation for User Story 2

- [ ] T035 [US2] Implement setBrightness(amount) method: configure TwoPoleLP filter cutoff based on brightness parameter (0.0-1.0), apply to excitation noise before delay line injection (FR-013, FR-014)
- [ ] T036 [US2] Implement setPickPosition(position) method: calculate delay line tap offset (position × delayLength), read from tap during excitation fill to create physically accurate comb filtering (FR-015, FR-016)
- [ ] T037 [US2] Integrate brightness filter into pluck() method: apply TwoPoleLP to noise burst before writing to delay line
- [ ] T038 [US2] Integrate pick position into pluck() method: read from delay line tap at calculated offset, mix with excitation signal
- [ ] T039 [US2] Verify all User Story 2 tests pass: brightness full/low spectrum, pick position middle/bridge

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T040 [US2] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 functions added (no changes needed)

### 4.4 Commit (MANDATORY)

- [ ] T041 [US2] **Commit completed User Story 2 work** - tone shaping with brightness and pick position

**Checkpoint**: User Stories 1 AND 2 should both work independently - diverse string timbres now available

---

## Phase 5: User Story 3 - Continuous Excitation (Bowing) (Priority: P3)

**Goal**: Create sustained string sounds with continuous excitation for infinite sustain effects

**Independent Test**: Call bow() with pressure and verify sustained oscillation continues indefinitely

**Requirements Covered**: FR-007, FR-008, SC-009

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T042 [P] [US3] Add bow sustained oscillation test (TC-KS-008, SC-009, US3-AC1): verify bow(0.5f) produces sustained oscillation that does not decay for 10+ seconds
- [ ] T043 [P] [US3] Add bow pressure scaling test (TC-KS-008, US3-AC2): verify increasing bow pressure increases output amplitude proportionally
- [ ] T044 [P] [US3] Add bow release test (TC-KS-009, US3-AC3): verify bow(0.0f) causes natural decay as if released

### 5.2 Implementation for User Story 3

- [ ] T045 [US3] Implement bow(pressure) method: continuously inject filtered noise into delay line at amplitude scaled by pressure parameter (0.0-1.0), apply brightness filter if set (FR-007, FR-008)
- [ ] T046 [US3] Integrate bow() into process() method: add bow noise injection after feedback write, handle bow pressure state transitions
- [ ] T047 [US3] Verify all User Story 3 tests pass: bow sustained oscillation, pressure scaling, release behavior

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T048 [US3] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 functions added (no changes needed)

### 5.4 Commit (MANDATORY)

- [ ] T049 [US3] **Commit completed User Story 3 work** - continuous excitation (bowing) mode

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently - sustained string sounds now available

---

## Phase 6: User Story 4 - Custom Excitation Signal (Priority: P4)

**Goal**: Inject custom excitation signals for unique hybrid sounds blending synthesis with sampled content

**Independent Test**: Provide custom excitation buffer and verify string resonates at set frequency with excitation's timbral character

**Requirements Covered**: FR-009, FR-010

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T050 [P] [US4] Add custom excitation test (TC-KS-010, US4-AC1): verify excite() with 100-sample sine wave burst produces purer, more tonal quality than noise excitation via spectral analysis
- [ ] T051 [P] [US4] Add sympathetic resonance test (TC-KS-010, US4-AC2): verify process(input) with external audio causes string to resonate sympathetically at tuned frequency

### 6.2 Implementation for User Story 4

- [ ] T052 [US4] Implement excite(signal, length) method: copy custom excitation buffer into delay line, apply brightness filter if set, handle buffer length clamping (FR-009)
- [ ] T053 [US4] Integrate external excitation into process(input) method: add input signal to feedback path, apply brightness filter (FR-010)
- [ ] T054 [US4] Verify all User Story 4 tests pass: custom excitation with sine burst, sympathetic resonance with external audio

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T055 [US4] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 functions added (no changes needed)

### 6.4 Commit (MANDATORY)

- [ ] T056 [US4] **Commit completed User Story 4 work** - custom excitation signal injection

**Checkpoint**: User Stories 1-4 should all work independently - hybrid synthesis now available

---

## Phase 7: User Story 5 - Inharmonicity Control (Stretch Tuning) (Priority: P5)

**Goal**: Add piano-like inharmonicity by introducing dispersion that causes upper harmonics to be slightly sharp

**Independent Test**: Measure frequency of upper partials with stretch enabled vs disabled

**Requirements Covered**: FR-020, FR-021, SC-010

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T057 [P] [US5] Add harmonic tuning test (TC-KS-017, US5-AC1): verify setStretch(0.0f) produces harmonics that are integer multiples of fundamental via FFT analysis
- [ ] T058 [P] [US5] Add inharmonicity test (TC-KS-018, SC-010, US5-AC2): verify setStretch(0.5f) produces upper harmonics progressively sharper than integer multiples (piano-like)
- [ ] T059 [P] [US5] Add bell-like timbre test (TC-KS-019, US5-AC3): verify high stretch values produce bell-like or metallic timbre

### 7.2 Implementation for User Story 5

- [ ] T060 [US5] Implement setStretch(amount) method: calculate Allpass1Pole frequency coefficient based on stretch parameter (0.0-1.0) to create dispersion (FR-020, FR-021)
- [ ] T061 [US5] Integrate stretch into feedback loop: ensure Allpass1Pole is active in feedback path when stretch > 0.0, bypass when stretch = 0.0 for efficiency
- [ ] T062 [US5] Verify all User Story 5 tests pass: harmonic tuning, inharmonicity, bell-like timbre

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T063 [US5] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 functions added (no changes needed)

### 7.4 Commit (MANDATORY)

- [ ] T064 [US5] **Commit completed User Story 5 work** - inharmonicity control (stretch tuning)

**Checkpoint**: All 5 user stories should now be independently functional - complete Karplus-Strong synthesizer

---

## Phase 8: Edge Cases and Stability

**Purpose**: Handle all edge cases documented in spec.md and ensure long-term stability

**Requirements Covered**: FR-032, FR-033, SC-004, SC-005, SC-007, SC-008

### 8.1 Edge Case Tests (Write FIRST - Must FAIL)

- [ ] T065 [P] Add parameter clamping test (FR-032): verify all parameters (damping, brightness, pickPosition, stretch, velocity, pressure) clamped to 0.0-1.0 range
- [ ] T066 [P] Add re-pluck normalization test (TC-KS-011, FR-033): verify pluck() during active excitation adds to buffer and normalizes if sum exceeds ±1.0
- [ ] T067 [P] Add extreme frequency low test (TC-KS-020): verify frequency below 20Hz clamped to minFrequency
- [ ] T068 [P] Add extreme frequency high test (TC-KS-021, FR-031): verify frequency above Nyquist/2 clamped to sampleRate/2 * 0.99
- [ ] T069 [P] Add very short decay test (TC-KS-022): verify setDecay(<10ms) produces brief transient with minimum decay time enforced
- [ ] T070 [P] Add very long decay test (TC-KS-023): verify setDecay(>30s) feedback clamped to 0.9999 to prevent instability
- [ ] T071 [P] Add long-term stability test (TC-KS-024, SC-005): verify 10 minutes continuous operation with no runaway feedback, DC offset, or denormal slowdown
- [ ] T072 [P] Add parameter smoothness test (SC-008): verify all parameter changes respond without audible clicks or discontinuities
- [ ] T072.5 [P] Add modulation integration test (SC-007): verify setFrequency/setDamping/setBrightness can be called at audio rate (per-sample) without artifacts or instability

### 8.2 Edge Case Implementation

- [ ] T073 Implement parameter clamping in all setter methods using std::clamp() (FR-032)
- [ ] T074 Implement re-pluck normalization: sum new excitation with existing buffer, check max absolute value, normalize if > 1.0 (FR-033)
- [ ] T075 Implement minimum decay time enforcement (10ms) and maximum feedback clamping (0.9999) in setDecay()
- [ ] T076 Add parameter smoothing for damping and brightness cutoff changes to prevent clicks (SC-008)
- [ ] T077 Verify all edge case tests pass: parameter clamping, re-pluck normalization, extreme frequencies, extreme decay times, long-term stability, parameter smoothness

### 8.3 Performance Testing

- [ ] T078 Add CPU usage test (TC-KS-025, SC-004): verify processing uses < 0.5% CPU per voice at 44.1kHz sample rate on reference hardware
- [ ] T079 Optimize if needed: consider bypass logic for unused features (stretch=0, brightness=1.0, pickPosition=0.0)
- [ ] T080 Verify CPU usage test passes after optimization

### 8.4 Cross-Platform Verification (MANDATORY)

- [ ] T081 **Verify IEEE 754 compliance**: Confirm processors/karplus_strong_tests.cpp is in `-fno-fast-math` list (already added in T017)

### 8.5 Commit (MANDATORY)

- [ ] T082 **Commit completed edge case handling and stability work**

**Checkpoint**: All edge cases handled, long-term stability verified, performance target met

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [ ] T083 **Update specs/_architecture_/layer-1-primitives.md**: Add TwoPoleLP entry with purpose (12dB/oct Butterworth lowpass), public API (prepare, setCutoff, getCutoff, process, processBlock, reset), file location (dsp/include/krate/dsp/primitives/two_pole_lp.h), when to use (excitation filtering, brightness control)
- [ ] T084 **Update specs/_architecture_/layer-2-processors.md**: Add KarplusStrong entry with purpose (plucked string synthesis), public API summary (lifecycle, excitation methods, tone shaping, processing), file location (dsp/include/krate/dsp/processors/karplus_strong.h), when to use (realistic plucked/bowed strings, guitar/harp/harpsichord sounds), usage examples (basic pluck with decay/damping)
- [ ] T085 Verify no duplicate functionality introduced: confirm no overlap with FeedbackComb or other resonators

### 9.2 Final Commit

- [ ] T086 **Commit architecture documentation updates**
- [ ] T087 Verify all spec work is committed to feature branch 084-karplus-strong

**Checkpoint**: Architecture documentation reflects TwoPoleLP and KarplusStrong components

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T088 **Review ALL FR-001 to FR-033 requirements** from spec.md against implementation: delay line algorithm, allpass interpolation, frequency range, excitation methods, damping/tone control, decay control, inharmonicity, lifecycle, real-time safety, edge cases
- [ ] T089 **Review ALL SC-001 to SC-010 success criteria** and verify measurable targets are achieved: 1 cent pitch accuracy, 30 second usability, 10% decay accuracy, <0.5% CPU, 10 minute stability, 1ms frequency response, modulation integration, smooth parameters, infinite bow sustain, audible stretch at 0.3+
- [ ] T090 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in dsp/include/krate/dsp/processors/karplus_strong.h
  - [ ] No `// placeholder` or `// TODO` comments in dsp/include/krate/dsp/primitives/two_pole_lp.h
  - [ ] No test thresholds relaxed from spec requirements (1 cent, 10%, <0.5%, etc.)
  - [ ] No features quietly removed from scope (all 5 user stories implemented)

### 10.2 Fill Compliance Table in spec.md

- [ ] T091 **Update spec.md "Implementation Verification" section** with compliance status (MET/NOT MET/PARTIAL/DEFERRED) for each FR-001 to FR-033 requirement with test evidence
- [ ] T092 **Update spec.md "Implementation Verification" section** with compliance status for each SC-001 to SC-010 success criterion with measurement evidence
- [ ] T093 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T094 **All self-check questions answered "no"** (or gaps documented honestly in spec.md compliance table)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [ ] T095 **Commit all spec work** to feature branch 084-karplus-strong
- [ ] T096 **Verify all tests pass**: Run dsp_tests target and confirm all karplus_strong_tests.cpp and two_pole_lp_tests.cpp pass

### 11.2 Completion Claim

- [ ] T097 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete - Karplus-Strong String Synthesizer ready for integration

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
  - Creates TwoPoleLP primitive (Layer 1)
- **Foundational (Phase 2)**: Depends on Phase 1 completion - BLOCKS all user stories
  - Creates KarplusStrong core structure with lifecycle and feedback loop
- **User Stories (Phases 3-7)**: All depend on Foundational phase completion
  - Can proceed in parallel (if staffed) or sequentially in priority order (P1 → P2 → P3 → P4 → P5)
- **Edge Cases (Phase 8)**: Depends on all user stories being complete
- **Documentation (Phase 9)**: Depends on Phase 8 completion
- **Verification (Phase 10-11)**: Depends on Phase 9 completion

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
  - Implements pluck(), setDamping(), setDecay()
- **User Story 2 (P2)**: Can start after Foundational (Phase 2) - Extends US1 pluck() but independently testable
  - Implements setBrightness(), setPickPosition()
- **User Story 3 (P3)**: Can start after Foundational (Phase 2) - Independent excitation mode
  - Implements bow()
- **User Story 4 (P4)**: Can start after Foundational (Phase 2) - Independent excitation mode
  - Implements excite() and process(input)
- **User Story 5 (P5)**: Can start after Foundational (Phase 2) - Independent tone shaping
  - Implements setStretch()

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation after all tests written
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- **Phase 1 (Setup)**: All tasks sequential (TwoPoleLP creation)
- **Phase 2 (Foundational)**: Tasks mostly sequential (building core structure)
- **Phases 3-7 (User Stories)**: Once Foundational completes, all user stories can start in parallel (if team capacity allows)
  - Within each story: All test tasks marked [P] can run in parallel
- **Phase 8 (Edge Cases)**: All test tasks marked [P] can run in parallel

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task T019: "Pitch accuracy test"
Task T020: "Damping tone test"
Task T021: "Damping decay test"
Task T022: "Exponential decay test"
Task T023: "Pluck velocity test"
Task T024: "Frequency response test"

# Then implement sequentially:
Task T025: "Implement pluck()"
Task T026: "Implement setDamping()"
Task T027: "Implement setDecay()"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (TwoPoleLP primitive)
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (basic plucked string)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Deploy/demo if ready

**Result**: Realistic plucked string sounds (guitar, harp, harpsichord) achievable within 30 seconds (SC-002)

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Deploy/Demo (MVP - basic plucked string!)
3. Add User Story 2 → Test independently → Deploy/Demo (tone shaping with brightness/pick position)
4. Add User Story 3 → Test independently → Deploy/Demo (bowing mode for sustained sounds)
5. Add User Story 4 → Test independently → Deploy/Demo (custom excitation for hybrid sounds)
6. Add User Story 5 → Test independently → Deploy/Demo (inharmonicity for piano-like character)
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (pluck + damping + decay)
   - Developer B: User Story 2 (brightness + pick position)
   - Developer C: User Story 3 (bowing)
   - Developer D: User Story 4 (custom excitation)
   - Developer E: User Story 5 (inharmonicity)
3. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files or independent test cases, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- All tests use Catch2 framework via dsp_tests target
- FFT-based pitch detection available in test infrastructure for pitch accuracy verification (SC-001)
- RT60 decay measurement needed for decay accuracy verification (SC-003)
- Spectral analysis needed for damping, brightness, pick position, and stretch verification
- CPU usage measurement via chrono for performance verification (SC-004)
