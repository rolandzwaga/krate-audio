# Tasks: Ruinae Engine Composition

**Input**: Design documents from `/specs/044-engine-composition/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/ruinae-engine-api.md

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
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/systems/ruinae_engine_test.cpp  # ADD YOUR FILE HERE
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

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Prepare RuinaeVoice for engine composition by adding missing forwarding methods

- [X] T001 Add `setOscAPhaseMode(PhaseMode)` and `setOscBPhaseMode(PhaseMode)` forwarding methods to RuinaeVoice in `dsp/include/krate/dsp/systems/ruinae_voice.h` (required by FR-035). **Validated**: grep confirmed these methods do NOT exist in ruinae_voice.h. SelectableOscillator has `setPhaseMode()` but oscA_/oscB_ are private — forwarding methods are required.

**Checkpoint**: RuinaeVoice API compatible with engine requirements

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core engine structure and basic lifecycle - MUST be complete before ANY user story implementation

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Tests for Foundation (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T002 [P] Write failing tests for RuinaeEngine construction and constants (FR-001, FR-002) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T003 [P] Write failing tests for prepare() lifecycle (FR-003) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T004 [P] Write failing tests for reset() lifecycle (FR-004) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`

### 2.2 Implementation for Foundation

- [X] T005 Create RuinaeEngine header skeleton with RuinaeModDest enum (FR-001, FR-020) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T006 Implement constructor and member variable declarations (16 RuinaeVoice, VoiceAllocator, MonoHandler, NoteProcessor, ModulationEngine, 2 SVF, RuinaeEffectsChain, all buffers) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T007 Implement prepare() method: initialize all sub-components, allocate buffers (voiceScratch, mixL/R, previousOutputL/R) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T008 Implement reset() method: clear all state, reset all sub-components in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T009 Verify foundation tests pass
- [X] T010 Add test source files to `dsp/CMakeLists.txt` in dsp_tests target

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Polyphonic Voice Playback (Priority: P1) 🎯 MVP

**Goal**: Enable polyphonic playback with voice pool, allocation, summing, and deferred lifecycle management. The core use case.

**Independent Test**: Create engine, prepare(), send multiple noteOn events for a chord, process stereo block, verify non-zero audio containing frequency content from all notes. Send noteOff, process until silence.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T011 [P] [US1] Write failing tests for poly mode noteOn dispatch (FR-005) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T012 [P] [US1] Write failing tests for poly mode noteOff dispatch (FR-006) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T013 [P] [US1] Write failing tests for polyphony configuration (FR-010) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T014 [P] [US1] Write failing tests for voice summing into mono output (FR-012, mono mixing) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T015 [P] [US1] Write failing tests for deferred voiceFinished() notifications (FR-033, FR-034) in `dsp/tests/unit/systems/ruinae_engine_test.cpp` — covers edge cases: "voice finishes mid-block", "processBlock with numSamples=0"
- [X] T016 [P] [US1] Write failing tests for getActiveVoiceCount() query (FR-040) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T017 [P] [US1] Write failing integration test for MIDI noteOn -> stereo audio output in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`
- [X] T018 [P] [US1] Write failing integration test for MIDI chord -> polyphonic mix in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`
- [X] T019 [P] [US1] Write failing integration test for noteOff -> release -> silence in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`
- [X] T020 [P] [US1] Write failing integration test for voice stealing signal path in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`

### 3.2 Implementation for User Story 1

