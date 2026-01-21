# Tasks: Formant Filter

**Input**: Design documents from `/specs/077-formant-filter/`
**Prerequisites**: plan.md (complete), spec.md (complete), research.md (complete), data-model.md (complete), contracts/ (complete)

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
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             processors/formant_filter_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [ ] T001 Create directory structure for FormantFilter at dsp/include/krate/dsp/processors/formant_filter.h and dsp/tests/processors/formant_filter_test.cpp
- [ ] T002 Verify dependencies exist: Biquad (dsp/include/krate/dsp/primitives/biquad.h), OnePoleSmoother (dsp/include/krate/dsp/primitives/smoother.h), FormantData/Vowel (dsp/include/krate/dsp/core/filter_tables.h)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [ ] T003 Create FormantFilter header skeleton at dsp/include/krate/dsp/processors/formant_filter.h with class declaration, includes, and namespace structure
- [ ] T004 Create test file skeleton at dsp/tests/processors/formant_filter_test.cpp with Catch2 includes and test section structure
- [ ] T005 Add formant_filter_test.cpp to dsp/tests/CMakeLists.txt in dsp_tests target

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Discrete Vowel Selection for Vocal Effects (Priority: P1) 🎯 MVP

**Goal**: Enable discrete vowel selection (A, E, I, O, U) to apply recognizable vocal formant characteristics to non-vocal audio sources

**Independent Test**: Process white noise through FormantFilter with each vowel setting (A, E, I, O, U) and verify output spectrum shows peaks at expected formant frequencies from filter_tables.h

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T006 [P] [US1] Write failing unit test for FormantFilter::prepare() in dsp/tests/processors/formant_filter_test.cpp - verify prepared_ flag, sample rate storage
- [ ] T007 [P] [US1] Write failing unit test for FormantFilter::setVowel() in dsp/tests/processors/formant_filter_test.cpp - verify vowel selection for all 5 vowels
- [ ] T008 [P] [US1] Write failing unit test for FormantFilter::reset() in dsp/tests/processors/formant_filter_test.cpp - verify filter states cleared
- [ ] T009 [US1] Write failing spectral test for vowel A in dsp/tests/processors/formant_filter_test.cpp - process white noise, verify peaks at 600Hz (F1), 1040Hz (F2), 2250Hz (F3) within +/-10%
- [ ] T010 [P] [US1] Write failing spectral test for vowel E in dsp/tests/processors/formant_filter_test.cpp - verify peaks at 400Hz, 1620Hz, 2400Hz
- [ ] T011 [P] [US1] Write failing spectral test for vowel I in dsp/tests/processors/formant_filter_test.cpp - verify peaks at 250Hz, 1750Hz, 2600Hz
- [ ] T012 [P] [US1] Write failing spectral test for vowel O in dsp/tests/processors/formant_filter_test.cpp - verify peaks at 400Hz, 750Hz, 2400Hz
- [ ] T013 [P] [US1] Write failing spectral test for vowel U in dsp/tests/processors/formant_filter_test.cpp - verify peaks at 350Hz, 600Hz, 2400Hz
- [ ] T014 [US1] Build dsp_tests target - confirm all tests FAIL (no implementation yet)

### 3.2 Implementation for User Story 1

