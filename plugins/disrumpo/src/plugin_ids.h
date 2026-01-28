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
// - Global parameters: 0x0Fxx (band = 0xF reserved for global)
// - Sweep parameters: 0x0Exx (band = 0xE reserved for sweep)
// - Per-band parameters: makeBandParamId(bandIndex, paramType)
// - Per-node parameters: makeNodeParamId(bandIndex, nodeIndex, paramType)
// ==============================================================================

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Disrumpo {

// Processor Component ID
// The audio processing component (runs on audio thread)
// UUID generated specifically for Disrumpo (unique from Iterum)
static const Steinberg::FUID kProcessorUID(0xA1B2C3D4, 0xE5F67890, 0x12345678, 0x9ABCDEF0);

// Controller Component ID
// The edit controller component (runs on UI thread)
// UUID generated specifically for Disrumpo (unique from Iterum)
static const Steinberg::FUID kControllerUID(0xF0E1D2C3, 0xB4A59687, 0x78563412, 0xEFCDAB90);

// ==============================================================================
// Parameter IDs
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
// - 0xF = Global parameters (this skeleton)
// - 0xE = Sweep parameters (future)
// - 0x0-0x7 = Per-band parameters (future)
//
// Constitution Principle V: All parameter values MUST be normalized (0.0 to 1.0)
// ==============================================================================

enum ParameterIDs : Steinberg::Vst::ParamID {
    // ==========================================================================
    // Global Parameters (0x0Fxx) - Skeleton Parameters
    // ==========================================================================
    kInputGainId  = 0x0F00,  // 3840 - Input gain control
    kOutputGainId = 0x0F01,  // 3841 - Output gain control
    kGlobalMixId  = 0x0F02,  // 3842 - Global dry/wet mix
    kBandCountId  = 0x0F03,  // 3843 - Band count (1-8) per FR-008

    // ==========================================================================
    // Reserved ID Ranges (for future implementation)
    // ==========================================================================
    // 0x0F00-0x0FFF: Global parameters (256 available)
    // 0x0E00-0x0EFF: Sweep parameters (256 available)
    // 0xF000-0xFFFF: Band-level parameters (node=0xF)
    // 0x0000-0x7FFF: Per-node parameters (8 bands x 4 nodes)
};

// =============================================================================
// Band Parameter Types (T005)
// =============================================================================
// Per-band parameter encoding: makeBandParamId(bandIndex, paramType)
// Encoding: (0xF << 12) | (band << 8) | param
// =============================================================================

enum class BandParamType : uint8_t {
    kBandGain   = 0x00,  ///< Band gain in dB [-24, +24]
    kBandPan    = 0x01,  ///< Band pan [-1, +1]
    kBandSolo   = 0x02,  ///< Band solo flag
    kBandBypass = 0x03,  ///< Band bypass flag
    kBandMute   = 0x04,  ///< Band mute flag
};

/// @brief Create parameter ID for per-band parameters.
/// @param band Band index (0-7)
/// @param param BandParamType enum value
/// @return Encoded parameter ID
/// @example makeBandParamId(0, BandParamType::kBandGain) = 0xF000
/// @example makeBandParamId(1, BandParamType::kBandPan) = 0xF101
/// @example makeBandParamId(7, BandParamType::kBandMute) = 0xF704
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
// Crossover Parameter Types (T007)
// =============================================================================
// Crossover frequency parameter encoding: makeCrossoverParamId(crossoverIndex)
// Uses global parameter space: 0x0F10 + index
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
// State Versioning
// ==============================================================================
// Version field for preset migration. Always serialize this as first int32.
// Increment when adding parameters to ensure backward compatibility.
// ==============================================================================
constexpr int32_t kPresetVersion = 1;

// ==============================================================================
// Plugin Metadata
// ==============================================================================
// Note: Vendor info (company name, URL, email, copyright) is defined in
// version.h.in which CMake uses to generate version.h
// ==============================================================================

// VST3 Sub-categories (see VST3 SDK documentation for full list)
// FR-008: Plugin subcategory MUST be "Distortion"
constexpr const char* kSubCategories = "Fx|Distortion";

} // namespace Disrumpo
