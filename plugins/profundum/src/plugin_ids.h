#pragma once

// ==============================================================================
// Profundum - Plugin Identifiers and Parameter IDs (FR-011 - FR-014)   Plan section 2.1
// ==============================================================================
// These GUIDs uniquely identify the plugin components.
//
// IMPORTANT: Once published, NEVER change these IDs or hosts will not
// recognize saved projects using your plugin.
// ==============================================================================

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <cstddef>

namespace Profundum {

/// FR-012. Shared by processor and controller; neither includes the other.
constexpr Steinberg::int32 kCurrentStateVersion = 1;

/// FR-044: version (4) + global block (4 + 4 + 4 + 4) + macro block (4 x 4).
constexpr std::size_t kStateV1Bytes = 36;
static_assert(kStateV1Bytes == 4 + (4 + 4 + 4 + 4) + 4 * 4, "spec FR-044 byte layout (plan 3.4)");

/// FR-011. Fresh v4 GUIDs generated 2026-10-22 with Node crypto.randomUUID(); compared
/// against the sixteen registered FUIDs (spec New components): no match. NEVER change.
static const Steinberg::FUID kProcessorUID(0x94C64960, 0xF7E844BE, 0xB439A0F3, 0x95AF84C9);
static const Steinberg::FUID kControllerUID(0x449C9192, 0xE39F4DA0, 0xAD6E7BCA, 0x260F4CA0);

/// FR-014. DEF_CLASS2 subcategory string; instrument.
/// Deliberately `const`, NOT `constexpr` (cross-platform rule: anything
/// initialized from / handed to an SDK constant is `const`). The pointer itself
/// is also `const` so the unused-in-this-TU case falls under
/// `-Wunused-const-variable` (off by default in C++) rather than
/// `-Wunused-variable`, which GCC 13 emits for a mutable namespace-scope static
/// in every TU that includes this header without using it.
/// (Rationale copied from plugins/vorago/src/plugin_ids.h:65-72.)
static const char* const kSubCategories = "Instrument|Synth";

// ==============================================================================
// Parameter IDs
// ==============================================================================
// All parameter values at the VST boundary are normalized (0.0 to 1.0).
//
/// FR-013. Reserved band map (roadmap lines 762-765):
///   0-99 Global | 100-199 Macros + Mass + Morph position | 200-299 Core | 300-399 Anchor
///   400-499 Drop / note | 500-599 Fold / Shape | 600-699 Character | 700-799 Mass dynamics
///   800-899 Stereo field | 900-999 Motion | 1000-1099 Morph control. Phase 11 owns 104+ and 200+.
enum ParameterIDs : Steinberg::Vst::ParamID {
    kOutputGainId = 0,
    kVoiceModeId = 1,
    kPolyphonyId = 2,
    kOutputCeilingId = 3,
    kGlobalParamRangeEnd = 100,

    kMacroWeightId = 100,
    kMacroDarkBrightId = 101,
    kMacroTightLooseId = 102,
    kMacroCleanViolentId = 103,
    kMacroParamRangeEnd = 200,
};

}  // namespace Profundum