- [ ] T015 [P] [US1] Implement FormantFilter member variables in dsp/include/krate/dsp/processors/formant_filter.h - add std::array<Biquad, 3> formants_, std::array<OnePoleSmoother, 3> freqSmoothers_, bwSmoothers_, parameters, state
- [ ] T016 [P] [US1] Implement FormantFilter::prepare() in dsp/include/krate/dsp/processors/formant_filter.h - configure all smoothers for sample rate, reset filters, set prepared_ flag
- [ ] T017 [P] [US1] Implement FormantFilter::reset() in dsp/include/krate/dsp/processors/formant_filter.h - call reset() on all formant filters
- [ ] T018 [US1] Implement FormantFilter::setVowel() in dsp/include/krate/dsp/processors/formant_filter.h - set currentVowel_, useMorphMode_ = false, call calculateTargetFormants()
- [ ] T019 [US1] Implement FormantFilter::calculateTargetFormants() (discrete mode) in dsp/include/krate/dsp/processors/formant_filter.h - read kVowelFormants, apply shift/gender, set smoother targets
- [ ] T020 [US1] Implement FormantFilter::clampFrequency() helper in dsp/include/krate/dsp/processors/formant_filter.h - clamp to [20.0f, 0.45f * sampleRate] (private implementation detail)
- [ ] T021 [US1] Implement FormantFilter::calculateQ() helper in dsp/include/krate/dsp/processors/formant_filter.h - Q = frequency / bandwidth, clamp to [0.5f, 20.0f] (private implementation detail, can be static)
- [ ] T022 [US1] Implement FormantFilter::updateFilterCoefficients() in dsp/include/krate/dsp/processors/formant_filter.h - process smoothers, configure biquads as Bandpass
- [ ] T023 [US1] Implement FormantFilter::process() in dsp/include/krate/dsp/processors/formant_filter.h - update coefficients, process input through all 3 formants, sum outputs
- [ ] T024 [US1] Implement FormantFilter::processBlock() in dsp/include/krate/dsp/processors/formant_filter.h - loop calling process() for each sample
- [ ] T025 [P] [US1] Implement getters in dsp/include/krate/dsp/processors/formant_filter.h - getVowel(), getFormantShift(), getGender(), getSmoothingTime(), isInMorphMode(), isPrepared()
- [ ] T026 [US1] Build dsp_tests target with Release config - fix all compiler warnings (no C4244, C4267, C4100)
- [ ] T026b [US1] Write static_assert test for FR-014 noexcept guarantees in dsp/tests/processors/formant_filter_test.cpp - verify `static_assert(noexcept(std::declval<FormantFilter>().process(0.0f)))` and similar for processBlock
- [ ] T027 [US1] Run formant_filter_test - verify all User Story 1 tests PASS (SC-001: all 5 vowels show correct formant peaks within +/-10%)

### 3.3 Cross-Platform Verification (MANDATORY)

- [ ] T028 [US1] **Verify IEEE 754 compliance**: Check if formant_filter_test.cpp uses `std::isnan`/`std::isfinite`/`std::isinf` - if so, add to `-fno-fast-math` list in dsp/tests/CMakeLists.txt (apply pattern from project constitution)

### 3.4 Commit (MANDATORY)

- [ ] T029 [US1] **Commit completed User Story 1 work** with message: "feat(dsp): implement discrete vowel selection for FormantFilter (US1)"

**Checkpoint**: User Story 1 should be fully functional, tested, and committed - discrete vowel filtering working for A, E, I, O, U

---

## Phase 4: User Story 2 - Smooth Vowel Morphing for Animated Effects (Priority: P1)

**Goal**: Enable continuous vowel morphing (0.0-4.0 position) to create animated "talking wah" effects with smooth, click-free transitions

**Independent Test**: Sweep vowel morph position from 0.0 to 4.0 over 100ms while processing audio and verify smooth transitions without clicks or discontinuities

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T030 [P] [US2] Write failing unit test for FormantFilter::setVowelMorph() in dsp/tests/processors/formant_filter_test.cpp - verify morph position clamping [0, 4], useMorphMode_ flag
- [ ] T031 [US2] Write failing interpolation test for vowel morph position 0.5 (A-E) in dsp/tests/processors/formant_filter_test.cpp - verify F1 is interpolated between 600Hz and 400Hz (expected ~500Hz)
- [ ] T032 [P] [US2] Write failing interpolation test for vowel morph position 1.5 (E-I) in dsp/tests/processors/formant_filter_test.cpp - verify interpolated frequencies
- [ ] T033 [P] [US2] Write failing interpolation test for vowel morph position 2.5 (I-O) in dsp/tests/processors/formant_filter_test.cpp - verify interpolated frequencies
- [ ] T034 [US2] Write failing smoothness test for continuous morph sweep in dsp/tests/processors/formant_filter_test.cpp - sweep position 0.0->4.0 over 50ms processing pink noise, verify transient peaks < -60dB (SC-006)
- [ ] T035 [US2] Build dsp_tests target - confirm User Story 2 tests FAIL (morph not implemented yet)

