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

#include <krate/dsp/systems/bass_morph_engine.h>

#include <cstddef>

namespace Profundum {

/// FR-012 / FR-040 (Phase 11: v2, 510 bytes); Phase 12 FR-047 / FR-096: v3, 574 bytes (the v2 body plus two
/// endpoint-name blocks, plan 4.5). Shared by processor and controller; neither includes the other.
constexpr Steinberg::int32 kCurrentStateVersion = 3;

/// FR-044: version (4) + global block (4 + 4 + 4 + 4) + macro block (4 x 4).
constexpr std::size_t kStateV1Bytes = 36;
static_assert(kStateV1Bytes == 4 + (4 + 4 + 4 + 4) + 4 * 4, "spec FR-044 byte layout (plan 3.4)");

// ------------------------------------------------------------------------------
// State v2 layout (Phase 11, FR-040; plan 3.2).
// ------------------------------------------------------------------------------
constexpr Steinberg::int32 kStateV1Version = 1;               // a Phase 10 stream
constexpr Steinberg::int32 kStateV2Version = 2;               // a Phase 11 stream (Phase 12 T019)
constexpr std::size_t kStateNoteBlockBytes = 32;              // bytes 36-67
constexpr std::size_t kStateMorphControlBytes = 16;           // bytes 68-83
constexpr std::size_t kStateSeedBytes = 4;                    // bytes 84-87
constexpr std::size_t kStateEndpointAOffset = 88;
constexpr std::size_t kStateEndpointBOffset = 289;
constexpr std::size_t kStateRuntimeOffset = 490;
constexpr std::size_t kStateV2Bytes = 510;
static_assert(kStateEndpointAOffset ==
                  kStateV1Bytes + kStateNoteBlockBytes + kStateMorphControlBytes + kStateSeedBytes,
              "plan 3.2: endpoint A follows the v1, note, morph-control and seed blocks");
static_assert(kStateEndpointBOffset == kStateEndpointAOffset + Krate::DSP::kMorphStateBytes,
              "plan 3.2: endpoint B follows endpoint A");
static_assert(kStateRuntimeOffset == kStateEndpointBOffset + Krate::DSP::kMorphStateBytes,
              "plan 3.2: morph runtime follows endpoint B");
static_assert(kStateV2Bytes == kStateRuntimeOffset + Krate::DSP::BassMorphEngine::kMorphRuntimeBytes,
              "plan 3.2: v2 stream ends after the morph runtime blob");
static_assert(kStateV1Version + 1 == kStateV2Version && kStateV2Version + 1 == kCurrentStateVersion,
              "FR-040 / FR-096: v2 follows the Phase 10 v1 stream, v3 the Phase 11 v2 stream");

// ------------------------------------------------------------------------------
// State v3 (Phase 12, FR-047 / FR-096; plan 4.5): the v2 body (version word 3), then two name blocks.
//   510-541 name A: u8 length L (0 - 31), then L UTF-8 bytes, zero-padded to 31
//   542-573 name B: same layout
// ------------------------------------------------------------------------------
constexpr std::size_t kEndpointNameMaxBytes = 31;   // UTF-8 bytes; 1 length byte + 31 = 32 per endpoint
constexpr std::size_t kEndpointNameBlockBytes = 32;
constexpr std::size_t kStateNamesOffset = kStateV2Bytes;   // 510: the Phase 11 "append after byte 510" rule
constexpr std::size_t kStateV3Bytes = kStateNamesOffset + 2 * kEndpointNameBlockBytes;   // 574
static_assert(kEndpointNameBlockBytes == kEndpointNameMaxBytes + 1, "plan 4.5: the u8 length byte + 31 bytes");
static_assert(kStateV3Bytes == 574, "plan 4.5: v3 is 574 bytes");

// ------------------------------------------------------------------------------
// The 20-byte morph runtime blob (FR-041; plan 4.5.2). The engine keeps its offsets private
// (bass_morph_engine.h:340-347), so they are re-stated here for the synthesized blob; SC-012 arm 5
// checks them against a real engine's serializeMorphRuntime.
// ------------------------------------------------------------------------------
constexpr std::size_t kRtVersionOffset = 0;          // u8, kMorphRuntimeFormatVersion
constexpr std::size_t kRtTargetOffset = 1;           // f32 target position
constexpr std::size_t kRtPositionOffset = 5;         // f32 ramped p
constexpr std::size_t kRtAnchorPositionOffset = 9;   // f32 ramped p_a
constexpr std::size_t kRtModeOffset = 13;            // u8 Anchor morph mode
constexpr std::size_t kRtRampOffset = 14;            // f32 ramp ms
constexpr std::size_t kRtMainMaskOffset = 18;        // u8 main-group selection mask
constexpr std::size_t kRtAnchorSelectionOffset = 19; // u8 Anchor selection
static_assert(kRtAnchorSelectionOffset + 1 == Krate::DSP::BassMorphEngine::kMorphRuntimeBytes,
              "plan 4.5.2: the runtime blob ends after the Anchor selection byte");

// ------------------------------------------------------------------------------
// Message IDs (processor <-> controller; FR-025, FR-030, FR-031; plan 3.3)
// ------------------------------------------------------------------------------
inline constexpr const char* kMsgCopyEndpoint = "ProfundumCopyEndpoint";   // ctl -> proc, int attr "from" (0 A, 1 B)
inline constexpr const char* kMsgCompare = "ProfundumCompare";             // ctl -> proc, int attr "mode"
inline constexpr const char* kMsgEndpoint = "ProfundumEndpoint";           // proc -> ctl, int "endpoint", binary "blob" (201 B)
inline constexpr const char* kMsgCompareMode = "ProfundumCompareMode";     // proc -> ctl, int "mode"
inline constexpr const char* kMsgRequestSync = "ProfundumRequestSync";     // ctl -> proc, no attributes (FR-025 pull)
inline constexpr const char* kMsgEndpointName = "ProfundumEndpointName";   // ctl -> proc, int "endpoint", binary "name" (0..31 B)

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
///   400-499 Drop / note behaviour / amp envelope | 500-599 Fold / Shape | 600-699 Character | 700-799 Mass dynamics
///   800-899 Stereo field | 900-999 Motion | 1000-1099 Morph control. Phase 11 owns 104+ and 200+.
enum ParameterIDs : Steinberg::Vst::ParamID {
    kOutputGainId = 0,
    kVoiceModeId = 1,
    kPolyphonyId = 2,
    kOutputCeilingId = 3,
    kPitchBendId = 4,
    kSustainPedalId = 5,
    kModWheelId = 6,
    kChannelPressureId = 7,
    kGlobalParamRangeEnd = 100,

