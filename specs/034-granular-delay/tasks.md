# Tasks: Granular Delay

**Input**: Design documents from `specs/034-granular-delay/`
**Prerequisites**: plan.md (required), spec.md (required), research.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by DSP layer and user story to enable independent implementation and testing.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow:

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` is in context window
2. **Write Failing Tests**: Create test file and write tests that FAIL
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and file structure

- [ ] T001 Create Layer 0 file stubs in src/dsp/core/pitch_utils.h and src/dsp/core/grain_envelope.h
- [ ] T002 Create Layer 1 file stub in src/dsp/primitives/grain_pool.h
- [ ] T003 [P] Create Layer 2 file stubs in src/dsp/processors/grain_scheduler.h and src/dsp/processors/grain_processor.h
- [ ] T004 [P] Create Layer 3 file stub in src/dsp/systems/granular_engine.h
- [ ] T005 [P] Create Layer 4 file stub in src/dsp/features/granular_delay.h
- [ ] T006 Add new source files to CMakeLists.txt

---

## Phase 2: Foundational - Layer 0 (Blocking Prerequisites)

**Purpose**: Core utilities that ALL layers depend on - MUST complete before any user story

**CRITICAL**: No Layer 1+ work can begin until this phase is complete

### 2.1 Pre-Implementation (MANDATORY)

- [ ] T007 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 2.2 Tests for Layer 0 (Write FIRST - Must FAIL)

- [ ] T008 [P] Write tests for semitonesToRatio/ratioToSemitones in tests/unit/core/test_pitch_utils.cpp
- [ ] T009 [P] Write tests for GrainEnvelope::generate and lookup in tests/unit/core/test_grain_envelope.cpp

### 2.3 Implementation for Layer 0

- [ ] T010 [P] Implement semitonesToRatio() constexpr in src/dsp/core/pitch_utils.h
  - Formula: ratio = 2^(semitones/12)
  - +12 semitones = 2.0, -12 = 0.5, 0 = 1.0
- [ ] T011 [P] Implement ratioToSemitones() constexpr in src/dsp/core/pitch_utils.h
  - Formula: semitones = 12 * log2(ratio)
- [ ] T012 [P] Implement GrainEnvelopeType enum in src/dsp/core/grain_envelope.h
  - Hann, Trapezoid, Sine, Blackman
- [ ] T013 [P] Implement GrainEnvelope::generate() in src/dsp/core/grain_envelope.h
  - Use existing Window::generateHann for Hann type
  - Implement Trapezoid with attack/sustain/decay ratios
  - Implement Sine (half-cosine) for pitch shifting
  - Implement Blackman for low sidelobe
- [ ] T014 [P] Implement GrainEnvelope::lookup() with linear interpolation in src/dsp/core/grain_envelope.h

### 2.4 Verification and Commit

- [ ] T015 Verify all Layer 0 tests pass
- [ ] T016 **Commit completed Layer 0 work**

**Checkpoint**: Layer 0 complete - Layer 1 can now begin

---

## Phase 3: Foundational - Layer 1 (Blocking Prerequisites)

**Purpose**: Grain primitive that Layer 2 depends on - MUST complete before user stories

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T017 **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 3.2 Tests for Layer 1 (Write FIRST - Must FAIL)

- [ ] T018 Write tests for Grain struct in tests/unit/primitives/test_grain_pool.cpp
  - Default initialization
  - Field assignments
- [ ] T019 Write tests for GrainPool in tests/unit/primitives/test_grain_pool.cpp
  - acquireGrain returns valid grain
  - releaseGrain returns grain to pool
  - activeGrains returns correct list
  - activeCount tracks correctly
  - Voice stealing when pool exhausted (oldest grain)
  - prepare/reset lifecycle
  - Max 64 grains constraint

### 3.3 Implementation for Layer 1

- [ ] T020 Implement Grain struct in src/dsp/primitives/grain_pool.h
  - Fields: readPosition, playbackRate, envelopePhase, envelopeIncrement
  - Fields: amplitude, panL, panR, active, reverse, startSample
- [ ] T021 Implement GrainPool::prepare() in src/dsp/primitives/grain_pool.h
- [ ] T022 Implement GrainPool::reset() in src/dsp/primitives/grain_pool.h
- [ ] T023 Implement GrainPool::acquireGrain() with voice stealing in src/dsp/primitives/grain_pool.h
  - Find first inactive grain OR steal oldest active grain
- [ ] T024 Implement GrainPool::releaseGrain() in src/dsp/primitives/grain_pool.h
- [ ] T025 Implement GrainPool::activeGrains() returning std::span in src/dsp/primitives/grain_pool.h
- [ ] T026 Implement GrainPool::activeCount() in src/dsp/primitives/grain_pool.h

### 3.4 Verification and Commit

- [ ] T027 Verify all Layer 1 tests pass
- [ ] T028 **Commit completed Layer 1 work**

**Checkpoint**: Layers 0-1 complete - User story implementation can now begin

---

## Phase 4: User Story 1 - Basic Granular Texture (Priority: P1) MVP

**Goal**: Transform audio into granular textures with grain size (10-500ms) and density (1-100 Hz) control

**Independent Test**: Feed audio, adjust grain size from 10ms to 500ms and density from 1-50 grains/sec, verify texture changes

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T029 [US1] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 4.2 Tests for User Story 1 (Write FIRST - Must FAIL)

- [ ] T030 [P] [US1] Write tests for GrainScheduler in tests/unit/processors/test_grain_scheduler.cpp
  - setDensity sets grains per second
  - process() returns true at correct intervals
  - Asynchronous mode produces stochastic timing
  - seed() produces reproducible sequence
- [ ] T031 [P] [US1] Write tests for GrainProcessor in tests/unit/processors/test_grain_processor.cpp
  - initializeGrain sets correct envelope increment based on grain size
  - processGrain applies envelope correctly
  - isGrainComplete detects end of grain
  - Pan law produces correct L/R gains
- [ ] T032 [P] [US1] Write tests for GranularEngine in tests/unit/systems/test_granular_engine.cpp
  - prepare/reset lifecycle
  - setGrainSize/setDensity work correctly
  - process generates textured output
  - activeGrainCount tracks grains
- [ ] T033 [P] [US1] Write tests for GranularDelay in tests/unit/features/test_granular_delay.cpp
  - prepare/reset lifecycle
  - setGrainSize/setDensity/setDelayTime work
  - setDryWet blends correctly
  - process produces output
- [ ] T033b [P] [US1] Write tests for pan spray (FR-022) in tests/unit/systems/test_granular_engine.cpp
  - setPanSpray(0) -> all grains center panned
  - setPanSpray(1.0) -> grains distributed across stereo field
  - Verify stereo output differs from mono when pan spray > 0

### 4.3 Implementation for User Story 1

**Layer 2: GrainScheduler**
- [ ] T034 [US1] Implement GrainScheduler class in src/dsp/processors/grain_scheduler.h
  - Include Xorshift32 for stochastic timing
- [ ] T035 [US1] Implement GrainScheduler::prepare() and reset() in src/dsp/processors/grain_scheduler.h
- [ ] T036 [US1] Implement GrainScheduler::setDensity() in src/dsp/processors/grain_scheduler.h
- [ ] T037 [US1] Implement GrainScheduler::process() returning trigger decision in src/dsp/processors/grain_scheduler.h
  - Asynchronous: decrement counter, trigger when <= 0, reset with random interonset
  - Formula: interonset = sampleRate / density (with optional jitter)

**Layer 2: GrainProcessor**
- [ ] T038 [P] [US1] Implement GrainProcessor class in src/dsp/processors/grain_processor.h
  - Include envelope table vector (pre-allocated)
- [ ] T039 [P] [US1] Implement GrainProcessor::prepare() with envelope table allocation in src/dsp/processors/grain_processor.h
- [ ] T040 [P] [US1] Implement GrainProcessor::initializeGrain() in src/dsp/processors/grain_processor.h
  - Calculate envelope increment from grain size
  - Convert pitch semitones to playback rate using semitonesToRatio
  - Calculate pan gains from pan value
- [ ] T041 [P] [US1] Implement GrainProcessor::processGrain() in src/dsp/processors/grain_processor.h
  - Read from delay buffer at grain position with interpolation
  - Apply envelope from lookup table
  - Apply pan gains
  - Advance envelope phase and read position
  - Return {left, right} output
- [ ] T042 [P] [US1] Implement GrainProcessor::isGrainComplete() in src/dsp/processors/grain_processor.h

**Layer 3: GranularEngine**
- [ ] T043 [US1] Implement GranularEngine class in src/dsp/systems/granular_engine.h
  - Compose: DelayLine (L/R), GrainPool, GrainScheduler, GrainProcessor
  - Include parameter smoothers
- [ ] T044 [US1] Implement GranularEngine::prepare() in src/dsp/systems/granular_engine.h
  - Prepare all child components
  - Configure smoothers
- [ ] T045 [US1] Implement GranularEngine::reset() in src/dsp/systems/granular_engine.h
- [ ] T046 [US1] Implement GranularEngine parameter setters in src/dsp/systems/granular_engine.h
  - setGrainSize, setDensity, setPosition, setPitch (basic)
- [ ] T047 [US1] Implement GranularEngine::process() in src/dsp/systems/granular_engine.h
  - Write input to delay buffers
  - Check scheduler for new grain trigger
  - If triggered: acquire grain, initialize with current params
  - Process all active grains, sum outputs
  - Release completed grains

**Layer 4: GranularDelay**
- [ ] T048 [US1] Implement GranularDelay class in src/dsp/features/granular_delay.h
  - Compose: GranularEngine
  - Include: output smoothers (dryWet, gain)
- [ ] T049 [US1] Implement GranularDelay::prepare() and reset() in src/dsp/features/granular_delay.h
- [ ] T050 [US1] Implement GranularDelay parameter setters in src/dsp/features/granular_delay.h
  - setGrainSize, setDensity, setDelayTime, setDryWet, setOutputGain
- [ ] T051 [US1] Implement GranularDelay::process() block processing in src/dsp/features/granular_delay.h
  - For each sample: process through engine
  - Apply dry/wet mix
  - Apply output gain

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T052 [US1] **Verify IEEE 754 compliance**: Check test files for std::isnan/isfinite/isinf -> add to -fno-fast-math in tests/CMakeLists.txt

### 4.5 Commit (MANDATORY)

- [ ] T053 [US1] Verify all User Story 1 tests pass
- [ ] T054 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Basic granular delay working - can create textured output from input

---

## Phase 5: User Story 2 - Per-Grain Pitch Shifting (Priority: P2)

**Goal**: Apply pitch shifting to grains with +/-24 semitones and optional spray/randomization

**Independent Test**: Set pitch to +12 semitones, verify octave up; enable 50% spray, verify randomized pitches

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T055 [US2] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 5.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T056 [P] [US2] Write tests for pitch accuracy in tests/unit/processors/test_grain_processor.cpp
  - +12 semitones = exactly 2.0x playback rate
  - -12 semitones = exactly 0.5x playback rate
  - 0 semitones = exactly 1.0x rate
- [ ] T057 [P] [US2] Write tests for pitch spray in tests/unit/systems/test_granular_engine.cpp
  - setPitchSpray(0) -> all grains same pitch
  - setPitchSpray(0.5) -> grains within +/-12 semitones
- [ ] T058 [P] [US2] Write pitch integration tests in tests/unit/features/test_granular_delay.cpp
  - setPitch/setPitchSpray work correctly

### 5.3 Implementation for User Story 2

- [ ] T059 [US2] Implement GranularEngine::setPitchSpray() in src/dsp/systems/granular_engine.h
- [ ] T060 [US2] Update GranularEngine::process() to apply pitch spray when initializing grains
  - pitchOffset = pitchSpray * 24.0f * (rng.nextFloat())
  - effectivePitch = basePitch + pitchOffset
- [ ] T061 [US2] Implement GranularDelay::setPitch() and setPitchSpray() in src/dsp/features/granular_delay.h

### 5.4 Verification and Commit

- [ ] T062 [US2] Verify all User Story 2 tests pass
- [ ] T063 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Pitch shifting working - can create shimmer-like effects

---

## Phase 6: User Story 3 - Position Randomization (Priority: P2)

**Goal**: Control grain tap position with delay time and position spray for time smearing

**Independent Test**: Set 500ms delay with 0% spray -> coherent echo; 100% spray -> scattered texture

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T064 [US3] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 6.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T065 [P] [US3] Write tests for position spray in tests/unit/systems/test_granular_engine.cpp
  - setPosition sets base delay time
  - setPositionSpray(0) -> all grains tap same position
  - setPositionSpray(1.0) -> grains tap within +/-100% of delay time
- [ ] T066 [P] [US3] Write position integration tests in tests/unit/features/test_granular_delay.cpp
  - setDelayTime/setPositionSpray work correctly
  - Maximum 2000ms delay time supported

### 6.3 Implementation for User Story 3

- [ ] T067 [US3] Implement GranularEngine::setPositionSpray() in src/dsp/systems/granular_engine.h
- [ ] T068 [US3] Update GranularEngine::process() to apply position spray when initializing grains
  - positionOffset = positionSpray * positionMs * rng.nextFloat()
  - effectivePosition = basePosition + positionOffset
  - Clamp to buffer bounds
- [ ] T069 [US3] Implement GranularDelay::setPositionSpray() in src/dsp/features/granular_delay.h

### 6.4 Verification and Commit

- [ ] T070 [US3] Verify all User Story 3 tests pass
- [ ] T071 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Position randomization working - can create time-smeared textures

---

## Phase 7: User Story 4 - Reverse Grain Playback (Priority: P3)

**Goal**: Random reverse playback of grains based on probability 0-100%

**Independent Test**: Set reverse prob 100% -> all grains backward; 50% -> ~half backward

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T072 [US4] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 7.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T073 [P] [US4] Write tests for reverse grain processing in tests/unit/processors/test_grain_processor.cpp
  - Grain with reverse=true reads backward through buffer
  - No clicks at grain boundaries
- [ ] T074 [P] [US4] Write tests for reverse probability in tests/unit/systems/test_granular_engine.cpp
  - setReverseProbability(0) -> 0% grains reversed
  - setReverseProbability(1.0) -> 100% grains reversed
  - setReverseProbability(0.5) -> ~50% grains reversed (statistical)
- [ ] T075 [P] [US4] Write reverse integration tests in tests/unit/features/test_granular_delay.cpp

### 7.3 Implementation for User Story 4

- [ ] T076 [US4] Update GrainProcessor::processGrain() to handle reverse playback in src/dsp/processors/grain_processor.h
  - If grain.reverse: decrement readPosition instead of increment
  - Read position should start at grain end, move toward start
- [ ] T077 [US4] Implement GranularEngine::setReverseProbability() in src/dsp/systems/granular_engine.h
- [ ] T078 [US4] Update GranularEngine::process() to set reverse flag based on probability
  - reverse = rng.nextUnipolar() < reverseProbability
- [ ] T079 [US4] Implement GranularDelay::setReverseProbability() in src/dsp/features/granular_delay.h

### 7.4 Verification and Commit

- [ ] T080 [US4] Verify all User Story 4 tests pass
- [ ] T081 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Reverse playback working - can create otherworldly textures

---

## Phase 8: User Story 5 - Granular Freeze (Priority: P3)

**Goal**: Freeze delay buffer for infinite sustain drone textures

**Independent Test**: Enable freeze, stop input -> grains continue indefinitely from frozen buffer

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T082 [US5] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 8.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T083 [P] [US5] Write tests for freeze mode in tests/unit/systems/test_granular_engine.cpp
  - setFreeze(true) -> buffer stops updating
  - setFreeze(false) -> buffer resumes updating
  - Freeze transition uses crossfade (no clicks)
- [ ] T084 [P] [US5] Write freeze integration tests in tests/unit/features/test_granular_delay.cpp
  - setFreeze works correctly
  - Grains sustain at least 60 seconds when frozen (SC-004)

### 8.3 Implementation for User Story 5

- [ ] T085 [US5] Implement GranularEngine::setFreeze() in src/dsp/systems/granular_engine.h
  - Set frozen_ flag
  - Trigger freezeCrossfade_ ramp
- [ ] T086 [US5] Update GranularEngine::process() to handle freeze in src/dsp/systems/granular_engine.h
  - If not frozen: write to delay buffers
  - If frozen: skip delay buffer writes
  - Apply crossfade during transition (freezeCrossfade_ ramp)
- [ ] T087 [US5] Implement GranularDelay::setFreeze() in src/dsp/features/granular_delay.h

### 8.4 Verification and Commit

- [ ] T088 [US5] Verify all User Story 5 tests pass
- [ ] T089 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Freeze mode working - can create infinite sustain drones

---

## Phase 9: User Story 6 - Feedback Path (Priority: P4)

**Goal**: Granulated output fed back to input for evolving textures (0-120%)

**Independent Test**: Set 80% feedback -> grains become re-granulated, building density

### 9.1 Pre-Implementation (MANDATORY)

- [ ] T090 [US6] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 9.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T091 [P] [US6] Write tests for feedback path in tests/unit/features/test_granular_delay.cpp
  - setFeedback(0) -> no feedback
  - setFeedback(0.5) -> gradual decay
  - setFeedback(1.0) -> sustains indefinitely
  - setFeedback(1.2) -> soft-limited, no runaway

### 9.3 Implementation for User Story 6

- [ ] T092 [US6] Implement GranularDelay::setFeedback() in src/dsp/features/granular_delay.h
- [ ] T093 [US6] Update GranularDelay::process() to include feedback path in src/dsp/features/granular_delay.h
  - Mix input with feedbackL_/feedbackR_ before sending to engine
  - Store granular output for next iteration feedback
  - Soft-limit feedback signal with tanh when > 1.0

### 9.4 Verification and Commit

- [ ] T094 [US6] Verify all User Story 6 tests pass
- [ ] T095 [US6] **Commit completed User Story 6 work**

**Checkpoint**: Feedback path working - all user stories complete

---

## Phase 10: VST3 Integration

**Purpose**: Connect GranularDelay to VST3 processor and controller

### 10.1 Pre-Implementation

- [ ] T096 **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 10.2 VST3 Integration Tasks

- [ ] T097 Add GranularDelay parameter IDs to src/plugin_ids.h
  - kGranularGrainSizeId, kGranularDensityId, kGranularDelayTimeId
  - kGranularPitchId, kGranularPitchSprayId
  - kGranularPositionSprayId, kGranularReverseProbId
  - kGranularFreezeId, kGranularFeedbackId
  - kGranularDryWetId, kGranularOutputGainId
- [ ] T098 Add GranularDelay instance to Processor class in src/processor/processor.h
- [ ] T099 Integrate GranularDelay::prepare() in Processor::setupProcessing in src/processor/processor.cpp
- [ ] T100 Integrate GranularDelay::process() in Processor::process in src/processor/processor.cpp
- [ ] T101 Add parameter handling in Processor::processParameterChanges in src/processor/processor.cpp
- [ ] T102 Register parameters in Controller::initialize in src/controller/controller.cpp
- [ ] T103 Add state save/load for GranularDelay parameters in processor and controller

### 10.3 Verification and Commit

- [ ] T104 Build and test plugin loads correctly
- [ ] T105 **Commit VST3 integration work**

---

## Phase 11: Polish & Cross-Cutting Concerns

**Purpose**: Performance, documentation, benchmarks

### 11.1 Performance Verification

- [ ] T106 [P] Create CPU benchmark test in tests/benchmark_granular_delay.cpp
  - Target: <3% CPU at 44.1kHz stereo with 32 grains (SC-005)
- [ ] T107 Add benchmark target to tests/CMakeLists.txt
- [ ] T108 Run benchmark and document results
- [ ] T108b Verify tests pass at multiple sample rates (SC-007)
  - Run test suite with sampleRate = 44100, 48000, 96000, 192000
  - Verify grain timing and pitch accuracy at each rate

### 11.2 Code Quality

- [ ] T109 [P] Review all new code for constitution compliance
  - No allocations in process paths
  - noexcept on all processing functions
  - Layer dependencies respected
- [ ] T110 [P] Add parameter smoothing verification test
  - Verify all parameter changes are click-free (SC-006)

---

## Phase 12: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation

### 12.1 Architecture Documentation Update

- [ ] T111 **Update ARCHITECTURE.md** with new components:
  - Layer 0: pitch_utils.h, grain_envelope.h
  - Layer 1: grain_pool.h
  - Layer 2: grain_scheduler.h, grain_processor.h
  - Layer 3: granular_engine.h
  - Layer 4: granular_delay.h
  - Include public API summaries and "when to use"

### 12.2 Final Commit

- [ ] T112 **Commit ARCHITECTURE.md updates**

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met

### 13.1 Requirements Verification

- [ ] T113 **Review ALL FR-xxx requirements** (FR-001 through FR-026) from spec.md
- [ ] T113b **Perform A/B listening test** (SC-001): Compare granular delay output to regular delay
  - Verify granular texture is audibly distinct from time-domain echo
  - Document test conditions (grain size, density, pitch settings used)
- [ ] T114 **Review ALL SC-xxx success criteria** (SC-001 through SC-008) from spec.md
- [ ] T115 **Search for cheating patterns**:
  - No `// placeholder` or `// TODO` comments
  - No test thresholds relaxed from spec
  - No features quietly removed

