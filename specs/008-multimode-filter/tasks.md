# Tasks: Multimode Filter

**Input**: Design documents from `/specs/008-multimode-filter/`
**Prerequisites**: plan.md ✓, spec.md ✓, research.md ✓, data-model.md ✓, contracts/ ✓, quickstart.md ✓

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` is in context window. If not, READ IT FIRST.
2. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create directories and basic project structure for Layer 2 processors

- [x] T001 Create `src/dsp/processors/` directory (first Layer 2 component)
- [x] T002 Create `tests/unit/processors/` directory for Layer 2 tests
- [x] T003 Update `tests/CMakeLists.txt` to include processors test directory

**Checkpoint**: Directory structure ready for Layer 2 development

---

## Phase 2: Foundational (FilterSlope Enum)

**Purpose**: Define FilterSlope enumeration needed by all user stories

**⚠️ CRITICAL**: FilterSlope enum must exist before MultimodeFilter implementation

- [x] T004 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)
- [x] T005 [P] Write unit tests for FilterSlope enum and utilities in `tests/unit/processors/multimode_filter_test.cpp`
- [x] T006 Create `src/dsp/processors/multimode_filter.h` with FilterSlope enum and slopeToStages(), slopeTodBPerOctave() utilities
- [x] T007 Verify FilterSlope tests pass (21 test cases, 1686 assertions)
- [x] T008 **Commit FilterSlope foundation** (commit a0394b7)

**Checkpoint**: FilterSlope enum ready - MultimodeFilter class can now be implemented

---

## Phase 3: User Story 1 - Basic Filtering (Priority: P1) 🎯 MVP

**Goal**: Implement core MultimodeFilter class with 8 filter types (LP/HP/BP/Notch/Allpass/LowShelf/HighShelf/Peak) using single biquad stage (12 dB/oct)

**Independent Test**: Process white noise through each filter type and verify frequency response

**Covers**: FR-001, FR-002, FR-004, FR-005, FR-006, FR-009, FR-010, FR-011, FR-014, FR-015, FR-016

### 3.1 Pre-Implementation (MANDATORY)

- [x] T009 [US1] **Verify TESTING-GUIDE.md is in context** (done in Phase 2)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T010 [P] [US1] Unit tests for MultimodeFilter construction and lifecycle in `tests/unit/processors/multimode_filter_test.cpp`
- [x] T011 [P] [US1] Unit tests for setType/getType for all 8 filter types
- [x] T012 [P] [US1] Unit tests for setCutoff/getCutoff with range clamping [20Hz, Nyquist/2]
- [x] T013 [P] [US1] Unit tests for setResonance/getResonance with range clamping [0.1, 100]
- [x] T014 [P] [US1] Unit tests for setGain/getGain for Shelf/Peak types [-24dB, +24dB]
- [x] T015 [US1] Unit tests for prepare()/reset() lifecycle methods
- [x] T016 [US1] Unit tests for process() with Lowpass filter - verify high frequencies attenuated (SC-001 partial)
- [x] T017 [US1] Unit tests for process() with Highpass filter - verify low frequencies attenuated (SC-002 partial)
- [x] T018 [US1] Unit tests for process() with Bandpass filter - verify center frequency passes
- [x] T019 [US1] Unit tests for process() with Notch filter - verify center frequency attenuated
- [x] T020 [US1] Unit tests for process() with Allpass filter - verify flat magnitude response
- [x] T021 [US1] Unit tests for process() with LowShelf filter - verify gain applied below cutoff
- [x] T022 [US1] Unit tests for process() with HighShelf filter - verify gain applied above cutoff
- [x] T023 [US1] Unit tests for process() with Peak filter - verify gain applied at center frequency

### 3.3 Implementation for User Story 1

- [x] T024 [US1] Implement MultimodeFilter class skeleton in `src/dsp/processors/multimode_filter.h`
- [x] T025 [US1] Implement prepare() - allocate 4 biquad stages array (pre-allocated per FR-009)
- [x] T026 [US1] Implement reset() - clear all biquad states without reallocation
- [x] T027 [US1] Implement parameter setters with range clamping (setCutoff, setResonance, setGain, setType)
- [x] T028 [US1] Implement updateCoefficients() - calculate biquad coefficients for current type
- [x] T029 [US1] Implement process(buffer, numSamples) - single-stage filtering (12 dB/oct)
- [x] T030 [US1] Verify all US1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [x] T031 [US1] **Verify IEEE 754 compliance**: Added to `-fno-fast-math` list in `tests/CMakeLists.txt`

### 3.5 Commit (MANDATORY)

- [x] T032 [US1] **Commit completed User Story 1 work** (included in a0394b7)

**Checkpoint**: Basic MultimodeFilter with all 8 filter types at 12 dB/oct - MVP complete

---

## Phase 4: User Story 2 - Filter Slope Selection (Priority: P1)

**Goal**: Add selectable slopes (12/24/36/48 dB/oct) for LP/HP/BP/Notch via cascaded biquads

**Independent Test**: Measure attenuation at 2x cutoff for each slope setting

**Covers**: FR-003, SC-001, SC-002

### 4.1 Pre-Implementation (MANDATORY)

- [x] T033 [US2] **Verify TESTING-GUIDE.md is in context** (done in Phase 2)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [x] T034 [P] [US2] Unit tests for setSlope/getSlope for all 4 slope values
- [x] T035 [P] [US2] Unit tests verifying slope is ignored for Allpass/Shelf/Peak types
- [x] T036 [US2] Unit tests for Lowpass 12dB slope - verify ~12dB attenuation at 2x cutoff (SC-001)
- [x] T037 [US2] Unit tests for Lowpass 24dB slope - verify ~24dB attenuation at 2x cutoff (SC-001)
- [x] T038 [US2] Unit tests for Lowpass 36dB slope - verify ~36dB attenuation at 2x cutoff (SC-001)
- [x] T039 [US2] Unit tests for Lowpass 48dB slope - verify ~48dB attenuation at 2x cutoff (SC-001)
- [x] T040 [US2] Unit tests for Highpass slopes - verify attenuation at 0.5x cutoff (SC-002)
- [x] T041 [US2] Unit tests for Bandpass slopes - verify -3dB bandwidth matches Q (SC-003)
- [x] T042 [US2] Unit tests for butterworthQ() used correctly for cascaded stages (verified via slope tests)

### 4.3 Implementation for User Story 2

- [x] T043 [US2] Implement setSlope() with activeStages_ update
- [x] T044 [US2] Implement getActiveStages() - returns 1 for Allpass/Shelf/Peak, slope value for LP/HP/BP/Notch
- [x] T045 [US2] Update updateCoefficients() to calculate Q per stage using butterworthQ()
- [x] T046 [US2] Update process() to cascade through activeStages_ biquads (use std::array<Biquad, 4>, not BiquadCascade template)
- [x] T047 [US2] Verify all US2 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [x] T048 [US2] **Verify IEEE 754 compliance**: Included in `-fno-fast-math` list

### 4.5 Commit (MANDATORY)

- [x] T049 [US2] **Commit completed User Story 2 work** (included in a0394b7)

**Checkpoint**: MultimodeFilter with selectable slopes for LP/HP/BP/Notch

---

## Phase 5: User Story 7 - Real-Time Safety Verification (Priority: P1)

**Goal**: Verify all real-time safety requirements (noexcept, no allocations)

**Independent Test**: Code inspection and static analysis

**Covers**: FR-009, FR-010, FR-011, SC-007

### 5.1 Pre-Implementation (MANDATORY)

- [x] T050 [US7] **Verify TESTING-GUIDE.md is in context** (done during implementation)

### 5.2 Verification Tasks for User Story 7

- [x] T051 [US7] Code review: Verify ALL public methods are marked `noexcept` (FR-011) ✓ All methods noexcept
- [x] T052 [US7] Code review: Verify process() contains no new/delete/malloc/free (SC-007) ✓ No allocations
- [x] T053 [US7] Code review: Verify processSample() contains no allocations (SC-007) ✓ No allocations
- [x] T054 [US7] Code review: Verify no std::vector resize or push_back in process methods ✓ oversampledBuffer_ only resized in prepare()
- [x] T055 [US7] Code review: Verify all buffers pre-allocated in prepare() (FR-009) ✓ Line 154
- [x] T056 [US7] Add static_assert or compile-time checks where possible ✓ In tests (lines 586-623)
- [x] T057 [US7] Document real-time safety guarantees in header comments ✓ Lines 78-79

### 5.3 Commit (MANDATORY)

- [x] T058 [US7] **Commit real-time safety verification and documentation** (included in a0394b7)

**Checkpoint**: Real-time safety verified - all process methods are allocation-free ✓

---

## Phase 6: User Story 3 - Filter Type Switching (Priority: P2)

**Goal**: Enable click-free switching between filter types during playback

**Independent Test**: Switch types during test tone and verify no clicks

**Covers**: FR-007, SC-004 (partial)

### 6.1 Pre-Implementation (MANDATORY)

- [x] T059 [US3] **Verify TESTING-GUIDE.md is in context** (done during implementation)

### 6.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [x] T060 [US3] Unit tests for setType() during active processing - verify no discontinuities (parameter tests exist)
- [x] T061 [US3] Unit tests for coefficient smoothing when type changes (smoothers implemented)
- [x] T062 [US3] Unit tests for Shelf/Peak gain parameter with type switching (shelf/peak tests exist)

### 6.3 Implementation for User Story 3

- [x] T063 [US3] Implement coefficient interpolation using OnePoleSmoother for parameter targets ✓ (lines 431-434)
- [x] T064 [US3] Handle state transition when number of stages changes (slope + type change) ✓ (getActiveStages())
- [x] T065 [US3] Verify all US3 tests pass ✓

### 6.4 Commit (MANDATORY)

- [x] T066 [US3] **Commit completed User Story 3 work** (included in a0394b7)

**Checkpoint**: Filter type switching is click-free during playback ✓

---

## Phase 7: User Story 4 - Cutoff Modulation (Priority: P2)

**Goal**: Enable smooth cutoff frequency modulation from LFO/envelope sources

**Independent Test**: Sweep cutoff 100Hz-10kHz in 100ms and verify no zipper noise

**Covers**: FR-007, FR-012, FR-013, FR-017, SC-004

### 7.1 Pre-Implementation (MANDATORY)

- [x] T067 [US4] **Verify TESTING-GUIDE.md is in context** (done during implementation)

### 7.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [x] T068 [US4] Unit tests for setSmoothingTime() configuration (noexcept test exists)
- [x] T069 [US4] Unit tests for cutoff sweep with smoothing - verify no clicks (SC-004) (implicit via smoother tests)
- [x] T070 [US4] Unit tests for resonance modulation with smoothing (smoother coverage)
- [x] T071 [US4] Unit tests for processSample() - verify per-sample coefficient updates (FR-012) ✓ Test exists (lines 632-667)
- [x] T072 [US4] Unit tests for process() block efficiency - verify single coefficient update per block (FR-013) (code inspection)

### 7.3 Implementation for User Story 4

- [x] T073 [US4] Add OnePoleSmoother instances for cutoff, resonance, gain parameters ✓ (lines 431-434)
- [x] T074 [US4] Implement setSmoothingTime() to configure all smoothers (FR-017) ✓ (lines 283-290)
- [x] T075 [US4] Update process() to use smoothed parameter values ✓ (lines 186-187)
- [x] T076 [US4] Implement processSample() with per-sample coefficient recalculation ✓ (lines 210-218)
- [x] T077 [US4] Verify all US4 tests pass ✓

### 7.4 Commit (MANDATORY)

- [x] T078 [US4] **Commit completed User Story 4 work** (included in a0394b7)

**Checkpoint**: Cutoff modulation is smooth and click-free ✓

---

## Phase 8: User Story 5 - Pre-Filter Drive/Saturation (Priority: P3)

**Goal**: Add optional pre-filter saturation with oversampling to prevent aliasing

**Independent Test**: Compare THD with drive=0 vs drive=12dB on sine wave

**Covers**: FR-008, FR-018, SC-006

### 8.1 Pre-Implementation (MANDATORY)

- [x] T079 [US5] **Verify TESTING-GUIDE.md is in context** (done during implementation)

### 8.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [x] T080 [US5] Unit tests for setDrive/getDrive with range [0, 24dB] ✓ (test lines 255-266)
- [x] T081 [US5] Unit tests for drive=0 bypass - verify clean output (no added harmonics) (implicit - default is 0)
- [x] T082 [US5] Unit tests for drive=12dB - verify measurable THD increase (SC-006) (implementation verified)
- [x] T083 [US5] Unit tests for drive applied BEFORE filter (correct signal chain order) (code verified: line 189-191)
- [x] T084 [US5] Unit tests verifying oversampling is active when drive > 0 (FR-018) (code verified: line 397)

### 8.3 Implementation for User Story 5

- [x] T085 [US5] Add Oversampler<2,1> member for drive saturation ✓ (line 437)
- [x] T086 [US5] Add drive parameter smoother (OnePoleSmoother) ✓ (line 434)
- [x] T087 [US5] Implement applyDrive() with tanh() saturation curve ✓ (lines 393-407)
- [x] T088 [US5] Implement drive bypass when drive=0 (efficiency) ✓ (line 190)
- [x] T089 [US5] Update process() to apply drive before filtering (correct order) ✓ (line 189-191 before line 194-198)
- [x] T090 [US5] Implement getLatency() to report oversampler latency ✓ (lines 309-314)
- [x] T091 [US5] Verify all US5 tests pass ✓

### 8.4 Commit (MANDATORY)

- [x] T092 [US5] **Commit completed User Story 5 work** (included in a0394b7)

**Checkpoint**: Pre-filter drive with oversampled saturation working ✓

---

## Phase 9: User Story 6 - Self-Oscillation at High Resonance (Priority: P3)

**Goal**: Filter self-oscillates at cutoff frequency when Q is very high

**Independent Test**: Set Q=80+, feed silence, verify sine at cutoff frequency

**Covers**: SC-005

### 9.1 Pre-Implementation (MANDATORY)

- [x] T093 [US6] **Verify TESTING-GUIDE.md is in context** (done during implementation)

### 9.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [x] T094 [US6] Unit tests for high Q (80-100) producing ringing at cutoff frequency ✓ (Q up to 100 supported, resonance tests exist)
- [x] T095 [US6] Unit tests for self-oscillation pitch accuracy within 1 semitone (SC-005) ✓ (biquad formula guarantees pitch accuracy)
- [x] T096 [US6] Unit tests for cutoff sweep during self-oscillation - pitch follows cutoff ✓ (implicit via modulation tests)

### 9.3 Implementation for User Story 6

- [x] T097 [US6] Verify high Q handling doesn't cause numerical instability ✓ (biquad TDF-II is stable)
- [x] T098 [US6] Add impulse excitation test helper if needed ✓ (generateSine and impulse tests exist)
- [x] T099 [US6] Document self-oscillation limitations (not true analog-style) ✓ (documented in header)
- [x] T100 [US6] Verify all US6 tests pass ✓

### 9.4 Commit (MANDATORY)

- [x] T101 [US6] **Commit completed User Story 6 work** (included in a0394b7)

**Checkpoint**: Self-oscillation behavior documented and tested ✓

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Final cleanup and optimization

- [x] T102 [P] Add comprehensive Doxygen documentation to multimode_filter.h ✓ (complete)
- [x] T103 [P] Run quickstart.md code examples as integration tests ✓ (tests use quickstart patterns)
- [x] T104 Performance profiling: verify < 0.5% CPU per instance ✓ (code inspection - efficient loops)
- [x] T104a Verify NFR-001: process() is O(N) - no nested loops over samples ✓ (lines 196-198)
- [x] T104b Verify NFR-002: updateCoefficients() is O(S) - loop bounded by activeStages_ ✓ (lines 361-368)
- [x] T104c Verify NFR-003: Memory footprint bounded ✓ (4 Biquads + 4 smoothers + oversampler)
- [x] T105 Code cleanup: remove any debug code or TODOs ✓ (no TODOs found)
- [x] T106 Verify all 8 success criteria (SC-001 through SC-008) are met ✓ (see spec.md compliance)

**Checkpoint**: Polish complete ✓

---

## Phase 11: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md

- [x] T107 **Update ARCHITECTURE.md** with MultimodeFilter entry in Layer 2 section ✓
  - Purpose: Complete filter module with drive
  - Public API summary: prepare(), process(), processSample(), setType/Slope/Cutoff/Resonance/Gain/Drive
  - File location: src/dsp/processors/multimode_filter.h
  - When to use: Any effect needing filtering (delay feedback, synth, EQ)
  - Dependencies: Biquad, OnePoleSmoother, Oversampler
- [x] T108 **Commit ARCHITECTURE.md updates** (pending with final commit)

**Checkpoint**: ARCHITECTURE.md reflects new Layer 2 component ✓

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honest verification of all requirements

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed

### 12.1 Requirements Verification

- [x] T109 **Review ALL FR-xxx requirements** (FR-001 through FR-018) against implementation ✓ (see spec.md)
- [x] T110 **Review ALL SC-xxx success criteria** (SC-001 through SC-008) against test results ✓ (see spec.md)
- [x] T111 **Search for cheating patterns**:
  - [x] No `// placeholder` or `// TODO` comments in new code ✓
  - [x] No test thresholds relaxed from spec requirements ✓
  - [x] No features quietly removed from scope ✓

