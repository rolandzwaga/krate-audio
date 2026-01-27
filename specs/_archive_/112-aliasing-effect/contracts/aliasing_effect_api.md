# API Contract: AliasingEffect

**Feature**: 112-aliasing-effect | **Date**: 2026-01-27
**Layer**: 2 (DSP Processor) | **Namespace**: `Krate::DSP`

## Class Declaration

```cpp
namespace Krate {
namespace DSP {

/// @brief Layer 2 DSP Processor - Intentional aliasing with band isolation
///
/// Creates digital grunge/lo-fi aesthetic by downsampling without anti-aliasing,
/// causing high frequencies to fold back into the audible spectrum. Features
/// configurable band isolation and pre-downsample frequency shifting.
///
/// @par Algorithm
/// 1. Band isolation: Separate input into band and non-band components
/// 2. Frequency shift: Apply SSB modulation to shift band content
/// 3. Downsample: Sample-and-hold without anti-aliasing (creates aliasing)
/// 4. Recombine: Sum non-band signal with aliased band signal
/// 5. Mix: Blend with dry input
///
/// @par Features
/// - Configurable downsample factor [2, 32] for mild to extreme aliasing
/// - Frequency shift [-5000, +5000] Hz before downsample affects aliasing patterns
/// - Band isolation [20Hz, Nyquist] with 24dB/oct slopes
/// - Click-free parameter automation via 10ms smoothing
/// - Mono processing only (instantiate two for stereo)
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free after prepare().
/// Safe for audio callbacks.
///
/// @par Thread Safety
/// Not thread-safe. Create separate instances per audio channel.
///
/// @par Latency
/// Approximately 5 samples from internal frequency shifter (Hilbert transform).
/// Not compensated in output.
///
/// @par Usage
/// @code
/// AliasingEffect aliaser;
/// aliaser.prepare(44100.0, 512);
/// aliaser.setDownsampleFactor(8.0f);
/// aliaser.setAliasingBand(2000.0f, 8000.0f);
/// aliaser.setFrequencyShift(500.0f);
/// aliaser.setMix(0.75f);
///
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = aliaser.process(input[i]);
/// }
/// @endcode
class AliasingEffect {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinDownsampleFactor = 2.0f;
    static constexpr float kMaxDownsampleFactor = 32.0f;
    static constexpr float kDefaultDownsampleFactor = 2.0f;

    static constexpr float kMinFrequencyShiftHz = -5000.0f;
    static constexpr float kMaxFrequencyShiftHz = 5000.0f;

    static constexpr float kMinBandFrequencyHz = 20.0f;
    // Max band frequency is sampleRate * 0.45 (set dynamically)

    static constexpr float kSmoothingTimeMs = 10.0f;

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor
    ///
    /// Creates an unprepared processor. Call prepare() before processing.
    /// Processing before prepare() returns input unchanged.
    AliasingEffect() noexcept = default;

    /// @brief Destructor
    ~AliasingEffect() = default;

    // Non-copyable due to FrequencyShifter containing non-copyable components
    AliasingEffect(const AliasingEffect&) = delete;
    AliasingEffect& operator=(const AliasingEffect&) = delete;
    AliasingEffect(AliasingEffect&&) noexcept = default;
    AliasingEffect& operator=(AliasingEffect&&) noexcept = default;

    /// @brief Initialize for given sample rate (FR-001, FR-003)
    ///
    /// Prepares all internal components. Must be called before processing.
    /// Supports sample rates from 44100Hz to 192000Hz.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum block size (for future buffer pre-allocation)
    /// @note NOT real-time safe (FrequencyShifter allocates internally)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Clear all internal state without reallocation (FR-002)
    ///
    /// Resets all filters, shifter, reducer, and smoothers.
    /// Does not change parameter values or sample rate.
    void reset() noexcept;

    // =========================================================================
    // Downsample Control (FR-004, FR-005, FR-006, FR-007)
    // =========================================================================

    /// @brief Set the downsample factor (FR-004, FR-005)
    ///
    /// Higher factors create more severe aliasing. No anti-aliasing filter
    /// is applied (FR-007), so all frequencies above reduced Nyquist fold back.
    ///
    /// @param factor Reduction factor, clamped to [2.0, 32.0]
    ///               2 = mild aliasing, 32 = extreme aliasing
    /// @note Change is smoothed over 10ms (FR-006)
    void setDownsampleFactor(float factor) noexcept;

    /// @brief Get current downsample factor
    [[nodiscard]] float getDownsampleFactor() const noexcept;

    // =========================================================================
    // Frequency Shift Control (FR-008, FR-009, FR-010, FR-011, FR-012, FR-012a)
    // =========================================================================

    /// @brief Set pre-downsample frequency shift (FR-008, FR-009)
    ///
    /// Shifts all frequencies by a constant Hz amount before downsampling.
    /// This affects which frequencies alias and where they fold to.
    /// Uses SSB modulation (FR-012) with fixed internal configuration (FR-012a).
    ///
    /// @param hz Shift amount in Hz, clamped to [-5000, +5000]
    ///           Positive = frequencies shift up, Negative = frequencies shift down
    /// @note Change is smoothed over 10ms (FR-010)
    /// @note Applied before downsampling (FR-011)
    void setFrequencyShift(float hz) noexcept;

    /// @brief Get current frequency shift in Hz
    [[nodiscard]] float getFrequencyShift() const noexcept;

    // =========================================================================
    // Aliasing Band Control (FR-013, FR-014, FR-015, FR-016, FR-017, FR-018)
    // =========================================================================

    /// @brief Set the frequency band to apply aliasing to (FR-013)
    ///
    /// Only content within this band is processed through the aliaser.
    /// Content outside the band bypasses the aliaser and recombines after (FR-018).
    /// Band filter uses 24dB/oct slopes (FR-017).
    ///
    /// @param lowHz Low band frequency, clamped to [20, sampleRate*0.45] Hz (FR-014)
    /// @param highHz High band frequency, clamped to [20, sampleRate*0.45] Hz (FR-014)
    /// @note lowHz is constrained to be <= highHz (FR-015)
    /// @note Changes are smoothed over 10ms (FR-016)
    void setAliasingBand(float lowHz, float highHz) noexcept;

    /// @brief Get current aliasing band low frequency in Hz
    [[nodiscard]] float getAliasingBandLow() const noexcept;

    /// @brief Get current aliasing band high frequency in Hz
    [[nodiscard]] float getAliasingBandHigh() const noexcept;

    // =========================================================================
    // Mix Control (FR-019, FR-020, FR-021, FR-022)
    // =========================================================================

    /// @brief Set dry/wet mix (FR-019, FR-020)
    ///
    /// @param mix Mix amount, clamped to [0.0, 1.0]
    ///            0.0 = bypass (dry only), 1.0 = full wet
    /// @note Change is smoothed over 10ms (FR-021)
    /// @note Formula: output = (1-mix)*dry + mix*wet (FR-022)
    void setMix(float mix) noexcept;

    /// @brief Get current dry/wet mix
    [[nodiscard]] float getMix() const noexcept;

    // =========================================================================
    // Processing (FR-023, FR-024, FR-025, FR-026, FR-027, FR-028, FR-029, FR-030)
    // =========================================================================

    /// @brief Process a single sample (FR-023)
    ///
    /// Processing chain (FR-028):
    /// input -> band isolation -> frequency shift (FR-029) ->
    /// downsample (no AA) -> recombine with non-band (FR-030) -> mix with dry
    ///
    /// @param input Input sample
    /// @return Processed output sample
    ///
    /// @note Returns input unchanged if prepare() not called
    /// @note Returns 0 and resets on NaN/Inf input (FR-025)
    /// @note noexcept, allocation-free (FR-024)
    /// @note Output is bounded, no NaN/Inf output (FR-027)
    /// @note Flushes denormals (FR-026)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a buffer in-place (FR-023)
    ///
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    ///
    /// @note noexcept, allocation-free (FR-024)
    void process(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Query (FR-034)
    // =========================================================================

    /// @brief Check if processor has been prepared
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get processing latency in samples (FR-034)
    ///
    /// @return Approximately 5 samples (from internal frequency shifter)
    [[nodiscard]] static constexpr size_t getLatencySamples() noexcept {
        return 5;  // From FrequencyShifter's Hilbert transform
    }

private:
    // Internal components - see data-model.md for details
    // ...
};

} // namespace DSP
} // namespace Krate
```

