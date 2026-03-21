// ==============================================================================
// API Contract: PhysicalModelMixer
// ==============================================================================
// Innexus Plugin-Local DSP | Namespace: Innexus
// Location: plugins/innexus/src/dsp/physical_model_mixer.h
//
// Stateless crossfader that blends the existing additive path
// (harmonic + residual) with the physical modelling path
// (harmonic + modal resonator output).
// ==============================================================================

#pragma once

namespace Innexus {

/// Blends the existing signal path with the physical model output.
///
/// Formula:
///   dry  = harmonicSignal + residualSignal   (current behavior)
///   wet  = harmonicSignal + physicalSignal   (physical replaces residual)
///   out  = dry * (1 - mix) + wet * mix
///
/// Simplifies to:
///   out  = harmonicSignal + (1 - mix) * residualSignal + mix * physicalSignal
///
/// At mix=0: output = harmonic + residual (bit-exact current behavior)
/// At mix=1: output = harmonic + physical (full modal replacement)
struct PhysicalModelMixer
{
    /// Mix the three signal components.
    ///
    /// @param harmonicSignal  Output from HarmonicOscillatorBank (passes through unchanged)
    /// @param residualSignal  Output from ResidualSynthesizer (scaled by residualLevel)
    /// @param physicalSignal  Output from ModalResonatorBank (after soft clip)
    /// @param mix             Physical Model Mix parameter [0.0, 1.0]
    /// @return                Blended output
    [[nodiscard]] static float process(
        float harmonicSignal,
        float residualSignal,
        float physicalSignal,
        float mix
    ) noexcept
    {
        return harmonicSignal
             + (1.0f - mix) * residualSignal
             + mix * physicalSignal;
    }
};

} // namespace Innexus
