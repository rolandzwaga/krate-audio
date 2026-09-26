#pragma once

// ==============================================================================
// Vorago - Smear Parameters (ID 700-799)   Phase 12 T021, FR-011/012/013
// ==============================================================================
// Six-function pack contract (plan section 3.4, shape of global_params.h).
//
//   700 Smear Amount       %   [0, 1]   default 0.20  n0 0.20  lin
//   701 Smear Decoherence  %   [0, 1]   default 0.20  n0 0.20  lin
//   702 Smear Tilt             [-1, 1]  default 0.0   n0 0.5   lin
//
// Plain ranges are the engine setter clamps (plan section 3.2: setSmearAmount [0,1],
// setSmearDecoherence [0,1], setSmearTilt [-1,1]).
//
// Stream: 3 x float32 = 12 bytes, ascending ID order.
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

inline constexpr double kSmearAmountMin = 0.0;
inline constexpr double kSmearAmountMax = 1.0;
inline constexpr double kSmearAmountDefault = 0.20;

inline constexpr double kSmearDecoherenceMin = 0.0;
inline constexpr double kSmearDecoherenceMax = 1.0;
inline constexpr double kSmearDecoherenceDefault = 0.20;

inline constexpr double kSmearTiltMin = -1.0;
inline constexpr double kSmearTiltMax = 1.0;
inline constexpr double kSmearTiltDefault = 0.0;

struct SmearParams {
    std::atomic<float> amount{static_cast<float>(kSmearAmountDefault)};            ///< [0, 1]
    std::atomic<float> decoherence{static_cast<float>(kSmearDecoherenceDefault)};  ///< [0, 1]
    std::atomic<float> tilt{static_cast<float>(kSmearTiltDefault)};                ///< [-1, 1]
};

// ==============================================================================
// Parameter Change Handler (caller has rejected non-finite and clamped to [0, 1])
// ==============================================================================

inline void handleSmearParamChange(SmearParams& params, Steinberg::Vst::ParamID id,
                                   Steinberg::Vst::ParamValue value) noexcept {
    switch (id) {
        case kSmearAmountId:
            params.amount.store(
                static_cast<float>(linearFromNormalized(value, kSmearAmountMin, kSmearAmountMax)),
                std::memory_order_relaxed);
            break;
        case kSmearDecoherenceId:
            params.decoherence.store(static_cast<float>(linearFromNormalized(
                                         value, kSmearDecoherenceMin, kSmearDecoherenceMax)),
                                     std::memory_order_relaxed);
            break;
        case kSmearTiltId:
            params.tilt.store(
                static_cast<float>(linearFromNormalized(value, kSmearTiltMin, kSmearTiltMax)),
                std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================

inline void registerSmearParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(
        STR16("Smear Amount"), STR16("%"), 0,
        linearToNormalized(kSmearAmountDefault, kSmearAmountMin, kSmearAmountMax),
        ParameterInfo::kCanAutomate, kSmearAmountId);
    parameters.addParameter(
        STR16("Smear Decoherence"), STR16("%"), 0,
        linearToNormalized(kSmearDecoherenceDefault, kSmearDecoherenceMin, kSmearDecoherenceMax),
        ParameterInfo::kCanAutomate, kSmearDecoherenceId);
    parameters.addParameter(
        STR16("Smear Tilt"), STR16(""), 0,
        linearToNormalized(kSmearTiltDefault, kSmearTiltMin, kSmearTiltMax),
        ParameterInfo::kCanAutomate, kSmearTiltId);
}

// ==============================================================================
// Display Formatting
// ==============================================================================

inline Steinberg::tresult formatSmearParam(Steinberg::Vst::ParamID id,
                                           Steinberg::Vst::ParamValue value,
                                           Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    char8 text[32];
    switch (id) {
        case kSmearAmountId:
            snprintf(text, sizeof(text), "%.0f%%",
                     linearFromNormalized(value, kSmearAmountMin, kSmearAmountMax) * 100.0);
            break;
        case kSmearDecoherenceId:
            snprintf(text, sizeof(text), "%.0f%%",
                     linearFromNormalized(value, kSmearDecoherenceMin, kSmearDecoherenceMax) *
                         100.0);
            break;
        case kSmearTiltId:
            snprintf(text, sizeof(text), "%.2f",
                     linearFromNormalized(value, kSmearTiltMin, kSmearTiltMax));
            break;
        default:
            return kResultFalse;
    }
    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// ==============================================================================
// State Persistence - 12 bytes (3 x float32), ascending ID order
// ==============================================================================

inline void saveSmearParams(const SmearParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.amount.load(std::memory_order_relaxed));
    streamer.writeFloat(params.decoherence.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tilt.load(std::memory_order_relaxed));
}

/// EOF-safe: a failed read returns false and leaves every later field at its CURRENT
/// value. Non-finite floats are skipped (field unchanged); finite floats are clamped
/// to the plain range.
inline bool loadSmearParams(SmearParams& params, Steinberg::IBStreamer& streamer) {
    const auto loadOne = [&streamer](std::atomic<float>& field, double mn, double mx) {
        float v = 0.0f;
        if (!streamer.readFloat(v))
            return false;
        if (Krate::DSP::detail::isFinite(v)) {  // fast-math-immune, db_utils.h:118
            field.store(std::clamp(v, static_cast<float>(mn), static_cast<float>(mx)),
                        std::memory_order_relaxed);
        }
        return true;
    };

    if (!loadOne(params.amount, kSmearAmountMin, kSmearAmountMax))
        return false;
    if (!loadOne(params.decoherence, kSmearDecoherenceMin, kSmearDecoherenceMax))
        return false;
    if (!loadOne(params.tilt, kSmearTiltMin, kSmearTiltMax))
        return false;
    return true;
}

// ==============================================================================
// Controller State Sync (inverts every mapping above)
// ==============================================================================

template <typename SetParamFunc>
inline void loadSmearParamsToController(Steinberg::IBStreamer& streamer,
                                        SetParamFunc setParam) {
    const auto mirrorOne = [&streamer, &setParam](Steinberg::Vst::ParamID id, double mn,
                                                  double mx) {
        float v = 0.0f;
        if (!streamer.readFloat(v))
            return false;
        // Same finite/clamp rule as loadSmearParams, so controller and processor agree.
        if (Krate::DSP::detail::isFinite(v)) {
            const float clamped = std::clamp(v, static_cast<float>(mn), static_cast<float>(mx));
            setParam(id, linearToNormalized(static_cast<double>(clamped), mn, mx));
        }
        return true;
    };

    if (!mirrorOne(kSmearAmountId, kSmearAmountMin, kSmearAmountMax))
        return;
    if (!mirrorOne(kSmearDecoherenceId, kSmearDecoherenceMin, kSmearDecoherenceMax))
        return;
    mirrorOne(kSmearTiltId, kSmearTiltMin, kSmearTiltMax);
}

}  // namespace Vorago