### 4.2 Implementation for User Story 2

- [ ] T036 [US2] Implement FormantFilter::setVowelMorph() in dsp/include/krate/dsp/processors/formant_filter.h - clamp position to [0, 4], set vowelMorphPosition_, useMorphMode_ = true, call calculateTargetFormants()
- [ ] T037 [US2] Extend FormantFilter::calculateTargetFormants() (morph mode) in dsp/include/krate/dsp/processors/formant_filter.h - compute lowerIdx/upperIdx/fraction, use std::lerp for F1/F2/F3/BW1/BW2/BW3, apply shift/gender
- [ ] T038 [US2] Implement FormantFilter::getVowelMorph() getter in dsp/include/krate/dsp/processors/formant_filter.h - return vowelMorphPosition_
- [ ] T039 [US2] Build dsp_tests target with Release config - fix all compiler warnings
- [ ] T040 [US2] Run formant_filter_test - verify all User Story 2 tests PASS (SC-002: vowel morphing produces linearly interpolated formant frequencies; SC-006: transient peaks < -60dB when sweeping morph position)

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T041 [US2] **Verify IEEE 754 compliance**: Confirm formant_filter_test.cpp `-fno-fast-math` status (already added in US1 or add now if needed)

### 4.4 Commit (MANDATORY)

- [ ] T042 [US2] **Commit completed User Story 2 work** with message: "feat(dsp): implement smooth vowel morphing for FormantFilter (US2)"

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed - discrete vowels + smooth morphing

---

## Phase 5: User Story 3 - Formant Shift for Pitch-Independent Character (Priority: P2)

**Goal**: Enable formant frequency shifting (+/-24 semitones) to change perceived "size" or character without changing pitch - supports cartoon voices and pitch-shift compensation

**Independent Test**: Apply +12 semitone formant shift to vowel A and verify all formant frequencies are multiplied by 2^(12/12) = 2.0 (within 1% tolerance)

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T043 [P] [US3] Write failing unit test for FormantFilter::setFormantShift() in dsp/tests/processors/formant_filter_test.cpp - verify shift clamping [-24, +24], parameter storage
- [ ] T044 [US3] Write failing spectral test for +12 semitone shift on vowel A in dsp/tests/processors/formant_filter_test.cpp - verify F1 doubles to ~1200Hz, F2 to ~2080Hz (within 1%)
- [ ] T045 [P] [US3] Write failing spectral test for -12 semitone shift on vowel A in dsp/tests/processors/formant_filter_test.cpp - verify formants halved (F1 ~300Hz, F2 ~520Hz)
- [ ] T046 [P] [US3] Write failing smoothness test for shift sweep -24 to +24 in dsp/tests/processors/formant_filter_test.cpp - sweep over 100ms, verify transient peaks < -60dB (SC-007)
- [ ] T047 [US3] Write failing frequency clamping test in dsp/tests/processors/formant_filter_test.cpp - extreme shift (+24 semitones at 192kHz), verify formants stay within [20Hz, 0.45*192000Hz]
- [ ] T048 [US3] Build dsp_tests target - confirm User Story 3 tests FAIL (shift not applied yet)

### 5.2 Implementation for User Story 3

- [ ] T049 [US3] Implement FormantFilter::setFormantShift() in dsp/include/krate/dsp/processors/formant_filter.h - clamp semitones to [-24, +24], store in formantShift_, call calculateTargetFormants()
- [ ] T050 [US3] Extend FormantFilter::calculateTargetFormants() (shift logic) in dsp/include/krate/dsp/processors/formant_filter.h - compute shiftMultiplier = pow(2, semitones/12), apply to base frequencies/bandwidths before clamping
- [ ] T051 [US3] Build dsp_tests target with Release config - fix all compiler warnings
- [ ] T052 [US3] Run formant_filter_test - verify all User Story 3 tests PASS (SC-003: +12 semitones doubles frequencies within 1%; SC-007: transient peaks < -60dB during shift sweep; SC-012: frequencies stay in valid range)

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T053 [US3] **Verify IEEE 754 compliance**: Confirm formant_filter_test.cpp `-fno-fast-math` status (should already be set from US1)

