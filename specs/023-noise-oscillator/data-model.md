# Data Model: Noise Oscillator Primitive

**Feature**: 023-noise-oscillator
**Date**: 2026-02-05
**Updated**: 2026-02-05 (Added Grey noise, PinkNoiseFilter extraction)

## Entities

### NoiseColor Enumeration (FR-001)

**NOTE**: Reuse existing enum from `<krate/dsp/core/pattern_freeze_types.h>`.

Represents the six noise spectral colors with their characteristic slopes.

```cpp
// From dsp/include/krate/dsp/core/pattern_freeze_types.h
// DO NOT REDEFINE - use existing enum to avoid ODR violation

namespace Krate::DSP {

/// @brief Noise color types by spectral slope.
///
/// Each color represents a distinct power spectral density characteristic:
/// - White: Flat spectrum (0 dB/octave)
/// - Pink: -3 dB/octave (equal energy per octave)
/// - Brown: -6 dB/octave (Brownian motion, random walk)
/// - Blue: +3 dB/octave (differentiated pink)
/// - Violet: +6 dB/octave (differentiated white)
/// - Grey: Inverse A-weighting (perceptually flat loudness)
enum class NoiseColor : uint8_t {
    White = 0,   ///< Flat spectrum, uniform random samples
    Pink = 1,    ///< -3 dB/octave via Paul Kellet filter
    Brown = 2,   ///< -6 dB/octave via leaky integrator
    Blue = 3,    ///< +3 dB/octave via differentiated pink
    Violet = 4,  ///< +6 dB/octave via differentiated white
    Grey = 5,    ///< Inverse A-weighting via dual biquad shelf cascade
    Velvet = 6,  ///< (Layer 2 only - not used by NoiseOscillator)
    RadioStatic = 7  ///< (Layer 2 only - not used by NoiseOscillator)
};

} // namespace Krate::DSP
```

**Validation Rules**:
- Values 0-5 are valid for NoiseOscillator (White through Grey)
- Values 6-7 (Velvet, RadioStatic) are Layer 2 effect-specific
- Used as switch discriminant in process methods

---

### PinkNoiseFilter Class (RF-001, RF-002) - EXTRACTED PRIMITIVE

**Location**: `dsp/include/krate/dsp/primitives/pink_noise_filter.h`

Extracted from NoiseGenerator to enable sharing between NoiseGenerator (Layer 2) and NoiseOscillator (Layer 1).

```cpp
namespace Krate::DSP {

/// @brief Paul Kellet's pink noise filter.
///
/// Converts white noise to pink noise (-3dB/octave spectral rolloff).
/// Uses a 7-state recursive filter for excellent accuracy with minimal CPU.
///
/// Accuracy: +/- 0.05dB from 9.2Hz to Nyquist at 44.1kHz.
///
/// @par Reference
/// https://www.firstpr.com.au/dsp/pink-noise/
///
/// @par Layer
/// Layer 1 (primitives/) - depends only on Layer 0
///
/// @par Real-Time Safety
/// process() is fully real-time safe (noexcept, no allocation)
class PinkNoiseFilter {
public:
    /// @brief Process one white noise sample through the filter.
    /// @param white Input white noise sample (typically [-1, 1])
    /// @return Pink noise sample (bounded to [-1, 1])
    [[nodiscard]] float process(float white) noexcept {
        // Paul Kellet's filter coefficients (RF-002: exact coefficients preserved)
        b0_ = 0.99886f * b0_ + white * 0.0555179f;
        b1_ = 0.99332f * b1_ + white * 0.0750759f;
        b2_ = 0.96900f * b2_ + white * 0.1538520f;
        b3_ = 0.86650f * b3_ + white * 0.3104856f;
        b4_ = 0.55000f * b4_ + white * 0.5329522f;
        b5_ = -0.7616f * b5_ - white * 0.0168980f;

        float pink = b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + white * 0.5362f;
        b6_ = white * 0.115926f;

        // Normalize output to stay within [-1, 1] range
        // (RF-002: exact normalization factor 0.2f preserved)
        float normalized = pink * 0.2f;
        return (normalized > 1.0f) ? 1.0f : ((normalized < -1.0f) ? -1.0f : normalized);
    }

    /// @brief Reset filter state to zero.
    void reset() noexcept {
        b0_ = b1_ = b2_ = b3_ = b4_ = b5_ = b6_ = 0.0f;
    }

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

---

### GreyNoiseState Internal Structure (FR-019)

Internal filter state for grey noise inverse A-weighting approximation.

```cpp
// Internal to NoiseOscillator - not a public class

