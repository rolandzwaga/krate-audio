#pragma once

// ==============================================================================
// Vorago Phase 12 - Ghost Parameters (ID 1400-1499)   FR-011 / FR-012 / FR-013
// ==============================================================================
// The six-function pack contract (plan section 3.4; shape of global_params.h).
//
//   1400 Ghost Peak Level          %  [0, 1]  0.60  lin   MB  GhostPeakLevel
//   1401 Ghost Blur                %  [0, 1]  0.85  lin   MB  AtmosBlur
//   1402 Ghost Reverse Probability %  [0, 1]  0.0   lin   ENG setGhostReverseProbability
//   1403 Ghost Event Triggers      L(2) Off, On   0       ENG setGhostEventTriggers
//
// Stream (ascending ID): 3 x float + int32 = 16 bytes (plan section 4.9).
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

inline constexpr int kGhostEventTriggersCount = 2;  ///< Off, On

struct GhostParams {
    std::atomic<float> peakLevel{0.60f};          ///< [0, 1]
    std::atomic<float> blur{0.85f};               ///< [0, 1]
    std::atomic<float> reverseProbability{0.0f};  ///< [0, 1]
    std::atomic<int> eventTriggers{0};            ///< index: 0 Off, 1 On
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================
// The caller has already rejected non-finite values and clamped to [0, 1].

inline void handleGhostParamChange(GhostParams& params, Steinberg::Vst::ParamID id,
                                   Steinberg::Vst::ParamValue value) noexcept {
    switch (id) {
        case kGhostPeakLevelId:
            params.peakLevel.store(static_cast<float>(linearFromNormalized(value, 0.0, 1.0)),
                                   std::memory_order_relaxed);
            break;
        case kGhostBlurId:
            params.blur.store(static_cast<float>(linearFromNormalized(value, 0.0, 1.0)),
                              std::memory_order_relaxed);
            break;
        case kGhostReverseProbabilityId:
            params.reverseProbability.store(
                static_cast<float>(linearFromNormalized(value, 0.0, 1.0)),
                std::memory_order_relaxed);
            break;
        case kGhostEventTriggersId:
            params.eventTriggers.store(indexFromNormalized(value, kGhostEventTriggersCount),
                                       std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================

inline void registerGhostParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Ghost Peak Level"), STR16("%"), 0, 0.60,
                            ParameterInfo::kCanAutomate, kGhostPeakLevelId);
    parameters.addParameter(STR16("Ghost Blur"), STR16("%"), 0, 0.85,
                            ParameterInfo::kCanAutomate, kGhostBlurId);
    parameters.addParameter(STR16("Ghost Reverse Probability"), STR16("%"), 0, 0.0,
                            ParameterInfo::kCanAutomate, kGhostReverseProbabilityId);

    auto* triggers = Krate::Plugins::createDropdownParameterWithDefault(
        STR16("Ghost Event Triggers"), kGhostEventTriggersId, /*defaultIndex=*/0,
        {STR16("Off"), STR16("On")});
    // P-1 (global_params.h:82-84): the helper sets only the CURRENT value; pin the
    // REGISTERED default explicitly.
    triggers->getInfo().defaultNormalizedValue = 0.0;
    parameters.addParameter(triggers);
}

// ==============================================================================
// Display Formatting (continuous IDs only; the list formats itself)
// ==============================================================================

inline Steinberg::tresult formatGhostParam(Steinberg::Vst::ParamID id,
                                           Steinberg::Vst::ParamValue value,
                                           Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    switch (id) {
        case kGhostPeakLevelId:
        case kGhostBlurId:
        case kGhostReverseProbabilityId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%",
                     linearFromNormalized(value, 0.0, 1.0) * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default:
            return kResultFalse;
    }
}

// ==============================================================================
// State Persistence - 16 bytes (3 x float + int32), ascending ID order
// ==============================================================================

inline void saveGhostParams(const GhostParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.peakLevel.load(std::memory_order_relaxed));
    streamer.writeFloat(params.blur.load(std::memory_order_relaxed));
    streamer.writeFloat(params.reverseProbability.load(std::memory_order_relaxed));
    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.eventTriggers.load(std::memory_order_relaxed)));
}

/// EOF-safe: stops at the first failed read (returns false; later fields keep their
/// CURRENT values). Non-finite floats leave their field unchanged; finite floats are
/// clamped to [0, 1]; the index is clamped to [0, 1].
inline bool loadGhostParams(GhostParams& params, Steinberg::IBStreamer& streamer) {
    float f = 0.0f;
    Steinberg::int32 i = 0;

    if (!streamer.readFloat(f)) { return false; }
    if (Krate::DSP::detail::isFinite(f)) {
        params.peakLevel.store(std::clamp(f, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    if (!streamer.readFloat(f)) { return false; }
    if (Krate::DSP::detail::isFinite(f)) {
        params.blur.store(std::clamp(f, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    if (!streamer.readFloat(f)) { return false; }
    if (Krate::DSP::detail::isFinite(f)) {
        params.reverseProbability.store(std::clamp(f, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    if (!streamer.readInt32(i)) { return false; }
    params.eventTriggers.store(std::clamp(static_cast<int>(i), 0, kGhostEventTriggersCount - 1),
                               std::memory_order_relaxed);

    return true;
}

// ==============================================================================
// Controller State Sync (inverts every mapping above; same read order and rules)
// ==============================================================================

template <typename SetParamFunc>
inline void loadGhostParamsToController(Steinberg::IBStreamer& streamer,
                                        SetParamFunc setParam) {
    float f = 0.0f;
    Steinberg::int32 i = 0;

    constexpr std::array<Steinberg::Vst::ParamID, 3> kFloatIds = {
        kGhostPeakLevelId, kGhostBlurId, kGhostReverseProbabilityId};
    for (const Steinberg::Vst::ParamID id : kFloatIds) {
        if (!streamer.readFloat(f)) { return; }
        if (Krate::DSP::detail::isFinite(f)) {
            setParam(id, linearToNormalized(static_cast<double>(f), 0.0, 1.0));
        }
    }

    if (!streamer.readInt32(i)) { return; }
    setParam(kGhostEventTriggersId, indexToNormalized(static_cast<int>(i), kGhostEventTriggersCount));
}

}  // namespace Vorago