### 5.4 Commit (MANDATORY)

- [ ] T054 [US3] **Commit completed User Story 3 work** with message: "feat(dsp): implement formant shift parameter for FormantFilter (US3)"

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed - discrete vowels + morphing + formant shift

---

## Phase 6: User Story 4 - Gender Parameter for Male/Female Character (Priority: P2)

**Goal**: Provide intuitive "gender" parameter (-1 male, +1 female) to quickly adjust perceived gender of vocal/vocal-like sounds with appropriate formant scaling

**Independent Test**: Set gender to +1.0 (female) and verify formants shift up approximately 19% (multiplier ~1.189), set gender to -1.0 (male) and verify formants shift down approximately 17% (multiplier ~0.841)

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T055 [P] [US4] Write failing unit test for FormantFilter::setGender() in dsp/tests/processors/formant_filter_test.cpp - verify gender clamping [-1, +1], parameter storage
- [ ] T056 [US4] Write failing spectral test for gender +1.0 (female) on vowel A in dsp/tests/processors/formant_filter_test.cpp - verify formants scaled up by 1.17-1.21x (SC-004: approximately 19% increase)
- [ ] T057 [P] [US4] Write failing spectral test for gender -1.0 (male) on vowel A in dsp/tests/processors/formant_filter_test.cpp - verify formants scaled down by 0.82-0.86x (SC-005: approximately 16% decrease)
- [ ] T058 [P] [US4] Write failing spectral test for gender 0.0 (neutral) on vowel A in dsp/tests/processors/formant_filter_test.cpp - verify formants match original vowel table values exactly
- [ ] T059 [US4] Write failing combination test for shift + gender in dsp/tests/processors/formant_filter_test.cpp - set shift +6 semitones and gender +0.5, verify multiplicative combination (shift first, then gender)
- [ ] T060 [US4] Build dsp_tests target - confirm User Story 4 tests FAIL (gender not applied yet)

### 6.2 Implementation for User Story 4

- [ ] T061 [US4] Implement FormantFilter::setGender() in dsp/include/krate/dsp/processors/formant_filter.h - clamp amount to [-1, +1], store in gender_, call calculateTargetFormants()
- [ ] T062 [US4] Extend FormantFilter::calculateTargetFormants() (gender logic) in dsp/include/krate/dsp/processors/formant_filter.h - compute genderMultiplier = pow(2, gender * 0.25), apply AFTER shift multiplier to frequencies/bandwidths
- [ ] T063 [US4] Implement FormantFilter::getGender() getter in dsp/include/krate/dsp/processors/formant_filter.h - return gender_
- [ ] T064 [US4] Build dsp_tests target with Release config - fix all compiler warnings
- [ ] T065 [US4] Run formant_filter_test - verify all User Story 4 tests PASS (SC-004: +1.0 scales by 1.17-1.21x; SC-005: -1.0 scales by 0.82-0.86x; multiplicative combination correct)

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T066 [US4] **Verify IEEE 754 compliance**: Confirm formant_filter_test.cpp `-fno-fast-math` status (should already be set from US1)

### 6.4 Commit (MANDATORY)

- [ ] T067 [US4] **Commit completed User Story 4 work** with message: "feat(dsp): implement gender parameter for FormantFilter (US4)"

**Checkpoint**: All user stories (US1-US4) should now be independently functional and committed - complete FormantFilter feature set

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories and final verification

### 7.1 Smoothing Configuration

- [ ] T068 [P] Implement FormantFilter::setSmoothingTime() in dsp/include/krate/dsp/processors/formant_filter.h - clamp ms to reasonable range [0.1, 1000], store in smoothingTime_, reconfigure all smoothers if prepared
- [ ] T069 [P] Write test for smoothing time adjustment in dsp/tests/processors/formant_filter_test.cpp - verify parameter reaches 99% of target within 5 * smoothingTime (SC-008)
- [ ] T070 Run dsp_tests and verify smoothing tests pass

