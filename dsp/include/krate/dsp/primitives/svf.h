// ==============================================================================
// Layer 1: DSP Primitives
// svf.h - TPT State Variable Filter
// ==============================================================================
// API Contract for specs/080-svf
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (RAII, value semantics)
// - Principle IX: Layer 1 (depends only on Layer 0)
// - Principle X: DSP Constraints (flush denormals, handle edge cases)
//
// Reference: Cytomic SvfLinearTrapOptimised2.pdf
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>

#include <cmath>
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
    void prepare(double sampleRate) noexcept {
        // Clamp sample rate to valid minimum
        sampleRate_ = (sampleRate >= 1000.0) ? sampleRate : 1000.0;
        prepared_ = true;

        // Recalculate all coefficients
        updateCoefficients();
    }

    // =========================================================================
    // Configuration (FR-005 through FR-009)
    // =========================================================================

    /// @brief Set the filter mode for process() output.
    ///
    /// Does not affect processMulti() which always returns all four basic outputs.
    ///
    /// @param mode Filter response type
    /// @see SVFMode for available modes
    void setMode(SVFMode mode) noexcept {
        mode_ = mode;
        updateMixCoefficients();
    }

    /// @brief Set the cutoff/center frequency.
    ///
    /// Coefficients are recalculated immediately (no smoothing).
    /// The frequency is clamped to [1 Hz, sampleRate * 0.495].
    ///
    /// @param hz Cutoff frequency in Hz
    void setCutoff(float hz) noexcept {
        cutoffHz_ = clampCutoff(hz);
        updateCoefficients();
    }

    /// @brief Set the Q factor (resonance).
    ///
    /// Coefficients are recalculated immediately (no smoothing).
    /// The Q is clamped to [0.1, 30.0].
    ///
    /// @param q Q factor (0.7071 = Butterworth, higher = more resonant)
    void setResonance(float q) noexcept {
        q_ = clampQ(q);
        updateCoefficients();
    }

    /// @brief Set the gain for peak and shelf modes.
    ///
    /// Ignored for Lowpass, Highpass, Bandpass, Notch, and Allpass modes.
    /// The gain is clamped to [-24 dB, +24 dB].
    ///
    /// @param dB Gain in decibels
    void setGain(float dB) noexcept {
        gainDb_ = clampGainDb(dB);
        // FR-008: Calculate A immediately
        A_ = detail::constexprPow10(gainDb_ / 40.0f);
        updateMixCoefficients();  // m1_, m2_ depend on A_ for shelf modes
    }

    /// @brief Reset filter state without changing parameters.
    ///
    /// Clears the internal integrator states (ic1eq, ic2eq) to zero.
    /// Use when starting a new audio region to prevent click artifacts.
    void reset() noexcept {
        ic1eq_ = 0.0f;
        ic2eq_ = 0.0f;
    }

    // =========================================================================
    // Getters
    // =========================================================================

    /// @brief Get the current filter mode.
    [[nodiscard]] SVFMode getMode() const noexcept { return mode_; }

    /// @brief Get the current cutoff frequency in Hz.
    [[nodiscard]] float getCutoff() const noexcept { return cutoffHz_; }

    /// @brief Get the current Q factor.
    [[nodiscard]] float getResonance() const noexcept { return q_; }

    /// @brief Get the current gain in dB.
    [[nodiscard]] float getGain() const noexcept { return gainDb_; }

    /// @brief Check if the filter has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

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
    [[nodiscard]] float process(float input) noexcept {
        // FR-021: Return input unchanged if not prepared
        if (!prepared_) {
            return input;
        }

        // FR-022: Handle NaN/Inf input
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // FR-016: Per-sample computation
        const float v3 = input - ic2eq_;
        const float v1 = a1_ * ic1eq_ + a2_ * v3;
        const float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;

        // Update integrator states (trapezoidal rule)
        ic1eq_ = 2.0f * v1 - ic1eq_;
        ic2eq_ = 2.0f * v2 - ic2eq_;

        // FR-019: Flush denormals after every process() call
        ic1eq_ = detail::flushDenormal(ic1eq_);
        ic2eq_ = detail::flushDenormal(ic2eq_);

        // FR-016: Compute outputs
        // Note: HP = v0 - k*v1 - v2 (using input v0, not v3)
        const float low = v2;
        const float band = v1;
        const float high = input - k_ * v1 - v2;

        // FR-017: Mode mixing
        return m0_ * high + m1_ * band + m2_ * low;
    }

    /// @brief Process a block of samples in-place.
    ///
    /// Equivalent to calling process() on each sample sequentially.
    /// Produces bit-identical output to the equivalent process() calls.
    ///
    /// @param buffer Pointer to sample buffer (modified in-place)
    /// @param numSamples Number of samples to process
    ///
    /// @note No memory allocation occurs during processing
    void processBlock(float* buffer, size_t numSamples) noexcept {
        // Simple loop - produces bit-identical output to process() calls (SC-012)
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

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
    [[nodiscard]] SVFOutputs processMulti(float input) noexcept {
        // FR-021: Return zeros if not prepared
        if (!prepared_) {
            return SVFOutputs{0.0f, 0.0f, 0.0f, 0.0f};
        }

        // FR-022: Handle NaN/Inf input
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return SVFOutputs{0.0f, 0.0f, 0.0f, 0.0f};
        }

        // FR-016: Per-sample computation
        const float v3 = input - ic2eq_;
        const float v1 = a1_ * ic1eq_ + a2_ * v3;
        const float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;

        // Update integrator states (trapezoidal rule)
        ic1eq_ = 2.0f * v1 - ic1eq_;
        ic2eq_ = 2.0f * v2 - ic2eq_;

        // FR-019: Flush denormals after every process() call
        ic1eq_ = detail::flushDenormal(ic1eq_);
        ic2eq_ = detail::flushDenormal(ic2eq_);

        // Compute all outputs
        // Note: HP = v0 - k*v1 - v2 (using input v0, not v3)
        // Note: Band is normalized by k for constant 0dB peak gain (BPK)
        SVFOutputs out;
        out.low = v2;
        out.band = k_ * v1;  // Normalized for constant 0dB peak
        out.high = input - k_ * v1 - v2;
        out.notch = out.low + out.high;

        return out;
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Update all filter coefficients based on current parameters.
    void updateCoefficients() noexcept {
        // FR-013: g = tan(pi * cutoff / sampleRate)
        g_ = std::tan(kPi * cutoffHz_ / static_cast<float>(sampleRate_));

        // FR-013: k = 1/Q
        k_ = 1.0f / q_;

        // FR-014: Derived coefficients
        a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;

        // Update mode mixing (depends on k_ and A_)
        updateMixCoefficients();
    }

    /// @brief Update mode mixing coefficients based on current mode and parameters.
    ///
    /// The Cytomic SVF computes: output = m0*high + m1*band + m2*low
    /// where high = input - k*band - low (computed differently for efficiency)
    ///
    /// Mode mixing coefficients per Cytomic SvfLinearTrapOptimised2.pdf:
    /// - Lowpass:   m0=0, m1=0, m2=1
    /// - Highpass:  m0=1, m1=0, m2=0
    /// - Bandpass (constant peak gain): m0=0, m1=k, m2=0 (normalized by 1/Q)
    /// - Notch:     m0=1, m1=0, m2=1 (low + high)
    /// - Allpass:   m0=1, m1=-2*k, m2=1 (phase shift only)
    /// - Peak (boost): m0=1, m1=k*(A^2-1), m2=1
    /// - LowShelf:  m0=1, m1=k*(A-1), m2=A^2 - 1 + 1 = A^2
    /// - HighShelf: m0=A^2, m1=k*(A-1), m2=1
    void updateMixCoefficients() noexcept {
        // FR-017: Mode mixing coefficients
        switch (mode_) {
            case SVFMode::Lowpass:
                m0_ = 0.0f;
                m1_ = 0.0f;
                m2_ = 1.0f;
                break;
            case SVFMode::Highpass:
                m0_ = 1.0f;
                m1_ = 0.0f;
                m2_ = 0.0f;
                break;
            case SVFMode::Bandpass:
                // Constant 0dB peak gain bandpass (BPK in Cytomic)
                // The raw bandpass v1 has gain proportional to Q
                // Multiply by k = 1/Q to normalize to 0dB peak
                m0_ = 0.0f;
                m1_ = k_;
                m2_ = 0.0f;
                break;
            case SVFMode::Notch:
                // Notch = low + high
                m0_ = 1.0f;
                m1_ = 0.0f;
                m2_ = 1.0f;
                break;
            case SVFMode::Allpass:
                // Allpass = low + high - k*band = low + high - 2*k*band + k*band
                // Actually: allpass = input - 2*k*band (phase-only transformation)
                // Using mix: high + (-2*k)*band + low
                // Note: high already has -k*band subtracted, so:
                // high + low = notch, and allpass = notch - k*band
                // So: m0=1, m1=-k, m2=1 (wait, need to verify)
                // Actually from Cytomic: AP = v0 - 2*k*v1 where v0=input
                // But we're mixing: m0*high + m1*band + m2*low
                // high = v0 - k*v1 - low, so:
                // allpass = v0 - 2*k*v1 = (v0 - k*v1 - low) + low - k*v1
                //         = high + low - k*band
                // So m0=1, m1=-k, m2=1
                m0_ = 1.0f;
                m1_ = -k_;
                m2_ = 1.0f;
                break;
            case SVFMode::Peak:
                // Peak EQ (parametric bell) from Cytomic:
                // peak = input + k*(A^2-1)*band
                // Rewritten as mix: high + low + k*(A^2-1)*band
                //                 = (input - k*band - low) + low + k*(A^2-1)*band
                //                 = input - k*band + k*(A^2-1)*band
                //                 = input + k*band*(A^2-1-1)
                //                 = input + k*band*(A^2-2)
                // Hmm, let me re-check. The Cytomic peak formula is:
                // peak = v0 + k*(A*A - 1)*v1 where A = 10^(dB/40)
                // To express as mix: m0*high + m1*band + m2*low
                // high + low = v0 - k*v1 (that's the notch)
                // So: peak = (high + low) + k*v1 + k*(A*A-1)*v1
                //          = high + low + k*A*A*v1
                //          = 1*high + k*A*A*band + 1*low
                m0_ = 1.0f;
                m1_ = k_ * A_ * A_;
                m2_ = 1.0f;
                break;
            case SVFMode::LowShelf:
                // LowShelf from Cytomic: shelf = input + k*(A-1)*band + (A*A-1)*low
                // = (high + low) + k*band + k*(A-1)*band + (A*A-1)*low
                // = high + k*band*(1 + A - 1) + low + (A*A-1)*low
                // Wait, let me re-derive:
                // input = high + k*band + low (from high definition)
                // shelf = input + k*(A-1)*band + (A*A-1)*low
                //       = high + k*band + low + k*(A-1)*band + (A*A-1)*low
                //       = high + k*A*band + A*A*low
                m0_ = 1.0f;
                m1_ = k_ * A_;
                m2_ = A_ * A_;
                break;
            case SVFMode::HighShelf:
                // HighShelf from Cytomic: shelf = input*A*A + k*(A-1)*band + (1-A*A)*low
                //                              = input*A*A + k*(A-1)*band - (A*A-1)*low
                // input = high + k*band + low
                // shelf = (high + k*band + low)*A*A + k*(A-1)*band - (A*A-1)*low
                //       = high*A*A + k*A*A*band + low*A*A + k*(A-1)*band - A*A*low + low
                //       = A*A*high + k*band*(A*A + A - 1) + low
                // Hmm, this is getting complex. Let me use the simpler Cytomic formula:
                // HighShelf: m0 = A*A, m1 = k*(A-1), m2 = 1
                // But we need to add the base signal too...
                // Actually from Cytomic Table:
                // HSH (high shelf): A*A*v0 + k*(A-1)*v1 + (1-A*A)*v2
                // = A*A*(high + k*band + low) + k*(A-1)*band + (1-A*A)*low
                // = A*A*high + A*A*k*band + A*A*low + k*(A-1)*band + low - A*A*low
                // = A*A*high + k*band*(A*A + A - 1) + low
                // So m0 = A*A, m1 = k*(A*A + A - 1), m2 = 1
                // At A=1 (0dB): m0=1, m1=k*(1+1-1)=k, m2=1, which gives notch + k*band = input (unity)
                // Let me verify with A = 2 (6dB): m0=4, m1=k*5, m2=1
                // Simplified: just use Cytomic's direct form
                m0_ = A_ * A_;
                m1_ = k_ * (A_ * A_ + A_ - 1.0f);
                m2_ = 1.0f;
                break;
        }
    }

    /// @brief Clamp cutoff frequency to valid range.
    [[nodiscard]] float clampCutoff(float hz) const noexcept {
        const float maxFreq = static_cast<float>(sampleRate_) * kMaxCutoffRatio;
        if (hz < kMinCutoff) return kMinCutoff;
        if (hz > maxFreq) return maxFreq;
        return hz;
    }

    /// @brief Clamp Q factor to valid range.
    [[nodiscard]] float clampQ(float q) const noexcept {
        if (q < kMinQ) return kMinQ;
        if (q > kMaxQ) return kMaxQ;
        return q;
    }

    /// @brief Clamp gain to valid range.
    [[nodiscard]] float clampGainDb(float dB) const noexcept {
        if (dB < kMinGainDb) return kMinGainDb;
        if (dB > kMaxGainDb) return kMaxGainDb;
        return dB;
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

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
};

} // namespace DSP
} // namespace Krate
