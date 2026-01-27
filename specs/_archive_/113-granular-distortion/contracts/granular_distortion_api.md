# API Contract: GranularDistortion

**Version**: 1.0.0 | **Layer**: 2 (Processors) | **Namespace**: `Krate::DSP`

## Class Declaration

```cpp
namespace Krate {
namespace DSP {

class GranularDistortion {
public:
    // =========================================================================
    // Constants (FR-005, FR-008, FR-014, FR-021, FR-026)
    // =========================================================================

    static constexpr float kMinGrainSizeMs = 5.0f;
    static constexpr float kMaxGrainSizeMs = 100.0f;
    static constexpr float kMinDensity = 1.0f;
    static constexpr float kMaxDensity = 8.0f;
    static constexpr float kMinDrive = 1.0f;
    static constexpr float kMaxDrive = 20.0f;
    static constexpr float kMinPositionJitterMs = 0.0f;
    static constexpr float kMaxPositionJitterMs = 50.0f;
    static constexpr float kSmoothingTimeMs = 10.0f;

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor
    /// @post Object in unprepared state. Must call prepare() before processing.
    GranularDistortion() noexcept;

    /// @brief Destructor
    ~GranularDistortion() = default;

    // Non-copyable (contains non-copyable GrainScheduler)
    GranularDistortion(const GranularDistortion&) = delete;
    GranularDistortion& operator=(const GranularDistortion&) = delete;
    GranularDistortion(GranularDistortion&&) noexcept = default;
    GranularDistortion& operator=(GranularDistortion&&) noexcept = default;

    /// @brief Initialize for given sample rate (FR-001, FR-003)
    ///
    /// Prepares all internal components including grain pool, scheduler,
    /// envelope table, and circular buffer. Must be called before processing.
    /// Supports sample rates from 44100Hz to 192000Hz.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum expected block size (currently unused; reserved for
    ///                     potential future optimizations such as pre-allocating scratch
    ///                     buffers or SIMD-aligned processing. Pass any reasonable value.)
    /// @note NOT real-time safe (initializes vectors/arrays)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Clear all internal state without reallocation (FR-002)
    ///
    /// Resets circular buffer, all active grains, and smoothers.
    /// Does not change parameter values or sample rate.
    void reset() noexcept;

    // =========================================================================
    // Grain Size Control (FR-004, FR-005, FR-006)
    // =========================================================================

    /// @brief Set grain window duration (FR-004)
    ///
    /// @param ms Grain size in milliseconds, clamped to [5.0, 100.0] (FR-005)
    /// @note Changes apply to newly triggered grains only (FR-006)
    void setGrainSize(float ms) noexcept;

    /// @brief Get current grain size in milliseconds
    [[nodiscard]] float getGrainSize() const noexcept;

    // =========================================================================
    // Grain Density Control (FR-007, FR-008, FR-009)
    // =========================================================================

    /// @brief Set grain density / overlap amount (FR-007)
    ///
    /// @param density Approximate simultaneous grains, clamped to [1.0, 8.0] (FR-008)
    /// @note Changes are click-free via scheduler smoothing (FR-009)
    void setGrainDensity(float density) noexcept;

    /// @brief Get current grain density
    [[nodiscard]] float getGrainDensity() const noexcept;

    // =========================================================================
    // Distortion Type Control (FR-010, FR-011, FR-012)
    // =========================================================================

    /// @brief Set base distortion algorithm (FR-010)
    ///
    /// @param type Waveshape type (Tanh, Atan, Cubic, Tube, HardClip, etc.) (FR-011)
    /// @note Changes apply to newly triggered grains (FR-012)
    void setDistortionType(WaveshapeType type) noexcept;

    /// @brief Get current base distortion type
    [[nodiscard]] WaveshapeType getDistortionType() const noexcept;

    // =========================================================================
    // Drive Control (FR-025, FR-026, FR-027)
    // =========================================================================

    /// @brief Set base drive / distortion intensity (FR-025)
    ///
    /// @param drive Drive amount, clamped to [1.0, 20.0] (FR-026)
    /// @note Changes are click-free via 10ms smoothing (FR-027)
    void setDrive(float drive) noexcept;

    /// @brief Get current base drive
    [[nodiscard]] float getDrive() const noexcept;

    // =========================================================================
    // Drive Variation Control (FR-013, FR-014, FR-015, FR-016)
    // =========================================================================

    /// @brief Set per-grain drive randomization amount (FR-013)
    ///
    /// @param amount Variation amount, clamped to [0.0, 1.0] (FR-014)
    ///               0.0 = all grains get identical drive (FR-016)
    ///               1.0 = maximum variation
    /// @note Per-grain drive = baseDrive * (1 + amount * random[-1,1]) (FR-015)
    /// @note Result is clamped to [1.0, 20.0] (FR-015)
    void setDriveVariation(float amount) noexcept;

    /// @brief Get current drive variation amount
    [[nodiscard]] float getDriveVariation() const noexcept;

    // =========================================================================
    // Algorithm Variation Control (FR-017, FR-018, FR-019)
    // =========================================================================

    /// @brief Enable/disable per-grain algorithm randomization (FR-017)
    ///
    /// @param enabled true = random algorithm per grain (FR-018)
    ///                false = all grains use base distortion type (FR-019)
    void setAlgorithmVariation(bool enabled) noexcept;

    /// @brief Get current algorithm variation state
    [[nodiscard]] bool getAlgorithmVariation() const noexcept;

    // =========================================================================
    // Position Jitter Control (FR-020, FR-021, FR-022, FR-023, FR-024-NEW)
    // =========================================================================

    /// @brief Set grain start position randomization (FR-020)
    ///
    /// @param ms Maximum jitter in milliseconds, clamped to [0.0, 50.0] (FR-021)
    ///           0 = grains start at current position (FR-023)
    /// @note Each grain's start offset = random[-jitter, +jitter] (FR-022)
    /// @note Jitter is clamped dynamically to available buffer history (FR-024-NEW)
    void setPositionJitter(float ms) noexcept;

    /// @brief Get current position jitter in milliseconds
    [[nodiscard]] float getPositionJitter() const noexcept;

    // =========================================================================
    // Mix Control (FR-028, FR-029, FR-030, FR-031)
    // =========================================================================

    /// @brief Set dry/wet mix (FR-028)
    ///
    /// @param mix Mix amount, clamped to [0.0, 1.0] (FR-029)
    ///            0.0 = bypass (dry only), 1.0 = full wet
    /// @note Changes are click-free via 10ms smoothing (FR-030)
    /// @note Formula: output = (1-mix)*dry + mix*wet (FR-031)
    void setMix(float mix) noexcept;

    /// @brief Get current mix amount
    [[nodiscard]] float getMix() const noexcept;

    // =========================================================================
    // Processing (FR-032, FR-033, FR-034, FR-035, FR-036)
    // =========================================================================

    /// @brief Process a single sample (FR-032)
    ///
    /// @param input Input sample (expected normalized [-1, 1])
    /// @return Processed output sample
    ///
    /// @note Real-time safe: noexcept, no allocations (FR-033)
    /// @note Returns 0 and resets on NaN/Inf input (FR-034)
    /// @note Flushes denormals (FR-035)
    /// @note Output is bounded, no NaN/Inf (FR-036)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process buffer in-place (FR-032)
    ///
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    void process(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if processor has been prepared
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get number of currently active grains
    [[nodiscard]] size_t getActiveGrainCount() const noexcept;

    /// @brief Get maximum grain capacity (always 64)
    [[nodiscard]] static constexpr size_t getMaxGrains() noexcept;

    /// @brief Seed RNG for reproducible behavior (testing only)
    void seed(uint32_t seedValue) noexcept;
};

} // namespace DSP
} // namespace Krate
```

