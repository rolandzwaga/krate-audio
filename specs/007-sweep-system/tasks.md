# Tasks: Sweep System

**Input**: Design documents from `/specs/007-sweep-system/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

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
   - Add the file to the `-fno-fast-math` list in `plugins/Disrumpo/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             dsp/sweep_processor_tests.cpp  # ADD YOUR FILE HERE
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

- [X] T001 Create sweep_types.h enum definitions in plugins/Disrumpo/src/dsp/sweep_types.h
- [X] T002 Add Custom mode to MorphLinkMode enum in plugins/Disrumpo/src/plugin_ids.h (update COUNT to 8)
- [X] T003 Verify sweep parameter IDs exist in plugins/Disrumpo/src/plugin_ids.h (0x0E00-0x0E05)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core DSP primitives that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 SweepPositionBuffer (Lock-Free Audio-UI Sync)

- [X] T004 Write failing tests for SweepPositionBuffer in dsp/tests/primitives/sweep_position_buffer_tests.cpp (FIFO order, full buffer, getLatest, clear)
- [X] T005 Implement SweepPositionBuffer in dsp/include/krate/dsp/primitives/sweep_position_buffer.h
- [X] T006 Verify SweepPositionBuffer tests pass
- [X] T007 Commit SweepPositionBuffer primitive

### 2.2 Morph Link Curve Functions

- [X] T008 Write failing tests for all 7 preset morph link curves in plugins/Disrumpo/tests/dsp/sweep_morph_link_tests.cpp (Linear, Inverse, EaseIn, EaseOut, HoldRise, Stepped)
- [X] T009 Implement preset morph link curve functions in contracts/sweep_morph_link.h (inline functions matching formulas from research.md)
- [X] T010 Verify morph link curve tests pass
- [X] T011 Commit morph link curve functions

### 2.3 CustomCurve (Breakpoint Interpolation)

- [X] T012 Write failing tests for CustomCurve in plugins/Disrumpo/tests/dsp/custom_curve_tests.cpp (default linear, add/remove breakpoints, 2-8 limit, interpolation, sorting, serialization)
- [X] T013 Implement CustomCurve class in plugins/Disrumpo/src/dsp/custom_curve.h and custom_curve.cpp
- [X] T014 Verify CustomCurve tests pass
- [X] T015 Commit CustomCurve class

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Core Sweep DSP (Priority: P1) 🎯 MVP

**Goal**: Enable frequency-focused distortion with Gaussian/linear intensity distribution

**Independent Test**: Enable sweep at 1kHz with 2-octave width, verify bands near 1kHz receive higher distortion intensity than bands far from 1kHz

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T016 [P] [US1] Write failing Gaussian intensity calculation tests in plugins/Disrumpo/tests/dsp/sweep_intensity_tests.cpp (center = intensity param, 1 sigma = 0.606*intensity, 2 sigma = 0.135*intensity per SC-001, SC-002, SC-003)
- [X] T017 [P] [US1] Write failing Sharp falloff tests in plugins/Disrumpo/tests/dsp/sweep_intensity_tests.cpp (center = intensity param, edge = 0.0, beyond edge = 0.0 per SC-004, SC-005)
- [X] T018 [P] [US1] Write failing SweepProcessor unit tests in plugins/Disrumpo/tests/dsp/sweep_processor_tests.cpp (prepare, enable/disable, setCenterFrequency targets smoother, process advances smoother, calculateBandIntensity)

### 3.2 Implementation for User Story 1

- [X] T019 [US1] Implement calculateGaussianIntensity() function in plugins/Disrumpo/src/dsp/sweep_processor.cpp using formula from research.md (FR-006, FR-008, FR-009, FR-010)
- [X] T020 [US1] Implement calculateLinearFalloff() function in plugins/Disrumpo/src/dsp/sweep_processor.cpp using formula from research.md (FR-006a)
- [X] T021 [US1] Implement SweepProcessor class in plugins/Disrumpo/src/dsp/sweep_processor.h and sweep_processor.cpp (prepare, reset, enable/disable, parameter setters, process, calculateBandIntensity per data-model.md)
- [X] T022 [US1] Integrate OnePoleSmoother for frequency smoothing (10-50ms, default 20ms per FR-007a)
- [X] T023 [US1] Verify all User Story 1 tests pass (FR-001 through FR-010, SC-001 through SC-005)

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T024 [US1] **Verify IEEE 754 compliance**: Check if sweep_processor_tests.cpp uses `std::isnan`/`std::isfinite` → add to `-fno-fast-math` list in plugins/Disrumpo/tests/CMakeLists.txt if needed

