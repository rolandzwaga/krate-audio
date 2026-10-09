#pragma once

// ==============================================================================
// Vorago - Ecosystem Parameters (ID 900-999)   T023, FR-011/FR-012/FR-013;
//                                              Phase 14 T016, FR-072/FR-074
// ==============================================================================
// Pack contract of plan section 3.4 (shape of global_params.h):
//   900 Ecosystem Depth, %, [0, 1], default 0.85, n0 0.85, linear.
// Route MB EcosystemDepth; plain-range clamp == VoragoVoice::setEcosystemDepth
// (vorago_voice.h:1407, std::clamp(d, 0, 1)).
// Phase 14 (plan 5.2 "Pack"), route VP:
//   901 Ecosystem Sync, "", [0, 0.5], default 0.0, n0 0.0, linear
//       (== EcosystemEngine::setSyncRate clamp, ecosystem_engine.h:599).
//   902 Ecosystem Self Affinity, "", [-2, 2], default -1.0, n0 0.25, linear
//       (== EcosystemEngine::kMinAffinity/kMaxAffinity, ecosystem_engine.h:237-238).
//
// Stream: v2 pack = float depth = 4 bytes (mid-stream, unchanged).
//         v3 extension = float syncRate + float selfAffinity = 8 bytes,
//         written after the life pack (saveEcosystemParamsV3Ext).
// ==============================================================================

#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/ecosystem_engine.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Vorago {

inline constexpr double kEcosystemDepthMin = 0.0;
inline constexpr double kEcosystemDepthMax = 1.0;

inline constexpr double kEcosystemSyncRateMin = 0.0;           // == setSyncRate clamp
inline constexpr double kEcosystemSyncRateMax = 0.5;
inline constexpr double kEcosystemSyncRateDefault = 0.0;       // n0 = 0
inline constexpr double kEcosystemSelfAffinityMin = -2.0;      // == kMinAffinity
inline constexpr double kEcosystemSelfAffinityMax = 2.0;       // == kMaxAffinity
inline constexpr double kEcosystemSelfAffinityDefault = -1.0;  // n0 = 0.25
static_assert(kEcosystemSelfAffinityMin ==
                      static_cast<double>(Krate::DSP::EcosystemEngine::kMinAffinity) &&
                  kEcosystemSelfAffinityMax ==
                      static_cast<double>(Krate::DSP::EcosystemEngine::kMaxAffinity),
              "902's registered range must equal the engine's affinity clamp");

struct EcosystemParams {
    std::atomic<float> depth{0.85f};         ///< [0, 1]; plain == normalized (linear)
    std::atomic<float> syncRate{0.0f};       ///< 901, [0, 0.5], linear
    std::atomic<float> selfAffinity{-1.0f};  ///< 902, [-2, 2], linear
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
        case kEcosystemSyncRateId:
            params.syncRate.store(static_cast<float>(linearFromNormalized(
                                      value, kEcosystemSyncRateMin, kEcosystemSyncRateMax)),
                                  std::memory_order_relaxed);
            break;
        case kEcosystemSelfAffinityId:
            params.selfAffinity.store(
                static_cast<float>(linearFromNormalized(value, kEcosystemSelfAffinityMin,
                                                        kEcosystemSelfAffinityMax)),
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
    parameters.addParameter(STR16("Ecosystem Sync"), STR16(""), 0,
                            linearToNormalized(kEcosystemSyncRateDefault, kEcosystemSyncRateMin,
                                               kEcosystemSyncRateMax),
                            ParameterInfo::kCanAutomate, kEcosystemSyncRateId);
    parameters.addParameter(STR16("Ecosystem Self Affinity"), STR16(""), 0,
                            linearToNormalized(kEcosystemSelfAffinityDefault,
                                               kEcosystemSelfAffinityMin,
                                               kEcosystemSelfAffinityMax),
                            ParameterInfo::kCanAutomate, kEcosystemSelfAffinityId);
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
    if (id == kEcosystemSyncRateId) {
        const double plain =
            linearFromNormalized(value, kEcosystemSyncRateMin, kEcosystemSyncRateMax);
        char8 text[32];
        snprintf(text, sizeof(text), "%.2f", plain);
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    if (id == kEcosystemSelfAffinityId) {
        const double plain =
            linearFromNormalized(value, kEcosystemSelfAffinityMin, kEcosystemSelfAffinityMax);
        char8 text[32];
        snprintf(text, sizeof(text), "%+.2f", plain);
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

// ==============================================================================
// v3 Extension (FR-072) - 8 bytes (float syncRate, float selfAffinity),
// written after the life pack. The v2 functions above are unchanged.
// ==============================================================================

inline void saveEcosystemParamsV3Ext(const EcosystemParams& params,
                                     Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.syncRate.load(std::memory_order_relaxed));
    streamer.writeFloat(params.selfAffinity.load(std::memory_order_relaxed));
}

/// EOF-safe like loadEcosystemParams: false at the first failed read, that and
/// later fields unchanged; non-finite rejected (field unchanged); finite clamped.
inline bool loadEcosystemParamsV3Ext(EcosystemParams& params, Steinberg::IBStreamer& streamer) {
    float sync = 0.0f;
    if (!streamer.readFloat(sync)) { return false; }
    if (Krate::DSP::detail::isFinite(sync)) {
        params.syncRate.store(std::clamp(sync, static_cast<float>(kEcosystemSyncRateMin),
                                         static_cast<float>(kEcosystemSyncRateMax)),
                              std::memory_order_relaxed);
    }
    float affinity = 0.0f;
    if (!streamer.readFloat(affinity)) { return false; }
    if (Krate::DSP::detail::isFinite(affinity)) {
        params.selfAffinity.store(
            std::clamp(affinity, static_cast<float>(kEcosystemSelfAffinityMin),
                       static_cast<float>(kEcosystemSelfAffinityMax)),
            std::memory_order_relaxed);
    }
    return true;
}

template <typename SetParamFunc>
inline void loadEcosystemParamsV3ExtToController(Steinberg::IBStreamer& streamer,
                                                 SetParamFunc setParam) {
    float sync = 0.0f;
    if (!streamer.readFloat(sync)) { return; }
    if (Krate::DSP::detail::isFinite(sync)) {
        setParam(kEcosystemSyncRateId,
                 linearToNormalized(static_cast<double>(sync), kEcosystemSyncRateMin,
                                    kEcosystemSyncRateMax));
    }
    float affinity = 0.0f;
    if (!streamer.readFloat(affinity)) { return; }
    if (Krate::DSP::detail::isFinite(affinity)) {
        setParam(kEcosystemSelfAffinityId,
                 linearToNormalized(static_cast<double>(affinity), kEcosystemSelfAffinityMin,
                                    kEcosystemSelfAffinityMax));
    }
}

}  // namespace Vorago
