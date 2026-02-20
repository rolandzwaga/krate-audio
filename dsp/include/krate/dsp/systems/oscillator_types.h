// ==============================================================================
// Layer 3: System Component - Oscillator Type Definitions
// ==============================================================================
// Enumerations for oscillator type selection and phase behavior,
// used by SelectableOscillator and related systems.
//
// Layer: 3 (Systems)
// Dependencies: None (pure enums)
// ==============================================================================

#pragma once

#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// OscType Enumeration
// =============================================================================

/// @brief Oscillator type selection for SelectableOscillator slots.
///
/// Each type maps to a specific oscillator implementation in the variant.
/// PolyBLEP is the default type used when a voice is first prepared.
enum class OscType : uint8_t {
    PolyBLEP = 0,        ///< Band-limited subtractive (Layer 1)
    Wavetable,            ///< Mipmapped wavetable (Layer 1)
    PhaseDistortion,      ///< Phase distortion synthesis (Layer 2)
    Sync,                 ///< Hard-sync dual oscillator (Layer 2)
    Additive,             ///< Additive harmonic synthesis (Layer 2)
    Chaos,                ///< Chaos attractor oscillator (Layer 2)
    Particle,             ///< Particle swarm oscillator (Layer 2)
    Formant,              ///< Formant/vocal synthesis (Layer 2)
    SpectralFreeze,       ///< Spectral freeze oscillator (Layer 2)
    Noise,                ///< Multi-color noise (Layer 1)
    NumTypes              ///< Sentinel: total number of types (= 10)
};

// =============================================================================
// PhaseMode Enumeration
// =============================================================================

/// @brief Oscillator phase behavior on type switch.
enum class PhaseMode : uint8_t {
    Reset = 0,            ///< Reset phase to 0 on type switch (default)
    Continuous            ///< Attempt to preserve phase across type switch
};

// =============================================================================
// OscParam Enumeration
// =============================================================================

/// @brief Type-specific oscillator parameter identifiers.
///
/// Used with OscillatorSlot::setParam() to set type-specific parameters
/// without adding per-parameter virtual methods to the interface. Groups
/// are spaced by 10 to allow future per-type parameter additions without
/// renumbering.
///
/// @note Values are DSP-domain (not normalized VST values).
/// @note Adapters silently ignore OscParam values they don't recognize.
enum class OscParam : uint16_t {
    // PolyBLEP (Waveform/PulseWidth unique to PolyBLEP;
    // PhaseModulation/FrequencyModulation shared with Wavetable)
    Waveform = 0,
    PulseWidth,
    PhaseModulation,
    FrequencyModulation,

    // Phase Distortion
    PDWaveform = 10,
    PDDistortion,

    // Sync
    SyncSlaveRatio = 20,
    SyncSlaveWaveform,
    SyncMode,
    SyncAmount,
    SyncSlavePulseWidth,

    // Additive
    AdditiveNumPartials = 30,
    AdditiveSpectralTilt,
    AdditiveInharmonicity,

    // Chaos
    ChaosAttractor = 40,
    ChaosAmount,
    ChaosCoupling,
    ChaosOutput,

    // Particle
    ParticleScatter = 50,
    ParticleDensity,
    ParticleLifetime,
    ParticleSpawnMode,
    ParticleEnvType,
    ParticleDrift,

    // Formant
    FormantVowel = 60,
    FormantMorph,

    // Spectral Freeze
    SpectralPitchShift = 70,
    SpectralTilt,
    SpectralFormantShift,

    // Noise
    NoiseColor = 80,
};

} // namespace Krate::DSP
