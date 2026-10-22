#pragma once

// ==============================================================================
// Profundum - Fold / Shape section pack (IDs 500-501)   FR-001, FR-002, FR-005, FR-009; plan 4.2
// ==============================================================================
// Two E-parameters over leaves 9 (foldAmount, 0-100) and 10 (shape)
// (morph_state.h:178-181). FR-009: ID 500 is titled "Harmonic Fold" (Mass100
// display) and 501 "Shape" is a plain Percent with no corner names. Ranges and
// defaults come from lawOf(leaf) / kDefaultLeafBits only (FR-002, FR-003).
// ==============================================================================

#include "parameters/sound_param_row.h"
#include "plugin_ids.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Profundum {

inline constexpr std::array<SoundParamRow, 2> kFoldShapeSoundRows{{
    {kFoldAmountId, 9, kNoLeaf, STR16("Harmonic Fold"), STR16(""), nullptr, 0, DisplayUnit::Mass100},
    {kFoldShapeId, 10, kNoLeaf, STR16("Shape"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
}};

// ---- compile-time proofs (FR-002; tasks.md G7 rules a-d) ----

// (a) the rows cover leaves {9, 10} exactly: each once, no other leaf.
static_assert(
    [] {
        constexpr std::array<std::uint8_t, 2> want{9, 10};
        std::size_t used = 0;
        for (const auto& r : kFoldShapeSoundRows) {
            used += (r.leaf != kNoLeaf ? 1u : 0u) + (r.leaf2 != kNoLeaf ? 1u : 0u);
        }
        if (used != want.size()) {
            return false;
        }
        for (const std::uint8_t w : want) {
            int hits = 0;
            for (const auto& r : kFoldShapeSoundRows) {
                hits += (r.leaf == w ? 1 : 0) + (r.leaf2 == w ? 1 : 0);
            }
            if (hits != 1) {
                return false;
            }
        }
        return true;
    }(),
    "fold/shape pack: rows cover leaves {9, 10} exactly once");

// (b) / (d) no fold/shape row is a list, so every row is a continuous F32 leaf.
static_assert(
    [] {
        for (const auto& r : kFoldShapeSoundRows) {
            if (r.labels != nullptr || r.labelCount != 0 || lawOf(r.leaf).kind != MorphState::Kind::F32) {
                return false;
            }
        }
        return true;
    }(),
    "fold/shape pack: every row is a continuous F32 leaf");

// (c) every ID lies in the fold/shape band [500, 600).
static_assert(
    [] {
        for (const auto& r : kFoldShapeSoundRows) {
            if (r.id < kFoldAmountId || r.id >= kFoldParamRangeEnd) {
                return false;
            }
        }
        return true;
    }(),
    "fold/shape pack: every ID lies in 500-599");

// Each row names the leaf the spec ID table says it does (morph_state.h:178-181).
static_assert(std::string_view(lawOf(kFoldShapeSoundRows[0].leaf).name) == "foldAmount" &&
                  std::string_view(lawOf(kFoldShapeSoundRows[1].leaf).name) == "shape",
              "fold/shape pack: row -> leaf names match the spec ID table");

// ---- FR-005 entry points ----

inline void registerFoldShapeParams(Steinberg::Vst::ParameterContainer& parameters) {
    for (const auto& row : kFoldShapeSoundRows) {
        registerSoundRow(parameters, row);
    }
}

/// Real-time safe. Returns true when id is a fold/shape row (handled).
inline bool handleFoldShapeParamChange(EndpointParams& target, Steinberg::Vst::ParamID id, double value) noexcept {
    for (const auto& row : kFoldShapeSoundRows) {
        if (row.id == id) {
            writeSoundRow(target, row, value);
            return true;
        }
    }
    return false;
}

inline Steinberg::tresult formatFoldShapeParam(Steinberg::Vst::ParamID id, double value,
                                               Steinberg::Vst::String128 string) {
    for (const auto& row : kFoldShapeSoundRows) {
        if (row.id == id) {
            return formatSoundRow(row, value, string);
        }
    }
    return Steinberg::kResultFalse;
}

}  // namespace Profundum
