#pragma once

// ==============================================================================
// Profundum - Morph pack: Mass E-row (104), morph control (105, 1000-1002), CompareMode
// Phase 11 T012 (FR-002, FR-004, FR-005, FR-009, FR-031, FR-040 - FR-046; plan 4.2)
// ==============================================================================
//   104  Mass               E-row, leaf 52 (kLawTable "mass"), [0, 100] linear, Mass100 display
//   105  Morph Position     [0, 1] linear, default 0; stored through sanitizeMorphPosition (Risk R10)
//   1000 Anchor Morph Mode  list 3 "Hold, Blend, Yield" == engine Lock, Blend, Punch; default Blend
//   1001 Edit Target        list 2 "A, B"; kIsList ONLY (not kCanAutomate, FR-004); persisted; default A
//   1002 Morph Ramp Time    [5, 2000] ms log, default 40 (BassMorphEngine::kDefaultPositionRampMs)
//
// Stream (spec bytes 68-83, 16 bytes): position f32, ramp ms f32, Anchor Morph Mode i32, Edit Target i32.
//
// The Mass range comes from lawOf(52) only (FR-002): no morphed range literal lives here.
// The morph-control values are not leaves; their defaults are the engine's own constants
// (bass_morph_engine.h:67-73), so the per-block push changes nothing on a fresh instance (P-8).
// ==============================================================================

#include "plugin_ids.h"
#include "parameters/sound_param_row.h"

