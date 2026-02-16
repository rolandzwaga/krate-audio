#pragma once
#include "plugin_ids.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

// =============================================================================
// TransientParams: atomic parameter storage for real-time safety
// =============================================================================

struct TransientParams {
    std::atomic<float> sensitivity{0.5f};  // [0, 1] (default 0.5)
    std::atomic<float> attackMs{2.0f};     // [0.5, 10] ms (default 2 ms)
    std::atomic<float> decayMs{50.0f};     // [20, 200] ms (default 50 ms)
};

// =============================================================================
// Attack mapping: normalized [0,1] <-> ms [0.5, 10] (linear)
// ms = 0.5 + normalized * 9.5
// Default 2 ms: norm = (2 - 0.5) / 9.5 = 0.1579
// =============================================================================

inline float transientAttackFromNormalized(double normalized) {
    double clamped = std::clamp(normalized, 0.0, 1.0);
    return static_cast<float>(std::clamp(
        0.5 + clamped * 9.5, 0.5, 10.0));
}

inline double transientAttackToNormalized(float ms) {
    double clampedMs = std::clamp(static_cast<double>(ms), 0.5, 10.0);
    return std::clamp((clampedMs - 0.5) / 9.5, 0.0, 1.0);
}

// =============================================================================
// Decay mapping: normalized [0,1] <-> ms [20, 200] (linear)
// ms = 20 + normalized * 180
// Default 50 ms: norm = (50 - 20) / 180 = 0.1667
// =============================================================================

inline float transientDecayFromNormalized(double normalized) {
    double clamped = std::clamp(normalized, 0.0, 1.0);
    return static_cast<float>(std::clamp(
        20.0 + clamped * 180.0, 20.0, 200.0));
}

inline double transientDecayToNormalized(float ms) {
    double clampedMs = std::clamp(static_cast<double>(ms), 20.0, 200.0);
    return std::clamp((clampedMs - 20.0) / 180.0, 0.0, 1.0);
}

// =============================================================================
// Parameter change handler (processor side)
// =============================================================================

inline void handleTransientParamChange(
    TransientParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kTransientSensitivityId:
            params.sensitivity.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        case kTransientAttackId:
            params.attackMs.store(
                transientAttackFromNormalized(value),
                std::memory_order_relaxed);
            break;
        case kTransientDecayId:
            params.decayMs.store(
                transientDecayFromNormalized(value),
                std::memory_order_relaxed);
            break;
        default: break;
    }
}

// =============================================================================
// Parameter registration (controller side)
// =============================================================================

inline void registerTransientParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    // Sensitivity: continuous [0, 1], default 0.5
    parameters.addParameter(STR16("Trn Sensitivity"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kTransientSensitivityId);
    // Attack: continuous, linear mapping [0.5, 10] ms, default 2 ms (norm ~0.1579)
    parameters.addParameter(STR16("Trn Attack"), STR16("ms"), 0, 0.1579,
        ParameterInfo::kCanAutomate, kTransientAttackId);
    // Decay: continuous, linear mapping [20, 200] ms, default 50 ms (norm ~0.1667)
    parameters.addParameter(STR16("Trn Decay"), STR16("ms"), 0, 0.1667,
        ParameterInfo::kCanAutomate, kTransientDecayId);
}

// =============================================================================
// Display formatting
// =============================================================================

inline Steinberg::tresult formatTransientParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    char8 text[32];
    switch (id) {
        case kTransientSensitivityId:
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        case kTransientAttackId: {
            float ms = transientAttackFromNormalized(value);
            snprintf(text, sizeof(text), "%.1f ms", static_cast<double>(ms));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kTransientDecayId: {
            float ms = transientDecayFromNormalized(value);
            snprintf(text, sizeof(text), "%.0f ms", static_cast<double>(ms));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default:
            return kResultFalse;
    }
}

// =============================================================================
// State persistence
// =============================================================================

inline void saveTransientParams(const TransientParams& params,
                                 Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.sensitivity.load(std::memory_order_relaxed));
    streamer.writeFloat(params.attackMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.decayMs.load(std::memory_order_relaxed));
}

inline bool loadTransientParams(TransientParams& params,
                                 Steinberg::IBStreamer& streamer) {
    float fv = 0.0f;

    if (!streamer.readFloat(fv)) { return false; }
    params.sensitivity.store(fv, std::memory_order_relaxed);

    if (!streamer.readFloat(fv)) { return false; }
    params.attackMs.store(fv, std::memory_order_relaxed);

    if (!streamer.readFloat(fv)) { return false; }
    params.decayMs.store(fv, std::memory_order_relaxed);

    return true;
}

template<typename SetParamFunc>
inline void loadTransientParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f;

    // Sensitivity: already in [0,1] range
    if (streamer.readFloat(fv))
        setParam(kTransientSensitivityId, static_cast<double>(fv));
    // Attack: read ms, convert back to normalized
    if (streamer.readFloat(fv))
        setParam(kTransientAttackId, transientAttackToNormalized(fv));
    // Decay: read ms, convert back to normalized
    if (streamer.readFloat(fv))
        setParam(kTransientDecayId, transientDecayToNormalized(fv));
}

} // namespace Ruinae
