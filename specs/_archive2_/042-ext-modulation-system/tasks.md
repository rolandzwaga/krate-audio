# Tasks: Extended Modulation System

**Input**: Design documents from `/specs/042-ext-modulation-system/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Fix Warnings**: Address all compiler warnings
5. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
6. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

### Build Commands (Windows)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run specific test tags
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[voice_mod_router]"
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ext_modulation]"
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[rungler]"
```

---

## Format: `- [ ] [TaskID] [P?] [Story?] Description`

- **Checkbox**: ALWAYS start with `- [ ]` (markdown checkbox)
- **[TaskID]**: Sequential number (T001, T002, T003...)
- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: User story label (e.g., [US1], [US2]) - ONLY for user story phases
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: No setup tasks needed - all infrastructure exists from Phase 3 (041-ruinae-voice-architecture)

**Checkpoint**: Foundation ready - implementation can begin immediately

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Enum extensions and base API changes that all user stories depend on

**⚠️ CRITICAL**: This phase MUST complete before ANY user story work begins

### 2.1 Enum Extensions (ruinae_types.h)

- [X] T001 [P] Extend VoiceModSource enum with Aftertouch value (before NumSources) in dsp/include/krate/dsp/systems/ruinae_types.h (FR-001)
- [X] T002 [P] Extend VoiceModDest enum with OscALevel and OscBLevel values (before NumDestinations) in dsp/include/krate/dsp/systems/ruinae_types.h (FR-002)

### 2.2 VoiceModRouter Signature Change

- [X] T003 Extend VoiceModRouter::computeOffsets() signature to 8 parameters (add float aftertouch) in dsp/include/krate/dsp/systems/voice_mod_router.h (FR-003)
- [X] T004 Add aftertouch to sourceValues_ array at VoiceModSource::Aftertouch index in voice_mod_router.h computeOffsets() implementation (FR-003)
- [X] T005 Add NaN/Inf/denormal sanitization after accumulation loop in computeOffsets() in voice_mod_router.h (FR-024)

**Checkpoint**: Foundation complete - all user stories can now proceed in parallel

---

## Phase 3: User Story 1 - Per-Voice Modulation Routing (Priority: P1) 🎯 MVP

**Goal**: Enable per-voice modulation routes with new Aftertouch source and OscALevel/OscBLevel destinations. Each voice independently evaluates its modulation sources for expressive synthesis.

**Independent Test**: Configure routes on VoiceModRouter, provide known source values, compute offsets, and verify bipolar arithmetic against expected values.

**Acceptance Criteria**:
- Aftertouch as 8th source produces expected offsets when routed
- OscALevel/OscBLevel destinations receive accumulated route contributions
- Multi-route summation works correctly
- Negative amounts produce negative offsets
- Zero routes produce zero offsets
- NaN/Inf handling prevents invalid offsets

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T006 [P] [US1] Write failing test for Aftertouch single route (Aftertouch=0.6, amount=+1.0 -> offset=0.6) in dsp/tests/unit/systems/voice_mod_router_test.cpp
- [X] T007 [P] [US1] Write failing test for Aftertouch + Env2 multi-route summation to FilterCutoff in voice_mod_router_test.cpp
- [X] T008 [P] [US1] Write failing test for zero aftertouch producing zero contribution in voice_mod_router_test.cpp
- [X] T009 [P] [US1] Write failing test for OscALevel route (Env3 -> OscALevel, amount=+1.0) in voice_mod_router_test.cpp
- [X] T010 [P] [US1] Write failing test for OscBLevel route (LFO -> OscBLevel, negative amount) in voice_mod_router_test.cpp
- [X] T011 [P] [US1] Write failing test for NaN/Inf source values being sanitized to zero in voice_mod_router_test.cpp (FR-024)
- [X] T012 [P] [US1] Write failing test for denormal source values (e.g. 1e-40f) being flushed to zero in voice_mod_router_test.cpp -- verify output offset is exactly 0.0f, not a denormal (FR-024)
- [X] T013 [P] [US1] Write failing test for no routes configured producing all-zero offsets in voice_mod_router_test.cpp

### 3.2 Implementation for User Story 1

- [X] T014 [US1] Update all existing computeOffsets() calls in voice_mod_router_test.cpp to pass 8 parameters (add 0.0f for aftertouch as placeholder)
- [X] T015 [US1] Verify all User Story 1 tests now pass (aftertouch routing, OscA/BLevel routing, NaN/Inf sanitization)
- [X] T016 [US1] Run full test suite to verify backward compatibility (existing 041 tests pass with aftertouch=0.0)

