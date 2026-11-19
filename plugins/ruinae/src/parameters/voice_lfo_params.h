#pragma once
// Voice LFO: the per-voice LFO behind the "Voice LFO" voice-route source.
// Rate uses the same 0.01-50 Hz map as LFO 1/2 and Shape the same six
// waveforms. Defaults (1 Hz, Sine) are what the source played before these
// parameters existed, so states without them keep their sound.
#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "parameters/lfo_params.h"
#include "parameters/state_load_helpers.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

struct VoiceLfoParams {
    static constexpr float kDefaultRateHz = 1.0f;
    static constexpr int kDefaultShape = 0;  // Sine

    std::atomic<float> rateHz{kDefaultRateHz};  // 0.01-50 Hz
    std::atomic<int> shape{kDefaultShape};      // Waveform enum (0-5)
};

inline int voiceLfoShapeFromNormalized(double value) {
    return std::clamp(static_cast<int>(value * (kWaveformCount - 1) + 0.5), 0, kWaveformCount - 1);
}

inline void resetVoiceLfoParams(VoiceLfoParams& params) {
    params.rateHz.store(VoiceLfoParams::kDefaultRateHz, std::memory_order_relaxed);
    params.shape.store(VoiceLfoParams::kDefaultShape, std::memory_order_relaxed);
}

// =============================================================================
// Parameter change handler (processor side)
// =============================================================================

inline void handleVoiceLfoParamChange(
    VoiceLfoParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kVoiceLfoRateId:
            params.rateHz.store(lfoRateFromNormalized(value), std::memory_order_relaxed);
            break;
        case kVoiceLfoShapeId:
            params.shape.store(voiceLfoShapeFromNormalized(value), std::memory_order_relaxed);
            break;
        default: break;
    }
}

// =============================================================================
// Parameter registration (controller side)
// =============================================================================

inline void registerVoiceLfoParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("Voice LFO Rate"), STR16("Hz"), 0,
        lfoRateToNormalized(VoiceLfoParams::kDefaultRateHz),
        ParameterInfo::kCanAutomate, kVoiceLfoRateId);
    parameters.addParameter(createDropdownParameter(
        STR16("Voice LFO Shape"), kVoiceLfoShapeId,
        {STR16("Sine"), STR16("Triangle"), STR16("Sawtooth"),
         STR16("Square"), STR16("Sample & Hold"), STR16("Smooth Random")}
    ));
}

// =============================================================================
// Display formatting
// =============================================================================

inline Steinberg::tresult formatVoiceLfoParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    if (id != kVoiceLfoRateId) return kResultFalse;
    char8 text[32];
    snprintf(text, sizeof(text), "%.2f Hz", static_cast<double>(lfoRateFromNormalized(value)));
    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// =============================================================================
// State persistence (appended after the arp pack, state version 9)
// =============================================================================

inline void saveVoiceLfoParams(const VoiceLfoParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeInt32(params.shape.load(std::memory_order_relaxed));
}

/// A state older than v9, or one that ends before the pack, loads the defaults:
/// the fixed 1 Hz sine the source played before it had parameters.
inline void loadVoiceLfoParams(VoiceLfoParams& params, Steinberg::IBStreamer& streamer,
                               Steinberg::int32 version) {
    resetVoiceLfoParams(params);
    if (version < 9) return;
    float fv = 0.0f;
    Steinberg::int32 iv = 0;
    if (!streamer.readFloat(fv)) return;
    params.rateHz.store(finiteClamp(fv, 0.01f, 50.0f, VoiceLfoParams::kDefaultRateHz),
                        std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) return;
    params.shape.store(std::clamp(static_cast<int>(iv), 0, kWaveformCount - 1),
                       std::memory_order_relaxed);
}

template<typename SetParamFunc>
inline void loadVoiceLfoParamsToController(
    Steinberg::IBStreamer& streamer, const SetParamFunc& setParam, Steinberg::int32 version) {
    VoiceLfoParams params;
    loadVoiceLfoParams(params, streamer, version);
    setParam(kVoiceLfoRateId, lfoRateToNormalized(params.rateHz.load(std::memory_order_relaxed)));
    setParam(kVoiceLfoShapeId,
             static_cast<double>(params.shape.load(std::memory_order_relaxed)) / (kWaveformCount - 1));
}

} // namespace Ruinae