## Method Signatures Summary

| Method | Parameters | Return | Thread-Safe | RT-Safe |
|--------|------------|--------|-------------|---------|
| `prepare` | `double sampleRate, size_t maxBlockSize` | `void` | No | No |
| `reset` | none | `void` | No | Yes |
| `setDownsampleFactor` | `float factor` | `void` | No | Yes |
| `getDownsampleFactor` | none | `float` | No | Yes |
| `setFrequencyShift` | `float hz` | `void` | No | Yes |
| `getFrequencyShift` | none | `float` | No | Yes |
| `setAliasingBand` | `float lowHz, float highHz` | `void` | No | Yes |
| `getAliasingBandLow` | none | `float` | No | Yes |
| `getAliasingBandHigh` | none | `float` | No | Yes |
| `setMix` | `float mix` | `void` | No | Yes |
| `getMix` | none | `float` | No | Yes |
| `process` (single) | `float input` | `float` | No | Yes |
| `process` (block) | `float* buffer, size_t n` | `void` | No | Yes |
| `isPrepared` | none | `bool` | No | Yes |
| `getLatencySamples` | none | `size_t` | Yes | Yes |

## Parameter Ranges

| Parameter | Min | Max | Default | Unit |
|-----------|-----|-----|---------|------|
| Downsample Factor | 2.0 | 32.0 | 2.0 | ratio |
| Frequency Shift | -5000.0 | +5000.0 | 0.0 | Hz |
| Band Low | 20.0 | sampleRate*0.45 | 20.0 | Hz |
| Band High | 20.0 | sampleRate*0.45 | 20000.0* | Hz |
| Mix | 0.0 | 1.0 | 1.0 | normalized |

*Band High default is clamped to sampleRate*0.45 during prepare()

## Error Handling

| Condition | Behavior |
|-----------|----------|
| Process before prepare() | Returns input unchanged |
| NaN input | Resets state, returns 0.0f |
| Inf input | Resets state, returns 0.0f |
| Out-of-range parameter | Clamped to valid range |
| Band low > band high | Low clamped to high value |

## Include Requirements

```cpp
#include <krate/dsp/processors/aliasing_effect.h>

// Dependencies (included by aliasing_effect.h):
// - <krate/dsp/primitives/biquad.h>
// - <krate/dsp/primitives/sample_rate_reducer.h>
// - <krate/dsp/primitives/smoother.h>
// - <krate/dsp/processors/frequency_shifter.h>
// - <krate/dsp/core/db_utils.h>
```
