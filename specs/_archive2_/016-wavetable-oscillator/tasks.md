# Tasks: Wavetable Oscillator with Mipmapping

**Input**: Design documents from `/specs/016-wavetable-oscillator/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Fix Warnings**: Build and fix all compiler warnings
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check

**CRITICAL**: After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/core/your_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons (MSVC/Clang differ at 7th-8th digits)

## Format: `- [ ] [ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2, US3, etc.)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Project Initialization)

**Purpose**: Verify repository structure for new Layer 0/Layer 1 headers and tests

- [X] T001 Verify layer-0-core structure exists at F:\projects\iterum\dsp\include\krate\dsp\core\
- [X] T002 Verify layer-1-primitives structure exists at F:\projects\iterum\dsp\include\krate\dsp\primitives\
- [X] T003 Verify test structure exists at F:\projects\iterum\dsp\tests\unit\core\ and F:\projects\iterum\dsp\tests\unit\primitives\

---

## Phase 2: Foundational (No Blocking Prerequisites)

**Purpose**: This spec has no foundational phase - all components build on existing Layer 0 infrastructure

**Checkpoint**: Ready to begin user story implementation

---

## Phase 3: User Story 1 - Wavetable Data Structure and Mipmap Level Selection (Priority: P1)

**Goal**: Deliver `WavetableData` struct and `selectMipmapLevel` functions providing standardized mipmapped wavetable storage and level selection logic. This is the foundation for all wavetable-based components.

**Independent Test**: Create a WavetableData, verify memory layout, test selectMipmapLevel with known frequency/sampleRate/tableSize combinations.

### 3.1 Tests for WavetableData (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T004 [P] [US1] Create F:\projects\iterum\dsp\tests\unit\core\wavetable_data_test.cpp with Catch2 structure and [WavetableData] tag
- [X] T005 [US1] Write failing test for default construction (SC-001 area) - verify kDefaultTableSize=2048 and kMaxMipmapLevels=11
- [X] T006 [US1] Write failing test for default state - verify numLevels()=0 and all table data is zero-initialized
- [X] T007 [US1] Write failing test for getLevel() with invalid index - verify returns nullptr
- [X] T008 [US1] Write failing test for tableSize() - verify returns 2048

### 3.2 Tests for selectMipmapLevel (Write FIRST - Must FAIL)

- [X] T009 [US1] Write failing test for low frequency (SC-001) - verify selectMipmapLevel(20.0f, 44100.0f, 2048) returns 0
- [X] T010 [US1] Write failing test for high frequency (SC-002) - verify selectMipmapLevel(10000.0f, 44100.0f, 2048) returns 8
- [X] T011 [US1] Write failing test for zero frequency (SC-003) - verify selectMipmapLevel(0.0f, 44100.0f, 2048) returns 0
- [X] T012 [US1] Write failing test for Nyquist frequency (SC-004) - verify selectMipmapLevel(22050.0f, 44100.0f, 2048) returns highest valid level (10)
- [X] T013 [US1] Write failing test for negative frequency - verify returns 0 (no aliasing risk)
- [X] T014 [US1] Write failing test for frequency > Nyquist - verify clamped to highest level

### 3.3 Tests for selectMipmapLevelFractional (Write FIRST - Must FAIL)

- [X] T015 [US1] Write failing test for fractional level calculation - verify returns float values for crossfading
- [X] T016 [US1] Write failing test for low frequency - verify selectMipmapLevelFractional(100.0f, 44100.0f, 2048) returns value near 0.0
- [X] T017 [US1] Write failing test for octave doubling - verify each frequency doubling increases fractional level by approximately 1.0
- [X] T018 [US1] Write failing test for clamping - verify result clamped to [0.0, kMaxMipmapLevels - 1.0]

### 3.4 Implementation of WavetableData and Level Selection

- [X] T019 [US1] Create F:\projects\iterum\dsp\include\krate\dsp\core\wavetable_data.h with #pragma once, namespace Krate::DSP
- [X] T020 [US1] Add standard Layer 0 file header comment documenting constitution compliance (Principles II, III, IX, XII)
- [X] T021 [P] [US1] Define constants kDefaultTableSize=2048, kMaxMipmapLevels=11, kGuardSamples=4
- [X] T022 [P] [US1] Define WavetableData struct with std::array<std::array<float, kDefaultTableSize + kGuardSamples>, kMaxMipmapLevels> storage
- [X] T023 [US1] Implement WavetableData::getLevel(size_t) const noexcept returning pointer to data start (index 1 in physical array)
- [X] T024 [US1] Implement WavetableData::getMutableLevel(size_t) noexcept for generator use
- [X] T025 [US1] Implement WavetableData::tableSize() const noexcept returning kDefaultTableSize
- [X] T026 [US1] Implement WavetableData::numLevels() const noexcept and setNumLevels(size_t) noexcept
- [X] T027 [P] [US1] Implement selectMipmapLevel(float frequency, float sampleRate, size_t tableSize) using loop-based log2 calculation
- [X] T028 [P] [US1] Implement selectMipmapLevelFractional(float frequency, float sampleRate, size_t tableSize) using std::log2f
- [X] T029 [US1] Build dsp_tests target - verify zero warnings on MSVC
- [X] T030 [US1] Run wavetable_data_test.cpp - verify tests T005-T018 pass

