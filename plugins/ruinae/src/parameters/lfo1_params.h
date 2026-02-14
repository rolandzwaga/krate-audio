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
#include <cmath>
#include <cstdio>

namespace Ruinae {

struct LFO1Params {
    std::atomic<float> rateHz{1.0f};       // 0.01-50 Hz
    std::atomic<int> shape{0};             // Waveform enum (0-5)
    std::atomic<float> depth{1.0f};        // 0-1
    std::atomic<bool> sync{true};          // on/off (default: sync to host)
    // Extended params (v12)
    std::atomic<float> phaseOffset{0.0f};  // 0-360 degrees
    std::atomic<bool> retrigger{true};     // on/off
    std::atomic<int> noteValue{10};        // dropdown index (default: 1/8 note)
    std::atomic<bool> unipolar{false};     // bipolar by default
    std::atomic<float> fadeInMs{0.0f};     // 0-5000 ms
    std::atomic<float> symmetry{0.5f};     // 0-1 (0.5 = centered)
    std::atomic<int> quantizeSteps{0};     // 0=off, 2-16
};

// Exponential rate: 0-1 -> 0.01-50 Hz
inline float lfoRateFromNormalized(double value) {
    float hz = 0.01f * std::pow(5000.0f, static_cast<float>(value));
    return std::clamp(hz, 0.01f, 50.0f);
}

inline double lfoRateToNormalized(float hz) {
    return std::clamp(static_cast<double>(std::log(hz / 0.01f) / std::log(5000.0f)), 0.0, 1.0);
}

// Exponential fade-in: 0-1 -> 0-5000 ms (0 = off)
inline float lfoFadeInFromNormalized(double value) {
    if (value < 0.001) return 0.0f;
    return 1.0f * std::pow(5000.0f, static_cast<float>(value));
}

inline double lfoFadeInToNormalized(float ms) {
    if (ms <= 0.0f) return 0.0;
    return std::clamp(static_cast<double>(std::log(ms) / std::log(5000.0f)), 0.0, 1.0);
}

// Quantize: 0=off, 1->2 steps, ..., 14->16 steps (stepCount=15)
inline constexpr int kQuantizeStepCount = 15;  // 16 positions: 0=off, 1-15 -> 2-16 steps

inline int lfoQuantizeFromNormalized(double value) {
    int index = std::clamp(static_cast<int>(value * kQuantizeStepCount + 0.5), 0, kQuantizeStepCount);
    if (index == 0) return 0;  // off
    return index + 1;          // 2-16
}

inline double lfoQuantizeToNormalized(int steps) {
    if (steps < 2) return 0.0;
    int index = steps - 1;  // 2->1, 3->2, ..., 16->15
    return static_cast<double>(index) / kQuantizeStepCount;
}

inline void handleLFO1ParamChange(
    LFO1Params& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kLFO1RateId:
            params.rateHz.store(lfoRateFromNormalized(value), std::memory_order_relaxed); break;
        case kLFO1ShapeId:
            params.shape.store(std::clamp(static_cast<int>(value * (kWaveformCount - 1) + 0.5), 0, kWaveformCount - 1), std::memory_order_relaxed); break;
        case kLFO1DepthId:
            params.depth.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed); break;
        case kLFO1SyncId:
            params.sync.store(value >= 0.5, std::memory_order_relaxed); break;
        case kLFO1PhaseOffsetId:
            params.phaseOffset.store(static_cast<float>(value * 360.0), std::memory_order_relaxed); break;
        case kLFO1RetriggerId:
            params.retrigger.store(value >= 0.5, std::memory_order_relaxed); break;
        case kLFO1NoteValueId:
            params.noteValue.store(
                std::clamp(static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                    0, Parameters::kNoteValueDropdownCount - 1),
                std::memory_order_relaxed); break;
        case kLFO1UnipolarId:
            params.unipolar.store(value >= 0.5, std::memory_order_relaxed); break;
        case kLFO1FadeInId:
            params.fadeInMs.store(lfoFadeInFromNormalized(value), std::memory_order_relaxed); break;
        case kLFO1SymmetryId:
            params.symmetry.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed); break;
        case kLFO1QuantizeId:
            params.quantizeSteps.store(lfoQuantizeFromNormalized(value), std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerLFO1Params(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    // Default 1 Hz -> log(1/0.01)/log(5000) ~ 0.540
    parameters.addParameter(STR16("LFO 1 Rate"), STR16("Hz"), 0, 0.540,
        ParameterInfo::kCanAutomate, kLFO1RateId);
    parameters.addParameter(createDropdownParameter(
        STR16("LFO 1 Shape"), kLFO1ShapeId,
        {STR16("Sine"), STR16("Triangle"), STR16("Sawtooth"),
         STR16("Square"), STR16("Sample & Hold"), STR16("Smooth Random")}
    ));
    parameters.addParameter(STR16("LFO 1 Depth"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kLFO1DepthId);
    parameters.addParameter(STR16("LFO 1 Sync"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kLFO1SyncId);
    // Extended params
    parameters.addParameter(STR16("LFO 1 Phase"), STR16("deg"), 0, 0.0,
        ParameterInfo::kCanAutomate, kLFO1PhaseOffsetId);
    parameters.addParameter(STR16("LFO 1 Retrigger"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kLFO1RetriggerId);
    parameters.addParameter(createNoteValueDropdown(
        STR16("LFO 1 Note Value"), kLFO1NoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex));
    parameters.addParameter(STR16("LFO 1 Unipolar"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kLFO1UnipolarId);
    parameters.addParameter(STR16("LFO 1 Fade In"), STR16("ms"), 0, 0.0,
        ParameterInfo::kCanAutomate, kLFO1FadeInId);
    parameters.addParameter(STR16("LFO 1 Symmetry"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kLFO1SymmetryId);
    parameters.addParameter(STR16("LFO 1 Quantize"), STR16(""), kQuantizeStepCount, 0.0,
        ParameterInfo::kCanAutomate, kLFO1QuantizeId);
}

inline Steinberg::tresult formatLFO1Param(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kLFO1RateId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.2f Hz", lfoRateFromNormalized(value));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFO1DepthId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFO1PhaseOffsetId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f deg", value * 360.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFO1FadeInId: {
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
        case kLFO1SymmetryId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kLFO1QuantizeId: {
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

inline void saveLFO1Params(const LFO1Params& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeInt32(params.shape.load(std::memory_order_relaxed));
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sync.load(std::memory_order_relaxed) ? 1 : 0);
}

inline bool loadLFO1Params(LFO1Params& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (!streamer.readFloat(fv)) { return false; } params.rateHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.shape.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.depth.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.sync.store(iv != 0, std::memory_order_relaxed);
    return true;
}

// Extended save/load for v12+
inline void saveLFO1ExtendedParams(const LFO1Params& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.phaseOffset.load(std::memory_order_relaxed));
    streamer.writeInt32(params.retrigger.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
    streamer.writeInt32(params.unipolar.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeFloat(params.fadeInMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.symmetry.load(std::memory_order_relaxed));
    streamer.writeInt32(params.quantizeSteps.load(std::memory_order_relaxed));
}

inline bool loadLFO1ExtendedParams(LFO1Params& params, Steinberg::IBStreamer& streamer) {
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
inline void loadLFO1ParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (streamer.readFloat(fv)) setParam(kLFO1RateId, lfoRateToNormalized(fv));
    if (streamer.readInt32(iv)) setParam(kLFO1ShapeId, static_cast<double>(iv) / (kWaveformCount - 1));
    if (streamer.readFloat(fv)) setParam(kLFO1DepthId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kLFO1SyncId, iv != 0 ? 1.0 : 0.0);
}

template<typename SetParamFunc>
inline void loadLFO1ExtendedParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (streamer.readFloat(fv)) setParam(kLFO1PhaseOffsetId, static_cast<double>(fv) / 360.0);
    if (streamer.readInt32(iv)) setParam(kLFO1RetriggerId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(iv)) setParam(kLFO1NoteValueId, static_cast<double>(iv) / (Parameters::kNoteValueDropdownCount - 1));
    if (streamer.readInt32(iv)) setParam(kLFO1UnipolarId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readFloat(fv)) setParam(kLFO1FadeInId, lfoFadeInToNormalized(fv));
    if (streamer.readFloat(fv)) setParam(kLFO1SymmetryId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kLFO1QuantizeId, lfoQuantizeToNormalized(iv));
}

} // namespace Ruinae
