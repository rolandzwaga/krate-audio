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
// - 0x0-0x3 = Per-band and per-node parameters
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
    kGlobalInputGain       = 0x00,  ///< Input gain [-24, +24] dB
    kGlobalOutputGain      = 0x01,  ///< Output gain [-24, +24] dB
    kGlobalMix             = 0x02,  ///< Global dry/wet mix [0, 100] %
    kGlobalBandCount       = 0x03,  ///< Band count [1-4]
    kGlobalOversample      = 0x04,  ///< Max oversample [1x, 2x, 4x, 8x]
    kGlobalModPanelVisible = 0x06,  ///< Modulation panel visibility [on/off] (Spec 012)
    kGlobalMidiLearnActive = 0x07,  ///< MIDI Learn mode active [on/off] (Spec 012)
    kGlobalMidiLearnTarget = 0x08,  ///< MIDI Learn target parameter ID (Spec 012)
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

    // Sweep LFO parameters (FR-024, FR-025)
    kSweepLFOEnable    = 0x10,  ///< Enable internal LFO [on/off]
    kSweepLFORate      = 0x11,  ///< LFO rate [0.01, 20] Hz
    kSweepLFOWaveform  = 0x12,  ///< LFO waveform [Sine, Triangle, Saw, Square, S&H, Random]
    kSweepLFODepth     = 0x13,  ///< LFO depth [0, 100] %
    kSweepLFOSync      = 0x14,  ///< LFO tempo sync [on/off]
    kSweepLFONoteValue = 0x15,  ///< LFO note value (when tempo sync enabled)

    // Sweep Envelope Follower parameters (FR-026, FR-027)
    kSweepEnvEnable    = 0x20,  ///< Enable envelope follower [on/off]
    kSweepEnvAttack    = 0x21,  ///< Envelope attack [1, 100] ms
    kSweepEnvRelease   = 0x22,  ///< Envelope release [10, 500] ms
    kSweepEnvSensitivity = 0x23, ///< Envelope sensitivity [0, 100] %

    // Custom Curve parameters (FR-039a, FR-039b, FR-039c)
    kSweepCustomCurvePointCount = 0x30,  ///< Number of breakpoints [2-8]
    kSweepCustomCurveP0X = 0x31,  ///< Point 0 X (always 0.0)
    kSweepCustomCurveP0Y = 0x32,  ///< Point 0 Y [0, 1]
    kSweepCustomCurveP1X = 0x33,  ///< Point 1 X [0, 1]
    kSweepCustomCurveP1Y = 0x34,  ///< Point 1 Y [0, 1]
    kSweepCustomCurveP2X = 0x35,  ///< Point 2 X [0, 1]
    kSweepCustomCurveP2Y = 0x36,  ///< Point 2 Y [0, 1]
    kSweepCustomCurveP3X = 0x37,  ///< Point 3 X [0, 1]
    kSweepCustomCurveP3Y = 0x38,  ///< Point 3 Y [0, 1]
    kSweepCustomCurveP4X = 0x39,  ///< Point 4 X [0, 1]
    kSweepCustomCurveP4Y = 0x3A,  ///< Point 4 Y [0, 1]
    kSweepCustomCurveP5X = 0x3B,  ///< Point 5 X [0, 1]
    kSweepCustomCurveP5Y = 0x3C,  ///< Point 5 Y [0, 1]
    kSweepCustomCurveP6X = 0x3D,  ///< Point 6 X [0, 1]
    kSweepCustomCurveP6Y = 0x3E,  ///< Point 6 Y [0, 1]
    kSweepCustomCurveP7X = 0x3F,  ///< Point 7 X (always 1.0)
    kSweepCustomCurveP7Y = 0x40,  ///< Point 7 Y [0, 1]

    // MIDI parameters (FR-028, FR-029)
    kSweepMidiLearnActive = 0x50,  ///< MIDI Learn toggle [on/off]
    kSweepMidiCCNumber    = 0x51,  ///< Assigned MIDI CC number [0-128], 128 = none
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
// Output Parameter IDs (Processor -> Controller)
// =============================================================================
// Output parameters use standalone IDs outside the encoding scheme.
// These are read-only parameters written by the Processor and observed
// by the Controller for real-time UI updates.
// =============================================================================

