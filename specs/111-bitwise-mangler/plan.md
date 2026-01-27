# Implementation Plan: BitwiseMangler

**Branch**: `111-bitwise-mangler` | **Date**: 2026-01-27 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/111-bitwise-mangler/spec.md`

## Summary

BitwiseMangler is a Layer 1 DSP primitive implementing bit manipulation distortion for "wild tonal shifts" through six operation modes: XorPattern, XorPrevious, BitRotate, BitShuffle, BitAverage, and OverflowWrap. The processor converts floating-point audio samples to 24-bit integer representation, applies bitwise operations, and converts back to float. Core novelty lies in treating audio as raw binary data for manipulation.

## Technical Context

**Language/Version**: C++20 (matching existing codebase)
**Primary Dependencies**: Layer 0 core utilities only (random.h for Xorshift32, db_utils.h for NaN/Inf/denormal handling)
**Storage**: N/A (stateless except for previous sample state in XorPrevious mode and permutation table in BitShuffle)
**Testing**: Catch2 (per testing.md architecture)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - cross-platform VST3
**Project Type**: Single DSP primitive in shared library
**Performance Goals**: < 0.1% CPU per instance at 44100Hz (Layer 1 primitive budget)
**Constraints**: Real-time safe (no allocations in process), zero latency, noexcept

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No allocations in audio callbacks (std::array for permutation, no std::vector)
- [x] No locks, mutexes, or blocking primitives (pure function processing)
- [x] No file I/O, network ops, or system calls in process()
- [x] No throw/catch in process()

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 1 primitive depends only on Layer 0 (core utilities)
- [x] No circular dependencies
- [x] Will be placed in `dsp/include/krate/dsp/primitives/`

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle X (DSP Constraints):**
- [x] No oversampling needed (bit operations are not aliasing-prone waveshaping)
- [x] Consider DC blocking if operations introduce offset (defer to higher layer)

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: BitwiseMangler, BitwiseOperation (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| BitwiseMangler | `grep -r "class BitwiseMangler" dsp/ plugins/` | No | Create New |
| BitwiseOperation | `grep -r "BitwiseOperation" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: floatToInt24, int24ToFloat (inline helpers)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| floatToInt24 | `grep -r "floatToInt" dsp/ plugins/` | No | N/A | Create as inline in header |
| int24ToFloat | `grep -r "intToFloat" dsp/ plugins/` | No | N/A | Create as inline in header |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Xorshift32 | dsp/include/krate/dsp/core/random.h | 0 | PRNG for BitShuffle permutation generation |
| detail::isNaN | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN input detection (FR-022) |
| detail::isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Inf input detection (FR-022) |
| detail::flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal flushing (FR-023) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no BitwiseMangler)
- [x] `specs/_architecture_/` - Component inventory (no existing bitwise components)
- [x] `dsp/include/krate/dsp/primitives/bit_crusher.h` - Reference for float-to-int patterns (different approach: quantization levels)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are unique and not found in codebase. BitwiseMangler and BitwiseOperation are completely new concepts. The float-to-int conversion will be inline helper functions in the BitwiseMangler header, not extracted to Layer 0 (only one consumer initially).

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Xorshift32 | constructor | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` | Yes |
| Xorshift32 | next() | `[[nodiscard]] constexpr uint32_t next() noexcept` | Yes |
| Xorshift32 | seed() | `constexpr void seed(uint32_t value) noexcept` | Yes |
| detail::isNaN | function | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | function | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| detail::flushDenormal | function | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - isNaN, isInf, flushDenormal functions

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Xorshift32 | Zero seed replaced with default | Pass non-zero seed or accept default |
| detail::isNaN | Requires -fno-fast-math compile flag | Source file needs -fno-fast-math |
| detail::flushDenormal | Returns 0 for very small values | Apply after processing, before output |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

This is a Layer 1 primitive. Analysis for potential Layer 0 extraction:

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| floatToInt24 / int24ToFloat | DEFER - only 1 consumer | N/A | Only BitwiseMangler for now |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| floatToInt24 | Only one consumer, inline helper specific to this primitive |
| int24ToFloat | Only one consumer, inline helper specific to this primitive |
| shuffleBits | Specific to BitShuffle mode, not general-purpose |
| rotateBits | Could be reused but very simple, keep local |

**Decision**: Keep all conversion and bit manipulation functions as inline private helpers in BitwiseMangler header. If future features (spec 112-aliasing-effect, 113-granular-distortion, 114-fractal-distortion) need similar conversion, extract to `dsp/include/krate/dsp/core/bit_utils.h` at that time.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from DST-ROADMAP.md Priority 8):
- 112-aliasing-effect - Intentional aliasing processor (Layer 2, different approach)
- 113-granular-distortion - Per-grain variable distortion (Layer 2, composition)
- 114-fractal-distortion - Recursive multi-scale distortion (Layer 2, composition)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| floatToInt24/int24ToFloat | MEDIUM | 112-aliasing if it uses bit manipulation | Keep local, extract after 2nd use |
| BitwiseOperation enum pattern | LOW | Different processors have different modes | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep helpers local | First feature using 24-bit int conversion; wait for concrete 2nd consumer |
| No shared base class | Bit manipulation is unique pattern, not shared with sibling features |

### Review Trigger

After implementing **112-aliasing-effect**, review this section:
- [ ] Does aliasing effect need float-to-int conversion? -> Extract to bit_utils.h
- [ ] Does aliasing effect use similar bit operations? -> Consider shared patterns

## Project Structure

### Documentation (this feature)

```text
specs/111-bitwise-mangler/
├── plan.md              # This file
├── research.md          # Phase 0 output (below)
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # N/A (no API contracts, this is internal DSP)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── primitives/
│       └── bitwise_mangler.h    # Header-only Layer 1 primitive
└── tests/
    └── unit/
        └── primitives/
            └── bitwise_mangler_test.cpp  # Catch2 tests
