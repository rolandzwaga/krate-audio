# Implementation Plan: Formant Filter

**Branch**: `077-formant-filter` | **Date**: 2026-01-21 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/077-formant-filter/spec.md`

## Summary

Implement a Layer 2 FormantFilter processor that uses 3 parallel bandpass filters to model vocal formants (F1, F2, F3). The processor supports discrete vowel selection (A, E, I, O, U), continuous vowel morphing, formant frequency shifting, and gender scaling. All parameter changes are smoothed using OnePoleSmoother to prevent clicks. The implementation reuses existing Biquad filters configured as bandpasses and the formant data already defined in filter_tables.h.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Biquad (biquad.h), OnePoleSmoother (smoother.h), FormantData/Vowel/kVowelFormants (filter_tables.h)
**Storage**: N/A (stateless between audio blocks except for filter state)
**Testing**: Catch2 (existing test framework in dsp/tests/)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - cross-platform
**Project Type**: VST3 plugin monorepo, shared DSP library
**Performance Goals**: < 50ns per sample (SC-011), simpler than crossover filter
**Constraints**: Real-time safe (noexcept, no allocations in process), allocation-free after prepare()
**Scale/Scope**: Single processor class, ~300-400 lines header

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No memory allocation in process methods
- [x] No locks/mutexes in audio thread
- [x] No file I/O in audio thread
- [x] No exceptions in audio thread
- [x] Pre-allocate all buffers in prepare()

**Required Check - Principle III (Modern C++):**
- [x] C++20 standard
- [x] RAII for resource management
- [x] constexpr where applicable
- [x] No raw new/delete

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 2 processor depends only on Layer 0/1
- [x] Biquad is Layer 1
- [x] OnePoleSmoother is Layer 1
- [x] filter_tables.h is Layer 0

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: FormantFilter

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| FormantFilter | `grep -r "class FormantFilter" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (all utilities exist)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| lerp | N/A | Yes (std::lerp C++20) | standard library | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Biquad | dsp/include/krate/dsp/primitives/biquad.h | 1 | 3 parallel bandpass filters for F1, F2, F3 |
| FilterType::Bandpass | dsp/include/krate/dsp/primitives/biquad.h | 1 | Configure Biquad as bandpass |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Smooth formant frequencies and bandwidths |
| Vowel | dsp/include/krate/dsp/core/filter_tables.h | 0 | Type-safe vowel selection enum |
| FormantData | dsp/include/krate/dsp/core/filter_tables.h | 0 | Struct with f1, f2, f3, bw1, bw2, bw3 |
| kVowelFormants | dsp/include/krate/dsp/core/filter_tables.h | 0 | Constexpr array of FormantData for 5 vowels |
| getFormant() | dsp/include/krate/dsp/core/filter_tables.h | 0 | Type-safe accessor for formant table |
| kButterworthQ | dsp/include/krate/dsp/primitives/biquad.h | 1 | Reference for Q calculation pattern |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/filter_tables.h` - FormantData, Vowel, kVowelFormants exist
- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad, FilterType::Bandpass exist
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother exists
- [x] `dsp/include/krate/dsp/processors/` - No FormantFilter exists

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: FormantFilter is a unique new class. All dependencies exist and are well-documented. The Vowel enum and FormantData struct are already defined in filter_tables.h, eliminating risk of duplicate definitions.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Biquad | configure | `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Biquad | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| FormantData | f1, f2, f3 | `float f1; float f2; float f3;` | Yes |
| FormantData | bw1, bw2, bw3 | `float bw1; float bw2; float bw3;` | Yes |
| kVowelFormants | array access | `inline constexpr std::array<FormantData, kNumVowels> kVowelFormants` | Yes |
| getFormant | helper function | `constexpr FormantData getFormant(Vowel v) noexcept` | Yes |
| Vowel | enum values | `enum class Vowel : uint8_t { A = 0, E = 1, I = 2, O = 3, U = 4 }` | Yes |
| kNumVowels | constant | `inline constexpr size_t kNumVowels = 5;` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad class, FilterType enum
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/filter_tables.h` - FormantData, Vowel, kVowelFormants
- [x] `dsp/include/krate/dsp/processors/multimode_filter.h` - Reference for Layer 2 API patterns
- [x] `dsp/include/krate/dsp/processors/crossover_filter.h` - Reference for Layer 2 API patterns

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| OnePoleSmoother | `process()` returns smoothed value AND advances state | Store return value |
| Biquad::configure | Q parameter is 4th, gainDb is not used for Bandpass | `configure(FilterType::Bandpass, freq, Q, 0.0f, sr)` |
| kVowelFormants | Use getFormant() helper for type-safe access | `getFormant(Vowel::A)` preferred over `kVowelFormants[static_cast<size_t>(Vowel::A)]` |
| filter_tables.h | Uses bass male voice values from Csound | Formant freqs differ from some academic sources |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None | All needed utilities already exist in Layer 0/1 | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateTargetFormants() | Class-specific, uses internal state (vowel, morph position, shift, gender). Sets target values for all 6 smoothers. |
| clampFrequency(float freq) | Class-specific helper, uses sampleRate_ member. Returns frequency clamped to [kMinFrequency, kMaxFrequencyRatio * sampleRate_]. |
| calculateQ(float freq, float bandwidth) | Pure helper, could be static. Returns Q = freq/bandwidth clamped to [kMinQ, kMaxQ]. |
| updateFilterCoefficients() | Class-specific, processes smoothers and configures all 3 Biquad filters. |

**Decision**: No new Layer 0 utilities needed. All math operations (pow, clamp, lerp) are standard library or already available.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from FLT-ROADMAP.md or known plans):
- EnvelopeFilter - different topology (single filter with envelope follower)
- CrossoverFilter - different topology (band splitting)
- Phaser - different topology (cascaded allpass)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| FormantFilter | LOW | None direct | Keep local |
| Vowel morphing algorithm | MEDIUM | VocalProcessor (future Layer 3/4) | Keep local, document for extraction if needed |
| Gender scaling constants | LOW | Unlikely | Keep local (spec-defined formula) |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | First formant processor, patterns not established |
| Keep formant data in filter_tables.h | Already exists there, good location |

### Review Trigger

After implementing **EnvelopeFilter or VocalProcessor**, review this section:
- [ ] Does sibling need vowel morphing? -> Consider extracting interpolation
- [ ] Does sibling use same filter composition pattern? -> Document shared pattern

## Project Structure

### Documentation (this feature)

```text
specs/077-formant-filter/
├── spec.md              # Feature specification (complete)
├── plan.md              # This file
├── research.md          # Phase 0 output (below)
├── data-model.md        # Phase 1 output (below)
├── quickstart.md        # Phase 1 output (below)
└── contracts/           # Phase 1 output (API contracts)
    └── formant_filter_api.h  # Header contract
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── core/
│   │   └── filter_tables.h      # Existing - FormantData, Vowel, kVowelFormants
│   ├── primitives/
│   │   ├── biquad.h             # Existing - Biquad, FilterType::Bandpass
│   │   └── smoother.h           # Existing - OnePoleSmoother
│   └── processors/
│       └── formant_filter.h     # NEW - FormantFilter class
└── tests/
    └── processors/
        └── formant_filter_test.cpp  # NEW - Unit tests
