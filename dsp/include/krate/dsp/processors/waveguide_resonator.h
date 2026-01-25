// ==============================================================================
// Layer 2: DSP Processor - Waveguide Resonator
// ==============================================================================
// Digital waveguide implementing bidirectional wave propagation for flute/pipe-
// like resonances. Implements Kelly-Lochbaum scattering at terminations for
// physically accurate end reflection modeling.
//
// Part of Phase 13.3 (Physical Modeling Resonators) in the filter roadmap.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 2 (depends only on Layer 0/1)
// - Principle XII: Test-First Development
//
// Reference: specs/085-waveguide-resonator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/one_pole.h>
#include <krate/dsp/primitives/one_pole_allpass.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace Krate {
namespace DSP {

/// @brief Digital waveguide resonator for flute/pipe-like resonances.
///
/// Implements bidirectional wave propagation with Kelly-Lochbaum scattering
/// at terminations for physically accurate pipe/tube modeling.
///
/// @par Waveguide Model:
/// A digital waveguide models a 1D acoustic medium (string, tube, etc.) using
/// two delay lines representing traveling waves in opposite directions (FR-001).
///
/// @code
///     [Left End]                                          [Right End]
///     leftReflection                                      rightReflection
///          |                                                    |
///          v                                                    v
///     <----+<---[leftGoingDelay]<----[Loss/Disp]<---------------+
///          |                              ^                     |
///     [Loss/Disp]               (excitation point)              |
///          |                        input/output                |
///          v                              v                     |
///     +----+--->[rightGoingDelay]-------->+-------------------->+
/// @endcode
///
/// The resonant frequency is determined by the total round-trip delay:
/// - f0 = sampleRate / (2 * delaySamples) for open-open or closed-closed
/// - f0 = sampleRate / (4 * delaySamples) for open-closed (half-wavelength)
///
/// @par Features:
/// - Configurable end reflections (open, closed, partial)
/// - Frequency-dependent loss (high frequencies decay faster)
/// - Dispersion for inharmonicity (bell-like timbres)
/// - Excitation point control (affects harmonic emphasis)
/// - Parameter smoothing (click-free automation)
///
/// @par Usage Example
/// @code
/// WaveguideResonator wg;
/// wg.prepare(44100.0);
/// wg.setFrequency(440.0f);
/// wg.setEndReflection(-1.0f, -1.0f);  // Open-open (flute-like)
/// wg.setLoss(0.1f);
///
/// // In audio callback:
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = wg.process(input[i]);
/// }
/// @endcode
///
/// @see specs/085-waveguide-resonator/spec.md
class WaveguideResonator {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Minimum supported frequency in Hz
    static constexpr float kMinFrequency = 20.0f;

    /// Maximum frequency ratio relative to sample rate
    static constexpr float kMaxFrequencyRatio = 0.45f;

    /// Minimum delay in samples (prevents instability at very high frequencies)
    static constexpr size_t kMinDelaySamples = 2;

    /// Minimum reflection coefficient
    static constexpr float kMinReflection = -1.0f;

    /// Maximum reflection coefficient
    static constexpr float kMaxReflection = +1.0f;

    /// Maximum loss value (prevents complete signal zeroing)
    static constexpr float kMaxLoss = 0.9999f;

    /// Default smoothing time for parameters (ms)
    static constexpr float kDefaultSmoothingMs = 20.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor.
    WaveguideResonator() noexcept = default;

    /// @brief Destructor.
    ~WaveguideResonator() = default;

    // Non-copyable, movable
    WaveguideResonator(const WaveguideResonator&) = delete;
    WaveguideResonator& operator=(const WaveguideResonator&) = delete;
    WaveguideResonator(WaveguideResonator&&) noexcept = default;
    WaveguideResonator& operator=(WaveguideResonator&&) noexcept = default;

    /// @brief Prepare the waveguide for processing.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @note FR-020: Allocates delay lines for 20Hz minimum frequency
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

        // Calculate max delay for minimum frequency (20Hz)
        // For a waveguide: total_round_trip = sampleRate / frequency
        // Each direction = total / 2
        float maxDelaySeconds = 1.0f / kMinFrequency;
        rightGoingDelay_.prepare(sampleRate_, maxDelaySeconds);
        leftGoingDelay_.prepare(sampleRate_, maxDelaySeconds);

        // Prepare loss filters (FR-009: one in each delay line)
        lossFilter_.prepare(sampleRate_);
        leftLossFilter_.prepare(sampleRate_);