- [X] T021 [US1] Implement noteOn() method with poly mode dispatch (FR-005) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T022 [US1] Implement noteOff() method with poly mode dispatch (FR-006) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T023 [US1] Implement setPolyphony() with gain compensation recalculation (FR-010) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T024 [US1] Implement processBlock() skeleton with mono voice summing (FR-032 partial, FR-033, FR-034) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T025 [US1] Implement getActiveVoiceCount() and getMode() queries (FR-040) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T026 [US1] Implement timestamping for noteOn events in dispatchPolyNoteOn() in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T027 [US1] Verify all User Story 1 tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T028 [US1] Verify IEEE 754 compliance: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/CMakeLists.txt`

### 3.4 Commit (MANDATORY)

- [X] T029 [US1] Commit completed User Story 1 work

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Stereo Voice Mixing with Pan Spread (Priority: P1)

**Goal**: Pan each mono voice to stereo positions using equal-power law, distribute via stereo spread parameter, and add stereo width control.

**Independent Test**: Play single note with spread=0 and verify equal L/R output, then set spread=1.0 and play two notes to verify different pan positions.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T030 [P] [US2] Write failing tests for equal-power pan law (FR-012) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T031 [P] [US2] Write failing tests for stereo spread pan position calculation (FR-013) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T032 [P] [US2] Write failing tests for stereo width Mid/Side processing (FR-014) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T033 [P] [US2] Write failing integration test for stereo spread verification (SC-010) in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`

### 4.2 Implementation for User Story 2

- [X] T034 [P] [US2] Implement setStereoSpread() with pan position recalculation (FR-013) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T035 [P] [US2] Implement setStereoWidth() (FR-014) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T036 [US2] Update processBlock() to use stereo mix buffers with equal-power panning (FR-012, FR-032 step 7e) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T037 [US2] Add stereo width Mid/Side processing to processBlock() (FR-014, FR-032 step 8) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T038 [US2] Add recalculatePanPositions() helper method in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T039 [US2] Verify all User Story 2 tests pass

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T040 [US2] Verify IEEE 754 compliance: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/CMakeLists.txt`

### 4.4 Commit (MANDATORY)

- [X] T041 [US2] Commit completed User Story 2 work

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Mono/Poly Mode Switching with Legato and Portamento (Priority: P1)

**Goal**: Enable mono mode with legato (no envelope retrigger) and portamento (smooth pitch glide). Support seamless mode switching.

**Independent Test**: Set mono mode, play overlapping notes, verify single-voice behavior with legato. Switch to poly, verify multiple voices play simultaneously.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T042 [P] [US3] Write failing tests for mono mode noteOn dispatch (FR-007) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T043 [P] [US3] Write failing tests for mono mode noteOff dispatch (FR-008) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T044 [P] [US3] Write failing tests for mono mode portamento processing (FR-009) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T045 [P] [US3] Write failing tests for mode switching poly->mono (FR-011) in `dsp/tests/unit/systems/ruinae_engine_test.cpp` — covers edge cases: "poly->mono preserves most recent voice", "setMode same mode is no-op"
- [X] T046 [P] [US3] Write failing tests for mode switching mono->poly (FR-011) in `dsp/tests/unit/systems/ruinae_engine_test.cpp` — covers edge case: "mono->poly: voice 0 continues, MonoHandler reset to neutral"
- [X] T047 [P] [US3] Write failing tests for mono mode configuration (FR-036) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T048 [P] [US3] Write failing integration test for mono legato signal path in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`
- [X] T049 [P] [US3] Write failing integration test for portamento pitch glide (SC-006) in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`
- [X] T050 [P] [US3] Write failing integration test for mode switching under load (SC-007) in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`

### 5.2 Implementation for User Story 3

- [X] T051 [US3] Implement setMode() with poly->mono and mono->poly transitions (FR-011) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T052 [US3] Implement dispatchMonoNoteOn() (FR-007) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T053 [US3] Implement dispatchMonoNoteOff() (FR-008) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T054 [US3] Update processBlock() to add mono mode branch with per-sample portamento (FR-009, FR-032 step 7b) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T055 [US3] Implement mono mode configuration forwarding methods (FR-036) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T056 [US3] Verify all User Story 3 tests pass

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T057 [US3] Verify IEEE 754 compliance: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/CMakeLists.txt`

### 5.4 Commit (MANDATORY)

- [X] T058 [US3] Commit completed User Story 3 work

**Checkpoint**: All core voice playback modes (poly, mono, mode switching) should now be functional and committed

---

## Phase 6: User Story 4 - Global Modulation Engine Integration (Priority: P2)

**Goal**: Integrate global ModulationEngine (LFOs, Chaos, Rungler, macros) with routing to engine-wide parameters and "AllVoice" forwarding.

**Independent Test**: Set up global routing (LFO1 -> GlobalFilterCutoff), process blocks, verify filter cutoff changes over time according to LFO.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T059 [P] [US4] Write failing tests for global modulation processing order (FR-018) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T060 [P] [US4] Write failing tests for global routing configuration (FR-019) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T061 [P] [US4] Write failing tests for RuinaeModDest enum destinations (FR-020) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T062 [P] [US4] Write failing tests for AllVoice offset forwarding (FR-021) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T063 [P] [US4] Write failing tests for global mod source configuration (FR-022) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T064 [P] [US4] Write failing integration test for global modulation -> filter cutoff (SC-011) in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`

