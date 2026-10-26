#pragma once

// ==============================================================================
// Profundum - Sound parameter aggregate table and route table
// Phase 11 T013 (FR-001, FR-002, FR-010, FR-012, FR-054; plan 4.3)
// ==============================================================================
// kSoundRows is the concatenation of the nine section packs' E rows in band
// order (104, 200, 300, 400, 500, 600, 700, 800, 900): 60 rows over the 62
// MorphState leaves (the two Sync Rate rows carry two leaves each).
//
// kParamRoutes maps every ID in [0, kNumRoutedIds) to the block that owns it.
// 84 IDs are routed: 8 from Phase 10 (0-3 Global, 100-103 Macro), the 4
// performance IDs (4-7), the 60 sound rows, the 4 morph-control IDs (105,
// 1000-1002) and the 8 note-behaviour IDs (410-417).
//
// The aggregate entry points dispatch on id / 100 to the pack wrappers
// (band 1 -> morph_params.h, which owns 104). Header-only.
// ==============================================================================

#include "parameters/anchor_params.h"
#include "parameters/character_params.h"
#include "parameters/core_params.h"
#include "parameters/dynamics_params.h"
#include "parameters/field_params.h"
#include "parameters/fold_shape_params.h"
#include "parameters/morph_params.h"
#include "parameters/motion_params.h"
#include "parameters/note_params.h"
#include "parameters/sound_param_row.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Profundum {

// ------------------------------------------------------------------------------
// kSoundRows (plan 4.3)
// ------------------------------------------------------------------------------

namespace sound_table_detail {

template <std::size_t... Ns>
[[nodiscard]] constexpr std::array<SoundParamRow, (Ns + ...)> concatRows(
    const std::array<SoundParamRow, Ns>&... packs) noexcept {
    std::array<SoundParamRow, (Ns + ...)> out{};
    std::size_t i = 0;
    const auto append = [&out, &i](const auto& pack) {
        for (const auto& r : pack) {
            out[i++] = r;
        }
    };
    (append(packs), ...);
    return out;
}

}  // namespace sound_table_detail

inline constexpr std::array<SoundParamRow, 60> kSoundRows = sound_table_detail::concatRows(
    kMorphSoundRows, kCoreSoundRows, kAnchorSoundRows, kNoteRows, kFoldShapeSoundRows, kCharacterSoundRows,
    kDynamicsSoundRows, kFieldRows, kMotionRows);

// ------------------------------------------------------------------------------
// Route table (plan 4.3)
// ------------------------------------------------------------------------------

enum class RouteKind : std::uint8_t { None, Global, Macro, Performance, Sound, Note, MorphControl };

struct ParamRoute {
    RouteKind kind;
    std::uint8_t index;  // index into kSoundRows for Sound; 0 otherwise
};

namespace sound_table_detail {

[[nodiscard]] constexpr std::array<ParamRoute, kNumRoutedIds> buildParamRoutes() noexcept {
    std::array<ParamRoute, kNumRoutedIds> t{};  // every entry {None, 0}
    const auto setRange = [&t](Steinberg::Vst::ParamID first, Steinberg::Vst::ParamID last, RouteKind kind) {
        for (Steinberg::Vst::ParamID id = first; id <= last; ++id) {
            t[id] = ParamRoute{kind, 0};
        }
    };
    setRange(kOutputGainId, kOutputCeilingId, RouteKind::Global);           // 0-3
    setRange(kPitchBendId, kChannelPressureId, RouteKind::Performance);     // 4-7
    setRange(kMacroWeightId, kMacroCleanViolentId, RouteKind::Macro);       // 100-103
    setRange(kMorphPositionId, kMorphPositionId, RouteKind::MorphControl);  // 105
    setRange(kMorphAnchorModeId, kMorphRampTimeId, RouteKind::MorphControl);  // 1000-1002
    setRange(kNoteMonoPriorityId, kNotePitchBendRangeId, RouteKind::Note);  // 410-417
    for (std::size_t i = 0; i < kSoundRows.size(); ++i) {
        t[kSoundRows[i].id] = ParamRoute{RouteKind::Sound, static_cast<std::uint8_t>(i)};
    }
    return t;
}

}  // namespace sound_table_detail

inline constexpr std::array<ParamRoute, kNumRoutedIds> kParamRoutes = sound_table_detail::buildParamRoutes();

// ------------------------------------------------------------------------------
// Compile-time proofs (plan 4.3)
// ------------------------------------------------------------------------------

