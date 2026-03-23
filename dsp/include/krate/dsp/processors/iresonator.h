// ==============================================================================
// API Contract: IResonator Interface
// ==============================================================================
// Shared interface for interchangeable resonator types in Innexus.
// Phase 3: ModalResonatorBank + WaveguideString
// Phase 4+: BowJunction coupling, Body Resonance wrapper
//
// Layer 2 (processors) | Namespace: Krate::DSP
// Spec: 129-waveguide-string-resonance (FR-020 through FR-025)
// ==============================================================================

#pragma once

namespace Krate {
namespace DSP {

/// @brief Shared interface for interchangeable resonator types.
///
/// Both ModalResonatorBank and WaveguideString conform to this interface,
/// enabling the voice engine to use either type through a common API.
///
/// @par Design Decisions (FR-021, FR-022):
/// - No noteOn/noteOff: voice engine owns note lifecycle
/// - No setParameter(int, float): named setters preserve type safety
/// - Energy at output tap: automatic perceptual scale (FR-024)
/// - getFeedbackVelocity() for Phase 4 bow coupling readiness
///
/// @par Energy Model (FR-023):
/// Two separate EMA followers:
/// - Control energy (tau ~5ms): fast, for internal choke/gate decisions
/// - Perceptual energy (tau ~30ms): slow, for crossfade gain matching
class IResonator {
public:
    virtual ~IResonator() = default;

    /// Prepare for processing at the given sample rate.
    /// Allocates internal buffers. Call before any process().
    virtual void prepare(double sampleRate) noexcept = 0;

    /// Set the fundamental frequency.
    /// @param f0 Frequency in Hz, clamped to [20, sampleRate*0.45]
    virtual void setFrequency(float f0) noexcept = 0;

    /// Set the decay time (T60).
    /// @param t60 Decay time in seconds
    virtual void setDecay(float t60) noexcept = 0;

    /// Set the brightness / spectral tilt.
    /// @param brightness 0.0 = flat decay, 1.0 = maximum HF damping
    virtual void setBrightness(float brightness) noexcept = 0;

    /// Process one sample of excitation through the resonator.
    /// @param excitation Input excitation signal
    /// @return Resonator output (velocity for waveguide, displacement for modal)
    [[nodiscard]] virtual float process(float excitation) noexcept = 0;

    /// Get fast energy follower (tau ~5ms).
    /// Used for internal decisions (choke, gate).
    [[nodiscard]] virtual float getControlEnergy() const noexcept = 0;

    /// Get slow energy follower (tau ~30ms).
    /// Used for crossfade gain matching.
    [[nodiscard]] virtual float getPerceptualEnergy() const noexcept = 0;

    /// Clear all internal state, including energy followers.
    /// Called on voice steal before reassignment.
    virtual void silence() noexcept = 0;

    /// Get feedback velocity at the interaction point.
    /// Phase 3: returns 0.0f (no bow coupling).
    /// Phase 4: waveguide returns velocity wave sum at bow position.
    [[nodiscard]] virtual float getFeedbackVelocity() const noexcept { return 0.0f; }
};

} // namespace DSP
} // namespace Krate