    kMacroWeightId = 100,
    kMacroDarkBrightId = 101,
    kMacroTightLooseId = 102,
    kMacroCleanViolentId = 103,
    kMassId = 104,
    kMorphPositionId = 105,
    kMacroParamRangeEnd = 200,

    kCoreDepthId = 200,
    kCoreBodyId,
    kCoreBodyCurvatureId,
    kCoreBodyEmphasisId,
    kCoreEdgeId,
    kCoreShiftId,
    kCoreExcitationMixId,
    kCoreExcitationDriveId,
    kCoreExcitationAsymmetryId,  // 206-208
    kCoreParamRangeEnd = 300,

    kAnchorWaveformId = 300,
    kAnchorModeId,
    kAnchorLevelId,
    kAnchorPunchBoostId,
    kAnchorPunchTimeId,
    kAnchorGhostTimeId,
    kAnchorFoldFeedId,  // 300-306
    kAnchorParamRangeEnd = 400,

    kDropAmountId = 400,
    kDropTimeId,
    kDropShapeId,
    kDropReboundId,  // 400-403
    kNoteMonoPriorityId = 410,
    kNoteLegatoId,
    kNotePortamentoId,
    kNotePortamentoModeId,
    kNoteGlideModeId,
    kNoteMemoryId,
    kNoteRetriggerId,
    kNotePitchBendRangeId,  // 410-417
    kAmpAttackId = 420,
    kAmpDecayId,
    kAmpSustainId,
    kAmpReleaseId,
    kAmpAttackCurveId,
    kAmpDecayCurveId,
    kAmpReleaseCurveId,  // 420-426
    kNoteParamRangeEnd = 500,

    kFoldAmountId = 500,
    kFoldShapeId = 501,
    kFoldParamRangeEnd = 600,

    kCharacterId = 600,
    kCharacterAmountId,
    kCharacterToneId,
    kCharacterParamRangeEnd = 700,

    kDynamicsTransientAttackId = 700,
    kDynamicsTransientSustainId,
    kDynamicsParamRangeEnd = 800,

    kFieldImageModeId = 800,
    kFieldSpreadId,
    kFieldGravityId,
    kFieldCrossoverId,
    kFieldCurveId,
    kFieldMotionDepthId,
    kFieldMotionRateId,
    kFieldMotionSourceId,
    kFieldSyncId,
    kFieldSyncRateId,  // 800-809
    kFieldParamRangeEnd = 900,

    kMotionSourceId = 900,
    kMotionTargetId,
    kMotionAmountId,
    kMotionDepthId,
    kMotionCycleRateId,
    kMotionCycleShapeId,
    kMotionCycleSyncId,
    kMotionCycleSyncRateId,
    kMotionCyclePhaseId,
    kMotionCycleRetriggerId,
    kMotionCycleFadeInId,
    kMotionPolarityId,
    kMotionDriftSmoothnessId,
    kMotionEnvAttackId,
    kMotionEnvDecayId,  // 900-914
    kMotionParamRangeEnd = 1000,

    kMorphAnchorModeId = 1000,
    kMorphEditTargetId = 1001,
    kMorphRampTimeId = 1002,
    kMorphParamRangeEnd = 1100,

    kNumRoutedIds = 1003,  // route table size: IDs 0..1002
};

static_assert(kMorphRampTimeId + 1 == kNumRoutedIds, "plan 3.1: the route table covers IDs 0..1002");
static_assert(kAmpReleaseCurveId == 426 && kMotionEnvDecayId == 914 && kFieldSyncRateId == 809 &&
                  kCoreExcitationAsymmetryId == 208 && kAnchorFoldFeedId == 306 && kNotePitchBendRangeId == 417,
              "plan 3.1: implicit enumerator runs land on the spec ID table");

}  // namespace Profundum
