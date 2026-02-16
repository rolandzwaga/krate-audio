#pragma once
#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/note_value_ui.h"
#include "parameters/lfo1_params.h"  // for lfoRateFromNormalized/lfoRateToNormalized
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

// =============================================================================
// RandomParams: atomic parameter storage for real-time safety
// =============================================================================

struct RandomParams {
    std::atomic<float> rateHz{4.0f};       // [0.1, 50] Hz (default 4 Hz)
    std::atomic<bool> sync{false};         // tempo sync on/off (default off)
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex}; // default 1/8
    std::atomic<float> smoothness{0.0f};   // [0, 1] (default 0)
};

// =============================================================================
// Parameter change handler (processor side)
// =============================================================================

inline void handleRandomParamChange(
    RandomParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kRandomRateId:
            params.rateHz.store(
                std::clamp(lfoRateFromNormalized(value), 0.1f, 50.0f),
                std::memory_order_relaxed);
            break;
        case kRandomSyncId:
            params.sync.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kRandomNoteValueId:
            params.noteValue.store(
                std::clamp(static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                    0, Parameters::kNoteValueDropdownCount - 1),
                std::memory_order_relaxed);
            break;
        case kRandomSmoothnessId:
            params.smoothness.store(
                static_cast<float>(std::clamp(value, 0.0, 1.0)),
                std::memory_order_relaxed);
            break;
        default: break;
    }
}

// =============================================================================
// Parameter registration (controller side)
// =============================================================================

inline void registerRandomParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    // Rate: continuous, log mapping [0.1, 50] Hz, default 4 Hz (norm ~0.702)
    parameters.addParameter(STR16("Rnd Rate"), STR16("Hz"), 0, 0.702,
        ParameterInfo::kCanAutomate, kRandomRateId);
    // Sync: boolean toggle, default off
    parameters.addParameter(STR16("Rnd Sync"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kRandomSyncId);
    // Note Value: dropdown, default 1/8 (index 10)
    parameters.addParameter(createNoteValueDropdown(
        STR16("Rnd Note Value"), kRandomNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex));
    // Smoothness: continuous, [0, 1], default 0.0
    parameters.addParameter(STR16("Rnd Smoothness"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kRandomSmoothnessId);
}

// =============================================================================
// Display formatting
// =============================================================================

inline Steinberg::tresult formatRandomParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    char8 text[32];
    switch (id) {
        case kRandomRateId:
            snprintf(text, sizeof(text), "%.2f Hz",
                static_cast<double>(std::clamp(lfoRateFromNormalized(value), 0.1f, 50.0f)));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        case kRandomSmoothnessId:
            snprintf(text, sizeof(text), "%.0f%%",
                std::clamp(value, 0.0, 1.0) * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        default:
            return kResultFalse;
    }
}

// =============================================================================
// State persistence
// =============================================================================

inline void saveRandomParams(const RandomParams& params,
                              Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sync.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
    streamer.writeFloat(params.smoothness.load(std::memory_order_relaxed));
}

inline bool loadRandomParams(RandomParams& params,
                              Steinberg::IBStreamer& streamer) {
    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    if (!streamer.readFloat(fv)) { return false; }
    params.rateHz.store(fv, std::memory_order_relaxed);

    if (!streamer.readInt32(iv)) { return false; }
    params.sync.store(iv != 0, std::memory_order_relaxed);

    if (!streamer.readInt32(iv)) { return false; }
    params.noteValue.store(iv, std::memory_order_relaxed);

    if (!streamer.readFloat(fv)) { return false; }
    params.smoothness.store(fv, std::memory_order_relaxed);

    return true;
}

template<typename SetParamFunc>
inline void loadRandomParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    // Rate: read Hz, convert back to normalized
    if (streamer.readFloat(fv))
        setParam(kRandomRateId, lfoRateToNormalized(fv));
    // Sync: read int32, convert to 0.0/1.0
    if (streamer.readInt32(iv))
        setParam(kRandomSyncId, iv != 0 ? 1.0 : 0.0);
    // Note Value: read int32, convert to normalized
    if (streamer.readInt32(iv))
        setParam(kRandomNoteValueId,
            static_cast<double>(iv) / (Parameters::kNoteValueDropdownCount - 1));
    // Smoothness: read float, direct [0,1]
    if (streamer.readFloat(fv))
        setParam(kRandomSmoothnessId, static_cast<double>(fv));
}

} // namespace Ruinae
