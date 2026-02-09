// ==============================================================================
// Layer 3: System Component - Ruinae Voice Type Definitions
// ==============================================================================
// Enumerations and type aliases for the Ruinae chaos/spectral hybrid
// synthesizer voice architecture.
//
// Feature: 041-ruinae-voice-architecture
// Layer: 3 (Systems)
// Dependencies: None (pure enums and POD struct)
//
// Constitution Compliance:
// - Principle III: Modern C++ (C++20, scoped enums)
// - Principle IX: Layer 3 (no dependencies beyond stdlib)
// - Principle XIV: ODR Prevention (unique enum/struct names)
//
// Reference: specs/041-ruinae-voice-architecture/spec.md
// ==============================================================================

#pragma once

#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// OscType Enumeration (FR-001)
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
// MixMode Enumeration (FR-006)
// =============================================================================

/// @brief Mixer mode selection for dual-oscillator blending.
enum class MixMode : uint8_t {
    CrossfadeMix = 0,     ///< Linear crossfade: oscA*(1-pos) + oscB*pos
    SpectralMorph         ///< FFT-based spectral interpolation
};

// =============================================================================
// PhaseMode Enumeration (FR-005)
// =============================================================================

/// @brief Oscillator phase behavior on type switch.
enum class PhaseMode : uint8_t {
    Reset = 0,            ///< Reset phase to 0 on type switch (default)
    Continuous            ///< Attempt to preserve phase across type switch
};

// =============================================================================
// RuinaeFilterType Enumeration (FR-010)
// =============================================================================

/// @brief Voice filter type selection.
///
/// SVF modes are collapsed into separate enum values since each SVF mode
/// has distinct frequency response characteristics.
enum class RuinaeFilterType : uint8_t {
    SVF_LP = 0,           ///< State Variable Filter - Lowpass (12 dB/oct)
    SVF_HP,               ///< State Variable Filter - Highpass
    SVF_BP,               ///< State Variable Filter - Bandpass
    SVF_Notch,            ///< State Variable Filter - Notch
    Ladder,               ///< Moog-style ladder (24 dB/oct)
    Formant,              ///< Vowel/formant filter
    Comb,                 ///< Feedback comb filter (metallic)
    NumTypes              ///< Sentinel: total number of filter types
};

// =============================================================================
// RuinaeDistortionType Enumeration (FR-013)
// =============================================================================

/// @brief Voice distortion type selection.
///
/// Clean mode uses std::monostate in the variant (true bypass).
enum class RuinaeDistortionType : uint8_t {
    Clean = 0,            ///< No distortion (bypass)
    ChaosWaveshaper,      ///< Lorenz-driven waveshaping (Layer 1)
    SpectralDistortion,   ///< FFT-based spectral distortion (Layer 2)
    GranularDistortion,   ///< Granular micro-distortion (Layer 2)
    Wavefolder,           ///< Wavefolder with multiple stages (Layer 1)
    TapeSaturator,        ///< Tape saturation emulation (Layer 2)
    NumTypes              ///< Sentinel: total number of distortion types
};

// =============================================================================
// RuinaeDelayType Enumeration (043-effects-section FR-007, FR-008)
// =============================================================================

/// @brief Delay type selection for the Ruinae effects chain.
///
/// Each type maps to a specific delay effect implementation in the chain:
/// - Digital: Clean digital delay (DigitalDelay, Layer 4)
/// - Tape: Tape echo emulation (TapeDelay, Layer 4)
/// - PingPong: Alternating L/R delay (PingPongDelay, Layer 4)
/// - Granular: Grain-based delay (GranularDelay, Layer 4)
/// - Spectral: FFT per-bin delay (SpectralDelay, Layer 4)
enum class RuinaeDelayType : uint8_t {
    Digital = 0,    ///< DigitalDelay (pristine, 80s, lo-fi)
    Tape = 1,       ///< TapeDelay (motor inertia, heads, wear)
    PingPong = 2,   ///< PingPongDelay (alternating L/R)
    Granular = 3,   ///< GranularDelay (grain-based)
    Spectral = 4,   ///< SpectralDelay (FFT per-bin)
    NumTypes = 5    ///< Sentinel: total number of delay types
};

// =============================================================================
// VoiceModSource Enumeration (FR-025)
// =============================================================================

/// @brief Per-voice modulation sources.
///
/// Source value ranges:
/// - Env1/2/3: [0, 1] (envelope output)
/// - VoiceLFO: [-1, +1] (bipolar LFO)
/// - GateOutput: [0, 1] (TranceGate smoothed value)
/// - Velocity: [0, 1] (constant per note)
/// - KeyTrack: [-1, +1] ((midiNote - 60) / 60)
enum class VoiceModSource : uint8_t {
    Env1 = 0,             ///< Amplitude envelope (ENV 1)
    Env2,                 ///< Filter envelope (ENV 2)
    Env3,                 ///< General modulation envelope (ENV 3)
    VoiceLFO,             ///< Per-voice LFO
    GateOutput,           ///< TranceGate envelope value
    Velocity,             ///< Note velocity (constant per note)
    KeyTrack,             ///< Key tracking relative to C4
    Aftertouch,           ///< Channel aftertouch [0, 1] (FR-001, 042-ext-modulation-system)
    NumSources            ///< Sentinel: total number of sources (= 8)
};

// =============================================================================
// VoiceModDest Enumeration (FR-026)
// =============================================================================

/// @brief Per-voice modulation destinations.
///
/// Offset interpretation:
/// - FilterCutoff, OscAPitch, OscBPitch: semitones
/// - FilterResonance, MorphPosition, DistortionDrive, TranceGateDepth: linear
enum class VoiceModDest : uint8_t {
    FilterCutoff = 0,     ///< Filter cutoff (semitone offset)
    FilterResonance,      ///< Filter resonance (linear offset)
    MorphPosition,        ///< OSC mix/morph position (linear offset)
    DistortionDrive,      ///< Distortion drive (linear offset)
    TranceGateDepth,      ///< TranceGate depth (linear offset)
    OscAPitch,            ///< OSC A pitch (semitone offset)
    OscBPitch,            ///< OSC B pitch (semitone offset)
    OscALevel,            ///< OSC A level offset (FR-002, 042-ext-modulation-system)
    OscBLevel,            ///< OSC B level offset (FR-002, 042-ext-modulation-system)
    NumDestinations       ///< Sentinel: total number of destinations (= 9)
};

// =============================================================================
// VoiceModRoute Structure (FR-024)
// =============================================================================

/// @brief A single modulation route connecting a source to a destination.
///
/// Amount is bipolar [-1, +1] and is multiplied by the source value.
/// For semitone destinations, the result is in semitones.
/// For linear destinations, the result is in normalized units.
struct VoiceModRoute {
    VoiceModSource source{VoiceModSource::Env1};
    VoiceModDest destination{VoiceModDest::FilterCutoff};
    float amount{0.0f};  ///< Bipolar: [-1.0, +1.0]
};

} // namespace Krate::DSP