### 7.2 Stability and Edge Cases

- [ ] T071 [P] Write stability test in dsp/tests/processors/formant_filter_test.cpp - verify no NaN/infinity for all valid parameter combinations at 44.1kHz, 48kHz, 96kHz, 192kHz (SC-009, SC-010)
- [ ] T072 [P] Write edge case test for DC input in dsp/tests/processors/formant_filter_test.cpp - verify DC is attenuated by bandpass filters
- [ ] T073 [P] Write edge case test for parameter clamping in dsp/tests/processors/formant_filter_test.cpp - verify out-of-range values are clamped correctly
- [ ] T074 Run dsp_tests and verify all stability/edge case tests pass

### 7.3 Performance Verification

- [ ] T075 Write performance benchmark in dsp/tests/processors/formant_filter_test.cpp - measure CPU time per sample in Release build at 44.1kHz, verify < 50ns on reference hardware (SC-011, simpler than crossover filter). Document benchmark conditions: Release config, 1M samples, exclude first 1K for warmup.
- [ ] T076 Run benchmark on reference hardware - document results

### 7.4 Build Verification

- [ ] T077 Clean build of dsp_tests target in Release config - verify ZERO warnings
- [ ] T078 Run complete dsp_tests suite - verify ALL tests PASS on Windows (MSVC)
- [ ] T079 Build and test on macOS (Clang) if available - verify cross-platform correctness (SC-010)
- [ ] T080 Build and test on Linux (GCC) if available - verify cross-platform correctness (SC-010)

### 7.5 Quickstart Validation

- [ ] T081 Walk through quickstart.md examples manually - verify all code snippets compile and work as documented

### 7.6 Final Commit

- [ ] T082 **Commit all polish work** with message: "polish(dsp): finalize FormantFilter with smoothing, stability tests, and performance verification"

**Checkpoint**: All polish tasks complete, all tests passing, ready for documentation update

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 8.1 Architecture Documentation Update

- [ ] T083 **Update `specs/_architecture_/layer-2-processors.md`** with FormantFilter entry:
  - Add FormantFilter to Filters section
  - Include: purpose (vocal formant filtering with 3 parallel bandpass), file location (dsp/include/krate/dsp/processors/formant_filter.h)
  - Document public API: prepare(), setVowel(), setVowelMorph(), setFormantShift(), setGender(), setSmoothingTime(), process(), processBlock(), reset()
  - Add "when to use this" guidance: vocal effects, talking wah, character adjustment
  - Add usage example: basic vowel selection and morphing snippet
  - Verify no duplicate functionality was introduced (FormantFilter is unique)

### 8.2 Final Commit

- [ ] T084 **Commit architecture documentation updates** with message: "docs(arch): add FormantFilter to Layer 2 processors documentation"
- [ ] T085 Verify all spec work is committed to feature branch 077-formant-filter

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T086 **Review ALL FR-xxx requirements (FR-001 through FR-019)** from specs/077-formant-filter/spec.md against implementation - check each requirement individually. Note: FR-019 documents single-threaded assumption, no internal synchronization required.
- [ ] T087 **Review ALL SC-xxx success criteria (SC-001 through SC-012)** and verify measurable targets are achieved - run tests and document actual measured values
- [ ] T088 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in dsp/include/krate/dsp/processors/formant_filter.h
  - [ ] No test thresholds relaxed from spec requirements in dsp/tests/processors/formant_filter_test.cpp
  - [ ] No features quietly removed from scope (all 4 user stories implemented)

### 9.2 Fill Compliance Table in spec.md

- [ ] T089 **Update specs/077-formant-filter/spec.md "Implementation Verification" section** - fill compliance table with status (MET/NOT MET/PARTIAL/DEFERRED) and evidence for each FR-xxx and SC-xxx requirement
- [ ] T090 **Mark overall status honestly in spec.md**: COMPLETE / NOT COMPLETE / PARTIAL

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T091 **All self-check questions answered "no"** - or gaps documented honestly in spec.md

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Commit

