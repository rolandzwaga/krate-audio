// ==============================================================================
// API Contract: WaveguideString
// ==============================================================================
// Digital waveguide string resonator with EKS extensions.
// Two-segment delay loop with dispersion allpass cascade,
// Thiran fractional delay, weighted one-zero loss filter,
// and DC blocker. Velocity wave convention for Phase 4 bow readiness.
//
// Layer 2 (processors) | Namespace: Krate::DSP
// Spec: 129-waveguide-string-resonance
// ==============================================================================

#pragma once

#include <krate/dsp/processors/iresonator.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/xorshift32.h>

#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

class WaveguideString : public IResonator {
public:
    // =========================================================================
    // Constants
    // =========================================================================
    static constexpr int kMaxDispersionSections = 4;
    static constexpr size_t kMinDelaySamples = 4;
    static constexpr float kDefaultPickPosition = 0.13f;
    static constexpr float kSoftClipThreshold = 1.0f;
    static constexpr float kEnergyFloor = 1e-20f;
    static constexpr float kDcBlockerCutoffHz = 3.5f;
    static constexpr float kMinFrequency = 20.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================
    WaveguideString() noexcept = default;
    ~WaveguideString() override = default;

    // Non-copyable, movable
    WaveguideString(const WaveguideString&) = delete;
    WaveguideString& operator=(const WaveguideString&) = delete;
    WaveguideString(WaveguideString&&) noexcept = default;
    WaveguideString& operator=(WaveguideString&&) noexcept = default;

    /// Prepare for processing. Allocates delay lines for 20 Hz minimum.
    void prepare(double sampleRate) noexcept override;

    /// Seed per-voice RNG. Call once after prepare().
    void prepareVoice(uint32_t voiceId) noexcept;

    // =========================================================================
    // IResonator Interface
    // =========================================================================
    void setFrequency(float f0) noexcept override;
    void setDecay(float t60) noexcept override;
    void setBrightness(float brightness) noexcept override;
    [[nodiscard]] float process(float excitation) noexcept override;
    [[nodiscard]] float getControlEnergy() const noexcept override;
    [[nodiscard]] float getPerceptualEnergy() const noexcept override;
    void silence() noexcept override;
    [[nodiscard]] float getFeedbackVelocity() const noexcept override;

    // =========================================================================
    // Type-Specific Setters
    // =========================================================================

    /// Set string stiffness (inharmonicity). Frozen at note onset.
    /// @param stiffness 0.0 = flexible string, 1.0 = maximum inharmonicity
    void setStiffness(float stiffness) noexcept;

    /// Set pick/interaction position. Frozen at note onset.
    /// @param position Normalised position [0.0, 1.0], default 0.13
    void setPickPosition(float position) noexcept;

    // =========================================================================
    // Note Lifecycle (called by voice engine)
    // =========================================================================

    /// Trigger a new note. Freezes stiffness and pick position,
    /// computes delay lengths, fills excitation, resets loop state.
    /// @param f0 Fundamental frequency in Hz
    /// @param velocity MIDI velocity [0.0, 1.0]
    void noteOn(float f0, float velocity) noexcept;

private:
    // Delay segments (FR-002)
    DelayLine nutSideDelay_;       // segment A: nut-side
    DelayLine bridgeSideDelay_;    // segment B: bridge-side

    // Loop filters
    float lossState_ = 0.0f;      // x_prev for one-zero loss filter
    float lossRho_ = 0.999f;      // frequency-independent loss
    float lossS_ = 0.25f;         // brightness (spectral tilt)
    Biquad dispersionFilters_[kMaxDispersionSections]; // allpass cascade (FR-009)
    float thiranState_ = 0.0f;    // Thiran allpass state
    float thiranEta_ = 0.0f;      // Thiran allpass coefficient
    DCBlocker dcBlocker_;          // in-loop (FR-008)

    // Smoothers
    OnePoleSmoother frequencySmoother_;
    OnePoleSmoother decaySmoother_;
    OnePoleSmoother brightnessSmoother_;

    // Energy followers (FR-023)
    float controlEnergy_ = 0.0f;
    float perceptualEnergy_ = 0.0f;
    float controlAlpha_ = 0.0f;
    float perceptualAlpha_ = 0.0f;

    // Frozen parameters
    float pickPosition_ = kDefaultPickPosition;
    float stiffness_ = 0.0f;
    float frozenPickPosition_ = kDefaultPickPosition;
    float frozenStiffness_ = 0.0f;

    // Delay lengths (computed at note onset)
    size_t nutDelaySamples_ = 0;
    size_t bridgeDelaySamples_ = 0;
    float totalLoopDelay_ = 0.0f;

    // Excitation
    XorShift32 rng_;
    float excitationGain_ = 1.0f;

    // Velocity wave state (FR-013)
    float feedbackVelocity_ = 0.0f;

    // Runtime
    double sampleRate_ = 44100.0;
    float frequency_ = 440.0f;
    float decayTime_ = 0.5f;
    float brightness_ = 0.5f;
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
