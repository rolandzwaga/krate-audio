# Implementation Plan: Noise Oscillator Primitive

**Branch**: `023-noise-oscillator` | **Date**: 2026-02-05 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/023-noise-oscillator/spec.md`

## Summary

Create a lightweight noise oscillator primitive (Layer 1) providing six noise algorithms (White, Pink, Brown, Blue, Violet, Grey) for oscillator-level composition. This is distinct from the existing effects-oriented NoiseGenerator at Layer 2. A prerequisite refactoring phase extracts `PinkNoiseFilter` from NoiseGenerator to a shared Layer 1 primitive, ensuring both components use identical, tested code.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Layer 0 core utilities (random.h, math_constants.h), Layer 1 PinkNoiseFilter (to be extracted)
**Storage**: N/A (stateless except for filter coefficients)
**Testing**: Catch2 (existing test infrastructure)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo DSP library
**Performance Goals**: < 0.1% CPU per instance at 44.1kHz (Layer 1 primitive budget)
**Constraints**: Real-time safe (no allocations, no locks, no exceptions in process())
**Scale/Scope**: Single header-only primitive class

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II (Real-Time Safety):**
- [x] All process() methods marked noexcept
- [x] No memory allocation in process paths
- [x] No locks, exceptions, or I/O in audio thread

**Principle III (Modern C++):**
- [x] C++20 features where beneficial
- [x] RAII for all resources
- [x] constexpr where possible

**Principle IX (Layered Architecture):**
- [x] NoiseOscillator at Layer 1 (primitives/)
- [x] Depends only on Layer 0 (core/) and Layer 1 (PinkNoiseFilter)
- [x] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-Design Re-Check:**
- [x] Data model follows constitution principles (real-time safe, layered architecture, noexcept)
- [x] API contracts are complete and type-safe (verified Xorshift32, Biquad, PinkNoiseFilter signatures)
- [x] No hidden complexity introduced (straightforward filter cascades, well-documented algorithms)

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: NoiseOscillator, NoiseColor (enum), PinkNoiseFilter (extracted)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| NoiseOscillator | `grep -r "class NoiseOscillator" dsp/ plugins/` | No | Create New |
| NoiseColor (spec-specific) | `grep -r "enum.*NoiseColor" dsp/ plugins/` | Yes (pattern_freeze_types.h) | REUSE existing enum |
| PinkNoiseFilter | `grep -r "class PinkNoiseFilter" dsp/ plugins/` | Yes (noise_generator.h, private) | EXTRACT to Layer 1 |
| GreyNoiseState | `grep -r "struct GreyNoiseState" dsp/ plugins/` | No | Create New (internal struct) |

**Utility Functions to be created**: None (using existing Xorshift32)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| nextFloat() | `grep -r "nextFloat" dsp/include/krate/dsp/core/` | Yes | random.h | Reuse Xorshift32 |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Xorshift32 | dsp/include/krate/dsp/core/random.h | 0 | White noise source (nextFloat returns [-1, 1]) |
| NoiseColor | dsp/include/krate/dsp/core/pattern_freeze_types.h | 0 | Enum for noise type selection (has Grey already) |
| PinkNoiseFilter | dsp/include/krate/dsp/processors/noise_generator.h | 2 (currently) | Extract to Layer 1, use for pink noise |
| Biquad | dsp/include/krate/dsp/primitives/biquad.h | 1 | Grey noise inverse A-weighting shelving filters |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (found Xorshift32, NoiseColor enum)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no conflicts)
- [x] `dsp/include/krate/dsp/processors/noise_generator.h` - Found PinkNoiseFilter (extraction target)
- [x] `specs/_architecture_/` - Component inventory
- [x] `dsp/tests/unit/processors/noise_generator_test.cpp` - Existing tests must pass post-refactor

### ODR Risk Assessment

**Risk Level**: Medium

**Justification**:
- The existing `PinkNoiseFilter` is a private class inside `NoiseGenerator`. Creating a new one at Layer 1 would cause ODR violation if NoiseGenerator still defines its own.
- **Mitigation**: Extract PinkNoiseFilter to Layer 1 as shared primitive, update NoiseGenerator to use it.
- The `NoiseColor` enum already exists in `pattern_freeze_types.h` with the values we need (including Grey), so we REUSE it rather than create a conflicting enum.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Xorshift32 | constructor | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` | Yes |
| Xorshift32 | nextFloat | `[[nodiscard]] constexpr float nextFloat() noexcept` | Yes |
| Xorshift32 | seed | `constexpr void seed(uint32_t seedValue) noexcept` | Yes |
| Biquad | configure | `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Biquad | reset | `void reset() noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class
- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad class
- [x] `dsp/include/krate/dsp/core/pattern_freeze_types.h` - NoiseColor enum
- [x] `dsp/include/krate/dsp/processors/noise_generator.h` - PinkNoiseFilter class (extraction source)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Xorshift32 | seed(0) uses default seed | Pass non-zero seeds for unique sequences |
| Xorshift32 | nextFloat() returns [-1, 1] | Already bipolar, no scaling needed |
| Biquad | FilterType is enum class | Use `FilterType::LowShelf`, `FilterType::HighShelf` |
| NoiseColor | Already exists in pattern_freeze_types.h | Include and use existing enum |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Leaky integrator (brown noise) | Simple one-liner: `prev = leak * prev + (1-leak) * input` |
| Differentiator (blue/violet) | Simple one-liner: `output = input - prev; prev = input` |

