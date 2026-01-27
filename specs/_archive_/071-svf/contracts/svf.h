// ==============================================================================
// API Contract: State Variable Filter (SVF)
// ==============================================================================
// This file defines the PUBLIC API contract for the SVF implementation.
// The actual implementation will be in: dsp/include/krate/dsp/primitives/svf.h
//
// Feature: 071-svf
// Layer: 1 (Primitive)
// Dependencies: Layer 0 only (math_constants.h, db_utils.h)
//
// Reference: specs/071-svf/spec.md
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// SVFMode Enumeration (FR-001)
// =============================================================================

/// @brief Filter mode selection for SVF::process() output.
///
/// Determines which linear combination of LP/HP/BP outputs is returned.
/// For simultaneous access to all outputs, use SVF::processMulti() instead.
///
/// @note Peak and shelf modes use the gainDb parameter set via setGain().
enum class SVFMode : uint8_t {
    Lowpass,   ///< 12 dB/oct lowpass, -3dB at cutoff
    Highpass,  ///< 12 dB/oct highpass, -3dB at cutoff
    Bandpass,  ///< Constant 0 dB peak gain
    Notch,     ///< Band-reject filter
    Allpass,   ///< Flat magnitude, phase shift
    Peak,      ///< Parametric EQ bell curve (uses gainDb)
    LowShelf,  ///< Boost/cut below cutoff (uses gainDb)
    HighShelf  ///< Boost/cut above cutoff (uses gainDb)
};

// =============================================================================
// SVFOutputs Structure (FR-002)
// =============================================================================

/// @brief Simultaneous outputs from SVF::processMulti().
///
/// All four outputs are computed in a single processing cycle with minimal
/// additional overhead compared to single-output processing.
///
/// @note Peak, allpass, and shelf outputs are not included in this struct.
///       Use SVF::process() with the appropriate SVFMode for those responses.
struct SVFOutputs {
    float low;   ///< Lowpass output (12 dB/oct)
    float high;  ///< Highpass output (12 dB/oct)
    float band;  ///< Bandpass output (constant 0 dB peak)
    float notch; ///< Notch (band-reject) output
};

// =============================================================================
// SVF Class (FR-003 through FR-027)
// =============================================================================

/// @brief TPT State Variable Filter with excellent modulation stability.
///
/// Implements the Cytomic TPT (Topology-Preserving Transform) SVF topology
/// using trapezoidal integration for stable audio-rate parameter modulation.
///
/// Key advantages over Biquad:
/// - **Modulation-stable**: No clicks when cutoff/Q change at audio rate
/// - **Multi-output**: Get LP/HP/BP/Notch in one computation via processMulti()
/// - **Orthogonal**: Cutoff and Q are truly independent parameters
/// - **Efficient**: ~10 multiplies + 8 adds per sample
///
/// @par Usage
/// @code
/// SVF filter;
/// filter.prepare(44100.0);
/// filter.setMode(SVFMode::Lowpass);
/// filter.setCutoff(1000.0f);
/// filter.setResonance(0.7071f);  // Butterworth Q
///
/// // Single output processing
/// for (auto& sample : buffer) {
///     sample = filter.process(sample);
/// }
///
/// // Or multi-output processing
/// SVFOutputs outputs = filter.processMulti(input);
/// float lpOut = outputs.low;
/// float hpOut = outputs.high;
/// @endcode
///
/// @par Real-Time Safety
/// All processing methods are noexcept and perform no allocations.
/// Denormals are flushed after every sample to prevent CPU spikes.
///
/// @par Thread Safety
/// Not thread-safe. Create separate instances for each audio thread.
///
/// @see SVFMode for available filter modes
/// @see SVFOutputs for multi-output processing
class SVF {
public:
    // =========================================================================
    // Lifecycle (FR-004)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Creates an unprepared filter. Call prepare() before processing.
    /// Calling process() before prepare() returns input unchanged.
    SVF() noexcept = default;

    /// @brief Prepare the filter for processing at the given sample rate.
    ///
    /// Must be called before processing. Can be called again if sample rate changes.
    /// Recalculates all coefficients for the new sample rate.
    ///
    /// @param sampleRate Sample rate in Hz (clamped to minimum 1000.0)
    void prepare(double sampleRate) noexcept;

    // =========================================================================
    // Configuration (FR-005 through FR-009)
    // =========================================================================

    /// @brief Set the filter mode for process() output.
    ///
    /// Does not affect processMulti() which always returns all four basic outputs.
    ///
    /// @param mode Filter response type
    /// @see SVFMode for available modes
    void setMode(SVFMode mode) noexcept;

    /// @brief Set the cutoff/center frequency.
    ///
    /// Coefficients are recalculated immediately (no smoothing).
    /// The frequency is clamped to [1 Hz, sampleRate * 0.495].
    ///
    /// @param hz Cutoff frequency in Hz
    void setCutoff(float hz) noexcept;

