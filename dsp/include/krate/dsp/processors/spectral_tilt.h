// ==============================================================================
// Layer 2: DSP Processor - Spectral Tilt Filter
// ==============================================================================
// Applies a linear dB/octave gain slope across the frequency spectrum using
// an efficient dual-shelf IIR implementation (low-shelf + high-shelf cascade).
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, constexpr, value semantics, C++20)
// - Principle IX: Layer 2 (depends on Layer 0-1 only)
// - Principle X: DSP Constraints (zero latency, denormal prevention)
// - Principle XII: Test-First Development
//
// Reference: specs/082-spectral-tilt/spec.md
//
// Algorithm:
// Uses a dual-shelf cascade (low-shelf + high-shelf) meeting at the pivot
// frequency. This approach provides:
// - Exact 0 dB gain at the pivot frequency (FR-006)
// - Better slope linearity near the pivot
// - Proper tilt behavior above and below pivot
//
// For positive tilt (boost highs, cut lows):
// - Low-shelf: cuts frequencies below pivot
// - High-shelf: boosts frequencies above pivot
// - At pivot: both shelves are at their half-gain point, summing to 0 dB
//
// The shelf gains are clamped to prevent extreme boost/cut that would cause
// numerical instability or excessive gain at frequency extremes.
//
// Denormal Prevention:
// Uses the Biquad's built-in flushDenormal() method which flushes small values
// to zero in the filter state variables.
//
// Research References:
// - CCRMA Stanford: Spectral Tilt Filters (J.O. Smith)
// - Audio EQ Cookbook (R. Bristow-Johnson)
// - GroupDIY/Gearspace: Tilt EQ design discussions
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <cmath>

namespace Krate {
namespace DSP {

/// @brief Spectral Tilt Filter - Layer 2 Processor
///
/// Applies a linear dB/octave gain slope across the frequency spectrum
/// using an efficient dual-shelf IIR cascade (low-shelf + high-shelf).
///
/// @par Features
/// - Configurable tilt amount (-12 to +12 dB/octave)
/// - Configurable pivot frequency (20 Hz to 20 kHz)
/// - Parameter smoothing for click-free automation
/// - Zero latency (pure IIR implementation)
/// - Gain limiting for stability (+24 dB max, -48 dB min)
///
/// @par Real-Time Safety
/// All processing methods (process, processBlock) are noexcept and
/// allocation-free. Safe for audio thread use.
///
/// @par Thread Safety
/// Not thread-safe. Create separate instances for each audio thread.
///
/// @par Usage Example
/// @code
/// SpectralTilt tilt;
/// tilt.prepare(44100.0);
/// tilt.setTilt(6.0f);              // +6 dB/octave brightness
/// tilt.setPivotFrequency(1000.0f); // Pivot at 1 kHz
///
/// // In audio callback
/// for (int i = 0; i < numSamples; ++i) {
///     output[i] = tilt.process(input[i]);
/// }
/// @endcode
///
/// @see EnvelopeFilter, TiltEQ, SpectralMorphFilter (for FFT-based tilt)
class SpectralTilt {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// @name Parameter Ranges
    /// @{

    /// Minimum tilt amount in dB/octave (FR-002)
    static constexpr float kMinTilt = -12.0f;

    /// Maximum tilt amount in dB/octave (FR-002)
    static constexpr float kMaxTilt = +12.0f;

    /// Minimum pivot frequency in Hz (FR-003, Edge Case)
    static constexpr float kMinPivot = 20.0f;

    /// Maximum pivot frequency in Hz (FR-003, Edge Case)
    static constexpr float kMaxPivot = 20000.0f;

    /// Minimum smoothing time in milliseconds (FR-014)
    static constexpr float kMinSmoothing = 1.0f;

    /// Maximum smoothing time in milliseconds (FR-014)
    static constexpr float kMaxSmoothing = 500.0f;

    /// @}

    /// @name Default Values
    /// @{

    /// Default smoothing time in milliseconds (FR-014, Assumptions)
    static constexpr float kDefaultSmoothing = 50.0f;

    /// Default pivot frequency in Hz (Assumptions)
    static constexpr float kDefaultPivot = 1000.0f;

    /// Default tilt amount in dB/octave (Assumptions)
    static constexpr float kDefaultTilt = 0.0f;

    /// @}

    /// @name Gain Limits
    /// @{

    /// Maximum gain at any frequency in dB (FR-024)
    static constexpr float kMaxGainDb = +24.0f;

    /// Minimum gain at any frequency in dB (FR-025)
    static constexpr float kMinGainDb = -48.0f;