### 6.2 Implementation for User Story 4

- [X] T065 [US4] Implement setGlobalModRoute() and clearGlobalModRoute() (FR-019) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T066 [US4] Implement global mod source setters (FR-022) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T067 [US4] Update processBlock() to add steps 2-5: set block context, process ModulationEngine, read offsets, apply to engine params, forward AllVoice (FR-018, FR-021, FR-032 steps 2-5) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T068 [US4] Add previousOutputL_/R_ buffer copy at end of processBlock() (FR-032 step 15) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T069 [US4] Verify all User Story 4 tests pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T070 [US4] Verify IEEE 754 compliance: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/CMakeLists.txt`

### 6.4 Commit (MANDATORY)

- [X] T071 [US4] Commit completed User Story 4 work

**Checkpoint**: Global modulation should route to engine-level and per-voice parameters

---

## Phase 7: User Story 5 - Effects Chain Integration (Priority: P2)

**Goal**: Process summed voice output through RuinaeEffectsChain (freeze, delay, reverb) with independent controls and latency reporting.

**Independent Test**: Play notes with reverb mix=0.5, verify reverberant tail extends beyond voice release. Test delay echoes with delay type=Digital.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T072 [P] [US5] Write failing tests for effects chain processing (FR-026) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T073 [P] [US5] Write failing tests for effects parameter forwarding (FR-027) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T074 [P] [US5] Write failing tests for getLatencySamples() (FR-028) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T075 [P] [US5] Write failing integration test for reverb tail (SC-012) in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`
- [X] T076 [P] [US5] Write failing integration test for delay echoes in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`

### 7.2 Implementation for User Story 5

- [X] T077 [P] [US5] Implement effects chain parameter forwarding methods (FR-027) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T078 [P] [US5] Implement getLatencySamples() (FR-028) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T079 [US5] Update processBlock() to add step 10: process effects chain in-place (FR-026, FR-032 step 10) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T080 [US5] Verify all User Story 5 tests pass

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T081 [US5] Verify IEEE 754 compliance: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/CMakeLists.txt`

### 7.4 Commit (MANDATORY)

- [X] T082 [US5] Commit completed User Story 5 work

**Checkpoint**: Effects chain should process the summed voice output with reverb, delay, and freeze

---

## Phase 8: User Story 6 - Master Output with Gain Compensation and Soft Limiting (Priority: P2)

**Goal**: Apply 1/sqrt(N) gain compensation, tanh-based soft limiting, and NaN/Inf flush to prevent clipping and ensure safe output.

**Independent Test**: Play 16 voices at full velocity with soft limiting enabled, verify no output sample exceeds [-1.0, +1.0].

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T083 [P] [US6] Write failing tests for master gain configuration (FR-029) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T084 [P] [US6] Write failing tests for soft limiter toggle (FR-030) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T085 [P] [US6] Write failing tests for NaN/Inf flush (FR-031) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T086 [P] [US6] Write failing integration test for soft limiter under full load (SC-003) in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`
- [X] T087 [P] [US6] Write failing integration test for soft limiter transparency at low levels (SC-004) in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`
- [X] T088 [P] [US6] Write failing integration test for gain compensation accuracy (SC-005) in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`

### 8.2 Implementation for User Story 6

- [X] T089 [P] [US6] Implement setMasterGain() (FR-029) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T090 [P] [US6] Implement setSoftLimitEnabled() (FR-030) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T091 [US6] Update processBlock() to add steps 11-13: apply master gain, soft limiter, NaN/Inf flush (FR-029, FR-030, FR-031, FR-032 steps 11-13) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T092 [US6] Verify all User Story 6 tests pass

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T093 [US6] Verify IEEE 754 compliance: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/CMakeLists.txt`

