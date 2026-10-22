#pragma once

// ==============================================================================
// Profundum - Field section pack (IDs 800-809, leaves 25-35)
// Phase 11 T010 (FR-001, FR-002, FR-005, FR-006, FR-012; plan 4.2)
// ==============================================================================
// Ten E-parameters over the eleven field.* leaves of the MorphState table
// (morph_state.h:219-245). Field Sync Rate (809) is a Sync Rate pair: one
// 30-entry dropdown writing field.cycleNote (34) and field.cycleModifier (35)
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

namespace Profundum {

// ------------------------------------------------------------------------------
// List labels (ASCII)
// ------------------------------------------------------------------------------

inline constexpr const Steinberg::Vst::TChar* kFieldImageModeLabels[] = {STR16("Solid"), STR16("Spread"),
                                                                         STR16("Wide")};
inline constexpr const Steinberg::Vst::TChar* kFieldMotionSourceLabels[] = {STR16("Drift"), STR16("Cycle")};
inline constexpr const Steinberg::Vst::TChar* kFieldSyncLabels[] = {STR16("Off"), STR16("On")};

// ------------------------------------------------------------------------------
// Rows (ID -> leaf; plan 4.2 table; titles from the spec ID table)
// ------------------------------------------------------------------------------

inline constexpr std::array<SoundParamRow, 10> kFieldRows = {{
    {kFieldImageModeId, 25, kNoLeaf, STR16("Image Mode"), STR16(""), kFieldImageModeLabels,
     static_cast<std::uint8_t>(std::size(kFieldImageModeLabels)), DisplayUnit::List},
    {kFieldSpreadId, 26, kNoLeaf, STR16("Spread"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kFieldGravityId, 27, kNoLeaf, STR16("Gravity"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kFieldCrossoverId, 28, kNoLeaf, STR16("Crossover"), STR16("Hz"), nullptr, 0, DisplayUnit::Hz},
    {kFieldCurveId, 29, kNoLeaf, STR16("Curve"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kFieldMotionDepthId, 30, kNoLeaf, STR16("Field Motion Depth"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kFieldMotionRateId, 31, kNoLeaf, STR16("Field Motion Rate"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kFieldMotionSourceId, 32, kNoLeaf, STR16("Field Motion Source"), STR16(""), kFieldMotionSourceLabels,
     static_cast<std::uint8_t>(std::size(kFieldMotionSourceLabels)), DisplayUnit::List},
    {kFieldSyncId, 33, kNoLeaf, STR16("Field Sync"), STR16(""), kFieldSyncLabels,
     static_cast<std::uint8_t>(std::size(kFieldSyncLabels)), DisplayUnit::List},
    {kFieldSyncRateId, 34, 35, STR16("Field Sync Rate"), STR16(""),
     Krate::Plugins::Parameters::kNoteValueDropdownStrings,
     static_cast<std::uint8_t>(std::size(Krate::Plugins::Parameters::kNoteValueDropdownStrings)), DisplayUnit::List},
}};

// ------------------------------------------------------------------------------
// Compile-time conformance (FR-002; tasks.md G7 (a)-(d) + the T010 default index)
// ------------------------------------------------------------------------------

namespace field_pack_detail {

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
    for (const auto& r : rows) {
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
    }
    return true;
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

}  // namespace field_pack_detail

static_assert(field_pack_detail::coversLeavesExactly(kFieldRows, 25, 35), "field rows must cover leaves {25..35}");
static_assert(field_pack_detail::labelsMatchLaws(kFieldRows), "field list label counts must equal maxEnum + 1");
static_assert(field_pack_detail::idsDenseInBand(kFieldRows, kFieldImageModeId, kFieldParamRangeEnd),
              "field IDs must be 800..809 in row order");
static_assert(kFieldImageModeId == 800 && kFieldSyncRateId == 809 && kFieldParamRangeEnd == 900);
static_assert(std::size(Krate::Plugins::Parameters::kNoteValueDropdownStrings) ==
              static_cast<std::size_t>(Krate::DSP::kNoteValueDropdownCount));
// plan 1.6: field.cycleNote = Whole, field.cycleModifier = None (bass_stereo_field.h:109-110) -> "1/1", index 19.
static_assert(syncRateIndexOf(kDefaultLeafBits[34], kDefaultLeafBits[35]) == 19,
              "Field Sync Rate default must be dropdown index 19 (1/1)");

// ------------------------------------------------------------------------------
// FR-005 pack entry points (thin wrappers over the generic row path)
// ------------------------------------------------------------------------------

/// Registers 800-809 in row order.
inline void registerFieldParams(Steinberg::Vst::ParameterContainer& parameters) {
    for (const auto& row : kFieldRows) {
        registerSoundRow(parameters, row);
    }
}

/// Writes an 800-809 value into the target endpoint. Returns false for an ID outside the pack. Real-time safe.
inline bool handleFieldParamChange(EndpointParams& target, Steinberg::Vst::ParamID id, double v) noexcept {
    if (id < kFieldImageModeId || id > kFieldSyncRateId) {
        return false;
    }
    writeSoundRow(target, kFieldRows[static_cast<std::size_t>(id - kFieldImageModeId)], v);
    return true;
}

/// FR-006 display of an 800-809 value; kResultFalse for list rows and IDs outside the pack.
inline Steinberg::tresult formatFieldParam(Steinberg::Vst::ParamID id, double v, Steinberg::Vst::String128 string) {
    if (id < kFieldImageModeId || id > kFieldSyncRateId) {
        return Steinberg::kResultFalse;
    }
    return formatSoundRow(kFieldRows[static_cast<std::size_t>(id - kFieldImageModeId)], v, string);
}

}  // namespace Profundum
