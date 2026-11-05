#pragma once

// ==============================================================================
// Profundum - Core section pack (IDs 200-210)   FR-001, FR-002, FR-005; plan 4.2
// ==============================================================================
// Eleven E-parameters (morphed, addressed through Edit Target) over the recipe and
// excitation leaves 0-8 of the MorphState table (morph_state.h:159-176) and, since
// Phase 15, Stiffness and Phase (leaves 62-63, specs/Profundum-roadmap.md). Ranges,
// laws and defaults come from lawOf(leaf) / kDefaultLeafBits only (FR-002, FR-003);
// no range literal lives here. The three entry points are thin wrappers over the
// generic row path of sound_param_row.h.
// ==============================================================================

#include "parameters/sound_param_row.h"
#include "plugin_ids.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <algorithm>

namespace Profundum {

inline constexpr std::array<SoundParamRow, 11> kCoreSoundRows{{
    {.id = kCoreDepthId, .leaf = 0, .leaf2 = kNoLeaf, .title = STR16("Depth"), .units = STR16("%"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Percent},
    {.id = kCoreBodyId, .leaf = 1, .leaf2 = kNoLeaf, .title = STR16("Body"), .units = STR16("%"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Percent},
    {.id = kCoreBodyCurvatureId, .leaf = 2, .leaf2 = kNoLeaf, .title = STR16("Body Curvature"), .units = STR16("%"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Percent},
    {.id = kCoreBodyEmphasisId, .leaf = 3, .leaf2 = kNoLeaf, .title = STR16("Body Emphasis"), .units = STR16("%"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::BipolarPercent},
    {.id = kCoreEdgeId, .leaf = 4, .leaf2 = kNoLeaf, .title = STR16("Edge"), .units = STR16("%"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Percent},
    {.id = kCoreShiftId, .leaf = 5, .leaf2 = kNoLeaf, .title = STR16("Spectral Shift"), .units = STR16("%"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::BipolarPercent},
    {.id = kCoreExcitationMixId, .leaf = 6, .leaf2 = kNoLeaf, .title = STR16("Excitation Mix"), .units = STR16("%"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Percent},
    {.id = kCoreExcitationDriveId, .leaf = 7, .leaf2 = kNoLeaf, .title = STR16("Excitation Drive"), .units = STR16("%"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Percent},
    {.id = kCoreExcitationAsymmetryId, .leaf = 8, .leaf2 = kNoLeaf, .title = STR16("Excitation Asymmetry"), .units = STR16("%"), .labels = nullptr, .labelCount = 0,
     .display = DisplayUnit::Percent},
    {.id = kCoreStiffnessId, .leaf = 62, .leaf2 = kNoLeaf, .title = STR16("Stiffness"), .units = STR16("%"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Percent},
    {.id = kCorePhaseId, .leaf = 63, .leaf2 = kNoLeaf, .title = STR16("Phase"), .units = STR16("%"), .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Percent},
}};

// ---- compile-time proofs (FR-002; tasks.md G7 rules a-d) ----

// (a) the rows cover leaves {0..8, 62, 63} exactly: each once, no other leaf.
static_assert(
    [] {
        constexpr std::array<std::uint8_t, 11> want{0, 1, 2, 3, 4, 5, 6, 7, 8, 62, 63};
        std::size_t used = 0;
        for (const auto& r : kCoreSoundRows) {
            used += (r.leaf != kNoLeaf ? 1u : 0u) + (r.leaf2 != kNoLeaf ? 1u : 0u);
        }
        if (used != want.size()) {
            return false;
        }
        for (const std::uint8_t w : want) {
            int hits = 0;
            for (const auto& r : kCoreSoundRows) {
                hits += (r.leaf == w ? 1 : 0) + (r.leaf2 == w ? 1 : 0);
            }
            if (hits != 1) {
                return false;
            }
        }
        return true;
    }(),
    "core pack: rows cover leaves {0..8, 62, 63} exactly once");

// (b) / (d) every list row's label count matches its leaf; no core row is a list, so every row is an F32 leaf.
static_assert(
    [] {
        return std::ranges::all_of(kCoreSoundRows, [&](const auto& r) {
            return !(r.labels != nullptr || r.labelCount != 0 || lawOf(r.leaf).kind != MorphState::Kind::F32);
        });
    }(),
    "core pack: every row is a continuous F32 leaf");

// (c) every ID lies in the core band [200, 300).
static_assert(
    [] {
        return std::ranges::all_of(kCoreSoundRows, [&](const auto& r) {
            return !(r.id < kCoreDepthId || r.id >= kCoreParamRangeEnd);
        });
    }(),
    "core pack: every ID lies in 200-299");

// Each row names the leaf the spec ID table says it does (morph_state.h:159-176).
static_assert(
    [] {
        constexpr std::array<std::string_view, 11> names{
            "recipe.depth",   "recipe.body",    "recipe.bodyCurvature", "recipe.bodyEmphasis",  "recipe.edge",
            "recipe.shift",   "excitation.mix", "excitation.drive",     "excitation.asymmetry", "stiffness",
            "phase"};
        for (std::size_t i = 0; i < kCoreSoundRows.size(); ++i) {
            if (std::string_view(lawOf(kCoreSoundRows[i].leaf).name) != names[i]) {
                return false;
            }
        }
        return true;
    }(),
    "core pack: row -> leaf names match the spec ID table");

// ---- FR-005 entry points ----

inline void registerCoreParams(Steinberg::Vst::ParameterContainer& parameters) {
    for (const auto& row : kCoreSoundRows) {
        registerSoundRow(parameters, row);
    }
}

/// Real-time safe. Returns true when id is a core row (handled).
inline bool handleCoreParamChange(EndpointParams& target, Steinberg::Vst::ParamID id, double value) noexcept {
    for (const auto& row : kCoreSoundRows) {
        if (row.id == id) {
            writeSoundRow(target, row, value);
            return true;
        }
    }
    return false;
}

inline Steinberg::tresult formatCoreParam(Steinberg::Vst::ParamID id, double value,
                                          Steinberg::Vst::String128 string) {
    for (const auto& row : kCoreSoundRows) {
        if (row.id == id) {
            return formatSoundRow(row, value, string);
        }
    }
    return Steinberg::kResultFalse;
}

}  // namespace Profundum