/// @brief Filter state for grey noise (inverse A-weighting).
///
/// Uses dual biquad shelf cascade to approximate inverse A-weighting:
/// - Low-shelf: +15dB boost below 200Hz (compensates for A-weighting rolloff)
/// - High-shelf: +4dB boost above 6kHz (compensates for HF rolloff)
struct GreyNoiseState {
    Biquad lowShelf;   ///< +15dB below 200Hz
    Biquad highShelf;  ///< +4dB above 6kHz

    /// @brief Configure filters for given sample rate.
    void configure(float sampleRate) noexcept {
        lowShelf.configure(FilterType::LowShelf, 200.0f, 0.707f, 15.0f, sampleRate);
        highShelf.configure(FilterType::HighShelf, 6000.0f, 0.707f, 4.0f, sampleRate);
    }

    /// @brief Reset filter state.
    void reset() noexcept {
        lowShelf.reset();
        highShelf.reset();
    }

    /// @brief Process white noise through inverse A-weighting filters.
    [[nodiscard]] float process(float input) noexcept {
        return highShelf.process(lowShelf.process(input));
    }
};
```

---

### NoiseOscillator Class (FR-002 through FR-019)

Lightweight Layer 1 primitive for noise generation.

```cpp
namespace Krate::DSP {

/// @brief Lightweight noise oscillator providing six noise colors.
///
/// Layer 1 primitive for oscillator-level composition. Distinct from
/// Layer 2 NoiseGenerator which provides effects-oriented noise types.
///
/// @par Thread Safety
/// Single-threaded model. All methods called from audio thread.
///
/// @par Real-Time Safety
/// process() and processBlock() are fully real-time safe:
/// - No memory allocation
/// - No locks or blocking
/// - No exceptions (noexcept)
/// - No I/O
///
/// @par Dependencies
/// Layer 0: random.h (Xorshift32), pattern_freeze_types.h (NoiseColor)
/// Layer 1: pink_noise_filter.h (PinkNoiseFilter), biquad.h (Biquad)
///
/// @par Usage
/// @code
/// NoiseOscillator osc;
/// osc.prepare(44100.0);
/// osc.setSeed(12345);
/// osc.setColor(NoiseColor::Pink);
///
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = osc.process();
/// }
/// @endcode
class NoiseOscillator {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor. Must call prepare() before processing.
    NoiseOscillator() noexcept = default;

    /// @brief Destructor.
    ~NoiseOscillator() = default;

    // Copyable and movable (value semantics, no heap allocation)
    NoiseOscillator(const NoiseOscillator&) noexcept = default;
    NoiseOscillator& operator=(const NoiseOscillator&) noexcept = default;
    NoiseOscillator(NoiseOscillator&&) noexcept = default;
    NoiseOscillator& operator=(NoiseOscillator&&) noexcept = default;

    // =========================================================================
    // Configuration (FR-003, FR-004, FR-005, FR-006)
    // =========================================================================

    /// @brief Initialize oscillator for given sample rate.
    /// @param sampleRate Sample rate in Hz (44100-192000)
    /// @post Oscillator is ready for processing
    /// @note NOT real-time safe (may allocate if needed)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset internal state, restart sequence from seed.
    /// @post Filter state cleared, PRNG reset to initial seed state
    /// @note Real-time safe
    void reset() noexcept;