### 3.5 Cross-Platform Verification (MANDATORY)

- [X] T031 [US1] Verify no IEEE 754 functions used (no -fno-fast-math needed for this test file)
- [X] T032 [US1] Verify all floating-point comparisons use Approx().margin() where appropriate

### 3.6 Commit (MANDATORY)

- [ ] T033 [US1] Commit wavetable_data.h and wavetable_data_test.cpp with message: "Add WavetableData struct and mipmap level selection functions"

**Checkpoint**: User Story 1 (WavetableData + level selection) complete, tested, committed

---

## Phase 4: User Story 2 - Generate Mipmapped Standard Waveforms (Priority: P1)

**Goal**: Deliver generator functions for standard waveforms (sawtooth, square, triangle) that produce mipmapped wavetables via FFT/IFFT. Each mipmap level is independently normalized and has correct guard samples.

**Independent Test**: Generate a sawtooth table, verify level 0 has expected harmonic content via FFT analysis, verify higher levels have progressively fewer harmonics, verify guard samples are set correctly.

### 4.1 Tests for Standard Waveform Generation (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T034 [P] [US2] Create F:\projects\iterum\dsp\tests\unit\primitives\wavetable_generator_test.cpp with Catch2 structure and [WavetableGenerator] tag
- [X] T035 [US2] Write failing test for generateMipmappedSaw level 0 harmonic content (SC-005) - FFT verify 1/n series within 5% for first 20 harmonics
- [X] T036 [US2] Write failing test for generateMipmappedSaw mipmap levels - verify level N has approximately half the harmonics of level N-1
- [X] T037 [US2] Write failing test for generateMipmappedSaw no aliasing (SC-008) - verify no harmonics above Nyquist limit for any level
- [X] T038 [US2] Write failing test for generateMipmappedSaw highest level is sine - verify level 10 has only fundamental (bin 1)
- [X] T039 [US2] Write failing test for generateMipmappedSaw normalization (US2 scenario 4) - verify all values within [-1.05, 1.05]
- [X] T040 [US2] Write failing test for generateMipmappedSquare odd harmonics only (SC-006) - verify even harmonics below -60 dB
- [X] T041 [US2] Write failing test for generateMipmappedSquare level 0 harmonic content - verify 1/n amplitude for odd harmonics
- [X] T042 [US2] Write failing test for generateMipmappedTriangle level 0 harmonic content (SC-007) - verify 1/n^2 series for first 10 odd harmonics within 5%
- [X] T043 [US2] Write failing test for generateMipmappedTriangle alternating sign - verify harmonic phases match expected pattern
- [X] T044 [US2] Write failing test for guard samples (SC-018) - verify table[-1]==table[N-1], table[N]==table[0], etc. for all levels and all waveforms

### 4.2 Implementation of Standard Waveform Generators

- [X] T045 [US2] Create F:\projects\iterum\dsp\include\krate\dsp\primitives\wavetable_generator.h with #pragma once, namespace Krate::DSP
- [X] T046 [US2] Add standard Layer 1 file header comment documenting constitution compliance and Layer 1 dependencies
- [X] T047 [US2] Add includes for core/wavetable_data.h, core/math_constants.h, primitives/fft.h, <algorithm>, <cmath>, <vector>
- [X] T048 [P] [US2] Implement internal helper setGuardSamples(float* levelData, size_t tableSize) setting wraparound samples
- [X] T049 [P] [US2] Implement internal helper normalizeToPeak(float* data, size_t count, float targetPeak = 0.96f)
- [X] T050 [US2] Implement generateMipmappedSaw(WavetableData& data) - for each level: create spectrum with harmonics 1..maxHarmonic amplitude 1/n, IFFT, normalize, set guards
- [X] T051 [US2] Implement generateMipmappedSquare(WavetableData& data) - odd harmonics only, amplitude 1/n
- [X] T052 [US2] Implement generateMipmappedTriangle(WavetableData& data) - odd harmonics, amplitude 1/n^2, alternating sign
- [X] T053 [US2] Build dsp_tests target - verify zero warnings
- [X] T054 [US2] Run wavetable_generator_test.cpp - verify tests T035-T044 pass

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T055 [US2] Verify no IEEE 754 functions used in test file (FFT analysis does not require -fno-fast-math)
- [X] T056 [US2] Verify all FFT magnitude comparisons use Approx().margin() for tolerance

