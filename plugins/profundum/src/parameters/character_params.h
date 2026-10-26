#pragma once

// ==============================================================================
// Profundum - Character section pack (IDs 600-602)   FR-001, FR-002, FR-005, FR-009; plan 4.2
// ==============================================================================
// Three E-parameters over leaves 11-13 (characterAlgorithm / Amount / Tone,
// morph_state.h:182-189). FR-009 fixes the list labels "Tube, Wavefold, Rectify,
// Bite, Crush, Fracture", in BassCharacterStage::Algorithm order
// (bass_character_stage.h:78: Tube, Fold, Rectify, Bite, Crush, Fracture).
// Ranges, enumerator count and defaults come from lawOf(leaf) / kDefaultLeafBits.
// ==============================================================================

#include "parameters/sound_param_row.h"
#include "plugin_ids.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

namespace Profundum {

/// FR-009 (permanent once shipped). Index i is BassCharacterStage::Algorithm i (bass_character_stage.h:78).
inline constexpr const Steinberg::Vst::TChar* kCharacterLabels[] = {STR16("Tube"),  STR16("Wavefold"),
                                                                    STR16("Rectify"), STR16("Bite"),
                                                                    STR16("Crush"), STR16("Fracture")};

inline constexpr std::array<SoundParamRow, 3> kCharacterSoundRows{{
    {kCharacterId, 11, kNoLeaf, STR16("Character"), STR16(""), kCharacterLabels,
     static_cast<std::uint8_t>(std::size(kCharacterLabels)), DisplayUnit::List},
    {kCharacterAmountId, 12, kNoLeaf, STR16("Character Amount"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kCharacterToneId, 13, kNoLeaf, STR16("Character Tone"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
}};

// ---- compile-time proofs (FR-002; tasks.md G7 rules a-d) ----

// (a) the rows cover leaves {11..13} exactly: each once, no other leaf.
static_assert(
    [] {
        constexpr std::array<std::uint8_t, 3> want{11, 12, 13};
        std::size_t used = 0;
        for (const auto& r : kCharacterSoundRows) {
            used += (r.leaf != kNoLeaf ? 1u : 0u) + (r.leaf2 != kNoLeaf ? 1u : 0u);
        }
        if (used != want.size()) {
            return false;
        }
        for (const std::uint8_t w : want) {
            int hits = 0;
            for (const auto& r : kCharacterSoundRows) {
                hits += (r.leaf == w ? 1 : 0) + (r.leaf2 == w ? 1 : 0);
            }
            if (hits != 1) {
                return false;
            }
        }
        return true;
    }(),
    "character pack: rows cover leaves {11..13} exactly once");

// (b) / (d) a non-F32 leaf is a list whose label count is maxEnum + 1 (2 for a Bool); an F32 leaf has no labels.
static_assert(
    [] {
        for (const auto& r : kCharacterSoundRows) {
            const auto& f = lawOf(r.leaf);
            if (f.kind == MorphState::Kind::F32) {
                if (r.labels != nullptr || r.labelCount != 0 || r.display == DisplayUnit::List) {
                    return false;
                }
            } else {
                if (r.labels == nullptr || r.display != DisplayUnit::List ||
                    static_cast<int>(r.labelCount) != static_cast<int>(f.maxEnum) + 1) {
                    return false;
                }
                if (f.kind == MorphState::Kind::Bool && r.labelCount != 2) {
                    return false;
                }
            }
        }
        return true;
    }(),
    "character pack: list label counts equal lawOf(leaf).maxEnum + 1");

// (c) every ID lies in the character band [600, 700).
static_assert(
    [] {
        for (const auto& r : kCharacterSoundRows) {
            if (r.id < kCharacterId || r.id >= kCharacterParamRangeEnd) {
                return false;
            }
        }
        return true;
    }(),
    "character pack: every ID lies in 600-699");

// Each row names the leaf the spec ID table says it does (morph_state.h:182-189).
static_assert(std::string_view(lawOf(kCharacterSoundRows[0].leaf).name) == "characterAlgorithm" &&
                  std::string_view(lawOf(kCharacterSoundRows[1].leaf).name) == "characterAmount" &&
                  std::string_view(lawOf(kCharacterSoundRows[2].leaf).name) == "characterTone",
              "character pack: row -> leaf names match the spec ID table");

// ---- FR-005 entry points ----

inline void registerCharacterParams(Steinberg::Vst::ParameterContainer& parameters) {
    for (const auto& row : kCharacterSoundRows) {
        registerSoundRow(parameters, row);
    }
}

/// Real-time safe. Returns true when id is a character row (handled).
inline bool handleCharacterParamChange(EndpointParams& target, Steinberg::Vst::ParamID id, double value) noexcept {
    for (const auto& row : kCharacterSoundRows) {
        if (row.id == id) {
            writeSoundRow(target, row, value);
            return true;
        }
    }
    return false;
}

/// List rows return kResultFalse (the StringListParameter formats itself).
inline Steinberg::tresult formatCharacterParam(Steinberg::Vst::ParamID id, double value,
                                               Steinberg::Vst::String128 string) {
    for (const auto& row : kCharacterSoundRows) {
        if (row.id == id) {
            return formatSoundRow(row, value, string);
        }
    }
    return Steinberg::kResultFalse;
}

}  // namespace Profundum