        // Prepare dispersion filters (FR-012: one in each delay line)
        dispersionFilter_.prepare(sampleRate_);
        leftDispersionFilter_.prepare(sampleRate_);

        // Prepare DC blocker with 10Hz cutoff (FR-026)
        dcBlocker_.prepare(sampleRate_, 10.0f);

        // Configure parameter smoothers (20ms smoothing time)
        float sampleRateFloat = static_cast<float>(sampleRate_);
        frequencySmoother_.configure(kDefaultSmoothingMs, sampleRateFloat);
        lossSmoother_.configure(kDefaultSmoothingMs, sampleRateFloat);
        dispersionSmoother_.configure(kDefaultSmoothingMs, sampleRateFloat);

        prepared_ = true;

        // Apply default settings (snap smoothers to initial values)
        setFrequency(440.0f);
        frequencySmoother_.snapTo(frequency_);
        setLoss(0.1f);
        lossSmoother_.snapTo(loss_);
        setDispersion(0.0f);
        dispersionSmoother_.snapTo(dispersion_);
        setEndReflection(-1.0f, -1.0f);  // Default: open-open
        setExcitationPoint(0.5f);  // Default: center

        updateDelayLength();
        updateLossFilter();
        updateDispersionFilter();
    }

    /// @brief Reset all state to silence.
    /// @note FR-021: Clears delay lines, filters, and smoothers
    /// @note FR-024: No memory allocation during reset
    void reset() noexcept {
        rightGoingDelay_.reset();
        leftGoingDelay_.reset();
        lossFilter_.reset();
        leftLossFilter_.reset();
        dispersionFilter_.reset();
        leftDispersionFilter_.reset();
        dcBlocker_.reset();

        // Snap smoothers to current targets
        frequencySmoother_.snapTo(frequency_);
        lossSmoother_.snapTo(loss_);
        dispersionSmoother_.snapTo(dispersion_);
    }

    // =========================================================================
    // Frequency Control
    // =========================================================================

    /// @brief Set the resonant frequency.
    /// @param hz Frequency in Hz
    /// @note FR-002, FR-004: Clamped to [20Hz, sampleRate * 0.45]
    /// @note FR-018: Uses parameter smoothing
    void setFrequency(float hz) noexcept {
        float maxFreq = static_cast<float>(sampleRate_) * kMaxFrequencyRatio;
        frequency_ = std::clamp(hz, kMinFrequency, maxFreq);
        frequencySmoother_.setTarget(frequency_);
    }

    /// @brief Get the current frequency setting.
    /// @return Frequency in Hz (target value, may differ from smoothed)
    [[nodiscard]] float getFrequency() const noexcept {
        return frequency_;
    }

    /// @brief Snap all smoothed parameters to their target values instantly.
    /// @note Useful for testing or when immediate parameter changes are needed.
    void snapParameters() noexcept {
        frequencySmoother_.snapTo(frequency_);
        lossSmoother_.snapTo(loss_);
        dispersionSmoother_.snapTo(dispersion_);
        updateDelayLength();
        updateLossFilter();
        updateDispersionFilter();
    }

    // =========================================================================
    // End Reflection Control
    // =========================================================================

    /// @brief Set both end reflection coefficients.
    /// @param left Left end reflection [-1.0, +1.0]
    /// @param right Right end reflection [-1.0, +1.0]
    /// @note FR-005, FR-006, FR-007: Kelly-Lochbaum impedance-based reflections
    /// @note FR-019: Changes instantly (no smoothing)
    void setEndReflection(float left, float right) noexcept {
        leftReflection_ = std::clamp(left, kMinReflection, kMaxReflection);
        rightReflection_ = std::clamp(right, kMinReflection, kMaxReflection);
    }

    /// @brief Set left end reflection coefficient.
    /// @param coefficient Reflection [-1.0 = open/inverted, +1.0 = closed/positive]
    void setLeftReflection(float coefficient) noexcept {
        leftReflection_ = std::clamp(coefficient, kMinReflection, kMaxReflection);
    }

    /// @brief Set right end reflection coefficient.
    /// @param coefficient Reflection [-1.0 = open/inverted, +1.0 = closed/positive]
    void setRightReflection(float coefficient) noexcept {
        rightReflection_ = std::clamp(coefficient, kMinReflection, kMaxReflection);
    }

    /// @brief Get left end reflection coefficient.
    [[nodiscard]] float getLeftReflection() const noexcept {
        return leftReflection_;
    }

    /// @brief Get right end reflection coefficient.
    [[nodiscard]] float getRightReflection() const noexcept {
        return rightReflection_;
    }

