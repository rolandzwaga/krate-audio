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
// RunglerParams: atomic parameter storage for real-time safety
// =============================================================================

struct RunglerParams {
    std::atomic<float> osc1FreqHz{2.0f};  // [0.1, 100] Hz UI range
    std::atomic<float> osc2FreqHz{3.0f};  // [0.1, 100] Hz UI range
    std::atomic<float> depth{0.0f};       // [0, 1] cross-mod depth
    std::atomic<float> filter{0.0f};      // [0, 1] CV smoothing
    std::atomic<int> bits{8};             // [4, 16] shift register bits
    std::atomic<bool> loopMode{false};    // false=chaos, true=loop
};

// =============================================================================
// Frequency mapping: normalized [0,1] <-> Hz [0.1, 100] (logarithmic)
// =============================================================================

inline float runglerFreqFromNormalized(double normalized) {
    double clamped = std::clamp(normalized, 0.0, 1.0);
    return static_cast<float>(std::clamp(
        0.1 * std::pow(1000.0, clamped), 0.1, 100.0));
}

inline double runglerFreqToNormalized(float hz) {
    double clampedHz = std::clamp(static_cast<double>(hz), 0.1, 100.0);
    return std::clamp(std::log(clampedHz / 0.1) / std::log(1000.0), 0.0, 1.0);
}

// =============================================================================
// Bits mapping: normalized [0,1] <-> bits [4, 16] (stepCount=12)
// =============================================================================

inline int runglerBitsFromNormalized(double normalized) {
    return 4 + std::clamp(static_cast<int>(normalized * 12 + 0.5), 0, 12);
}

inline double runglerBitsToNormalized(int bits) {
    return static_cast<double>(std::clamp(bits, 4, 16) - 4) / 12.0;
}

// =============================================================================
// Parameter change handler (processor side)
// =============================================================================

inline void handleRunglerParamChange(
    RunglerParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kRunglerOsc1FreqId:
            params.osc1FreqHz.store(
                runglerFreqFromNormalized(value),
                std::memory_order_relaxed);
            break;
        case kRunglerOsc2FreqId:
            params.osc2FreqHz.store(
                runglerFreqFromNormalized(value),
                std::memory_order_relaxed);
            break;
        case kRunglerDepthId:
            params.depth.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        case kRunglerFilterId:
            params.filter.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        case kRunglerBitsId:
            params.bits.store(
                runglerBitsFromNormalized(value),
                std::memory_order_relaxed);
            break;
        case kRunglerLoopModeId:
            params.loopMode.store(
                value >= 0.5,
                std::memory_order_relaxed);
            break;
        default: break;
    }
}

// =============================================================================
// Parameter registration (controller side)
// =============================================================================

inline void registerRunglerParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    // Osc1 Freq: continuous, log mapping [0.1, 100] Hz, default 2.0 Hz (norm ~0.4337)
    parameters.addParameter(STR16("Rng Osc1 Freq"), STR16("Hz"), 0, 0.4337,
        ParameterInfo::kCanAutomate, kRunglerOsc1FreqId);
    // Osc2 Freq: continuous, log mapping [0.1, 100] Hz, default 3.0 Hz (norm ~0.4924)
    parameters.addParameter(STR16("Rng Osc2 Freq"), STR16("Hz"), 0, 0.4924,
        ParameterInfo::kCanAutomate, kRunglerOsc2FreqId);
    // Depth: continuous [0, 1], default 0
    parameters.addParameter(STR16("Rng Depth"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kRunglerDepthId);
    // Filter: continuous [0, 1], default 0
    parameters.addParameter(STR16("Rng Filter"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kRunglerFilterId);
    // Bits: discrete [4, 16], stepCount=12, default 8 (norm 0.3333)
    parameters.addParameter(STR16("Rng Bits"), STR16(""), 12, 0.3333,
        ParameterInfo::kCanAutomate, kRunglerBitsId);
    // Loop Mode: boolean, default off (chaos mode)
    parameters.addParameter(STR16("Rng Loop Mode"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kRunglerLoopModeId);
}

// =============================================================================
// Display formatting
// =============================================================================

inline Steinberg::tresult formatRunglerParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    char8 text[32];
    switch (id) {
        case kRunglerOsc1FreqId:
        case kRunglerOsc2FreqId: {
            float hz = runglerFreqFromNormalized(value);
            snprintf(text, sizeof(text), "%.2f Hz", static_cast<double>(hz));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kRunglerDepthId:
        case kRunglerFilterId:
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        case kRunglerBitsId: {
            int bits = runglerBitsFromNormalized(value);
            snprintf(text, sizeof(text), "%d", bits);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kRunglerLoopModeId:
            // Let framework handle on/off display
            return kResultFalse;
        default:
            return kResultFalse;
    }
}

// =============================================================================
// State persistence
// =============================================================================

inline void saveRunglerParams(const RunglerParams& params,
                              Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.osc1FreqHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.osc2FreqHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.filter.load(std::memory_order_relaxed));
    streamer.writeInt32(static_cast<Steinberg::int32>(
        params.bits.load(std::memory_order_relaxed)));
    streamer.writeInt32(params.loopMode.load(std::memory_order_relaxed) ? 1 : 0);
}

inline bool loadRunglerParams(RunglerParams& params,
                              Steinberg::IBStreamer& streamer) {
    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    if (!streamer.readFloat(fv)) { return false; }
    params.osc1FreqHz.store(fv, std::memory_order_relaxed);

    if (!streamer.readFloat(fv)) { return false; }
    params.osc2FreqHz.store(fv, std::memory_order_relaxed);

    if (!streamer.readFloat(fv)) { return false; }
    params.depth.store(fv, std::memory_order_relaxed);

    if (!streamer.readFloat(fv)) { return false; }
    params.filter.store(fv, std::memory_order_relaxed);

    if (!streamer.readInt32(iv)) { return false; }
    params.bits.store(static_cast<int>(iv), std::memory_order_relaxed);

    if (!streamer.readInt32(iv)) { return false; }
    params.loopMode.store(iv != 0, std::memory_order_relaxed);

    return true;
}

template<typename SetParamFunc>
inline void loadRunglerParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f;
    Steinberg::int32 iv = 0;

    // Osc1 freq: read Hz, convert back to normalized
    if (streamer.readFloat(fv))
        setParam(kRunglerOsc1FreqId, runglerFreqToNormalized(fv));
    // Osc2 freq: read Hz, convert back to normalized
    if (streamer.readFloat(fv))
        setParam(kRunglerOsc2FreqId, runglerFreqToNormalized(fv));
    // Depth: already in [0,1] range
    if (streamer.readFloat(fv))
        setParam(kRunglerDepthId, static_cast<double>(fv));
    // Filter: already in [0,1] range
    if (streamer.readFloat(fv))
        setParam(kRunglerFilterId, static_cast<double>(fv));
    // Bits: read int32, convert back to normalized
    if (streamer.readInt32(iv))
        setParam(kRunglerBitsId, runglerBitsToNormalized(static_cast<int>(iv)));
    // Loop mode: read int32, 0 or 1
    if (streamer.readInt32(iv))
        setParam(kRunglerLoopModeId, iv != 0 ? 1.0 : 0.0);
}

} // namespace Ruinae
