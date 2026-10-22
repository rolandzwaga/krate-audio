#pragma once

// ==============================================================================
// Profundum - Anchor section pack (IDs 300-306)   FR-001, FR-002, FR-005; plan 4.2
// ==============================================================================
// Seven E-parameters over the anchor leaves 55-61 (morph_state.h:297-315).
// Ranges, laws, enumerator counts and defaults come from lawOf(leaf) /
// kDefaultLeafBits only (FR-002, FR-003). The list labels follow the
// AnchorOscillator enums (anchor_oscillator.h:57-58), in enumerator order.
// ==============================================================================

#include "parameters/sound_param_row.h"
#include "plugin_ids.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

namespace Profundum {

/// AnchorOscillator::Waveform { Sine, Triangle, RoundedSine, AsymmetricSine, OctaveDownSine } (anchor_oscillator.h:57).
inline constexpr const Steinberg::Vst::TChar* kAnchorWaveformLabels[] = {
    STR16("Sine"), STR16("Triangle"), STR16("Rounded Sine"), STR16("Asymmetric Sine"), STR16("Octave Down")};

/// AnchorOscillator::Mode { Lock, Follow, Punch, Ghost } (anchor_oscillator.h:58).
inline constexpr const Steinberg::Vst::TChar* kAnchorModeLabels[] = {STR16("Lock"), STR16("Follow"), STR16("Punch"),
                                                                     STR16("Ghost")};

inline constexpr std::array<SoundParamRow, 7> kAnchorSoundRows{{
    {kAnchorWaveformId, 55, kNoLeaf, STR16("Anchor Waveform"), STR16(""), kAnchorWaveformLabels,
     static_cast<std::uint8_t>(std::size(kAnchorWaveformLabels)), DisplayUnit::List},
    {kAnchorModeId, 56, kNoLeaf, STR16("Anchor Mode"), STR16(""), kAnchorModeLabels,
     static_cast<std::uint8_t>(std::size(kAnchorModeLabels)), DisplayUnit::List},
    {kAnchorLevelId, 57, kNoLeaf, STR16("Anchor Level"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
    {kAnchorPunchBoostId, 58, kNoLeaf, STR16("Punch Boost"), STR16("dB"), nullptr, 0, DisplayUnit::Db},
    {kAnchorPunchTimeId, 59, kNoLeaf, STR16("Punch Time"), STR16("ms"), nullptr, 0, DisplayUnit::Time},
    {kAnchorGhostTimeId, 60, kNoLeaf, STR16("Ghost Time"), STR16("ms"), nullptr, 0, DisplayUnit::Time},
    {kAnchorFoldFeedId, 61, kNoLeaf, STR16("Anchor Fold Feed"), STR16("%"), nullptr, 0, DisplayUnit::Percent},
}};

// ---- compile-time proofs (FR-002; tasks.md G7 rules a-d) ----

// (a) the rows cover leaves {55..61} exactly: each once, no other leaf.
static_assert(
    [] {
        constexpr std::array<std::uint8_t, 7> want{55, 56, 57, 58, 59, 60, 61};
        std::size_t used = 0;
        for (const auto& r : kAnchorSoundRows) {
            used += (r.leaf != kNoLeaf ? 1u : 0u) + (r.leaf2 != kNoLeaf ? 1u : 0u);
        }
        if (used != want.size()) {
            return false;
        }
        for (const std::uint8_t w : want) {
            int hits = 0;
            for (const auto& r : kAnchorSoundRows) {
                hits += (r.leaf == w ? 1 : 0) + (r.leaf2 == w ? 1 : 0);
            }
            if (hits != 1) {
                return false;
            }
        }
        return true;
    }(),
    "anchor pack: rows cover leaves {55..61} exactly once");

// (b) / (d) a non-F32 leaf is a list whose label count is maxEnum + 1 (2 for a Bool); an F32 leaf has no labels.
static_assert(
    [] {
        for (const auto& r : kAnchorSoundRows) {
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
    "anchor pack: list label counts equal lawOf(leaf).maxEnum + 1");

// (c) every ID lies in the anchor band [300, 400).
static_assert(
    [] {
        for (const auto& r : kAnchorSoundRows) {
            if (r.id < kAnchorWaveformId || r.id >= kAnchorParamRangeEnd) {
                return false;
            }
        }
        return true;
    }(),
    "anchor pack: every ID lies in 300-399");

// Each row names the leaf the spec ID table says it does (morph_state.h:297-315).
static_assert(
    [] {
        constexpr std::array<std::string_view, 7> names{
            "anchor.waveform",   "anchor.mode",       "anchor.level",  "anchor.punchBoostDb",
            "anchor.punchTimeMs", "anchor.ghostTimeMs", "anchorFoldFeed"};
        for (std::size_t i = 0; i < kAnchorSoundRows.size(); ++i) {
            if (std::string_view(lawOf(kAnchorSoundRows[i].leaf).name) != names[i]) {
                return false;
            }
        }
        return true;
    }(),
    "anchor pack: row -> leaf names match the spec ID table");

// ---- FR-005 entry points ----

inline void registerAnchorParams(Steinberg::Vst::ParameterContainer& parameters) {
    for (const auto& row : kAnchorSoundRows) {
        registerSoundRow(parameters, row);
    }
}

/// Real-time safe. Returns true when id is an anchor row (handled).
inline bool handleAnchorParamChange(EndpointParams& target, Steinberg::Vst::ParamID id, double value) noexcept {
    for (const auto& row : kAnchorSoundRows) {
        if (row.id == id) {
            writeSoundRow(target, row, value);
            return true;
        }
    }
    return false;
}

/// List rows return kResultFalse (the StringListParameter formats itself).
inline Steinberg::tresult formatAnchorParam(Steinberg::Vst::ParamID id, double value,
                                            Steinberg::Vst::String128 string) {
    for (const auto& row : kAnchorSoundRows) {
        if (row.id == id) {
            return formatSoundRow(row, value, string);
        }
    }
    return Steinberg::kResultFalse;
}

}  // namespace Profundum