```

**Structure Decision**: Header-only implementation following the pattern of BitCrusher, Waveshaper, and other Layer 1 primitives. All implementation in header file for inline optimization.

## Complexity Tracking

> No constitution violations. Feature is straightforward Layer 1 primitive.

---

# Phase 0: Research

## Research Tasks

### R1: Float-to-Int Conversion (FR-025, FR-026, FR-026a)

**Decision**: Multiply by 8388608 (2^23) as specified in clarifications.

**Rationale**:
- Standard audio DSP practice matches float mantissa precision
- 24-bit range: -8388608 to +8388607
- Formula: `int24 = static_cast<int32_t>(floatSample * 8388608.0f)`
- Inverse: `floatSample = static_cast<float>(int24) / 8388608.0f`
- Slight asymmetry acceptable (standard audio conversion approach)

**Alternatives considered**:
- 2^24: Would exceed 24-bit signed range
- 2^23-1: More complex for no practical benefit
- Symmetric scaling: Adds complexity without audible improvement

### R2: XOR Pattern Implementation (FR-010, FR-011, FR-012)

**Decision**: Direct XOR with configurable 32-bit pattern on 24-bit integer representation.

**Rationale**:
- XOR is reversible and creates predictable spectral changes
- Pattern 0xAAAAAAAA alternates bits (1010...) for interesting harmonics
- Pattern 0x55555555 is complement (0101...) for different character
- Pattern 0xFFFFFFFF inverts all bits (maximum mangling)
- Pattern 0x00000000 is identity (bypass)

**Implementation**:
```cpp
int32_t mangledInt = intSample ^ (static_cast<int32_t>(pattern_) & 0x00FFFFFF);
```

### R3: XorPrevious Implementation (FR-028, FR-029)

**Decision**: Store previous sample as int32_t, XOR with current sample.

**Rationale**:
- Signal-dependent distortion responds to input character
- High-frequency content changes more between adjacent samples -> more XOR difference
- Low-frequency content changes slowly -> less XOR difference
- Creates natural transient emphasis

**Implementation**:
```cpp
int32_t mangledInt = intSample ^ previousSampleInt_;
previousSampleInt_ = intSample;  // Update state
```

### R4: Bit Rotation Implementation (FR-013, FR-014, FR-015)

**Decision**: Circular rotation on 24-bit value using standard rotate pattern.

**Rationale**:
- Left rotation by N bits: `(x << N) | (x >> (24 - N))`
- Right rotation by N bits: `(x >> N) | (x << (24 - N))`
- Amount clamped to [-16, +16] for meaningful range
- Modulo 24 applied internally (rotation by 24 = no change)

**Implementation**:
```cpp
// Normalize to unsigned for rotation
uint32_t unsignedVal = static_cast<uint32_t>(intSample) & 0x00FFFFFF;
int amount = rotateAmount_ % 24;
if (amount > 0) {
    unsignedVal = ((unsignedVal << amount) | (unsignedVal >> (24 - amount))) & 0x00FFFFFF;
} else if (amount < 0) {
    int absAmount = -amount;
    unsignedVal = ((unsignedVal >> absAmount) | (unsignedVal << (24 - absAmount))) & 0x00FFFFFF;
}
// Convert back to signed 24-bit
int32_t mangledInt = static_cast<int32_t>(unsignedVal);
if (mangledInt & 0x00800000) mangledInt |= 0xFF000000;  // Sign extend
```

### R5: BitShuffle Permutation Generation (FR-016, FR-017, FR-018a, FR-018b)

**Decision**: Pre-compute permutation table on setSeed() using Fisher-Yates shuffle.

**Rationale**:
- Fisher-Yates (Knuth shuffle) produces unbiased random permutation
- Pre-computing avoids PRNG calls during process() (real-time safe)
- 24-element array maps bit position i to permutation_[i]
- std::array<uint8_t, 24> is fixed-size, stack-allocated

**Implementation**:
```cpp
void generatePermutation(uint32_t seed) noexcept {
    Xorshift32 rng(seed);
    // Initialize identity permutation
    for (uint8_t i = 0; i < 24; ++i) {
        permutation_[i] = i;
    }
    // Fisher-Yates shuffle
    for (int i = 23; i > 0; --i) {
        // Generate random index in [0, i]
        uint32_t r = rng.next();
        int j = static_cast<int>(r % static_cast<uint32_t>(i + 1));
        std::swap(permutation_[i], permutation_[j]);
    }
}
```

**Apply permutation**:
```cpp
uint32_t shuffleBits(uint32_t input) const noexcept {
    uint32_t output = 0;
    for (int i = 0; i < 24; ++i) {
        if (input & (1u << i)) {
            output |= (1u << permutation_[i]);
        }
    }
    return output;
}
```

### R6: BitAverage AND Implementation (FR-030, FR-031, FR-032)

**Decision**: Bitwise AND with previous sample (fixed operation per clarification).

**Rationale**:
- AND preserves only bits that are set in both samples
- Creates "smoothing" effect - output tends toward zero for differing samples
- Maintains state like XorPrevious for sample-by-sample processing
- Future parameter candidate for AND/OR selection (noted in spec)

**Implementation**:
```cpp
int32_t mangledInt = intSample & previousSampleInt_;
previousSampleInt_ = intSample;
```

### R7: OverflowWrap Implementation (FR-033, FR-034, FR-034a)

**Decision**: Convert to int, allow wrap on conversion, convert back.

**Rationale**:
- Input may exceed [-1, 1] from upstream processing
- Two's complement wrap naturally occurs when casting to int32_t
- No internal gain applied (per clarification)
- Output may exceed [-1, 1] after unwrap - user must handle downstream

**Implementation**:
```cpp
// Input may be > 1.0 or < -1.0 from upstream
// Multiplying by 8388608 and casting wraps naturally
int32_t intSample = static_cast<int32_t>(input * 8388608.0f);
// Output conversion - may exceed [-1, 1] if wrapping occurred
float output = static_cast<float>(intSample) / 8388608.0f;
```

### R8: Intensity Blending (FR-007, FR-008, FR-009)

**Decision**: Linear blend between original and mangled signal.

**Rationale**:
- Formula: `output = original * (1 - intensity) + mangled * intensity`
- Intensity 0.0 = bypass (bit-exact passthrough per SC-009)
- Intensity 1.0 = full effect
- Applied after int-to-float conversion

**Implementation**:
```cpp
// Special case for bypass
if (intensity_ <= 0.0f) {
    return input;  // Bit-exact passthrough
}
// Full processing
float mangled = int24ToFloat(mangledInt);
return input * (1.0f - intensity_) + mangled * intensity_;
```

### R9: NaN/Inf/Denormal Handling (FR-022, FR-023, FR-024)

**Decision**: Use existing db_utils.h detail namespace functions.

**Rationale**:
- detail::isNaN and detail::isInf use bit manipulation (immune to -ffast-math)
- detail::flushDenormal prevents CPU slowdowns
- Return 0.0f on NaN/Inf (per FR-022)
- Flush denormals before final output

**Implementation**:
```cpp
[[nodiscard]] float process(float x) noexcept {
    // Handle invalid input
    if (detail::isNaN(x) || detail::isInf(x)) {
        return 0.0f;
    }
    // Flush denormals
    x = detail::flushDenormal(x);
    // ... processing ...
    return detail::flushDenormal(output);
}
```

---

# Phase 1: Design

## Data Model

### BitwiseOperation Enum

```cpp
/// @brief Bit manipulation operation mode selection.
enum class BitwiseOperation : uint8_t {
    XorPattern = 0,    ///< XOR with configurable 32-bit pattern
    XorPrevious = 1,   ///< XOR current sample with previous sample
    BitRotate = 2,     ///< Circular bit rotation left/right
    BitShuffle = 3,    ///< Deterministic bit permutation from seed
    BitAverage = 4,    ///< Bitwise AND with previous sample
    OverflowWrap = 5   ///< Integer overflow wrap behavior
};
```

### BitwiseMangler Class

**Public Interface**:
```cpp
class BitwiseMangler {
public:
    // Constants
    static constexpr float kDefaultIntensity = 1.0f;
    static constexpr uint32_t kDefaultPattern = 0xAAAAAAAAu;
    static constexpr int kMinRotateAmount = -16;
    static constexpr int kMaxRotateAmount = 16;
    static constexpr uint32_t kDefaultSeed = 12345u;

    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Operation selection
    void setOperation(BitwiseOperation op) noexcept;
    [[nodiscard]] BitwiseOperation getOperation() const noexcept;