- [ ] T092 **Commit all spec work** to feature branch 077-formant-filter with message: "feat(dsp): complete FormantFilter implementation (spec 077)"
- [ ] T093 **Verify all tests pass** - run full dsp_tests suite one last time

### 10.2 Completion Claim

- [ ] T094 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phases 3-6)**: All depend on Foundational phase completion
  - User Story 1 (P1): Can start after Foundational - No dependencies on other stories
  - User Story 2 (P1): Can start after Foundational - No dependencies on other stories (US1+US2 together form minimal viable product)
  - User Story 3 (P2): Can start after Foundational - No dependencies on US1/US2 (shift logic is independent)
  - User Story 4 (P2): Can start after Foundational - No dependencies on US1/US2/US3 (gender logic is independent)
- **Polish (Phase 7)**: Depends on all desired user stories being complete (at minimum US1+US2 for MVP)
- **Documentation (Phase 8)**: Depends on Polish completion
- **Verification (Phase 9)**: Depends on Documentation completion
- **Completion (Phase 10)**: Depends on Verification completion

### User Story Dependencies

All user stories are **independently implementable** after Foundational phase:

- **User Story 1 (P1)**: Discrete vowel selection - foundation for all other features
- **User Story 2 (P1)**: Vowel morphing - extends vowel selection with interpolation (MVP = US1 + US2)
- **User Story 3 (P2)**: Formant shift - independent parameter that scales all formants
- **User Story 4 (P2)**: Gender parameter - independent parameter that scales all formants (different formula than shift)

**Recommended Order**: US1 → US2 → US3 → US4 (priority order, though US3/US4 could be swapped or parallelized)

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Implementation to make tests pass
3. Build and fix warnings
4. Verify tests pass
5. **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
6. **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Setup tasks**: T001-T002 can run in parallel
- **Foundational tests**: T006-T008 can run in parallel (all in same file but testing different methods)
- **User Story 1 spectral tests**: T010-T013 can run in parallel (different vowels, same file)
- **User Story 2 interpolation tests**: T031-T033 can run in parallel (different morph positions)
- **User Story 3 spectral tests**: T044-T047 can run in parallel (different shift values)
- **User Story 4 spectral tests**: T056-T059 can run in parallel (different gender values)
- **Implementation tasks with [P] marker**: Can be written in parallel if staffed (different functions/methods)
- **All 4 user stories**: After Foundational phase completes, US1/US2/US3/US4 can all be worked on in parallel by different developers

---

## Parallel Example: User Story 1

```bash
# Launch all spectral tests for User Story 1 together (after unit tests written):
T010: "Write failing spectral test for vowel A"
T011: "Write failing spectral test for vowel E"
T012: "Write failing spectral test for vowel I"
T013: "Write failing spectral test for vowel O"
# (All same file, different TEST_CASE sections)

# Launch multiple implementation tasks together:
T015: "Implement FormantFilter member variables"
T016: "Implement FormantFilter::prepare()"
T017: "Implement FormantFilter::reset()"
# (All same file, different methods - can write in parallel if careful)
```

---

## Implementation Strategy

### MVP First (User Story 1 + 2 Only)

This is the recommended approach for rapid validation:

1. Complete Phase 1: Setup (T001-T002)
2. Complete Phase 2: Foundational (T003-T005)
3. Complete Phase 3: User Story 1 - Discrete Vowel Selection (T006-T029)
4. Complete Phase 4: User Story 2 - Vowel Morphing (T030-T042)
5. **STOP and VALIDATE**: Test discrete vowels + morphing independently
6. **MVP READY**: FormantFilter with 5 discrete vowels and smooth morphing is production-ready
7. Deploy/demo if ready, or continue to User Story 3/4 for extended features

