#pragma once

// ==============================================================================
// Vorago Phase 12 - Events parameter pack (ID 800-899)   FR-011, FR-012, FR-013
// ==============================================================================
// Shared pack contract (tasks.md Group 7; shape of global_params.h):
//   800 Event Rate, x, [0.1, 10], default 1.0, n0 0.5 (exact log midpoint), log.
// Route MB EventRateScale (plan section 3.2). Plain-range clamp == the destination
// setter clamp VoragoVoice::setEventRateScale, vorago_voice.h:1447-1451.
//
// Stream: float eventRateScale = 4 bytes.
// ==============================================================================

#include "plugin_ids.h"

#include "ui/parameter_helpers.h"  // plugins/shared/src/ui/parameter_helpers.h

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Vorago {

inline constexpr double kEventsRateScaleMin = 0.1;
inline constexpr double kEventsRateScaleMax = 10.0;

struct EventsParams {
    std::atomic<float> eventRateScale{1.0f};  ///< x, [0.1, 10], log taper; n0 0.5
};

// ==============================================================================
// Parameter Change Handler (caller has rejected non-finite and clamped to [0, 1])
// ==============================================================================

inline void handleEventsParamChange(EventsParams& params, Steinberg::Vst::ParamID id,
                                    Steinberg::Vst::ParamValue value) noexcept {
    switch (id) {
        case kEventsRateScaleId:
            params.eventRateScale.store(
                static_cast<float>(Krate::Plugins::logMapFromNormalized(
                    value, kEventsRateScaleMin, kEventsRateScaleMax)),
                std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================

inline void registerEventsParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    // Default 1.0 x == log midpoint of [0.1, 10] -> normalized 0.5 exactly.
    parameters.addParameter(STR16("Event Rate"), STR16("x"), 0, 0.5,
                            ParameterInfo::kCanAutomate, kEventsRateScaleId);
}

// ==============================================================================
// Display Formatting
// ==============================================================================

inline Steinberg::tresult formatEventsParam(Steinberg::Vst::ParamID id,
                                            Steinberg::Vst::ParamValue value,
                                            Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    if (id == kEventsRateScaleId) {
        const double scale =
            Krate::Plugins::logMapFromNormalized(value, kEventsRateScaleMin, kEventsRateScaleMax);
        char8 text[32];
        snprintf(text, sizeof(text), "%.2f x", scale);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    return kResultFalse;
}

// ==============================================================================
// State Persistence - 4 bytes (float)
// ==============================================================================

inline void saveEventsParams(const EventsParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.eventRateScale.load(std::memory_order_relaxed));
}

/// EOF-safe: a failed read returns false and leaves the field at its CURRENT value.
/// A non-finite value is skipped (field unchanged); a finite one is clamped to the
/// setter range.
inline bool loadEventsParams(EventsParams& params, Steinberg::IBStreamer& streamer) {
    float s = 1.0f;
    if (!streamer.readFloat(s)) { return false; }
    if (Krate::DSP::detail::isFinite(s)) {  // fast-math-immune, db_utils.h:118
        params.eventRateScale.store(
            static_cast<float>(
                std::clamp(static_cast<double>(s), kEventsRateScaleMin, kEventsRateScaleMax)),
            std::memory_order_relaxed);
    }
    return true;
}

// ==============================================================================
// Controller State Sync (inverts the mapping above)
// ==============================================================================

template <typename SetParamFunc>
inline void loadEventsParamsToController(Steinberg::IBStreamer& streamer,
                                         SetParamFunc setParam) {
    float s = 1.0f;
    if (!streamer.readFloat(s)) { return; }
    if (Krate::DSP::detail::isFinite(s)) {
        setParam(kEventsRateScaleId,
                 Krate::Plugins::logMapToNormalized(
                     std::clamp(static_cast<double>(s), kEventsRateScaleMin, kEventsRateScaleMax),
                     kEventsRateScaleMin, kEventsRateScaleMax));
    }
}

}  // namespace Vorago