### 4.4 Commit (MANDATORY)

- [ ] T057 [US2] Commit wavetable_generator.h and wavetable_generator_test.cpp (standard waveforms) with message: "Add mipmapped sawtooth, square, triangle generators"

**Checkpoint**: User Story 2 (standard waveform generation) complete, tested, committed

---

## Phase 5: User Story 3 - Generate Mipmapped Tables from Custom Harmonic Spectra (Priority: P1)

**Goal**: Deliver `generateMipmappedFromHarmonics` enabling custom timbres from arbitrary harmonic spectra. This differentiates wavetable synthesis from PolyBLEP oscillators.

**Independent Test**: Provide a known harmonic spectrum (e.g., only fundamental and 3rd harmonic), generate tables, verify via FFT that each level contains only specified harmonics below Nyquist.

### 5.1 Tests for Custom Harmonic Generation (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T058 [P] [US3] Write failing test for fundamental-only spectrum (US3 scenario 1) - verify all mipmap levels contain identical sine wave
- [X] T059 [US3] Write failing test for 4-harmonic custom spectrum (US3 scenario 2) - verify FFT magnitudes match within 1% for specified harmonics
- [X] T060 [US3] Write failing test for high-harmonic count - verify 512 harmonics input produces correct level 0, progressively band-limited higher levels
- [X] T061 [US3] Write failing test for zero harmonics (edge case FR-028) - verify all levels filled with silence (zeros)
- [X] T062 [US3] Write failing test for normalization - verify each level independently normalized to ~0.96 peak
- [X] T063 [US3] Write failing test for guard samples - verify correct wraparound for custom spectrum tables

### 5.2 Implementation of Custom Harmonic Generator

- [X] T064 [US3] Implement generateMipmappedFromHarmonics(WavetableData& data, const float* harmonicAmplitudes, size_t numHarmonics) in wavetable_generator.h
- [X] T065 [US3] Handle numHarmonics==0 case (FR-028) - fill all levels with silence, set numLevels to kMaxMipmapLevels
- [X] T066 [US3] For each level: copy harmonics 1..min(maxHarmonicForLevel, numHarmonics) to spectrum, IFFT, normalize, set guards
- [X] T067 [US3] Build dsp_tests target - verify zero warnings
- [X] T068 [US3] Run wavetable_generator_test.cpp custom harmonic tests - verify T058-T063 pass

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T069 [US3] Verify floating-point comparisons use appropriate tolerances for FFT magnitude checks

### 5.4 Commit (MANDATORY)

- [ ] T070 [US3] Commit generateMipmappedFromHarmonics implementation and tests with message: "Add custom harmonic spectrum generator"

**Checkpoint**: User Story 3 (custom harmonic generation) complete, tested, committed

---

## Phase 6: User Story 4 - Generate Mipmapped Tables from Raw Waveform Samples (Priority: P2)

**Goal**: Deliver `generateMipmappedFromSamples` enabling import of external wavetables (e.g., from .wav files) with automatic band-limiting via FFT analysis/resynthesis.

**Independent Test**: Provide a known single-cycle waveform (e.g., hand-crafted sawtooth), generate mipmapped tables, verify level 0 matches original and higher levels are progressively smoother.

### 6.1 Tests for Raw Sample Generation (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T071 [P] [US4] Write failing test for sine input (US4 scenario 1) - verify all mipmap levels contain identical sine waves
- [X] T072 [US4] Write failing test for raw sawtooth input (US4 scenario 2) - verify level 0 matches generateMipmappedSaw within 1e-3 tolerance
- [X] T073 [US4] Write failing test for input size mismatch (US4 scenario 3) - verify sampleCount != tableSize is handled via FFT resampling
- [X] T074 [US4] Write failing test for zero-length input (edge case) - verify data left in default state (numLevels=0)
- [X] T075 [US4] Write failing test for normalization and guard samples - verify same quality as other generators

### 6.2 Implementation of Raw Sample Generator