**MVP Scope**: US1 + US2 = Discrete vowels (A, E, I, O, U) + smooth morphing = complete "talking filter" effect

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Commit (discrete vowels working)
3. Add User Story 2 → Test independently → Commit (MVP: vowels + morphing)
4. Add User Story 3 → Test independently → Commit (add formant shift for pitch-independent character)
5. Add User Story 4 → Test independently → Commit (add gender parameter for easy male/female adjustment)
6. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers (if applicable):

1. Team completes Setup + Foundational together (T001-T005)
2. Once Foundational is done:
   - Developer A: User Story 1 (T006-T029)
   - Developer B: User Story 2 (T030-T042) - NOTE: May need to coordinate calculateTargetFormants() extension
   - Developer C: User Story 3 (T043-T054)
   - Developer D: User Story 4 (T055-T067)
3. Stories complete and integrate independently (calculateTargetFormants() becomes more complete as each story merges)

**Note**: For this specific feature, sequential implementation (US1→US2→US3→US4) is recommended due to calculateTargetFormants() being extended by each story. Parallel implementation would require careful coordination of this method.

---

## Task Summary

- **Total Tasks**: 94 tasks
- **Setup Phase**: 2 tasks
- **Foundational Phase**: 3 tasks
- **User Story 1 (P1)**: 24 tasks (tests, implementation, verification, commit)
- **User Story 2 (P1)**: 13 tasks (tests, implementation, verification, commit)
- **User Story 3 (P2)**: 12 tasks (tests, implementation, verification, commit)
- **User Story 4 (P2)**: 13 tasks (tests, implementation, verification, commit)
- **Polish Phase**: 15 tasks (smoothing, stability, performance, build verification)
- **Documentation Phase**: 3 tasks (architecture docs, commit)
- **Verification Phase**: 6 tasks (requirements review, compliance table, self-check)
- **Completion Phase**: 3 tasks (final commit, completion claim)

### Parallel Opportunities Identified

- Setup: 2 tasks can run in parallel
- Foundational tests: 3 unit tests can be written in parallel
- User Story 1: 8 implementation tasks marked [P], 4 spectral tests can run in parallel
- User Story 2: 3 interpolation tests can run in parallel
- User Story 3: 3 spectral tests marked [P]
- User Story 4: 4 spectral tests can run in parallel
- Polish: 6 tasks marked [P] can run in parallel

### Independent Test Criteria

- **User Story 1**: Process white noise through FormantFilter with each vowel (A, E, I, O, U), verify spectrum shows peaks at expected formant frequencies within +/-10%
- **User Story 2**: Sweep vowel morph position 0.0→4.0 over 100ms while processing audio, verify smooth transitions without clicks
- **User Story 3**: Apply +12 semitone shift to vowel A, verify formant frequencies doubled within 1% tolerance
- **User Story 4**: Set gender +1.0, verify formants scaled up by 1.17-1.21x; set gender -1.0, verify formants scaled down by 0.82-0.86x

### Suggested MVP Scope

**MVP = User Story 1 + User Story 2** (Phases 1-4 complete)

This provides:
- Discrete vowel selection for all 5 vowels (A, E, I, O, U)
- Smooth vowel morphing with click-free transitions
- Complete "talking filter" / "formant wah" effect
- Ready for production use in vocal effects, synth pads, guitar processing

User Stories 3 & 4 (formant shift and gender) are valuable extensions but not required for MVP.

---

## Format Validation

**Checklist Format Compliance**: ✅ ALL tasks follow the mandatory format:
- ✅ Every task starts with `- [ ]` (checkbox)
- ✅ Every task has Task ID (T001, T002, T003...)
- ✅ [P] marker present ONLY for parallelizable tasks
- ✅ [Story] label (US1, US2, US3, US4) present for all user story phase tasks
- ✅ Clear descriptions with exact file paths
- ✅ Sequential task IDs in execution order

---

## Notes

- [P] tasks = different files or different methods/test cases, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list in dsp/tests/CMakeLists.txt)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- This is a DSP feature with NO GUI components - all work is in the dsp/ directory
- Layer 2 processor depends only on Layer 0 (filter_tables.h) and Layer 1 (Biquad, OnePoleSmoother)
