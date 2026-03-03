# Tasks: Ruinae Effects Section

**Input**: Design documents from `/specs/043-effects-section/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

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
4. **Run Clang-Tidy**: Static analysis check (see Phase 9.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/systems/ruinae_effects_chain_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2)
- Include exact file paths in descriptions

## Path Conventions

- DSP library component: `dsp/include/krate/dsp/systems/ruinae_effects_chain.h`
- Tests: `dsp/tests/unit/systems/ruinae_effects_chain_test.cpp`

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization - add delay type enum to existing Ruinae type system

**IMPORTANT**: This feature adds ONE new header (`ruinae_effects_chain.h`) and modifies ONE existing file (`ruinae_types.h`). There is NO project initialization needed.

- [X] T001 Add RuinaeDelayType enum to `dsp/include/krate/dsp/systems/ruinae_types.h` per spec FR-007, FR-008 (Digital=0, Tape=1, PingPong=2, Granular=3, Spectral=4, NumTypes=5, underlying type uint8_t)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core class skeleton that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T002 Create `dsp/tests/unit/systems/ruinae_effects_chain_test.cpp` test file with basic skeleton and test includes
- [X] T003 Create `dsp/include/krate/dsp/systems/ruinae_effects_chain.h` skeleton with all member variables from data-model.md E-002 (sampleRate_, maxBlockSize_, prepared_, tempoBPM_, freeze_, freezeEnabled_, all 5 delay instances, crossfade state, latency compensation arrays, reverb_, temp buffers)
- [X] T004 Implement constructor with default initialization (all bools false, activeDelayType_ = Digital, crossfadeAlpha_ = 0.0f)
- [X] T005 Implement prepare() method per FR-002 (prepare all 5 delays per plan.md Dependency API Contracts table, prepare freeze with maxDelayMs=5000, prepare reverb, allocate temp buffers to maxBlockSize, query spectral delay latency, prepare compensation delays)
- [X] T006 Implement reset() method per FR-003 (reset all delays, freeze, reverb, compensation delays, crossfade state to idle)
- [X] T007 Add test file to `dsp/tests/CMakeLists.txt` in dsp_tests target sources
- [X] T008 Write failing test for RuinaeDelayType enum values (verify Digital=0, NumTypes=5, underlying type)
- [X] T009 Write failing test for prepare/reset lifecycle (construct, prepare at 44.1kHz/512, verify prepared_ = true, reset, verify state cleared)
- [X] T010 Run tests and verify they pass
- [X] T011 Commit foundational skeleton

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Stereo Effects Chain Processing (Priority: P1) 🎯 MVP

**Goal**: Implement fixed-order effects chain (Freeze -> Delay -> Reverb -> Output) with dry pass-through when all effects are at default settings

**Independent Test**: Process a stereo sine wave through the chain with all effects bypassed (freeze off, delay mix = 0, reverb mix = 0) and verify output is identical to input within -120 dBFS (SC-004)

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T012 [P] [US1] Write failing test for FR-006: dry pass-through when all effects at default (freeze off, delay mix = 0.0, reverb mix = 0.0) - verify output within -120 dBFS of input sine wave
- [X] T013 [P] [US1] Write failing test for FR-005: fixed processing order (freeze -> delay -> reverb) - verify by enabling each effect independently and checking output
- [X] T014 [P] [US1] Write failing test for FR-004: processBlock() handles zero-sample blocks safely (verify no crash, no state modification)

### 3.2 Implementation for User Story 1

- [X] T015 [US1] Implement processBlock() skeleton per FR-004 (in-place stereo processing, noexcept, signature: `void processBlock(float* left, float* right, size_t numSamples) noexcept`)
- [X] T016 [US1] Add freeze slot processing in processBlock() per FR-005 (if freezeEnabled_: call freeze_.process(left, right, numSamples, ctx), else: pass-through)
- [X] T017 [US1] Add delay slot processing in processBlock() per FR-005 (process active delay type only, no crossfade yet, construct BlockContext with tempoBPM_)
- [X] T018 [US1] Implement processActiveDelay() helper that dispatches to correct delay type using switch on activeDelayType_ per plan.md R-001
- [X] T019 [US1] Add reverb slot processing in processBlock() per FR-005, FR-022 (call reverb_.processBlock(left, right, numSamples) as final stage)
- [X] T020 [US1] Verify all tests pass (dry pass-through, fixed order, zero-sample safety)

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T021 [US1] **Verify IEEE 754 compliance**: Check if `ruinae_effects_chain_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 3.4 Commit (MANDATORY)