### 8.4 Commit (MANDATORY)

- [X] T094 [US6] Commit completed User Story 6 work

**Checkpoint**: Master output should prevent clipping and maintain safe output levels

---

## Phase 9: User Story 7 - Unified Parameter Forwarding to RuinaeVoice Pool (Priority: P2)

**Goal**: Forward all RuinaeVoice parameters (oscillators, filter, distortion, trance gate, envelopes, modulation) from engine to all 16 voices uniformly.

**Independent Test**: Set filter cutoff=500Hz on engine, trigger note, verify voice uses 500Hz cutoff. Change distortion type, verify all active voices switch types.

### 9.1 Tests for User Story 7 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T095 [P] [US7] Write failing tests for oscillator parameter forwarding (FR-035) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T096 [P] [US7] Write failing tests for mixer parameter forwarding (FR-035) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T097 [P] [US7] Write failing tests for filter parameter forwarding (FR-035) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T098 [P] [US7] Write failing tests for distortion parameter forwarding (FR-035) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T099 [P] [US7] Write failing tests for trance gate parameter forwarding (FR-035) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T100 [P] [US7] Write failing tests for envelope parameter forwarding (FR-035) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T101 [P] [US7] Write failing tests for per-voice modulation routing forwarding (FR-035) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`

### 9.2 Implementation for User Story 7

- [X] T102 [P] [US7] Implement oscillator parameter forwarding methods (FR-035) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T103 [P] [US7] Implement mixer parameter forwarding methods (FR-035) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T104 [P] [US7] Implement filter parameter forwarding methods (FR-035) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T105 [P] [US7] Implement distortion parameter forwarding methods (FR-035) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T106 [P] [US7] Implement trance gate parameter forwarding methods (FR-035) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T107 [P] [US7] Implement envelope parameter forwarding methods (FR-035) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T108 [P] [US7] Implement per-voice modulation routing forwarding methods (FR-035) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T109 [US7] Verify all User Story 7 tests pass

### 9.3 Cross-Platform Verification (MANDATORY)

- [X] T110 [US7] Verify IEEE 754 compliance: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/CMakeLists.txt`

### 9.4 Commit (MANDATORY)

- [X] T111 [US7] Commit completed User Story 7 work

**Checkpoint**: All RuinaeVoice parameters should be controllable from engine level

---

## Phase 10: User Story 8 - Tempo and Transport Synchronization (Priority: P3)

**Goal**: Forward tempo and transport information to all tempo-aware components (global LFOs, trance gates, delay).

**Independent Test**: Set tempo to 120 BPM, verify tempo-synced LFO rate corresponds to 2 Hz. Change tempo to 140 BPM, verify trance gate pattern adjusts.

### 10.1 Tests for User Story 8 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T112 [P] [US8] Write failing tests for setTempo() and setBlockContext() forwarding (FR-039) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T113 [P] [US8] Write failing integration test for tempo sync through chain in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`

### 10.2 Implementation for User Story 8

- [X] T114 [P] [US8] Implement setTempo() and setBlockContext() forwarding (FR-039) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T115 [US8] Verify all User Story 8 tests pass

### 10.3 Cross-Platform Verification (MANDATORY)