### 3.4 Commit (MANDATORY)

- [X] T025 [US1] **Commit completed User Story 1 work** (Core sweep DSP with Gaussian/linear intensity calculation)

**Checkpoint**: User Story 1 should be fully functional - sweep processor calculates per-band intensities using Gaussian or linear falloff

---

## Phase 4: User Story 2 - Sweep Parameters (Priority: P1)

**Goal**: User can control sweep characteristics (center frequency, width, intensity, falloff shape)

**Independent Test**: Adjust each sweep parameter and verify the corresponding change in audio processing behavior

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T026 [P] [US2] Write failing tests for parameter ranges in plugins/Disrumpo/tests/dsp/sweep_processor_tests.cpp (frequency 20Hz-20kHz, width 0.5-4.0 oct, intensity 0-200%, falloff Sharp/Smooth per FR-002, FR-003, FR-004, FR-005)
- [X] T027 [P] [US2] Write failing tests for parameter changes affecting intensity in plugins/Disrumpo/tests/dsp/sweep_processor_tests.cpp (frequency changes move focus, width changes spread, intensity scales values)

### 4.2 Implementation for User Story 2

- [X] T028 [US2] Verify SweepProcessor parameter setters correctly update internal state (setCenterFrequency, setWidth, setIntensity, setFalloffMode per data-model.md)
- [X] T029 [US2] Add parameter normalization/denormalization in plugins/Disrumpo/src/processor/processor.cpp (processParameterChanges: SweepFrequency log scale, SweepWidth linear, SweepIntensity linear per data-model.md mappings)
- [X] T030 [US2] Verify all User Story 2 tests pass (FR-002 through FR-005)

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T031 [US2] **Verify IEEE 754 compliance**: Check if new test files use `std::isnan`/`std::isfinite` → add to `-fno-fast-math` list if needed

### 4.4 Commit (MANDATORY)

- [X] T032 [US2] **Commit completed User Story 2 work** (Sweep parameter control)

**Checkpoint**: User Story 2 should work - all sweep parameters adjustable and affect processing

---

## Phase 5: User Story 3 - Sweep Visualization (Priority: P1)

**Goal**: User sees sweep position/width overlay on spectrum display

**Independent Test**: Enable sweep, verify SweepIndicator overlay appears on SpectrumDisplay at correct position with correct width

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T033 [US3] Write failing tests for audio-UI sync in dsp/tests/primitives/sweep_position_buffer_tests.cpp if not already covered (verify push from audio thread, pop from UI thread)
- [ ] T034 [US3] Write UI rendering tests if applicable (manual visual verification will be primary test) [DEFERRED: UI tests require manual verification]

### 5.2 Implementation for User Story 3

- [ ] T035 [US3] Implement SweepIndicator class in plugins/Disrumpo/src/controller/sweep_indicator.h and sweep_indicator.cpp (draw Gaussian/triangular curve, center line per FR-040 through FR-045) [DEFERRED: P2 UI feature]
- [ ] T036 [US3] Integrate SweepIndicator into SpectrumDisplay in plugins/Disrumpo/src/controller/ (add as overlay layer) [DEFERRED: P2 UI feature]
- [X] T037 [US3] Implement audio-to-UI position data push in plugins/Disrumpo/src/processor/processor.cpp (push SweepPositionData to buffer each block per FR-046)
- [ ] T038 [US3] Implement UI position data read in plugins/Disrumpo/src/controller/controller.cpp (read from buffer, interpolate for 60fps per FR-047, FR-049) [DEFERRED: P2 UI feature]
- [ ] T039 [US3] Add SweepIndicator update callback in controller idle() or timer [DEFERRED: P2 UI feature]
- [ ] T040 [US3] Verify sweep indicator renders at correct position (manual test: SC-008, SC-009) [DEFERRED: P2 UI feature]

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T041 [US3] **Verify IEEE 754 compliance**: Check if new test files use `std::isnan`/`std::isfinite` → add to `-fno-fast-math` list if needed