    /// @}

    /// @name Internal Constants
    /// @{

    /// Q factor for Butterworth response (maximally flat)
    static constexpr float kButterworthQ = 0.7071067811865476f;

    /// @}

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor
    ///
    /// Creates an unprepared SpectralTilt with default parameters.
    /// Call prepare() before processing.
    SpectralTilt() noexcept = default;

    /// @brief Destructor
    ~SpectralTilt() noexcept = default;

    // Non-copyable, movable
    SpectralTilt(const SpectralTilt&) = delete;
    SpectralTilt& operator=(const SpectralTilt&) = delete;
    SpectralTilt(SpectralTilt&&) noexcept = default;
    SpectralTilt& operator=(SpectralTilt&&) noexcept = default;

    /// @brief Prepare for processing at given sample rate
    /// @param sampleRate Sample rate in Hz (typically 44100-192000)
    /// @pre None
    /// @post isPrepared() returns true
    /// @note NOT real-time safe (may allocate for smoothers)
    /// @note FR-015
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate;
        prepared_ = true;

        // Configure smoothers
        tiltSmoother_.configure(smoothingMs_, static_cast<float>(sampleRate));
        pivotSmoother_.configure(smoothingMs_, static_cast<float>(sampleRate));

        // Snap smoothers to current values to avoid initial ramp
        tiltSmoother_.snapTo(tilt_);
        pivotSmoother_.snapTo(pivotFrequency_);

        // Initialize filter coefficients
        updateCoefficients(tilt_, pivotFrequency_);

        // Reset filter state
        lowShelf_.reset();
        highShelf_.reset();
    }

    /// @brief Reset internal state without changing parameters
    /// @pre prepare() has been called
    /// @post Filter state cleared; parameters unchanged
    /// @note Real-time safe
    /// @note FR-016
    void reset() noexcept {
        lowShelf_.reset();
        highShelf_.reset();
    }

    // =========================================================================
    // Parameters
    // =========================================================================

    /// @brief Set tilt amount
    /// @param dBPerOctave Tilt in dB/octave, clamped to [-12, +12]
    /// @note Positive values boost frequencies above pivot
    /// @note Negative values cut frequencies above pivot
    /// @note Changes are smoothed to prevent clicks (FR-012)
    /// @note FR-002
    void setTilt(float dBPerOctave) {
        tilt_ = std::clamp(dBPerOctave, kMinTilt, kMaxTilt);
        tiltSmoother_.setTarget(tilt_);
    }

    /// @brief Set pivot frequency
    /// @param hz Pivot frequency in Hz, clamped to [20, 20000]
    /// @note Gain at pivot is always 0 dB regardless of tilt (FR-006)
    /// @note Changes are smoothed to prevent clicks (FR-013)
    /// @note FR-003
    void setPivotFrequency(float hz) {
        pivotFrequency_ = std::clamp(hz, kMinPivot, kMaxPivot);
        pivotSmoother_.setTarget(pivotFrequency_);
    }

