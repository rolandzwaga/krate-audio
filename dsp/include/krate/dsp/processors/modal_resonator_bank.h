#pragma once

// ==============================================================================
// ModalResonatorBank - Bank of damped coupled-form resonators
// ==============================================================================
// Layer 2 Processor | Namespace: Krate::DSP
//
// Up to 96 parallel modes using the Gordon-Smith coupled-form topology.
// SoA memory layout with 32-byte alignment for SIMD processing.
// Chaigne-Lambourg frequency-dependent damping model.
// ==============================================================================

#include <krate/dsp/core/dsp_utils.h>
#include <krate/dsp/processors/iresonator.h>
#include <krate/dsp/processors/modal_resonator_bank_simd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace Krate {
namespace DSP {

class ModalResonatorBank : public IResonator {
public:
    static constexpr int kMaxModes = 96;

    ModalResonatorBank() noexcept = default;
    ~ModalResonatorBank() override = default;

    // Non-copyable (large aligned arrays), movable
    ModalResonatorBank(const ModalResonatorBank&) = delete;
    ModalResonatorBank& operator=(const ModalResonatorBank&) = delete;
    ModalResonatorBank(ModalResonatorBank&&) noexcept = default;
    ModalResonatorBank& operator=(ModalResonatorBank&&) noexcept = default;

    /// Prepare for processing at the given sample rate.
    void prepare(double sampleRate) noexcept override
    {
        sampleRate_ = static_cast<float>(sampleRate);
        smoothCoeff_ = std::exp(-1.0f / (kSmoothingTimeMs * 0.001f * sampleRate_));
        envelopeAttackCoeff_ = std::exp(-1.0f / (kEnvelopeAttackMs * 0.001f * sampleRate_));
        // Energy follower EMA coefficients (FR-023)
        controlAlpha_ = std::exp(-1.0f / (kControlEnergyTauMs * 0.001f * sampleRate_));
        perceptualAlpha_ = std::exp(-1.0f / (kPerceptualEnergyTauMs * 0.001f * sampleRate_));
        prepared_ = true;
    }

    /// Reset all filter states and envelope state to zero.
    void reset() noexcept
    {
        std::memset(sinState_, 0, sizeof(sinState_));
        std::memset(cosState_, 0, sizeof(cosState_));
        envelopeState_ = 0.0f;
        previousEnvelope_ = 0.0f;
        // Snap smoothed arrays to targets
        std::memcpy(epsilon_, epsilonTarget_, sizeof(epsilon_));
        std::memcpy(radius_, radiusTarget_, sizeof(radius_));
        std::memcpy(inputGain_, inputGainTarget_, sizeof(inputGain_));
    }

    /// Configure all modes from analyzed harmonic data.
    /// Clears filter states (for note-on).
    void setModes(
        const float* frequencies,
        const float* amplitudes,
        int numPartials,
        float decayTime,
        float brightness,
        float stretch,
        float scatter
    ) noexcept
    {
        // Clear filter states on note-on (FR-018)
        std::memset(sinState_, 0, sizeof(sinState_));
        std::memset(cosState_, 0, sizeof(cosState_));
        envelopeState_ = 0.0f;
        previousEnvelope_ = 0.0f;
        computeModeCoefficients(frequencies, amplitudes, numPartials,
                                decayTime, brightness, stretch, scatter, true);
    }

    /// Update mode coefficients without clearing filter states.
    /// Used during frame transitions (FR-019).
    void updateModes(
        const float* frequencies,
        const float* amplitudes,
        int numPartials,
        float decayTime,
        float brightness,
        float stretch,
        float scatter
    ) noexcept
    {
        computeModeCoefficients(frequencies, amplitudes, numPartials,
                                decayTime, brightness, stretch, scatter, false);
    }

    /// Process a single sample through the resonator bank.
    /// Includes per-sample coefficient smoothing (for single-sample callers).
    [[nodiscard]] float processSample(float excitation) noexcept
    {
        return processSample(excitation, 1.0f);
    }

    /// Process a single sample with optional decay scaling (mallet choke).
    /// @param excitation Input excitation signal
    /// @param decayScale Decay acceleration factor:
    ///   - 1.0f = normal operation (no choke)
    ///   - >1.0f = accelerated decay (choke), applied as R_eff = pow(R, decayScale)
    ///   - Preserves relative damping between modes (material character retained)
    /// @note The pow() per mode is only computed when decayScale != 1.0f
    [[nodiscard]] float processSample(float excitation, float decayScale) noexcept
    {
        smoothCoefficients();
        return processSampleCore(excitation, decayScale);
    }