    // Parameters
    void setIntensity(float intensity) noexcept;          // [0.0, 1.0]
    [[nodiscard]] float getIntensity() const noexcept;

    void setPattern(uint32_t pattern) noexcept;           // XorPattern mode
    [[nodiscard]] uint32_t getPattern() const noexcept;

    void setRotateAmount(int bits) noexcept;              // [-16, +16], BitRotate mode
    [[nodiscard]] int getRotateAmount() const noexcept;

    void setSeed(uint32_t seed) noexcept;                 // BitShuffle mode
    [[nodiscard]] uint32_t getSeed() const noexcept;

    // Processing
    [[nodiscard]] float process(float x) noexcept;
    void processBlock(float* buffer, size_t n) noexcept;

private:
    // Conversion helpers
    [[nodiscard]] static int32_t floatToInt24(float x) noexcept;
    [[nodiscard]] static float int24ToFloat(int32_t x) noexcept;

    // Mode-specific processing
    [[nodiscard]] int32_t processXorPattern(int32_t x) const noexcept;
    [[nodiscard]] int32_t processXorPrevious(int32_t x) noexcept;
    [[nodiscard]] int32_t processBitRotate(int32_t x) const noexcept;
    [[nodiscard]] int32_t processBitShuffle(int32_t x) const noexcept;
    [[nodiscard]] int32_t processBitAverage(int32_t x) noexcept;
    [[nodiscard]] int32_t processOverflowWrap(float x) const noexcept;  // Takes float for wrap detection

