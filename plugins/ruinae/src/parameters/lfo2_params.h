#pragma once
#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "parameters/note_value_ui.h"
#include "parameters/lfo1_params.h"  // for lfoRateFromNormalized/lfoRateToNormalized/lfoFadeIn*/lfoQuantize*
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

struct LFO2Params {
    std::atomic<float> rateHz{1.0f};
    std::atomic<int> shape{0};
    std::atomic<float> depth{1.0f};
    std::atomic<bool> sync{true};
    // Extended params (v12)
    std::atomic<float> phaseOffset{0.0f};
    std::atomic<bool> retrigger{true};
    std::atomic<int> noteValue{10};
    std::atomic<bool> unipolar{false};
    std::atomic<float> fadeInMs{0.0f};
    std::atomic<float> symmetry{0.5f};
    std::atomic<int> quantizeSteps{0};
};

inline void handleLFO2ParamChange(
    LFO2Params& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kLFO2RateId:
            params.rateHz.store(lfoRateFromNormalized(value), std::memory_order_relaxed); break;
        case kLFO2ShapeId:
            params.shape.store(std::clamp(static_cast<int>(value * (kWaveformCount - 1) + 0.5), 0, kWaveformCount - 1), std::memory_order_relaxed); break;
        case kLFO2DepthId:
            params.depth.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed); break;
        case kLFO2SyncId:
            params.sync.store(value >= 0.5, std::memory_order_relaxed); break;
        case kLFO2PhaseOffsetId:
            params.phaseOffset.store(static_cast<float>(value * 360.0), std::memory_order_relaxed); break;
        case kLFO2RetriggerId:
            params.retrigger.store(value >= 0.5, std::memory_order_relaxed); break;
        case kLFO2NoteValueId:
            params.noteValue.store(
                std::clamp(static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                    0, Parameters::kNoteValueDropdownCount - 1),
                std::memory_order_relaxed); break;
        case kLFO2UnipolarId:
            params.unipolar.store(value >= 0.5, std::memory_order_relaxed); break;
        case kLFO2FadeInId:
            params.fadeInMs.store(lfoFadeInFromNormalized(value), std::memory_order_relaxed); break;
        case kLFO2SymmetryId:
            params.symmetry.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed); break;
        case kLFO2QuantizeId:
            params.quantizeSteps.store(lfoQuantizeFromNormalized(value), std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerLFO2Params(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("LFO 2 Rate"), STR16("Hz"), 0, 0.540,
        ParameterInfo::kCanAutomate, kLFO2RateId);
    parameters.addParameter(createDropdownParameter(
        STR16("LFO 2 Shape"), kLFO2ShapeId,
        {STR16("Sine"), STR16("Triangle"), STR16("Sawtooth"),
         STR16("Square"), STR16("Sample & Hold"), STR16("Smooth Random")}
    ));
    parameters.addParameter(STR16("LFO 2 Depth"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kLFO2DepthId);
    parameters.addParameter(STR16("LFO 2 Sync"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kLFO2SyncId);
    // Extended params
    parameters.addParameter(STR16("LFO 2 Phase"), STR16("deg"), 0, 0.0,
        ParameterInfo::kCanAutomate, kLFO2PhaseOffsetId);
    parameters.addParameter(STR16("LFO 2 Retrigger"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kLFO2RetriggerId);
    parameters.addParameter(createNoteValueDropdown(
        STR16("LFO 2 Note Value"), kLFO2NoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex));
    parameters.addParameter(STR16("LFO 2 Unipolar"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kLFO2UnipolarId);
    parameters.addParameter(STR16("LFO 2 Fade In"), STR16("ms"), 0, 0.0,
        ParameterInfo::kCanAutomate, kLFO2FadeInId);
    parameters.addParameter(STR16("LFO 2 Symmetry"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kLFO2SymmetryId);
    parameters.addParameter(STR16("LFO 2 Quantize"), STR16(""), kQuantizeStepCount, 0.0,
        ParameterInfo::kCanAutomate, kLFO2QuantizeId);
}

inline Steinberg::tresult formatLFO2Param(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kLFO2RateId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.2f Hz", lfoRateFromNormalized(value));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFO2DepthId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFO2PhaseOffsetId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f deg", value * 360.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFO2FadeInId: {
            char8 text[32];
            float ms = lfoFadeInFromNormalized(value);
            if (ms < 1.0f) {
                snprintf(text, sizeof(text), "Off");
            } else if (ms < 1000.0f) {
                snprintf(text, sizeof(text), "%.0f ms", ms);
            } else {
                snprintf(text, sizeof(text), "%.1f s", ms / 1000.0f);
            }
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFO2SymmetryId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFO2QuantizeId: {
            char8 text[32];
            int steps = lfoQuantizeFromNormalized(value);
            if (steps < 2) {
                snprintf(text, sizeof(text), "Off");
            } else {
                snprintf(text, sizeof(text), "%d steps", steps);
            }
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveLFO2Params(const LFO2Params& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeInt32(params.shape.load(std::memory_order_relaxed));
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sync.load(std::memory_order_relaxed) ? 1 : 0);
}

inline bool loadLFO2Params(LFO2Params& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (!streamer.readFloat(fv)) { return false; } params.rateHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.shape.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.depth.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.sync.store(iv != 0, std::memory_order_relaxed);
    return true;
}

// Extended save/load for v12+
inline void saveLFO2ExtendedParams(const LFO2Params& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.phaseOffset.load(std::memory_order_relaxed));
    streamer.writeInt32(params.retrigger.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
    streamer.writeInt32(params.unipolar.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeFloat(params.fadeInMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.symmetry.load(std::memory_order_relaxed));
    streamer.writeInt32(params.quantizeSteps.load(std::memory_order_relaxed));
}

inline bool loadLFO2ExtendedParams(LFO2Params& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (!streamer.readFloat(fv)) { return false; } params.phaseOffset.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.retrigger.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.noteValue.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.unipolar.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.fadeInMs.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.symmetry.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.quantizeSteps.store(iv, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadLFO2ParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (streamer.readFloat(fv)) setParam(kLFO2RateId, lfoRateToNormalized(fv));
    if (streamer.readInt32(iv)) setParam(kLFO2ShapeId, static_cast<double>(iv) / (kWaveformCount - 1));
    if (streamer.readFloat(fv)) setParam(kLFO2DepthId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kLFO2SyncId, iv != 0 ? 1.0 : 0.0);
}

template<typename SetParamFunc>
inline void loadLFO2ExtendedParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (streamer.readFloat(fv)) setParam(kLFO2PhaseOffsetId, static_cast<double>(fv) / 360.0);
    if (streamer.readInt32(iv)) setParam(kLFO2RetriggerId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(iv)) setParam(kLFO2NoteValueId, static_cast<double>(iv) / (Parameters::kNoteValueDropdownCount - 1));
    if (streamer.readInt32(iv)) setParam(kLFO2UnipolarId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readFloat(fv)) setParam(kLFO2FadeInId, lfoFadeInToNormalized(fv));
    if (streamer.readFloat(fv)) setParam(kLFO2SymmetryId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kLFO2QuantizeId, lfoQuantizeToNormalized(iv));
}

} // namespace Ruinae