    /// Process a block of samples.
    /// Smooths coefficients once at block start (block-rate smoothing is
    /// sufficient for coefficient updates and saves ~9 FLOPs/mode/sample).
    /// Uses SIMD-accelerated mode loop via Highway dynamic dispatch.
    void processBlock(const float* input, float* output, int numSamples) noexcept
    {
        smoothCoefficients();
        for (int i = 0; i < numSamples; ++i) {
            float ex = applyTransientEmphasis(input[i]);

            // SIMD-accelerated mode loop (processes all modes for one sample)
            float modeSum = processModalBankSampleSIMD(
                sinState_, cosState_, epsilon_, radius_, inputGain_,
                ex, numModes_);

            // Soft-clip safety limiter (FR-010)
            output[i] = softClip(modeSum / kSoftClipThreshold) * kSoftClipThreshold;
        }
        flushSilentModes();
    }

    /// Process a block of samples with decay scaling (mallet choke).
    void processBlock(const float* input, float* output, int numSamples,
                      float decayScale) noexcept
    {
        smoothCoefficients();
        for (int i = 0; i < numSamples; ++i) {
            output[i] = processSampleCore(input[i], decayScale);
        }
        flushSilentModes();
    }

    /// Check mode energy and zero out states below silence threshold (FR-027).
    void flushSilentModes() noexcept
    {
        for (int k = 0; k < numModes_; ++k) {
            if (!active_[k])
                continue;
            float energy = sinState_[k] * sinState_[k] + cosState_[k] * cosState_[k];
            if (energy < kSilenceThreshold) {
                sinState_[k] = 0.0f;
                cosState_[k] = 0.0f;
                active_[k] = false;
                --numActiveModes_;
            }
        }
    }

    /// @return Number of active (non-culled) modes
    [[nodiscard]] int getNumActiveModes() const noexcept
    {
        return numActiveModes_;
    }

    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept
    {
        return prepared_;
    }

    // =========================================================================
    // IResonator interface adapter methods (FR-020, FR-023, FR-024, FR-025)
    // =========================================================================

    /// Store frequency for use by the voice engine via IResonator.
    /// ModalResonatorBank receives frequencies via setModes()/updateModes(),
    /// so this stores the value for future setModes calls from the voice engine.
    void setFrequency(float f0) noexcept override
    {
        storedFrequency_ = f0;
    }

    /// Store decay time for use via IResonator interface.
    void setDecay(float t60) noexcept override
    {
        storedDecayTime_ = t60;
    }

    /// Store brightness for use via IResonator interface.
    void setBrightness(float brightness) noexcept override
    {
        storedBrightness_ = brightness;
    }

    /// Process one sample through the resonator bank (IResonator interface).
    /// Delegates to processSample() and updates energy followers.
    [[nodiscard]] float process(float excitation) noexcept override
    {
        float output = processSample(excitation);
        // Update dual energy followers (FR-023, FR-024)
        float squaredOutput = output * output;
        controlEnergy_ = controlAlpha_ * controlEnergy_
                       + (1.0f - controlAlpha_) * squaredOutput;
        perceptualEnergy_ = perceptualAlpha_ * perceptualEnergy_
                          + (1.0f - perceptualAlpha_) * squaredOutput;
        return output;
    }

    /// Get fast energy follower (tau ~5ms) (FR-023).
    [[nodiscard]] float getControlEnergy() const noexcept override
    {
        return controlEnergy_;
    }

    /// Get slow energy follower (tau ~30ms) (FR-023).
    [[nodiscard]] float getPerceptualEnergy() const noexcept override
    {
        return perceptualEnergy_;
    }

    /// Clear all internal state including energy followers (FR-025).
    void silence() noexcept override
    {
        reset();
        controlEnergy_ = 0.0f;
        perceptualEnergy_ = 0.0f;
    }

