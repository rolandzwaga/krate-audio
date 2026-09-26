#pragma once

// ==============================================================================
// Vorago - Ecosystem Parameters (ID 900-999)   T023, FR-011/FR-012/FR-013
// ==============================================================================
// Pack contract of plan section 3.4 (shape of global_params.h):
//   900 Ecosystem Depth, %, [0, 1], default 0.85, n0 0.85, linear.
// Route MB EcosystemDepth; plain-range clamp == VoragoVoice::setEcosystemDepth
// (vorago_voice.h:1407, std::clamp(d, 0, 1)).
//
// Stream: float depth = 4 bytes.
// ==============================================================================

#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Vorago {

inline constexpr double kEcosystemDepthMin = 0.0;
inline constexpr double kEcosystemDepthMax = 1.0;

struct EcosystemParams {
    std::atomic<float> depth{0.85f};  ///< [0, 1]; plain == normalized (linear)
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================

inline void handleEcosystemParamChange(EcosystemParams& params, Steinberg::Vst::ParamID id,
                                       Steinberg::Vst::ParamValue value) noexcept {
    switch (id) {
        case kEcosystemDepthId:
            params.depth.store(static_cast<float>(linearFromNormalized(
                                   value, kEcosystemDepthMin, kEcosystemDepthMax)),
                               std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================

inline void registerEcosystemParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("Ecosystem Depth"), STR16("%"), 0, 0.85,
                            ParameterInfo::kCanAutomate, kEcosystemDepthId);
}

// ==============================================================================
// Display Formatting
// ==============================================================================

inline Steinberg::tresult formatEcosystemParam(Steinberg::Vst::ParamID id,
                                               Steinberg::Vst::ParamValue value,
                                               Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    if (id == kEcosystemDepthId) {
        const double plain = linearFromNormalized(value, kEcosystemDepthMin, kEcosystemDepthMax);
        char8 text[32];
        snprintf(text, sizeof(text), "%.0f %%", plain * 100.0);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    return kResultFalse;
}

// ==============================================================================
// State Persistence - 4 bytes (float)
// ==============================================================================

inline void saveEcosystemParams(const EcosystemParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
}

/// EOF-safe: a failed read returns false and leaves the field at its CURRENT value.
/// Non-finite values are rejected (field unchanged); finite values are clamped.
inline bool loadEcosystemParams(EcosystemParams& params, Steinberg::IBStreamer& streamer) {
    float d = 0.0f;
    if (!streamer.readFloat(d)) { return false; }
    if (Krate::DSP::detail::isFinite(d)) {  // fast-math-immune, db_utils.h:118
        params.depth.store(std::clamp(d, static_cast<float>(kEcosystemDepthMin),
                                      static_cast<float>(kEcosystemDepthMax)),
                           std::memory_order_relaxed);
    }
    return true;
}

// ==============================================================================
// Controller State Sync (inverse map)
// ==============================================================================

template <typename SetParamFunc>
inline void loadEcosystemParamsToController(Steinberg::IBStreamer& streamer,
                                            SetParamFunc setParam) {
    float d = 0.0f;
    if (!streamer.readFloat(d)) { return; }
    if (Krate::DSP::detail::isFinite(d)) {
        setParam(kEcosystemDepthId,
                 linearToNormalized(static_cast<double>(d), kEcosystemDepthMin,
                                    kEcosystemDepthMax));
    }
}

}  // namespace Vorago
