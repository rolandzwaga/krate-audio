#pragma once

// ==============================================================================
// OSC B Parameters (ID 200-299)
// ==============================================================================

#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

struct OscBParams {
    std::atomic<int> type{0};
    std::atomic<float> tuneSemitones{0.0f};
    std::atomic<float> fineCents{0.0f};
    std::atomic<float> level{1.0f};
    std::atomic<float> phase{0.0f};
};

inline void handleOscBParamChange(
    OscBParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kOscBTypeId:
            params.type.store(
                std::clamp(static_cast<int>(value * (kOscTypeCount - 1) + 0.5), 0, kOscTypeCount - 1),
                std::memory_order_relaxed);
            break;
        case kOscBTuneId:
            params.tuneSemitones.store(
                std::clamp(static_cast<float>(value * 48.0 - 24.0), -24.0f, 24.0f),
                std::memory_order_relaxed);
            break;
        case kOscBFineId:
            params.fineCents.store(
                std::clamp(static_cast<float>(value * 200.0 - 100.0), -100.0f, 100.0f),
                std::memory_order_relaxed);
            break;
        case kOscBLevelId:
            params.level.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        case kOscBPhaseId:
            params.phase.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        default: break;
    }
}

inline void registerOscBParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(createDropdownParameter(
        STR16("OSC B Type"), kOscBTypeId,
        {STR16("PolyBLEP"), STR16("Wavetable"), STR16("Phase Dist"),
         STR16("Sync"), STR16("Additive"), STR16("Chaos"),
         STR16("Particle"), STR16("Formant"), STR16("Spectral Freeze"), STR16("Noise")}
    ));
    parameters.addParameter(STR16("OSC B Tune"), STR16("st"), 0, 0.5,
        ParameterInfo::kCanAutomate, kOscBTuneId);
    parameters.addParameter(STR16("OSC B Fine"), STR16("ct"), 0, 0.5,
        ParameterInfo::kCanAutomate, kOscBFineId);
    parameters.addParameter(STR16("OSC B Level"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kOscBLevelId);
    parameters.addParameter(STR16("OSC B Phase"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kOscBPhaseId);
}

inline Steinberg::tresult formatOscBParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kOscBTuneId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%+.0f st", value * 48.0 - 24.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBFineId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%+.0f ct", value * 200.0 - 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscBLevelId: case kOscBPhaseId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveOscBParams(const OscBParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.type.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tuneSemitones.load(std::memory_order_relaxed));
    streamer.writeFloat(params.fineCents.load(std::memory_order_relaxed));
    streamer.writeFloat(params.level.load(std::memory_order_relaxed));
    streamer.writeFloat(params.phase.load(std::memory_order_relaxed));
}

inline bool loadOscBParams(OscBParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;
    if (!streamer.readInt32(intVal)) return false;
    params.type.store(intVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.tuneSemitones.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.fineCents.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.level.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.phase.store(floatVal, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadOscBParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;
    if (streamer.readInt32(intVal))
        setParam(kOscBTypeId, static_cast<double>(intVal) / (kOscTypeCount - 1));
    if (streamer.readFloat(floatVal))
        setParam(kOscBTuneId, static_cast<double>((floatVal + 24.0f) / 48.0f));
    if (streamer.readFloat(floatVal))
        setParam(kOscBFineId, static_cast<double>((floatVal + 100.0f) / 200.0f));
    if (streamer.readFloat(floatVal))
        setParam(kOscBLevelId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam(kOscBPhaseId, static_cast<double>(floatVal));
}

} // namespace Ruinae
