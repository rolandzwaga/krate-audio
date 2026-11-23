// ==============================================================================
// Ruinae Plugin - Type Definitions
// ==============================================================================
// Enumerations specific to the Ruinae plugin: filter types, distortion types,
// delay types, and oscillator mix modes.
//
// These types have no DSP-layer consumers and belong at the plugin level.
// ==============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// MixMode Enumeration
// =============================================================================

/// @brief Mixer mode selection for dual-oscillator blending.
enum class MixMode : uint8_t {
    CrossfadeMix = 0,     ///< Linear crossfade: oscA*(1-pos) + oscB*pos
    SpectralMorph         ///< FFT-based spectral interpolation
};

// =============================================================================
// RuinaeFilterType Enumeration
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
    SVF_Allpass,          ///< State Variable Filter - Allpass (phase shift)
    SVF_Peak,             ///< State Variable Filter - Peak (parametric EQ bell)
    SVF_LowShelf,         ///< State Variable Filter - Low Shelf (boost/cut below cutoff)
    SVF_HighShelf,        ///< State Variable Filter - High Shelf (boost/cut above cutoff)
    EnvelopeFilter,       ///< Auto-wah (input amplitude drives cutoff)
    SelfOscillating,      ///< Melodic filter ping (ladder self-oscillation)
    NumTypes              ///< Sentinel: total number of filter types
};

// =============================================================================
// RuinaeDistortionType Enumeration
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
    RingModulator,        ///< Ring modulation with internal carrier (Layer 2)
    NumTypes              ///< Sentinel: total number of distortion types
};

// =============================================================================
// RuinaeDelayType Enumeration
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

/// Spectral delay FFT sizes in Delay FFT Size dropdown order.
inline constexpr std::array<std::size_t, 4> kSpectralDelayFFTSizes = {512, 1024, 2048, 4096};

/// The spectral delay FFT size for a Delay FFT Size dropdown index; an
/// out-of-range index clamps to the nearest end.
[[nodiscard]] inline std::size_t fftSizeFromIndex(int index) noexcept {
    return kSpectralDelayFFTSizes[static_cast<std::size_t>(
        std::clamp(index, 0, static_cast<int>(kSpectralDelayFFTSizes.size()) - 1))];
}

// =============================================================================
// RuinaeModDest Enumeration (FR-020)
// =============================================================================

/// @brief Internal enum for global modulation destinations within the
///        RuinaeEngine. Values start at 64 to avoid collision with
///        per-voice modulation destination IDs.
enum class RuinaeModDest : std::uint8_t {
    GlobalFilterCutoff     = 64,  ///< Global filter cutoff frequency offset
    GlobalFilterResonance  = 65,  ///< Global filter resonance offset
    MasterVolume           = 66,  ///< Master volume offset
    EffectMix              = 67,  ///< Effect chain mix offset (delay mix)
    AllVoiceFilterCutoff   = 68,  ///< Offset forwarded to all voices' filter cutoff
    AllVoiceMorphPosition  = 69,  ///< Offset forwarded to all voices' morph position
    AllVoiceTranceGateRate = 70,  ///< Offset forwarded to all voices' trance gate rate
    AllVoiceSpectralTilt   = 71,  ///< Offset forwarded to all voices' spectral tilt
    AllVoiceResonance      = 72,  ///< Offset forwarded to all voices' filter resonance
    AllVoiceFilterEnvAmt   = 73,  ///< Offset forwarded to all voices' filter env amount
    // Arpeggiator modulation destinations (078-modulation-integration)
    ArpRate                = 74,  ///< Arp rate/speed modulation
    ArpGateLength          = 75,  ///< Arp gate length modulation
    ArpOctaveRange         = 76,  ///< Arp octave range modulation
    ArpSwing               = 77,  ///< Arp swing modulation
    ArpSpice               = 78   ///< Arp spice amount modulation
};

// FR-020: Validate linear mapping from UI index to enum value.
// modDestFromIndex() computes GlobalFilterCutoff + index, so ArpRate must be
// exactly GlobalFilterCutoff + 10 (since ArpRate is at UI index 10).
static_assert(static_cast<uint32_t>(RuinaeModDest::ArpRate) ==
              static_cast<uint32_t>(RuinaeModDest::GlobalFilterCutoff) + 10,
              "ArpRate enum value must equal GlobalFilterCutoff + 10 "
              "for modDestFromIndex() to work correctly");

} // namespace Krate::DSP
