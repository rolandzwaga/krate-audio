#pragma once

// ==============================================================================
// Global Parameters (ID 0-99)
// ==============================================================================

#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

// ==============================================================================
// Parameter Storage
// ==============================================================================

struct GlobalParams {
    std::atomic<float> masterGain{1.0f};    // 0-2 (linear gain)
    std::atomic<int> voiceMode{0};          // 0=Poly, 1=Mono
    std::atomic<int> polyphony{8};          // 1-16
    std::atomic<bool> softLimit{true};      // on/off
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================

inline void handleGlobalParamChange(
    GlobalParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

    switch (id) {
        case kMasterGainId:
            // 0-1 normalized -> 0-2 linear gain
            params.masterGain.store(
                std::clamp(static_cast<float>(value * 2.0), 0.0f, 2.0f),
                std::memory_order_relaxed);
            break;
        case kVoiceModeId:
            // 0-1 normalized -> 0 or 1
            params.voiceMode.store(
                static_cast<int>(value + 0.5),
                std::memory_order_relaxed);
            break;
        case kPolyphonyId:
            // 0-1 normalized -> 1-16
            params.polyphony.store(
                std::clamp(static_cast<int>(value * 15.0 + 1.0 + 0.5), 1, 16),
                std::memory_order_relaxed);
            break;
        case kSoftLimitId:
            params.softLimit.store(value >= 0.5, std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================

inline void registerGlobalParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    // Master Gain (0-200%)
    parameters.addParameter(
        STR16("Master Gain"), STR16("dB"), 0, 0.5,
        ParameterInfo::kCanAutomate, kMasterGainId);

    // Voice Mode
    parameters.addParameter(createDropdownParameter(
        STR16("Voice Mode"), kVoiceModeId,
        {STR16("Poly"), STR16("Mono")}
    ));

    // Polyphony (1-16, default 8 => normalized = 7/15)
    parameters.addParameter(
        STR16("Polyphony"), STR16(""), 15, 7.0 / 15.0,
        ParameterInfo::kCanAutomate, kPolyphonyId);

    // Soft Limit (on/off, default on)
    parameters.addParameter(
        STR16("Soft Limit"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kSoftLimitId);
}

// ==============================================================================
// Display Formatting
// ==============================================================================

inline Steinberg::tresult formatGlobalParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    switch (id) {
        case kMasterGainId: {
            float gain = static_cast<float>(value * 2.0);
            float dB = (gain > 0.0001f) ? 20.0f * std::log10(gain) : -80.0f;
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f dB", dB);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kPolyphonyId: {
            int voices = std::clamp(static_cast<int>(value * 15.0 + 1.0 + 0.5), 1, 16);
            char8 text[32];
            snprintf(text, sizeof(text), "%d", voices);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        // VoiceMode and SoftLimit handled by StringListParameter/default
        default:
            break;
    }
    return Steinberg::kResultFalse;
}

// ==============================================================================
// State Persistence
// ==============================================================================

inline void saveGlobalParams(const GlobalParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.masterGain.load(std::memory_order_relaxed));
    streamer.writeInt32(params.voiceMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.polyphony.load(std::memory_order_relaxed));
    streamer.writeInt32(params.softLimit.load(std::memory_order_relaxed) ? 1 : 0);
}

inline bool loadGlobalParams(GlobalParams& params, Steinberg::IBStreamer& streamer) {
    float floatVal = 1.0f;
    Steinberg::int32 intVal = 0;

    if (!streamer.readFloat(floatVal)) return false;
    params.masterGain.store(floatVal, std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    params.voiceMode.store(intVal, std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    params.polyphony.store(intVal, std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    params.softLimit.store(intVal != 0, std::memory_order_relaxed);

    return true;
}

// ==============================================================================
// Controller State Sync
// ==============================================================================

template<typename SetParamFunc>
inline void loadGlobalParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {

    float floatVal = 0.0f;
    Steinberg::int32 intVal = 0;

    if (streamer.readFloat(floatVal))
        setParam(kMasterGainId, static_cast<double>(floatVal / 2.0f));
    if (streamer.readInt32(intVal))
        setParam(kVoiceModeId, static_cast<double>(intVal));
    if (streamer.readInt32(intVal))
        setParam(kPolyphonyId, (static_cast<double>(intVal) - 1.0) / 15.0);
    if (streamer.readInt32(intVal))
        setParam(kSoftLimitId, intVal != 0 ? 1.0 : 0.0);
}

} // namespace Ruinae