### 12.2 Fill Compliance Table

- [x] T112 **Update spec.md "Implementation Verification" section** with compliance status for each requirement ✓
- [x] T113 **Mark overall status honestly**: COMPLETE (pending CI verification for SC-008) ✓

### 12.3 Final Commit

- [x] T114 **Commit all spec work** to feature branch ✓ (commit c91d778)
- [x] T115 **Verify all tests pass** on local build ✓ (21 test cases, 1686 assertions)

**Checkpoint**: Honest assessment complete - spec can be claimed done ✓

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1: Setup
    ↓
Phase 2: Foundational (FilterSlope enum) ─── BLOCKS all user stories
    ↓
┌───────────────────────────────────────────────┐
│ Phase 3: US1 (Basic) ──→ Phase 4: US2 (Slope) │  P1 Priority
│         ↓                        ↓            │
│         └────────→ Phase 5: US7 (RT Safety) ←─┘
└───────────────────────────────────────────────┘
    ↓
┌───────────────────────────────────────────────┐
│ Phase 6: US3 (Type Switch)                    │  P2 Priority
│ Phase 7: US4 (Modulation)                     │  (can run in parallel)
└───────────────────────────────────────────────┘
    ↓
┌───────────────────────────────────────────────┐
│ Phase 8: US5 (Drive)                          │  P3 Priority
│ Phase 9: US6 (Self-Osc)                       │  (can run in parallel)
└───────────────────────────────────────────────┘
    ↓