    // Permutation generation for BitShuffle
    void generatePermutation() noexcept;
    [[nodiscard]] uint32_t shuffleBits(uint32_t input) const noexcept;

    // State
    BitwiseOperation operation_ = BitwiseOperation::XorPattern;
    float intensity_ = kDefaultIntensity;
    uint32_t pattern_ = kDefaultPattern;
    int rotateAmount_ = 0;
    uint32_t seed_ = kDefaultSeed;

    int32_t previousSampleInt_ = 0;  // For XorPrevious and BitAverage
    std::array<uint8_t, 24> permutation_{};  // For BitShuffle

    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};
```

## API Contracts

N/A - This is an internal DSP primitive, not an external API. The public interface is the C++ class definition above.

## Quickstart

### Basic Usage

```cpp
#include <krate/dsp/primitives/bitwise_mangler.h>

using namespace Krate::DSP;

// Create and prepare
BitwiseMangler mangler;
mangler.prepare(44100.0);

// Configure for XOR pattern distortion
mangler.setOperation(BitwiseOperation::XorPattern);
mangler.setPattern(0xAAAAAAAA);  // Alternating bits
mangler.setIntensity(0.5f);      // 50% wet

// Process audio
float output = mangler.process(input);
```

### Frequency-Dependent Distortion (XorPrevious)

```cpp
mangler.setOperation(BitwiseOperation::XorPrevious);
mangler.setIntensity(1.0f);