    /// @brief Set noise color/algorithm.
    /// @param color Noise color to generate
    /// @post Filter state reset to zero, PRNG state preserved
    /// @note Real-time safe
    void setColor(NoiseColor color) noexcept;

    /// @brief Set PRNG seed for deterministic sequences.
    /// @param seed Seed value (0 uses default seed)
    /// @post PRNG reseeded, filter state preserved
    /// @note Real-time safe
    void setSeed(uint32_t seed) noexcept;

    // =========================================================================
    // Processing (FR-007, FR-008, FR-015)
    // =========================================================================

    /// @brief Generate single noise sample.
    /// @return Noise sample in range [-1.0, 1.0]
    /// @pre prepare() has been called
    /// @note Real-time safe, zero allocation
    [[nodiscard]] float process() noexcept;

    /// @brief Generate block of noise samples.
    /// @param output Output buffer to fill
    /// @param numSamples Number of samples to generate
    /// @pre prepare() has been called
    /// @pre output has capacity for numSamples
    /// @note Real-time safe, zero allocation
    void processBlock(float* output, size_t numSamples) noexcept;

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Get current noise color.
    /// @return Current color setting
    [[nodiscard]] NoiseColor color() const noexcept;

    /// @brief Get current PRNG seed.
    /// @return Current seed value
    [[nodiscard]] uint32_t seed() const noexcept;

    /// @brief Get sample rate.
    /// @return Sample rate in Hz
    [[nodiscard]] double sampleRate() const noexcept;

private:
    // =========================================================================
    // Internal State
    // =========================================================================

    // Sample rate
    double sampleRate_ = 44100.0;

    // Configuration
    NoiseColor color_ = NoiseColor::White;
    uint32_t seed_ = 1;

    // PRNG (Layer 0)
    Xorshift32 rng_{1};

    // Pink noise filter (extracted Layer 1 primitive)
    PinkNoiseFilter pinkFilter_;

    // Brown noise integrator state
    float brown_ = 0.0f;

    // Differentiator states (for blue/violet)
    float prevPink_ = 0.0f;   // Previous pink sample for blue
    float prevWhite_ = 0.0f;  // Previous white sample for violet

    // Grey noise filter state (inverse A-weighting)
    GreyNoiseState grey_;

    // =========================================================================
    // Internal Processing
    // =========================================================================

    /// @brief Generate white noise sample from PRNG.
    [[nodiscard]] float processWhite() noexcept;

    /// @brief Generate pink noise via Paul Kellet filter.
    [[nodiscard]] float processPink(float white) noexcept;

    /// @brief Generate brown noise via leaky integrator.
    [[nodiscard]] float processBrown(float white) noexcept;

    /// @brief Generate blue noise via differentiated pink.
    [[nodiscard]] float processBlue(float pink) noexcept;

    /// @brief Generate violet noise via differentiated white.
    [[nodiscard]] float processViolet(float white) noexcept;

    /// @brief Generate grey noise via inverse A-weighting.
    [[nodiscard]] float processGrey(float white) noexcept;

    /// @brief Reset filter state (called on color change).
    void resetFilterState() noexcept;
};

} // namespace Krate::DSP
```

---

## State Transitions

### Color Change Behavior (Edge Case from spec)

When `setColor()` is called:

```
Current State          Action                New State
---------------------------------------------------------
Any color       ->   setColor(X)     ->     color_ = X
                                            pinkFilter_.reset()
                                            brown_ = 0.0f
                                            prevPink_ = 0.0f
                                            prevWhite_ = 0.0f
                                            grey_.reset()
                                            (PRNG state preserved)
```

**Rationale**: Reset filter state ensures correct spectral characteristics immediately. Preserve PRNG state to minimize audible discontinuity in the underlying random sequence.

### Reset Behavior

When `reset()` is called:

```
Current State          Action                New State
---------------------------------------------------------
Any state       ->   reset()         ->     rng_.seed(seed_)
                                            pinkFilter_.reset()
                                            brown_ = 0.0f
                                            prevPink_ = 0.0f
                                            prevWhite_ = 0.0f
                                            grey_.reset()