- [X] T116 [US8] Verify IEEE 754 compliance: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/CMakeLists.txt`

### 10.4 Commit (MANDATORY)

- [X] T117 [US8] Commit completed User Story 8 work

**Checkpoint**: Tempo-synced features should respond to DAW transport changes

---

## Phase 11: User Story 9 - Aftertouch and Performance Controller Forwarding (Priority: P3)

**Goal**: Forward aftertouch (to all voices), pitch bend (via NoteProcessor), and mod wheel (to global ModulationEngine as macro).

**Independent Test**: Set pitch bend to +1.0, verify all active voices shift pitch upward by bend range. Set aftertouch=0.6, verify per-voice routing responds.

### 11.1 Tests for User Story 9 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T118 [P] [US9] Write failing tests for setPitchBend() (FR-023) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T119 [P] [US9] Write failing tests for setAftertouch() (FR-024) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T120 [P] [US9] Write failing tests for setModWheel() (FR-025) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T121 [P] [US9] Write failing integration test for pitch bend through full chain in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`
- [X] T122 [P] [US9] Write failing integration test for aftertouch -> voice modulation in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`

### 11.2 Implementation for User Story 9

- [X] T123 [P] [US9] Implement setPitchBend() (FR-023) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T124 [P] [US9] Implement setAftertouch() (FR-024) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T125 [P] [US9] Implement setModWheel() (FR-025) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T126 [US9] Update processBlock() to add step 6: process pitch bend smoother (FR-032 step 6) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T127 [US9] Update processBlock() to apply pitch bend in poly mode (FR-032 step 7a) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T128 [US9] Verify all User Story 9 tests pass

### 11.3 Cross-Platform Verification (MANDATORY)

- [X] T129 [US9] Verify IEEE 754 compliance: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/CMakeLists.txt`

### 11.4 Commit (MANDATORY)

- [X] T130 [US9] Commit completed User Story 9 work

**Checkpoint**: Performance controllers should provide expressive control over synthesis

---

## Phase 12: Additional Requirements (Non-User-Story Features)

**Purpose**: Complete remaining functional requirements not tied to specific user stories

### 12.1 Tests for Additional Requirements (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T131 [P] Write failing tests for global filter (FR-015, FR-016, FR-017) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T132 [P] Write failing tests for voice allocator configuration (FR-037) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T133 [P] Write failing tests for note processor configuration (FR-038) in `dsp/tests/unit/systems/ruinae_engine_test.cpp`
- [X] T134 [P] Write failing tests for parameter safety (FR-043) in `dsp/tests/unit/systems/ruinae_engine_test.cpp` — covers edge cases: "NaN/Inf float inputs silently ignored", "out-of-range values clamped", "noteOn before prepare() ignored"
- [X] T135 [P] Write failing integration test for multi-sample-rate validation (SC-008) in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`
- [X] T136 [P] Write failing integration test for full signal chain in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`
- [X] T136b [P] Write failing integration test for global filter signal processing: play note, enable global filter at 500Hz LP, verify output spectral content is low-pass filtered (FR-015, FR-016, FR-017) in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`

### 12.2 Implementation for Additional Requirements

- [X] T137 [P] Implement global filter methods (FR-015, FR-016, FR-017) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T138 [P] Implement voice allocator configuration forwarding (FR-037) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T139 [P] Implement note processor configuration forwarding (FR-038) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T140 [US] Update processBlock() to add step 9: global filter processing (FR-032 step 9) in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T141 [US] Add parameter validation (FR-043) to all setter methods in `dsp/include/krate/dsp/systems/ruinae_engine.h`
- [X] T142 Verify all additional requirement tests pass

### 12.3 Cross-Platform Verification (MANDATORY)

- [X] T143 Verify IEEE 754 compliance: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/CMakeLists.txt`

### 12.4 Commit (MANDATORY)

- [X] T144 Commit completed additional requirements work

**Checkpoint**: All functional requirements complete

---

## Phase 13: CPU Performance Benchmark (SC-001)

**Purpose**: Verify CPU performance meets <10% single core target

### 13.1 Performance Test

- [X] T145 Write CPU performance benchmark test (SC-001) in `dsp/tests/unit/systems/ruinae_engine_integration_test.cpp`
- [X] T146 Run benchmark: 8 voices at 44.1 kHz for 1 second, measure wall-clock time
- [X] T147 Verify < 10% single core usage (approximately < 23 ms for 1 second of audio)

### 13.2 Optimization (if needed)

- [X] T148 If benchmark fails: profile hot spots and optimize
- [X] T149 Re-run benchmark and verify target met

### 13.3 Commit (MANDATORY)

- [X] T150 Commit performance benchmark work

**Checkpoint**: Performance target verified

---