#include "ui/parameter_helpers.h"  // plugins/shared/src/ui/parameter_helpers.h:98, :118

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/bass_morph_engine.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace Profundum {

// ------------------------------------------------------------------------------
// Mass E-row (104 -> leaf 52)
// ------------------------------------------------------------------------------

inline constexpr std::uint8_t kMassLeaf = 52;

inline constexpr std::array<SoundParamRow, 1> kMorphSoundRows{{
    SoundParamRow{.id = kMassId, .leaf = kMassLeaf, .leaf2 = kNoLeaf, .title = STR16("Mass"), .units = STR16(""),
                  .labels = nullptr, .labelCount = 0, .display = DisplayUnit::Mass100},
}};

/// Pack proofs (tasks.md G7 (a)-(d)), evaluated at compile time.
namespace morph_pack_detail {

/// (a) the leaf / leaf2 values over the rows are exactly {52}: no duplicate, no gap.
[[nodiscard]] constexpr bool morphRowsCoverLeaves() noexcept {
    std::size_t count = 0;
    for (const auto& r : kMorphSoundRows) {
        if (r.leaf != kMassLeaf) {
            return false;
        }
        ++count;
        if (r.leaf2 != kNoLeaf) {
            return false;
        }
    }
    return count == 1;
}

/// (b) every list row has labelCount == maxEnum + 1 (a Sync Rate row: the 30-entry product);
/// (d) every Bool row has labelCount == 2; continuous rows carry no labels.
[[nodiscard]] constexpr bool morphRowsLabelsMatch() noexcept {
    for (const auto& r : kMorphSoundRows) {
        const FieldInfo& f = lawOf(r.leaf);
        if (r.leaf2 != kNoLeaf) {
            const int product = (static_cast<int>(f.maxEnum) + 1) * (static_cast<int>(lawOf(r.leaf2).maxEnum) + 1);
            if (r.labelCount != product || product != Krate::DSP::kNoteValueDropdownCount || r.labels == nullptr) {
                return false;
            }
            continue;
        }
        if (f.kind == MorphState::Kind::F32) {
            if (r.labels != nullptr || r.labelCount != 0 || r.display == DisplayUnit::List) {
                return false;
            }
            continue;
        }
        if (r.labels == nullptr || r.labelCount != static_cast<int>(f.maxEnum) + 1) {
            return false;
        }
        if (f.kind == MorphState::Kind::Bool && r.labelCount != 2) {
            return false;
        }
    }
    return true;
}

/// (c) every row ID lies in the pack's band: 104-199 (the macro band; Mass sits there per roadmap line 763).
[[nodiscard]] constexpr bool morphRowsInBand() noexcept {
    for (const auto& r : kMorphSoundRows) {
        if (r.id < static_cast<Steinberg::Vst::ParamID>(kMassId) ||
            r.id >= static_cast<Steinberg::Vst::ParamID>(kMacroParamRangeEnd)) {
            return false;
        }
    }
    return true;
}

}  // namespace morph_pack_detail

static_assert(morph_pack_detail::morphRowsCoverLeaves(), "T012 (a): the morph pack covers leaf {52} exactly");
static_assert(morph_pack_detail::morphRowsLabelsMatch(), "T012 (b)/(d): list label counts match lawOf(leaf)");
static_assert(morph_pack_detail::morphRowsInBand(), "T012 (c): every morph-pack row ID lies in 104-199");
static_assert(std::string_view(lawOf(kMassLeaf).name) == "mass", "leaf 52 is the kLawTable Mass row");
static_assert(lawOf(kMassLeaf).kind == MorphState::Kind::F32 && lawOf(kMassLeaf).law == MorphState::Law::Linear,
              "Mass is a linear F32 leaf (spec ID table: [0, 100] linear)");
static_assert(lawOf(52).lo == 0.0f && lawOf(52).hi == 100.0f, "T012: Mass 104 spans [0, 100] (Mass100 display)");

/// Thin wrappers over the generic row path (plan 4.2).
inline void registerMorphParams(Steinberg::Vst::ParameterContainer& parameters) {
    for (const auto& r : kMorphSoundRows) {
        registerSoundRow(parameters, r);
    }
}

/// Returns true if the ID is a row of this pack (and the write was applied).
inline bool handleMorphParamChange(EndpointParams& target, Steinberg::Vst::ParamID id, double value) noexcept {
    for (const auto& r : kMorphSoundRows) {
        if (r.id == id) {
            writeSoundRow(target, r, value);
            return true;
        }
    }
    return false;
}

inline Steinberg::tresult formatMorphParam(Steinberg::Vst::ParamID id, double value,
                                           Steinberg::Vst::String128 string) {
    for (const auto& r : kMorphSoundRows) {
        if (r.id == id) {
            return formatSoundRow(r, value, string);
        }
    }
    return Steinberg::kResultFalse;
}

// ------------------------------------------------------------------------------
// Compare (FR-031). Not persisted, not a parameter; the processor owns it.
// ------------------------------------------------------------------------------

enum class CompareMode : std::uint8_t { AB = 0, A = 1, B = 2 };

// ------------------------------------------------------------------------------
// Morph control (105, 1000-1002)
// ------------------------------------------------------------------------------

/// Morph Ramp Time (1002): [5, 2000] ms log (plan 4.1 non-leaf laws).
inline constexpr PlainLaw kMorphRampLaw{static_cast<double>(Krate::DSP::BassMorphEngine::kMinPositionRampMs),
                                        static_cast<double>(Krate::DSP::BassMorphEngine::kMaxPositionRampMs), 0.0,
                                        true};
static_assert(Krate::DSP::BassMorphEngine::kMinPositionRampMs == 5.0f &&
                  Krate::DSP::BassMorphEngine::kMaxPositionRampMs == 2000.0f,
              "spec ID table: Morph Ramp Time [5, 2000] ms (bass_morph_engine.h:67-68)");

inline constexpr int kNumAnchorMorphModes = 3;  // Hold, Blend, Yield
inline constexpr int kNumEditTargets = 2;       // A, B
static_assert(static_cast<int>(Krate::DSP::BassMorphEngine::AnchorMorphMode::Lock) == 0 &&
                  static_cast<int>(Krate::DSP::BassMorphEngine::AnchorMorphMode::Blend) == 1 &&
                  static_cast<int>(Krate::DSP::BassMorphEngine::AnchorMorphMode::Punch) == kNumAnchorMorphModes - 1,
              "list index == engine enumerator (bass_morph_engine.h:49): Hold = Lock, Blend, Yield = Punch");
static_assert(static_cast<int>(Krate::DSP::BassMorphEngine::Endpoint::A) == 0 &&
                  static_cast<int>(Krate::DSP::BassMorphEngine::Endpoint::B) == kNumEditTargets - 1,
              "Edit Target index == BassMorphEngine::Endpoint (bass_morph_engine.h:48)");

/// FR-009: the permanent Anchor Morph Mode strings.
inline constexpr std::array<const Steinberg::Vst::TChar*, kNumAnchorMorphModes> kAnchorMorphModeLabels{
    STR16("Hold"), STR16("Blend"), STR16("Yield")};
inline constexpr std::array<const Steinberg::Vst::TChar*, kNumEditTargets> kEditTargetLabels{STR16("A"), STR16("B")};

inline constexpr float kDefaultMorphPosition = 0.0f;
inline constexpr float kDefaultMorphRampMs = Krate::DSP::BassMorphEngine::kDefaultPositionRampMs;  // 40
inline constexpr int kDefaultAnchorMorphModeIndex =
    static_cast<int>(Krate::DSP::BassMorphEngine::kDefaultAnchorMorphMode);  // Blend = 1
inline constexpr int kDefaultEditTarget = 0;                                  // Endpoint::A
static_assert(kDefaultAnchorMorphModeIndex == 1, "plan 4.2: the default Anchor Morph Mode is Blend (index 1)");

struct MorphControlParams {
    std::atomic<float> position{kDefaultMorphPosition};
    std::atomic<float> rampMs{kDefaultMorphRampMs};
    std::atomic<int> anchorMorphMode{kDefaultAnchorMorphModeIndex};
    std::atomic<int> editTarget{kDefaultEditTarget};
};

/// Plan 4.2 / Risk R10: the engine's own position rule (BassMorphEngine::setPosition, bass_morph_engine.h:139-146),
/// applied before storing, so the stored position is bit-identical to the engine's targetPosition().
/// Non-finite -> previous; p < kDenormalThreshold (1e-15f: -0, negatives, denormals, tiny normals) -> +0;
/// p >= 1 -> 1; else p.
[[nodiscard]] inline float sanitizeMorphPosition(float p, float previous) noexcept {
    if (!Krate::DSP::detail::isFinite(p)) {
        return previous;
    }
    if (p < Krate::DSP::kDenormalThreshold) {
        return 0.0f;
    }
    if (p >= 1.0f) {
        return 1.0f;
    }
    return p;
}

/// Morph Ramp Time: normalised -> stored ms (float), inside [5, 2000].
[[nodiscard]] inline float morphRampMsFromNormalized(double v) noexcept {
    return std::clamp(static_cast<float>(plainFromNormalized(kMorphRampLaw, v)),
                      Krate::DSP::BassMorphEngine::kMinPositionRampMs,
                      Krate::DSP::BassMorphEngine::kMaxPositionRampMs);
}

/// value arrives already finite and clamped to [0, 1] (processParameterChanges). Relaxed stores.
/// Returns true if the ID belongs to the morph-control set (105, 1000-1002).
inline bool handleMorphControlParamChange(MorphControlParams& params, Steinberg::Vst::ParamID id,
                                          Steinberg::Vst::ParamValue value) noexcept {
    switch (id) {
        case kMorphPositionId: {
            const float prev = params.position.load(std::memory_order_relaxed);
            params.position.store(sanitizeMorphPosition(static_cast<float>(value), prev), std::memory_order_relaxed);
            return true;
        }
        case kMorphAnchorModeId:
            params.anchorMorphMode.store(indexFromNormalized(value, kNumAnchorMorphModes - 1),
                                         std::memory_order_relaxed);
            return true;
        case kMorphEditTargetId:
            params.editTarget.store(indexFromNormalized(value, kNumEditTargets - 1), std::memory_order_relaxed);
            return true;
        case kMorphRampTimeId:
            params.rampMs.store(morphRampMsFromNormalized(value), std::memory_order_relaxed);
            return true;
        default:
            return false;
    }
}

/// FR-001 / FR-004. 105 and 1002 continuous kCanAutomate; 1000 a list (kCanAutomate | kIsList) with its registered
/// default pinned by hand (createDropdownParameterWithDefault sets only the current value, parameter_helpers.h:118);
/// 1001 a list with flags = kIsList ONLY (not automatable).
inline void registerMorphControlParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Morph Position"), STR16("%"), 0, static_cast<ParamValue>(kDefaultMorphPosition),
                            ParameterInfo::kCanAutomate, static_cast<Steinberg::int32>(kMorphPositionId));

    auto* mode = Krate::Plugins::createDropdownParameterWithDefault(
        STR16("Anchor Morph Mode"), kMorphAnchorModeId, static_cast<std::int32_t>(kDefaultAnchorMorphModeIndex),
        kAnchorMorphModeLabels.data(), kNumAnchorMorphModes);
    mode->getInfo().defaultNormalizedValue =
        static_cast<double>(kDefaultAnchorMorphModeIndex) / static_cast<double>(kNumAnchorMorphModes - 1);
    parameters.addParameter(mode);

    auto* editTarget = Krate::Plugins::createDropdownParameter(STR16("Edit Target"), kMorphEditTargetId,
                                                               kEditTargetLabels.data(), kNumEditTargets);
    editTarget->getInfo().flags = ParameterInfo::kIsList;  // FR-004: NOT kCanAutomate
    editTarget->getInfo().defaultNormalizedValue = static_cast<double>(kDefaultEditTarget);
    parameters.addParameter(editTarget);

    parameters.addParameter(STR16("Morph Ramp Time"), STR16("ms"), 0,
                            normalizedFromPlain(kMorphRampLaw, static_cast<double>(kDefaultMorphRampMs)),
                            ParameterInfo::kCanAutomate, static_cast<Steinberg::int32>(kMorphRampTimeId));
}