- [X] T076 [US4] Implement generateMipmappedFromSamples(WavetableData& data, const float* samples, size_t sampleCount) in wavetable_generator.h
- [X] T077 [US4] Handle sampleCount==0 case - return early with no modifications
- [X] T078 [US4] If sampleCount != kDefaultTableSize: FFT at nearest power-of-2, resample spectrum to tableSize bins, IFFT
- [X] T079 [US4] For each level: zero bins above maxHarmonicForLevel, IFFT, normalize, set guards
- [X] T080 [US4] Build dsp_tests target - verify zero warnings
- [X] T081 [US4] Run wavetable_generator_test.cpp raw sample tests - verify T071-T075 pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T082 [US4] Verify FFT-based comparisons use appropriate tolerances

### 6.4 Commit (MANDATORY)

- [ ] T083 [US4] Commit generateMipmappedFromSamples implementation and tests with message: "Add raw waveform sample generator with FFT resampling"

**Checkpoint**: User Story 4 (raw sample generation) complete, tested, committed

---

## Phase 7: User Story 5 - Wavetable Oscillator Playback with Automatic Mipmap Selection (Priority: P1)

**Goal**: Deliver `WavetableOscillator` class providing real-time playback with cubic Hermite interpolation, automatic mipmap selection, and mipmap crossfading during frequency sweeps. Follows same lifecycle/interface as PolyBlepOscillator.

**Independent Test**: Create oscillator, load sawtooth table, generate output at various frequencies, verify correct waveform at low frequencies, progressively smoother at high frequencies, alias suppression via FFT analysis.

### 7.1 Tests for Oscillator Lifecycle and Basic Playback (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T084 [P] [US5] Create F:\projects\iterum\dsp\tests\unit\primitives\wavetable_oscillator_test.cpp with Catch2 structure and [WavetableOscillator] tag
- [X] T085 [US5] Write failing test for default construction - verify sampleRate=0, frequency=440, table=nullptr
- [X] T086 [US5] Write failing test for prepare(sampleRate) - verify resets all state, stores sample rate
- [X] T087 [US5] Write failing test for reset() - verify phase returns to 0, modulation cleared, frequency/sampleRate/table preserved
- [X] T088 [US5] Write failing test for setWavetable(nullptr) - verify process() returns 0.0 safely (SC-016)
- [X] T089 [US5] Write failing test for setFrequency(hz) - verify frequency clamped to [0, sampleRate/2)

### 7.2 Tests for Oscillator Output Quality (Write FIRST - Must FAIL)

- [X] T090 [US5] Write failing test for sawtooth output at 440 Hz (US5 scenario 1) - verify output resembles sawtooth in [-1, 1]
- [X] T091 [US5] Write failing test for table match at 100 Hz (US5 scenario 2) - verify output matches level 0 table data within cubic Hermite tolerance (<0.01)
- [X] T092 [US5] Write failing test for alias suppression at 10000 Hz (SC-009) - FFT verify no harmonics above Nyquist, aliases at least 50 dB below fundamental
- [X] T093 [US5] Write failing test for cubic Hermite interpolation accuracy (SC-019) - verify sine table at 440 Hz matches sin(2*pi*n*440/44100) within 1e-3
- [X] T094 [US5] Write failing test for processBlock equivalence (SC-011) - verify processBlock(output, 512) identical to 512 sequential process() calls

### 7.3 Tests for Mipmap Crossfading (Write FIRST - Must FAIL)

- [X] T095 [US5] Write failing test for frequency sweep crossfade (SC-020) - sweep 440 Hz to 880 Hz, verify fractional level increases smoothly, no audible clicks
- [X] T096 [US5] Write failing test for single-lookup optimization - verify when fractional level near integer (<0.05 or >0.95), only one table read occurs
- [X] T097 [US5] Write failing test for dual-lookup crossfade - verify when fractional level between levels, two lookups occur and are blended
- [X] T097a [US5] Write failing test for crossfade threshold values - verify when frac < 0.05 or frac > 0.95 only one lookup occurs; verify when frac is in [0.05, 0.95] two lookups are blended
- [X] T097b [US5] Write failing test for SC-020 measurable criteria - sweep 440-880 Hz, verify max sample-to-sample diff at transition boundary < 0.05 and no spectral energy spikes above -60 dB

### 7.4 Implementation of WavetableOscillator Core

