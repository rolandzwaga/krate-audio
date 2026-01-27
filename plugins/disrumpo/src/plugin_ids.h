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

    // ==========================================================================
    // Reserved ID Ranges (for future implementation)
    // ==========================================================================
    // 0x0F00-0x0FFF: Global parameters (256 available)
    // 0x0E00-0x0EFF: Sweep parameters (256 available)
    // 0xF000-0xFFFF: Band-level parameters (node=0xF)
    // 0x0000-0x7FFF: Per-node parameters (8 bands x 4 nodes)
};

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
