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
// SampleHoldParams: atomic parameter storage for real-time safety
// =============================================================================

struct SampleHoldParams {
    std::atomic<float> rateHz{4.0f};   // [0.1, 50] Hz (default 4 Hz)
    std::atomic<bool> sync{false};     // tempo sync on/off (default off)
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex}; // default 1/8
    std::atomic<float> slewMs{0.0f};   // [0, 500] ms (default 0 ms)
};

// =============================================================================
// Slew mapping: normalized [0,1] <-> ms [0, 500] (linear)
// ms = normalized * 500.0
// Default 0 ms: norm = 0.0
// =============================================================================

inline float sampleHoldSlewFromNormalized(double normalized) {
    double clamped = std::clamp(normalized, 0.0, 1.0);
    return static_cast<float>(std::clamp(clamped * 500.0, 0.0, 500.0));
}

inline double sampleHoldSlewToNormalized(float ms) {
    double clampedMs = std::clamp(static_cast<double>(ms), 0.0, 500.0);
    return std::clamp(clampedMs / 500.0, 0.0, 1.0);
}

// =============================================================================
// Parameter change handler (processor side)
// =============================================================================

inline void handleSampleHoldParamChange(
    SampleHoldParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kSampleHoldRateId:
            params.rateHz.store(
                std::clamp(lfoRateFromNormalized(value), 0.1f, 50.0f),
                std::memory_order_relaxed);
            break;
        case kSampleHoldSyncId:
            params.sync.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kSampleHoldNoteValueId:
            params.noteValue.store(
                std::clamp(static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                    0, Parameters::kNoteValueDropdownCount - 1),
                std::memory_order_relaxed);
            break;
        case kSampleHoldSlewId:
            params.slewMs.store(
                sampleHoldSlewFromNormalized(value),
                std::memory_order_relaxed);
            break;
        default: break;
    }
}

// =============================================================================
// Parameter registration (controller side)
// =============================================================================

inline void registerSampleHoldParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    // Rate: continuous, log mapping [0.1, 50] Hz, default 4 Hz (norm ~0.702)
    parameters.addParameter(STR16("S&H Rate"), STR16("Hz"), 0, 0.702,
        ParameterInfo::kCanAutomate, kSampleHoldRateId);
    // Sync: boolean toggle, default off
    parameters.addParameter(STR16("S&H Sync"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kSampleHoldSyncId);
    // Note Value: dropdown, default 1/8 (index 10)
    parameters.addParameter(createNoteValueDropdown(
        STR16("S&H Note Value"), kSampleHoldNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex));
    // Slew: continuous, linear [0, 500] ms, default 0 ms (norm 0.0)
    parameters.addParameter(STR16("S&H Slew"), STR16("ms"), 0, 0.0,
        ParameterInfo::kCanAutomate, kSampleHoldSlewId);
}

// =============================================================================
// Display formatting
// =============================================================================

inline Steinberg::tresult formatSampleHoldParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    char8 text[32];
    switch (id) {
        case kSampleHoldRateId:
            snprintf(text, sizeof(text), "%.2f Hz",
                static_cast<double>(std::clamp(lfoRateFromNormalized(value), 0.1f, 50.0f)));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        case kSampleHoldSlewId:
            snprintf(text, sizeof(text), "%.0f ms",
                static_cast<double>(sampleHoldSlewFromNormalized(value)));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        default:
            return kResultFalse;
    }
}

// =============================================================================
// State persistence
// =============================================================================

inline void saveSampleHoldParams(const SampleHoldParams& params,
                                  Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sync.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
    streamer.writeFloat(params.slewMs.load(std::memory_order_relaxed));
}

inline bool loadSampleHoldParams(SampleHoldParams& params,
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
    params.slewMs.store(fv, std::memory_order_relaxed);

    return true;
}

template<typename SetParamFunc>
inline void loadSampleHoldParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    // Rate: read Hz, convert back to normalized
    if (streamer.readFloat(fv))
        setParam(kSampleHoldRateId, lfoRateToNormalized(fv));
    // Sync: read int32, convert to 0.0/1.0
    if (streamer.readInt32(iv))
        setParam(kSampleHoldSyncId, iv != 0 ? 1.0 : 0.0);
    // Note Value: read int32, convert to normalized
    if (streamer.readInt32(iv))
        setParam(kSampleHoldNoteValueId,
            static_cast<double>(iv) / (Parameters::kNoteValueDropdownCount - 1));
    // Slew: read ms, convert back to normalized
    if (streamer.readFloat(fv))
        setParam(kSampleHoldSlewId, sampleHoldSlewToNormalized(fv));
}

} // namespace Ruinae