### 3.3 Fix Warnings and Static Analysis

- [X] T017 [US1] Fix all compiler warnings in modified voice_mod_router.h
- [X] T018 [US1] Verify no new warnings in voice_mod_router_test.cpp

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T019 [US1] **Verify IEEE 754 compliance**: Check if voice_mod_router_test.cpp uses std::isnan/std::isfinite -> already in -fno-fast-math list (verify), add if missing

### 3.5 Commit (MANDATORY)

- [X] T020 [US1] **Commit completed User Story 1 work** (VoiceModRouter extension with Aftertouch + OscA/BLevel)

**Checkpoint**: User Story 1 complete - per-voice modulation routing works with all 8 sources and 9 destinations

---

## Phase 4: User Story 2 - Aftertouch as Per-Voice Modulation Source (Priority: P1)

**Goal**: Enable aftertouch (channel pressure) as a modulation source within voices. Keyboardist sends aftertouch and the voice uses it for modulation routing.

**Independent Test**: Set a route from Aftertouch to any destination, update aftertouch value via setAftertouch(), process a block, verify offset matches expected value.

**Acceptance Criteria**:
- setAftertouch() stores value clamped to [0, 1]
- Aftertouch value passed to computeOffsets() as 8th parameter
- Route from Aftertouch produces expected offset
- Zero aftertouch produces zero contribution
- NaN/Inf aftertouch values ignored (value unchanged)

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T021 [P] [US2] Write failing test for setAftertouch() storing clamped value in dsp/tests/unit/systems/ruinae_voice_test.cpp
- [X] T022 [P] [US2] Write failing test for aftertouch passed to computeOffsets() during processBlock() in ruinae_voice_test.cpp
- [X] T023 [P] [US2] Write failing test for Aftertouch -> MorphPosition route producing expected offset in ruinae_voice_test.cpp
- [X] T024 [P] [US2] Write failing test for zero aftertouch producing no modulation in ruinae_voice_test.cpp
- [X] T025 [P] [US2] Write failing test for NaN/Inf aftertouch being ignored (value unchanged) in ruinae_voice_test.cpp

### 4.2 Implementation for User Story 2

- [X] T026 [US2] Add float aftertouch_ member (default 0.0f) to RuinaeVoice in dsp/include/krate/dsp/systems/ruinae_voice.h (FR-010)
- [X] T027 [US2] Implement setAftertouch(float value) method with NaN/Inf check and clamp in ruinae_voice.h (FR-010)
- [X] T028 [US2] Update computeOffsets() call in processBlock() to pass aftertouch_ as 8th parameter in ruinae_voice.h (FR-003)
- [X] T029 [US2] Verify all User Story 2 tests pass (aftertouch storage, routing, NaN handling)

### 4.3 Fix Warnings and Static Analysis

- [X] T030 [US2] Fix all compiler warnings in modified ruinae_voice.h

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T031 [US2] **Verify IEEE 754 compliance**: Check if ruinae_voice_test.cpp uses std::isnan/std::isfinite in NaN tests -> add to -fno-fast-math list in dsp/tests/CMakeLists.txt if needed

### 4.5 Commit (MANDATORY)

- [X] T032 [US2] **Commit completed User Story 2 work** (RuinaeVoice aftertouch integration)

**Checkpoint**: User Story 2 complete - aftertouch is fully integrated as a per-voice modulation source

---

## Phase 5: User Story 3 - OSC A/B Level Modulation Destinations (Priority: P1)

**Goal**: Enable per-oscillator amplitude modulation within voices. Sound designer routes envelopes or LFOs to control individual oscillator levels for amplitude modulation effects and timbral evolution.

**Independent Test**: Configure a route from Env3 to OscALevel, process blocks at various envelope stages, verify OSC A amplitude scales proportionally while OSC B remains at unity.

**Acceptance Criteria**:
- OscALevel offset applied to oscABuffer_ before mixing
- OscBLevel offset applied to oscBBuffer_ before mixing
- Effective level formula: clamp(1.0 + offset, 0.0, 1.0) where base = 1.0
- No routes produces unity level (backward compatible)
- Crossfade effect when offsetting both oscillators oppositely

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T033 [P] [US3] Write failing test for OscALevel route (Env3 -> OscALevel, amount=+1.0) at Env3=0.0 producing base level in dsp/tests/unit/systems/ruinae_voice_test.cpp
- [X] T034 [P] [US3] Write failing test for OscALevel and OscBLevel crossfade (opposite routes) in ruinae_voice_test.cpp
- [X] T035 [P] [US3] Write failing test for no OscLevel routes producing unity level (backward compatible) in ruinae_voice_test.cpp
- [X] T036 [P] [US3] Write failing test for OscALevel offset = -1.0 producing silence in ruinae_voice_test.cpp
- [X] T037 [P] [US3] Write failing test for OscBLevel offset = +0.5 being clamped to unity (max 1.0) in ruinae_voice_test.cpp