    // =========================================================================
    // Loss Control
    // =========================================================================

    /// @brief Set the loss amount (frequency-dependent damping).
    /// @param amount Loss [0.0 = no loss, ~1.0 = maximum loss]
    /// @note FR-008, FR-009, FR-010: Controls OnePoleLP cutoff in feedback
    /// @note FR-018: Uses parameter smoothing
    void setLoss(float amount) noexcept {
        loss_ = std::clamp(amount, 0.0f, kMaxLoss);
        lossSmoother_.setTarget(loss_);
    }

    /// @brief Get the current loss setting.
    [[nodiscard]] float getLoss() const noexcept {
        return loss_;
    }

    // =========================================================================
    // Dispersion Control
    // =========================================================================

    /// @brief Set the dispersion amount (inharmonicity).
    /// @param amount Dispersion [0.0 = harmonic, higher = more inharmonic]
    /// @note FR-011, FR-012, FR-013: Controls OnePoleAllpass frequency
    /// @note FR-018: Uses parameter smoothing
    void setDispersion(float amount) noexcept {
        dispersion_ = std::clamp(amount, 0.0f, 1.0f);
        dispersionSmoother_.setTarget(dispersion_);
    }

    /// @brief Get the current dispersion setting.
    [[nodiscard]] float getDispersion() const noexcept {
        return dispersion_;
    }

    // =========================================================================
    // Excitation Point Control
    // =========================================================================

    /// @brief Set the excitation/output point position along the waveguide.
    /// @param position Position [0.0 = left end, 1.0 = right end, 0.5 = center]
    /// @note FR-014, FR-015, FR-016: Controls input injection and output tap
    /// @note FR-019: Changes instantly (no smoothing)
    void setExcitationPoint(float position) noexcept {
        excitationPoint_ = std::clamp(position, 0.0f, 1.0f);
    }

