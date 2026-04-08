#pragma once

// ==============================================================================
// Plugin Identifiers -- Membrum (Synthesized Drum Machine)
// ==============================================================================
// GUIDs uniquely identify the plugin components.
// IMPORTANT: Once published, NEVER change these IDs or hosts will not
// recognize saved projects using your plugin.
// ==============================================================================

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Membrum {

// Processor Component ID
static const Steinberg::FUID kProcessorUID(0x4D656D62, 0x72756D50, 0x726F6331, 0x00000136);

// Controller Component ID
static const Steinberg::FUID kControllerUID(0x4D656D62, 0x72756D43, 0x74726C31, 0x00000136);

// VST3 subcategories: Instrument|Drum
static constexpr auto kSubCategories = "Instrument|Drum";

// State version for serialization
constexpr Steinberg::int32 kCurrentStateVersion = 1;

// ==============================================================================
// Parameter IDs
// ==============================================================================
// Phase 1 parameter range: 100-199
// Phase 2+ will use higher ranges for per-pad parameters.
// ==============================================================================

enum ParameterIds : Steinberg::Vst::ParamID
{
    kMaterialId       = 100,  // 0.0 woody -- 1.0 metallic
    kSizeId           = 101,  // 0.0 small(500Hz) -- 1.0 large(50Hz)
    kDecayId          = 102,  // 0.0 short -- 1.0 long
    kStrikePositionId = 103,  // 0.0 center -- 1.0 edge
    kLevelId          = 104,  // 0.0 silent -- 1.0 full
};

} // namespace Membrum
