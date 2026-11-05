#pragma once

// ==============================================================================
// Profundum - Motion section pack (IDs 900-914, leaves 36-51)
// Phase 11 T010 (FR-001, FR-002, FR-005, FR-006, FR-012; plan 4.2)
// ==============================================================================
// Fifteen E-parameters over the sixteen motion.* leaves of the MorphState table
// (morph_state.h:247-285). Cycle Sync Rate (907) is a Sync Rate pair: one
// 30-entry dropdown writing motion.cycleNote (43) and motion.cycleModifier (44)
// through kNoteValueDropdownMapping (note_value.h:136; FR-012).
//
// Every range, law and enumerator count comes from lawOf(leaf) (FR-002): no
// range literal for a morphed control appears here. The static_asserts below
// are this pack's compile-time test (T010); T013 adds the runtime proof.
// ==============================================================================

#include "plugin_ids.h"
#include "parameters/sound_param_row.h"
#include "ui/note_value_ui.h"  // plugins/shared/src/ui/note_value_ui.h:21 kNoteValueDropdownStrings

#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/note_value.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <algorithm>

namespace Profundum {

// ------------------------------------------------------------------------------
// List labels (ASCII)
// ------------------------------------------------------------------------------

inline constexpr const Steinberg::Vst::TChar* kMotionSourceLabels[] = {STR16("Off"), STR16("Env"), STR16("Cycle"),
                                                                       STR16("Drift")};
inline constexpr const Steinberg::Vst::TChar* kMotionTargetLabels[] = {STR16("Mass"), STR16("Depth"), STR16("Body"),
                                                                       STR16("Edge"), STR16("Shift")};
inline constexpr const Steinberg::Vst::TChar* kMotionCycleShapeLabels[] = {
    STR16("Sine"), STR16("Triangle"), STR16("Sawtooth"), STR16("Square"), STR16("S&H"), STR16("Smooth Random")};
inline constexpr const Steinberg::Vst::TChar* kMotionCycleSyncLabels[] = {STR16("Off"), STR16("On")};
inline constexpr const Steinberg::Vst::TChar* kMotionCycleRetriggerLabels[] = {STR16("Off"), STR16("On")};
inline constexpr const Steinberg::Vst::TChar* kMotionPolarityLabels[] = {STR16("Bipolar"), STR16("Unipolar")};

// ------------------------------------------------------------------------------
// Rows (ID -> leaf; plan 4.2 table; titles from the spec ID table)
// ------------------------------------------------------------------------------

inline constexpr std::array<SoundParamRow, 15> kMotionRows = {{
    {.id = kMotionSourceId, .leaf = 36, .leaf2 = kNoLeaf, .title = STR16("Motion Source"), .units = STR16(""), .labels = kMotionSourceLabels,
     .labelCount = static_cast<std::uint8_t>(std::size(kMotionSourceLabels)), .display = DisplayUnit::List},
    {.id = kMotionTargetId, .leaf = 37, .leaf2 = kNoLeaf, .title = STR16("Motion Target"), .units = STR16(""), .labels = kMotionTargetLabels,
     .labelCount = static_cast<std::uint8_t>(std::size(kMotionTargetLabels)), .display = DisplayUnit::List},
    {.id = kMotionAmountId, .leaf = 38, .leaf2 = kNoLeaf, .title = STR16("Motion Amount"), .units = STR16("%"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::BipolarPercent},
    {.id = kMotionDepthId, .leaf = 39, .leaf2 = kNoLeaf, .title = STR16("Motion Depth"), .units = STR16("%"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Percent},
    {.id = kMotionCycleRateId, .leaf = 40, .leaf2 = kNoLeaf, .title = STR16("Cycle Rate"), .units = STR16("Hz"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Hz},
    {.id = kMotionCycleShapeId, .leaf = 41, .leaf2 = kNoLeaf, .title = STR16("Cycle Shape"), .units = STR16(""), .labels = kMotionCycleShapeLabels,
     .labelCount = static_cast<std::uint8_t>(std::size(kMotionCycleShapeLabels)), .display = DisplayUnit::List},
    {.id = kMotionCycleSyncId, .leaf = 42, .leaf2 = kNoLeaf, .title = STR16("Cycle Sync"), .units = STR16(""), .labels = kMotionCycleSyncLabels,
     .labelCount = static_cast<std::uint8_t>(std::size(kMotionCycleSyncLabels)), .display = DisplayUnit::List},
    {.id = kMotionCycleSyncRateId, .leaf = 43, .leaf2 = 44, .title = STR16("Cycle Sync Rate"), .units = STR16(""),
     .labels = Krate::Plugins::Parameters::kNoteValueDropdownStrings,
     .labelCount = static_cast<std::uint8_t>(std::size(Krate::Plugins::Parameters::kNoteValueDropdownStrings)), .display = DisplayUnit::List},
    {.id = kMotionCyclePhaseId, .leaf = 45, .leaf2 = kNoLeaf, .title = STR16("Cycle Phase"), .units = STR16("deg"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Degrees},
    {.id = kMotionCycleRetriggerId, .leaf = 46, .leaf2 = kNoLeaf, .title = STR16("Cycle Retrigger"), .units = STR16(""), .labels = kMotionCycleRetriggerLabels,
     .labelCount = static_cast<std::uint8_t>(std::size(kMotionCycleRetriggerLabels)), .display = DisplayUnit::List},
    {.id = kMotionCycleFadeInId, .leaf = 47, .leaf2 = kNoLeaf, .title = STR16("Cycle Fade-In"), .units = STR16("ms"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Time},
    {.id = kMotionPolarityId, .leaf = 48, .leaf2 = kNoLeaf, .title = STR16("Motion Polarity"), .units = STR16(""), .labels = kMotionPolarityLabels,
     .labelCount = static_cast<std::uint8_t>(std::size(kMotionPolarityLabels)), .display = DisplayUnit::List},
    {.id = kMotionDriftSmoothnessId, .leaf = 49, .leaf2 = kNoLeaf, .title = STR16("Drift Smoothness"), .units = STR16("%"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Percent},
    {.id = kMotionEnvAttackId, .leaf = 50, .leaf2 = kNoLeaf, .title = STR16("Env Attack"), .units = STR16("ms"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Time},
    {.id = kMotionEnvDecayId, .leaf = 51, .leaf2 = kNoLeaf, .title = STR16("Env Decay"), .units = STR16("ms"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Time},
}};

// ------------------------------------------------------------------------------
// Compile-time conformance (FR-002; tasks.md G7 (a)-(d) + the T010 default index)
// ------------------------------------------------------------------------------

namespace motion_pack_detail {

/// (a) The leaf / leaf2 values over the rows are exactly {first..last}: no duplicate, no gap, nothing outside.
template <std::size_t N>
[[nodiscard]] constexpr bool coversLeavesExactly(const std::array<SoundParamRow, N>& rows, std::size_t first,
                                                 std::size_t last) noexcept {
    for (const auto& r : rows) {
        if (static_cast<std::size_t>(r.leaf) >= kNumLeaves ||
            (r.leaf2 != kNoLeaf && static_cast<std::size_t>(r.leaf2) >= kNumLeaves)) {
            return false;
        }
    }
    for (std::size_t leaf = 0; leaf < kNumLeaves; ++leaf) {
        int count = 0;
        for (const auto& r : rows) {
            count += (static_cast<std::size_t>(r.leaf) == leaf) ? 1 : 0;
            count += (r.leaf2 != kNoLeaf && static_cast<std::size_t>(r.leaf2) == leaf) ? 1 : 0;
        }
        const int expected = (leaf >= first && leaf <= last) ? 1 : 0;
        if (count != expected) {
            return false;
        }
    }
    return true;
}

/// (b) + (d): list rows carry maxEnum + 1 labels (Bool: 2; Sync Rate pair: 10 x 3 == 30); continuous rows none.
template <std::size_t N>
[[nodiscard]] constexpr bool labelsMatchLaws(const std::array<SoundParamRow, N>& rows) noexcept {
    return std::ranges::all_of(rows, [&](const auto& r) {
        const FieldInfo& f = lawOf(r.leaf);
        if (r.leaf2 != kNoLeaf) {
            const FieldInfo& f2 = lawOf(r.leaf2);
            const int pairCount = (static_cast<int>(f.maxEnum) + 1) * (static_cast<int>(f2.maxEnum) + 1);
            if (r.labels == nullptr || r.display != DisplayUnit::List || f.kind != MorphState::Kind::Enum8 ||
                f2.kind != MorphState::Kind::Enum8 || static_cast<int>(r.labelCount) != pairCount ||
                pairCount != Krate::DSP::kNoteValueDropdownCount) {
                return false;
            }
        } else if (f.kind != MorphState::Kind::F32) {
            if (r.labels == nullptr || r.display != DisplayUnit::List ||
                static_cast<int>(r.labelCount) != static_cast<int>(f.maxEnum) + 1) {
                return false;
            }
            if (f.kind == MorphState::Kind::Bool && r.labelCount != 2) {
                return false;
            }
        } else if (r.labels != nullptr || r.labelCount != 0 || r.display == DisplayUnit::List) {
            return false;
        }
        return true;
    });
}

/// (c) Every ID lies in the band, and the rows are dense from the band's first ID (O(1) dispatch).
template <std::size_t N>
[[nodiscard]] constexpr bool idsDenseInBand(const std::array<SoundParamRow, N>& rows, Steinberg::Vst::ParamID bandLo,
                                            Steinberg::Vst::ParamID bandEnd) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        const Steinberg::Vst::ParamID id = rows[i].id;
        if (id < bandLo || id >= bandEnd || id != bandLo + static_cast<Steinberg::Vst::ParamID>(i)) {
            return false;
        }
    }
    return true;
}

}  // namespace motion_pack_detail

static_assert(motion_pack_detail::coversLeavesExactly(kMotionRows, 36, 51), "motion rows must cover leaves {36..51}");
static_assert(motion_pack_detail::labelsMatchLaws(kMotionRows), "motion list label counts must equal maxEnum + 1");
static_assert(motion_pack_detail::idsDenseInBand(kMotionRows, kMotionSourceId, kMotionParamRangeEnd),
              "motion IDs must be 900..914 in row order");
static_assert(kMotionSourceId == 900 && kMotionEnvDecayId == 914 && kMotionParamRangeEnd == 1000);
static_assert(std::size(Krate::Plugins::Parameters::kNoteValueDropdownStrings) ==
              static_cast<std::size_t>(Krate::DSP::kNoteValueDropdownCount));
// plan 1.6: motion.cycleNote = Quarter, motion.cycleModifier = None (morph_state.h:92-93) -> "1/4", index 13.
static_assert(syncRateIndexOf(kDefaultLeafBits[43], kDefaultLeafBits[44]) == 13,
              "Cycle Sync Rate default must be dropdown index 13 (1/4)");

// ------------------------------------------------------------------------------
// FR-005 pack entry points (thin wrappers over the generic row path)
// ------------------------------------------------------------------------------

/// Registers 900-914 in row order.
inline void registerMotionParams(Steinberg::Vst::ParameterContainer& parameters) {
    for (const auto& row : kMotionRows) {
        registerSoundRow(parameters, row);
    }
}

/// Writes a 900-914 value into the target endpoint. Returns false for an ID outside the pack. Real-time safe.
inline bool handleMotionParamChange(EndpointParams& target, Steinberg::Vst::ParamID id, double v) noexcept {
    if (id < kMotionSourceId || id > kMotionEnvDecayId) {
        return false;
    }
    writeSoundRow(target, kMotionRows[static_cast<std::size_t>(id - kMotionSourceId)], v);
    return true;
}

/// FR-006 display of a 900-914 value; kResultFalse for list rows and IDs outside the pack.
inline Steinberg::tresult formatMotionParam(Steinberg::Vst::ParamID id, double v, Steinberg::Vst::String128 string) {
    if (id < kMotionSourceId || id > kMotionEnvDecayId) {
        return Steinberg::kResultFalse;
    }
    return formatSoundRow(kMotionRows[static_cast<std::size_t>(id - kMotionSourceId)], v, string);
}

}  // namespace Profundum