```

**Structure Decision**: Single header in processors/ with corresponding test file. Follows existing patterns (crossover_filter.h, multimode_filter.h).

## Complexity Tracking

No constitution violations to justify.

---

# Phase 0: Research

## Research Tasks Completed

### R1: Formant Filter Topology

**Decision**: Parallel bandpass filters (3 filters for F1, F2, F3)

**Rationale**:
- Industry standard for formant synthesis (Csound, Max/MSP, synth plugins)
- Each formant is independent and can be individually tuned
- Simple to implement with existing Biquad configured as Bandpass
- Sum of outputs produces formant spectral shape

**Alternatives Considered**:
- Series resonant filters: Rejected - harder to control, formants interact
- FIR formant filters: Rejected - much higher CPU cost, less modulation-friendly
- FFT-based formant: Rejected - latency, complexity, overkill for 3 formants

### R2: Q Calculation from Bandwidth

**Decision**: Q = frequency / bandwidth (clamped to [0.5, 20.0])

**Rationale**:
- Standard formula: Q = f0 / BW where BW is the 3dB bandwidth
- Clamping ensures filter stability and useful range
- Q < 0.5 is essentially flat (wasteful)
- Q > 20 risks instability and excessive resonance

**Formula Verification**:
```cpp
// From spec: FR-017
float Q = formantFrequency / bandwidth;
Q = std::clamp(Q, 0.5f, 20.0f);

