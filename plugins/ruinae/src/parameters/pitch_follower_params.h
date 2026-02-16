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
// PitchFollowerParams: atomic parameter storage for real-time safety
// =============================================================================

struct PitchFollowerParams {
    std::atomic<float> minHz{80.0f};       // [20, 500] Hz (default 80 Hz)
    std::atomic<float> maxHz{2000.0f};     // [200, 5000] Hz (default 2000 Hz)
    std::atomic<float> confidence{0.5f};   // [0, 1] (default 0.5)
    std::atomic<float> speedMs{50.0f};     // [10, 300] ms (default 50 ms)
};

// =============================================================================
// Min Hz mapping: normalized [0,1] <-> Hz [20, 500] (logarithmic)
// hz = 20 * pow(25.0, normalized)
// Default 80 Hz: norm = log(80/20) / log(25) = log(4) / log(25) = 0.4307
// =============================================================================

inline float pitchFollowerMinHzFromNormalized(double normalized) {
    double clamped = std::clamp(normalized, 0.0, 1.0);
    return static_cast<float>(std::clamp(
        20.0 * std::pow(25.0, clamped), 20.0, 500.0));
}

inline double pitchFollowerMinHzToNormalized(float hz) {
    double clampedHz = std::clamp(static_cast<double>(hz), 20.0, 500.0);
    return std::clamp(std::log(clampedHz / 20.0) / std::log(25.0), 0.0, 1.0);
}

// =============================================================================
// Max Hz mapping: normalized [0,1] <-> Hz [200, 5000] (logarithmic)
// hz = 200 * pow(25.0, normalized)
// Default 2000 Hz: norm = log(2000/200) / log(25) = log(10) / log(25) = 0.7153
// =============================================================================

inline float pitchFollowerMaxHzFromNormalized(double normalized) {
    double clamped = std::clamp(normalized, 0.0, 1.0);
    return static_cast<float>(std::clamp(
        200.0 * std::pow(25.0, clamped), 200.0, 5000.0));
}

inline double pitchFollowerMaxHzToNormalized(float hz) {
    double clampedHz = std::clamp(static_cast<double>(hz), 200.0, 5000.0);
    return std::clamp(std::log(clampedHz / 200.0) / std::log(25.0), 0.0, 1.0);
}

// =============================================================================
// Speed mapping: normalized [0,1] <-> ms [10, 300] (linear)
// ms = 10 + normalized * 290
// Default 50 ms: norm = (50 - 10) / 290 = 0.1379
// =============================================================================

inline float pitchFollowerSpeedFromNormalized(double normalized) {
    double clamped = std::clamp(normalized, 0.0, 1.0);
    return static_cast<float>(std::clamp(10.0 + clamped * 290.0, 10.0, 300.0));
}

inline double pitchFollowerSpeedToNormalized(float ms) {
    double clampedMs = std::clamp(static_cast<double>(ms), 10.0, 300.0);
    return std::clamp((clampedMs - 10.0) / 290.0, 0.0, 1.0);
}

// =============================================================================
// Parameter change handler (processor side)
// =============================================================================

inline void handlePitchFollowerParamChange(
    PitchFollowerParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kPitchFollowerMinHzId:
            params.minHz.store(
                pitchFollowerMinHzFromNormalized(value),
                std::memory_order_relaxed);
            break;
        case kPitchFollowerMaxHzId:
            params.maxHz.store(
                pitchFollowerMaxHzFromNormalized(value),
                std::memory_order_relaxed);
            break;
        case kPitchFollowerConfidenceId:
            params.confidence.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        case kPitchFollowerSpeedId:
            params.speedMs.store(
                pitchFollowerSpeedFromNormalized(value),
                std::memory_order_relaxed);
            break;
        default: break;
    }
}

// =============================================================================
// Parameter registration (controller side)
// =============================================================================

inline void registerPitchFollowerParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    // Min Hz: continuous, log mapping [20, 500] Hz, default 80 Hz (norm ~0.4307)
    parameters.addParameter(STR16("PF Min Hz"), STR16("Hz"), 0, 0.4307,
        ParameterInfo::kCanAutomate, kPitchFollowerMinHzId);
    // Max Hz: continuous, log mapping [200, 5000] Hz, default 2000 Hz (norm ~0.7153)
    parameters.addParameter(STR16("PF Max Hz"), STR16("Hz"), 0, 0.7153,
        ParameterInfo::kCanAutomate, kPitchFollowerMaxHzId);
    // Confidence: continuous [0, 1], default 0.5
    parameters.addParameter(STR16("PF Confidence"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kPitchFollowerConfidenceId);
    // Speed: continuous, linear mapping [10, 300] ms, default 50 ms (norm ~0.1379)
    parameters.addParameter(STR16("PF Speed"), STR16("ms"), 0, 0.1379,
        ParameterInfo::kCanAutomate, kPitchFollowerSpeedId);
}

// =============================================================================
// Display formatting
// =============================================================================

inline Steinberg::tresult formatPitchFollowerParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    char8 text[32];
    switch (id) {
        case kPitchFollowerMinHzId: {
            float hz = pitchFollowerMinHzFromNormalized(value);
            snprintf(text, sizeof(text), "%.0f Hz", static_cast<double>(hz));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kPitchFollowerMaxHzId: {
            float hz = pitchFollowerMaxHzFromNormalized(value);
            snprintf(text, sizeof(text), "%.0f Hz", static_cast<double>(hz));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kPitchFollowerConfidenceId:
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        case kPitchFollowerSpeedId: {
            float ms = pitchFollowerSpeedFromNormalized(value);
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

inline void savePitchFollowerParams(const PitchFollowerParams& params,
                                     Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.minHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.maxHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.confidence.load(std::memory_order_relaxed));
    streamer.writeFloat(params.speedMs.load(std::memory_order_relaxed));
}

inline bool loadPitchFollowerParams(PitchFollowerParams& params,
                                     Steinberg::IBStreamer& streamer) {
    float fv = 0.0f;

    if (!streamer.readFloat(fv)) { return false; }
    params.minHz.store(fv, std::memory_order_relaxed);

    if (!streamer.readFloat(fv)) { return false; }
    params.maxHz.store(fv, std::memory_order_relaxed);

    if (!streamer.readFloat(fv)) { return false; }
    params.confidence.store(fv, std::memory_order_relaxed);

    if (!streamer.readFloat(fv)) { return false; }
    params.speedMs.store(fv, std::memory_order_relaxed);

    return true;
}

template<typename SetParamFunc>
inline void loadPitchFollowerParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f;

    // Min Hz: read Hz, convert back to normalized
    if (streamer.readFloat(fv))
        setParam(kPitchFollowerMinHzId, pitchFollowerMinHzToNormalized(fv));
    // Max Hz: read Hz, convert back to normalized
    if (streamer.readFloat(fv))
        setParam(kPitchFollowerMaxHzId, pitchFollowerMaxHzToNormalized(fv));
    // Confidence: already in [0,1] range
    if (streamer.readFloat(fv))
        setParam(kPitchFollowerConfidenceId, static_cast<double>(fv));
    // Speed: read ms, convert back to normalized
    if (streamer.readFloat(fv))
        setParam(kPitchFollowerSpeedId, pitchFollowerSpeedToNormalized(fv));
}

} // namespace Ruinae