/// @brief Output parameter: modulated sweep frequency (normalized [0,1])
/// Written by Processor in process() after computing modulated frequency.
/// Observed by Controller to update SweepIndicator and SpectrumDisplay.
constexpr Steinberg::Vst::ParamID kSweepModulatedFrequencyOutputId = 0x0F80;

/// @brief Output parameter: detected MIDI CC number during MIDI Learn (normalized)
/// Written by Processor when a CC event is detected while MIDI Learn is active.
constexpr Steinberg::Vst::ParamID kSweepDetectedCCOutputId = 0x0F81;

// =============================================================================
// Band Parameter Type Enum (FR-002)
// =============================================================================
// Per-band parameter encoding: makeBandParamId(bandIndex, paramType)
// Encoding: (0xF << 12) | (band << 8) | param -> 0xFbpp
// NOTE: BandParamType values match dsp-details.md exactly
// =============================================================================

enum class BandParamType : uint8_t {
    kBandGain        = 0x00,  ///< Band gain in dB [-24, +24]
    kBandPan         = 0x01,  ///< Band pan [-1, +1]
    kBandSolo        = 0x02,  ///< Band solo flag
    kBandBypass      = 0x03,  ///< Band bypass flag
    kBandMute        = 0x04,  ///< Band mute flag
    kBandExpanded    = 0x05,  ///< Band expanded state (UI only) [0=collapsed, 1=expanded]
    kBandActiveNodes = 0x06,  ///< Active nodes count [2, 3, 4] (US6)
    kBandMorphSmoothing = 0x07,  ///< Morph smoothing time [0, 500] ms (FR-031)
    kBandMorphX      = 0x08,  ///< Morph X position [0, 1]
    kBandMorphY      = 0x09,  ///< Morph Y position [0, 1]
    kBandMorphMode   = 0x0A,  ///< Morph mode [1D Linear, 2D Planar, 2D Radial]
    kBandMorphXLink  = 0x0B,  ///< Morph X Link mode (US8 FR-032)
    kBandMorphYLink  = 0x0C,  ///< Morph Y Link mode (US8 FR-033)
    kBandSelectedNode = 0x0D, ///< Selected node for editing (0-3) (US7 FR-025)
    kBandDisplayedType = 0x0E, ///< Proxy type for UIViewSwitchContainer (mirrors selected node's type)
};

/// @brief Create parameter ID for per-band parameters.
/// @param band Band index (0-3)
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
/// @return Band index (0-3)
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
// NOTE: Node nibble is 0-3, Band nibble is 0-3
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
/// @param band Band index (0-3)
/// @param node Node index (0-3)
/// @param param NodeParamType enum value
/// @return Encoded parameter ID
/// @example makeNodeParamId(0, 0, NodeParamType::kNodeType) = 0x0000 = 0
/// @example makeNodeParamId(1, 2, NodeParamType::kNodeDrive) = 0x2101 = 8449
/// @example makeNodeParamId(3, 3, NodeParamType::kNodeType) = 0x3300 = 13056
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
/// @return Band index (0-3)
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

    // Node parameters have node nibble 0-3 and band nibble 0-3
    uint8_t bandNibble = (paramId >> 8) & 0x0F;
    return nodeNibble <= 3 && bandNibble <= 3;
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

/// @brief Crossover parameter base ID (0x0F10 - 0x0F12 for 3 crossovers)
constexpr Steinberg::Vst::ParamID kCrossoverParamBase = 0x0F10;

/// @brief Create parameter ID for crossover frequency parameters.
/// @param index Crossover index (0-2, for up to 3 crossovers in 4-band config)
/// @return Encoded parameter ID
constexpr Steinberg::Vst::ParamID makeCrossoverParamId(uint8_t index) {
    return kCrossoverParamBase + static_cast<Steinberg::Vst::ParamID>(index);
}

