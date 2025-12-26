# Tasks: Digital Delay Mode

**Input**: Design documents from `/specs/026-digital-delay/`
**Prerequisites**: plan.md (required), spec.md (required), research.md
**Branch**: `026-digital-delay`

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` is in context window. If not, READ IT FIRST.
2. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Project initialization and file structure

- [ ] T001 Create feature header file `src/dsp/features/digital_delay.h` with include guards and namespace
- [ ] T002 Create test file `tests/unit/features/digital_delay_test.cpp` with Catch2 includes
- [ ] T003 Add test file to `tests/CMakeLists.txt` build configuration

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core enums and class skeleton that ALL user stories depend on

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)
- [ ] T005 Define `DigitalEra` enum (Pristine, EightiesDigital, LoFi) in `src/dsp/features/digital_delay.h`
- [ ] T006 Define `LimiterCharacter` enum (Soft, Medium, Hard) in `src/dsp/features/digital_delay.h`
- [ ] T007 Create `DigitalDelay` class skeleton with:
  - prepare() method signature
  - process() method signature
  - reset() method signature
  - Member composition slots for DelayEngine, FeedbackNetwork, CharacterProcessor, DynamicsProcessor, LFO
- [ ] T008 Write basic instantiation test in `tests/unit/features/digital_delay_test.cpp`
- [ ] T009 Build and verify test compiles (will fail - no implementation yet)
- [ ] T010 **Commit foundational structure**

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Pristine Digital Delay (Priority: P1)

**Goal**: Crystal-clear digital delay with zero coloration - the baseline that all other modes build upon

**Independent Test**: Process test signal at 100% wet, verify output matches input (time-shifted) with flat frequency response within 0.1dB from 20Hz-20kHz

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T011 [US1] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T012 [US1] Write test: delay time range 1ms to 10,000ms (FR-001)
- [ ] T013 [US1] Write test: pristine mode flat frequency response within 0.1dB (FR-006, SC-001)
- [ ] T014 [US1] Write test: pristine mode no measurable noise (FR-007, SC-002)
- [ ] T015 [US1] Write test: 100% feedback maintains constant amplitude within 0.5dB (User Story 1 scenario 2)
- [ ] T016 [US1] Write test: 0% mix passes dry signal unaffected (FR-034)
- [ ] T017 [US1] Write test: parameter smoothing prevents zipper noise (FR-033)
- [ ] T018 [US1] Verify all US1 tests fail (no implementation yet)

### 3.3 Implementation for User Story 1

- [ ] T019 [US1] Implement `prepare()` method:
  - Store sample rate
  - Initialize DelayEngine with 10-second max delay
  - Initialize FeedbackNetwork
  - Initialize CharacterProcessor in Clean mode
  - Initialize OnePoleSmoother for parameters
- [ ] T020 [US1] Implement delay time parameter (1ms - 10,000ms range)
- [ ] T021 [US1] Implement feedback parameter (0% - 120% range)
- [ ] T022 [US1] Implement mix parameter (0% - 100%)
- [ ] T023 [US1] Implement output level parameter (-inf to +12dB)
- [ ] T024 [US1] Implement `process()` method for Pristine era:
  - Read from DelayEngine
  - Apply feedback via FeedbackNetwork
  - Bypass CharacterProcessor (clean mode)
  - Apply mix and output level
- [ ] T025 [US1] Implement `reset()` method
- [ ] T026 [US1] Verify all US1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T027 [US1] **Verify IEEE 754 compliance**: Check if test file uses `std::isnan`/`std::isfinite` -> add to `-fno-fast-math` list if needed

### 3.5 Commit (MANDATORY)

- [ ] T028 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Pristine delay fully functional - can be used as transparent digital delay

---

## Phase 4: User Story 2 - 80s Digital Character (Priority: P2)

**Goal**: Authentic 80s digital delay character with subtle quantization and high-frequency rolloff

**Independent Test**: Compare output frequency response showing rolloff starting at 12-14kHz, audible vintage character

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T029 [US2] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T030 [US2] Write test: 80s era applies high-frequency rolloff (FR-008)
- [ ] T031 [US2] Write test: 80s era adds sample rate reduction effect (FR-009)
- [ ] T032 [US2] Write test: 80s era adds noise floor characteristic (FR-010)
- [ ] T033 [US2] Write test: Age parameter controls degradation intensity (FR-041, FR-043)
- [ ] T034 [US2] Write test: era transition has no clicks (SC-005)
- [ ] T035 [US2] Verify all US2 tests fail (no implementation yet)

### 4.3 Implementation for User Story 2

- [ ] T036 [US2] Add era parameter setter with smooth transition
- [ ] T037 [US2] Add age parameter (0-100%) for degradation intensity
- [ ] T038 [US2] Implement 80s era processing path:
  - Configure CharacterProcessor to DigitalVintage mode
  - Map age 0-50% to subtle SR reduction (effective 32kHz)
  - Add low-level noise floor (-80dB)
- [ ] T039 [US2] Implement era crossfade for smooth transitions
- [ ] T040 [US2] Verify all US2 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T041 [US2] **Verify IEEE 754 compliance**: Check test file for IEEE 754 functions

### 4.5 Commit (MANDATORY)

- [ ] T042 [US2] **Commit completed User Story 2 work**

**Checkpoint**: 80s Digital character works independently of other eras

---

## Phase 5: User Story 4 - Tempo-Synced Delay (Priority: P2)

**Goal**: Delay time locks to DAW tempo with musical note values

**Independent Test**: Set tempo sync to quarter note at 120 BPM, verify delay time is exactly 500ms

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T043 [US4] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 5.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T044 [US4] Write test: tempo sync supports note values 1/64 to 1/1 with dotted and triplet (FR-003)
- [ ] T045 [US4] Write test: quarter note at 120 BPM = 500ms (User Story 4 scenario 1)
- [ ] T046 [US4] Write test: dotted eighth at 120 BPM = 375ms (User Story 4 scenario 2)
- [ ] T047 [US4] Write test: tempo change transitions smoothly (FR-004, User Story 4 scenario 3)
- [ ] T048 [US4] Write test: sync accuracy within 1 sample (SC-008)
- [ ] T049 [US4] Verify all US4 tests fail (no implementation yet)

### 5.3 Implementation for User Story 4

- [ ] T050 [US4] Add time mode parameter (Free/Synced) - reuse TimeMode enum from DelayEngine
- [ ] T051 [US4] Add note value parameter with NoteValue support
- [ ] T052 [US4] Implement tempo sync calculation using BlockContext tempo
- [ ] T053 [US4] Implement smooth delay time transition on tempo changes
- [ ] T054 [US4] Verify all US4 tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T055 [US4] **Verify IEEE 754 compliance**: Check test file for IEEE 754 functions

### 5.5 Commit (MANDATORY)

- [ ] T056 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Tempo sync works with any era preset

---

## Phase 6: User Story 3 - Lo-Fi Digital Degradation (Priority: P3)

**Goal**: Aggressive digital degradation for creative sound design

**Independent Test**: Process audio with Lo-Fi era, verify obvious bit reduction and aliasing artifacts

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T057 [US3] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 6.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T058 [US3] Write test: Lo-Fi applies aggressive bit depth reduction (FR-011)
- [ ] T059 [US3] Write test: Lo-Fi creates audible aliasing (FR-012)
- [ ] T060 [US3] Write test: Lo-Fi produces obviously degraded audio (FR-013, SC-004)
- [ ] T061 [US3] Write test: Age at 100% provides maximum degradation (FR-044)
- [ ] T062 [US3] Write test: bit reduction maintains minimum depth for quiet signals (Edge case)
- [ ] T063 [US3] Verify all US3 tests fail (no implementation yet)

### 6.3 Implementation for User Story 3

- [ ] T064 [US3] Implement Lo-Fi era processing path:
  - Configure CharacterProcessor with aggressive settings
  - Map age 0-100% to bit depth (down to 8-bit at max)
  - Map age to sample rate reduction (down to 8kHz at max)
- [ ] T065 [US3] Add minimum bit depth floor for quiet signals (prevent total silence)
- [ ] T066 [US3] Verify all US3 tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [ ] T067 [US3] **Verify IEEE 754 compliance**: Check test file for IEEE 754 functions

### 6.5 Commit (MANDATORY)

- [ ] T068 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Lo-Fi mode provides creative degradation effects

---

## Phase 7: User Story 5 - Program-Dependent Limiting (Priority: P3)

**Goal**: Feedback limiter that prevents clipping while preserving transients

**Independent Test**: Set feedback to 120%, verify output remains stable without digital clipping

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T069 [US5] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T070 [US5] Write test: feedback 120% produces stable output (FR-016, User Story 5 scenario 1)
- [ ] T071 [US5] Write test: limiter allows transients through on initial repeats (FR-017, User Story 5 scenario 2)
- [ ] T072 [US5] Write test: limiter applies increasing gain reduction as feedback builds (FR-018, User Story 5 scenario 3)
- [ ] T073 [US5] Write test: limiter character selectable (Soft/Medium/Hard) (FR-019)
- [ ] T074 [US5] Verify all US5 tests fail (no implementation yet)

### 7.3 Implementation for User Story 5

- [ ] T075 [US5] Add limiter character parameter (LimiterCharacter enum)
- [ ] T076 [US5] Configure DynamicsProcessor in feedback path:
  - Detection: Peak (transient-responsive)
  - Threshold: -0.5dBFS
  - Ratio: 100:1 (true limiting)
- [ ] T077 [US5] Implement knee mapping:
  - Soft: 6dB knee
  - Medium: 3dB knee
  - Hard: 0dB knee (hard limiting)
- [ ] T078 [US5] Integrate limiter into feedback path after FeedbackNetwork
- [ ] T079 [US5] Verify all US5 tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [ ] T080 [US5] **Verify IEEE 754 compliance**: Check test file for IEEE 754 functions

### 7.5 Commit (MANDATORY)

- [ ] T081 [US5] **Commit completed User Story 5 work**

**Checkpoint**: High feedback settings are safe and musically useful

---

## Phase 8: User Story 6 - Modulated Digital Delay (Priority: P3)

**Goal**: Flexible LFO modulation with 6 waveform shapes for creative delay effects

**Independent Test**: Enable modulation with each waveform type, verify characteristic pitch variation

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T082 [US6] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 8.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T083 [US6] Write test: modulation depth 0% produces zero pitch variation (FR-024)
- [ ] T084 [US6] Write test: Sine waveform produces smooth pitch variation (FR-025)
- [ ] T085 [US6] Write test: Triangle waveform produces linear sweeps (FR-026)
- [ ] T086 [US6] Write test: Sawtooth waveform produces pitch ramps (FR-027)
- [ ] T087 [US6] Write test: Square waveform produces alternating pitch steps (FR-028)
- [ ] T088 [US6] Write test: Sample & Hold produces random stepped changes (FR-029)
- [ ] T089 [US6] Write test: Random (smoothed) produces continuous random movement (FR-030)
- [ ] T090 [US6] Write test: modulation rate range 0.1Hz to 10Hz (FR-022)
- [ ] T091 [US6] Verify all US6 tests fail (no implementation yet)

### 8.3 Implementation for User Story 6

- [ ] T092 [US6] Add modulation depth parameter (0-100%)
- [ ] T093 [US6] Add modulation rate parameter (0.1Hz - 10Hz)
- [ ] T094 [US6] Add waveform parameter using existing LFO::Waveform enum
- [ ] T095 [US6] Integrate LFO primitive for delay time modulation:
  - Map waveform selection to LFO
  - Apply depth to DelayEngine time modulation
- [ ] T096 [US6] Ensure 0% depth truly bypasses modulation
- [ ] T097 [US6] Verify all US6 tests pass

### 8.4 Cross-Platform Verification (MANDATORY)

- [ ] T098 [US6] **Verify IEEE 754 compliance**: Check test file for IEEE 754 functions

### 8.5 Commit (MANDATORY)

- [ ] T099 [US6] **Commit completed User Story 6 work**

**Checkpoint**: All 6 waveform shapes provide distinct modulation character

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Final integration and quality improvements

- [ ] T100 Verify stereo processing (FR-035, FR-037)
- [ ] T101 Verify mono processing (FR-036)
- [ ] T102 Verify real-time safety: no allocations in process() (FR-038)
- [ ] T103 Verify all processing functions are noexcept (FR-039)
- [ ] T104 Performance test: verify < 1% CPU at 44.1kHz stereo (SC-007)
- [ ] T105 Test edge case: minimum delay time (1ms) produces clean comb filtering
- [ ] T106 Test edge case: maximum delay time (10s) works without buffer issues
- [ ] T107 Verify Age parameter has no effect in Pristine mode (FR-042)

---

## Phase 10: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T108 **Update ARCHITECTURE.md** with new components:
  - Add DigitalDelay to Layer 4 features section
  - Document: purpose, public API summary, file location, "when to use this"
  - Add usage examples
  - Verify no duplicate functionality was introduced

### 10.2 Commit Documentation

- [ ] T109 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 11.1 Requirements Verification

- [ ] T110 **Review ALL FR-xxx requirements** (FR-001 to FR-044) from spec.md against implementation
- [ ] T111 **Review ALL SC-xxx success criteria** (SC-001 to SC-012) and verify measurable targets
- [ ] T112 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table

- [ ] T113 **Update spec.md "Implementation Verification" section** with compliance status
- [ ] T114 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Final Commit

- [ ] T115 **Commit all spec work** to feature branch
- [ ] T116 **Verify all tests pass**
- [ ] T117 **Claim completion** (only if all requirements MET or gaps explicitly approved)

**Checkpoint**: Honest assessment complete - spec implementation done

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **User Stories (Phases 3-8)**: All depend on Foundational phase completion
  - US1 (Pristine) must complete first - other stories build on it
  - US2 (80s Digital) and US4 (Tempo Sync) can proceed in parallel after US1
  - US3 (Lo-Fi) can proceed after US2 (shares CharacterProcessor pattern)
  - US5 (Limiter) can proceed after US1
  - US6 (Modulation) can proceed after US1
- **Polish (Phase 9)**: Depends on all user stories complete
- **Documentation (Phase 10)**: Depends on Polish
- **Verification (Phase 11)**: Final phase

### User Story Dependencies

```
US1 (Pristine) ──┬──> US2 (80s Digital) ──> US3 (Lo-Fi)
                 │
                 ├──> US4 (Tempo Sync)
                 │
                 ├──> US5 (Limiter)
                 │
                 └──> US6 (Modulation)
