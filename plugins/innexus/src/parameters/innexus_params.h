#pragma once

// ==============================================================================
// Innexus M1 Parameter Registration Helpers
// ==============================================================================
// Follows the Ruinae parameter registration pattern:
// - Struct holds atomic parameter values (for processor)
// - Free functions for registration, parameter change handling, save/load
//
// Parameters:
//   kReleaseTimeId (200): Note-off release time, 20-5000ms, default 100ms
//   kInharmonicityAmountId (201): Harmonic vs source inharmonicity, 0-100%, default 100%
// ==============================================================================

#include "plugin_ids.h"

#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>

namespace Innexus {

// ==============================================================================
// M1 Parameter Struct
// ==============================================================================

struct InnexusParams
{
    // Release Time: stored in ms (20-5000), default 100ms
    std::atomic<float> releaseTimeMs{100.0f};

    // Inharmonicity Amount: stored as 0-1 linear (0% to 100%), default 1.0 (100%)
    std::atomic<float> inharmonicityAmount{1.0f};
};

// ==============================================================================
// Normalization Helpers
// ==============================================================================
// Release time uses exponential mapping: 20ms to 5000ms
// normalized = log(value/20) / log(5000/20)  =>  value = 20 * (5000/20)^normalized

inline float releaseTimeFromNormalized(double normalized)
{
    constexpr float kMinMs = 20.0f;
    constexpr float kMaxMs = 5000.0f;
    constexpr float kRatio = kMaxMs / kMinMs; // 250

    auto value = kMinMs * std::pow(kRatio, static_cast<float>(normalized));
    return std::clamp(value, kMinMs, kMaxMs);
}

inline double releaseTimeToNormalized(float valueMs)
{
    constexpr float kMinMs = 20.0f;
    constexpr float kMaxMs = 5000.0f;
    constexpr float kRatio = kMaxMs / kMinMs;

    auto clamped = std::clamp(valueMs, kMinMs, kMaxMs);
    return static_cast<double>(std::log(clamped / kMinMs) / std::log(kRatio));
}

// ==============================================================================
// Parameter Change Handling (called from Processor::process)
// ==============================================================================

inline void handleInnexusParamChange(
    InnexusParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value)
{
    switch (id)
    {
    case kReleaseTimeId:
        params.releaseTimeMs.store(
            releaseTimeFromNormalized(value),
            std::memory_order_relaxed);
        break;
    case kInharmonicityAmountId:
        // 0-1 normalized maps directly to 0-1 amount
        params.inharmonicityAmount.store(
            std::clamp(static_cast<float>(value), 0.0f, 1.0f),
            std::memory_order_relaxed);
        break;
    default:
        break;
    }
}

// ==============================================================================
// Parameter Registration (called from Controller::initialize)
// ==============================================================================

inline void registerInnexusParams(
    Steinberg::Vst::ParameterContainer& parameters)
{
    using namespace Steinberg::Vst;

    // Release Time (20-5000ms, default 100ms)
    // Uses RangeParameter for proper plain<->normalized mapping
    auto* releaseParam = new RangeParameter(
        STR16("Release Time"), kReleaseTimeId,
        STR16("ms"),
        20.0,    // min
        5000.0,  // max
        100.0,   // default
        0,       // stepCount (continuous)
        ParameterInfo::kCanAutomate);
    parameters.addParameter(releaseParam);

    // Inharmonicity Amount (0-100%, default 100%)
    parameters.addParameter(
        STR16("Inharmonicity"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate,
        kInharmonicityAmountId);
}

// ==============================================================================
// State Persistence
// ==============================================================================

inline void saveInnexusParams(
    const InnexusParams& params,
    Steinberg::IBStreamer& streamer)
{
    streamer.writeFloat(params.releaseTimeMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.inharmonicityAmount.load(std::memory_order_relaxed));
}

inline bool loadInnexusParams(
    InnexusParams& params,
    Steinberg::IBStreamer& streamer)
{
    float floatVal = 0.0f;

    if (!streamer.readFloat(floatVal))
        return false;
    params.releaseTimeMs.store(
        std::clamp(floatVal, 20.0f, 5000.0f),
        std::memory_order_relaxed);

    if (!streamer.readFloat(floatVal))
        return false;
    params.inharmonicityAmount.store(
        std::clamp(floatVal, 0.0f, 1.0f),
        std::memory_order_relaxed);

    return true;
}

// Type alias for controller state loading
using SetParamFunc = std::function<void(Steinberg::Vst::ParamID, double)>;

inline void loadInnexusParamsToController(
    Steinberg::IBStreamer& streamer,
    SetParamFunc setParam)
{
    float floatVal = 0.0f;

    // Release Time: engine value (ms) -> normalized
    if (streamer.readFloat(floatVal))
        setParam(kReleaseTimeId, releaseTimeToNormalized(floatVal));

    // Inharmonicity: stored value (0-1) = normalized (0-1)
    if (streamer.readFloat(floatVal))
        setParam(kInharmonicityAmountId, static_cast<double>(floatVal));
}

} // namespace Innexus