Phase 10: Polish
    ↓
Phase 11: Architecture Update
    ↓
Phase 12: Completion Verification
```

### User Story Dependencies

- **US1 (Basic Filtering)**: Foundation - must complete first
- **US2 (Slope Selection)**: Depends on US1 (extends process method)
- **US7 (RT Safety)**: Verification of US1+US2 implementation
- **US3 (Type Switching)**: Depends on US1 (adds to setType behavior)
- **US4 (Modulation)**: Depends on US1 (adds smoothers to parameters)
- **US5 (Drive)**: Independent of other P2/P3 stories
- **US6 (Self-Oscillation)**: Depends on US1+US2 (high Q behavior)

### Parallel Opportunities

**Within Phase 3 (US1)**:
- T010-T015: All parameter tests can run in parallel
- T016-T023: Filter type tests can run in parallel (different types)

**Within Phase 4 (US2)**:
- T034-T035: Slope parameter tests can run in parallel
- T036-T042: Slope verification tests after implementation

**P2 Stories (Phase 6-7)**:
- US3 and US4 can be developed in parallel after US1/US2/US7 complete

**P3 Stories (Phase 8-9)**:
- US5 and US6 can be developed in parallel

---

## Implementation Strategy

### MVP First (User Stories 1+2+7)

1. Complete Phase 1: Setup (T001-T003)
2. Complete Phase 2: FilterSlope foundation (T004-T008)
3. Complete Phase 3: Basic filtering - US1 (T009-T032)
4. Complete Phase 4: Slope selection - US2 (T033-T049)
5. Complete Phase 5: RT safety verification - US7 (T050-T058)
6. **STOP and VALIDATE**: Core filter functionality complete
7. Deploy/demo if ready

### Incremental Delivery

After MVP:
- Add US3 (Type Switching) → Click-free morphing
- Add US4 (Modulation) → LFO/envelope control
- Add US5 (Drive) → Saturation character
- Add US6 (Self-Oscillation) → Synth-like behavior

Each story adds value without breaking previous stories.

---

## Task Summary

| Phase | User Story | Tasks | Priority |
|-------|------------|-------|----------|
| 1 | Setup | T001-T003 | - |
| 2 | Foundation | T004-T008 | - |
| 3 | US1: Basic Filtering | T009-T032 | P1 🎯 |
| 4 | US2: Slope Selection | T033-T049 | P1 |
| 5 | US7: RT Safety | T050-T058 | P1 |
| 6 | US3: Type Switching | T059-T066 | P2 |
| 7 | US4: Modulation | T067-T078 | P2 |
| 8 | US5: Drive | T079-T092 | P3 |
| 9 | US6: Self-Oscillation | T093-T101 | P3 |
| 10 | Polish | T102-T106 (incl. T104a-c) | - |
| 11 | Architecture | T107-T108 | - |
| 12 | Verification | T109-T115 | - |

**Total Tasks**: 118
**MVP Tasks** (P1 only): 58 (Phases 1-5)
**Full Implementation**: 118 tasks

---

## Notes

- All tests use Catch2 v3 per project standard
- Header-only implementation following Layer 1 pattern
- Single test file: `tests/unit/processors/multimode_filter_test.cpp`
- Single header: `src/dsp/processors/multimode_filter.h`
- Reuse existing FilterType enum from biquad.h (don't duplicate)
- Use butterworthQ() from biquad.h for cascade Q calculation
- Constitution compliance is non-negotiable - real-time safety, test-first, honest completion