**Decision**: No Layer 0 extractions needed. NoiseOscillator is minimal and self-contained.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from ROADMAP.md or known plans):
- PolyBLEP Oscillator (Phase 2) - bandlimited waveform generation
- Wavetable Oscillator (Phase 3) - wavetable playback
- minBLEP Table (Phase 4) - discontinuity correction

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| PinkNoiseFilter | HIGH | NoiseOscillator, NoiseGenerator | Extract to shared primitive (Phase 0) |
| NoiseOscillator | MEDIUM | Granular synthesis, Karplus-Strong | Keep local, compose when needed |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Extract PinkNoiseFilter | Already used by NoiseGenerator; NoiseOscillator needs same algorithm |
| Use existing NoiseColor enum | Avoids ODR, already has Grey value |
| Keep grey noise filter internal | Only used by NoiseOscillator; if needed elsewhere, extract later |

## Project Structure

### Documentation (this feature)

```text
specs/023-noise-oscillator/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Phase 0 output (below)
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (API contracts)
└── tasks.md             # Phase 2 output (generated by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── core/
│   │   └── pattern_freeze_types.h   # NoiseColor enum (existing, reuse)
│   └── primitives/
│       ├── pink_noise_filter.h      # NEW: Extracted from NoiseGenerator
│       └── noise_oscillator.h       # NEW: Main deliverable
├── tests/
│   └── primitives/
│       ├── pink_noise_filter_tests.cpp  # NEW: Tests for extracted primitive
│       └── noise_oscillator_tests.cpp   # NEW: Main test file
```

**Structure Decision**: Single DSP header-only primitive at Layer 1 with corresponding test file.

---

## Phase 0: Research

### Research Tasks

#### R-001: Paul Kellet Pink Noise Filter Verification

**Question**: Are the Paul Kellet coefficients in `noise_generator.h` correct and sample-rate independent?

