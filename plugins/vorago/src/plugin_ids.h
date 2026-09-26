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

#include <cstddef>

namespace Vorago {

/// FR-012. State version for serialization (bump when the format changes
/// post-release). Shared by processor and controller; neither includes the
/// other, so the constant lives here.
/// Phase 12 (spec C-7, FR-040): version 2 - the v1 stream is a strict prefix.
constexpr Steinberg::int32 kCurrentStateVersion = 2;

/// Spec C-7 / plan 4.9: the v2 stream length. Version, the v1 block (global +
/// macros), then the global v2 extension and the 14 packs in ascending ID band.
/// Sustain pedal and channel pressure are never written (FR-045).
constexpr std::size_t kStateV2Bytes = 428;
static_assert(kStateV2Bytes == 4    // int32 version
                                   + 8    // v1 global: float masterGain, int32 polyphony
                                   + 48   // v1 macros: 12 x float
                                   + 8    // v2 global: int32 seedIndex, float outputSaturation
                                   + 28   // cloud      7F
                                   + 92   // noise      3F + 4I + 4I + 4F + 4F + 4F
                                   + 16   // resonance  3F + 1I
                                   + 32   // ecology    2F + 6I
                                   + 20   // sub        5F
                                   + 12   // smear      3F
                                   + 4    // events     1F
                                   + 4    // ecosystem  1F
                                   + 24   // body       4F + 2I
                                   + 64   // space      15F + 1I
                                   + 28   // envelope   1I + 6F
                                   + 8    // bloom      2F
                                   + 16   // ghost      3F + 1I
                                   + 12,  // life       3F
              "kStateV2Bytes must equal the per-pack sum of spec C-7 / plan 4.9");

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
/// FR-013 / Phase 12 FR-010 (spec C-5). Band map (roadmap line 533; bands in roadmap
/// line 549's pack order):
///   0-99      Global     (0-1 shipped Phase 11; 2-5 Phase 12)
///   100-199   Macros     (shipped Phase 11; LIVE from Phase 12)
///   200-299 Cloud · 300-399 Noise · 400-499 Resonance · 500-599 Ecology · 600-699 Sub
///   700-799 Smear · 800-899 Events · 900-999 Ecosystem · 1000-1099 Body · 1100-1199 Space
///   1200-1299 Envelope · 1300-1399 Bloom · 1400-1499 Ghost · 1500-1599 Life
///             (the four bands above 1200 are claimed whole by Phase 12)
///   1600+     UNASSIGNED
/// REGISTERED TYPES ARE FROZEN (roadmap line 559): kMasterGainId + 12 macros are plain
/// Steinberg::Vst::Parameter; kPolyphonyId is a StringListParameter.
enum ParameterIDs : Steinberg::Vst::ParamID {
    // --- Global (0-99) ---
    kMasterGainId = 0,
    kPolyphonyId = 1,
    kSeedId = 2,
    kOutputSaturationId = 3,
    kSustainPedalId = 4,     // performance controller, hidden, not persisted (FR-045)
    kChannelPressureId = 5,  // performance controller, hidden, not persisted (FR-045)

    // --- Macros (100-199) ---
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

    // --- Cloud (200-299) ---
    kCloudRichnessId = 200,
    kCloudTiltId = 201,
    kCloudMutationId = 202,
    kCloudInharmonicityId = 203,
    kCloudDriftDepthId = 204,
    kCloudStereoSpreadId = 205,
    kCloudSpectralGravityId = 206,

    // --- Noise (300-399) ---
    kNoiseLevelId = 300,
    kNoiseWakeId = 301,
    kNoiseWanderRateId = 302,
    kNoiseSlot0ModelId = 310,
    kNoiseSlot1ModelId = 311,
    kNoiseSlot2ModelId = 312,
    kNoiseSlot3ModelId = 313,
    kNoiseSlot0TypeId = 320,
    kNoiseSlot1TypeId = 321,
    kNoiseSlot2TypeId = 322,
    kNoiseSlot3TypeId = 323,
    kNoiseSlot0CombFundamentalId = 330,
    kNoiseSlot1CombFundamentalId = 331,
    kNoiseSlot2CombFundamentalId = 332,
    kNoiseSlot3CombFundamentalId = 333,
    kNoiseSlot0CombSpreadId = 340,
    kNoiseSlot1CombSpreadId = 341,
    kNoiseSlot2CombSpreadId = 342,
    kNoiseSlot3CombSpreadId = 343,
    kNoiseSlot0CombFeedbackId = 350,
    kNoiseSlot1CombFeedbackId = 351,
    kNoiseSlot2CombFeedbackId = 352,
    kNoiseSlot3CombFeedbackId = 353,

    // --- Resonance (400-499) ---
    kResonanceGravityId = 400,
    kResonanceMixId = 401,
    kResonanceWanderRateId = 402,
    kResonanceAnchorModeId = 403,

