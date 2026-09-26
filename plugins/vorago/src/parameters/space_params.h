#pragma once

// ==============================================================================
// Vorago Phase 12 - Space parameter pack (ID 1100-1199)   T025, FR-011..FR-013
// ==============================================================================
// Group 7 shared contract (specs/vorago-phase12-parameters/tasks.md; shape of
// global_params.h). 16 IDs: 1100-1106 route MB (Cavern* macro targets),
// 1107-1115 route CV (CavernVerb setters, cavern_verb.h:613-760).
//
// Plain ranges are the destination setter clamps (plan section 3.2):
//   every % field [0, 1] lin; 1102 decay [0.5, 60] s log; 1110 early size
//   [80, 300] ms log (plan D-P5: the prepared config's maxEarlySeconds 0.30
//   ceiling); 1114 damper rate [0, 1] offset-log eps 0.01 (plan D-P4);
//   1115 freeze L(2) Off/On.
// Defaults equal CavernVerb::kDefault* (cavern_verb.h:221, 247-265); the pack
// test pins them.
//
// Stream: 15 floats (ascending ID 1100-1114) + int32 freeze = 64 bytes.
// ==============================================================================

#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "ui/parameter_helpers.h"  // plugins/shared/src/ui/parameter_helpers.h

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>

namespace Vorago {

struct SpaceParams {  // PLAIN units; initializers == C-6 defaults
    std::atomic<float> size{0.50f};             ///< 1100 [0, 1]
    std::atomic<float> darkness{0.80f};         ///< 1101 [0, 1]
    std::atomic<float> decaySeconds{20.0f};     ///< 1102 [0.5, 60] s
    std::atomic<float> fog{0.30f};              ///< 1103 [0, 1]
    std::atomic<float> damperDepth{0.35f};      ///< 1104 [0, 1]
    std::atomic<float> mix{1.00f};              ///< 1105 [0, 1]
    std::atomic<float> width{1.00f};            ///< 1106 [0, 1]
    std::atomic<float> density{0.75f};          ///< 1107 [0, 1]
    std::atomic<float> dimensionality{0.50f};   ///< 1108 [0, 1]
    std::atomic<float> breath{0.50f};           ///< 1109 [0, 1]
    std::atomic<float> earlySizeMs{220.0f};     ///< 1110 [80, 300] ms
    std::atomic<float> earlyLevel{0.80f};       ///< 1111 [0, 1]
    std::atomic<float> earlyAbsorption{0.60f};  ///< 1112 [0, 1]
    std::atomic<float> earlySend{0.70f};        ///< 1113 [0, 1]
    std::atomic<float> damperRate{0.15f};       ///< 1114 [0, 1] offset-log
    std::atomic<int> freeze{0};                 ///< 1115 index 0 = Off, 1 = On
};

inline constexpr double kSpaceDecayMinSeconds = 0.5;
inline constexpr double kSpaceDecayMaxSeconds = 60.0;
inline constexpr double kSpaceEarlySizeMinMs = 80.0;
inline constexpr double kSpaceEarlySizeMaxMs = 300.0;
inline constexpr double kSpaceDamperRateEps = 0.01;
inline constexpr int kSpaceFreezeCount = 2;

/// The 15 continuous IDs in stream (ascending ID) order.
inline constexpr std::array<Steinberg::Vst::ParamID, 15> kSpaceFloatIds = {
    kSpaceSizeId,           kSpaceDarknessId,   kSpaceDecayId,           kSpaceFogId,
    kSpaceDamperDepthId,    kSpaceMixId,        kSpaceWidthId,           kSpaceDensityId,
    kSpaceDimensionalityId, kSpaceBreathId,     kSpaceEarlySizeId,       kSpaceEarlyLevelId,
    kSpaceEarlyAbsorptionId, kSpaceEarlySendId, kSpaceDamperRateId};

// ==============================================================================
// Per-ID mapping helpers (continuous IDs only)
// ==============================================================================

[[nodiscard]] inline std::atomic<float>* spaceFloatField(SpaceParams& p,
                                                         Steinberg::Vst::ParamID id) noexcept {
    switch (id) {
        case kSpaceSizeId: return &p.size;
        case kSpaceDarknessId: return &p.darkness;
        case kSpaceDecayId: return &p.decaySeconds;
        case kSpaceFogId: return &p.fog;
        case kSpaceDamperDepthId: return &p.damperDepth;
        case kSpaceMixId: return &p.mix;
        case kSpaceWidthId: return &p.width;
        case kSpaceDensityId: return &p.density;
        case kSpaceDimensionalityId: return &p.dimensionality;
        case kSpaceBreathId: return &p.breath;
        case kSpaceEarlySizeId: return &p.earlySizeMs;
        case kSpaceEarlyLevelId: return &p.earlyLevel;
        case kSpaceEarlyAbsorptionId: return &p.earlyAbsorption;
        case kSpaceEarlySendId: return &p.earlySend;
        case kSpaceDamperRateId: return &p.damperRate;
        default: return nullptr;
    }
}

[[nodiscard]] inline const std::atomic<float>* spaceFloatField(
    const SpaceParams& p, Steinberg::Vst::ParamID id) noexcept {
    return spaceFloatField(const_cast<SpaceParams&>(p), id);  // NOLINT(cppcoreguidelines-pro-type-const-cast)
}

[[nodiscard]] inline double spaceFloatMin(Steinberg::Vst::ParamID id) noexcept {
    switch (id) {
        case kSpaceDecayId: return kSpaceDecayMinSeconds;
        case kSpaceEarlySizeId: return kSpaceEarlySizeMinMs;
        default: return 0.0;
    }
}

[[nodiscard]] inline double spaceFloatMax(Steinberg::Vst::ParamID id) noexcept {
    switch (id) {
        case kSpaceDecayId: return kSpaceDecayMaxSeconds;
        case kSpaceEarlySizeId: return kSpaceEarlySizeMaxMs;
        default: return 1.0;
    }
}

[[nodiscard]] inline double spaceFloatFromNormalized(Steinberg::Vst::ParamID id,
                                                     double n) noexcept {
    switch (id) {
        case kSpaceDecayId:
        case kSpaceEarlySizeId:
            return Krate::Plugins::logMapFromNormalized(n, spaceFloatMin(id), spaceFloatMax(id));
        case kSpaceDamperRateId:
            return offsetLogFromNormalized(n, 0.0, 1.0, kSpaceDamperRateEps);
        default:
            return linearFromNormalized(n, spaceFloatMin(id), spaceFloatMax(id));
    }
}

[[nodiscard]] inline double spaceFloatToNormalized(Steinberg::Vst::ParamID id,
                                                   double u) noexcept {
    switch (id) {
        case kSpaceDecayId:
        case kSpaceEarlySizeId:
            return Krate::Plugins::logMapToNormalized(u, spaceFloatMin(id), spaceFloatMax(id));
        case kSpaceDamperRateId:
            return offsetLogToNormalized(u, 0.0, 1.0, kSpaceDamperRateEps);
        default:
            return linearToNormalized(u, spaceFloatMin(id), spaceFloatMax(id));
    }
}

[[nodiscard]] inline float clampSpaceFloat(Steinberg::Vst::ParamID id, float v) noexcept {
    return std::clamp(v, static_cast<float>(spaceFloatMin(id)),
                      static_cast<float>(spaceFloatMax(id)));
}

// ==============================================================================
// Parameter Change Handler
// ==============================================================================
// The caller has already rejected non-finite normalized values and clamped to [0, 1].

inline void handleSpaceParamChange(SpaceParams& params, Steinberg::Vst::ParamID id,
                                   Steinberg::Vst::ParamValue value) noexcept {
    switch (id) {
        case kSpaceSizeId:
        case kSpaceDarknessId:
        case kSpaceDecayId:
        case kSpaceFogId:
        case kSpaceDamperDepthId:
        case kSpaceMixId:
        case kSpaceWidthId:
        case kSpaceDensityId:
        case kSpaceDimensionalityId:
        case kSpaceBreathId:
        case kSpaceEarlySizeId:
        case kSpaceEarlyLevelId:
        case kSpaceEarlyAbsorptionId:
        case kSpaceEarlySendId:
        case kSpaceDamperRateId:
            spaceFloatField(params, id)->store(static_cast<float>(spaceFloatFromNormalized(id, value)),
                                               std::memory_order_relaxed);
            break;
        case kSpaceFreezeId:
            params.freeze.store(indexFromNormalized(value, kSpaceFreezeCount),
                                std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================

inline void registerSpaceParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    // n0 is the inverse map of the DOUBLE default (plan 3.4 "defaults from inverse
    // map"); mapping the float atomics would cost up to ~1e-8 of n0 accuracy.
    const auto add = [&](const TChar* title, const TChar* units, ParamID id, double def) {
        parameters.addParameter(title, units, 0, spaceFloatToNormalized(id, def),
                                ParameterInfo::kCanAutomate, static_cast<Steinberg::int32>(id));
    };

    add(STR16("Space Size"), STR16("%"), kSpaceSizeId, 0.50);
    add(STR16("Space Darkness"), STR16("%"), kSpaceDarknessId, 0.80);
    add(STR16("Space Decay"), STR16("s"), kSpaceDecayId, 20.0);
    add(STR16("Space Fog"), STR16("%"), kSpaceFogId, 0.30);
    add(STR16("Space Damper Depth"), STR16("%"), kSpaceDamperDepthId, 0.35);
    add(STR16("Space Mix"), STR16("%"), kSpaceMixId, 1.00);
    add(STR16("Space Width"), STR16("%"), kSpaceWidthId, 1.00);
    add(STR16("Space Density"), STR16("%"), kSpaceDensityId, 0.75);
    add(STR16("Space Dimensionality"), STR16("%"), kSpaceDimensionalityId, 0.50);
    add(STR16("Space Breath"), STR16("%"), kSpaceBreathId, 0.50);
    add(STR16("Space Early Size"), STR16("ms"), kSpaceEarlySizeId, 220.0);
    add(STR16("Space Early Level"), STR16("%"), kSpaceEarlyLevelId, 0.80);
    add(STR16("Space Early Absorption"), STR16("%"), kSpaceEarlyAbsorptionId, 0.60);
    add(STR16("Space Early Send"), STR16("%"), kSpaceEarlySendId, 0.70);
    add(STR16("Space Damper Rate"), STR16(""), kSpaceDamperRateId, 0.15);

    auto* freeze = Krate::Plugins::createDropdownParameterWithDefault(
        STR16("Space Freeze"), kSpaceFreezeId, /*defaultIndex=*/0, {STR16("Off"), STR16("On")});
    // P-1 (global_params.h:82-84): pin the REGISTERED default, not just the current value.
    freeze->getInfo().defaultNormalizedValue = indexToNormalized(0, kSpaceFreezeCount);
    parameters.addParameter(freeze);
}

// ==============================================================================
// Display Formatting (continuous IDs only; Freeze is a StringListParameter)
// ==============================================================================

inline Steinberg::tresult formatSpaceParam(Steinberg::Vst::ParamID id,
                                           Steinberg::Vst::ParamValue value,
                                           Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    char8 text[32];
    switch (id) {
        case kSpaceDecayId:
            snprintf(text, sizeof(text), "%.1f s", spaceFloatFromNormalized(id, value));
            break;
        case kSpaceEarlySizeId:
            snprintf(text, sizeof(text), "%.0f ms", spaceFloatFromNormalized(id, value));
            break;
        case kSpaceDamperRateId:
            snprintf(text, sizeof(text), "%.3f", spaceFloatFromNormalized(id, value));
            break;
        case kSpaceSizeId:
        case kSpaceDarknessId:
        case kSpaceFogId:
        case kSpaceDamperDepthId:
        case kSpaceMixId:
        case kSpaceWidthId:
        case kSpaceDensityId:
        case kSpaceDimensionalityId:
        case kSpaceBreathId:
        case kSpaceEarlyLevelId:
        case kSpaceEarlyAbsorptionId:
        case kSpaceEarlySendId:
            snprintf(text, sizeof(text), "%.1f %%", spaceFloatFromNormalized(id, value) * 100.0);
            break;
        default:
            return kResultFalse;
    }
    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// ==============================================================================
// State Persistence - 64 bytes (15 x float + int32), ascending ID order
// ==============================================================================

inline void saveSpaceParams(const SpaceParams& params, Steinberg::IBStreamer& streamer) {
    for (const auto id : kSpaceFloatIds)
        streamer.writeFloat(spaceFloatField(params, id)->load(std::memory_order_relaxed));
    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.freeze.load(std::memory_order_relaxed)));
}

/// EOF-safe: a failed read returns false and leaves every later field at its CURRENT
/// value. Non-finite floats are skipped (field unchanged); finite floats clamp to the
/// plain range; the freeze index clamps to [0, 1].
inline bool loadSpaceParams(SpaceParams& params, Steinberg::IBStreamer& streamer) {
    for (const auto id : kSpaceFloatIds) {
        float v = 0.0f;
        if (!streamer.readFloat(v)) { return false; }
        if (Krate::DSP::detail::isFinite(v)) {  // fast-math-immune, db_utils.h:118
            spaceFloatField(params, id)->store(clampSpaceFloat(id, v), std::memory_order_relaxed);
        }
    }

    Steinberg::int32 n = 0;
    if (!streamer.readInt32(n)) { return false; }
    params.freeze.store(std::clamp(static_cast<int>(n), 0, kSpaceFreezeCount - 1),
                        std::memory_order_relaxed);
    return true;
}

// ==============================================================================
// Controller State Sync (inverts every mapping above)
// ==============================================================================

template <typename SetParamFunc>
inline void loadSpaceParamsToController(Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    for (const auto id : kSpaceFloatIds) {
        float v = 0.0f;
        if (!streamer.readFloat(v)) { return; }
        if (Krate::DSP::detail::isFinite(v)) {
            setParam(id, spaceFloatToNormalized(id, static_cast<double>(clampSpaceFloat(id, v))));
        }
    }

    Steinberg::int32 n = 0;
    if (!streamer.readInt32(n)) { return; }
    setParam(static_cast<Steinberg::Vst::ParamID>(kSpaceFreezeId),
             indexToNormalized(std::clamp(static_cast<int>(n), 0, kSpaceFreezeCount - 1),
                               kSpaceFreezeCount));
}

}  // namespace Vorago