    /// @brief Get the current excitation point position.
    [[nodiscard]] float getExcitationPoint() const noexcept {
        return excitationPoint_;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample.
    /// @param input Input sample (excitation signal)
    /// @return Resonated output sample
    /// @note FR-022, FR-023, FR-024, FR-025, FR-026, FR-027
    [[nodiscard]] float process(float input) noexcept {
        // FR-025: Return 0 if not prepared
        if (!prepared_) {
            return 0.0f;
        }

        // FR-027: NaN/Inf input handling - reset and return 0
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // Update smoothed parameters
        float smoothedFreq = frequencySmoother_.process();
        float smoothedLoss = lossSmoother_.process();
        float smoothedDispersion = dispersionSmoother_.process();

        // Update filter coefficients if parameters are still smoothing
        if (!frequencySmoother_.isComplete()) {
            updateDelayLengthFromSmoothed(smoothedFreq);
        }
        if (!lossSmoother_.isComplete()) {
            updateLossFilterFromSmoothed(smoothedLoss);
        }
        if (!dispersionSmoother_.isComplete()) {
            updateDispersionFilterFromSmoothed(smoothedDispersion);
        }

        // =====================================================================
        // True Bidirectional Digital Waveguide (FR-001)
        // =====================================================================
        //
        // This implementation uses two delay lines for bidirectional wave
        // propagation as required by FR-001:
        //   - rightGoingDelay_: stores waves traveling LEFT → RIGHT
        //   - leftGoingDelay_: stores waves traveling RIGHT → LEFT
        //
        // Wave topology (per D'Alembert's solution to 1D wave equation):
        //
        //     [Left End]                                    [Right End]
        //     leftReflection_                              rightReflection_
        //          |                                              |
        //          v                                              v
        //     <----+<---[leftGoingDelay_]<----+<---[Loss/Disp]<---+
        //          |                          |                   |
        //     [Loss/Disp]              (excitation point)         |
        //          |                     input/output             |
        //          v                          |                   v
        //     +----+--->>[rightGoingDelay_]-->+------------------>+
        //
        // Wave travel:
        //   - rightGoingDelay_ wave arrives at RIGHT end after delaySamples_
        //   - leftGoingDelay_ wave arrives at LEFT end after delaySamples_
        //
        // Kelly-Lochbaum scattering at terminations (FR-007):
        //   - Wave arriving at RIGHT end reflects with rightReflection_
        //   - Wave arriving at LEFT end reflects with leftReflection_
        //   - Reflected wave starts traveling in opposite direction
        //
        // =====================================================================

        // 1. Read waves arriving at each end
        //    rightGoingDelay_ stores L→R waves, so read gives wave at RIGHT end
        //    leftGoingDelay_ stores R→L waves, so read gives wave at LEFT end
        float waveAtRightEnd = rightGoingDelay_.readAllpass(delaySamples_);
        float waveAtLeftEnd = leftGoingDelay_.readAllpass(delaySamples_);

        // 2. Apply reflections at each end (Kelly-Lochbaum, FR-007)
        //    -1.0 = open end (inverted reflection)
        //    +1.0 = closed end (non-inverted reflection)
        float reflectedAtRight = rightReflection_ * waveAtRightEnd;
        float reflectedAtLeft = leftReflection_ * waveAtLeftEnd;

        // 3. Apply loss filters (frequency-dependent damping, FR-008, FR-009)
        //    Each reflection path has its own loss filter for symmetric damping
        float lossedRight = lossFilter_.process(reflectedAtRight);
        float lossedLeft = leftLossFilter_.process(reflectedAtLeft);

        // 4. Apply dispersion filters if enabled (FR-011, FR-012)
        if (dispersion_ > 0.001f) {
            lossedRight = dispersionFilter_.process(lossedRight);
            lossedLeft = leftDispersionFilter_.process(lossedLeft);
        }

        // 5. Flush denormals in feedback paths (FR-025)
        lossedRight = detail::flushDenormal(lossedRight);
        lossedLeft = detail::flushDenormal(lossedLeft);

        // 6. Input injection based on excitation point (FR-014, FR-015)
        //    excitationPoint_ = 0.0 -> inject into right-going wave only (left end)
        //    excitationPoint_ = 1.0 -> inject into left-going wave only (right end)
        //    excitationPoint_ = 0.5 -> equal injection to both (center)
        float rightGoingInput = input * (1.0f - excitationPoint_);
        float leftGoingInput = input * excitationPoint_;

        // 7. Write to delay lines (correct bidirectional routing)
        //    Wave reflected at LEFT end → now travels RIGHT → rightGoingDelay_
        //    Wave reflected at RIGHT end → now travels LEFT → leftGoingDelay_
        rightGoingDelay_.write(lossedLeft + rightGoingInput);
        leftGoingDelay_.write(lossedRight + leftGoingInput);

        // 8. Read output at excitation point (FR-017)
        //    Output is sum of both waves at the excitation point position
        //    For excitation point p, we read from:
        //      - rightGoingDelay at position p * delaySamples_ (wave traveling from left)
        //      - leftGoingDelay at position (1-p) * delaySamples_ (wave traveling from right)
        float rightReadDelay = excitationPoint_ * delaySamples_;
        float leftReadDelay = (1.0f - excitationPoint_) * delaySamples_;

        // Ensure minimum delay of 1 sample for valid read
        rightReadDelay = std::max(rightReadDelay, 1.0f);
        leftReadDelay = std::max(leftReadDelay, 1.0f);

        float outputRight = rightGoingDelay_.readLinear(rightReadDelay);
        float outputLeft = leftGoingDelay_.readLinear(leftReadDelay);

        // Sum both traveling waves (models acoustic pressure at excitation point)
        float output = outputRight + outputLeft;

        // 9. Apply DC blocking (FR-026)
        output = dcBlocker_.process(output);

        return output;
    }

    /// @brief Process a block of samples in-place.
    /// @param buffer Audio buffer (input, modified to output)
    /// @param numSamples Number of samples to process
    void processBlock(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    /// @brief Process a block with separate input/output buffers.
    /// @param input Input buffer
    /// @param output Output buffer
    /// @param numSamples Number of samples to process
    void processBlock(const float* input, float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process(input[i]);
        }
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if the waveguide has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

private:
    // =========================================================================
    // Private Methods
    // =========================================================================

    /// Update delay line length based on current frequency
    void updateDelayLength() noexcept {
        updateDelayLengthFromSmoothed(frequency_);
    }

    /// Calculate loss filter cutoff frequency based on loss parameter
    /// @param smoothedLoss Current smoothed loss value
    /// @return Loss filter cutoff frequency in Hz
    [[nodiscard]] float calculateLossCutoff(float smoothedLoss) const noexcept {
        float maxCutoff = static_cast<float>(sampleRate_) * 0.45f;
        float minCutoff = std::max(frequency_, 100.0f);
        return maxCutoff - smoothedLoss * (maxCutoff - minCutoff);
    }

    /// Update delay line length from smoothed frequency value
    /// Uses frequency-dependent phase delay compensation for accurate pitch (SC-002)
    ///
    /// Based on research (specs/085-waveguide-resonator/research.md Section 7):
    /// The loss filter introduces frequency-dependent phase delay that must be
    /// compensated to achieve accurate pitch.
    ///
    /// Note: First-order allpass interpolation in feedback loops has inherent
    /// tuning limitations due to the interaction between allpass state and
    /// resonant signal. Literature recommends accepting ~3 cent accuracy or
    /// using higher-order interpolation (Thiran, Lagrange).
    void updateDelayLengthFromSmoothed(float smoothedFreq) noexcept {
        float totalDelay = static_cast<float>(sampleRate_) / smoothedFreq;
        float delayPerDirection = totalDelay * 0.5f;

        // Calculate loss filter cutoff (same formula as updateLossFilterFromSmoothed)
        float lossCutoff = calculateLossCutoff(loss_);

        // Phase delay of first-order lowpass at frequency f with cutoff fc:
        // phaseDelay_samples = arctan(f / fc) / (2 * pi * f) * sampleRate
        // Reference: research.md Section 7, "Loss Filter Phase Delay"
        constexpr float kPi = 3.14159265358979323846f;
        float lossPhaseDelay = std::atan(smoothedFreq / lossCutoff) /
                               (2.0f * kPi * smoothedFreq) *
                               static_cast<float>(sampleRate_);

        // Allpass interpolator contributes approximately 0.5 samples at fundamental
        // (This is an inherent property of first-order allpass in feedback)
        constexpr float kAllpassBaseDelay = 0.5f;

        float compensation = lossPhaseDelay + kAllpassBaseDelay;

        delaySamples_ = delayPerDirection - compensation;
        delaySamples_ = std::max(delaySamples_, static_cast<float>(kMinDelaySamples));
    }

    /// Update loss filter cutoff based on current loss parameter
    void updateLossFilter() noexcept {
        updateLossFilterFromSmoothed(loss_);
    }

    /// Update loss filter cutoff from smoothed loss value
    void updateLossFilterFromSmoothed(float smoothedLoss) noexcept {
        // Higher loss = lower cutoff = faster HF decay
        // Map loss [0, 1] to cutoff [Nyquist*0.9, fundamental]
        float cutoff = calculateLossCutoff(smoothedLoss);

        lossFilter_.setCutoff(cutoff);
        leftLossFilter_.setCutoff(cutoff);  // FR-009: symmetric damping
    }

    /// Update dispersion filter frequency based on current dispersion parameter
    void updateDispersionFilter() noexcept {
        updateDispersionFilterFromSmoothed(dispersion_);
    }

    /// Update dispersion filter frequency from smoothed dispersion value
    void updateDispersionFilterFromSmoothed(float smoothedDispersion) noexcept {
        // Higher dispersion = lower break frequency = more phase dispersion
        float maxFreq = static_cast<float>(sampleRate_) * 0.4f;
        float minFreq = 100.0f;
        float breakFreq = maxFreq - smoothedDispersion * (maxFreq - minFreq);

        dispersionFilter_.setFrequency(breakFreq);
        leftDispersionFilter_.setFrequency(breakFreq);  // FR-012: symmetric dispersion
    }

    // =========================================================================
    // Components
    // =========================================================================

    DelayLine rightGoingDelay_;  ///< Right-going wave delay line
    DelayLine leftGoingDelay_;   ///< Left-going wave delay line

    OnePoleLP lossFilter_;       ///< Loss filter for right reflection path
    OnePoleLP leftLossFilter_;   ///< Loss filter for left reflection path (FR-009)
    OnePoleAllpass dispersionFilter_;      ///< Dispersion filter for right path
    OnePoleAllpass leftDispersionFilter_;  ///< Dispersion filter for left path (FR-012)

    DCBlocker dcBlocker_;  ///< DC blocking at output

    OnePoleSmoother frequencySmoother_;   ///< Smooth frequency changes
    OnePoleSmoother lossSmoother_;        ///< Smooth loss changes
    OnePoleSmoother dispersionSmoother_;  ///< Smooth dispersion changes

    // =========================================================================
    // Parameters
    // =========================================================================

    double sampleRate_ = 44100.0;
    float frequency_ = 440.0f;
    float leftReflection_ = -1.0f;   ///< Default: open end (inverted)
    float rightReflection_ = -1.0f;  ///< Default: open end (inverted)
    float loss_ = 0.1f;
    float dispersion_ = 0.0f;
    float excitationPoint_ = 0.5f;   ///< Default: center

    // =========================================================================
    // State
    // =========================================================================

    float delaySamples_ = 50.0f;  ///< Delay per direction

    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