    // --- Ecology (500-599) ---
    kEcologyMixId = 500,
    kEcologyLoopGainId = 501,
    kEcologyLoop0FilterModeId = 510,
    kEcologyLoop1FilterModeId = 511,
    kEcologyLoop2FilterModeId = 512,
    kEcologyLoop3FilterModeId = 513,
    kEcologyLoop4FilterModeId = 514,
    kEcologyLoop5FilterModeId = 515,

    // --- Sub (600-699) ---
    kSubLevelOffsetId = 600,
    kSubTrackingId = 601,
    kSubDiv2LevelId = 610,
    kSubDiv4LevelId = 611,
    kSubFifthBelowLevelId = 612,

    // --- Smear (700-799) ---
    kSmearAmountId = 700,
    kSmearDecoherenceId = 701,
    kSmearTiltId = 702,

    // --- Events (800-899) ---
    kEventsRateScaleId = 800,

    // --- Ecosystem (900-999) ---
    kEcosystemDepthId = 900,

    // --- Body (1000-1099) ---
    kBodyBlendId = 1000,
    kBodyDampingId = 1001,
    kBodyResonanceId = 1002,
    kBodyMixId = 1003,
    kBodyMaterialAId = 1004,
    kBodyMaterialBId = 1005,

    // --- Space (1100-1199) ---
    kSpaceSizeId = 1100,
    kSpaceDarknessId = 1101,
    kSpaceDecayId = 1102,
    kSpaceFogId = 1103,
    kSpaceDamperDepthId = 1104,
    kSpaceMixId = 1105,
    kSpaceWidthId = 1106,
    kSpaceDensityId = 1107,
    kSpaceDimensionalityId = 1108,
    kSpaceBreathId = 1109,
    kSpaceEarlySizeId = 1110,
    kSpaceEarlyLevelId = 1111,
    kSpaceEarlyAbsorptionId = 1112,
    kSpaceEarlySendId = 1113,
    kSpaceDamperRateId = 1114,
    kSpaceFreezeId = 1115,

    // --- Envelope (1200-1299) ---
    kEnvelopeModeId = 1200,
    kEnvelopeStage0TimeId = 1201,
    kEnvelopeStage1TimeId = 1202,
    kEnvelopeStage2TimeId = 1203,
    kEnvelopeStage3TimeId = 1204,
    kEnvelopeReleaseId = 1205,
    kEnvelopeGrowthDurationId = 1206,

    // --- Bloom (1300-1399) ---
    kBloomDepthId = 1300,
    kBloomSpawnRateId = 1301,

    // --- Ghost (1400-1499) ---
    kGhostPeakLevelId = 1400,
    kGhostBlurId = 1401,
    kGhostReverseProbabilityId = 1402,
    kGhostEventTriggersId = 1403,

    // --- Life (1500-1599) ---
    kLifeBreathingDepthId = 1500,
    kLifeBreathingIrregularityId = 1501,
    kLifeTidalDepthId = 1502,
};

/// FR-043 range dispatch: a pack owns [previous range end, its range end).
constexpr Steinberg::Vst::ParamID kGlobalParamRangeEnd = 100;      // id <  100 -> global pack
constexpr Steinberg::Vst::ParamID kMacroParamRangeEnd = 200;       // id <  200 -> macro pack
constexpr Steinberg::Vst::ParamID kCloudParamRangeEnd = 300;       // id <  300 -> cloud pack
constexpr Steinberg::Vst::ParamID kNoiseParamRangeEnd = 400;       // id <  400 -> noise pack
constexpr Steinberg::Vst::ParamID kResonanceParamRangeEnd = 500;   // id <  500 -> resonance pack
constexpr Steinberg::Vst::ParamID kEcologyParamRangeEnd = 600;     // id <  600 -> ecology pack
constexpr Steinberg::Vst::ParamID kSubParamRangeEnd = 700;         // id <  700 -> sub pack
constexpr Steinberg::Vst::ParamID kSmearParamRangeEnd = 800;       // id <  800 -> smear pack
constexpr Steinberg::Vst::ParamID kEventsParamRangeEnd = 900;      // id <  900 -> events pack
constexpr Steinberg::Vst::ParamID kEcosystemParamRangeEnd = 1000;  // id < 1000 -> ecosystem pack
constexpr Steinberg::Vst::ParamID kBodyParamRangeEnd = 1100;       // id < 1100 -> body pack
constexpr Steinberg::Vst::ParamID kSpaceParamRangeEnd = 1200;      // id < 1200 -> space pack
constexpr Steinberg::Vst::ParamID kEnvelopeParamRangeEnd = 1300;   // id < 1300 -> envelope pack
constexpr Steinberg::Vst::ParamID kBloomParamRangeEnd = 1400;      // id < 1400 -> bloom pack
constexpr Steinberg::Vst::ParamID kGhostParamRangeEnd = 1500;      // id < 1500 -> ghost pack
constexpr Steinberg::Vst::ParamID kLifeParamRangeEnd = 1600;       // id < 1600 -> life pack

}  // namespace Vorago