### 5.4 Commit (MANDATORY)

- [ ] T042 [US3] **Commit completed User Story 3 work** (Sweep visualization)

**Checkpoint**: User Story 3 should work - sweep indicator visible on spectrum display

---

## Phase 6: User Story 4 - Sweep-Morph Linking (Priority: P1)

**Goal**: Sweep position automatically drives morph position via selectable curves

**Independent Test**: Set sweep-morph link to Linear, move sweep frequency from 20Hz to 20kHz, verify morph position moves from 0 to 1

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T043 [P] [US4] Write failing tests for sweep-to-morph position mapping in plugins/Disrumpo/tests/dsp/sweep_morph_link_tests.cpp (test getMorphPosition() with each link mode, verify Linear/Inverse/EaseIn/EaseOut per FR-014 through FR-019)
- [ ] T044 [P] [US4] Write integration tests for MorphEngine position updates in plugins/Disrumpo/tests/integration/sweep_morph_integration_tests.cpp [DEFERRED: Requires per-band morph link integration]

### 6.2 Implementation for User Story 4

- [X] T045 [US4] Implement getMorphPosition() in SweepProcessor using applyMorphLinkCurve() (normalize frequency to [0,1], apply curve per data-model.md)
- [X] T046 [US4] Add sweep-morph link parameter handling in plugins/Disrumpo/src/processor/processor.cpp (processParameterChanges: SweepMorphLink parameter)
- [ ] T047 [US4] Integrate sweep-morph linking into audio processing loop (if link mode != None, call morphEngine_.setMorphPosition() with sweep's getMorphPosition()) [DEFERRED: Per-band integration complex]
- [X] T048 [US4] Verify all User Story 4 tests pass (FR-014 through FR-021, SC-007)

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T049 [US4] **Verify IEEE 754 compliance**: Check if new test files use `std::isnan`/`std::isfinite` → add to `-fno-fast-math` list if needed

### 6.4 Commit (MANDATORY)

- [ ] T050 [US4] **Commit completed User Story 4 work** (Sweep-morph linking)

**Checkpoint**: User Story 4 should work - sweep drives morph position via preset curves

---

## Phase 7: User Story 5 - Link Curve Modes (Priority: P2)

**Goal**: User can choose different preset curves for sweep-to-morph mapping

**Independent Test**: Select each link mode, verify morph position follows the expected curve (HoldRise, Stepped, EaseOut)

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T051 [P] [US5] Write failing tests for HoldRise curve in plugins/Disrumpo/tests/dsp/sweep_morph_link_tests.cpp (stays 0 until 60%, then rises per FR-020)
- [X] T052 [P] [US5] Write failing tests for Stepped curve in plugins/Disrumpo/tests/dsp/sweep_morph_link_tests.cpp (quantizes to 0, 0.33, 0.67, 1.0 per FR-021)

### 7.2 Implementation for User Story 5

- [X] T053 [US5] Verify HoldRise and Stepped curves in applyMorphLinkCurve() match formulas (already implemented in Phase 2.2, just verify)
- [X] T054 [US5] Verify all User Story 5 tests pass (FR-020, FR-021)

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T055 [US5] **Verify IEEE 754 compliance**: Check if new test files use `std::isnan`/`std::isfinite` → add to `-fno-fast-math` list if needed

### 7.4 Commit (MANDATORY)

- [ ] T056 [US5] **Commit completed User Story 5 work** (Advanced link curve modes)

**Checkpoint**: User Story 5 should work - all preset link curves functional

---

## Phase 8: User Story 6 - Audio-Visual Sync (Priority: P2)

**Goal**: Sweep indicator precisely synchronized with audio (within one buffer latency)

**Independent Test**: Automate sweep frequency, verify visual indicator moves in sync with audio effect (within ~12ms at 44.1kHz/512 samples)

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T057 [US6] Write tests for latency compensation in plugins/Disrumpo/tests/integration/sweep_sync_tests.cpp (verify visual delay matches audio latency per FR-048)

### 8.2 Implementation for User Story 6

- [ ] T058 [US6] Implement interpolation between position updates in SweepIndicator for smooth 60fps display (per FR-047)
- [ ] T059 [US6] Add output latency compensation to SweepIndicator timing (optional per FR-048, SHOULD not MUST)
- [ ] T060 [US6] Verify visual update rate meets 30fps minimum when sweep active (manual test: SC-011)

### 8.3 Cross-Platform Verification (MANDATORY)

- [ ] T061 [US6] **Verify IEEE 754 compliance**: Check if new test files use `std::isnan`/`std::isfinite` → add to `-fno-fast-math` list if needed

### 8.4 Commit (MANDATORY)

- [ ] T062 [US6] **Commit completed User Story 6 work** (Audio-visual synchronization)

**Checkpoint**: User Story 6 should work - sweep indicator stays in sync with audio

---

## Phase 9: User Story 7 - Per-Band Intensity (Priority: P1)

**Goal**: DSP system calculates per-band intensity multipliers for distortion scaling

**Independent Test**: Unit tests verify Gaussian intensity values at various distances from sweep center (already tested in Phase 3, verify integration)

### 9.1 Tests for User Story 7 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T063 [US7] Write integration tests for band intensity application in plugins/Disrumpo/tests/integration/sweep_band_intensity_test.cpp (verify SweepProcessor intensities applied to BandProcessor distortion)

### 9.2 Implementation for User Story 7

- [X] T064 [US7] Integrate SweepProcessor into Disrumpo Processor in plugins/Disrumpo/src/processor/processor.h (add sweepProcessor_ member)
- [X] T065 [US7] Call sweepProcessor_.prepare() in Processor::setupProcessing()
- [X] T066 [US7] Call sweepProcessor_.process() each audio block in Processor::process()
- [X] T067 [US7] Apply per-band intensities from sweepProcessor_.calculateBandIntensity() to band distortion processing (multiply distortion gain or intensity per band)
- [X] T068 [US7] Verify all User Story 7 tests pass (FR-001, FR-007, SC-001 through SC-005 at integration level)

### 9.3 Cross-Platform Verification (MANDATORY)

- [X] T069 [US7] **Verify IEEE 754 compliance**: Check if new test files use `std::isnan`/`std::isfinite` → add to `-fno-fast-math` list if needed

### 9.4 Commit (MANDATORY)

- [ ] T070 [US7] **Commit completed User Story 7 work** (Per-band intensity integration)

**Checkpoint**: User Story 7 should work - sweep intensity applied to band distortion processing

---

## Phase 10: User Story 8 - Sweep UI Controls (Priority: P1)

**Goal**: User has accessible UI controls for all sweep parameters

**Independent Test**: Locate all sweep controls in sweep panel, verify they adjust corresponding parameters

### 10.1 Tests for User Story 8 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T071 [US8] Write UI parameter binding tests if applicable (manual verification will be primary test)

### 10.2 Implementation for User Story 8

- [X] T072 [US8] Register sweep parameters in Controller::initialize() in plugins/Disrumpo/src/controller/controller.cpp (SweepEnable, SweepFrequency, SweepWidth, SweepIntensity, SweepFalloff, SweepMorphLink per FR-030 through FR-036)
- [X] T073 [US8] Create sweep panel section in plugins/Disrumpo/resources/editor.uidesc (COnOffButton for Enable, CKnob for Frequency/Width/Intensity, CSegmentButton for Falloff, COptionMenu for MorphLink)
- [X] T074 [US8] Bind sweep UI controls to parameters in editor.uidesc
- [ ] T075 [US8] Verify all sweep controls respond within 16ms (manual test: SC-010) [MANUAL TEST]

### 10.3 Cross-Platform Verification (MANDATORY)

- [ ] T076 [US8] **Verify cross-platform UI rendering**: Test sweep panel on Windows, macOS, Linux (VSTGUI should be cross-platform)

### 10.4 Commit (MANDATORY)

- [ ] T077 [US8] **Commit completed User Story 8 work** (Sweep UI controls)

**Checkpoint**: User Story 8 should work - all sweep controls accessible and functional

---

## Phase 11: User Story 9 - Sweep Automation (Priority: P1)

**Goal**: User can animate sweep frequency via LFO, envelope follower, MIDI CC, or host automation

**Independent Test**: Enable each automation source and verify sweep frequency responds accordingly

### 11.1 Tests for User Story 9 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T078 [P] [US9] Write failing tests for SweepLFO in plugins/Disrumpo/tests/dsp/sweep_lfo_tests.cpp (waveforms, rate accuracy, tempo sync, depth scaling per FR-024, FR-025, SC-015)
- [X] T079 [P] [US9] Write failing tests for SweepEnvelopeFollower in plugins/Disrumpo/tests/dsp/sweep_envelope_tests.cpp (input level response, attack/release times, sensitivity per FR-026, FR-027, SC-016)
- [ ] T080 [P] [US9] Write tests for modulation combination in plugins/Disrumpo/tests/dsp/sweep_modulation_tests.cpp (verify LFO + envelope follower modulation amounts are summed additively per FR-029a, result clamped to sweep frequency parameter range 20Hz-20kHz, test both sources enabled simultaneously)

### 11.2 Implementation for User Story 9

- [X] T081 [P] [US9] Implement SweepLFO class in plugins/Disrumpo/src/dsp/sweep_lfo.h (wrap Krate::DSP::LFO with sweep-specific range mapping per data-model.md)
- [X] T082 [P] [US9] Implement SweepEnvelopeFollower class in plugins/Disrumpo/src/dsp/sweep_envelope.h (wrap Krate::DSP::EnvelopeFollower per data-model.md)
- [ ] T083 [US9] Add SweepLFO and SweepEnvelopeFollower to Processor in plugins/Disrumpo/src/processor/processor.h
- [ ] T084 [US9] Handle sweep LFO parameters in processParameterChanges() (enable, rate, waveform, depth, tempo sync)
- [ ] T085 [US9] Handle sweep envelope parameters in processParameterChanges() (enable, attack, release, sensitivity)
- [ ] T086 [US9] Implement additive modulation combination in process() (base frequency + LFO offset + envelope offset, clamp to 20Hz-20kHz)
- [ ] T087 [US9] Add MIDI CC mapping for sweep frequency in Controller (IMidiMapping interface per research.md, FR-028, FR-029, SC-018)
- [ ] T088 [US9] Add sweep LFO/envelope UI controls to sweep panel in editor.uidesc (toggles, knobs per FR-037, FR-038)
- [ ] T089 [US9] Verify all User Story 9 tests pass (FR-023 through FR-029, SC-015, SC-016, SC-017, SC-018)

### 11.3 Cross-Platform Verification (MANDATORY)

- [ ] T090 [US9] **Verify IEEE 754 compliance**: Check if new test files use `std::isnan`/`std::isfinite` → add to `-fno-fast-math` list if needed

### 11.4 Commit (MANDATORY)

- [ ] T091 [US9] **Commit completed User Story 9 work** (Sweep automation sources)

**Checkpoint**: User Story 9 should work - sweep frequency can be automated via LFO, envelope, MIDI, host

---

## Phase 12: User Story 10 - Custom Link Curve (Priority: P2)

**Goal**: User can define custom breakpoint curve for sweep-to-morph mapping

**Independent Test**: Set link mode to Custom, define custom curve with breakpoints, verify morph position follows user-defined curve

### 12.1 Tests for User Story 10 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T092 [P] [US10] Write tests for Custom curve mode in plugins/Disrumpo/tests/dsp/sweep_morph_link_tests.cpp (verify custom breakpoints used when mode=Custom per FR-022)
- [ ] T093 [P] [US10] Write UI tests for curve editor visibility in plugins/Disrumpo/tests/controller/custom_curve_editor_tests.cpp (visible when Custom selected, hidden otherwise per FR-039a, FR-039b)

### 12.2 Implementation for User Story 10

- [ ] T094 [US10] Update SweepProcessor::getMorphPosition() to use CustomCurve when morphLinkMode_ == Custom
- [ ] T095 [US10] Add CustomCurve member to SweepProcessor in plugins/Disrumpo/src/dsp/sweep_processor.h
- [ ] T096 [US10] Implement CustomCurveEditor UI control in plugins/Disrumpo/src/controller/custom_curve_editor.h and custom_curve_editor.cpp (add/remove/drag breakpoints, 2-8 limit per FR-039c)
- [ ] T097 [US10] Add custom curve editor section to editor.uidesc (visible when Custom mode selected)
- [ ] T098 [US10] Implement curve editor visibility toggle based on sweep-morph link mode in Controller
- [ ] T099 [US10] Add CustomCurve serialization to preset save/load in Processor::setState/getState (per data-model.md serialization format)
- [ ] T100 [US10] Verify all User Story 10 tests pass (FR-022, FR-039a, FR-039b, FR-039c, SC-007 includes Custom)

### 12.3 Cross-Platform Verification (MANDATORY)

- [ ] T101 [US10] **Verify IEEE 754 compliance**: Check if new test files use `std::isnan`/`std::isfinite` → add to `-fno-fast-math` list if needed

### 12.4 Commit (MANDATORY)

- [ ] T102 [US10] **Commit completed User Story 10 work** (Custom sweep-morph link curve)

**Checkpoint**: User Story 10 should work - custom curves can be created and used for sweep-morph linking

---

## Phase 13: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T103 [P] Implement enable/disable state correctly (FR-011, FR-012, FR-013, SC-006)
- [ ] T104 [P] Add preset state serialization for all sweep state (sweep params, LFO, envelope, custom curve per data-model.md serialization format, SC-012)
- [ ] T105 Optimize sweep processing CPU overhead (target <0.1% per band per SC-013)
- [ ] T106 [P] Add sweep enable toggle parameter registration in Controller
- [ ] T107 Add MIDI Learn button for sweep frequency CC mapping in sweep panel UI (FR-039)
- [ ] T108 Verify all edge cases from spec.md (extreme frequencies, min/max width, intensity boundaries per FR-054)
- [ ] T109 Run quickstart.md validation (step through implementation guide, verify all steps work)

---

## Phase 14: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 14.1 Architecture Documentation Update

- [ ] T110 **Update `specs/_architecture_/layer-0-core.md`** with sweep_morph_link curve functions (purpose: pure math curves for sweep-to-morph mapping, when to use: any feature needing normalized position curve transformations)
- [ ] T111 **Update `specs/_architecture_/layer-1-primitives.md`** with SweepPositionBuffer (purpose: lock-free SPSC audio-UI sync, when to use: any audio-thread data needing UI visualization)
- [ ] T112 **Update `specs/_architecture_/layer-3-systems.md`** for Disrumpo plugin with SweepProcessor (purpose: frequency-focused distortion intensity distribution, when to use: sweep effect in Disrumpo)
- [ ] T113 Verify no duplicate functionality was introduced (cross-check against existing architecture entries)

### 14.2 Final Commit

- [ ] T114 **Commit architecture documentation updates**
- [ ] T115 Verify all spec work is committed to 007-sweep-system feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 15: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 15.1 Run Clang-Tidy Analysis

- [ ] T116 **Run clang-tidy** on all modified/new source files:
  ```bash
  # Windows (PowerShell)
  ./tools/run-clang-tidy.ps1 -Target disrumpo

  # Linux/macOS
  ./tools/run-clang-tidy.sh --target disrumpo
  ```

### 15.2 Address Findings

- [ ] T117 **Fix all errors** reported by clang-tidy (blocking issues)
- [ ] T118 **Review warnings** and fix where appropriate (use judgment for DSP code - Gaussian exp() may trigger performance warnings, suppress with NOLINT if needed)
- [ ] T119 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 16: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 16.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T120 **Review ALL FR-001 through FR-056 requirements** from spec.md against implementation
- [ ] T121 **Review ALL SC-001 through SC-018 success criteria** and verify measurable targets are achieved
- [ ] T122 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 16.2 Fill Compliance Table in spec.md

- [ ] T123 **Update spec.md "Implementation Verification" section** with compliance status (MET/NOT MET/PARTIAL/DEFERRED) for each FR-xxx and SC-xxx
- [ ] T124 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 16.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T125 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 17: Final Completion

**Purpose**: Final commit and completion claim

### 17.1 Final Commit

- [ ] T126 **Commit all spec work** to 007-sweep-system feature branch
- [ ] T127 **Verify all tests pass** (DSP tests, integration tests)
- [ ] T128 **Run pluginval** at strictness level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Disrumpo.vst3"`

### 17.2 Completion Claim

- [ ] T129 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-12)**: All depend on Foundational phase completion
  - US1 (Core Sweep DSP): Can start after Foundational - no dependencies on other stories
  - US2 (Sweep Parameters): Can start after Foundational - no dependencies on other stories
  - US3 (Sweep Visualization): Depends on US1 (needs SweepProcessor) - can start in parallel if US1 API stable
  - US4 (Sweep-Morph Linking): Depends on US1 (needs SweepProcessor) - can start in parallel if US1 API stable
  - US5 (Link Curve Modes): Depends on US4 (extends linking) - sequential after US4
  - US6 (Audio-Visual Sync): Depends on US3 (extends visualization) - sequential after US3
  - US7 (Per-Band Intensity): Depends on US1 (integrates SweepProcessor) - can start after US1 complete
  - US8 (Sweep UI Controls): Can start after Foundational - can run in parallel with DSP stories
  - US9 (Sweep Automation): Depends on US1, US2 (needs SweepProcessor parameters) - can start after US1, US2 complete
  - US10 (Custom Link Curve): Depends on US4 (extends linking) - sequential after US4