- [X] T022 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional - dry signal passes through chain, effects process in order

---

## Phase 4: User Story 2 - Selectable Delay Type (Priority: P1)

**Goal**: Enable selection from five delay types (Digital, Tape, PingPong, Granular, Spectral) with parameter forwarding and API normalization

**Independent Test**: Select each delay type, set delay time = 100ms, feedback = 0.5, mix = 1.0, process impulse, verify delayed output and verify output differs between types

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T023 [P] [US2] Write failing test for FR-009: setDelayType() selects active delay (set to Digital, verify Digital processes, set to Tape, verify Tape processes)
- [X] T024 [P] [US2] Write failing test for FR-014: setDelayType(same) is no-op (set to Digital, call setDelayType(Digital) again, verify no state change)
- [X] T025 [P] [US2] Write failing test for FR-015: delay parameter forwarding (setDelayTime, setDelayFeedback, setDelayMix) - verify each delay type responds to parameters
- [X] T026 [P] [US2] Write failing test for FR-017: delay time forwarding per type (Digital uses setTime, Tape uses setMotorSpeed, etc. per plan.md table)
- [X] T027 [P] [US2] Write failing test for FR-016: setDelayTempo() updates BlockContext tempo
- [X] T028 [P] [US2] Write failing test for all 5 delay types process correctly (impulse response differs between types, proving correct type is active)

### 4.2 Implementation for User Story 2

- [X] T029 [P] [US2] Implement setDelayType() per FR-009 (update activeDelayType_, no crossfade yet, FR-014: early-exit if type == activeDelayType_)
- [X] T030 [P] [US2] Implement getActiveDelayType() per API contract (return activeDelayType_)
- [X] T031 [US2] Implement setDelayTime() per FR-015, FR-017 (forward to all 5 delays using correct API per quickstart.md Parameter Forwarding: digitalDelay_.setTime, tapeDelay_.setMotorSpeed, pingPongDelay_.setDelayTimeMs, granularDelay_.setDelayTime, spectralDelay_.setBaseDelayMs)
- [X] T032 [P] [US2] Implement setDelayFeedback() per FR-015 (forward to all 5 delays: all use setFeedback(amount))
- [X] T033 [P] [US2] Implement setDelayMix() per FR-015 (forward to all 5 delays: Digital/Tape/PingPong use setMix, Granular uses setDryWet, Spectral uses setDryWetMix with 0-1 conversion per IMPORTANT context note)
- [X] T034 [P] [US2] Implement setDelayTempo() per FR-016 (store in tempoBPM_, BlockContext uses this in processBlock)
- [X] T035 [US2] Update processActiveDelay() to handle all 5 delay types per plan.md R-001 (switch on activeDelayType_, call correct process signature: TapeDelay has no BlockContext, GranularDelay uses separate buffers per plan.md R-005)
- [X] T036 [US2] Add GranularDelay buffer normalization per plan.md R-005 (copy in-place buffers to tempL_/tempR_, call granular_.process(tempL_, tempR_, left, right, n, ctx), result already in left/right)
- [X] T037 [US2] Verify all tests pass (all 5 types selectable, parameters forwarded correctly, outputs differ)

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T038 [US2] **Verify IEEE 754 compliance**: Check if test code added uses `std::isnan`/`std::isfinite`/`std::isinf` → update `-fno-fast-math` list if needed

### 4.4 Commit (MANDATORY)

- [X] T039 [US2] **Commit completed User Story 2 work**

**Checkpoint**: All 5 delay types selectable and functional, parameters forwarded correctly

---

## Phase 5: User Story 3 - Spectral Freeze as Insert Effect (Priority: P2)

**Goal**: Integrate spectral freeze effect with pitch shifting, shimmer mix, and decay control

