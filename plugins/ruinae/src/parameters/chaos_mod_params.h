#pragma once
#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "parameters/note_value_ui.h"
#include "parameters/state_load_helpers.h"
#include "parameters/lfo1_params.h"  // for lfoRateFromNormalized/lfoRateToNormalized
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

// Chaos speed: 0-1 -> 0.05-20, the range ChaosModSource honours (kMinSpeed / kMaxSpeed). It is a
// dimensionless multiplier on the attractor step, not a frequency (R-184).
inline float chaosSpeedFromNormalized(double value) {
    return static_cast<float>(logMapFromNormalized(value, 0.05, 20.0));
}

inline double chaosSpeedToNormalized(float speed) {
    return logMapToNormalized(static_cast<double>(speed), 0.05, 20.0);
}

struct ChaosModParams {
    std::atomic<float> rateHz{1.0f};   // 0.05-20 speed multiplier (log map)
    std::atomic<int> type{0};          // 0=Lorenz, 1=Rossler
    std::atomic<float> depth{1.0f};    // 0-1
    std::atomic<bool> sync{false};     // tempo sync on/off
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex}; // default 1/8 note
};

inline void handleChaosModParamChange(
    ChaosModParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kChaosModRateId:
            params.rateHz.store(chaosSpeedFromNormalized(value), std::memory_order_relaxed); break;
        case kChaosModTypeId:
            params.type.store(std::clamp(static_cast<int>(value * (kChaosTypeCount - 1) + 0.5), 0, kChaosTypeCount - 1), std::memory_order_relaxed); break;
        case kChaosModDepthId:
            params.depth.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed); break;
        case kChaosModSyncId:
            params.sync.store(value >= 0.5, std::memory_order_relaxed); break;
        case kChaosModNoteValueId:
            params.noteValue.store(
                std::clamp(static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                    0, Parameters::kNoteValueDropdownCount - 1),
                std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerChaosModParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("Chaos Rate"), STR16("x"), 0, chaosSpeedToNormalized(1.0f),
        ParameterInfo::kCanAutomate, kChaosModRateId);
    parameters.addParameter(createDropdownParameter(
        STR16("Chaos Type"), kChaosModTypeId,
        {STR16("Lorenz"), STR16("Rossler")}
    ));
    parameters.addParameter(STR16("Chaos Depth"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kChaosModDepthId);
    parameters.addParameter(STR16("Chaos Sync"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kChaosModSyncId);
    parameters.addParameter(createNoteValueDropdown(
        STR16("Chaos Note Value"), kChaosModNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex));
}

inline Steinberg::tresult formatChaosModParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kChaosModRateId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.2fx", chaosSpeedFromNormalized(value));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kChaosModDepthId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveChaosModParams(const ChaosModParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeInt32(params.type.load(std::memory_order_relaxed));
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sync.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
}

// Saved-field ranges, read by both parsers (R-211)
namespace ChaosModLoad {
inline constexpr LoadedFloat kRate{.lo = 0.05f, .hi = 20.0f, .def = 1.0f};
inline constexpr LoadedInt kType{.lo = 0, .hi = kChaosTypeCount - 1};
inline constexpr LoadedFloat kDepth{.lo = 0.0f, .hi = 1.0f, .def = 1.0f};
inline constexpr LoadedInt kNoteValue{.lo = 0, .hi = Parameters::kNoteValueDropdownCount - 1};
} // namespace ChaosModLoad

inline bool loadChaosModParams(ChaosModParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (!streamer.readFloat(fv)) { return false; } params.rateHz.store(ChaosModLoad::kRate(fv), std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.type.store(ChaosModLoad::kType(iv), std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.depth.store(ChaosModLoad::kDepth(fv), std::memory_order_relaxed);
    // New fields - optional for backward compat with old presets
    if (streamer.readInt32(iv)) params.sync.store(iv != 0, std::memory_order_relaxed);
    if (streamer.readInt32(iv)) params.noteValue.store(ChaosModLoad::kNoteValue(iv), std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadChaosModParamsToController(
    Steinberg::IBStreamer& streamer, const SetParamFunc& setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (streamer.readFloat(fv)) setParam(kChaosModRateId, chaosSpeedToNormalized(ChaosModLoad::kRate(fv)));
    if (streamer.readInt32(iv)) setParam(kChaosModTypeId, static_cast<double>(ChaosModLoad::kType(iv)) / (kChaosTypeCount - 1));
    if (streamer.readFloat(fv)) setParam(kChaosModDepthId, static_cast<double>(ChaosModLoad::kDepth(fv)));
    // New fields - optional for backward compat
    if (streamer.readInt32(iv)) setParam(kChaosModSyncId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(iv)) setParam(kChaosModNoteValueId, static_cast<double>(ChaosModLoad::kNoteValue(iv)) / (Parameters::kNoteValueDropdownCount - 1));
}

} // namespace Ruinae
