#pragma once

// ==============================================================================
// Vorago - Bloom Parameters (ID 1300-1399)   T027, FR-011 / FR-012 / FR-013
// ==============================================================================
// The six-function pack contract of global_params.h (tasks.md Group 7, plan 3.4).
//
//   1300 Bloom Depth       %   [0, 1]      default 0.60   n0 0.60          lin
//   1301 Bloom Spawn Rate  Hz  [0, 0.05]   default 1/240  n0 0.603772849   olog, eps = 1e-4 Hz
//
// Clamp sources: VoragoVoice::setBloomDepth clamps [0, 1] (vorago_voice.h:1457-1471);
// BloomEngine::setSpawnRateHz clamps [0, kMaxSpawnRateHz] (bloom_engine.h:280, 543-547).
// Spawn rate n = 0 maps to exactly 0 Hz (internal clock off); log(0) is never
// evaluated (offsetLogFromNormalized, param_mapping.h).
//
// Stream: float bloomDepth + float bloomSpawnRateHz = 8 bytes, ascending ID order.
// ==============================================================================

#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/bloom_engine.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Vorago {

inline constexpr double kBloomDepthMin = 0.0;
inline constexpr double kBloomDepthMax = 1.0;
inline constexpr double kBloomDepthDefault = 0.60;

inline constexpr double kBloomSpawnRateMinHz = 0.0;
inline constexpr double kBloomSpawnRateMaxHz = 0.05;
inline constexpr double kBloomSpawnRateEpsHz = 1e-4;  // plan 3.3.1
inline constexpr float kBloomSpawnRateDefaultHz = 1.0f / 240.0f;
/// offsetLogToNormalized(1/240, 0, 0.05, 1e-4) (plan 3.2, asserted by Vorago_ParamMapping).
inline constexpr double kBloomSpawnRateDefaultNorm = 0.603772849;

// Float literal vs float constant: MSVC's constant evaluator does not round
// static_cast<float>(0.05) to float before comparing.
static_assert(Krate::DSP::BloomEngine::kMaxSpawnRateHz == 0.05f,
              "spawn-rate range must equal the engine clamp");
static_assert(kBloomSpawnRateDefaultHz == Krate::DSP::BloomEngine::kDefaultSpawnRateHz,
              "spawn-rate default must equal the engine default");

struct BloomParams {
    std::atomic<float> depth{static_cast<float>(kBloomDepthDefault)};             ///< [0, 1]
    std::atomic<float> spawnRateHz{kBloomSpawnRateDefaultHz};  ///< [0, 0.05] Hz
};

// ==============================================================================
// Parameter Change Handler (caller has rejected non-finite and clamped [0, 1])
// ==============================================================================

inline void handleBloomParamChange(BloomParams& params, Steinberg::Vst::ParamID id,
                                   Steinberg::Vst::ParamValue value) noexcept {
    switch (id) {
        case kBloomDepthId:
            params.depth.store(
                static_cast<float>(linearFromNormalized(value, kBloomDepthMin, kBloomDepthMax)),
                std::memory_order_relaxed);
            break;
        case kBloomSpawnRateId:
            params.spawnRateHz.store(
                static_cast<float>(offsetLogFromNormalized(
                    value, kBloomSpawnRateMinHz, kBloomSpawnRateMaxHz, kBloomSpawnRateEpsHz)),
                std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================

inline void registerBloomParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Bloom Depth"), STR16("%"), 0,
                            kBloomDepthDefault,
                            ParameterInfo::kCanAutomate, kBloomDepthId);
    parameters.addParameter(STR16("Bloom Spawn Rate"), STR16("Hz"), 0,
                            kBloomSpawnRateDefaultNorm, ParameterInfo::kCanAutomate,
                            kBloomSpawnRateId);
}

// ==============================================================================
// Display Formatting
// ==============================================================================

inline Steinberg::tresult formatBloomParam(Steinberg::Vst::ParamID id,
                                           Steinberg::Vst::ParamValue value,
                                           Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    char8 text[32];
    switch (id) {
        case kBloomDepthId: {
            const double d = linearFromNormalized(value, kBloomDepthMin, kBloomDepthMax);
            snprintf(text, sizeof(text), "%.0f %%", d * 100.0);
            break;
        }
        case kBloomSpawnRateId: {
            const double hz = offsetLogFromNormalized(value, kBloomSpawnRateMinHz,
                                                      kBloomSpawnRateMaxHz, kBloomSpawnRateEpsHz);
            snprintf(text, sizeof(text), "%.4f Hz", hz);
            break;
        }
        default:
            return kResultFalse;
    }
    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// ==============================================================================
// State Persistence - 8 bytes (float, float)
// ==============================================================================

inline void saveBloomParams(const BloomParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.spawnRateHz.load(std::memory_order_relaxed));
}

/// EOF-safe: a failed read returns false, later fields keep their CURRENT value.
/// Non-finite floats are skipped (field unchanged); finite ones clamp to the plain range.
inline bool loadBloomParams(BloomParams& params, Steinberg::IBStreamer& streamer) {
    float v = 0.0f;

    if (!streamer.readFloat(v)) { return false; }
    if (Krate::DSP::detail::isFinite(v)) {
        params.depth.store(std::clamp(v, static_cast<float>(kBloomDepthMin),
                                      static_cast<float>(kBloomDepthMax)),
                           std::memory_order_relaxed);
    }

    if (!streamer.readFloat(v)) { return false; }
    if (Krate::DSP::detail::isFinite(v)) {
        params.spawnRateHz.store(std::clamp(v, static_cast<float>(kBloomSpawnRateMinHz),
                                            static_cast<float>(kBloomSpawnRateMaxHz)),
                                 std::memory_order_relaxed);
    }

    return true;
}

// ==============================================================================
// Controller State Sync (inverts every mapping above)
// ==============================================================================

template <typename SetParamFunc>
inline void loadBloomParamsToController(Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float v = 0.0f;

    if (!streamer.readFloat(v)) { return; }
    if (Krate::DSP::detail::isFinite(v)) {
        setParam(kBloomDepthId,
                 linearToNormalized(static_cast<double>(v), kBloomDepthMin, kBloomDepthMax));
    }

    if (!streamer.readFloat(v)) { return; }
    if (Krate::DSP::detail::isFinite(v)) {
        setParam(kBloomSpawnRateId,
                 offsetLogToNormalized(static_cast<double>(v), kBloomSpawnRateMinHz,
                                       kBloomSpawnRateMaxHz, kBloomSpawnRateEpsHz));
    }
}

}  // namespace Vorago