- [X] T098 [US5] Create F:\projects\iterum\dsp\include\krate\dsp\primitives\wavetable_oscillator.h with #pragma once, namespace Krate::DSP
- [X] T099 [US5] Add standard Layer 1 file header comment documenting Layer 0-only dependencies
- [X] T100 [US5] Add includes for core/wavetable_data.h, core/interpolation.h, core/phase_utils.h, core/math_constants.h, core/db_utils.h
- [X] T101 [US5] Define WavetableOscillator class with private members: PhaseAccumulator phaseAcc_, float sampleRate_, frequency_, fmOffset_, pmOffset_, const WavetableData* table_, bool phaseWrapped_
- [X] T102 [P] [US5] Implement prepare(double sampleRate) noexcept - reset all state, store sampleRate, reset PhaseAccumulator
- [X] T103 [P] [US5] Implement reset() noexcept - call phaseAcc_.reset(), clear modulation offsets, clear phaseWrapped flag
- [X] T104 [P] [US5] Implement setWavetable(const WavetableData* table) noexcept - store non-owning pointer
- [X] T105 [P] [US5] Implement setFrequency(float hz) noexcept - clamp to [0, sampleRate/2), call phaseAcc_.setFrequency()
- [X] T106 [US5] Implement internal helper readLevel(size_t level, double normalizedPhase) const noexcept - cubic Hermite interpolation using guard samples for branchless access
- [X] T107 [US5] Implement internal helper sanitize(float x) noexcept - bit-cast NaN detection, clamp to [-2.0, 2.0] (FR-051)
- [X] T108 [US5] Implement process() noexcept - compute effective freq/phase, select fractional mipmap level, single or dual lookup with crossfade, advance phase, reset modulation, sanitize output
- [X] T109 [US5] Implement processBlock(float* output, size_t numSamples) noexcept for constant frequency - compute fractional level once, loop process() logic
- [X] T110 [US5] Build dsp_tests target - verify zero warnings
- [X] T111 [US5] Run wavetable_oscillator_test.cpp core tests - verify T084-T097 pass

### 7.5 Cross-Platform Verification (MANDATORY)

- [X] T112 [US5] Add wavetable_oscillator_test.cpp to -fno-fast-math list in dsp/tests/CMakeLists.txt (uses bit-cast NaN detection in sanitize)
- [X] T113 [US5] Verify all FFT-based alias tests use Approx().margin() for dB thresholds

### 7.6 Commit (MANDATORY)

- [ ] T114 [US5] Commit wavetable_oscillator.h and wavetable_oscillator_test.cpp (core playback) with message: "Add WavetableOscillator with mipmap selection and crossfading"

**Checkpoint**: User Story 5 (oscillator playback) complete, tested, committed

---

## Phase 8: User Story 6 - Phase Interface Compatibility with PolyBlepOscillator (Priority: P2)

**Goal**: Implement phase access methods (phase(), phaseWrapped(), resetPhase()) matching PolyBlepOscillator interface for interchangeability in downstream components (FM Operator, PD Oscillator).

**Independent Test**: Verify phase trajectory matches PhaseAccumulator, resetPhase forces phase to specific value, phaseWrapped detection works correctly.

### 8.1 Tests for Phase Interface (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T115 [P] [US6] Write failing test for phase() accessor - verify returns current phase in [0, 1)
- [X] T116 [US6] Write failing test for phase wrap counting (SC-010) - verify 440 Hz produces ~440 wraps (±1) in 44100 samples
- [X] T117 [US6] Write failing test for resetPhase(0.5) (SC-012) - verify next phase() returns 0.5 and next process() starts from that position
- [X] T118 [US6] Write failing test for resetPhase with value outside [0, 1) - verify wrapped via wrapPhase()
- [X] T119 [US6] Write failing test for phaseWrapped() - verify returns true exactly when phase wraps from near-1.0 to near-0.0

### 8.2 Implementation of Phase Interface

- [X] T120 [P] [US6] Implement phase() const noexcept - return phaseAcc_.phase
- [X] T121 [P] [US6] Implement phaseWrapped() const noexcept - return phaseWrapped_ flag
- [X] T122 [US6] Implement resetPhase(double newPhase) noexcept - wrap newPhase to [0, 1), set phaseAcc_.phase
- [X] T123 [US6] Build dsp_tests target - verify zero warnings
- [X] T124 [US6] Run wavetable_oscillator_test.cpp phase interface tests - verify T115-T119 pass

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T125 [US6] Verify phase wrap counting test uses Approx().margin() for wrap count tolerance (±1)

### 8.4 Commit (MANDATORY)

- [ ] T126 [US6] Commit phase interface implementation with message: "Add phase access interface matching PolyBlepOscillator"

**Checkpoint**: User Story 6 (phase interface) complete, tested, committed

---

## Phase 9: User Story 7 - Shared Wavetable Data Across Oscillator Instances (Priority: P2)

**Goal**: Validate that multiple WavetableOscillator instances can share a single WavetableData via non-owning pointers without data corruption. Enables polyphonic voices.