### 13.2 Fill Compliance Table

- [ ] T116 **Update spec.md Implementation Verification section** with compliance for each requirement
- [ ] T117 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 13.3 Honest Self-Check

- [ ] T118 **All self-check questions answered "no"** (or gaps documented):
  1. Did I change ANY test threshold from spec?
  2. Are there ANY placeholder/TODO comments?
  3. Did I remove ANY features without user approval?
  4. Would the spec author consider this "done"?
  5. Would I feel cheated as user?

---

## Phase 14: Final Completion

- [ ] T119 **Verify all tests pass** (run full test suite)
- [ ] T120 **Final commit** to feature branch
- [ ] T121 **Claim completion** (only if all requirements MET)

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup)
    ↓
Phase 2 (Layer 0 - BLOCKING)
    ↓
Phase 3 (Layer 1 - BLOCKING)
    ↓
Phase 4-9 (User Stories - can parallelize after Phase 3)
    ↓
Phase 10 (VST3 Integration)
    ↓
Phase 11-14 (Polish, Docs, Verification)
```

### User Story Dependencies

| Story | Depends On | Can Parallel With |
|-------|------------|-------------------|
| US1 (P1) | Layers 0-1 | - |
| US2 (P2) | US1 | US3 |
| US3 (P2) | US1 | US2 |
| US4 (P3) | US1 | US5, US6 |
| US5 (P3) | US1 | US4, US6 |
| US6 (P4) | US1 | US4, US5 |

### MVP Path (User Story 1 Only)

1. Phase 1: Setup (T001-T006)
2. Phase 2: Layer 0 (T007-T016)
3. Phase 3: Layer 1 (T017-T028)
4. Phase 4: User Story 1 (T029-T054)
5. **STOP**: Working granular delay with basic controls

---

## Parallel Execution Examples

### Layer 0 Tests (Parallel)

```bash
# Can run simultaneously:
Task: T008 - Write pitch_utils tests
Task: T009 - Write grain_envelope tests
```

### Layer 2 Implementation (Parallel)

```bash
# Can run simultaneously after Layer 1:
Task: T038-T042 - GrainProcessor implementation
Task: T034-T037 - GrainScheduler implementation
```

### User Stories (Parallel after US1)

```bash
# With multiple developers after US1:
Developer A: User Story 2 (Pitch)
Developer B: User Story 3 (Position)
Developer C: User Story 4+5+6 (Reverse, Freeze, Feedback)
```

---

## Summary

| Metric | Count |
|--------|-------|
| **Total Tasks** | 124 |
| **Phase 1 (Setup)** | 6 |
| **Phase 2 (Layer 0)** | 10 |
| **Phase 3 (Layer 1)** | 12 |
| **Phase 4 (US1)** | 27 |
| **Phase 5 (US2)** | 9 |
| **Phase 6 (US3)** | 8 |
| **Phase 7 (US4)** | 10 |
| **Phase 8 (US5)** | 8 |
| **Phase 9 (US6)** | 6 |
| **Phase 10 (Integration)** | 10 |
| **Phases 11-14 (Polish)** | 18 |

**MVP Scope**: Complete through Phase 4 (User Story 1) = 55 tasks
**Full Scope**: All 124 tasks across 14 phases
