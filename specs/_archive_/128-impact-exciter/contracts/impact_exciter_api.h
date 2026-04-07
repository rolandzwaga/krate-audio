// Contract: ImpactExciter public API
// Location: dsp/include/krate/dsp/processors/impact_exciter.h
// Namespace: Krate::DSP
// Layer: 2 (processors)

#pragma once

#include <krate/dsp/core/xorshift32.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/primitives/delay_line.h>

namespace Krate::DSP {

class ImpactExciter {
public:
    ImpactExciter() noexcept = default;
    ~ImpactExciter() = default;

    // Non-copyable, movable
    ImpactExciter(const ImpactExciter&) = delete;
    ImpactExciter& operator=(const ImpactExciter&) = delete;
    ImpactExciter(ImpactExciter&&) noexcept = default;
    ImpactExciter& operator=(ImpactExciter&&) noexcept = default;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Allocate internal buffers for the given sample rate.
    /// Must be called before trigger() or process().
    /// @param sampleRate Sample rate in Hz
    /// @param voiceId Unique voice identifier for RNG seeding
    void prepare(double sampleRate, uint32_t voiceId) noexcept;

    /// Clear all internal state (pulse, noise, SVF, comb).
    void reset() noexcept;

    // =========================================================================
    // Triggering
    // =========================================================================

    /// Trigger a new impact excitation burst.
    /// Called on note-on. Does NOT reset resonator state.
    /// @param velocity     Normalized MIDI velocity [0.0, 1.0]
    /// @param hardness     Hardness parameter [0.0, 1.0]
    /// @param mass         Mass parameter [0.0, 1.0]
    /// @param brightness   Brightness trim [-1.0, +1.0]
    /// @param position     Strike position [0.0, 1.0]
    /// @param f0           Fundamental frequency in Hz (for comb filter)
    void trigger(float velocity, float hardness, float mass,
                 float brightness, float position, float f0) noexcept;

    // =========================================================================
    // Processing (FR-001)
    // =========================================================================

    /// Generate one sample of excitation signal.
    /// Primary API -- called per audio frame by the voice loop.
    /// @return Excitation sample (broadband signal for feeding ModalResonatorBank)
    [[nodiscard]] float process() noexcept;

    /// Generate a block of excitation samples.
    /// Convenience wrapper that loops over process().
    /// @param output Output buffer (must have at least numSamples capacity)
    /// @param numSamples Number of samples to generate
    void processBlock(float* output, int numSamples) noexcept;

    // =========================================================================
    // Queries
    // =========================================================================

    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] bool isActive() const noexcept;
};

} // namespace Krate::DSP