    /// Modal resonator has no feedback velocity (returns 0.0f).
    [[nodiscard]] float getFeedbackVelocity() const noexcept override
    {
        return 0.0f;
    }

private:
    // Voicing constants (may be promoted to parameters in future phases)
    // kTransientEmphasisGain may be promoted to a user-facing parameter in a future phase
    static constexpr float kTransientEmphasisGain = 4.0f;
    static constexpr float kMaxB3 = 4.0e-5f;
    static constexpr float kSilenceThreshold = 1e-12f;
    static constexpr float kNyquistGuard = 0.49f;
    static constexpr float kAmplitudeThresholdLinear = 0.0001f; // -80 dB
    static constexpr float kSmoothingTimeMs = 2.0f;
    static constexpr float kEnvelopeAttackMs = 5.0f;
    static constexpr float kSoftClipThreshold = 0.707f; // -3 dBFS
    static constexpr float kControlEnergyTauMs = 5.0f;     // FR-023: fast energy follower
    static constexpr float kPerceptualEnergyTauMs = 30.0f;  // FR-023: slow energy follower

    // Golden-ratio-derived scatter displacement constant
    static constexpr float kScatterD =
        std::numbers::pi_v<float> * (std::numbers::phi_v<float> - 1.0f);

    // SoA state arrays (32-byte aligned for SIMD)
    alignas(32) float sinState_[kMaxModes]{};
    alignas(32) float cosState_[kMaxModes]{};
    alignas(32) float epsilon_[kMaxModes]{};
    alignas(32) float epsilonTarget_[kMaxModes]{};
    alignas(32) float radius_[kMaxModes]{};
    alignas(32) float radiusTarget_[kMaxModes]{};
    alignas(32) float inputGain_[kMaxModes]{};
    alignas(32) float inputGainTarget_[kMaxModes]{};
    bool active_[kMaxModes]{};

    int numActiveModes_ = 0;
    int numModes_ = 0;
    float sampleRate_ = 44100.0f;
    float smoothCoeff_ = 0.0f;
    float envelopeState_ = 0.0f;
    float previousEnvelope_ = 0.0f;
    float envelopeAttackCoeff_ = 0.0f;
    bool prepared_ = false;

    // IResonator energy followers (FR-023)
    float controlEnergy_ = 0.0f;
    float perceptualEnergy_ = 0.0f;
    float controlAlpha_ = 0.0f;
    float perceptualAlpha_ = 0.0f;

    // IResonator stored parameters (adapter state)
    float storedFrequency_ = 440.0f;
    float storedDecayTime_ = 0.5f;
    float storedBrightness_ = 0.5f;

    /// Compute mode coefficients from partial data.
    void computeModeCoefficients(
        const float* frequencies,
        const float* amplitudes,
        int numPartials,
        float decayTime,
        float brightness,
        float stretch,
        float scatter,
        bool snapSmoothing
    ) noexcept
    {
        // Clamp parameters to valid ranges
        numPartials = std::clamp(numPartials, 0, kMaxModes);
        decayTime = std::clamp(decayTime, 0.01f, 5.0f);
        brightness = std::clamp(brightness, 0.0f, 1.0f);
        stretch = std::clamp(stretch, 0.0f, 1.0f);
        scatter = std::clamp(scatter, 0.0f, 1.0f);

        numModes_ = numPartials;
        numActiveModes_ = 0;

        // Chaigne-Lambourg damping coefficients (FR-006)
        const float b1 = 1.0f / decayTime;
        const float b3 = (1.0f - brightness) * kMaxB3;

        // Inharmonicity parameters
        const float B = stretch * stretch * 0.001f;
        const float C = scatter * 0.02f;

        for (int k = 0; k < numPartials; ++k) {
            float f_k = frequencies[k];
            float amp = amplitudes[k];

            // Amplitude culling (FR-016)
            if (amp < kAmplitudeThresholdLinear) {
                active_[k] = false;
                epsilonTarget_[k] = 0.0f;
                radiusTarget_[k] = 0.0f;
                inputGainTarget_[k] = 0.0f;
                continue;
            }

            // Apply Stretch warping (FR-011): stiff-string inharmonicity
            float f_w = f_k * std::sqrt(1.0f + B * static_cast<float>(k) * static_cast<float>(k));

            // Apply Scatter warping (FR-012): deterministic sinusoidal displacement
            f_w *= (1.0f + C * std::sin(static_cast<float>(k) * kScatterD));

            // Nyquist culling (FR-015)
            if (f_w >= kNyquistGuard * sampleRate_) {
                active_[k] = false;
                epsilonTarget_[k] = 0.0f;
                radiusTarget_[k] = 0.0f;
                inputGainTarget_[k] = 0.0f;
                continue;
            }

            // Frequency-dependent damping (FR-006, FR-014)
            float decayRate_k = b1 + b3 * f_w * f_w;
            float R_k = std::exp(-decayRate_k / sampleRate_);

            // Coupled-form frequency coefficient
            float eps_k = 2.0f * std::sin(std::numbers::pi_v<float> * f_w / sampleRate_);

            // Leaky-integrator input gain normalization (FR-009)
            float gain_k = amp * (1.0f - R_k);

            epsilonTarget_[k] = eps_k;
            radiusTarget_[k] = R_k;
            inputGainTarget_[k] = gain_k;
            active_[k] = true;
            ++numActiveModes_;
        }

        // Deactivate and zero coefficients for modes beyond numPartials
        for (int k = numPartials; k < kMaxModes; ++k) {
            active_[k] = false;
            epsilonTarget_[k] = 0.0f;
            radiusTarget_[k] = 0.0f;
            inputGainTarget_[k] = 0.0f;
        }

        if (snapSmoothing) {
            std::memcpy(epsilon_, epsilonTarget_, sizeof(epsilon_));
            std::memcpy(radius_, radiusTarget_, sizeof(radius_));
            std::memcpy(inputGain_, inputGainTarget_, sizeof(inputGain_));
        }
    }