**Independent Test**: Create one WavetableData and two oscillators pointing to it, run both at different frequencies, verify correct independent output.

### 9.1 Tests for Shared Data and Modulation (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T127 [P] [US7] Write failing test for shared data (SC-014) - two oscillators, one WavetableData, different frequencies, 100,000 samples, verify independent correct output
- [X] T128 [US7] Write failing test for setWavetable(nullptr) mid-stream (US7 scenario 2) - verify transitions to silence safely
- [X] T129 [US7] Write failing test for setWavetable(&newTable) mid-stream (US7 scenario 3) - verify transitions to new waveform at current phase position
- [X] T130 [US7] Write failing test for setPhaseModulation(0.0f) (SC-013) - verify output identical to unmodulated over 4096 samples
- [X] T131 [US7] Write failing test for setPhaseModulation(radians) - verify PM offset applied, then reset after process()
- [X] T132 [US7] Write failing test for setFrequencyModulation(hz) - verify FM offset applied to effective frequency, then reset after process()
- [X] T133 [US7] Write failing test for processBlock FM variant - verify per-sample FM buffer works correctly
- [X] T133a [US7] Write failing test for PM offset > 2*pi wrapping - verify setPhaseModulation with radians > 2*pi wraps correctly and produces valid output

### 9.2 Implementation of Modulation Interface

- [X] T134 [P] [US7] Implement setPhaseModulation(float radians) noexcept - store radians in pmOffset_ (converted to normalized in process())
- [X] T135 [P] [US7] Implement setFrequencyModulation(float hz) noexcept - store hz in fmOffset_
- [X] T136 [US7] Implement processBlock(float* output, const float* fmBuffer, size_t numSamples) noexcept - per-sample FM, mipmap selection per sample
- [X] T137 [US7] Update process() to apply pmOffset_ and fmOffset_ to effective phase/frequency, then reset both to 0 after phase advance
- [X] T138 [US7] Build dsp_tests target - verify zero warnings
- [X] T139 [US7] Run wavetable_oscillator_test.cpp shared data and modulation tests - verify T127-T133 pass

### 9.3 Tests for Edge Cases and Robustness (Write FIRST - Must FAIL)

- [X] T140 [US7] Write failing test for NaN/Inf frequency inputs (SC-017) - verify produces safe output (0.0), never emits NaN/Inf
- [X] T141 [US7] Write failing test for processBlock with 0 samples - verify no-op, no state changes
- [X] T142 [US7] Write failing test for oscillator used without prepare() - verify outputs 0.0 (sampleRate=0, increment=0)
- [X] T142a [US7] Write failing test for corrupted table data containing NaN values - verify oscillator produces safe output (0.0) and never emits NaN/Inf (FR-052 edge case)

### 9.4 Implementation of Edge Case Handling

- [X] T143 [US7] Add NaN/Inf guards in setFrequency() and process() for frequency inputs - treat as 0 Hz
- [X] T144 [US7] Add early return in processBlock() for numSamples == 0
- [X] T145 [US7] Build dsp_tests target - verify zero warnings
- [X] T146 [US7] Run wavetable_oscillator_test.cpp edge case tests - verify T140-T142 pass

### 9.5 Cross-Platform Verification (MANDATORY)

- [X] T147 [US7] Verify NaN/Inf test file is in -fno-fast-math list (already added in Phase 7)
- [X] T148 [US7] Verify PM comparison test uses Approx().margin() for floating-point tolerance

### 9.6 Commit (MANDATORY)

- [ ] T149 [US7] Commit modulation interface and edge case handling with message: "Add PM/FM modulation and shared data validation"

**Checkpoint**: User Story 7 (shared data + modulation) complete, tested, committed

---

## Phase 10: Build Integration & Cross-Platform Verification

**Purpose**: Integrate new headers and test files into CMake build system

- [X] T150 Add wavetable_data.h to dsp/CMakeLists.txt under KRATE_DSP_CORE_HEADERS
- [X] T151 Add wavetable_generator.h to dsp/CMakeLists.txt under KRATE_DSP_PRIMITIVES_HEADERS
- [X] T152 Add wavetable_oscillator.h to dsp/CMakeLists.txt under KRATE_DSP_PRIMITIVES_HEADERS
- [X] T153 Add wavetable_data_test.cpp to dsp/tests/CMakeLists.txt under Layer 0: Core section in add_executable(dsp_tests ...)
- [X] T154 Add wavetable_generator_test.cpp to dsp/tests/CMakeLists.txt under Layer 1: Primitives section in add_executable(dsp_tests ...)
- [X] T155 Add wavetable_oscillator_test.cpp to dsp/tests/CMakeLists.txt under Layer 1: Primitives section in add_executable(dsp_tests ...)
- [X] T156 Cross-platform verification: Ensure wavetable_oscillator_test.cpp is in -fno-fast-math block (uses bit-cast NaN detection)
- [ ] T157 Commit CMakeLists.txt changes with message: "Integrate wavetable system into build"

