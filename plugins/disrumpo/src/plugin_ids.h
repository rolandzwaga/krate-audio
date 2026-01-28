#pragma once

// ==============================================================================
// Plugin Identifiers
// ==============================================================================
// These GUIDs uniquely identify the plugin components.
//
// IMPORTANT: Once published, NEVER change these IDs or hosts will not
// recognize saved projects using your plugin.
//
// Parameter ID Encoding (per specs/Disrumpo/dsp-details.md):
// - Global parameters: 0x0Fxx (band = 0xF, node = 0x0)
// - Sweep parameters: 0x0Exx (band = 0xE, node = 0x0)
// - Per-band parameters: makeBandParamId(bandIndex, paramType) -> 0xFbpp
// - Per-node parameters: makeNodeParamId(bandIndex, nodeIndex, paramType) -> 0xNbpp
// ==============================================================================

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Disrumpo {

// Processor Component ID (FR-001)
// The audio processing component (runs on audio thread)
// UUID generated specifically for Disrumpo (unique from Iterum)
static const Steinberg::FUID kProcessorUID(0xA1B2C3D4, 0xE5F67890, 0x12345678, 0x9ABCDEF0);

// Controller Component ID (FR-001)
// The edit controller component (runs on UI thread)
// UUID generated specifically for Disrumpo (unique from Iterum)
static const Steinberg::FUID kControllerUID(0xF0E1D2C3, 0xB4A59687, 0x78563412, 0xEFCDAB90);

// ==============================================================================
// Parameter ID Encoding (FR-002, FR-003)
// ==============================================================================
// Disrumpo uses bit-encoded parameter IDs (per dsp-details.md):
//
// Bit Layout (16-bit ParamID):
// +--------+--------+--------+
// | 15..12 | 11..8  |  7..0  |
// |  node  |  band  | param  |
// +--------+--------+--------+
//
// Special Bands:
// - 0xF = Global parameters (node nibble = 0x0)
// - 0xE = Sweep parameters (node nibble = 0x0)
// - 0x0-0x7 = Per-band and per-node parameters
//
// Band-level parameters have node nibble = 0xF (makeBandParamId)
// Node-level parameters have node nibble = 0-3 (makeNodeParamId)
//
// Constitution Principle V: All parameter values MUST be normalized (0.0 to 1.0)
// ==============================================================================

// =============================================================================
// Global Parameter Type Enum (FR-002)
// =============================================================================
// Global parameters use 0x0Fxx encoding (band = 0xF, node = 0x0)
// =============================================================================

enum class GlobalParamType : uint8_t {
    kGlobalInputGain   = 0x00,  ///< Input gain [-24, +24] dB
    kGlobalOutputGain  = 0x01,  ///< Output gain [-24, +24] dB
    kGlobalMix         = 0x02,  ///< Global dry/wet mix [0, 100] %
    kGlobalBandCount   = 0x03,  ///< Band count [1-8]
    kGlobalOversample  = 0x04,  ///< Max oversample [1x, 2x, 4x, 8x]
};

/// @brief Create parameter ID for global parameters.
/// @param param GlobalParamType enum value
/// @return Encoded parameter ID in 0x0F00 range
constexpr Steinberg::Vst::ParamID makeGlobalParamId(GlobalParamType param) {
    return 0x0F00 | static_cast<Steinberg::Vst::ParamID>(param);
}

/// @brief Check if a parameter ID is a global parameter.
/// @param paramId Parameter ID to check
/// @return true if this is a global parameter (0x0F00-0x0FFF range)
constexpr bool isGlobalParamId(Steinberg::Vst::ParamID paramId) {
    return (paramId & 0xFF00) == 0x0F00;
}

// =============================================================================
// Sweep Parameter Type Enum (FR-002)
// =============================================================================
// Sweep parameters use 0x0Exx encoding (band = 0xE, node = 0x0)
// =============================================================================

enum class SweepParamType : uint8_t {
    kSweepEnable       = 0x00,  ///< Enable sweep [on/off]
    kSweepFrequency    = 0x01,  ///< Sweep frequency [20, 20000] Hz, log scale
    kSweepWidth        = 0x02,  ///< Sweep width [0.5, 4.0] octaves
    kSweepIntensity    = 0x03,  ///< Sweep intensity [0, 100] %
    kSweepMorphLink    = 0x04,  ///< Sweep-to-morph link mode
    kSweepFalloff      = 0x05,  ///< Sweep falloff [Hard, Soft]
};

/// @brief Create parameter ID for sweep parameters.
/// @param param SweepParamType enum value
/// @return Encoded parameter ID in 0x0E00 range
constexpr Steinberg::Vst::ParamID makeSweepParamId(SweepParamType param) {
    return 0x0E00 | static_cast<Steinberg::Vst::ParamID>(param);
}

/// @brief Check if a parameter ID is a sweep parameter.
/// @param paramId Parameter ID to check
/// @return true if this is a sweep parameter (0x0E00-0x0EFF range)
constexpr bool isSweepParamId(Steinberg::Vst::ParamID paramId) {
    return (paramId & 0xFF00) == 0x0E00;
}

// =============================================================================
// Band Parameter Type Enum (FR-002)
// =============================================================================
// Per-band parameter encoding: makeBandParamId(bandIndex, paramType)
// Encoding: (0xF << 12) | (band << 8) | param -> 0xFbpp
// NOTE: BandParamType values match dsp-details.md exactly
// =============================================================================

enum class BandParamType : uint8_t {
    kBandGain      = 0x00,  ///< Band gain in dB [-24, +24]
    kBandPan       = 0x01,  ///< Band pan [-1, +1]
    kBandSolo      = 0x02,  ///< Band solo flag
    kBandBypass    = 0x03,  ///< Band bypass flag
    kBandMute      = 0x04,  ///< Band mute flag
    // 0x05-0x07 reserved
    kBandMorphX    = 0x08,  ///< Morph X position [0, 1]
    kBandMorphY    = 0x09,  ///< Morph Y position [0, 1]
    kBandMorphMode = 0x0A,  ///< Morph mode [1D Linear, 2D Planar, 2D Radial]
};

/// @brief Create parameter ID for per-band parameters.
/// @param band Band index (0-7)
/// @param param BandParamType enum value
/// @return Encoded parameter ID
/// @example makeBandParamId(0, BandParamType::kBandGain) = 0xF000 = 61440
/// @example makeBandParamId(3, BandParamType::kBandGain) = 0xF300 = 62208
/// @example makeBandParamId(0, BandParamType::kBandMorphX) = 0xF008 = 61448
constexpr Steinberg::Vst::ParamID makeBandParamId(uint8_t band, BandParamType param) {
    return static_cast<Steinberg::Vst::ParamID>(
        (0xF << 12) | (static_cast<uint32_t>(band) << 8) | static_cast<uint32_t>(param)
    );
}

/// @brief Extract band index from a band parameter ID.
/// @param paramId Parameter ID to decode
/// @return Band index (0-7)
constexpr uint8_t extractBandIndex(Steinberg::Vst::ParamID paramId) {
    return static_cast<uint8_t>((paramId >> 8) & 0x0F);
}

/// @brief Extract parameter type from a band parameter ID.
/// @param paramId Parameter ID to decode
/// @return BandParamType enum value
constexpr BandParamType extractBandParamType(Steinberg::Vst::ParamID paramId) {
    return static_cast<BandParamType>(paramId & 0xFF);
}

/// @brief Check if a parameter ID is a band-level parameter.
/// @param paramId Parameter ID to check
/// @return true if this is a band-level parameter (node nibble = 0xF)
constexpr bool isBandParamId(Steinberg::Vst::ParamID paramId) {
    return ((paramId >> 12) & 0x0F) == 0x0F;
}

// =============================================================================
// Node Parameter Type Enum (FR-002)
// =============================================================================
// Per-node parameter encoding: makeNodeParamId(bandIndex, nodeIndex, paramType)
// Encoding: (node << 12) | (band << 8) | param -> 0xNbpp
// NOTE: Node nibble is 0-3, Band nibble is 0-7
// =============================================================================

enum class NodeParamType : uint8_t {
    kNodeType     = 0x00,  ///< Distortion type (26 types)
    kNodeDrive    = 0x01,  ///< Drive amount [0, 10]
    kNodeMix      = 0x02,  ///< Wet/dry mix [0, 100] %
    kNodeTone     = 0x03,  ///< Tone filter frequency [200, 8000] Hz
    kNodeBias     = 0x04,  ///< Bias offset [-1, +1]
    kNodeFolds    = 0x05,  ///< Fold count [1, 12] (for wavefolders)
    kNodeBitDepth = 0x06,  ///< Bit depth [4, 24] (for bitcrushers)
    // 0x07-0x08 reserved for future MorphPad node positioning (deferred to spec 005)
};

/// @brief Create parameter ID for per-node parameters.
/// @param band Band index (0-7)
/// @param node Node index (0-3)
/// @param param NodeParamType enum value
/// @return Encoded parameter ID
/// @example makeNodeParamId(0, 0, NodeParamType::kNodeType) = 0x0000 = 0
/// @example makeNodeParamId(1, 2, NodeParamType::kNodeDrive) = 0x2101 = 8449
/// @example makeNodeParamId(7, 3, NodeParamType::kNodeType) = 0x3700 = 14080
constexpr Steinberg::Vst::ParamID makeNodeParamId(uint8_t band, uint8_t node, NodeParamType param) {
    return static_cast<Steinberg::Vst::ParamID>(
        (static_cast<uint32_t>(node) << 12) | (static_cast<uint32_t>(band) << 8) | static_cast<uint32_t>(param)
    );
}

/// @brief Extract node index from a node parameter ID.
/// @param paramId Parameter ID to decode
/// @return Node index (0-3)
constexpr uint8_t extractNode(Steinberg::Vst::ParamID paramId) {
    return static_cast<uint8_t>((paramId >> 12) & 0x0F);
}

/// @brief Extract band index from a node parameter ID.
/// @param paramId Parameter ID to decode
/// @return Band index (0-7)
constexpr uint8_t extractBandFromNodeParam(Steinberg::Vst::ParamID paramId) {
    return static_cast<uint8_t>((paramId >> 8) & 0x0F);
}

/// @brief Extract parameter type from a node parameter ID.
/// @param paramId Parameter ID to decode
/// @return NodeParamType enum value
constexpr NodeParamType extractNodeParamType(Steinberg::Vst::ParamID paramId) {
    return static_cast<NodeParamType>(paramId & 0xFF);
}

/// @brief Check if a parameter ID is a node-level parameter.
/// @param paramId Parameter ID to check
/// @return true if this is a node-level parameter (node nibble = 0-3, band nibble = 0-7)
constexpr bool isNodeParamId(Steinberg::Vst::ParamID paramId) {
    // Exclude global (0x0Fxx), sweep (0x0Exx), and modulation (0x0Dxx) ranges
    // These are in the format 0x0Xpp where X is D, E, or F
    uint16_t highByte = (paramId >> 8) & 0xFF;
    if (highByte == 0x0F || highByte == 0x0E || highByte == 0x0D) {
        return false;
    }

    // Exclude band-level parameters (node nibble = 0xF)
    uint8_t nodeNibble = (paramId >> 12) & 0x0F;
    if (nodeNibble == 0x0F) {
        return false;
    }

    // Node parameters have node nibble 0-3 and band nibble 0-7
    uint8_t bandNibble = (paramId >> 8) & 0x0F;
    return nodeNibble <= 3 && bandNibble <= 7;
}

// =============================================================================
// Legacy/Compatibility Aliases
// =============================================================================
// These constants maintain backward compatibility with existing code

enum ParameterIDs : Steinberg::Vst::ParamID {
    // Global Parameters (0x0Fxx)
    kInputGainId     = 0x0F00,  // 3840 - Input gain control
    kOutputGainId    = 0x0F01,  // 3841 - Output gain control
    kGlobalMixId     = 0x0F02,  // 3842 - Global dry/wet mix
    kBandCountId     = 0x0F03,  // 3843 - Band count (1-8)
    kOversampleMaxId = 0x0F04,  // 3844 - Max oversample factor
};

// =============================================================================
// Crossover Parameter IDs
// =============================================================================
// Crossover frequency parameters use global space: 0x0F10 + index
// =============================================================================

/// @brief Crossover parameter base ID (0x0F10 - 0x0F16 for 7 crossovers)
constexpr Steinberg::Vst::ParamID kCrossoverParamBase = 0x0F10;

/// @brief Create parameter ID for crossover frequency parameters.
/// @param index Crossover index (0-6, for up to 7 crossovers in 8-band config)
/// @return Encoded parameter ID
constexpr Steinberg::Vst::ParamID makeCrossoverParamId(uint8_t index) {
    return kCrossoverParamBase + static_cast<Steinberg::Vst::ParamID>(index);
}

/// @brief Check if a parameter ID is a crossover frequency parameter.
/// @param paramId Parameter ID to check
/// @return true if this is a crossover parameter
constexpr bool isCrossoverParamId(Steinberg::Vst::ParamID paramId) {
    return paramId >= kCrossoverParamBase && paramId < (kCrossoverParamBase + 7);
}

/// @brief Extract crossover index from a crossover parameter ID.
/// @param paramId Parameter ID to decode
/// @return Crossover index (0-6)
constexpr uint8_t extractCrossoverIndex(Steinberg::Vst::ParamID paramId) {
    return static_cast<uint8_t>(paramId - kCrossoverParamBase);
}

// ==============================================================================
// Modulation Parameter ID Range (FR-002)
// ==============================================================================
// Reserved range for modulation source and routing parameters
// These will be populated in the modulation spec (Week 9)
// ==============================================================================

constexpr Steinberg::Vst::ParamID kModulationParamBase = 0x0D00;
constexpr Steinberg::Vst::ParamID kModulationParamEnd  = 0x0DFF;

/// @brief Check if a parameter ID is a modulation parameter.
/// @param paramId Parameter ID to check
/// @return true if this is a modulation parameter
constexpr bool isModulationParamId(Steinberg::Vst::ParamID paramId) {
    return paramId >= kModulationParamBase && paramId <= kModulationParamEnd;
}

// ==============================================================================
// Morph Link Mode Enum (FR-032, FR-033)
// ==============================================================================
// Defines how morph X/Y axes link to sweep frequency.
// Used by Band*MorphXLink and Band*MorphYLink parameters.
// ==============================================================================

enum class MorphLinkMode : uint8_t {
    None = 0,       ///< Manual control only, no link to sweep
    SweepFreq,      ///< Linear mapping: low freq = 0, high freq = 1
    InverseSweep,   ///< Inverted: high freq = 0, low freq = 1
    EaseIn,         ///< Exponential curve emphasizing low frequencies
    EaseOut,        ///< Exponential curve emphasizing high frequencies
    HoldRise,       ///< Hold at 0 until mid-point, then rise to 1
    Stepped,        ///< Quantize to discrete steps (0, 0.25, 0.5, 0.75, 1.0)
    COUNT           ///< Sentinel for iteration (7 modes)
};

/// @brief Total number of morph link modes.
constexpr int kMorphLinkModeCount = static_cast<int>(MorphLinkMode::COUNT);

/// @brief Get display name for a morph link mode.
/// @param mode The morph link mode
/// @return C-string display name
constexpr const char* getMorphLinkModeName(MorphLinkMode mode) noexcept {
    switch (mode) {
        case MorphLinkMode::None:         return "None";
        case MorphLinkMode::SweepFreq:    return "Sweep Freq";
        case MorphLinkMode::InverseSweep: return "Inverse Sweep";
        case MorphLinkMode::EaseIn:       return "Ease In";
        case MorphLinkMode::EaseOut:      return "Ease Out";
        case MorphLinkMode::HoldRise:     return "Hold-Rise";
        case MorphLinkMode::Stepped:      return "Stepped";
        default:                          return "Unknown";
    }
}

// ==============================================================================
// State Versioning
// ==============================================================================
// Version field for preset migration. Always serialize this as first int32.
// Increment when adding parameters to ensure backward compatibility.
//
// Version History:
// - v1: Initial skeleton (inputGain, outputGain, globalMix)
// - v2: Band management (bandCount, 8x bandState, 7x crossoverFreq)
// - v3: VSTGUI infrastructure (all ~450 parameters)
// ==============================================================================
constexpr int32_t kPresetVersion = 3;

// ==============================================================================
// Plugin Metadata
// ==============================================================================
// Note: Vendor info (company name, URL, email, copyright) is defined in
// version.h.in which CMake uses to generate version.h
// ==============================================================================

// VST3 Sub-categories (see VST3 SDK documentation for full list)
constexpr const char* kSubCategories = "Fx|Distortion";

} // namespace Disrumpo
