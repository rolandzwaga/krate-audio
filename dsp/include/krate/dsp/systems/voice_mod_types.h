// ==============================================================================
// Layer 3: System Component - Voice Modulation Type Definitions
// ==============================================================================
// Enumerations and structures for per-voice modulation routing,
// used by VoiceModRouter and related systems.
//
// Layer: 3 (Systems)
// Dependencies: None (pure enums and POD struct)
// ==============================================================================

#pragma once

#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// VoiceModSource Enumeration
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
    Aftertouch,           ///< Channel aftertouch [0, 1]
    NumSources            ///< Sentinel: total number of sources (= 8)
};

// =============================================================================
// VoiceModDest Enumeration
// =============================================================================

/// @brief Per-voice modulation destinations.
///
/// Offset interpretation:
/// - FilterCutoff, OscAPitch, OscBPitch: semitones
/// - FilterResonance, MorphPosition, DistortionDrive, TranceGateDepth, SpectralTilt: linear
enum class VoiceModDest : uint8_t {
    FilterCutoff = 0,     ///< Filter cutoff (semitone offset)
    FilterResonance,      ///< Filter resonance (linear offset)
    MorphPosition,        ///< OSC mix/morph position (linear offset)
    DistortionDrive,      ///< Distortion drive (linear offset)
    TranceGateDepth,      ///< TranceGate depth (linear offset)
    OscAPitch,            ///< OSC A pitch (semitone offset)
    OscBPitch,            ///< OSC B pitch (semitone offset)
    OscALevel,            ///< OSC A level offset
    OscBLevel,            ///< OSC B level offset
    SpectralTilt,         ///< Spectral tilt offset (dB/octave, linear offset)
    NumDestinations       ///< Sentinel: total number of destinations (= 10)
};

// =============================================================================
// VoiceModRoute Structure
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