namespace sound_table_detail {

/// Every kSoundRows ID is unique, routable, and lies in an E band (104, or 200-999).
[[nodiscard]] constexpr bool rowIdsUniqueAndInBand() noexcept {
    for (std::size_t i = 0; i < kSoundRows.size(); ++i) {
        const Steinberg::Vst::ParamID id = kSoundRows[i].id;
        if (id >= static_cast<Steinberg::Vst::ParamID>(kNumRoutedIds)) {
            return false;
        }
        const bool inBand = id == static_cast<Steinberg::Vst::ParamID>(kMassId) ||
                            (id >= static_cast<Steinberg::Vst::ParamID>(kCoreDepthId) &&
                             id < static_cast<Steinberg::Vst::ParamID>(kMotionParamRangeEnd));
        if (!inBand) {
            return false;
        }
        for (std::size_t j = i + 1; j < kSoundRows.size(); ++j) {
            if (kSoundRows[j].id == id) {
                return false;
            }
        }
    }
    return true;
}

/// The union of leaf / leaf2 over the 60 rows is {0..61}, each exactly once.
[[nodiscard]] constexpr bool leavesCoveredOnce() noexcept {
    std::array<int, kNumLeaves> hits{};
    for (const auto& r : kSoundRows) {
        if (static_cast<std::size_t>(r.leaf) >= kNumLeaves) {
            return false;
        }
        ++hits[r.leaf];
        if (r.leaf2 != kNoLeaf) {
            if (static_cast<std::size_t>(r.leaf2) >= kNumLeaves) {
                return false;
            }
            ++hits[r.leaf2];
        }
    }
    for (const int h : hits) {
        if (h != 1) {
            return false;
        }
    }
    return true;
}

/// Every sound row's route points back to that row (no sound ID overwrote, or was overwritten by, another block).
[[nodiscard]] constexpr bool soundRoutesPointBack() noexcept {
    for (std::size_t i = 0; i < kSoundRows.size(); ++i) {
        const ParamRoute& r = kParamRoutes[kSoundRows[i].id];
        if (r.kind != RouteKind::Sound || static_cast<std::size_t>(r.index) != i) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr std::size_t routedCount() noexcept {
    std::size_t n = 0;
    for (const auto& r : kParamRoutes) {
        n += r.kind != RouteKind::None ? 1u : 0u;
    }
    return n;
}

/// FR-054: the routed IDs in 400-499 are exactly {400-403, 410-417, 420-426}.
[[nodiscard]] constexpr bool noteBandExact() noexcept {
    for (Steinberg::Vst::ParamID id = 400; id < 500; ++id) {
        const bool want = (id >= 400 && id <= 403) || (id >= 410 && id <= 417) || (id >= 420 && id <= 426);
        const bool routed = kParamRoutes[id].kind != RouteKind::None;
        if (want != routed) {
            return false;
        }
    }
    return true;
}

}  // namespace sound_table_detail

static_assert(kSoundRows.size() == 60, "plan 4.3: 60 sound rows");
static_assert(sound_table_detail::rowIdsUniqueAndInBand(), "plan 4.3: sound row IDs unique and in their bands");
static_assert(sound_table_detail::leavesCoveredOnce(), "plan 4.3: sound rows cover leaves {0..61} exactly once");
static_assert(sound_table_detail::soundRoutesPointBack(), "plan 4.3: every sound row is routed to itself");
static_assert(sound_table_detail::routedCount() == 84, "plan 4.3: 84 routed IDs (8 Phase 10 + 76 new)");
static_assert(sound_table_detail::noteBandExact(), "FR-054: routed 400-499 IDs are {400-403, 410-417, 420-426}");

// ------------------------------------------------------------------------------
// Aggregate entry points (dispatch on id / 100 to the pack wrappers)
// ------------------------------------------------------------------------------

/// FR-010 / FR-011 / FR-012: writes a sound row's value into the target endpoint. Returns false (and writes
/// nothing) for an ID that is not a sound row. v is already finite and in [0, 1]. Real-time safe.
inline bool handleSoundParamChange(EndpointParams& target, Steinberg::Vst::ParamID id, double v) noexcept {
    switch (id / 100) {
        case 1: return handleMorphParamChange(target, id, v);
        case 2: return handleCoreParamChange(target, id, v);
        case 3: return handleAnchorParamChange(target, id, v);
        case 4: return handleNoteParamChange(target, id, v);
        case 5: return handleFoldShapeParamChange(target, id, v);
        case 6: return handleCharacterParamChange(target, id, v);
        case 7: return handleDynamicsParamChange(target, id, v);
        case 8: return handleFieldParamChange(target, id, v);
        case 9: return handleMotionParamChange(target, id, v);
        default: return false;
    }
}

/// FR-001: registers the 60 sound rows, the nine packs in band order (the note pack's E rows only; 410-417
/// register through registerNoteParams).
inline void registerSoundParams(Steinberg::Vst::ParameterContainer& parameters) {
    registerMorphParams(parameters);
    registerCoreParams(parameters);
    registerAnchorParams(parameters);
    registerNoteSoundRows(parameters);
    registerFoldShapeParams(parameters);
    registerCharacterParams(parameters);
    registerDynamicsParams(parameters);
    registerFieldParams(parameters);
    registerMotionParams(parameters);
}

/// FR-006: display of a sound row's value; kResultFalse for list rows and IDs that are not sound rows.
inline Steinberg::tresult formatSoundParam(Steinberg::Vst::ParamID id, double v, Steinberg::Vst::String128 string) {
    switch (id / 100) {
        case 1: return formatMorphParam(id, v, string);
        case 2: return formatCoreParam(id, v, string);
        case 3: return formatAnchorParam(id, v, string);
        case 4: return formatNoteParam(id, v, string);
        case 5: return formatFoldShapeParam(id, v, string);
        case 6: return formatCharacterParam(id, v, string);
        case 7: return formatDynamicsParam(id, v, string);
        case 8: return formatFieldParam(id, v, string);
        case 9: return formatMotionParam(id, v, string);
        default: return Steinberg::kResultFalse;
    }
}

/// The sound row of an ID (via kParamRoutes), or nullptr.
[[nodiscard]] inline const SoundParamRow* soundRowOf(Steinberg::Vst::ParamID id) noexcept {
    if (id >= static_cast<Steinberg::Vst::ParamID>(kNumRoutedIds)) {
        return nullptr;
    }
    const ParamRoute& r = kParamRoutes[id];
    if (r.kind != RouteKind::Sound) {
        return nullptr;
    }
    return &kSoundRows[r.index];
}

}  // namespace Profundum
