#pragma once
#include "plugin_ids.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace Ruinae {

// =============================================================================
// EnvFollowerParams: atomic parameter storage for real-time safety
// =============================================================================

struct EnvFollowerParams {
    std::atomic<float> sensitivity{0.5f};  // [0, 1] (default 0.5)
    std::atomic<float> attackMs{10.0f};    // [0.1, 500] ms (default 10 ms)
    std::atomic<float> releaseMs{100.0f};  // [1, 5000] ms (default 100 ms)
};

// =============================================================================
// Attack mapping: normalized [0,1] <-> ms [0.1, 500] (logarithmic)
// ms = 0.1 * pow(5000.0, normalized)
// Default 10 ms: norm = log(10/0.1) / log(5000) = log(100) / log(5000) = 0.5406
// =============================================================================

inline float envFollowerAttackFromNormalized(double normalized) {
    double clamped = std::clamp(normalized, 0.0, 1.0);
    return static_cast<float>(std::clamp(
        0.1 * std::pow(5000.0, clamped), 0.1, 500.0));
}

inline double envFollowerAttackToNormalized(float ms) {
    double clampedMs = std::clamp(static_cast<double>(ms), 0.1, 500.0);
    return std::clamp(std::log(clampedMs / 0.1) / std::log(5000.0), 0.0, 1.0);
}

// =============================================================================
// Release mapping: normalized [0,1] <-> ms [1, 5000] (logarithmic)
// ms = 1.0 * pow(5000.0, normalized)
// Default 100 ms: norm = log(100/1.0) / log(5000) = log(100) / log(5000) = 0.5406
// =============================================================================

inline float envFollowerReleaseFromNormalized(double normalized) {
    double clamped = std::clamp(normalized, 0.0, 1.0);
    return static_cast<float>(std::clamp(
        1.0 * std::pow(5000.0, clamped), 1.0, 5000.0));
}

inline double envFollowerReleaseToNormalized(float ms) {
    double clampedMs = std::clamp(static_cast<double>(ms), 1.0, 5000.0);
    return std::clamp(std::log(clampedMs / 1.0) / std::log(5000.0), 0.0, 1.0);
}

// =============================================================================
// Parameter change handler (processor side)
// =============================================================================

inline void handleEnvFollowerParamChange(
    EnvFollowerParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kEnvFollowerSensitivityId:
            params.sensitivity.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        case kEnvFollowerAttackId:
            params.attackMs.store(
                envFollowerAttackFromNormalized(value),
                std::memory_order_relaxed);
            break;
        case kEnvFollowerReleaseId:
            params.releaseMs.store(
                envFollowerReleaseFromNormalized(value),
                std::memory_order_relaxed);
            break;
        default: break;
    }
}

// =============================================================================
// Parameter registration (controller side)
// =============================================================================

inline void registerEnvFollowerParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    // Sensitivity: continuous [0, 1], default 0.5
    parameters.addParameter(STR16("EF Sensitivity"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kEnvFollowerSensitivityId);
    // Attack: continuous, log mapping [0.1, 500] ms, default 10 ms (norm ~0.5406)
    parameters.addParameter(STR16("EF Attack"), STR16("ms"), 0, 0.5406,
        ParameterInfo::kCanAutomate, kEnvFollowerAttackId);
    // Release: continuous, log mapping [1, 5000] ms, default 100 ms (norm ~0.5406)
    parameters.addParameter(STR16("EF Release"), STR16("ms"), 0, 0.5406,
        ParameterInfo::kCanAutomate, kEnvFollowerReleaseId);
}

// =============================================================================
// Display formatting
// =============================================================================

inline Steinberg::tresult formatEnvFollowerParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    char8 text[32];
    switch (id) {
        case kEnvFollowerSensitivityId:
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        case kEnvFollowerAttackId: {
            float ms = envFollowerAttackFromNormalized(value);
            if (ms < 100.0f) {
                snprintf(text, sizeof(text), "%.1f ms", static_cast<double>(ms));
            } else {
                snprintf(text, sizeof(text), "%.0f ms", static_cast<double>(ms));
            }
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kEnvFollowerReleaseId: {
            float ms = envFollowerReleaseFromNormalized(value);
            if (ms < 100.0f) {
                snprintf(text, sizeof(text), "%.1f ms", static_cast<double>(ms));
            } else {
                snprintf(text, sizeof(text), "%.0f ms", static_cast<double>(ms));
            }
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

inline void saveEnvFollowerParams(const EnvFollowerParams& params,
                                   Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.sensitivity.load(std::memory_order_relaxed));
    streamer.writeFloat(params.attackMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.releaseMs.load(std::memory_order_relaxed));
}

inline bool loadEnvFollowerParams(EnvFollowerParams& params,
                                   Steinberg::IBStreamer& streamer) {
    float fv = 0.0f;

    if (!streamer.readFloat(fv)) { return false; }
    params.sensitivity.store(fv, std::memory_order_relaxed);

    if (!streamer.readFloat(fv)) { return false; }
    params.attackMs.store(fv, std::memory_order_relaxed);

    if (!streamer.readFloat(fv)) { return false; }
    params.releaseMs.store(fv, std::memory_order_relaxed);

    return true;
}

template<typename SetParamFunc>
inline void loadEnvFollowerParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f;

    // Sensitivity: already in [0,1] range
    if (streamer.readFloat(fv))
        setParam(kEnvFollowerSensitivityId, static_cast<double>(fv));
    // Attack: read ms, convert back to normalized
    if (streamer.readFloat(fv))
        setParam(kEnvFollowerAttackId, envFollowerAttackToNormalized(fv));
    // Release: read ms, convert back to normalized
    if (streamer.readFloat(fv))
        setParam(kEnvFollowerReleaseId, envFollowerReleaseToNormalized(fv));
}

} // namespace Ruinae
