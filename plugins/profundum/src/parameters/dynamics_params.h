#pragma once

// ==============================================================================
// Profundum - Mass dynamics section pack (IDs 700-701)   FR-001, FR-002, FR-005; plan 4.2
// ==============================================================================
// Two E-parameters over leaves 53 / 54 (transientAttackDb / transientSustainDb,
// morph_state.h:290-295). Ranges and defaults come from lawOf(leaf) /
// kDefaultLeafBits only (FR-002, FR-003); dB display with sign (FR-006).
// ==============================================================================

#include "parameters/sound_param_row.h"
#include "plugin_ids.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Profundum {

inline constexpr std::array<SoundParamRow, 2> kDynamicsSoundRows{{
    {kDynamicsTransientAttackId, 53, kNoLeaf, STR16("Transient Attack"), STR16("dB"), nullptr, 0, DisplayUnit::Db},
    {kDynamicsTransientSustainId, 54, kNoLeaf, STR16("Transient Sustain"), STR16("dB"), nullptr, 0, DisplayUnit::Db},
}};

// ---- compile-time proofs (FR-002; tasks.md G7 rules a-d) ----

// (a) the rows cover leaves {53, 54} exactly: each once, no other leaf.
static_assert(
    [] {
        constexpr std::array<std::uint8_t, 2> want{53, 54};
        std::size_t used = 0;
        for (const auto& r : kDynamicsSoundRows) {
            used += (r.leaf != kNoLeaf ? 1u : 0u) + (r.leaf2 != kNoLeaf ? 1u : 0u);
        }
        if (used != want.size()) {
            return false;
        }
        for (const std::uint8_t w : want) {
            int hits = 0;
            for (const auto& r : kDynamicsSoundRows) {
                hits += (r.leaf == w ? 1 : 0) + (r.leaf2 == w ? 1 : 0);
            }
            if (hits != 1) {
                return false;
            }
        }
        return true;
    }(),
    "dynamics pack: rows cover leaves {53, 54} exactly once");

// (b) / (d) no dynamics row is a list, so every row is a continuous F32 leaf.
static_assert(
    [] {
        for (const auto& r : kDynamicsSoundRows) {
            if (r.labels != nullptr || r.labelCount != 0 || lawOf(r.leaf).kind != MorphState::Kind::F32) {
                return false;
            }
        }
        return true;
    }(),
    "dynamics pack: every row is a continuous F32 leaf");

// (c) every ID lies in the dynamics band [700, 800).
static_assert(
    [] {
        for (const auto& r : kDynamicsSoundRows) {
            if (r.id < kDynamicsTransientAttackId || r.id >= kDynamicsParamRangeEnd) {
                return false;
            }
        }
        return true;
    }(),
    "dynamics pack: every ID lies in 700-799");

// Each row names the leaf the spec ID table says it does (morph_state.h:290-295).
static_assert(std::string_view(lawOf(kDynamicsSoundRows[0].leaf).name) == "transientAttackDb" &&
                  std::string_view(lawOf(kDynamicsSoundRows[1].leaf).name) == "transientSustainDb",
              "dynamics pack: row -> leaf names match the spec ID table");

// ---- FR-005 entry points ----

inline void registerDynamicsParams(Steinberg::Vst::ParameterContainer& parameters) {
    for (const auto& row : kDynamicsSoundRows) {
        registerSoundRow(parameters, row);
    }
}

/// Real-time safe. Returns true when id is a dynamics row (handled).
inline bool handleDynamicsParamChange(EndpointParams& target, Steinberg::Vst::ParamID id, double value) noexcept {
    for (const auto& row : kDynamicsSoundRows) {
        if (row.id == id) {
            writeSoundRow(target, row, value);
            return true;
        }
    }
    return false;
}

inline Steinberg::tresult formatDynamicsParam(Steinberg::Vst::ParamID id, double value,
                                              Steinberg::Vst::String128 string) {
    for (const auto& row : kDynamicsSoundRows) {
        if (row.id == id) {
            return formatSoundRow(row, value, string);
        }
    }
    return Steinberg::kResultFalse;
}

}  // namespace Profundum