/// @brief Check if a parameter ID is a crossover frequency parameter.
/// @param paramId Parameter ID to check
/// @return true if this is a crossover parameter
constexpr bool isCrossoverParamId(Steinberg::Vst::ParamID paramId) {
    return paramId >= kCrossoverParamBase && paramId < (kCrossoverParamBase + 3);
}

/// @brief Extract crossover index from a crossover parameter ID.
/// @param paramId Parameter ID to decode
/// @return Crossover index (0-2)
constexpr uint8_t extractCrossoverIndex(Steinberg::Vst::ParamID paramId) {
    return static_cast<uint8_t>(paramId - kCrossoverParamBase);
}

// ==============================================================================
// Modulation Parameter ID Range (spec 008-modulation-system)
// ==============================================================================
// Modulation source and routing parameters use 0x0D00-0x0DFF range.
//
// Layout:
// - 0x0D00-0x0D06: LFO 1 parameters
// - 0x0D10-0x0D16: LFO 2 parameters
// - 0x0D20-0x0D23: Envelope Follower parameters
// - 0x0D30-0x0D32: Random source parameters
// - 0x0D38-0x0D3A: Chaos source parameters
// - 0x0D40-0x0D42: Sample & Hold parameters
// - 0x0D48-0x0D4B: Pitch Follower parameters
// - 0x0D50-0x0D52: Transient Detector parameters
// - 0x0D60-0x0D6F: Macro parameters (4 macros x 4 params)
// - 0x0D80-0x0DFF: Routing parameters (32 routings x 4 params)
// ==============================================================================

constexpr Steinberg::Vst::ParamID kModulationParamBase = 0x0D00;
constexpr Steinberg::Vst::ParamID kModulationParamEnd  = 0x0DFF;

/// @brief Check if a parameter ID is a modulation parameter.
/// @param paramId Parameter ID to check
/// @return true if this is a modulation parameter
constexpr bool isModulationParamId(Steinberg::Vst::ParamID paramId) {
    return paramId >= kModulationParamBase && paramId <= kModulationParamEnd;
}

// =============================================================================
// Modulation Source Parameter Type Enum
// =============================================================================

enum class ModParamType : uint8_t {
    // LFO 1 (0x00-0x06)
    kLFO1Rate       = 0x00,  ///< LFO 1 rate [0.01, 20] Hz
    kLFO1Shape      = 0x01,  ///< LFO 1 waveform [Sine, Triangle, Saw, Square, S&H, SmoothRandom]
    kLFO1Phase      = 0x02,  ///< LFO 1 phase offset [0, 360] degrees
    kLFO1Sync       = 0x03,  ///< LFO 1 tempo sync [on/off]
    kLFO1NoteValue  = 0x04,  ///< LFO 1 note value (when synced)
    kLFO1Unipolar   = 0x05,  ///< LFO 1 unipolar mode [on/off]
    kLFO1Retrigger  = 0x06,  ///< LFO 1 retrigger on transport start [on/off]

    // LFO 2 (0x10-0x16)
    kLFO2Rate       = 0x10,  ///< LFO 2 rate [0.01, 20] Hz
    kLFO2Shape      = 0x11,  ///< LFO 2 waveform
    kLFO2Phase      = 0x12,  ///< LFO 2 phase offset [0, 360] degrees
    kLFO2Sync       = 0x13,  ///< LFO 2 tempo sync [on/off]
    kLFO2NoteValue  = 0x14,  ///< LFO 2 note value (when synced)
    kLFO2Unipolar   = 0x15,  ///< LFO 2 unipolar mode [on/off]
    kLFO2Retrigger  = 0x16,  ///< LFO 2 retrigger on transport start [on/off]

    // Envelope Follower (0x20-0x23)
    kEnvFollowerAttack      = 0x20,  ///< Attack time [1, 100] ms
    kEnvFollowerRelease     = 0x21,  ///< Release time [10, 500] ms
    kEnvFollowerSensitivity = 0x22,  ///< Sensitivity [0, 100] %
    kEnvFollowerSource      = 0x23,  ///< Source type [InputL, InputR, Sum, Mid, Side]