**Independent Test**: Feed signal into chain, enable freeze, change input signal, verify output remains frozen spectrum (not new input)

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T040 [P] [US3] Write failing test for FR-018: setFreezeEnabled activates freeze slot (enable freeze, verify FreezeMode processes audio)
- [X] T041 [P] [US3] Write failing test for FR-019: setFreeze captures spectrum (freeze enabled and frozen, change input, verify output unchanged)
- [X] T042 [P] [US3] Write failing test for FR-020: freeze enable/disable transitions are click-free (toggle freeze rapidly, measure output discontinuities < -60 dBFS)
- [X] T043 [P] [US3] Write failing test for FR-018 freeze parameter forwarding (pitch semitones, shimmer mix, decay)
- [X] T044 [P] [US3] Write failing test for freeze pitch shifting (set pitch = +12 semitones, verify output one octave higher)
- [X] T045 [P] [US3] Write failing test for shimmer mix blending (shimmer mix = 0.0 produces unpitched output, mix = 1.0 produces pitched output)
- [X] T046 [P] [US3] Write failing test for decay control (decay = 1.0 produces fading output, decay = 0.0 sustains infinitely)

### 5.2 Implementation for User Story 3

- [X] T047 [P] [US3] Implement setFreezeEnabled() per FR-018 (store in freezeEnabled_, processBlock uses this for bypass)
- [X] T048 [P] [US3] Implement setFreeze() per FR-018 (forward to freeze_.setFreezeEnabled(frozen))
- [X] T049 [P] [US3] Implement setFreezePitchSemitones() per FR-018 (forward to freeze_.setPitchSemitones(semitones))
- [X] T050 [P] [US3] Implement setFreezeShimmerMix() per FR-018 (forward directly: freeze_.setShimmerMix(mix) — API uses 0-1 normalized, NO conversion needed)
- [X] T051 [P] [US3] Implement setFreezeDecay() per FR-018 (forward directly: freeze_.setDecay(decay) — API uses 0-1 normalized, NO conversion needed)
- [X] T052 [US3] Update prepare() to configure freeze for insert use per plan.md R-004 (prepare with maxDelayMs=5000, set dry/wet to 100% if enabled)
- [X] T053 [US3] Update processBlock freeze slot per plan.md R-008 (if freezeEnabled_: process, else: skip entirely for zero overhead)
- [X] T054 [US3] Verify all tests pass (freeze captures, parameters work, transitions click-free)

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T055 [US3] **Verify IEEE 754 compliance**: Check if test code added uses `std::isnan`/`std::isfinite`/`std::isinf` → update `-fno-fast-math` list if needed

### 5.4 Commit (MANDATORY)

- [X] T056 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Spectral freeze fully functional with pitch/shimmer/decay control

---

## Phase 6: User Story 4 - Dattorro Reverb Integration (Priority: P2)

**Goal**: Add Dattorro reverb as final chain stage with independent freeze control

**Independent Test**: Process stereo impulse with only reverb active (mix = 0.5, room size = 0.7), verify output contains dry impulse + reverberant tail

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T057 [P] [US4] Write failing test for FR-021: setReverbParams forwards all reverb parameters
- [X] T058 [P] [US4] Write failing test for FR-022: reverb processes delay output not dry input (enable delay with 100ms time, enable reverb, verify reverb acts on delayed signal)
- [X] T059 [P] [US4] Write failing test for FR-023: reverb freeze operates independently of spectral freeze (both frozen simultaneously, verify both operate independently)
- [X] T060 [P] [US4] Write failing test for reverb parameter changes during playback (change room size mid-stream, verify smooth transition)
- [X] T061 [P] [US4] Write failing test for reverb impulse response (process impulse, verify tail characteristics match room size/damping)

### 6.2 Implementation for User Story 4

- [X] T062 [US4] Implement setReverbParams() per FR-021 (forward params to reverb_.setParams(params), noexcept)
- [X] T063 [US4] Verify reverb position in processBlock per FR-022 (already implemented in US1 - reverb processes after delay slot)
- [X] T064 [US4] Verify all tests pass (reverb processes delay output, independent freeze, parameters work)

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T065 [US4] **Verify IEEE 754 compliance**: Check if test code added uses `std::isnan`/`std::isfinite`/`std::isinf` → update `-fno-fast-math` list if needed

### 6.4 Commit (MANDATORY)