---

## Phase 11: Polish & Cross-Cutting Concerns

**Purpose**: Final validation and quality checks

- [X] T158 Run full test suite with all Catch2 tags - verify 100% pass rate
- [X] T159 Verify zero compiler warnings across all three new headers and test files on MSVC
- [X] T160 Run quickstart.md usage examples manually to verify correctness
- [X] T161 Verify all FR-xxx requirements (FR-001 through FR-052) are met - cross-reference with implementation
- [X] T162 Verify all SC-xxx success criteria (SC-001 through SC-020) are measured and pass
- [X] T163 Verify no placeholder or TODO comments exist in new code
- [X] T164 Verify no test thresholds were relaxed from original spec requirements

---

## Phase 12: Architecture Documentation Update (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 12.1 Update specs/_architecture_/layer-0-core.md

- [X] T165 Add wavetable_data.h entry to F:\projects\iterum\specs\_architecture_\layer-0-core.md:
  - Purpose: Mipmapped wavetable storage and mipmap level selection
  - Public API: WavetableData struct, selectMipmapLevel, selectMipmapLevelFractional
  - File location: dsp/include/krate/dsp/core/wavetable_data.h
  - When to use: Any wavetable-based synthesis component
  - Memory layout: ~90 KB per WavetableData (11 levels × 2052 floats × 4 bytes)
- [X] T166 Add usage examples showing guard sample layout and getLevel() pattern

### 12.2 Update specs/_architecture_/layer-1-primitives.md

- [X] T167 Add wavetable_generator.h entry to F:\projects\iterum\specs\_architecture_\layer-1-primitives.md:
  - Purpose: Mipmapped wavetable generation via FFT/IFFT
  - Public API: generateMipmappedSaw, generateMipmappedSquare, generateMipmappedTriangle, generateMipmappedFromHarmonics, generateMipmappedFromSamples
  - File location: dsp/include/krate/dsp/primitives/wavetable_generator.h
  - When to use: Once during initialization (NOT real-time safe)
- [X] T168 Add wavetable_oscillator.h entry to F:\projects\iterum\specs\_architecture_\layer-1-primitives.md:
  - Purpose: Real-time wavetable playback with automatic mipmap selection
  - Public API: WavetableOscillator class (prepare/reset/process/processBlock, phase interface, PM/FM modulation)
  - File location: dsp/include/krate/dsp/primitives/wavetable_oscillator.h
  - When to use: Building synth voices, FM operators, PD oscillators
  - Interface compatibility: Matches PolyBlepOscillator (prepare/reset/process/phase/phaseWrapped/resetPhase/setPhaseModulation/setFrequencyModulation)
- [X] T169 Add note about WavetableOscillator vs PolyBlepOscillator tradeoffs: wavetable = no aliasing, arbitrary timbres, higher memory; polyblep = minimal memory, simple waveforms only
- [X] T170 Verify no duplicate functionality was introduced - document complementary roles. See plan.md section 'Higher-Layer Reusability Analysis' for content guidance

### 12.3 Commit Architecture Documentation

- [ ] T171 Commit layer-0-core.md and layer-1-primitives.md updates with message: "Document wavetable system in architecture"

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 13: Static Analysis (MANDATORY)

**Purpose**: Run clang-tidy before final verification

> **Pre-commit Quality Gate**: Static analysis to catch bugs, performance issues, style violations

### 13.1 Run Clang-Tidy Analysis

- [X] T172 Generate compile_commands.json if not already present: cmake --preset windows-ninja (from VS Developer PowerShell)
- [X] T173 Run clang-tidy on DSP library: .\tools\run-clang-tidy.ps1 -Target dsp -BuildDir build\windows-ninja
- [X] T174 Fix all clang-tidy errors (blocking issues) in wavetable_data.h, wavetable_generator.h, wavetable_oscillator.h
- [X] T175 Review clang-tidy warnings and fix where appropriate (use judgment for DSP-specific patterns like magic numbers)
- [X] T176 Document any intentionally ignored warnings with NOLINT comments and rationale
- [ ] T177 Commit clang-tidy fixes with message: "Apply clang-tidy fixes to wavetable system"

**Checkpoint**: Static analysis clean

---

## Phase 14: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements before claiming completion