// Example: Vowel A, F1
// Q = 600.0 / 60.0 = 10.0 (within range)
```

### R3: Vowel Morphing Implementation

**Decision**: Linear interpolation between adjacent vowels using morph position 0-4

**Rationale**:
- Position 0.0 = A, 1.0 = E, 2.0 = I, 3.0 = O, 4.0 = U
- Intermediate values interpolate between adjacent vowels
- std::lerp (C++20) provides efficient implementation
- Matches user expectation of smooth vowel transitions

**Implementation Pattern**:
```cpp
int lowerIdx = static_cast<int>(position);
int upperIdx = std::min(lowerIdx + 1, 4);
float fraction = position - static_cast<float>(lowerIdx);

float f1 = std::lerp(kVowelFormants[lowerIdx].f1,
                     kVowelFormants[upperIdx].f1,
                     fraction);
// Repeat for f2, f3, bw1, bw2, bw3
```

### R4: Formant Shift Formula

**Decision**: Exponential pitch scaling with semitones

**Rationale**:
- Standard pitch shifting formula: multiplier = pow(2, semitones/12)
- Preserves musical intervals
- +12 semitones = 2x frequency (one octave up)
- -12 semitones = 0.5x frequency (one octave down)

**Formula**:
```cpp
float shiftMultiplier = std::pow(2.0f, semitones / 12.0f);
float shiftedFreq = baseFreq * shiftMultiplier;
```

### R5: Gender Scaling Formula

**Decision**: Exponential scaling with +/-0.25 octave range (per spec clarification Q3)

**Rationale**:
- Exponential provides perceptually uniform changes
- +/-0.25 octave matches typical male-female formant differences (~19%)
- Simple formula: multiplier = pow(2, gender * 0.25)

**Formula**:
```cpp
float genderMultiplier = std::pow(2.0f, gender * 0.25f);
// gender = -1.0 -> 0.841 (~-17%)
// gender =  0.0 -> 1.000 (neutral)
// gender = +1.0 -> 1.189 (~+19%)
```

### R6: Parameter Smoothing Strategy

**Decision**: 6 OnePoleSmoother instances (3 frequencies + 3 bandwidths), default 5ms

**Rationale**:
- Smoothing frequencies independently allows natural transitions
- Smoothing bandwidths maintains Q stability during formant changes
- 5ms default provides click-free modulation without noticeable lag
- Matches existing pattern in MultimodeFilter and CrossoverFilter

**Implementation Pattern**:
```cpp
std::array<OnePoleSmoother, 3> freqSmoothers_;  // F1, F2, F3
std::array<OnePoleSmoother, 3> bwSmoothers_;    // BW1, BW2, BW3
```

### R7: Frequency Clamping Strategy

**Decision**: Clamp after all transformations (morph + shift + gender)

**Rationale**:
- Final frequency must be in valid range regardless of parameter combination
- Lower bound: 20Hz (below audible, filter instability)
- Upper bound: 0.45 * sampleRate (Nyquist margin for filter stability)
- Same pattern as existing filters (see biquad.h detail::clampFrequency)

**Formula**:
```cpp
float clampFormantFrequency(float freq, double sampleRate) {
    const float minFreq = 20.0f;
    const float maxFreq = static_cast<float>(sampleRate) * 0.45f;
    return std::clamp(freq, minFreq, maxFreq);
}
```

### R8: Existing Formant Data Review

**Decision**: Use kVowelFormants from filter_tables.h as-is

**Rationale**:
- Already exists in codebase (spec 070-filter-foundations)
- Based on Csound bass male voice values (industry standard)
- Frequencies and bandwidths are well-tested values
- No need to introduce alternative formant tables

**Existing Data Verification**:
```cpp
// From filter_tables.h
// Vowel A: F1=600, F2=1040, F3=2250, BW1=60, BW2=70, BW3=110
// Vowel E: F1=400, F2=1620, F3=2400, BW1=40, BW2=80, BW3=100
// Vowel I: F1=250, F2=1750, F3=2600, BW1=60, BW2=90, BW3=100
// Vowel O: F1=400, F2=750,  F3=2400, BW1=40, BW2=80, BW3=100
// Vowel U: F1=350, F2=600,  F3=2400, BW1=40, BW2=80, BW3=100
```

## Research Summary

All NEEDS CLARIFICATION items from spec have been resolved:
1. Formant topology: Parallel bandpass
2. Q calculation: frequency/bandwidth with clamping
3. Vowel morphing: Linear interpolation (std::lerp)
4. Shift formula: Exponential pitch scaling
5. Gender formula: Exponential with 0.25 octave range
6. Smoothing: 6 OnePoleSmoother instances
7. Frequency clamping: [20Hz, 0.45*sampleRate]
8. Formant data: Existing kVowelFormants

---

# Phase 1: Design

## Data Model

### FormantFilter Class

```cpp
class FormantFilter {
public:
    // Constants
    static constexpr int kNumFormants = 3;
    static constexpr float kMinFrequency = 20.0f;
    static constexpr float kMaxFrequencyRatio = 0.45f;
    static constexpr float kMinQ = 0.5f;
    static constexpr float kMaxQ = 20.0f;
    static constexpr float kMinShift = -24.0f;
    static constexpr float kMaxShift = 24.0f;
    static constexpr float kMinGender = -1.0f;
    static constexpr float kMaxGender = 1.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;

private:
    // Filter stages (3 parallel bandpass)
    std::array<Biquad, kNumFormants> formants_;

