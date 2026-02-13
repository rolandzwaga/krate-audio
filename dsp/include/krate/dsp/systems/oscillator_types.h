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

} // namespace Krate::DSP