> **Constitution Principle XV**: Claiming "done" when requirements are not met is a violation of trust

### 14.1 Requirements Review

- [X] T178 Review ALL FR-xxx requirements (FR-001 through FR-052) against implementation - verify each is MET
- [X] T179 Review ALL SC-xxx success criteria (SC-001 through SC-020) - verify measurable targets achieved
- [X] T180 Search for cheating patterns: no placeholders, no relaxed thresholds, no quietly removed features
- [X] T181 Fill spec.md compliance table with evidence for each requirement
- [X] T182 Honest assessment: Would the user feel cheated by this completion claim? If yes, document gaps and fix them.

### 14.2 Final Build and Test Verification

- [X] T183 Clean build: cmake --build build/windows-x64-release --config Release --clean-first
- [X] T184 Build dsp_tests: cmake --build build/windows-x64-release --config Release --target dsp_tests
- [X] T185 Run full test suite: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
- [X] T186 Verify all wavetable tests pass: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[WavetableData]" "[WavetableGenerator]" "[WavetableOscillator]"
- [X] T187 Verify zero warnings in build output

### 14.3 Final Commit

- [ ] T188 Final commit of compliance table and completion status with message: "Complete spec 016: Wavetable Oscillator with Mipmapping"

**Checkpoint**: Spec 016 complete - all requirements met, tested, documented

---

## Summary

**Total Tasks**: 193
**User Stories**: 7 (US1-US7)
**Phases**: 14

### Task Breakdown by User Story

- **US1 (WavetableData + Level Selection)**: 30 tasks (T004-T033)
- **US2 (Standard Waveforms)**: 24 tasks (T034-T057)
- **US3 (Custom Harmonics)**: 13 tasks (T058-T070)
- **US4 (Raw Samples)**: 13 tasks (T071-T083)
- **US5 (Oscillator Playback)**: 33 tasks (T084-T114, T097a-T097b)
- **US6 (Phase Interface)**: 12 tasks (T115-T126)
- **US7 (Shared Data + Modulation)**: 25 tasks (T127-T149, T133a, T142a)
- **Integration & Polish**: 39 tasks (T150-T188)

### Parallel Execution Opportunities

Tasks marked `[P]` can run in parallel within their phase:
- Phase 3 (US1): T021-T028 (WavetableData members and level selection can be implemented concurrently)
- Phase 4 (US2): T048-T049 (helper functions), generator implementations are independent per waveform
- Phase 5 (US3): T058-T063 (test writing)
- Phase 6 (US4): T071-T075 (test writing)
- Phase 7 (US5): T102-T109 (oscillator methods after class definition)
- Phase 8 (US6): T120-T122 (phase interface methods)
- Phase 9 (US7): T134-T136 (modulation methods)

### Independent Test Criteria per User Story

- **US1**: `selectMipmapLevel(20, 44100, 2048) == 0` and `selectMipmapLevel(10000, 44100, 2048) == 8`
- **US2**: Generated saw level 0 FFT matches 1/n harmonic series within 5%, no harmonics above Nyquist for any level
- **US3**: Custom 4-harmonic spectrum produces FFT magnitudes within 1% of specified values
- **US4**: Raw sine input produces identical sine at all levels, raw saw matches generateMipmappedSaw within 1e-3
- **US5**: Oscillator at 10000 Hz has no aliases above Nyquist (50 dB suppression), processBlock matches sequential process()
- **US6**: 440 Hz produces ~440 phase wraps in 44100 samples, resetPhase(0.5) works correctly
- **US7**: Two oscillators sharing one WavetableData produce correct independent output over 100,000 samples

### Suggested MVP Scope

**MVP = User Stories 1 + 2 + 5** (WavetableData, standard waveforms, oscillator playback)

This delivers a complete, working wavetable oscillator with sawtooth/square/triangle waveforms and anti-aliasing. Custom harmonics (US3), raw samples (US4), phase interface (US6), and modulation (US7) can be added incrementally.

### Implementation Strategy

1. **Phase 1-3 (US1)**: Foundation - data structure and level selection MUST complete first
2. **Phase 4 (US2)**: Standard waveforms - needed for US5 testing
3. **Phase 7 (US5)**: Core oscillator - main deliverable, depends on US1+US2
4. **Phases 5-6 (US3-US4)**: Enhanced generators - can be implemented after MVP
5. **Phases 8-9 (US6-US7)**: Advanced features - enable downstream integration (FM, PD)

---

**Format Validation**: All tasks follow `- [ ] [ID] [P?] [Story?] Description with file path` format. Tasks are organized by user story for independent implementation and testing.