    // Parameter smoothers (3 frequencies + 3 bandwidths)
    std::array<OnePoleSmoother, kNumFormants> freqSmoothers_;
    std::array<OnePoleSmoother, kNumFormants> bwSmoothers_;

    // Parameters
    Vowel currentVowel_ = Vowel::A;
    float vowelMorphPosition_ = 0.0f;  // 0-4
    float formantShift_ = 0.0f;        // semitones, -24 to +24
    float gender_ = 0.0f;              // -1 to +1
    float smoothingTime_ = kDefaultSmoothingMs;  // Valid range: [0.1ms, 1000ms]

    // State
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
    bool useMorphMode_ = false;  // Mode flag: true = use vowelMorphPosition_ for interpolation, false = use currentVowel_ for discrete selection. Determines which parameter calculateTargetFormants() uses.
};
```

### State Transitions

```
Initial State -> prepare() -> Ready State
Ready State -> setVowel() -> Discrete Mode
Ready State -> setVowelMorph() -> Morph Mode
Ready/Processing -> reset() -> Ready State (filters cleared)
Ready/Processing -> process()/processBlock() -> Processing
```

## API Contract

See `contracts/formant_filter_api.h` for complete header contract.

### Public API Summary

```cpp
// Lifecycle
void prepare(double sampleRate) noexcept;
void reset() noexcept;

// Vowel Selection
void setVowel(Vowel vowel) noexcept;
void setVowelMorph(float position) noexcept;  // 0-4

// Formant Modification
void setFormantShift(float semitones) noexcept;  // -24 to +24
void setGender(float amount) noexcept;           // -1 to +1

// Smoothing
void setSmoothingTime(float ms) noexcept;

// Processing
[[nodiscard]] float process(float input) noexcept;
void processBlock(float* buffer, size_t numSamples) noexcept;
```

## Quickstart Guide

See `quickstart.md` for usage examples.

---

# Post-Design Constitution Re-Check

**Principle II (Real-Time Safety):**
- [x] `process()` and `processBlock()` are noexcept
- [x] No allocations in processing (std::array fixed size)
- [x] No locks/mutexes (single-threaded audio processing)

**Principle IX (Layer Architecture):**
- [x] FormantFilter (Layer 2) depends only on Biquad (Layer 1), OnePoleSmoother (Layer 1), filter_tables.h (Layer 0)

**Principle XIV (ODR Prevention):**
- [x] FormantFilter is unique - no existing class with this name
- [x] All dependencies verified as existing

**All constitution checks pass.**

---

# Generated Artifacts

| Artifact | Path | Status |
|----------|------|--------|
| Implementation Plan | `specs/077-formant-filter/plan.md` | Complete |
| Research | `specs/077-formant-filter/research.md` | Inline above |
| Data Model | `specs/077-formant-filter/data-model.md` | To generate |
| API Contract | `specs/077-formant-filter/contracts/formant_filter_api.h` | To generate |
| Quickstart | `specs/077-formant-filter/quickstart.md` | To generate |