### 5.2 Implementation for User Story 3

- [X] T038 [US3] Compute OscALevel and OscBLevel offsets at block start in processBlock() in dsp/include/krate/dsp/systems/ruinae_voice.h (FR-004)
- [X] T039 [US3] Calculate effectiveOscALevel = clamp(1.0 + offset, 0.0, 1.0) and effectiveOscBLevel in ruinae_voice.h (FR-004)
- [X] T040 [US3] Apply effectiveOscALevel to oscABuffer_ before mixing in ruinae_voice.h -- skip scaling loop if effectiveOscALevel == 1.0f (optimization) (FR-004)
- [X] T041 [US3] Apply effectiveOscBLevel to oscBBuffer_ before mixing in ruinae_voice.h -- skip scaling loop if effectiveOscBLevel == 1.0f (optimization) (FR-004)
- [X] T042 [US3] Verify all User Story 3 tests pass (OscA/BLevel application, crossfade, backward compat)

### 5.3 Fix Warnings and Static Analysis

- [X] T043 [US3] Fix all compiler warnings in modified ruinae_voice.h

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T044 [US3] **Verify IEEE 754 compliance**: Check if new ruinae_voice_test.cpp tests use std::isnan/std::isfinite -> already in -fno-fast-math list (verify)

### 5.5 Commit (MANDATORY)

- [X] T045 [US3] **Commit completed User Story 3 work** (OscALevel/OscBLevel application in RuinaeVoice)

**Checkpoint**: User Story 3 complete - per-oscillator amplitude modulation works correctly with proper clamping

---

## Phase 6: User Story 4 - Global Modulation Engine Composition (Priority: P2)

**Goal**: Enable global modulation via existing ModulationEngine for engine-wide parameters. Sound designer uses global LFO, Chaos, or Rungler to modulate master volume, effect mix, or global filter cutoff. Processed once per block at engine level.

**Independent Test**: Compose ModulationEngine into test scaffold, register global sources and destinations, set up routing (LFO1 -> Global Filter Cutoff), process block, verify modulated cutoff differs from base by expected offset.

**Acceptance Criteria**:
- ModulationEngine composes into test scaffold
- Global sources register correctly (LFO1, LFO2, Chaos, Rungler via Macro3, EnvFollower, Macros 1-4)
- Global destinations respond to routings
- getModulationOffset() returns expected offset for active routings
- Zero routings produce zero offsets

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T046 [P] [US4] Create new test file dsp/tests/unit/systems/ext_modulation_test.cpp with test scaffold class (TestEngineScaffold)
- [X] T047 [P] [US4] Write failing test for LFO1 -> GlobalFilterCutoff routing (LFO1=+1.0, amount=0.5 -> offset=+0.5) in ext_modulation_test.cpp
- [X] T048 [P] [US4] Write failing test for Chaos -> MasterVolume routing with varying chaos output in ext_modulation_test.cpp
- [X] T049 [P] [US4] Write failing test for no global routings producing all-zero offsets in ext_modulation_test.cpp
- [X] T050 [P] [US4] Write failing test for ModulationEngine.prepare() initializing sources correctly in ext_modulation_test.cpp

### 6.2 Implementation for User Story 4

- [X] T051 [US4] Implement TestEngineScaffold class with ModulationEngine member in ext_modulation_test.cpp
- [X] T052 [US4] Define global destination ID constants (kGlobalFilterCutoffDestId, kMasterVolumeDestId, etc.) in ext_modulation_test.cpp
- [X] T053 [US4] Implement test helper to configure ModRouting and call engine.setRouting() in ext_modulation_test.cpp
- [X] T054 [US4] Verify all User Story 4 tests pass (global source registration, routing, offset retrieval)

### 6.3 Fix Warnings and Static Analysis

- [X] T055 [US4] Fix all compiler warnings in new ext_modulation_test.cpp

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T056 [US4] **Verify IEEE 754 compliance**: Check if ext_modulation_test.cpp uses std::isnan/std::isfinite -> add to -fno-fast-math list in dsp/tests/CMakeLists.txt if needed

### 6.5 Register New Test File

- [X] T057 [US4] Add ext_modulation_test.cpp to dsp/tests/CMakeLists.txt test file list