    /// @brief Set the Q factor (resonance).
    ///
    /// Coefficients are recalculated immediately (no smoothing).
    /// The Q is clamped to [0.1, 30.0].
    ///
    /// @param q Q factor (0.7071 = Butterworth, higher = more resonant)
    void setResonance(float q) noexcept;

    /// @brief Set the gain for peak and shelf modes.
    ///
    /// Ignored for Lowpass, Highpass, Bandpass, Notch, and Allpass modes.
    /// The gain is clamped to [-24 dB, +24 dB].
    ///
    /// @param dB Gain in decibels
    void setGain(float dB) noexcept;

    /// @brief Reset filter state without changing parameters.
    ///
    /// Clears the internal integrator states (ic1eq, ic2eq) to zero.
    /// Use when starting a new audio region to prevent click artifacts.
    void reset() noexcept;

    // =========================================================================
    // Getters
    // =========================================================================

    /// @brief Get the current filter mode.
    [[nodiscard]] SVFMode getMode() const noexcept;

    /// @brief Get the current cutoff frequency in Hz.
    [[nodiscard]] float getCutoff() const noexcept;

    /// @brief Get the current Q factor.
    [[nodiscard]] float getResonance() const noexcept;

    /// @brief Get the current gain in dB.
    [[nodiscard]] float getGain() const noexcept;

    /// @brief Check if the filter has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Processing (FR-010 through FR-012)
    // =========================================================================

    /// @brief Process a single sample.
    ///
    /// Returns the output for the currently selected mode (setMode).
    ///
    /// @param input Input sample
    /// @return Filtered output sample
    ///
    /// @note Returns input unchanged if prepare() not called
    /// @note Returns 0 and resets state on NaN/Inf input
    /// @note Denormals are flushed after processing
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place.
    ///
    /// Equivalent to calling process() on each sample sequentially.
    /// Produces bit-identical output to the equivalent process() calls.
    ///
    /// @param buffer Pointer to sample buffer (modified in-place)
    /// @param numSamples Number of samples to process
    ///
    /// @note No memory allocation occurs during processing
    void processBlock(float* buffer, size_t numSamples) noexcept;

    /// @brief Process a single sample and return all four basic outputs.
    ///
    /// Computes lowpass, highpass, bandpass, and notch outputs in a single
    /// processing cycle. More efficient than calling process() four times
    /// with different modes.
    ///
    /// @param input Input sample
    /// @return SVFOutputs struct containing all four outputs
    ///
    /// @note Returns all zeros if prepare() not called
    /// @note Returns all zeros and resets state on NaN/Inf input
    /// @note Peak, allpass, and shelf outputs are not included
    [[nodiscard]] SVFOutputs processMulti(float input) noexcept;

    // =========================================================================
    // Constants
    // =========================================================================

    /// @brief Butterworth Q value (maximally flat passband)
    static constexpr float kButterworthQ = 0.7071067811865476f;

    /// @brief Minimum allowed Q value
    static constexpr float kMinQ = 0.1f;

    /// @brief Maximum allowed Q value
    static constexpr float kMaxQ = 30.0f;

    /// @brief Minimum allowed cutoff frequency in Hz
    static constexpr float kMinCutoff = 1.0f;

    /// @brief Maximum cutoff as ratio of sample rate
    static constexpr float kMaxCutoffRatio = 0.495f;

    /// @brief Minimum allowed gain in dB (for shelf/peak modes)
    static constexpr float kMinGainDb = -24.0f;

    /// @brief Maximum allowed gain in dB (for shelf/peak modes)
    static constexpr float kMaxGainDb = 24.0f;

private:
    // Configuration
    double sampleRate_ = 44100.0;
    float cutoffHz_ = 1000.0f;
    float q_ = kButterworthQ;
    float gainDb_ = 0.0f;
    SVFMode mode_ = SVFMode::Lowpass;
    bool prepared_ = false;

    // Coefficients (see data-model.md for derivation)
    float g_ = 0.0f;   // tan(pi * fc / fs)
    float k_ = 1.0f;   // 1/Q
    float a1_ = 0.0f;  // 1 / (1 + g*(g+k))
    float a2_ = 0.0f;  // g * a1
    float a3_ = 0.0f;  // g * a2
    float A_ = 1.0f;   // 10^(dB/40) for shelf/peak

    // Mode mixing coefficients
    float m0_ = 0.0f;  // high coefficient
    float m1_ = 0.0f;  // band coefficient
    float m2_ = 1.0f;  // low coefficient

    // Integrator state
    float ic1eq_ = 0.0f;
    float ic2eq_ = 0.0f;

    // Internal methods
    void updateCoefficients() noexcept;
    void updateMixCoefficients() noexcept;
    float clampCutoff(float hz) const noexcept;
    float clampQ(float q) const noexcept;
    float clampGainDb(float dB) const noexcept;
};

} // namespace DSP
} // namespace Krate