    // Random (0x30-0x32)
    kRandomRate       = 0x30,  ///< Rate [0.1, 50] Hz
    kRandomSmoothness = 0x31,  ///< Smoothness [0, 100] %
    kRandomSync       = 0x32,  ///< Tempo sync [on/off]

    // Chaos (0x38-0x3A)
    kChaosModel    = 0x38,  ///< Model [Lorenz, Rossler, Chua, Henon]
    kChaosSpeed    = 0x39,  ///< Speed [0.05, 20.0]
    kChaosCoupling = 0x3A,  ///< Coupling [0, 1.0]

    // Sample & Hold (0x40-0x42)
    kSampleHoldSource = 0x40,  ///< Input source [Random, LFO1, LFO2, External]
    kSampleHoldRate   = 0x41,  ///< Rate [0.1, 50] Hz
    kSampleHoldSlew   = 0x42,  ///< Slew time [0, 500] ms

    // Pitch Follower (0x48-0x4B)
    kPitchFollowerMinHz         = 0x48,  ///< Min Hz [20, 500]
    kPitchFollowerMaxHz         = 0x49,  ///< Max Hz [200, 5000]
    kPitchFollowerConfidence    = 0x4A,  ///< Confidence threshold [0, 1.0]
    kPitchFollowerTrackingSpeed = 0x4B,  ///< Tracking speed [10, 300] ms

    // Transient Detector (0x50-0x52)
    kTransientSensitivity = 0x50,  ///< Sensitivity [0, 1.0]
    kTransientAttack      = 0x51,  ///< Attack time [0.5, 10] ms
    kTransientDecay       = 0x52,  ///< Decay time [20, 200] ms

    // Macros (0x60-0x6F: 4 macros x 4 params each)
    kMacro1Value = 0x60,  ///< Macro 1 value [0, 1]
    kMacro1Min   = 0x61,  ///< Macro 1 min output [0, 1]
    kMacro1Max   = 0x62,  ///< Macro 1 max output [0, 1]
    kMacro1Curve = 0x63,  ///< Macro 1 curve [Linear, Exp, S-Curve, Stepped]
    kMacro2Value = 0x64,
    kMacro2Min   = 0x65,
    kMacro2Max   = 0x66,
    kMacro2Curve = 0x67,
    kMacro3Value = 0x68,
    kMacro3Min   = 0x69,
    kMacro3Max   = 0x6A,
    kMacro3Curve = 0x6B,
    kMacro4Value = 0x6C,
    kMacro4Min   = 0x6D,
    kMacro4Max   = 0x6E,
    kMacro4Curve = 0x6F,
};

/// @brief Create parameter ID for modulation parameters.
/// @param param ModParamType enum value
/// @return Encoded parameter ID in 0x0D00 range
constexpr Steinberg::Vst::ParamID makeModParamId(ModParamType param) {
    return kModulationParamBase | static_cast<Steinberg::Vst::ParamID>(param);
}

// =============================================================================
// Routing Parameter Encoding
// =============================================================================
// 32 routings x 4 params each = 128 IDs
// Base: 0x0D80 + routingIndex * 4 + offset
// Offset 0 = Source, 1 = Dest, 2 = Amount, 3 = Curve
// =============================================================================

constexpr Steinberg::Vst::ParamID kRoutingParamBase = 0x0D80;

/// @brief Create parameter ID for routing parameters.
/// @param routingIndex Routing slot index (0-31)
/// @param offset Parameter offset: 0=Source, 1=Dest, 2=Amount, 3=Curve
/// @return Encoded parameter ID
constexpr Steinberg::Vst::ParamID makeRoutingParamId(uint8_t routingIndex, uint8_t offset) {
    return kRoutingParamBase + static_cast<Steinberg::Vst::ParamID>(routingIndex) * 4 + offset;
}

/// @brief Check if a parameter ID is a routing parameter.
/// @param paramId Parameter ID to check
/// @return true if this is a routing parameter
constexpr bool isRoutingParamId(Steinberg::Vst::ParamID paramId) {
    return paramId >= kRoutingParamBase && paramId < (kRoutingParamBase + 128);
}