- **Polish (Phase 13)**: Depends on desired user stories being complete
- **Documentation (Phase 14)**: Depends on all implementation complete
- **Static Analysis (Phase 15)**: Depends on all implementation complete
- **Verification (Phase 16)**: Depends on all previous phases
- **Completion (Phase 17)**: Final phase

### Recommended Implementation Order

**MVP Path (Minimal Functional Feature)**:
1. Phase 1: Setup
2. Phase 2: Foundational (CRITICAL - blocks all stories)
3. Phase 3: US1 (Core Sweep DSP) - enables basic sweep effect
4. Phase 4: US2 (Sweep Parameters) - user can control sweep
5. Phase 10: US8 (Sweep UI Controls) - user can interact with sweep
6. **STOP and VALIDATE**: Basic sweep effect works end-to-end

**Full Feature Path**:
1. Complete MVP Path above
2. Phase 5: US3 (Sweep Visualization) - visual feedback
3. Phase 6: US4 (Sweep-Morph Linking) - core differentiator
4. Phase 11: US9 (Sweep Automation) - dynamic movement
5. Phase 9: US7 (Per-Band Intensity) - integration validation
6. Phase 7: US5 (Link Curve Modes) - advanced curves
7. Phase 12: US10 (Custom Link Curve) - full creative control
8. Phase 8: US6 (Audio-Visual Sync) - polish
9. Phase 13-17: Polish, docs, verification