## Method Specifications

### prepare()

| Aspect | Specification |
|--------|--------------|
| **Pre-conditions** | None |
| **Post-conditions** | `isPrepared() == true`, all components initialized |
| **Thread safety** | NOT thread-safe, call from initialization context |
| **RT safety** | NOT real-time safe (allocates memory) |
| **Complexity** | O(bufferSize + envelopeSize) |

### reset()

| Aspect | Specification |
|--------|--------------|
| **Pre-conditions** | prepare() has been called |
| **Post-conditions** | All state cleared, smoothers snapped, parameters unchanged |
| **Thread safety** | NOT thread-safe |
| **RT safety** | Real-time safe (no allocations) |
| **Complexity** | O(bufferSize) |

### process(float)

| Aspect | Specification |
|--------|--------------|
| **Pre-conditions** | prepare() has been called |
| **Post-conditions** | Input processed, internal state advanced |
| **Thread safety** | NOT thread-safe (single instance per thread) |
| **RT safety** | Real-time safe (noexcept, no allocations) |
| **Complexity** | O(activeGrains) per sample |
| **Latency** | 0 samples (no lookahead) |

### Parameter Setters

| Aspect | Specification |
|--------|--------------|
| **Pre-conditions** | None (can be called before prepare) |
| **Post-conditions** | Parameter stored, smoother target updated if applicable |
| **Thread safety** | NOT thread-safe |
| **RT safety** | Real-time safe |
| **Complexity** | O(1) |