```

### Parallel Opportunities

Within each user story phase, tests marked [P] can run in parallel:
- All tests for a story can be written in parallel
- Implementation tasks must be sequential (build on each other)

After US1 completes, these can run in parallel:
- US2 + US4 (no dependencies on each other)
- US5 + US6 (no dependencies on each other)

---

## Task Summary

| Phase | Story | Task Count | Description |
|-------|-------|------------|-------------|
| 1 | - | 3 | Setup |
| 2 | - | 7 | Foundational |
| 3 | US1 | 18 | Pristine Digital Delay (P1) |
| 4 | US2 | 14 | 80s Digital Character (P2) |
| 5 | US4 | 14 | Tempo-Synced Delay (P2) |
| 6 | US3 | 12 | Lo-Fi Digital Degradation (P3) |
| 7 | US5 | 13 | Program-Dependent Limiting (P3) |
| 8 | US6 | 18 | Modulated Digital Delay (P3) |
| 9 | - | 8 | Polish |
| 10 | - | 2 | Architecture Documentation |
| 11 | - | 8 | Completion Verification |

**Total Tasks**: 117

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1 (Pristine)
4. **STOP and VALIDATE**: Test pristine delay independently
5. Can be deployed as a clean digital delay

### Incremental Delivery

1. Setup + Foundational -> Foundation ready
2. Add US1 (Pristine) -> Test -> Deploy (MVP!)
3. Add US2 (80s Digital) + US4 (Tempo Sync) -> Test -> Deploy
4. Add US3 (Lo-Fi) -> Test -> Deploy
5. Add US5 (Limiter) + US6 (Modulation) -> Test -> Deploy (Full feature)

Each increment adds value without breaking previous functionality.

---

## Notes

- Reuse existing `Waveform` enum from LFO (do not create `ModulationWaveform`)
- Reuse existing `TimeMode` enum from DelayEngine
- Use `CharacterMode::Clean` for Pristine, `CharacterMode::DigitalVintage` for 80s/Lo-Fi
- DynamicsProcessor configured as limiter (Peak detection, 100:1 ratio)
- All Layer 4 features go in `src/dsp/features/`
- Follow TapeDelay and BBDDelay patterns in same directory