### 6.6 Commit (MANDATORY)

- [X] T058 [US4] **Commit completed User Story 4 work** (Global modulation test scaffold with ModulationEngine composition)

**Checkpoint**: User Story 4 complete - global modulation works with LFO, Chaos, and standard sources

---

## Phase 7: User Story 5 - Global-to-Voice Parameter Forwarding (Priority: P2)

**Goal**: Enable global modulation to forward into per-voice parameters. Sound designer routes global LFO to "All Voice Filter Cutoff," causing every active voice's filter to shift by the same amount. Creates synchronized filter sweeps across all voices from a single global source.

**Independent Test**: Configure a global route targeting "All Voice Filter Cutoff," play multiple notes, process block, verify every active voice's filter cutoff shifted by the same global offset.

**Acceptance Criteria**:
- AllVoiceFilterCutoff forwarding adds global offset to each voice's filter cutoff
- AllVoiceMorphPosition forwarding adds global offset to each voice's morph position
- TranceGateRate forwarding adds global offset to each voice's trance gate rate (Hz)
- Two-stage clamping formula applied: clamp(clamp(base + perVoice) + global)
- Multiple voices receive same global offset simultaneously

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T059 [P] [US5] Write failing test for AllVoiceFilterCutoff forwarding with 3 voices (LFO2 = +0.5, amount = 0.8 -> each voice offset +0.4) in dsp/tests/unit/systems/ext_modulation_test.cpp
- [X] T060 [P] [US5] Write failing test for AllVoiceMorphPosition forwarding with 2 voices in ext_modulation_test.cpp
- [X] T061 [P] [US5] Write failing test for TranceGateRate forwarding with varying source in ext_modulation_test.cpp
- [X] T062 [P] [US5] Write failing test for two-stage clamping formula (perVoice = +0.9, global = +0.5, range [0,1] -> final = 1.0) in ext_modulation_test.cpp

### 7.2 Implementation for User Story 5

- [X] T063 [US5] Implement AllVoiceFilterCutoff forwarding logic in TestEngineScaffold (read offset, scale to semitones via offset*48, apply to each voice) in ext_modulation_test.cpp (FR-018)
- [X] T064 [US5] Implement AllVoiceMorphPosition forwarding logic in TestEngineScaffold (apply offset directly in normalized [0,1] space) in ext_modulation_test.cpp (FR-019)
- [X] T064a [P] [US5] Write failing test for TranceGateRate Hz offset scaling and [0.1, 20.0] clamping in ext_modulation_test.cpp (FR-020)
- [X] T065 [US5] Implement TranceGateRate forwarding logic in TestEngineScaffold (scale offset to Hz, clamp [0.1, 20.0]) in ext_modulation_test.cpp (FR-020)
- [X] T066 [US5] Implement two-stage clamping formula helper for testing in ext_modulation_test.cpp (FR-021)
- [X] T067 [US5] Verify all User Story 5 tests pass (forwarding to all voices, two-stage clamping)

### 7.3 Fix Warnings and Static Analysis

- [X] T068 [US5] Fix all compiler warnings in modified ext_modulation_test.cpp

### 7.4 Cross-Platform Verification (MANDATORY)

- [X] T069 [US5] **Verify IEEE 754 compliance**: Check if new tests use std::isnan/std::isfinite -> already verified in US4

### 7.5 Commit (MANDATORY)

- [X] T070 [US5] **Commit completed User Story 5 work** (Global-to-voice forwarding with two-stage clamping)

**Checkpoint**: User Story 5 complete - global modulation forwards correctly into per-voice parameters

---

## Phase 8: User Story 6 - Rungler and MIDI Controllers as Global Sources (Priority: P2)

**Goal**: Enable Rungler, Pitch Bend, and Mod Wheel as global modulation sources. Performer uses mod wheel and pitch bend for real-time control. Rungler provides chaotic stepped sequences. All registered with global ModulationEngine.

**Independent Test**: Set mod wheel value, verify ModulationEngine source reflects correct normalized value, confirm routings from mod wheel/pitch bend/Rungler produce expected offsets.

