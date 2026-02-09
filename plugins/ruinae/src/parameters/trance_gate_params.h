#pragma once
#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "parameters/note_value_ui.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

struct RuinaeTranceGateParams {
    std::atomic<bool> enabled{false};
    std::atomic<int> numStepsIndex{1};     // 0=8, 1=16, 2=32 (index into kNumStepsStrings)
    std::atomic<float> rateHz{4.0f};       // 0.1-100 Hz
    std::atomic<float> depth{1.0f};        // 0-1
    std::atomic<float> attackMs{2.0f};     // 1-20 ms
    std::atomic<float> releaseMs{10.0f};   // 1-50 ms
    std::atomic<bool> tempoSync{true};
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex};
};

inline void handleTranceGateParamChange(
    RuinaeTranceGateParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kTranceGateEnabledId:
            params.enabled.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kTranceGateNumStepsId:
            params.numStepsIndex.store(
                std::clamp(static_cast<int>(value * (kNumStepsCount - 1) + 0.5), 0, kNumStepsCount - 1),
                std::memory_order_relaxed);
            break;
        case kTranceGateRateId:
            // 0-1 -> 0.1-100 Hz
            params.rateHz.store(
                std::clamp(static_cast<float>(0.1 + value * 99.9), 0.1f, 100.0f),
                std::memory_order_relaxed);
            break;
        case kTranceGateDepthId:
            params.depth.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;
        case kTranceGateAttackId:
            // 0-1 -> 1-20 ms
            params.attackMs.store(
                std::clamp(static_cast<float>(1.0 + value * 19.0), 1.0f, 20.0f),
                std::memory_order_relaxed);
            break;
        case kTranceGateReleaseId:
            // 0-1 -> 1-50 ms
            params.releaseMs.store(
                std::clamp(static_cast<float>(1.0 + value * 49.0), 1.0f, 50.0f),
                std::memory_order_relaxed);
            break;
        case kTranceGateTempoSyncId:
            params.tempoSync.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kTranceGateNoteValueId:
            params.noteValue.store(
                std::clamp(static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                    0, Parameters::kNoteValueDropdownCount - 1),
                std::memory_order_relaxed);
            break;
        default: break;
    }
}

inline void registerTranceGateParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("Trance Gate"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kTranceGateEnabledId);
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Gate Steps"), kTranceGateNumStepsId, 1,
        {STR16("8"), STR16("16"), STR16("32")}
    ));
    parameters.addParameter(STR16("Gate Rate"), STR16("Hz"), 0, 0.039,
        ParameterInfo::kCanAutomate, kTranceGateRateId);
    parameters.addParameter(STR16("Gate Depth"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kTranceGateDepthId);
    parameters.addParameter(STR16("Gate Attack"), STR16("ms"), 0, 0.053,
        ParameterInfo::kCanAutomate, kTranceGateAttackId);
    parameters.addParameter(STR16("Gate Release"), STR16("ms"), 0, 0.184,
        ParameterInfo::kCanAutomate, kTranceGateReleaseId);
    parameters.addParameter(STR16("Gate Tempo Sync"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kTranceGateTempoSyncId);
    parameters.addParameter(createNoteValueDropdown(
        STR16("Gate Note Value"), kTranceGateNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));
}

inline Steinberg::tresult formatTranceGateParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kTranceGateRateId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f Hz", 0.1 + value * 99.9);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kTranceGateDepthId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kTranceGateAttackId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f ms", 1.0 + value * 19.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kTranceGateReleaseId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f ms", 1.0 + value * 49.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveTranceGateParams(const RuinaeTranceGateParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.enabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.numStepsIndex.load(std::memory_order_relaxed));
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.attackMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.releaseMs.load(std::memory_order_relaxed));
    streamer.writeInt32(params.tempoSync.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
}

inline bool loadTranceGateParams(RuinaeTranceGateParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;
    if (!streamer.readInt32(intVal)) return false;
    params.enabled.store(intVal != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(intVal)) return false;
    params.numStepsIndex.store(intVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.rateHz.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.depth.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.attackMs.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.releaseMs.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readInt32(intVal)) return false;
    params.tempoSync.store(intVal != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(intVal)) return false;
    params.noteValue.store(intVal, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadTranceGateParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;
    if (streamer.readInt32(intVal))
        setParam(kTranceGateEnabledId, intVal != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(intVal))
        setParam(kTranceGateNumStepsId, static_cast<double>(intVal) / (kNumStepsCount - 1));
    if (streamer.readFloat(floatVal))
        setParam(kTranceGateRateId, static_cast<double>((floatVal - 0.1f) / 99.9f));
    if (streamer.readFloat(floatVal))
        setParam(kTranceGateDepthId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam(kTranceGateAttackId, static_cast<double>((floatVal - 1.0f) / 19.0f));
    if (streamer.readFloat(floatVal))
        setParam(kTranceGateReleaseId, static_cast<double>((floatVal - 1.0f) / 49.0f));
    if (streamer.readInt32(intVal))
        setParam(kTranceGateTempoSyncId, intVal != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(intVal))
        setParam(kTranceGateNoteValueId, static_cast<double>(intVal) / (Parameters::kNoteValueDropdownCount - 1));
}

} // namespace Ruinae