## Phase 14: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [X] T151 [P] Code cleanup: remove any debug logging or commented-out code
- [X] T152 [P] Verify all noexcept annotations are correct
- [X] T153 [P] Verify all const-correctness and [[nodiscard]] annotations
- [X] T154 [P] Add comprehensive header documentation with usage examples
- [X] T155 Run quickstart.md validation: verify all code examples compile and run
- [X] T156 Commit polish work

**Checkpoint**: Code is production-ready

---

## Phase 15: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 15.1 Architecture Documentation Update

- [X] T157 Update `specs/_architecture_/layer3-systems.md` with RuinaeEngine component entry:
  - Purpose: Complete polyphonic Ruinae synthesizer engine
  - Public API summary: noteOn/Off, processBlock, all parameter setters
  - File location: `dsp/include/krate/dsp/systems/ruinae_engine.h`
  - When to use: Phase 7 plugin shell will instantiate this engine
  - Composition pattern: 16 RuinaeVoice + VoiceAllocator + MonoHandler + NoteProcessor + ModulationEngine + effects
  - Include usage example: basic prepare/noteOn/processBlock lifecycle

### 15.2 Final Commit

- [X] T158 Commit architecture documentation updates
- [X] T159 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 16: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 16.1 Run Clang-Tidy Analysis

- [X] T160 Run clang-tidy on all modified/new source files:
  ```bash
  # Windows (PowerShell)
  ./tools/run-clang-tidy.ps1 -Target all

  # Linux/macOS
  ./tools/run-clang-tidy.sh --target all
  ```

### 16.2 Address Findings

- [X] T161 Fix all errors reported by clang-tidy (blocking issues)
- [X] T162 Review warnings and fix where appropriate (use judgment for DSP code)
- [X] T163 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)
- [X] T164 Commit clang-tidy fixes

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 17: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 17.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T165 Review ALL FR-001 through FR-044 requirements from spec.md against implementation
- [X] T166 Review ALL SC-001 through SC-014 success criteria and verify measurable targets are achieved
- [X] T167 Search for cheating patterns in implementation:
  - [X] No `// placeholder` or `// TODO` comments in new code
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 17.2 Fill Compliance Table in spec.md

- [X] T168 Update spec.md "Implementation Verification" section with compliance status for each requirement
- [X] T169 For each FR-xxx: cite file path, line number, and description of how it's met
- [X] T170 For each SC-xxx: cite test name, actual measured value vs spec target
- [X] T171 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 17.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T172 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 18: Final Completion

**Purpose**: Final commit and completion claim

### 18.1 Final Commit

- [X] T173 Commit all spec work to feature branch
- [X] T174 Verify all tests pass
- [X] T175 Verify clean build with zero warnings

### 18.2 Completion Claim

- [X] T176 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup (Phase 1) completion - BLOCKS all user stories
- **User Stories (Phase 3-11)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (US1 → US2 → US3 → US4 → US5 → US6 → US7 → US8 → US9)
- **Additional Requirements (Phase 12)**: Can be done in parallel with user stories or after all stories complete
- **Performance Benchmark (Phase 13)**: Depends on complete processBlock() implementation (US1-US6)
- **Polish (Phase 14)**: Depends on all desired user stories being complete
- **Documentation (Phase 15)**: Depends on all implementation complete
- **Static Analysis (Phase 16)**: Depends on all implementation complete
- **Completion Verification (Phase 17)**: Depends on all work complete
- **Final Completion (Phase 18)**: Depends on verification passing

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1)**: Depends on US1 (extends processBlock with stereo panning)
- **User Story 3 (P1)**: Can start after Foundational (Phase 2) - Parallel with US1/US2. **Note (F2)**: US3 (mono mode) does NOT depend on US7 (parameter forwarding) — mono mode uses voice 0's existing envelope configuration directly. US7 can run fully in parallel with US3.
- **User Story 4 (P2)**: Can start after Foundational (Phase 2) - Parallel with all others. **Note (F1)**: US4 and US2 can be written in parallel, but final processBlock() integration requires correct ordering (modulation at step 3-5 before stereo mixing at step 7-8). Both stories modify processBlock() so coordinate insertion points.
- **User Story 5 (P2)**: Depends on US2 (stereo mix buffers for effects input)
- **User Story 6 (P2)**: Can start after Foundational (Phase 2) - Parallel with all others
- **User Story 7 (P2)**: Can start after Foundational (Phase 2) - Parallel with all others
- **User Story 8 (P3)**: Can start after Foundational (Phase 2) - Parallel with all others
- **User Story 9 (P3)**: Can start after Foundational (Phase 2) - Parallel with all others

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- **Implementation**: After tests fail
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in `dsp/CMakeLists.txt`
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- All Setup tasks can run in parallel
- All Foundational test tasks (T002-T004) can run in parallel
- Once Foundational phase completes, user stories can start in parallel (if team capacity allows):
  - US1, US3, US4, US6, US7, US8, US9 are fully independent
  - US2 requires US1 complete
  - US5 requires US2 complete
