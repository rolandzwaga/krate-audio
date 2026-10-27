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

namespace Profundum {

inline constexpr std::array<SoundParamRow, 11> kCoreSoundRows{{
    {kCoreDepthId, 0, kNoLeaf, STR16("Depth"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kCoreBodyId, 1, kNoLeaf, STR16("Body"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kCoreBodyCurvatureId, 2, kNoLeaf, STR16("Body Curvature"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kCoreBodyEmphasisId, 3, kNoLeaf, STR16("Body Emphasis"), STR16("%"), nullptr, 0, DisplayUnit::BipolarPercent},
    {kCoreEdgeId, 4, kNoLeaf, STR16("Edge"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kCoreShiftId, 5, kNoLeaf, STR16("Spectral Shift"), STR16("%"), nullptr, 0, DisplayUnit::BipolarPercent},
    {kCoreExcitationMixId, 6, kNoLeaf, STR16("Excitation Mix"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kCoreExcitationDriveId, 7, kNoLeaf, STR16("Excitation Drive"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kCoreExcitationAsymmetryId, 8, kNoLeaf, STR16("Excitation Asymmetry"), STR16("%"), nullptr, 0,
     DisplayUnit::Percent},
    {kCoreStiffnessId, 62, kNoLeaf, STR16("Stiffness"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kCorePhaseId, 63, kNoLeaf, STR16("Phase"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
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
        for (const auto& r : kCoreSoundRows) {
            if (r.labels != nullptr || r.labelCount != 0 || lawOf(r.leaf).kind != MorphState::Kind::F32) {
                return false;
            }
        }
        return true;
    }(),
    "core pack: every row is a continuous F32 leaf");

// (c) every ID lies in the core band [200, 300).
static_assert(
    [] {
        for (const auto& r : kCoreSoundRows) {
            if (r.id < kCoreDepthId || r.id >= kCoreParamRangeEnd) {
                return false;
            }
        }
        return true;
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
