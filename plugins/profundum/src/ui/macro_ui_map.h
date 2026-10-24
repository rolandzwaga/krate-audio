#pragma once

// ==============================================================================
// Profundum - macro -> registered-parameter map for the UI (Phase 12 T015)
// ==============================================================================
//   Serves: FR-005, FR-007 (the D3 / D4 generated tables), FR-060 (macro target highlight), FR-061 (rings).
//   Plan:   specs/profundum-phase12-ui/plan.md §3.2, §4.10
//   Tasks:  specs/profundum-phase12-ui/tasks.md (T015)
//
// kMacroTargetParamIds is in ProfundumMacroTarget order (profundum_macro_matrix.h:41-47): entry t is the
// registered parameter that ProfundumMacroMatrix::kRows[t] offsets. CompressionAmount has no registered control
// (profundum_macro_matrix.h:87; the engine hands it post-law), so its entry is kNoParamId.
//
// highlightMask(m) (FR-060) marks the targets of m's kRows rows with amount != 0, CompressionAmount excluded.
// DarkBright -> Shape (501) has amount 0 (profundum_macro_matrix.h:81) and is therefore never highlighted.
//
// Header-only, constexpr, no state. Controller / UI thread only by use; nothing here allocates.
// ==============================================================================

#include "parameters/sound_param_table.h"  // kParamRoutes, RouteKind (sound_param_table.h:70, :101)
#include "plugin_ids.h"                    // ParameterIDs, kNumRoutedIds (plugin_ids.h:100, :207)

#include "pluginterfaces/vst/vsttypes.h"

#include <krate/dsp/systems/profundum_macro_matrix.h>

#include <array>
#include <cstddef>

namespace Profundum::UI {

/// The "no registered control" sentinel (outside every routed ID).
inline constexpr Steinberg::Vst::ParamID kNoParamId = 0xFFFFFFFFu;

inline constexpr std::size_t kNumMacroTargets = Krate::DSP::ProfundumMacroMatrix::kNumTargets;

/// Entry t: the registered ID of ProfundumMacroTarget t (plan §4.10).
inline constexpr std::array<Steinberg::Vst::ParamID, kNumMacroTargets> kMacroTargetParamIds{
    kCoreDepthId, kCoreBodyId, kAnchorLevelId,                                         // Weight
    kCoreEdgeId, kCoreShiftId, kFoldShapeId, kCharacterToneId,                         // Dark <-> Bright
    kAmpAttackId, kAmpDecayId, kAmpReleaseId, kDynamicsTransientAttackId, kNoParamId,  // Tight <-> Loose (no Compression control)
    kCoreExcitationMixId, kCoreExcitationDriveId, kFoldAmountId, kCharacterAmountId};  // Clean <-> Violent

/// FR-060: true for the targets of m's kRows rows with amount != 0 (CompressionAmount excluded).
[[nodiscard]] constexpr std::array<bool, kNumRoutedIds> highlightMask(Krate::DSP::ProfundumMacro m) noexcept {
    std::array<bool, kNumRoutedIds> mask{};
    for (const auto& row : Krate::DSP::ProfundumMacroMatrix::kRows) {
        if (row.macro != m || row.amount == 0.0f ||
            row.target == Krate::DSP::ProfundumMacroTarget::CompressionAmount) {
            continue;
        }
        const Steinberg::Vst::ParamID id = kMacroTargetParamIds[static_cast<std::size_t>(row.target)];
        if (id < static_cast<Steinberg::Vst::ParamID>(kNumRoutedIds)) {
            mask[id] = true;
        }
    }
    return mask;
}

// ------------------------------------------------------------------------------
// Compile-time proofs (plan §4.10)
// ------------------------------------------------------------------------------

namespace macro_ui_detail {

inline constexpr std::size_t kCompressionIndex =
    static_cast<std::size_t>(Krate::DSP::ProfundumMacroTarget::CompressionAmount);

/// Every entry except CompressionAmount is a RouteKind::Sound route in kParamRoutes.
[[nodiscard]] constexpr bool everyTargetIsSoundRoute() noexcept {
    for (std::size_t t = 0; t < kNumMacroTargets; ++t) {
        if (t == kCompressionIndex) {
            continue;
        }
        const Steinberg::Vst::ParamID id = kMacroTargetParamIds[t];
        if (id >= static_cast<Steinberg::Vst::ParamID>(kNumRoutedIds)) {
            return false;
        }
        if (kParamRoutes[id].kind != RouteKind::Sound) {
            return false;
        }
    }
    return true;
}

/// kRows[t].target == t for every t, so kMacroTargetParamIds[t] and kRows[t] describe the same target.
[[nodiscard]] constexpr bool rowsInTargetOrder() noexcept {
    for (std::size_t t = 0; t < kNumMacroTargets; ++t) {
        if (static_cast<std::size_t>(Krate::DSP::ProfundumMacroMatrix::kRows[t].target) != t) {
            return false;
        }
    }
    return true;
}

}  // namespace macro_ui_detail

static_assert(kMacroTargetParamIds.size() == Krate::DSP::ProfundumMacroMatrix::kRows.size(),
              "plan §4.10: one registered ID per macro target");
static_assert(macro_ui_detail::everyTargetIsSoundRoute(),
              "plan §4.10: every non-Compression macro target is a RouteKind::Sound route");
static_assert(kMacroTargetParamIds[macro_ui_detail::kCompressionIndex] == kNoParamId,
              "plan §4.10: CompressionAmount has no registered control");
static_assert(macro_ui_detail::rowsInTargetOrder(), "plan §4.10: kRows[t].target == t");

}  // namespace Profundum::UI