- All test tasks within a user story marked [P] can run in parallel
- All implementation tasks within a user story marked [P] can run in parallel
- Additional requirements (Phase 12) can run in parallel with user stories

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task T011: Write failing tests for poly mode noteOn dispatch
Task T012: Write failing tests for poly mode noteOff dispatch
Task T013: Write failing tests for polyphony configuration
Task T014: Write failing tests for voice summing
Task T015: Write failing tests for deferred voiceFinished
Task T016: Write failing tests for getActiveVoiceCount
Task T017: Write failing integration test for MIDI noteOn -> stereo audio
Task T018: Write failing integration test for MIDI chord -> polyphonic mix
Task T019: Write failing integration test for noteOff -> release -> silence
Task T020: Write failing integration test for voice stealing signal path
```

---

## Implementation Strategy

### MVP First (User Story 1 + 2 + 3 Only)

1. Complete Phase 1: Setup (add phase mode forwarding to RuinaeVoice)
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (polyphonic playback)
4. Complete Phase 4: User Story 2 (stereo panning)
5. Complete Phase 5: User Story 3 (mono mode)
6. **STOP and VALIDATE**: Test core engine independently
7. This represents the minimum viable product

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Core voice playback works
3. Add User Story 2 → Test independently → Stereo panning works
4. Add User Story 3 → Test independently → Mono/poly mode works
5. Add User Story 4 → Test independently → Global modulation works
6. Add User Story 5 → Test independently → Effects chain works
7. Add User Story 6 → Test independently → Master output safe
8. Add User Story 7 → Test independently → All parameters controllable
9. Add User Stories 8-9 → Test independently → Complete feature set
10. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 → User Story 2 (sequential dependency)
   - Developer B: User Story 3 + User Story 4 (parallel)
   - Developer C: User Story 6 + User Story 7 (parallel)
3. After US2 completes: Developer A takes User Story 5 (depends on US2)
4. Stories complete and integrate independently

---

## Summary

**Total Tasks**: 176
**User Story Breakdown**:
- Setup: 1 task
- Foundational: 9 tasks
- US1 (Polyphonic Playback): 19 tasks
- US2 (Stereo Panning): 12 tasks
- US3 (Mono Mode): 17 tasks
- US4 (Global Modulation): 13 tasks
- US5 (Effects Chain): 11 tasks
- US6 (Master Output): 12 tasks
- US7 (Parameter Forwarding): 17 tasks
- US8 (Tempo Sync): 6 tasks
- US9 (Performance Controllers): 13 tasks
- Additional Requirements: 14 tasks
- Performance Benchmark: 6 tasks
- Polish: 6 tasks
- Documentation: 4 tasks
- Static Analysis: 5 tasks
- Completion Verification: 8 tasks
- Final Completion: 4 tasks

**Parallel Opportunities Identified**: 90+ tasks marked [P] across all user stories

**Independent Test Criteria**:
- Each user story has clear acceptance criteria defined in spec.md
- Integration tests verify each story works independently
- MVP scope = User Stories 1-3 (core playback modes)

**Suggested MVP Scope**: User Stories 1, 2, and 3 (polyphonic playback, stereo panning, mono mode)

**Format Validation**: All tasks follow the checklist format (checkbox, ID, [P] and [Story] labels where appropriate, file paths included)

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