- [X] T066 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Reverb fully integrated, processes delayed signal, independent freeze works

---

## Phase 7: User Story 5 - Click-Free Delay Type Switching (Priority: P2)

**Goal**: Implement linear crossfade between delay types (25-50ms) with fast-track support

**Independent Test**: Switch delay types during continuous audio, measure output discontinuities (must be < -60 dBFS per SC-002)

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T067 [P] [US5] Write failing test for FR-010: crossfade blends outgoing and incoming delay outputs (linear ramp: output = outgoing * (1-alpha) + incoming * alpha)
- [X] T068 [P] [US5] Write failing test for FR-011: crossfade duration 25-50ms (verify crossfade completes within spec range at 44.1kHz)
- [X] T069 [P] [US5] Write failing test for FR-012: fast-track on type switch during crossfade (start crossfade Digital->Tape, request Tape->Granular mid-fade, verify snap to completion and new crossfade starts)
- [X] T070 [P] [US5] Write failing test for FR-013: outgoing delay reset after crossfade completes
- [X] T071 [P] [US5] Write failing test for SC-002: crossfade produces no discontinuities > -60 dBFS (switch types during continuous audio, measure per-sample step sizes)
- [X] T072 [P] [US5] Write failing test for SC-008: 10 consecutive type switches click-free (cycle all 5 types twice, verify no clicks)

### 7.2 Implementation for User Story 5

- [X] T073 [US5] Update setDelayType() to initiate crossfade per FR-009 (if type != activeDelayType_: set incomingDelayType_ = type, crossfading_ = true, crossfadeAlpha_ = 0.0, calculate crossfadeIncrement_ using crossfadeIncrement(30.0f, sampleRate_) per plan.md R-006)
- [X] T074 [US5] Implement fast-track logic in setDelayType() per FR-012 (if crossfading_: snap alpha = 1.0, complete current crossfade, reset outgoing delay, start new crossfade)
- [X] T075 [US5] Update processBlock to process both delays during crossfade per FR-010 (if crossfading_: process active delay -> left/right, process incoming delay -> crossfadeOutL_/crossfadeOutR_, blend: left = left * (1-alpha) + crossfadeOutL_ * alpha, increment alpha)
- [X] T076 [US5] Implement crossfade completion logic per FR-013 (when alpha >= 1.0: activeDelayType_ = incomingDelayType_, reset outgoing delay, crossfading_ = false)
- [X] T077 [US5] Verify crossfade uses linear curve per spec Definitions (not equal-power, linear per plan.md R-002)
- [X] T078 [US5] Verify all tests pass (crossfade duration correct, no clicks, fast-track works, outgoing reset)

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T079 [US5] **Verify IEEE 754 compliance**: Check if test code added uses `std::isnan`/`std::isfinite`/`std::isinf` → update `-fno-fast-math` list if needed

### 7.4 Commit (MANDATORY)

- [X] T080 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Click-free delay type switching fully functional with fast-track support

---

## Phase 8: Latency Compensation & Reporting (FR-026, FR-027)

**Goal**: Implement per-delay latency compensation so all delay types align to spectral delay FFT latency

**Independent Test**: Query latency before and after switching delay types, verify constant value (SC-007)

### 8.1 Tests (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T081 [P] Write failing test for FR-026: getLatencySamples() returns spectral delay FFT latency
- [X] T082 [P] Write failing test for FR-027: latency constant across delay type switches (query before switch, query after, verify equal per SC-007)
- [X] T083 [P] Write failing test for latency compensation alignment (process impulse through all 5 delay types, verify all outputs time-aligned)
- [X] T084 [P] Write failing test for compensation only applied to non-spectral delays (spectral delay output not compensated, others compensated)

### 8.2 Implementation