    /// Smooth coefficients toward targets (one-pole per coefficient).
    /// Called once per block (in processBlock) or once per sample (in processSample).
    /// Branchless: inactive modes have zero targets and converge to zero.
    void smoothCoefficients() noexcept
    {
        const float oneMinusCoeff = 1.0f - smoothCoeff_;
        for (int k = 0; k < numModes_; ++k) {
            epsilon_[k] += oneMinusCoeff * (epsilonTarget_[k] - epsilon_[k]);
            radius_[k] += oneMinusCoeff * (radiusTarget_[k] - radius_[k]);
            inputGain_[k] += oneMinusCoeff * (inputGainTarget_[k] - inputGain_[k]);
        }
    }

    /// Core per-sample resonator processing (no coefficient smoothing).
    /// Branchless inner loop: inactive modes have zero coefficients and
    /// contribute nothing to output.
    [[nodiscard]] float processSampleCore(float excitation, float decayScale = 1.0f) noexcept
    {
        float ex = applyTransientEmphasis(excitation);
        float output = 0.0f;

        if (decayScale != 1.0f) {
            for (int k = 0; k < numModes_; ++k) {
                float s = sinState_[k];
                float c = cosState_[k];
                float eps = epsilon_[k];
                float R = std::pow(radius_[k], decayScale);
                float gain = inputGain_[k];

                float s_new = R * (s + eps * c) + gain * ex;
                float c_new = R * (c - eps * s_new);

                sinState_[k] = s_new;
                cosState_[k] = c_new;
                output += s_new;
            }
        } else {
            for (int k = 0; k < numModes_; ++k) {
                float s = sinState_[k];
                float c = cosState_[k];
                float eps = epsilon_[k];
                float R = radius_[k];
                float gain = inputGain_[k];

                // Gordon-Smith coupled-form resonator (FR-003)
                float s_new = R * (s + eps * c) + gain * ex;
                float c_new = R * (c - eps * s_new);

                sinState_[k] = s_new;
                cosState_[k] = c_new;
                output += s_new;
            }
        }

        // Soft-clip safety limiter (FR-010)
        output = softClip(output / kSoftClipThreshold) * kSoftClipThreshold;

        return output;
    }

    /// Transient emphasis: continuous proportional boost (FR-022).
    float applyTransientEmphasis(float sample) noexcept
    {
        // One-pole envelope follower (~5ms attack)
        envelopeState_ = envelopeAttackCoeff_ * envelopeState_
                       + (1.0f - envelopeAttackCoeff_) * std::abs(sample);

        // Compute derivative
        float derivative = envelopeState_ - previousEnvelope_;

        // Continuous proportional boost (NOT binary on/off)
        // kTransientEmphasisGain may be promoted to a user-facing parameter in a future phase
        float emphasis = 1.0f + kTransientEmphasisGain * std::max(0.0f, derivative);

        previousEnvelope_ = envelopeState_;

        return sample * emphasis;
    }
};

} // namespace DSP
} // namespace Krate
