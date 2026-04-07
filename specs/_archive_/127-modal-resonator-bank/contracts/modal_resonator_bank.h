// ==============================================================================
// API Contract: ModalResonatorBank
// ==============================================================================
// Layer 2 Processor | Namespace: Krate::DSP
// Location: dsp/include/krate/dsp/processors/modal_resonator_bank.h
//
// Bank of up to 96 parallel damped coupled-form resonators for modal synthesis.
// Uses SoA memory layout with 32-byte alignment for SIMD processing via
// Google Highway.
// ==============================================================================

#pragma once

#include <cstddef>

namespace Krate {
namespace DSP {

class ModalResonatorBank {
public:
    static constexpr int kMaxModes = 96;

    ModalResonatorBank() noexcept;
    ~ModalResonatorBank() = default;

    // Non-copyable (large aligned arrays), movable
    ModalResonatorBank(const ModalResonatorBank&) = delete;
    ModalResonatorBank& operator=(const ModalResonatorBank&) = delete;
    ModalResonatorBank(ModalResonatorBank&&) noexcept = default;
    ModalResonatorBank& operator=(ModalResonatorBank&&) noexcept = default;

    /// Prepare for processing at the given sample rate.
    /// Pre-computes smoothing coefficients and envelope attack coefficient.
    void prepare(double sampleRate) noexcept;

    /// Reset all filter states and envelope state to zero.
    /// Does NOT reset mode configuration.
    void reset() noexcept;

    /// Configure all modes from analyzed harmonic data.
    /// Clears filter states (for note-on). Applies inharmonic warping,
    /// frequency-dependent damping, Nyquist guard, and amplitude culling.
    ///
    /// @param frequencies   Array of partial frequencies in Hz
    /// @param amplitudes    Array of linear partial amplitudes
    /// @param numPartials   Number of partials [0, kMaxModes]
    /// @param decayTime     Base decay time in seconds [0.01, 5.0]
    /// @param brightness    HF damping control [0.0, 1.0] (0=wood, 1=metal)
    /// @param stretch       Stiff-string inharmonicity [0.0, 1.0]
    /// @param scatter       Irregular mode displacement [0.0, 1.0]
    void setModes(
        const float* frequencies,
        const float* amplitudes,
        int numPartials,
        float decayTime,
        float brightness,
        float stretch,
        float scatter
    ) noexcept;

    /// Update mode coefficients without clearing filter states.
    /// Used during frame transitions in sample playback mode.
    /// Smoothing targets are updated; per-sample interpolation prevents clicks.
    void updateModes(
        const float* frequencies,
        const float* amplitudes,
        int numPartials,
        float decayTime,
        float brightness,
        float stretch,
        float scatter
    ) noexcept;

    /// Process a single sample through the resonator bank.
    /// Applies transient emphasis to excitation, processes all active modes,
    /// sums their outputs, and returns the result.
    ///
    /// @param excitation   Input excitation sample (typically residual signal)
    /// @return             Sum of all active mode outputs
    [[nodiscard]] float processSample(float excitation) noexcept;

    /// Process a block of samples.
    /// Calls flushSilentModes() once per block for denormal protection.
    void processBlock(const float* input, float* output, int numSamples) noexcept;

    /// Check mode energy and zero out states below silence threshold.
    /// Called once per audio block (not per sample).
    void flushSilentModes() noexcept;

    /// @return Number of active (non-culled) modes
    [[nodiscard]] int getNumActiveModes() const noexcept;

    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // Voicing constants (may be promoted to parameters in future phases)
    static constexpr float kTransientEmphasisGain = 4.0f;
    static constexpr float kMaxB3 = 4.0e-5f;
    static constexpr float kSilenceThreshold = 1e-12f;
    static constexpr float kNyquistGuard = 0.49f;
    static constexpr float kAmplitudeThresholdLinear = 0.0001f; // -80 dB
    static constexpr float kSmoothingTimeMs = 2.0f;
    static constexpr float kEnvelopeAttackMs = 5.0f;
    static constexpr float kSoftClipThreshold = 0.707f; // -3 dBFS

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

    // Internal helpers
    void computeModeCoefficients(
        const float* frequencies,
        const float* amplitudes,
        int numPartials,
        float decayTime,
        float brightness,
        float stretch,
        float scatter,
        bool snapSmoothing
    ) noexcept;

    float applyTransientEmphasis(float sample) noexcept;
};

} // namespace DSP
} // namespace Krate