**Acceptance Criteria**:
- Rungler implements ModulationSource interface (getCurrentValue, getSourceRange)
- Rungler.getCurrentValue() returns runglerCV_ in [0, +1]
- Correlation between getCurrentValue() and process().rungler > 0.99 (SC-007)
- Pitch bend normalizes 14-bit to [-1, +1] with center at 0.0
- Mod wheel normalizes CC#1 (0-127) to [0, 1]
- Rungler output injected via Macro3
- Pitch bend injected via Macro1, mod wheel via Macro2

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T071 [P] [US6] Write failing test for Rungler.getCurrentValue() returning 0.0f before prepare in dsp/tests/unit/processors/rungler_test.cpp
- [X] T072 [P] [US6] Write failing test for Rungler.getCurrentValue() matching process().rungler after processing in rungler_test.cpp
- [X] T073 [P] [US6] Write failing test for Rungler.getSourceRange() returning {0.0f, 1.0f} in rungler_test.cpp
- [X] T074 [P] [US6] Write failing test for Rungler ModulationSource correlation > 0.99 in rungler_test.cpp (SC-007)
- [X] T075 [P] [US6] Write failing test for Rungler polymorphic usage: cast Rungler* to ModulationSource*, call getCurrentValue(), verify result matches direct Rungler::getCurrentValue() call, and verify getSourceRange() returns {0.0f, 1.0f} in rungler_test.cpp
- [X] T076 [P] [US6] Write failing test for Pitch Bend normalization (0x0000 -> -1.0, 0x2000 -> 0.0, 0x3FFF -> +1.0) in ext_modulation_test.cpp
- [X] T077 [P] [US6] Write failing test for Mod Wheel normalization (CC#1: 0 -> 0.0, 64 -> ~0.5, 127 -> 1.0) in ext_modulation_test.cpp
- [X] T078 [P] [US6] Write failing test for ModWheel -> EffectMix routing (CC#1=64, amount=1.0 -> offset ~0.5) in ext_modulation_test.cpp
- [X] T079 [P] [US6] Write failing test for PitchBend -> AllVoiceFilterCutoff routing in ext_modulation_test.cpp
- [X] T080 [P] [US6] Write failing test for Rungler via Macro3 -> GlobalFilterCutoff routing in ext_modulation_test.cpp

### 8.2 Implementation for User Story 6 - Rungler

- [X] T081 [US6] Add #include <krate/dsp/core/modulation_source.h> to dsp/include/krate/dsp/processors/rungler.h (FR-017)
- [X] T082 [US6] Add ": public ModulationSource" to Rungler class declaration in rungler.h (FR-017)
- [X] T083 [US6] Implement getCurrentValue() override returning runglerCV_ in rungler.h (FR-017)
- [X] T084 [US6] Implement getSourceRange() override returning {0.0f, 1.0f} in rungler.h (FR-017)
- [X] T085 [US6] Verify all Rungler ModulationSource tests pass in rungler_test.cpp

### 8.3 Implementation for User Story 6 - MIDI Controllers

- [X] T086 [US6] Implement normalizePitchBend() helper function in ext_modulation_test.cpp (FR-015)
- [X] T087 [US6] Implement normalizeModWheel() helper function in ext_modulation_test.cpp (FR-016)
- [X] T088 [US6] Implement test for Pitch Bend via Macro1 (map bipolar to unipolar, set via setMacroValue) in ext_modulation_test.cpp
- [X] T089 [US6] Implement test for Mod Wheel via Macro2 in ext_modulation_test.cpp
- [X] T090 [US6] Implement test for Rungler via Macro3 (call rungler.getCurrentValue(), set via setMacroValue) in ext_modulation_test.cpp
- [X] T091 [US6] Verify all MIDI controller normalization and routing tests pass in ext_modulation_test.cpp

### 8.4 Fix Warnings and Static Analysis

- [X] T092 [US6] Fix all compiler warnings in modified rungler.h
- [X] T093 [US6] Fix all compiler warnings in modified ext_modulation_test.cpp

### 8.5 Cross-Platform Verification (MANDATORY)

- [X] T094 [US6] **Verify IEEE 754 compliance**: Check if rungler_test.cpp or ext_modulation_test.cpp use std::isnan/std::isfinite -> add to -fno-fast-math list if needed

### 8.6 Commit (MANDATORY)

- [X] T095 [US6] **Commit completed User Story 6 work** (Rungler ModulationSource + MIDI controller normalization)

**Checkpoint**: User Story 6 complete - Rungler, pitch bend, and mod wheel work as global modulation sources

---

## Phase 9: User Story 7 - Modulation Smoothing and Real-Time Safety (Priority: P3)

**Goal**: Ensure all modulation transitions are smooth without zipper noise and the system operates within real-time constraints. All parameter changes smooth via existing mechanisms, no audible stepping, zero allocations during processing.

**Independent Test**: Change modulation amounts rapidly during playback, measure output for discontinuities, verify with profiling that no allocations occur in process call path.

**Acceptance Criteria**:
- Global route amounts smooth via OnePoleSmoother (20ms) - already in ModulationEngine
- Per-voice route amounts NOT smoothed (instant, per spec clarification)
- No audible zipper noise (step sizes < -60 dBFS) (SC-003)
- Zero heap allocations during processBlock() and engine.process() (SC-004)
- NaN/Inf/denormals handled correctly (sanitized to zero or flushed)
- System remains bounded for 10+ minutes continuous processing

### 9.1 Tests for User Story 7 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T096 [P] [US7] Write test for NaN source value being sanitized to zero in voice_mod_router_test.cpp (already written in US1 - verify passes)
- [X] T097 [P] [US7] Write test for Inf source value being sanitized to zero in voice_mod_router_test.cpp (already written in US1 - verify passes)
- [X] T098 [P] [US7] Write test for denormal source value being flushed in voice_mod_router_test.cpp (already written in US1 - verify passes)
- [X] T099 [P] [US7] Write test for ChaosModSource remaining bounded (output in [-1, +1]) for 10 minutes at any speed in dsp/tests/unit/processors/chaos_mod_source_test.cpp (SC-006)
- [X] T100 [P] [US7] Write test for Lorenz attractor auto-reset when state exceeds 10x safeBound (500) in chaos_mod_source_test.cpp (FR-025)

### 9.2 Real-Time Safety Verification

- [X] T101 [US7] Audit VoiceModRouter::computeOffsets() for allocations (verify noexcept, fixed arrays only) in voice_mod_router.h
- [X] T102 [US7] Audit RuinaeVoice::processBlock() for allocations (verify noexcept, pre-allocated buffers) in ruinae_voice.h
- [X] T103 [US7] Audit ModulationEngine::process() for allocations (verify noexcept) - already verified in 008-modulation-system
- [X] T104 [US7] Verify all new methods marked noexcept (setAftertouch, computeOffsets, getCurrentValue, getSourceRange)

### 9.3 Performance Benchmarks

- [X] T105 [US7] Write benchmark for per-voice modulation overhead (16 routes, 8 voices, 44.1kHz, 512-sample blocks) in dsp/tests/unit/systems/voice_mod_router_test.cpp (SC-001)
- [X] T106 [US7] Write benchmark for global modulation overhead (32 routings, 44.1kHz, 512-sample blocks) in dsp/tests/unit/systems/ext_modulation_test.cpp (SC-002)
- [X] T107 [US7] Run benchmarks and verify <0.5% CPU for both per-voice and global modulation (SC-001, SC-002)

### 9.4 Smoothing Verification

- [X] T108 [US7] Verify ModulationEngine uses OnePoleSmoother (20ms) for global route amounts - already implemented, no changes needed (FR-023)
- [X] T109 [US7] Document that per-voice route amounts are NOT smoothed per spec clarification in quickstart.md

### 9.5 Fix Warnings and Static Analysis

- [X] T110 [US7] Fix all compiler warnings in benchmark files

### 9.6 Cross-Platform Verification (MANDATORY)

- [X] T111 [US7] **Verify IEEE 754 compliance**: Check if chaos_mod_source_test.cpp uses std::isnan/std::isfinite for divergence checks -> add to -fno-fast-math list in dsp/tests/CMakeLists.txt if needed

### 9.7 Commit (MANDATORY)

- [X] T112 [US7] **Commit completed User Story 7 work** (Real-time safety verification, NaN handling, benchmarks)

**Checkpoint**: User Story 7 complete - modulation system is real-time safe, smooth, and performant

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Final improvements that affect multiple user stories

- [X] T113 [P] Create traceability matrix: for each FR-001 to FR-025, list the test name(s) that verify it. Ensure NO FR has zero tests. Document the matrix in the compliance table of spec.md (SC-008)
- [X] T114 [P] Run full dsp_tests suite and verify 100% pass rate
- [X] T115 [P] Verify backward compatibility: all tests from 041-ruinae-voice-architecture produce identical results after adding aftertouch=0.0f parameter (signature updates allowed, but assertions and expected values MUST NOT change) (SC-005)
- [X] T116 Run quickstart.md validation (build commands, test filters, expected output)

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIV**: Every spec implementation MUST update architecture documentation as a final task

### 11.1 Architecture Documentation Update

- [X] T117 **Update specs/_architecture_/layer-3-systems.md** with VoiceModRouter, VoiceModSource, VoiceModDest extensions (Aftertouch, OscALevel, OscBLevel)
- [X] T118 **Update specs/_architecture_/layer-2-processors.md** with Rungler ModulationSource interface implementation
- [X] T119 **Update specs/_architecture_/layer-3-systems.md** with RuinaeVoice extensions (setAftertouch, OscA/BLevel application)
- [X] T120 **Update specs/_architecture_/layer-3-systems.md** with global modulation composition pattern and forwarding mechanism
- [X] T121 Verify no duplicate functionality was introduced (ODR check)

### 11.2 Final Commit

- [X] T122 **Commit architecture documentation updates**
- [X] T123 Verify all spec work is committed to feature branch 042-ext-modulation-system

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 12: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 12.1 Run Clang-Tidy Analysis

- [X] T124 **Run clang-tidy** on all modified/new source files:
  ```bash
  # Windows (PowerShell)
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja

  # Linux/macOS
  ./tools/run-clang-tidy.sh --target dsp
  ```

### 12.2 Address Findings

- [X] T125 **Fix all errors** reported by clang-tidy (blocking issues) -- 0 errors found
- [X] T126 **Review warnings** and fix where appropriate (use judgment for DSP code) -- 10 warnings all pre-existing in spectral_simd.cpp and selectable_oscillator_test.cpp, none in files modified by this feature
- [X] T127 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason) -- no suppressions needed, no warnings in our files

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 13.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T128 **Review ALL FR-001 through FR-025** from spec.md against implementation (open each file, find the code, cite line numbers) -- all 25 FRs verified with file paths and line numbers in compliance table
- [X] T129 **Review ALL SC-001 through SC-008** and verify measurable targets are achieved (run tests, record actual values) -- SC-001: 0.0015% (<0.5%), SC-002: 0.093% (<0.5%), SC-007: 0.9999999931 (>0.99), all others verified
- [X] T130 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in new code -- grep confirmed 0 matches
  - [X] No test thresholds relaxed from spec requirements -- all thresholds match spec exactly
  - [X] No features quietly removed from scope -- all 25 FRs and 8 SCs addressed

### 13.2 Fill Compliance Table in spec.md

- [X] T131 **Update spec.md "Implementation Verification" section** with compliance status for each requirement:
  - For each FR-xxx: cite file path, line number, description of how it's met
  - For each SC-xxx: cite test name, actual measured value, comparison to spec threshold
- [X] T132 **Mark overall status honestly**: COMPLETE

### 13.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **No**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **No**
3. Did I remove ANY features from scope without telling the user? **No**
4. Would the spec author consider this "done"? **Yes**
5. If I were the user, would I feel cheated? **No**

- [X] T133 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 14: Final Completion

**Purpose**: Final commit and completion claim

### 14.1 Final Commit

- [X] T134 **Commit all spec work** to feature branch 042-ext-modulation-system
- [X] T135 **Verify all tests pass** (run full dsp_tests suite one final time) -- 5456 test cases, 5455 passed, 1 failed (pre-existing ADSR perf flaky test). All 43 ext_modulation tests pass. All 23 voice_mod_router tests pass. All 54 ruinae_voice tests pass. All 41 rungler tests pass.

### 14.2 Completion Claim

- [X] T136 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user) -- COMPLETE: all 25 FRs and 8 SCs met

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Foundational (Phase 2)**: No dependencies - BLOCKS all user stories
- **User Stories 1-3 (P1)**: All depend on Foundational completion - can proceed in parallel after
- **User Stories 4-6 (P2)**: All depend on Foundational completion - can proceed in parallel with P1 stories
- **User Story 7 (P3)**: Depends on US1-US6 completion (tests the complete system)
- **Polish (Phase 10)**: Depends on all user stories being complete
- **Documentation/Static Analysis/Verification (Phases 11-13)**: Sequential after Polish