    /// @brief Set parameter smoothing time
    /// @param ms Smoothing time in milliseconds, clamped to [1, 500]
    /// @note Affects both tilt and pivot smoothing
    /// @note FR-014
    void setSmoothing(float ms) {
        smoothingMs_ = std::clamp(ms, kMinSmoothing, kMaxSmoothing);
        if (prepared_) {
            tiltSmoother_.configure(smoothingMs_, static_cast<float>(sampleRate_));
            pivotSmoother_.configure(smoothingMs_, static_cast<float>(sampleRate_));
        }
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample
    /// @param input Input sample
    /// @return Processed output sample
    /// @pre None (returns input if not prepared - FR-019)
    /// @note Real-time safe: noexcept, no allocations (FR-021)
    /// @note Zero latency (FR-010)
    /// @note FR-017
    [[nodiscard]] float process(float input) noexcept {
        // Passthrough when not prepared (FR-019)
        if (!prepared_) {
            return input;
        }

        // Update smoothed parameters
        const float smoothedTilt = tiltSmoother_.process();
        const float smoothedPivot = pivotSmoother_.process();

        // Update coefficients if parameters have changed
        if (!tiltSmoother_.isComplete() || !pivotSmoother_.isComplete()) {
            updateCoefficients(smoothedTilt, smoothedPivot);
        }

        // Process through dual-shelf cascade (Biquad handles denormals and NaN)
        // Low-shelf first, then high-shelf
        return highShelf_.process(lowShelf_.process(input));
    }

    /// @brief Process a block of samples in-place
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @pre None (passthrough if not prepared)
    /// @note Real-time safe: noexcept, no allocations (FR-021)
    /// @note FR-018
    void processBlock(float* buffer, int numSamples) noexcept {
        if (numSamples <= 0) {
            return;
        }

        // Passthrough when not prepared
        if (!prepared_) {
            return;
        }

        // Process each sample
        for (int i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Get current tilt setting
    /// @return Tilt in dB/octave
    [[nodiscard]] float getTilt() const noexcept {
        return tilt_;
    }

    /// @brief Get current pivot frequency
    /// @return Pivot frequency in Hz
    [[nodiscard]] float getPivotFrequency() const noexcept {
        return pivotFrequency_;
    }

    /// @brief Get current smoothing time
    /// @return Smoothing time in milliseconds
    [[nodiscard]] float getSmoothing() const noexcept {
        return smoothingMs_;
    }

    /// @brief Check if processor is prepared
    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Update filter coefficients based on current tilt and pivot
    /// @param tilt Current (smoothed) tilt value in dB/octave
    /// @param pivot Current (smoothed) pivot frequency in Hz
    ///
    /// Dual-Shelf Algorithm:
    /// For a spectral tilt of X dB/octave, we configure two shelves with SYMMETRIC
    /// gains meeting at the pivot frequency:
    /// - Low-shelf at pivot: gain = -G (cuts below pivot for positive tilt)
    /// - High-shelf at pivot: gain = +G (boosts above pivot for positive tilt)
    ///
    /// At the pivot frequency, both shelves are at their half-gain transition point:
    /// - Low-shelf contributes -G/2
    /// - High-shelf contributes +G/2
    /// - Sum = 0 dB at pivot (FR-006)
    ///
    /// The gain G is calculated to give the desired tilt slope. Since a single
    /// first-order shelf has ~6 dB/octave slope, and the tilt has two slopes
    /// working together, G is scaled by a reference octave span.
    ///
    /// Using a 4-octave reference span: G = tilt * 4 = 24 dB for ±6 dB/octave tilt.
    /// This gives approximately correct slope near the pivot.
    void updateCoefficients(float tilt, float pivot) noexcept {
        // Clamp pivot frequency to valid range for the current sample rate
        const float maxFreq = static_cast<float>(sampleRate_) * 0.495f;
        const float clampedPivot = std::clamp(pivot, kMinPivot, std::min(kMaxPivot, maxFreq));

        // Calculate symmetric shelf gain for the target tilt slope
        // With Q=0.7071, at 1 octave from pivot each shelf provides ~75% of its gain.
        // For 6 dB/octave tilt, we want ~6 dB at 1 octave, so:
        // 0.75 * G = tilt → G = tilt / 0.75 ≈ tilt * 1.33
        // Using 1.5 as reference gives slightly more headroom.
        constexpr float kReferenceMultiplier = 1.5f;
        float shelfGainDb = tilt * kReferenceMultiplier;

        // Clamp gain to safe limits (FR-023, FR-024, FR-025)
        shelfGainDb = std::clamp(shelfGainDb, kMinGainDb, kMaxGainDb);

        // Calculate low-shelf coefficients (cuts below pivot for positive tilt)
        // Symmetric negative gain ensures half-gains cancel at pivot
        auto lowCoeffs = BiquadCoefficients::calculate(
            FilterType::LowShelf,
            clampedPivot,
            kButterworthQ,
            -shelfGainDb,  // Negative for low-shelf
            static_cast<float>(sampleRate_)
        );
        lowShelf_.setCoefficients(lowCoeffs);

        // Calculate high-shelf coefficients (boosts above pivot for positive tilt)
        // Symmetric positive gain ensures half-gains cancel at pivot
        auto highCoeffs = BiquadCoefficients::calculate(
            FilterType::HighShelf,
            clampedPivot,
            kButterworthQ,
            shelfGainDb,   // Positive for high-shelf
            static_cast<float>(sampleRate_)
        );
        highShelf_.setCoefficients(highCoeffs);
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Processing components - dual-shelf cascade
    Biquad lowShelf_;   ///< Low-shelf filter (cuts below pivot for positive tilt)
    Biquad highShelf_;  ///< High-shelf filter (boosts above pivot for positive tilt)

    // Parameter smoothers
    OnePoleSmoother tiltSmoother_;
    OnePoleSmoother pivotSmoother_;

    // Configuration
    double sampleRate_ = 44100.0;
    float tilt_ = kDefaultTilt;
    float pivotFrequency_ = kDefaultPivot;
    float smoothingMs_ = kDefaultSmoothing;

    // State
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