/// FR-006 (ASCII). 105 "%.0f%%"; 1002 the Time rule of plan 4.3 on the stored float; the two lists format
/// themselves (kResultFalse).
inline Steinberg::tresult formatMorphControlParam(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
                                                  Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    char8 text[64];
    if (id == kMorphPositionId) {
        const float p = sanitizeMorphPosition(static_cast<float>(value), 0.0f);
        std::snprintf(text, sizeof(text), "%.0f%%", 100.0 * static_cast<double>(p));
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    if (id == kMorphRampTimeId) {
        const double x = static_cast<double>(morphRampMsFromNormalized(value));
        const double r = std::round(10.0 * x) / 10.0;
        if (r < 1000.0) {
            std::snprintf(text, sizeof(text), "%.1f ms", x);
        } else {
            std::snprintf(text, sizeof(text), "%.2f s", x / 1000.0);
        }
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    return kResultFalse;
}

/// Spec bytes 68-83: position f32, ramp ms f32, Anchor Morph Mode i32, Edit Target i32 (16 bytes).
inline void saveMorphControlParams(const MorphControlParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.position.load(std::memory_order_relaxed));
    streamer.writeFloat(params.rampMs.load(std::memory_order_relaxed));
    streamer.writeInt32(static_cast<Steinberg::int32>(params.anchorMorphMode.load(std::memory_order_relaxed)));
    streamer.writeInt32(static_cast<Steinberg::int32>(params.editTarget.load(std::memory_order_relaxed)));
}

/// EOF-safe; false at the first failed read, every later field keeps its prior value (FR-044).
/// Sanitising (FR-045 rules, global_params.h:177-212 precedent): the position goes through sanitizeMorphPosition
/// (non-finite keeps); a non-finite ramp keeps, a finite one clamps to [5, 2000]; a mode outside [0, 2] or an Edit
/// Target outside [0, 1] is not stored.
inline bool loadMorphControlParams(MorphControlParams& params, Steinberg::IBStreamer& streamer) {
    float position = 0.0f;
    if (!streamer.readFloat(position)) {
        return false;
    }
    params.position.store(sanitizeMorphPosition(position, params.position.load(std::memory_order_relaxed)),
                          std::memory_order_relaxed);

    float rampMs = 0.0f;
    if (!streamer.readFloat(rampMs)) {
        return false;
    }
    if (Krate::DSP::detail::isFinite(rampMs)) {
        params.rampMs.store(std::clamp(rampMs, Krate::DSP::BassMorphEngine::kMinPositionRampMs,
                                       Krate::DSP::BassMorphEngine::kMaxPositionRampMs),
                            std::memory_order_relaxed);
    }

    Steinberg::int32 mode = 0;
    if (!streamer.readInt32(mode)) {
        return false;
    }
    if (mode >= 0 && mode < kNumAnchorMorphModes) {
        params.anchorMorphMode.store(static_cast<int>(mode), std::memory_order_relaxed);
    }

    Steinberg::int32 target = 0;
    if (!streamer.readInt32(target)) {
        return false;
    }
    if (target >= 0 && target < kNumEditTargets) {
        params.editTarget.store(static_cast<int>(target), std::memory_order_relaxed);
    }
    return true;
}

/// FR-045: the loadMorphControlParams rules applied before normalising, then setParam(id, normalized). EOF-safe;
/// returns at the first failed read. A non-finite float or an out-of-range enum makes NO call, so the parameter keeps
/// its value. The caller wraps the 1001 call in its inStateLoad_ guard (plan 4.6).
template <typename SetParamFunc>
inline void loadMorphControlParamsToController(Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float position = 0.0f;
    if (!streamer.readFloat(position)) {
        return;
    }
    if (Krate::DSP::detail::isFinite(position)) {
        // previous is unused for a finite value; the non-finite case makes no call (keeps the displayed value).
        setParam(kMorphPositionId, static_cast<double>(sanitizeMorphPosition(position, 0.0f)));
    }

    float rampMs = 0.0f;
    if (!streamer.readFloat(rampMs)) {
        return;
    }
    if (Krate::DSP::detail::isFinite(rampMs)) {
        const float clamped = std::clamp(rampMs, Krate::DSP::BassMorphEngine::kMinPositionRampMs,
                                         Krate::DSP::BassMorphEngine::kMaxPositionRampMs);
        setParam(kMorphRampTimeId, normalizedFromPlain(kMorphRampLaw, static_cast<double>(clamped)));
    }

    Steinberg::int32 mode = 0;
    if (!streamer.readInt32(mode)) {
        return;
    }
    if (mode >= 0 && mode < kNumAnchorMorphModes) {
        setParam(kMorphAnchorModeId, static_cast<double>(mode) / static_cast<double>(kNumAnchorMorphModes - 1));
    }

    Steinberg::int32 target = 0;
    if (!streamer.readInt32(target)) {
        return;
    }
    if (target >= 0 && target < kNumEditTargets) {
        setParam(kMorphEditTargetId, static_cast<double>(target) / static_cast<double>(kNumEditTargets - 1));
    }
}

/// FR-044 v1 load: position 0, ramp 40, Blend, Edit Target A.
inline void resetMorphControlParams(MorphControlParams& params) noexcept {
    params.position.store(kDefaultMorphPosition, std::memory_order_relaxed);
    params.rampMs.store(kDefaultMorphRampMs, std::memory_order_relaxed);
    params.anchorMorphMode.store(kDefaultAnchorMorphModeIndex, std::memory_order_relaxed);
    params.editTarget.store(kDefaultEditTarget, std::memory_order_relaxed);
}

}  // namespace Profundum