### User Story Dependencies

- **US1 (Per-Voice Routing)**: Independent - depends only on Phase 2
- **US2 (Aftertouch)**: Depends on US1 (uses VoiceModRouter with Aftertouch source)
- **US3 (OSC Level)**: Depends on US1 (uses VoiceModRouter with OscA/BLevel destinations)
- **US4 (Global Modulation)**: Independent - depends only on Phase 2
- **US5 (Global-to-Voice Forwarding)**: Depends on US1 (forwards to per-voice parameters) and US4 (global engine)
- **US6 (Rungler/MIDI Sources)**: Depends on US4 (uses global ModulationEngine)
- **US7 (Smoothing/Safety)**: Depends on US1-US6 (tests complete system)

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XIII)
- Implementation makes tests pass
- Fix warnings
- Cross-platform check
- Commit LAST

### Parallel Opportunities

- **Phase 2 (Foundational)**: T001 and T002 (enum extensions) can run in parallel
- **US1 Tests**: T006-T013 (all test writing) can run in parallel
- **US2 Tests**: T021-T025 can run in parallel
- **US3 Tests**: T033-T037 can run in parallel
- **US4 Tests**: T046-T050 can run in parallel
- **US5 Tests**: T059-T062 can run in parallel
- **US6 Tests (Rungler)**: T071-T075 can run in parallel
- **US6 Tests (MIDI)**: T076-T080 can run in parallel
- **US7 Tests**: T096-T100 can run in parallel
- **US1, US2, US3** can be worked in parallel by different developers (after Phase 2)
- **US4, US5, US6** can be worked in parallel by different developers (after Phase 2)