## Error Handling

| Input | Behavior | Reference |
|-------|----------|-----------|
| NaN input | `reset()` called, returns 0.0f | FR-034 |
| Inf input | `reset()` called, returns 0.0f | FR-034 |
| Parameter out of range | Clamped to valid range | FR-005, FR-008, etc. |
| process() before prepare() | Returns input unchanged | Defensive |
| Buffer null | No-op | Defensive |

## Usage Example

```cpp
#include <krate/dsp/processors/granular_distortion.h>

// Setup
Krate::DSP::GranularDistortion granular;
granular.prepare(44100.0, 512);

// Configure for evolving texture
granular.setGrainSize(50.0f);        // 50ms grains
granular.setGrainDensity(4.0f);      // ~4 overlapping grains
granular.setDistortionType(Krate::DSP::WaveshapeType::Tube);
granular.setDrive(5.0f);             // Moderate drive
granular.setDriveVariation(0.5f);    // 50% drive variation
granular.setAlgorithmVariation(true); // Random algorithms
granular.setPositionJitter(10.0f);   // 10ms position jitter
granular.setMix(0.75f);              // 75% wet

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = granular.process(input[i]);
}

// Or block processing
granular.process(buffer, numSamples);
```

## Compliance Matrix

| Requirement | API Method | Notes |
|-------------|------------|-------|
| FR-001 | prepare() | Initializes for processing |
| FR-002 | reset() | Clears state |
| FR-003 | prepare() | Supports 44.1-192kHz |
| FR-004 | setGrainSize() | |
| FR-005 | setGrainSize() | Clamps [5, 100] |
| FR-006 | setGrainSize() | New grains only |
| FR-007 | setGrainDensity() | |
| FR-008 | setGrainDensity() | Clamps [1, 8] |
| FR-009 | setGrainDensity() | Click-free via scheduler |
| FR-010 | setDistortionType() | |
| FR-011 | setDistortionType() | Supports Tanh, Atan, Cubic, Tube, HardClip |
| FR-012 | setDistortionType() | New grains only |
| FR-013 | setDriveVariation() | |
| FR-014 | setDriveVariation() | Clamps [0, 1] |
| FR-015 | Internal | drive * (1 + var * rand), clamp [1, 20] |
| FR-016 | setDriveVariation() | var=0 means identical drive |
| FR-017 | setAlgorithmVariation() | |
| FR-018 | Internal | Random from available types |
| FR-019 | setAlgorithmVariation() | false = base type |
| FR-020 | setPositionJitter() | |
| FR-021 | setPositionJitter() | Clamps [0, 50] |
| FR-022 | Internal | random[-j, +j] offset |
| FR-023 | setPositionJitter() | j=0 means current position |
| FR-024-NEW | Internal | Dynamic clamping to history |
| FR-025 | setDrive() | |
| FR-026 | setDrive() | Clamps [1, 20] |
| FR-027 | setDrive() | 10ms smoothing |
| FR-028 | setMix() | |
| FR-029 | setMix() | Clamps [0, 1] |
| FR-030 | setMix() | 10ms smoothing |
| FR-031 | process() | (1-mix)*dry + mix*wet |
| FR-032 | process() | |
| FR-033 | process() | noexcept, no alloc |
| FR-034 | process() | NaN/Inf handling |
| FR-035 | process() | Denormal flushing |
| FR-036 | process() | Bounded output |
| FR-037-041 | Internal | GrainPool usage |
| FR-042-044 | Internal | Waveshaper usage |
| FR-045-046 | Internal | Circular buffer |
| FR-047 | Class design | Mono only |
| FR-048-049 | process() | Stability guarantees |