// High-frequency content produces more dramatic changes
// Transients get more distortion than sustained notes
for (auto& sample : buffer) {
    sample = mangler.process(sample);
}
```

### Pseudo-Pitch Effects (BitRotate)

```cpp
mangler.setOperation(BitwiseOperation::BitRotate);
mangler.setRotateAmount(4);   // Rotate left by 4 bits
mangler.setIntensity(0.8f);

// Creates unusual frequency shifts
mangler.processBlock(buffer, numSamples);
```

### Deterministic Chaos (BitShuffle)

```cpp
mangler.setOperation(BitwiseOperation::BitShuffle);
mangler.setSeed(42);          // Same seed = same output
mangler.setIntensity(1.0f);

// Dramatically transforms signal but reproducibly
mangler.reset();  // Reset state for deterministic results
mangler.processBlock(buffer, numSamples);
```

### Integer Overflow Wrap

```cpp
// Apply upstream gain to push signal hot
for (auto& sample : buffer) {
    sample *= 2.0f;  // May exceed [-1, 1]
}

mangler.setOperation(BitwiseOperation::OverflowWrap);
mangler.setIntensity(1.0f);

// Wrap instead of clip
mangler.processBlock(buffer, numSamples);

// Output may exceed [-1, 1] - apply limiter downstream
```

---

# Phase 2: Testing Strategy

## Test File Structure

```text
dsp/tests/unit/primitives/bitwise_mangler_test.cpp
```

## Test Categories

### T010: Foundational Tests
- Default construction and parameter values
- Getter/setter validation with clamping
- prepare() and reset() behavior

### T020: XorPattern Mode Tests
- SC-001: THD > 10% with pattern 0xAAAAAAAA
- Pattern 0x00000000 produces bypass
- Pattern 0xFFFFFFFF produces maximum mangling
- Different patterns produce different spectra

### T030: XorPrevious Mode Tests
- SC-002: Higher THD for 8kHz than 100Hz input
- First sample after reset XORs with 0
- Transient vs sustained signal difference

### T040: BitRotate Mode Tests
- SC-003: +8 vs -8 rotation produces different spectra
- Rotation by 0 is passthrough
- Rotation by 24 equals rotation by 0 (modulo)

### T050: BitShuffle Mode Tests
- SC-004: Same seed produces bit-exact identical output
- Different seeds produce different outputs
- Permutation is valid (no duplicate mappings)

### T060: BitAverage Mode Tests
- AND operation behavior verification
- Adjacent samples with same bits preserved
- Different samples produce fewer set bits

### T070: OverflowWrap Mode Tests
- Values in [-1, 1] pass through unchanged
- Values > 1.0 wrap to negative
- Values < -1.0 wrap to positive

### T080: Intensity Blend Tests
- SC-009: Intensity 0.0 produces bit-exact passthrough
- Intensity 0.5 produces 50/50 blend
- Intensity 1.0 produces full effect

### T090: Edge Cases and Safety
- NaN input returns 0.0
- Inf input returns 0.0
- Denormal flushing
- SC-008: Float roundtrip precision (-144dB noise floor)

### T100: Performance Tests
- SC-006: < 0.1% CPU at 44100Hz
- SC-007: Zero latency
- SC-005: Parameter changes within one sample

## Success Criteria Verification

| Criteria | Test Method |
|----------|-------------|
| SC-001 | Calculate THD with SignalMetrics::calculateTHD |
| SC-002 | Compare THD for 8kHz vs 100Hz sine |
| SC-003 | Spectral comparison via FFT |
| SC-004 | Bit-exact comparison with REQUIRE_THAT(Approx().margin(0)) |
| SC-005 | Process single sample after parameter change |
| SC-006 | Benchmark with std::chrono |
| SC-007 | Verify getLatency() returns 0 |
| SC-008 | Roundtrip test with SNR measurement |
| SC-009 | Direct floating-point comparison |
| SC-010 | DC offset measurement with mean calculation |

---

# Risk Assessment

## Low Risk
- **Float-to-int conversion**: Standard approach, well-understood
- **XOR operations**: Simple, deterministic, no edge cases
- **Intensity blending**: Trivial linear interpolation

## Medium Risk
- **BitShuffle permutation**: Need to verify Fisher-Yates produces unbiased permutation
- **Sign extension after rotation**: Must correctly handle negative numbers in 24-bit signed representation
- **OverflowWrap edge cases**: Integer wrapping behavior must match two's complement expectations

## Mitigation
- Comprehensive unit tests for all operations
- Explicit test cases for negative numbers and boundary values
- Verify permutation validity (no duplicates, covers all indices)

---

# Architecture Documentation Update

After implementation, update `specs/_architecture_/layer-1-primitives.md` with:

```markdown
## BitwiseMangler
**Path:** [bitwise_mangler.h](../../dsp/include/krate/dsp/primitives/bitwise_mangler.h) | **Since:** 0.15.0