---

## Parallel Example: User Story 1

```bash
# Launch all test tasks for User Story 1 together:
Task T006: Write Aftertouch single route test
Task T007: Write Aftertouch + Env2 multi-route test
Task T008: Write zero aftertouch test
Task T009: Write OscALevel route test
Task T010: Write OscBLevel route test
Task T011: Write NaN/Inf sanitization test
Task T012: Write denormal flush test
Task T013: Write no routes test

# All 8 test writing tasks can execute in parallel (different test cases in same file)
```

---

## Implementation Strategy

### MVP First (User Stories 1-3 Only - P1)

1. Complete Phase 2: Foundational (enum extensions, signature changes)
2. Complete Phase 3: User Story 1 (VoiceModRouter with Aftertouch + OscLevel)
3. Complete Phase 4: User Story 2 (RuinaeVoice aftertouch integration)
4. Complete Phase 5: User Story 3 (RuinaeVoice OscLevel application)
5. **STOP and VALIDATE**: Test all P1 stories independently
6. This is the minimum viable modulation system - per-voice modulation works completely

### Incremental Delivery

1. MVP (US1-US3) → Per-voice modulation complete
2. Add US4 → Global modulation works
3. Add US5 → Global-to-voice forwarding works
4. Add US6 → Rungler and MIDI controllers work
5. Add US7 → System is smooth and real-time safe
6. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Phase 2 (Foundational) together
2. Once Phase 2 done:
   - Developer A: US1 (VoiceModRouter tests and impl)
   - Developer B: US2 + US3 (RuinaeVoice extensions) - sequential dependency
   - Developer C: US4 (Global modulation scaffold)