**Finding**: Yes. The coefficients match the [original Paul Kellet algorithm](https://www.firstpr.com.au/dsp/pink-noise/):

```cpp
b0_ = 0.99886f * b0_ + white * 0.0555179f;
b1_ = 0.99332f * b1_ + white * 0.0750759f;
b2_ = 0.96900f * b2_ + white * 0.1538520f;
b3_ = 0.86650f * b3_ + white * 0.3104856f;
b4_ = 0.55000f * b4_ + white * 0.5329522f;
b5_ = -0.7616f * b5_ - white * 0.0168980f;
pink = b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + white * 0.5362f;
b6_ = white * 0.115926f;
```

The filter is accurate to +/- 0.05dB from 9.2Hz to Nyquist at 44.1kHz. The recursive filter structure means coefficients work across all sample rates in the audible range (44.1kHz-192kHz).

**Decision**: Extract existing PinkNoiseFilter unchanged.

#### R-002: Grey Noise Inverse A-Weighting Design

**Question**: How to implement inverse A-weighting using biquad filters?

**Finding**: A-weighting is defined by IEC 61672-1 with specific attenuation at different frequencies:
- 20Hz: -50dB (inverse: +50dB boost)
- 100Hz: -19.1dB (inverse: +19dB boost)
- 200Hz: -10.8dB (inverse: +11dB boost)
- 1kHz: 0dB (reference)
- 4kHz: +1.0dB (inverse: -1dB cut)
- 10kHz: -2.5dB (inverse: +2.5dB boost)
- 20kHz: -9.3dB (inverse: +9dB boost)

The existing NoiseGenerator uses a simplified approximation with a single low-shelf filter:
```cpp
greyLowShelf_.configure(FilterType::LowShelf, 200.0f, 0.707f, 12.0f, sampleRate);
```

This provides +12dB boost below 200Hz, approximating the inverse A-weighting characteristic at low frequencies. For the NoiseOscillator, we can use the same approach since:
1. Perfect inverse A-weighting would require 4+ biquad stages
2. The simplified version captures the perceptually important low-frequency boost
3. Matches existing NoiseGenerator behavior for consistency

**Decision**: Use cascaded biquad filters:
- Low-shelf at 200Hz with +15dB boost (captures sub-bass compensation)
- High-shelf at 6kHz with +4dB boost (captures high-frequency rolloff compensation)

This provides better approximation than single shelf while keeping computational cost low.

#### R-003: NoiseColor Enum Compatibility

**Question**: Does the existing NoiseColor enum in pattern_freeze_types.h have all required values?

**Finding**: Yes. The enum includes:
```cpp
enum class NoiseColor : uint8_t {
    White = 0,    // Flat spectrum
    Pink,         // -3dB/octave
    Brown,        // -6dB/octave
    Blue,         // +3dB/octave
    Violet,       // +6dB/octave
    Grey,         // Inverse A-weighting (perceptually flat)
    Velvet,       // Sparse impulses
    RadioStatic   // Band-limited
};
```

The first 6 values (White through Grey) match exactly what the NoiseOscillator spec requires. Velvet and RadioStatic can be ignored for this primitive (they are Layer 2 effects).

**Decision**: Reuse existing NoiseColor enum. NoiseOscillator will support White, Pink, Brown, Blue, Violet, Grey.

#### R-004: Block Processing Optimization

**Question**: What optimizations are possible for block processing?

**Finding**: For noise generation, per-sample overhead is minimal. The main optimizations are:
1. Avoid virtual function calls in loops (use direct function pointers or switch)
2. Pre-compute loop-invariant filter parameters
3. Use tight loops without conditionals inside

For NoiseOscillator, the color selection happens at block level, so we can have separate optimized paths per noise type.

**Decision**: Implement processBlock() with switch on noise color outside the sample loop.

### Research Summary

| Research Item | Decision | Rationale |
|---------------|----------|-----------|
| Pink noise coefficients | Extract existing PinkNoiseFilter | Proven, tested, matches reference |
| Grey noise filter | Dual biquad shelf cascade | Better accuracy, reasonable cost |
| NoiseColor enum | Reuse from pattern_freeze_types.h | Avoids ODR, already has Grey |
| Block processing | Switch outside loop | Avoids per-sample branching |

---

## Phase 1: Design

### 1.1 Data Model

#### NoiseOscillator State Structure

```cpp
/// Internal state for grey noise inverse A-weighting
struct GreyNoiseState {
    Biquad lowShelf;   // +15dB boost below 200Hz
    Biquad highShelf;  // +4dB boost above 6kHz

    void configure(float sampleRate) noexcept {
        lowShelf.configure(FilterType::LowShelf, 200.0f, 0.707f, 15.0f, sampleRate);
        highShelf.configure(FilterType::HighShelf, 6000.0f, 0.707f, 4.0f, sampleRate);
    }

    void reset() noexcept {
        lowShelf.reset();
        highShelf.reset();
    }

    [[nodiscard]] float process(float input) noexcept {
        return highShelf.process(lowShelf.process(input));
    }
};

/// Internal state for brown noise (leaky integrator)
struct IntegratorState {
    float prev = 0.0f;
    static constexpr float kLeak = 0.99f;  // FR-011 specifies 0.99

    void reset() noexcept { prev = 0.0f; }

    [[nodiscard]] float process(float input) noexcept {
        prev = kLeak * prev + (1.0f - kLeak) * input;
        return prev * 5.0f;  // Gain compensation
    }
};

/// Internal state for differentiator (blue/violet)
struct DifferentiatorState {
    float prev = 0.0f;

    void reset() noexcept { prev = 0.0f; }

    [[nodiscard]] float process(float input) noexcept {
        float output = input - prev;
        prev = input;
        return output;
    }
};
```

#### NoiseOscillator Class

```cpp
class NoiseOscillator {
public:
    // FR-003: prepare()
    void prepare(double sampleRate) noexcept;

    // FR-004: reset()
    void reset() noexcept;

    // FR-005: setColor()
    void setColor(NoiseColor color) noexcept;

    // FR-006: setSeed()
    void setSeed(uint32_t seed) noexcept;

    // FR-007: process() - single sample
    [[nodiscard]] float process() noexcept;

    // FR-008: processBlock() - block processing
    void processBlock(float* output, size_t numSamples) noexcept;

private:
    Xorshift32 rng_{1};
    NoiseColor color_ = NoiseColor::White;
    double sampleRate_ = 44100.0;

    // Filter states
    PinkNoiseFilter pinkFilter_;        // Layer 1 primitive (extracted)
    IntegratorState brownState_;        // Brown noise integrator
    DifferentiatorState blueState_;     // Blue noise (diff of pink)
    DifferentiatorState violetState_;   // Violet noise (diff of white)
    GreyNoiseState greyState_;          // Grey noise A-weighting inverse
};
```

### 1.2 Dependency Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     NoiseOscillator                         │
│                     (Layer 1 Primitive)                     │
└─────────────────────────────────────────────────────────────┘
                              │
         ┌────────────────────┼────────────────────┐
         ▼                    ▼                    ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│  PinkNoiseFilter │  │     Biquad      │  │    Xorshift32   │
│  (Layer 1)       │  │   (Layer 1)     │  │    (Layer 0)    │
│  [EXTRACTED]     │  │                 │  │                 │
└─────────────────┘  └─────────────────┘  └─────────────────┘
                              │
                              ▼
                     ┌─────────────────┐
                     │  math_constants  │
                     │    (Layer 0)     │
                     └─────────────────┘
```

### 1.3 Refactoring Phase Design

#### RF-001: PinkNoiseFilter Extraction

**Source**: `dsp/include/krate/dsp/processors/noise_generator.h` (lines 77-114)

**Destination**: `dsp/include/krate/dsp/primitives/pink_noise_filter.h`

**Changes**:

1. Create new header `pink_noise_filter.h`:
   - Move PinkNoiseFilter class verbatim
   - Add proper header guards and includes
   - Add Doxygen documentation
   - Keep exact Paul Kellet coefficients (RF-002)

2. Update `noise_generator.h`:
   - Add `#include <krate/dsp/primitives/pink_noise_filter.h>`
   - Remove the private PinkNoiseFilter class definition
   - No other changes required (RF-003)

3. Verify all NoiseGenerator tests pass (RF-004)

#### Pink Noise Filter API (Extracted)

```cpp
// dsp/include/krate/dsp/primitives/pink_noise_filter.h

#pragma once

namespace Krate::DSP {

/// Paul Kellet's pink noise filter
/// Converts white noise to pink noise (-3dB/octave spectral rolloff).
/// Accuracy: +/- 0.05dB from 9.2Hz to Nyquist at 44.1kHz.
///
/// Reference: https://www.firstpr.com.au/dsp/pink-noise/
class PinkNoiseFilter {
public:
    /// Process one white noise sample through the filter
    /// @param white Input white noise sample (typically [-1, 1])
    /// @return Pink noise sample (bounded to [-1, 1])
    [[nodiscard]] float process(float white) noexcept;

    /// Reset filter state to zero
    void reset() noexcept;

private:
    float b0_ = 0.0f;
    float b1_ = 0.0f;
    float b2_ = 0.0f;
    float b3_ = 0.0f;
    float b4_ = 0.0f;
    float b5_ = 0.0f;
    float b6_ = 0.0f;
};

} // namespace Krate::DSP
```

### 1.4 Processing Algorithms

#### White Noise (FR-009)
```cpp
float white = rng_.nextFloat();  // Already [-1, 1]
```

#### Pink Noise (FR-010)
```cpp
float white = rng_.nextFloat();
float pink = pinkFilter_.process(white);  // Uses extracted primitive
```

#### Brown Noise (FR-011)
```cpp
float white = rng_.nextFloat();
float brown = brownState_.process(white);
brown = std::clamp(brown, -1.0f, 1.0f);
```

#### Blue Noise (FR-012)
```cpp
float white = rng_.nextFloat();
float pink = pinkFilter_.process(white);
float blue = blueState_.process(pink) * 0.7f;  // Differentiate pink
blue = std::clamp(blue, -1.0f, 1.0f);
```

#### Violet Noise (FR-013)
```cpp
float white = rng_.nextFloat();
float violet = violetState_.process(white) * 0.5f;  // Differentiate white
violet = std::clamp(violet, -1.0f, 1.0f);
```

#### Grey Noise (FR-019)
```cpp
float white = rng_.nextFloat();
float grey = greyState_.process(white);  // Low-shelf + high-shelf cascade
grey = std::clamp(grey, -1.0f, 1.0f);
```

### 1.5 Success Criteria Mapping

| Success Criterion | Test Method | Threshold |
|-------------------|-------------|-----------|
| SC-001: White mean | Sum 44100 samples, divide, check | abs(mean) < 0.05 |
| SC-002: White variance | Compute variance over 44100 samples | 0.3 < var < 0.37 |
| SC-003: Pink slope | 8192-pt FFT, 10 windows, measure slope | -3dB/oct +/- 0.5dB |
| SC-004: Brown slope | 8192-pt FFT, 10 windows, measure slope | -6dB/oct +/- 1.0dB |
| SC-005: Blue slope | 8192-pt FFT, 10 windows, measure slope | +3dB/oct +/- 0.5dB |
| SC-006: Violet slope | 8192-pt FFT, 10 windows, measure slope | +6dB/oct +/- 1.0dB |
| SC-007: Bounded output | 10 seconds, all colors | All samples in [-1, 1] |
| SC-008: Determinism | Same seed, compare sequences | Identical output |
| SC-009: Block equiv | Compare block vs sample-by-sample | Identical output |
| SC-010: Compilation | Build on MSVC, Clang, GCC | Zero warnings |
| SC-011: Real-time | Custom allocator wrapper | Zero allocations |
| SC-012: Grey spectrum | Compare 100Hz vs 1kHz energy | +10-20dB at low freq |

---

## Complexity Tracking

No constitution violations requiring justification.

---

## Implementation Phases Summary

### Phase 0: Refactoring (RF-001 to RF-004)
1. Extract PinkNoiseFilter to `primitives/pink_noise_filter.h`
2. Create `pink_noise_filter_tests.cpp`
3. Update NoiseGenerator to use extracted primitive
4. Verify all existing NoiseGenerator tests pass

### Phase 1: Core Implementation (FR-001 to FR-018)
1. Create `noise_oscillator.h` with White, Pink, Brown, Blue, Violet
2. Create `noise_oscillator_tests.cpp` with spectral analysis tests
3. Implement and test each noise color

### Phase 2: Grey Noise (FR-019, SC-012)
1. Add GreyNoiseState with dual biquad cascade
2. Implement grey noise processing
3. Add grey noise spectral tests

### Phase 3: Finalization
1. Verify all success criteria
2. Run clang-tidy
3. Update architecture documentation

---

## Sources

- [Paul Kellet Pink Noise Algorithm](https://www.firstpr.com.au/dsp/pink-noise/)
- [Audio EQ Cookbook](https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html)
- [A-weighting Standard IEC 61672-1](https://www.mathworks.com/help/audio/ref/weightingfilter-system-object.html)