Bit manipulation distortion with six operation modes for wild tonal shifts.

**Use when:**
- Creating unconventional digital distortion effects
- Need signal-dependent distortion (XorPrevious)
- Want deterministic chaos (BitShuffle)
- Simulating integer overflow artifacts

```cpp
enum class BitwiseOperation : uint8_t {
    XorPattern, XorPrevious, BitRotate, BitShuffle, BitAverage, OverflowWrap
};

class BitwiseMangler {
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setOperation(BitwiseOperation op) noexcept;
    void setIntensity(float intensity) noexcept;      // [0, 1]
    void setPattern(uint32_t pattern) noexcept;       // XorPattern
    void setRotateAmount(int bits) noexcept;          // [-16, +16]
    void setSeed(uint32_t seed) noexcept;             // BitShuffle
    [[nodiscard]] float process(float x) noexcept;
    void processBlock(float* buffer, size_t n) noexcept;
};
```

| Mode | Character | Use Case |
|------|-----------|----------|
| XorPattern | Metallic harmonics | Aggressive digital distortion |
| XorPrevious | Transient-responsive | Dynamic distortion |
| BitRotate | Pseudo-pitch shift | Unusual frequency effects |
| BitShuffle | Chaotic destruction | Extreme sound design |
| BitAverage | Smoothing/thinning | Subtle bit-level processing |
| OverflowWrap | Hard digital artifacts | Integer overflow simulation |
```
