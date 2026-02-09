#pragma once

// ==============================================================================
// OSC A Parameters (ID 100-199)
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

struct OscAParams {
    std::atomic<int> type{0};                // OscType enum (0-9)
    std::atomic<float> tuneSemitones{0.0f};  // -24 to +24
    std::atomic<float> fineCents{0.0f};      // -100 to +100
    std::atomic<float> level{1.0f};          // 0-1
    std::atomic<float> phase{0.0f};          // 0-1
};

inline void handleOscAParamChange(
    OscAParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kOscATypeId:
            params.type.store(
                std::clamp(static_cast<int>(value * (kOscTypeCount - 1) + 0.5), 0, kOscTypeCount - 1),
                std::memory_order_relaxed);
            break;
        case kOscATuneId:
            // 0-1 -> -24 to +24 semitones
            params.tuneSemitones.store(
                std::clamp(static_cast<float>(value * 48.0 - 24.0), -24.0f, 24.0f),
                std::memory_order_relaxed);
            break;
        case kOscAFineId:
            // 0-1 -> -100 to +100 cents
            params.fineCents.store(
                std::clamp(static_cast<float>(value * 200.0 - 100.0), -100.0f, 100.0f),
                std::memory_order_relaxed);
            break;
        case kOscALevelId:
            params.level.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        case kOscAPhaseId:
            params.phase.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        default: break;
    }
}

inline void registerOscAParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(createDropdownParameter(
        STR16("OSC A Type"), kOscATypeId,
        {STR16("PolyBLEP"), STR16("Wavetable"), STR16("Phase Dist"),
         STR16("Sync"), STR16("Additive"), STR16("Chaos"),
         STR16("Particle"), STR16("Formant"), STR16("Spectral Freeze"), STR16("Noise")}
    ));
    parameters.addParameter(STR16("OSC A Tune"), STR16("st"), 0, 0.5,
        ParameterInfo::kCanAutomate, kOscATuneId);
    parameters.addParameter(STR16("OSC A Fine"), STR16("ct"), 0, 0.5,
        ParameterInfo::kCanAutomate, kOscAFineId);
    parameters.addParameter(STR16("OSC A Level"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kOscALevelId);
    parameters.addParameter(STR16("OSC A Phase"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kOscAPhaseId);
}

inline Steinberg::tresult formatOscAParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kOscATuneId: {
            float st = static_cast<float>(value * 48.0 - 24.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%+.0f st", st);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscAFineId: {
            float ct = static_cast<float>(value * 200.0 - 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%+.0f ct", ct);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscALevelId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kOscAPhaseId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return Steinberg::kResultFalse;
}

inline void saveOscAParams(const OscAParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.type.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tuneSemitones.load(std::memory_order_relaxed));
    streamer.writeFloat(params.fineCents.load(std::memory_order_relaxed));
    streamer.writeFloat(params.level.load(std::memory_order_relaxed));
    streamer.writeFloat(params.phase.load(std::memory_order_relaxed));
}

inline bool loadOscAParams(OscAParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;
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
inline void loadOscAParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;
    if (streamer.readInt32(intVal))
        setParam(kOscATypeId, static_cast<double>(intVal) / (kOscTypeCount - 1));
    if (streamer.readFloat(floatVal))
        setParam(kOscATuneId, static_cast<double>((floatVal + 24.0f) / 48.0f));
    if (streamer.readFloat(floatVal))
        setParam(kOscAFineId, static_cast<double>((floatVal + 100.0f) / 200.0f));
    if (streamer.readFloat(floatVal))
        setParam(kOscALevelId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam(kOscAPhaseId, static_cast<double>(floatVal));
}

} // namespace Ruinae