### Parallel Opportunities

- **Phase 2 (Foundational)**: T004-T007 (SweepPositionBuffer), T008-T011 (Morph curves), T012-T015 (CustomCurve) can all run in parallel
- **After Foundational complete**:
  - US1 (Core DSP) + US2 (Parameters) + US8 (UI Controls) can run in parallel
  - US3 (Visualization) + US4 (Linking) can start once US1 API is stable
- **Within each story**: Tests marked [P] can run in parallel
- **Different team members**: Can work on different user stories simultaneously after Foundational phase

---

## Parallel Example: Foundational Phase

```bash
# Launch all foundational tasks together after Phase 1:
Task T004-T007: "SweepPositionBuffer (lock-free buffer primitive)"
Task T008-T011: "Morph link curve functions (pure math)"
Task T012-T015: "CustomCurve (breakpoint interpolation)"
# All three can proceed in parallel - different files, no dependencies
```

---

## Implementation Strategy

### MVP First (US1 + US2 + US8 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL)
3. Complete Phase 3: US1 (Core Sweep DSP)
4. Complete Phase 4: US2 (Sweep Parameters)
5. Complete Phase 10: US8 (Sweep UI Controls)
6. **STOP and VALIDATE**: Test basic sweep effect end-to-end
7. Deploy/demo if ready

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add US1 + US2 + US8 → Basic sweep works → Deploy/Demo (MVP!)
3. Add US3 → Visualization added → Deploy/Demo
4. Add US4 → Morph linking added → Deploy/Demo
5. Add US9 → Automation added → Deploy/Demo
6. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: US1 (Core DSP)
   - Developer B: US2 (Parameters) + US8 (UI Controls)
   - Developer C: Start US3 (Visualization) once US1 API is stable
3. Stories complete and integrate independently

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
