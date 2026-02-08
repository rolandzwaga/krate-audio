# Tasks: Ruinae Voice Architecture

**Input**: Design documents from `/specs/041-ruinae-voice-architecture/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), data-model.md, contracts/

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
4. **Run Clang-Tidy**: Static analysis check (see Phase 11.0)
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
             unit/systems/your_test.cpp  # ADD YOUR FILE HERE
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

**Purpose**: Project initialization and basic structure

**No tasks needed** - existing DSP library structure is already in place at `dsp/include/krate/dsp/systems/` and `dsp/tests/unit/systems/`.

---

## Phase 2: Foundational (Enumerations & Types)

**Purpose**: Core type definitions that ALL user stories depend on

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T001 Create `ruinae_types.h` with all enumerations in `dsp/include/krate/dsp/systems/ruinae_types.h`
- [X] T002 Verify enumerations compile and have correct values (compile-only test: verify `OscType::NumTypes == 10`, `RuinaeFilterType::NumTypes == 4`, `RuinaeDistortionType::NumTypes == 6`, `VoiceModSource::NumSources == 7`, `VoiceModDest::NumDests == 7`)

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 3 - Selectable Oscillator Type Switching (Priority: P1) 🎯 MVP Foundation

**Goal**: Real-time safe oscillator type switching with lazy initialization. This is the architectural foundation that enables Ruinae's diverse timbral palette.

**Independent Test**: Cycle through all 10 oscillator types while processing. Verify each produces expected character and no type switch causes allocation or audible artifacts.

**Why First**: SelectableOscillator is a dependency-free component that other user stories need. Building it first enables parallel work on US1 and US2.

### 3.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T003 [P] [US3] Write unit tests for SelectableOscillator in `dsp/tests/unit/systems/selectable_oscillator_test.cpp`
  - Default construction produces PolyBLEP type
  - All 10 types produce non-zero output after prepare (SC-005)
  - Type switching preserves frequency setting
  - Type switching same type is no-op (AS-3.1)
  - processBlock before prepare produces silence
  - setType with PhaseMode::Reset resets phase
  - Zero heap allocations during type switch for non-FFT types (SC-004)
  - NaN/Inf frequency is silently ignored

### 3.1b Build Integration (MANDATORY)

- [X] T003b [US3] Update `dsp/tests/CMakeLists.txt` to add `unit/systems/selectable_oscillator_test.cpp` to the `dsp_tests` target

### 3.2 Implementation for User Story 3

- [X] T004 [US3] Implement SelectableOscillator in `dsp/include/krate/dsp/systems/selectable_oscillator.h`
  - OscillatorVariant using `std::variant<std::monostate, PolyBlepOscillator, WavetableOscillator, PhaseDistortionOscillator, SyncOscillator, AdditiveOscillator, ChaosOscillator, ParticleOscillator, FormantOscillator, SpectralFreezeOscillator, NoiseOscillator>`
  - Visitor structs for prepare, reset, setFrequency, processBlock
  - Lazy initialization: default type constructed on prepare(), new types on setType()
  - Phase mode handling (Reset vs Continuous)

### 3.3 Verify Tests Pass

- [X] T005 [US3] Build and run all SelectableOscillator tests
- [X] T006 [US3] Verify SC-004: Zero heap allocations during type switch (use custom operator new override in test)
- [X] T007 [US3] Verify SC-005: All 10 oscillator types produce non-zero output at 440 Hz (RMS > -60 dBFS over 1 second)

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T008 [US3] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 3.5 Commit (MANDATORY)

- [X] T009 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Story 3 should be fully functional, tested, and committed

---

## Phase 4: User Story 6 - Per-Voice Modulation Routing (Priority: P2)

**Goal**: Per-voice modulation routing transforms static timbres into living, evolving sounds. Up to 16 modulation routes per voice.

**Independent Test**: Configure ENV 2 to modulate filter cutoff with a positive amount. Play a note and verify the cutoff sweeps during the attack phase of the filter envelope.

**Why Second**: VoiceModRouter is also dependency-free and needed by RuinaeVoice. Building it now enables parallel work on US1, US2, US4, US5.

### 4.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T010 [P] [US6] Write unit tests for VoiceModRouter in `dsp/tests/unit/systems/voice_mod_router_test.cpp`
  - Empty router produces zero offsets
  - Single route Env2 -> FilterCutoff with amount +48 -> offset = env2Value * 48
  - Two routes to same destination are summed (FR-027, AS-6.4)
  - Amount clamped to [-1.0, +1.0]
  - Velocity source is constant per note
  - 16 routes all functional
  - Clear route zeroes its contribution

### 4.1b Build Integration (MANDATORY)

- [X] T010b [US6] Update `dsp/tests/CMakeLists.txt` to add `unit/systems/voice_mod_router_test.cpp` to the `dsp_tests` target

### 4.2 Implementation for User Story 6

- [X] T011 [US6] Implement VoiceModRouter in `dsp/include/krate/dsp/systems/voice_mod_router.h`
  - Fixed `std::array<VoiceModRoute, 16>` for routes
  - `std::array<float, NumDestinations>` for computed offsets
  - `computeOffsets()` iterates active routes, reads source value, multiplies by amount, accumulates to destination offset
  - Source value mapping: Env1/2/3 in [0,1], LFO in [-1,+1], Gate in [0,1], Velocity in [0,1], KeyTrack = (midiNote - 60) / 60 in [-1,+1]

### 4.3 Verify Tests Pass

- [X] T012 [US6] Build and run all VoiceModRouter tests
- [X] T013 [US6] Verify FR-027: Multiple routes to same destination are summed correctly

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T014 [US6] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 4.5 Commit (MANDATORY)

- [X] T015 [US6] **Commit completed User Story 6 work**

**Checkpoint**: User Stories 3 AND 6 should both work independently and be committed

---

## Phase 5: User Story 1 - Basic Voice Playback with Default Configuration (Priority: P1) 🎯 MVP Core

**Goal**: Minimal viable voice - a single oscillator through a filter and amp envelope. Without basic voice playback, nothing else works.

**Independent Test**: Call `noteOn()`, process a block, verify non-zero output. Call `noteOff()`, verify voice eventually becomes inactive. Delivers a playable monophonic voice.

### 5.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T016 [P] [US1] Write unit tests for RuinaeVoice (basic playback) in `dsp/tests/unit/systems/ruinae_voice_test.cpp`
  - noteOn produces non-zero output at correct pitch (AS-1.1)
  - noteOff -> isActive false after envelope completes (AS-1.2)
  - Retrigger: envelopes restart from current level (AS-1.3)
  - processBlock before prepare produces silence (edge case)
  - reset() -> isActive false
  - SC-007: silence within 100ms of envelope idle

### 5.1b Build Integration (MANDATORY)

- [ ] T016b [US1] Update `dsp/tests/CMakeLists.txt` to add `unit/systems/ruinae_voice_test.cpp` to the `dsp_tests` target

### 5.2 Implementation for User Story 1 (Basic Chain)

- [ ] T017 [US1] Implement RuinaeVoice basic skeleton in `dsp/include/krate/dsp/systems/ruinae_voice.h`
  - Signal flow: OSC A -> Filter -> VCA (Amp Envelope) -> Output
  - Members: oscA_ (SelectableOscillator), filter_ (SVF default), ampEnv_ (ADSREnvelope), scratch buffers
  - Lifecycle: prepare(), reset(), noteOn(), noteOff(), setFrequency(), isActive()
  - processBlock(): OSC A -> Filter -> Amp Envelope -> Output with NaN/Inf safety

### 5.3 Verify Tests Pass

- [ ] T018 [US1] Build and run all RuinaeVoice tests for basic playback
- [ ] T019 [US1] Verify AS-1.1: Output contains non-zero audio at 440 Hz after noteOn
- [ ] T020 [US1] Verify AS-1.2: isActive() returns false after noteOff and envelope completes
- [ ] T021 [US1] Verify SC-007: Output is silence within 100ms of envelope idle

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T022 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 5.5 Commit (MANDATORY)

- [ ] T023 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional (basic voice), tested, and committed

---

## Phase 6: User Story 2 - Dual Oscillator with Crossfade Mixing (Priority: P1) 🎯 MVP Feature

**Goal**: Dual-oscillator architecture is fundamental to Ruinae's identity. Smooth CrossfadeMix blending between OSC A and OSC B.

**Independent Test**: Set OSC A and OSC B to different types, set mix position to 0.0, verify output matches OSC A alone; set to 1.0, verify output matches OSC B alone; set to 0.5, verify blended output.

### 6.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T024 [P] [US2] Write unit tests for dual oscillator mixing in `dsp/tests/unit/systems/ruinae_voice_test.cpp`
  - Mix position 0.0 = OSC A only (AS-2.1)
  - Mix position 1.0 = OSC B only (AS-2.2)
  - Mix position 0.5 = linear crossfade blend: `oscA * 0.5 + oscB * 0.5` (AS-2.3)
  - Oscillator type switch during playback: no clicks or allocation (AS-2.4)

### 6.2 Implementation for User Story 2 (Dual OSC + Mixer)

- [ ] T025 [US2] Extend RuinaeVoice with OSC B and CrossfadeMix mode in `dsp/include/krate/dsp/systems/ruinae_voice.h`
  - Add oscB_ (SelectableOscillator)
  - Add mixMode_ (MixMode enum), mixPosition_ (float)
  - Add oscBBuffer_ scratch buffer
  - processBlock: OSC A -> oscABuffer_, OSC B -> oscBBuffer_, CrossfadeMix -> mixBuffer_, Filter -> VCA -> Output
  - CrossfadeMix formula: `output = oscA * (1 - mixPosition) + oscB * mixPosition`

### 6.3 Verify Tests Pass

- [ ] T026 [US2] Build and run dual oscillator tests
- [ ] T027 [US2] Verify AS-2.1: Mix position 0.0 outputs OSC A signal only
- [ ] T028 [US2] Verify AS-2.2: Mix position 1.0 outputs OSC B signal only
- [ ] T029 [US2] Verify AS-2.3: Mix position 0.5 outputs blended signal
- [ ] T030 [US2] Verify AS-2.4: Type switch during playback causes no clicks or allocation

### 6.4 Cross-Platform Verification (MANDATORY)

- [ ] T031 [US2] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 6.5 Commit (MANDATORY)

- [ ] T032 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1, 2, 3, and 6 should all work independently and be committed

---

## Phase 7: User Story 4 - Selectable Filter Section (Priority: P2)

**Goal**: Filter is the primary timbral sculpting tool. 4 filter types (SVF, Ladder, Formant, Comb) with cutoff, resonance, envelope modulation, and key tracking.

**Independent Test**: Set filter type to each of the 4 types, apply a sweep of cutoff values, and verify frequency response matches the expected filter characteristic.

### 7.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T033 [P] [US4] Write unit tests for selectable filter in `dsp/tests/unit/systems/ruinae_voice_test.cpp`
  - SVF lowpass attenuates above cutoff (AS-4.1)
  - Ladder at max resonance self-oscillates (AS-4.2)
  - Key tracking doubles cutoff for octave (AS-4.3)
  - Filter type switch: no clicks or allocation (AS-4.4)
  - SC-006: Filter cutoff modulation within 1 semitone accuracy

### 7.2 Implementation for User Story 4 (Filter Section)

- [ ] T034 [US4] Extend RuinaeVoice with selectable filter in `dsp/include/krate/dsp/systems/ruinae_voice.h`
  - Add FilterVariant: `std::variant<SVF, LadderFilter, FormantFilter, FeedbackComb>`
  - Add filterType_ (RuinaeFilterType enum)
  - Add filterCutoffHz_, filterResonance_, filterEnvAmount_, filterKeyTrack_
  - Add filterEnv_ (ADSREnvelope for ENV 2)
  - Compute modulated cutoff: `effectiveCutoff = baseCutoff * semitonesToRatio(envAmount * filterEnvValue + keyTrackSemitones)`
  - Clamp cutoff to [20 Hz, 0.495 * sampleRate]

### 7.3 Verify Tests Pass

- [ ] T035 [US4] Build and run selectable filter tests
- [ ] T036 [US4] Verify AS-4.1: SVF lowpass attenuates frequencies above cutoff
- [ ] T037 [US4] Verify AS-4.2: Ladder filter self-oscillates at max resonance
- [ ] T038 [US4] Verify AS-4.3: Key tracking doubles cutoff for octave pitch change
- [ ] T039 [US4] Verify SC-006: Cutoff modulation accuracy within 1 semitone

### 7.4 Cross-Platform Verification (MANDATORY)

- [ ] T040 [US4] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 7.5 Commit (MANDATORY)

- [ ] T041 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Filter section functional and integrated with voice chain

---

## Phase 8: User Story 5 - Selectable Distortion Section (Priority: P2)

**Goal**: Distortion gives Ruinae its aggressive, chaotic character. 6 distortion types post-filter with drive and character parameters.

**Independent Test**: Process a sine wave through each distortion type at various drive levels. Verify harmonic content increases with drive and that Clean mode passes audio unmodified.

### 8.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T042 [P] [US5] Write unit tests for selectable distortion in `dsp/tests/unit/systems/ruinae_voice_test.cpp`
  - Clean mode: bit-identical passthrough (AS-5.1)
  - ChaosWaveshaper adds harmonics with drive > 0 (AS-5.2)
  - Distortion type switch: no allocation, no clicks (AS-5.3)

### 8.2 Implementation for User Story 5 (Distortion Section)

- [ ] T043 [US5] Extend RuinaeVoice with selectable distortion in `dsp/include/krate/dsp/systems/ruinae_voice.h`
  - Add DistortionVariant: `std::variant<std::monostate, ChaosWaveshaper, SpectralDistortion, GranularDistortion, Wavefolder, TapeSaturator>`
  - Add distortionType_ (RuinaeDistortionType enum), distortionDrive_, distortionCharacter_
  - Add dcBlocker_ (DCBlocker) for post-distortion DC removal
  - processBlock: Filter -> Distortion -> DC Blocker -> TranceGate -> VCA
  - Clean mode uses std::monostate (bypass)

### 8.3 Verify Tests Pass

- [ ] T044 [US5] Build and run selectable distortion tests
- [ ] T045 [US5] Verify AS-5.1: Clean distortion is bit-identical passthrough
- [ ] T046 [US5] Verify AS-5.2: ChaosWaveshaper adds harmonics at drive = 0.8
- [ ] T047 [US5] Verify AS-5.3: Distortion type switch causes no allocation or clicks

### 8.4 Cross-Platform Verification (MANDATORY)

- [ ] T048 [US5] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 8.5 Commit (MANDATORY)

- [ ] T049 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Distortion section functional and integrated with voice chain

---

## Phase 9: User Story 8 - TranceGate Integration (Priority: P3)

**Goal**: TranceGate adds rhythmic interest post-distortion, pre-VCA. Configurable pattern, depth, and rate per-voice.

**Independent Test**: Enable the trance gate with a simple alternating pattern. Verify the output amplitude follows the gate pattern with smooth transitions.

### 9.1 Tests for User Story 8 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T050 [P] [US8] Write unit tests for TranceGate integration in `dsp/tests/unit/systems/ruinae_voice_test.cpp`
  - Trance gate enabled: rhythmic amplitude variation at 4 Hz (AS-8.1)
  - Trance gate depth 0 = bypass (AS-8.2)
  - Trance gate does not affect voice lifetime (AS-8.3, FR-018)
  - getGateValue returns [0, 1] (AS-8.4)

### 9.2 Implementation for User Story 8 (TranceGate Integration)

- [ ] T051 [US8] Extend RuinaeVoice with TranceGate in `dsp/include/krate/dsp/systems/ruinae_voice.h`
  - Add tranceGate_ (TranceGate), tranceGateEnabled_ (bool)
  - Add setTranceGateEnabled(), setTranceGateParams(), setTranceGateTempo(), getGateValue()
  - processBlock: Distortion -> DC Blocker -> TranceGate (if enabled) -> VCA
  - When disabled, TranceGate section is skipped entirely (no processing cost)

### 9.3 Verify Tests Pass

- [ ] T052 [US8] Build and run TranceGate integration tests
- [ ] T053 [US8] Verify AS-8.1: Output exhibits rhythmic amplitude variation at 4 Hz
- [ ] T054 [US8] Verify AS-8.2: Gate depth 0 has no effect (bypass)
- [ ] T055 [US8] Verify AS-8.3: Gate does not affect voice lifetime (amp envelope controls isActive)

### 9.4 Cross-Platform Verification (MANDATORY)

- [ ] T056 [US8] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 9.5 Commit (MANDATORY)

- [ ] T057 [US8] **Commit completed User Story 8 work**

**Checkpoint**: TranceGate integrated and functional

---

## Phase 10: User Story 6 (Continued) - Modulation Routing Integration (Priority: P2)

**Goal**: Integrate VoiceModRouter into RuinaeVoice to enable per-voice modulation of filter cutoff, morph position, etc.

**Independent Test**: Configure ENV 2 to modulate filter cutoff with +48 semitones. Verify cutoff sweeps during envelope attack.

### 10.1 Tests for User Story 6 Integration (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T058 [P] [US6] Write unit tests for modulation routing in RuinaeVoice in `dsp/tests/unit/systems/ruinae_voice_test.cpp`
  - Env2 modulating cutoff during attack (AS-6.1)
  - LFO modulating morph position (AS-6.2)
  - Velocity modulating cutoff (AS-6.3)
  - Multiple routes summed in semitone space (AS-6.4)
  - SC-008: Modulation updates within one block

### 10.2 Implementation for User Story 6 Integration

- [ ] T059 [US6] Extend RuinaeVoice with VoiceModRouter integration in `dsp/include/krate/dsp/systems/ruinae_voice.h`
  - Add modRouter_ (VoiceModRouter)
  - Add modEnv_ (ADSREnvelope for ENV 3)
  - Add voiceLfo_ (LFO)
  - Add velocity_, noteFrequency_ state
  - processBlock: At start, call modRouter_.computeOffsets() with all source values
  - Apply modulation offsets to filter cutoff (semitone-space summation), morph position, distortion drive, etc.
  - Add setModRoute(), getAmpEnvelope(), getFilterEnvelope(), getModEnvelope(), getVoiceLFO()

### 10.3 Verify Tests Pass

- [ ] T060 [US6] Build and run modulation routing integration tests
- [ ] T061 [US6] Verify AS-6.1: ENV 2 modulates filter cutoff during attack phase
- [ ] T062 [US6] Verify AS-6.2: Voice LFO modulates morph position
- [ ] T063 [US6] Verify AS-6.3: Velocity modulates filter cutoff proportionally
- [ ] T064 [US6] Verify AS-6.4: ENV 2 (+24 st) + LFO (-12 st) = +12 st total cutoff offset
- [ ] T065 [US6] Verify SC-008: Modulation updates within one block (512 samples max)

### 10.4 Cross-Platform Verification (MANDATORY)

- [ ] T066 [US6] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 10.5 Commit (MANDATORY)

- [ ] T067 [US6] **Commit completed User Story 6 integration work**

**Checkpoint**: Per-voice modulation fully integrated and functional

---

## Phase 11: User Story 7 - SpectralMorph Mixing Mode (Priority: P3)

**Goal**: SpectralMorph is Ruinae's signature feature. Mixer routes both oscillators through SpectralMorphFilter, interpolating their magnitude spectra.

**Independent Test**: Set mixer mode to SpectralMorph, provide two distinct oscillator signals, sweep morph position, and verify the output spectral content transitions between the two sources.

### 11.1 Tests for User Story 7 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T068 [P] [US7] Write unit tests for SpectralMorph mode in `dsp/tests/unit/systems/ruinae_voice_test.cpp`
  - SpectralMorph at 0.0 matches OSC A spectrum (AS-7.1)
  - SpectralMorph at 1.0 matches OSC B spectrum (AS-7.2)
  - SpectralMorph at 0.5 exhibits blended spectral characteristics (AS-7.3)
  - SpectralMorph mode: no allocation during processBlock (AS-7.4)

### 11.2 Implementation for User Story 7 (SpectralMorph Mode)

- [ ] T069 [US7] Extend RuinaeVoice with SpectralMorph mode in `dsp/include/krate/dsp/systems/ruinae_voice.h`
  - Add spectralMorph_ (SpectralMorphFilter)
  - processBlock: When mixMode_ == SpectralMorph, route oscABuffer_ and oscBBuffer_ through spectralMorph_.processBlock(oscA, oscB, output, numSamples)
  - setMixPosition() updates spectralMorph_.setMorphAmount()
  - prepare() initializes SpectralMorphFilter with 1024-point FFT

### 11.3 Verify Tests Pass

- [ ] T070 [US7] Build and run SpectralMorph mode tests
- [ ] T071 [US7] Verify AS-7.1: Morph at 0.0 matches OSC A spectral content
- [ ] T072 [US7] Verify AS-7.2: Morph at 1.0 matches OSC B spectral content
- [ ] T073 [US7] Verify AS-7.3: Morph at 0.5 exhibits blended spectral characteristics
- [ ] T074 [US7] Verify AS-7.4: No memory allocation during processBlock (operator new override test)

### 11.4 Cross-Platform Verification (MANDATORY)

- [ ] T075 [US7] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 11.5 Commit (MANDATORY)

- [ ] T076 [US7] **Commit completed User Story 7 work**

**Checkpoint**: SpectralMorph mode functional and tested

---

## Phase 12: Performance and Safety Verification

**Purpose**: Verify CPU budget, allocation safety, and NaN/Inf guards meet spec requirements

### 12.1 Performance Tests (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T077 [P] Write performance benchmarks in `dsp/tests/unit/systems/ruinae_voice_test.cpp`
  - SC-001: Basic voice <1% CPU at 44.1kHz
  - SC-002: SpectralMorph voice <3% CPU
  - SC-003: 8 basic voices <8% CPU
  - SC-009: Memory footprint per voice <64KB

### 12.2 Allocation Safety Tests

- [ ] T078 [P] Write allocation detection tests in `dsp/tests/unit/systems/ruinae_voice_test.cpp`
  - SC-004: Zero heap allocations during oscillator type switch (operator new override)
  - SC-004: Zero heap allocations during filter type switch
  - SC-004: Zero heap allocations during distortion type switch

### 12.3 NaN/Inf Safety Tests

- [ ] T079 [P] Write NaN/Inf safety tests in `dsp/tests/unit/systems/ruinae_voice_test.cpp`
  - SC-010: No NaN/Inf in output after 10s of chaos oscillator processing
  - FR-036: NaN/Inf safety for all output stages (oscillator, filter, distortion)

### 12.4 Run All Performance and Safety Tests

- [ ] T080 Build and run all performance tests
- [ ] T081 Verify SC-001: Basic voice CPU consumption <1%
- [ ] T082 Verify SC-002: SpectralMorph voice CPU consumption <3%
- [ ] T083 Verify SC-003: 8 basic voices CPU consumption <8%
- [ ] T084 Verify SC-004: Zero heap allocations during type switches
- [ ] T085 Verify SC-009: Memory footprint per voice <64KB
- [ ] T086 Verify SC-010: No NaN/Inf in output after chaos processing
- [ ] T087 Verify SC-005: All 10 oscillator types produce non-zero output at 440 Hz (RMS > -60 dBFS)
- [ ] T088 Verify SC-007: Voice produces silence within 100ms of envelope idle

### 12.5 Cross-Platform Verification (MANDATORY)

- [ ] T089 **Verify IEEE 754 compliance**: Performance tests may use NaN/Inf checks → add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 12.6 Commit (MANDATORY)

- [ ] T090 **Commit performance and safety tests**

**Checkpoint**: All performance and safety criteria verified

---

## Phase 13: Code Quality

**Purpose**: Final cleanup and static analysis before completion

### 13.1 Compiler Warnings

- [ ] T091 Build with MSVC, Clang, and GCC (if available)
- [ ] T092 Fix all compiler warnings in `selectable_oscillator.h`
- [ ] T093 Fix all compiler warnings in `voice_mod_router.h`
- [ ] T094 Fix all compiler warnings in `ruinae_voice.h`
- [ ] T095 Verify zero warnings on all platforms

### 13.2 Commit Warning Fixes

- [ ] T096 **Commit compiler warning fixes**

**Checkpoint**: All code compiles with zero warnings

---

## Phase 14: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 14.1 Architecture Documentation Update

- [ ] T097 **Update `specs/_architecture_/layer-3-systems.md`** with new components:
  - Add SelectableOscillator entry: purpose (variant-based oscillator wrapper with lazy init), public API summary (setType, setFrequency, processBlock), file location, "when to use this" (any voice that needs multiple oscillator types)
  - Add VoiceModRouter entry: purpose (per-voice modulation routing), public API summary (setRoute, computeOffsets, getOffset), file location, "when to use this" (per-voice modulation needs)
  - Add RuinaeVoice entry: purpose (complete per-voice unit for Ruinae synth), public API summary (noteOn/Off, processBlock, section setters), file location, "when to use this" (RuinaeEngine composition)
  - Verify no duplicate functionality was introduced

### 14.2 Final Commit

- [ ] T098 **Commit architecture documentation updates**
- [ ] T099 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 15: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 15.1 Run Clang-Tidy Analysis

- [ ] T100 **Run clang-tidy** on all modified/new source files:
  ```bash
  # Windows (PowerShell)
  ./tools/run-clang-tidy.ps1 -Target dsp

  # Linux/macOS
  ./tools/run-clang-tidy.sh --target dsp
  ```

### 15.2 Address Findings

- [ ] T101 **Fix all errors** reported by clang-tidy (blocking issues)
- [ ] T102 **Review warnings** and fix where appropriate (use judgment for DSP code)
- [ ] T103 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 16: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 16.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T104 **Review ALL FR-001 through FR-036 requirements** from spec.md against implementation
- [ ] T105 **Review ALL SC-001 through SC-010 success criteria** and verify measurable targets are achieved
- [ ] T106 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 16.2 Fill Compliance Table in spec.md

- [ ] T107 **Update spec.md "Implementation Verification" section** with compliance status for each requirement:
  - For each FR-xxx: Open implementation file, read relevant code, cite file and line number
  - For each SC-xxx: Run specific test or measurement, copy actual output, compare to threshold
  - Fill compliance table with concrete evidence (file paths, line numbers, test names, measured values)

- [ ] T108 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 16.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T109 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 17: Final Completion

**Purpose**: Final commit and completion claim

### 17.1 Final Commit

- [ ] T110 **Commit all spec work** to feature branch
- [ ] T111 **Verify all tests pass** (dsp_tests target)

### 17.2 Completion Claim

- [ ] T112 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - SKIPPED (infrastructure exists)
- **Foundational (Phase 2)**: No dependencies - can start immediately - BLOCKS all user stories
- **User Stories (Phase 3-11)**: All depend on Foundational phase completion
  - **US3 (SelectableOscillator)**: Can start after Foundational - No dependencies on other stories
  - **US6 (VoiceModRouter)**: Can start after Foundational - No dependencies on other stories
  - **US1 (Basic Voice)**: Depends on US3 (SelectableOscillator)
  - **US2 (Dual OSC)**: Depends on US1 (Basic Voice) and US3 (SelectableOscillator)
  - **US4 (Filter)**: Depends on US2 (Dual OSC + Mixer)
  - **US5 (Distortion)**: Depends on US4 (Filter)
  - **US8 (TranceGate)**: Depends on US5 (Distortion)
  - **US6 Integration (Modulation)**: Depends on US8 (TranceGate), US6 Phase (VoiceModRouter)
  - **US7 (SpectralMorph)**: Depends on US2 (Dual OSC + Mixer)
- **Performance (Phase 12)**: Depends on all user stories being complete
- **Quality (Phase 13)**: Depends on Performance tests passing
- **Documentation (Phase 14)**: Can run in parallel with Quality
- **Static Analysis (Phase 15)**: Depends on Quality being complete
- **Completion (Phase 16-17)**: Depends on all previous phases

### User Story Dependencies

- **User Story 3 (P1)**: Foundation - no dependencies on other stories
- **User Story 6 Phase (P2)**: Foundation - no dependencies on other stories
- **User Story 1 (P1)**: Depends on US3 - independently testable
- **User Story 2 (P1)**: Depends on US1, US3 - independently testable
- **User Story 4 (P2)**: Depends on US2 - independently testable
- **User Story 5 (P2)**: Depends on US4 - independently testable
- **User Story 8 (P3)**: Depends on US5 - independently testable
- **User Story 6 Integration (P2)**: Depends on US8, US6 Phase - independently testable
- **User Story 7 (P3)**: Depends on US2 - independently testable

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation to make tests pass
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Phase 2 (Foundational)**: Single task - no parallelism
- **Phase 3 (US3) and Phase 4 (US6 Phase)**: Can run in parallel after Foundational completes
- **Tests within a user story**: Tests marked [P] can run in parallel (different test cases)
- **Phase 13 (Quality) and Phase 14 (Documentation)**: Can run in parallel

---

## Parallel Example: User Story 3 (SelectableOscillator)

```bash
# After Foundational phase completes:

# Developer A: User Story 3 (SelectableOscillator)
Task: "Write unit tests for SelectableOscillator in selectable_oscillator_test.cpp"
Task: "Implement SelectableOscillator in selectable_oscillator.h"

# Developer B: User Story 6 Phase (VoiceModRouter)
Task: "Write unit tests for VoiceModRouter in voice_mod_router_test.cpp"
Task: "Implement VoiceModRouter in voice_mod_router.h"

# Both can proceed independently
```

---

## Implementation Strategy

### MVP First (User Stories 3, 1, 2 Only)

1. Complete Phase 2: Foundational (enums and types)
2. Complete Phase 3: User Story 3 (SelectableOscillator)
3. Complete Phase 5: User Story 1 (Basic Voice)
4. Complete Phase 6: User Story 2 (Dual OSC + CrossfadeMix)
5. **STOP and VALIDATE**: Test basic dual-oscillator voice independently
6. This delivers a minimal but functional Ruinae voice architecture

### Incremental Delivery

1. Complete Foundational → Types ready
2. Add User Story 3 (SelectableOscillator) → Test independently
3. Add User Story 1 (Basic Voice) → Test independently → Minimal voice works
4. Add User Story 2 (Dual OSC) → Test independently → MVP complete!
5. Add User Story 4 (Filter) → Test independently
6. Add User Story 5 (Distortion) → Test independently
7. Add User Story 8 (TranceGate) → Test independently
8. Add User Story 6 Integration (Modulation) → Test independently
9. Add User Story 7 (SpectralMorph) → Test independently → Full feature set complete

### Parallel Team Strategy

With multiple developers:

1. Team completes Foundational together
2. Once Foundational is done:
   - Developer A: User Story 3 (SelectableOscillator)
   - Developer B: User Story 6 Phase (VoiceModRouter)
3. After US3 completes:
   - Developer A: User Story 1 (Basic Voice)
   - Developer B continues US6 Phase
4. Stories complete and integrate sequentially as dependencies allow

---

## Notes

- [P] tasks = different files, no dependencies
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