- [X] T085 Implement getLatencySamples() per FR-026 (return targetLatencySamples_, noexcept, const)
- [X] T086 Update prepare() to query spectral delay latency per plan.md R-003 (targetLatencySamples_ = spectralDelay_.getLatencySamples(), typically 1024)
- [X] T087 Update prepare() to allocate compensation delays per plan.md R-003 (4 pairs of DelayLine for Digital/Tape/PingPong/Granular, each sized to targetLatencySamples_)
- [X] T088 Implement compensateLatency() helper per data-model.md Processing Flow (write sample to comp delay, read at fixed offset = targetLatencySamples_)
- [X] T089 Update processActiveDelay() to apply compensation per plan.md R-003 (call compensateLatency after Digital/Tape/PingPong/Granular process, skip for Spectral)
- [X] T090 Verify all tests pass (constant latency, all outputs time-aligned, spectral not compensated)

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T091 **Verify IEEE 754 compliance**: Check if test code added uses `std::isnan`/`std::isfinite`/`std::isinf` → update `-fno-fast-math` list if needed

### 8.4 Commit (MANDATORY)

- [X] T092 **Commit completed latency compensation work**

**Checkpoint**: Latency compensation fully functional, constant latency reporting verified

---

## Phase 9: User Story 6 - Individual Effect Bypass (Priority: P3)

**Goal**: Enable independent bypass of freeze, delay, and reverb slots with smooth transitions

**Independent Test**: Enable/disable each effect independently, verify only targeted effect changes behavior. Bypass active effect with tail, verify smooth transition.

### 9.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T093a [P] [US6] Write failing test for spec US6 acceptance 1: delay disabled while freeze+reverb enabled → signal flows freeze → reverb, delay has no effect
- [X] T093b [P] [US6] Write failing test for spec US6 acceptance 2: all effects disabled, enable single effect → only that effect modifies signal
- [X] T093c [P] [US6] Write failing test for spec US6 acceptance 3: active effect with non-zero tail bypassed → smooth transition (no abrupt cut, discontinuity < -60 dBFS)

### 9.2 Implementation for User Story 6

- [X] T093d [US6] Implement delay bypass behavior (when delay mix = 0.0 or delay bypassed, signal passes through delay slot unmodified)
- [X] T093e [US6] Verify freeze bypass already implemented (freezeEnabled_ = false skips processing, from US3)
- [X] T093f [US6] Verify reverb bypass behavior (reverb mix = 0.0 passes through, smooth transition on parameter change)
- [X] T093g [US6] Verify all bypass tests pass (independent enable/disable, smooth transitions)

### 9.3 Cross-Platform Verification (MANDATORY)

- [X] T093h [US6] **Verify IEEE 754 compliance**: Check if test code added uses `std::isnan`/`std::isfinite`/`std::isinf` → update `-fno-fast-math` list if needed

### 9.4 Commit (MANDATORY)

- [X] T093i [US6] **Commit completed User Story 6 work**

**Checkpoint**: Individual effect bypass fully functional with smooth transitions

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Final improvements, multi-sample-rate verification, performance benchmarking

- [X] T094 [P] Add multi-sample-rate tests per SC-006 (run all tests at 44.1kHz and 96kHz, verify sample-rate-independent behavior)
- [X] T095 [P] Add CPU benchmark test for SC-001 (Digital delay + reverb active, 44.1kHz/512 blocks, verify < 3.0% CPU overhead)
- [X] T096 [P] Add allocation test for SC-003 (run processBlock and all setters under allocator instrumentation, verify zero allocations)
- [X] T097 [P] Verify all FR tests exist per SC-005 (audit test file, verify all 29 FRs have at least one test)
- [X] T098 Run all quickstart.md test scenarios as integration smoke test
- [X] T099 Code review and cleanup (remove any debug comments, verify Doxygen comments on all public methods)
- [X] T0100 **Verify noexcept on all runtime methods** per FR-028 (processBlock, setDelayType, setDelayTime, setDelayFeedback, setDelayMix, setFreeze, setFreezeEnabled, setReverbParams, setDelayTempo — all must be marked noexcept)
- [X] T0101 Commit polish work

---

## Phase 11: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 11.1 Run Clang-Tidy Analysis

- [X] T102 **Run clang-tidy** on all modified/new source files:
  ```bash
  # Windows (PowerShell)
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja

  # Linux/macOS
  ./tools/run-clang-tidy.sh --target dsp
  ```

### 11.2 Address Findings

- [X] T103 **Fix all errors** reported by clang-tidy (blocking issues)
- [X] T104 **Review warnings** and fix where appropriate (use judgment for DSP code)
- [X] T105 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 12: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 12.1 Architecture Documentation Update

