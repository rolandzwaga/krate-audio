#pragma once

// ==============================================================================
// Vorago Phase 12 - life parameter pack (ID 1500-1599)   T029, FR-011/012/013
// ==============================================================================
// The six-function pack contract (plan section 3.4, shape of global_params.h).
//   1500 Breathing Depth        %  [0, 1]  0.30  lin  (BreathingModulator::setDepth,
//                                                      breathing_modulator.h:177-179)
//   1501 Breathing Irregularity %  [0, 1]  0.30  lin  (setIrregularity, :183-185)
//   1502 Tidal Depth            %  [0, 1]  0.40  lin  (TidalModulator::setDepth,
//                                                      tidal_modulator.h:209-210)
//
// Stream: 3 floats in ascending ID order = 12 bytes.
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

struct LifeParams {  // PLAIN units; initializers == plan section 3.2 defaults
    std::atomic<float> breathingDepth{0.30f};
    std::atomic<float> breathingIrregularity{0.30f};
    std::atomic<float> tidalDepth{0.40f};
};

// All three fields share the plain range [0, 1] (the destination setter clamps).
inline constexpr double kLifeMin = 0.0;
inline constexpr double kLifeMax = 1.0;

// ==============================================================================
// Parameter Change Handler
// ==============================================================================

inline void handleLifeParamChange(LifeParams& params, Steinberg::Vst::ParamID id,
                                  Steinberg::Vst::ParamValue value) noexcept {
    const auto plain = static_cast<float>(linearFromNormalized(value, kLifeMin, kLifeMax));
    switch (id) {
        case kLifeBreathingDepthId:
            params.breathingDepth.store(plain, std::memory_order_relaxed);
            break;
        case kLifeBreathingIrregularityId:
            params.breathingIrregularity.store(plain, std::memory_order_relaxed);
            break;
        case kLifeTidalDepthId:
            params.tidalDepth.store(plain, std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================

inline void registerLifeParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("Breathing Depth"), STR16("%"), 0,
                            linearToNormalized(0.30, kLifeMin, kLifeMax),
                            ParameterInfo::kCanAutomate, kLifeBreathingDepthId);
    parameters.addParameter(STR16("Breathing Irregularity"), STR16("%"), 0,
                            linearToNormalized(0.30, kLifeMin, kLifeMax),
                            ParameterInfo::kCanAutomate, kLifeBreathingIrregularityId);
    parameters.addParameter(STR16("Tidal Depth"), STR16("%"), 0,
                            linearToNormalized(0.40, kLifeMin, kLifeMax),
                            ParameterInfo::kCanAutomate, kLifeTidalDepthId);
}

// ==============================================================================
// Display Formatting
// ==============================================================================

inline Steinberg::tresult formatLifeParam(Steinberg::Vst::ParamID id,
                                          Steinberg::Vst::ParamValue value,
                                          Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kLifeBreathingDepthId:
        case kLifeBreathingIrregularityId:
        case kLifeTidalDepthId: {
            const double pct = linearFromNormalized(value, kLifeMin, kLifeMax) * 100.0;
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f %%", pct);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default:
            return kResultFalse;
    }
}

// ==============================================================================
// State Persistence - 12 bytes (3 x float, ascending ID)
// ==============================================================================

inline void saveLifeParams(const LifeParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.breathingDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.breathingIrregularity.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tidalDepth.load(std::memory_order_relaxed));
}

/// EOF-safe: a failed read returns false and leaves that field and every later field
/// at its CURRENT value. Non-finite floats are skipped (field unchanged); finite
/// floats are clamped to [0, 1].
inline bool loadLifeParams(LifeParams& params, Steinberg::IBStreamer& streamer) {
    std::atomic<float>* const fields[] = {&params.breathingDepth,
                                          &params.breathingIrregularity, &params.tidalDepth};
    for (auto* f : fields) {
        float v = 0.0f;
        if (!streamer.readFloat(v)) { return false; }
        if (Krate::DSP::detail::isFinite(v)) {  // fast-math-immune, db_utils.h:118
            f->store(std::clamp(v, static_cast<float>(kLifeMin), static_cast<float>(kLifeMax)),
                     std::memory_order_relaxed);
        }
    }
    return true;
}

// ==============================================================================
// Controller State Sync (inverts the mapping above; same finite/clamp rule)
// ==============================================================================

template <typename SetParamFunc>
inline void loadLifeParamsToController(Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    constexpr Steinberg::Vst::ParamID ids[] = {kLifeBreathingDepthId,
                                               kLifeBreathingIrregularityId, kLifeTidalDepthId};
    for (const auto id : ids) {
        float v = 0.0f;
        if (!streamer.readFloat(v)) { return; }
        if (Krate::DSP::detail::isFinite(v)) {
            setParam(id, linearToNormalized(static_cast<double>(v), kLifeMin, kLifeMax));
        }
    }
}

}  // namespace Vorago
