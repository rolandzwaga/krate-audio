#pragma once

// ==============================================================================
// Vorago - Plugin Identifiers and Parameter IDs
// ==============================================================================
// These GUIDs uniquely identify the plugin components.
//
// IMPORTANT: Once published, NEVER change these IDs or hosts will not
// recognize saved projects using your plugin.
// ==============================================================================

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Vorago {

/// FR-012. State version for serialization (bump when the format changes
/// post-release). Shared by processor and controller; neither includes the
/// other, so the constant lives here.
constexpr Steinberg::int32 kCurrentStateVersion = 1;

/// FR-011. Freshly generated v4 GUIDs. NEVER reused, NEVER changed after release.
/// Processor component ID - the audio processing component (runs on the audio
/// thread).
static const Steinberg::FUID kProcessorUID(0xE25977E5, 0xFF444D29, 0x98D56C21, 0x3F178458);

/// FR-011. Controller component ID - the edit controller component (runs on the
/// UI thread).
static const Steinberg::FUID kControllerUID(0xBDDF94B8, 0xEABE4000, 0x8EE4CD39, 0xA8107DBC);

/// FR-014. DEF_CLASS2 subcategory string; instrument.
/// Deliberately `const`, NOT `constexpr` (cross-platform rule: anything
/// initialized from / handed to an SDK constant is `const`). The pointer itself
/// is also `const` so the unused-in-this-TU case falls under
/// `-Wunused-const-variable` (off by default in C++) rather than
/// `-Wunused-variable`, which GCC 13 emits for a mutable namespace-scope static
/// in every TU that includes this header without using it.
/// (Rationale copied from plugins/seraphis/src/plugin_ids.h:43-49.)
static const char* const kSubCategories = "Instrument|Synth";

// ==============================================================================
// Parameter IDs
// ==============================================================================
// All parameter values at the VST boundary are normalized (0.0 to 1.0).
//
/// FR-013. Reserved map (roadmap line 533; bands in roadmap line 549's pack order):
///   0-99      Global     (Phase 11 - SHIPPED: master gain, polyphony)
///   100-199   Macros     (Phase 11 - SHIPPED, INERT; wired in Phase 12)
///   200-299 Cloud · 300-399 Noise · 400-499 Resonance · 500-599 Ecology · 600-699 Sub
///   700-799 Smear · 800-899 Events · 900-999 Ecosystem · 1000-1099 Body · 1100-1199 Space
///   1200+     UNASSIGNED - Phase 12 claims a whole band for any unnamed section.
/// REGISTERED TYPES ARE FROZEN (roadmap line 559): kMasterGainId + 12 macros are plain
/// Steinberg::Vst::Parameter; kPolyphonyId is a StringListParameter.
enum ParameterIDs : Steinberg::Vst::ParamID {
    kMasterGainId = 0,
    kPolyphonyId = 1,

    kMacroDarknessId = 100,  // id - 100 == static_cast<int>(VoragoMacro::X)
    kMacroAgeId = 101,       // (vorago_macro_matrix.h:95-109)
    kMacroDensityId = 102,
    kMacroMovementId = 103,
    kMacroGravityId = 104,
    kMacroEntropyId = 105,
    kMacroPressureId = 106,
    kMacroWeightId = 107,
    kMacroFogId = 108,
    kMacroLifeId = 109,
    kMacroDepthId = 110,
    kMacroMassId = 111,
};

/// FR-043 range dispatch.
constexpr Steinberg::Vst::ParamID kGlobalParamRangeEnd = 100;  // id <  100 -> global pack
constexpr Steinberg::Vst::ParamID kMacroParamRangeEnd = 200;   // id <  200 -> macro pack

}  // namespace Vorago