```

**Result**: Sequence restarts from beginning (deterministic reproduction).

---

## Memory Layout

```
NoiseOscillator (estimated 200 bytes on 64-bit):
├── sampleRate_    : double (8 bytes)
├── color_         : uint8_t (1 byte) + padding (3 bytes)
├── seed_          : uint32_t (4 bytes)
├── rng_           : Xorshift32 (4 bytes - uint32_t state)
├── pinkFilter_    : PinkNoiseFilter (28 bytes - 7 floats)
├── brown_         : float (4 bytes)
├── prevPink_      : float (4 bytes)
├── prevWhite_     : float (4 bytes)
├── grey_.lowShelf : Biquad (~28 bytes - coeffs + state)
├── grey_.highShelf: Biquad (~28 bytes - coeffs + state)
└── padding        : ~8 bytes (alignment)
Total: ~200 bytes
```

**Real-time characteristics**:
- No heap allocation
- All state is fixed-size
- Cache-friendly sequential access in processBlock()

---

## Validation Rules

| Field | Valid Range | Default | Behavior on Invalid |
|-------|-------------|---------|---------------------|
| sampleRate | 44100-192000 | 44100.0 | Clamp to range |
| color | NoiseColor enum (0-5) | White | No validation (enum type safety) |
| seed | 0 uses default | 1 | seed(0) auto-converts to default |
| output samples | [-1.0, 1.0] | - | Hard clamped |

---

## Relationships

```
NoiseOscillator (Layer 1)
    │
    ├── depends on ──> Xorshift32 (Layer 0: random.h)
    │                  NoiseColor (Layer 0: pattern_freeze_types.h)
    │
    ├── depends on ──> PinkNoiseFilter (Layer 1: pink_noise_filter.h)
    │                  Biquad (Layer 1: biquad.h)
    │
    └── SHARES primitive with ──> NoiseGenerator (Layer 2)
                                  (both use PinkNoiseFilter)

Dependency Diagram:
                    ┌─────────────────────┐
                    │   NoiseOscillator   │
                    │     (Layer 1)       │
                    └─────────┬───────────┘
                              │
         ┌────────────────────┼────────────────────┐
         ▼                    ▼                    ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│ PinkNoiseFilter │  │     Biquad      │  │    Xorshift32   │
│   (Layer 1)     │  │   (Layer 1)     │  │    (Layer 0)    │
│   [SHARED]      │  │                 │  │                 │
└────────┬────────┘  └─────────────────┘  └─────────────────┘
         │
         ▼
┌─────────────────┐
│  NoiseGenerator │
│    (Layer 2)    │
│   [also uses]   │
└─────────────────┘
```

---

## Algorithm Summary

| Color | Algorithm | Formula |
|-------|-----------|---------|
| White | Direct PRNG | `out = rng.nextFloat()` |
| Pink | Paul Kellet 7-stage | `out = clamp(pinkFilter.process(white), -1, 1)` |
| Brown | Leaky integrator | `out = clamp((0.99*prev + 0.01*white) * 5, -1, 1)` |
| Blue | Diff pink | `out = clamp((pink - prevPink) * 0.7, -1, 1)` |
| Violet | Diff white | `out = clamp((white - prevWhite) * 0.5, -1, 1)` |
| Grey | Inverse A-weight | `out = clamp(highShelf(lowShelf(white)), -1, 1)` |

### Grey Noise Filter Details (FR-019)

Grey noise uses a dual biquad shelf cascade approximating inverse A-weighting:

```
white ──> [Low-Shelf +15dB @ 200Hz] ──> [High-Shelf +4dB @ 6kHz] ──> grey
```

This compensates for human hearing sensitivity:
- Low frequencies (sub-200Hz): A-weighting attenuates, grey boosts
- Mid frequencies (500Hz-4kHz): Approximately flat
- High frequencies (above 6kHz): A-weighting attenuates, grey boosts

Result: Perceptually flat loudness across the audible spectrum.
