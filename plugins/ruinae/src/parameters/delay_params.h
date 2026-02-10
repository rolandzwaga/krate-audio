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

struct RuinaeDelayParams {
    std::atomic<int> type{0};          // RuinaeDelayType (0-4)
    std::atomic<float> timeMs{500.0f}; // 1-5000 ms
    std::atomic<float> feedback{0.4f}; // 0-1.2
    std::atomic<float> mix{0.0f};      // 0-1
    std::atomic<bool> sync{false};
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex};
};

inline void handleDelayParamChange(
    RuinaeDelayParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kDelayTypeId:
            params.type.store(
                std::clamp(static_cast<int>(value * (kDelayTypeCount - 1) + 0.5), 0, kDelayTypeCount - 1),
                std::memory_order_relaxed); break;
        case kDelayTimeId:
            // 0-1 -> 1-5000 ms
            params.timeMs.store(
                std::clamp(static_cast<float>(1.0 + value * 4999.0), 1.0f, 5000.0f),
                std::memory_order_relaxed); break;
        case kDelayFeedbackId:
            // 0-1 -> 0-1.2
            params.feedback.store(
                std::clamp(static_cast<float>(value * 1.2), 0.0f, 1.2f),
                std::memory_order_relaxed); break;
        case kDelayMixId:
            params.mix.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kDelaySyncId:
            params.sync.store(value >= 0.5, std::memory_order_relaxed); break;
        case kDelayNoteValueId:
            params.noteValue.store(
                std::clamp(static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                    0, Parameters::kNoteValueDropdownCount - 1),
                std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerDelayParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(createDropdownParameter(
        STR16("Delay Type"), kDelayTypeId,
        {STR16("Digital"), STR16("Tape"), STR16("Ping Pong"),
         STR16("Granular"), STR16("Spectral")}
    ));
    parameters.addParameter(STR16("Delay Time"), STR16("ms"), 0, 0.100,
        ParameterInfo::kCanAutomate, kDelayTimeId);
    parameters.addParameter(STR16("Delay Feedback"), STR16("%"), 0, 0.333,
        ParameterInfo::kCanAutomate, kDelayFeedbackId);
    parameters.addParameter(STR16("Delay Mix"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kDelayMixId);
    parameters.addParameter(STR16("Delay Sync"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kDelaySyncId);
    parameters.addParameter(createNoteValueDropdown(
        STR16("Delay Note Value"), kDelayNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));
}

inline Steinberg::tresult formatDelayParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kDelayTimeId: {
            float ms = static_cast<float>(1.0 + value * 4999.0);
            char8 text[32];
            if (ms >= 1000.0f) snprintf(text, sizeof(text), "%.2f s", ms / 1000.0f);
            else snprintf(text, sizeof(text), "%.1f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kDelayFeedbackId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 120.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kDelayMixId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveDelayParams(const RuinaeDelayParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.type.load(std::memory_order_relaxed));
    streamer.writeFloat(params.timeMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sync.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
}

inline bool loadDelayParams(RuinaeDelayParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    if (!streamer.readInt32(iv)) { return false; } params.type.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.timeMs.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.feedback.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.mix.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.sync.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.noteValue.store(iv, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadDelayParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    if (streamer.readInt32(iv)) setParam(kDelayTypeId, static_cast<double>(iv) / (kDelayTypeCount - 1));
    if (streamer.readFloat(fv)) setParam(kDelayTimeId, static_cast<double>((fv - 1.0f) / 4999.0f));
    if (streamer.readFloat(fv)) setParam(kDelayFeedbackId, static_cast<double>(fv / 1.2f));
    if (streamer.readFloat(fv)) setParam(kDelayMixId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kDelaySyncId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(iv)) setParam(kDelayNoteValueId, static_cast<double>(iv) / (Parameters::kNoteValueDropdownCount - 1));
}

} // namespace Ruinae