/// @brief Extract routing index from a routing parameter ID.
/// @param paramId Parameter ID to decode
/// @return Routing index (0-31)
constexpr uint8_t extractRoutingIndex(Steinberg::Vst::ParamID paramId) {
    return static_cast<uint8_t>((paramId - kRoutingParamBase) / 4);
}

/// @brief Extract routing parameter offset from a routing parameter ID.
/// @param paramId Parameter ID to decode
/// @return Parameter offset (0=Source, 1=Dest, 2=Amount, 3=Curve)
constexpr uint8_t extractRoutingOffset(Steinberg::Vst::ParamID paramId) {
    return static_cast<uint8_t>((paramId - kRoutingParamBase) % 4);
}

// ==============================================================================
// Modulation Destination Index Mapping (FR-063, FR-064)
// ==============================================================================
// Maps modulation routing destination IDs (0-127) to actual Disrumpo parameters.
// The ModulationEngine uses modOffsets_[kMaxModDestinations=128] internally.
// These indices are what the routing destParamId field holds.
//
// Layout:
// - 0-2: Global parameters (InputGain, OutputGain, GlobalMix)
// - 3-5: Sweep parameters (Frequency, Width, Intensity)
// - 6-29: Per-band parameters (4 bands × 6 params each)
//
// Per-band params at offset (6 + band*6 + param):
//   +0=MorphX, +1=MorphY, +2=Drive, +3=Mix, +4=BandGain, +5=BandPan
// ==============================================================================

namespace ModDest {

// Global destinations
inline constexpr uint32_t kInputGain     = 0;
inline constexpr uint32_t kOutputGain    = 1;
inline constexpr uint32_t kGlobalMix     = 2;

// Sweep destinations
inline constexpr uint32_t kSweepFrequency = 3;
inline constexpr uint32_t kSweepWidth     = 4;
inline constexpr uint32_t kSweepIntensity = 5;

// Per-band destination base
inline constexpr uint32_t kBandBase       = 6;
inline constexpr uint32_t kParamsPerBand  = 6;

// Per-band parameter offsets within a band block
inline constexpr uint32_t kBandMorphX  = 0;
inline constexpr uint32_t kBandMorphY  = 1;
inline constexpr uint32_t kBandDrive   = 2;
inline constexpr uint32_t kBandMix     = 3;
inline constexpr uint32_t kBandGain    = 4;
inline constexpr uint32_t kBandPan     = 5;

// Total modulation destinations: 6 global/sweep + 4 bands × 6 params = 30
inline constexpr uint32_t kTotalDestinations = kBandBase + 4 * kParamsPerBand;

/// @brief Get modulation destination index for a per-band parameter.
/// @param band Band index (0-3)
/// @param paramOffset One of kBandMorphX..kBandPan (0-5)
/// @return Destination index for use with ModulationEngine
constexpr uint32_t bandParam(uint8_t band, uint32_t paramOffset) {
    return kBandBase + static_cast<uint32_t>(band) * kParamsPerBand + paramOffset;
}

}  // namespace ModDest

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
    Custom,         ///< User-defined breakpoint curve (007-sweep-system)
    COUNT           ///< Sentinel for iteration (8 modes)
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
        case MorphLinkMode::Custom:       return "Custom";
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
// - v4: Sweep system state (sweep params, LFO, envelope, custom curve)
// - v5: Modulation system (source params, routing params, macros)
// - v6: Morph node state (per-band morph position, mode, node params)
// - v7: Progressive disclosure (window size, MIDI CC mappings, mod panel visibility)
// - v8: Reduced max bands from 8 to 4 (stream format: 4 bands, 3 crossovers, 4 morph)
// ==============================================================================
constexpr int32_t kPresetVersion = 8;

// ==============================================================================
// Plugin Metadata
// ==============================================================================
// Note: Vendor info (company name, URL, email, copyright) is defined in
// version.h.in which CMake uses to generate version.h
// ==============================================================================

// VST3 Sub-categories (see VST3 SDK documentation for full list)
constexpr const char* kSubCategories = "Fx|Distortion";

} // namespace Disrumpo