- [X] T106 **Update `specs/_architecture_/layer-3-systems.md`** with RuinaeEffectsChain entry:
  - Purpose: Stereo effects chain for Ruinae synthesizer (Freeze -> Delay -> Reverb)
  - Public API: prepare, reset, processBlock, setDelayType, delay/freeze/reverb parameter setters, getLatencySamples
  - File location: `dsp/include/krate/dsp/systems/ruinae_effects_chain.h`
  - When to use: Ruinae engine composition (Phase 6), any multi-effect chain with delay type selection
  - Key features: 5 delay types with click-free crossfade, constant latency reporting, per-delay compensation, real-time safe
- [X] T107 **Update `specs/_architecture_/ruinae-types.md`** (or create if doesn't exist) with RuinaeDelayType enum documentation
- [X] T108 **Verify no duplicate functionality** introduced (search for existing effects chain patterns)

### 12.2 Final Commit

- [X] T109 **Commit architecture documentation updates**

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 13.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T110 **Review ALL FR-001 through FR-029** from spec.md against implementation (open header file, verify each FR is implemented with specific line numbers)
- [X] T111 **Review ALL SC-001 through SC-008** and verify measurable targets are achieved (run benchmarks, measure values, compare to spec thresholds)
- [X] T112 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in `ruinae_effects_chain.h`
  - [X] No test thresholds relaxed from spec requirements in `ruinae_effects_chain_test.cpp`
  - [X] No features quietly removed from scope (all 5 delay types, freeze, reverb, crossfade, latency compensation, individual bypass present)

### 13.2 Fill Compliance Table in spec.md

- [X] T113 **Update spec.md "Implementation Verification" section** with compliance status for each requirement:
  - For each FR-xxx: cite file path and line number where implemented
  - For each SC-xxx: cite actual measured value vs spec target (e.g., "SC-001: Measured 2.1% CPU vs target < 3.0%")
  - Mark status: MET / NOT MET / PARTIAL / DEFERRED

### 13.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **No.**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **No.** (Verified via grep.)
3. Did I remove ANY features from scope without telling the user? **No.** All 5 delay types, freeze, reverb, crossfade, latency compensation, individual bypass are present.
4. Would the spec author consider this "done"? **Yes.** All 29 FRs and 8 SCs are MET.
5. If I were the user, would I feel cheated? **No.**

- [X] T114 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 14: Final Completion

**Purpose**: Final commit and completion claim

### 14.1 Final Commit

- [X] T115 **Commit all spec work** to feature branch `043-effects-section`
- [X] T116 **Verify all tests pass** (run full dsp_tests suite)

### 14.2 Completion Claim

- [X] T117 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup (Phase 1) - BLOCKS all user stories
- **User Stories (Phase 3-9)**: All depend on Foundational (Phase 2) completion
  - US1: Stereo Effects Chain Processing (P1) - No dependencies on other stories
  - US2: Selectable Delay Type (P1) - No dependencies on other stories (can run parallel with US1)
  - US3: Spectral Freeze (P2) - Depends on US1 (chain must process)
  - US4: Reverb Integration (P2) - Depends on US1 (chain must process)
  - US5: Click-Free Switching (P2) - Depends on US2 (delay type selection must work)
  - Latency Compensation (Phase 8) - No dependencies on other stories (but needed for SC-007)
  - US6: Individual Effect Bypass (P3) - Depends on US1 (chain must process) and US3 (freeze bypass)
- **Polish (Phase 10)**: Depends on all desired user stories being complete
- **Static Analysis (Phase 11)**: Before completion verification
- **Architecture Docs (Phase 12)**: Before claiming completion
- **Completion Verification (Phase 13)**: Final verification phase
- **Final Completion (Phase 14)**: Final commit and completion claim

### User Story Dependencies

```
Foundation (Phase 2)
  |
  +-- US1 (Chain Processing - P1) ----+
  |                                    |
  +-- US2 (Delay Selection - P1)      +-- US3 (Freeze - P2)
  |     |                             +-- US4 (Reverb - P2)
  |     +-- US5 (Crossfade - P2)      +-- US6 (Bypass - P3)
  |
  +-- Latency Comp (Phase 8 - P1)
```

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Core implementation
3. **Verify tests pass**: After implementation
4. **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
5. **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Phase 2 (Foundational)**: T002-T007 can run in parallel (different setup tasks)
- **User Story Tests**: All test tasks within a story marked [P] can run in parallel
- **User Stories**: US1 + US2 + Latency Comp (Phase 8) can start in parallel after Phase 2
- **Polish Tasks**: T094-T100 marked [P] can run in parallel

---

## Parallel Example: User Story 2

```bash
# Launch all tests for User Story 2 together:
Task: "Write failing test for FR-009: setDelayType() selects active delay"
Task: "Write failing test for FR-014: setDelayType(same) is no-op"
Task: "Write failing test for FR-015: delay parameter forwarding"
Task: "Write failing test for FR-017: delay time forwarding per type"
Task: "Write failing test for FR-016: setDelayTempo() updates BlockContext"
Task: "Write failing test for all 5 delay types process correctly"

# After tests written, launch parallel implementation tasks:
Task: "Implement setDelayFeedback() (forward to all 5 delays)"
Task: "Implement setDelayMix() (forward to all 5 delays)"
Task: "Implement setDelayTempo() (store in tempoBPM_)"
```

---

## Implementation Strategy

### MVP First (User Stories 1, 2, Latency Comp Only)

1. Complete Phase 1: Setup (add enum)
2. Complete Phase 2: Foundational (skeleton class)
3. Complete Phase 3: User Story 1 (chain processes in order)
4. Complete Phase 4: User Story 2 (5 delay types selectable)
5. Complete Phase 8: Latency Compensation (constant latency)
6. **STOP and VALIDATE**: Test chain with all 5 delay types, constant latency
7. Deploy/demo if ready

This gives a functional effects chain with all delay types and latency compensation (core value).

### Incremental Delivery

1. MVP (US1 + US2 + Latency) → Chain with all delay types and latency
2. Add US5 (Click-Free Switching) → Smooth type transitions
3. Add US3 (Freeze) → Spectral freeze capability
4. Add US4 (Reverb) → Full chain with reverb
5. Add US6 (Individual Bypass) → Per-effect enable/disable
6. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together (Phase 1-2)
2. Once Foundational is done:
   - Developer A: User Story 1 (Chain Processing)
   - Developer B: User Story 2 (Delay Selection)
   - Developer C: Latency Compensation (Phase 8)
3. After US1/US2 complete:
   - Developer A: User Story 5 (Crossfade)
   - Developer B: User Story 3 (Freeze)
   - Developer C: User Story 4 (Reverb)
4. After US3/US4 complete: User Story 6 (Individual Bypass)
5. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files or independent work, no dependencies
- [Story] label (US1, US2, etc.) maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Verify noexcept on all runtime methods per FR-028 (Phase 10 task T100)
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Run clang-tidy static analysis before completion verification
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- **CRITICAL API GOTCHA**: FreezeMode and SpectralDelay mix parameters now use 0-1 scale (NOT 0-100) per recent refactor - use values directly, do NOT multiply by 100
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Summary

- **Total Tasks**: 126 tasks (T001-T117 + T093a-T093i)
- **User Stories**: 6 user stories (US1-US6) + latency compensation (FR-based)
- **Phases**: 14 phases (Setup → Foundation → US1-US6 → Polish → Static Analysis → Arch Docs → Verification → Final)
- **Parallel Opportunities**:
  - Foundation: 6 tasks can run in parallel
  - US1 tests: 3 parallel
  - US2 tests: 6 parallel
  - US3 tests: 7 parallel
  - US4 tests: 5 parallel
  - US5 tests: 6 parallel
  - Latency comp tests: 4 parallel
  - US6 bypass tests: 3 parallel
  - Polish: 5 parallel
- **Independent Test Criteria**: Each user story has clear acceptance criteria from spec.md
- **Suggested MVP Scope**: US1 + US2 + Latency Comp (chain processing + all delay types + constant latency)
- **Format Validation**: All tasks follow `- [ ] [ID] [P?] [Story?] Description with file path` format

**Implementation Strategy**: Complete foundation first (blocks all stories), then implement US1+US2+Latency Comp for MVP (core chain with all delays), then add US5 (crossfade), US3 (freeze), US4 (reverb), US6 (individual bypass) for full feature set.