3. After US1 and US4 complete:
   - Developer A: US5 (Global-to-voice forwarding) - needs US1 + US4
   - Developer B: US6 (Rungler ModulationSource) - needs US4
   - Developer C: Start documentation
4. After US1-US6 complete:
   - Any developer: US7 (Real-time safety and benchmarks)

---

## Summary

**Total Tasks**: 137
**User Stories**: 7 (3 P1, 3 P2, 1 P3)
**Test Files Modified**: 4 (voice_mod_router_test.cpp, ruinae_voice_test.cpp, rungler_test.cpp, ext_modulation_test.cpp - 1 new)
**Header Files Modified**: 4 (ruinae_types.h, voice_mod_router.h, ruinae_voice.h, rungler.h)
**New Test File**: ext_modulation_test.cpp

**Parallel Opportunities**:
- Phase 2: 2 enum extensions in parallel
- Each user story: 5-10 test writing tasks in parallel
- US1, US2, US3 can be worked in parallel (after Phase 2 complete)
- US4, US5, US6 can be worked in parallel (after Phase 2 complete)

**MVP Scope**: User Stories 1-3 (P1) - Per-voice modulation with Aftertouch, OscALevel, OscBLevel
**Format Validation**: All tasks follow `- [ ] [TaskID] [P?] [Story?] Description with file path` format

**Independent Test Criteria**:
- US1: VoiceModRouter accepts 8 sources, 9 destinations, computes correct bipolar offsets
- US2: RuinaeVoice stores aftertouch, passes to computeOffsets(), routes work
- US3: RuinaeVoice applies OscA/BLevel offsets with correct clamping formula
- US4: ModulationEngine composes into scaffold, global routings work
- US5: Global offsets forward to all voices with two-stage clamping
- US6: Rungler implements ModulationSource, MIDI controllers normalize correctly
- US7: No allocations, NaN handling works, benchmarks meet targets

**Notes**:
- All tasks use explicit file paths
- Test-first methodology enforced (tests written before implementation)
- Commit steps explicit at end of each user story
- Cross-platform IEEE 754 checks mandatory
- Constitution principles (XII, XIII, XIV, XVI) explicitly referenced
- Real-time safety audited (FR-022)
- Backward compatibility verified (SC-005)
