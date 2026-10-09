#pragma once

// ==============================================================================
// Vorago Phase 12 - Cloud Parameters (ID 200-299)   FR-011, FR-012, FR-013
// ==============================================================================
// The six-function pack contract (plan section 3.4) for the HarmonicCloud surface.
// Seven continuous, linear-taper parameters held in PLAIN units; ranges are the
// destination setter clamps (harmonic_cloud.h, plan section 3.2 Cloud rows).
//
// Stream: seven floats in ascending ID order = 28 bytes (plan section 3.4).
//
// ODR note: Vorago::CloudParams is a near-name of Seraphis::CloudParams
// (plugins/seraphis/src/parameters/cloud_params.h:82). Different namespaces, so no
// ODR violation - but no TU may `using namespace` both.
// ==============================================================================

#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdio>

namespace Vorago {

/// PLAIN units. Explicit initializers == the C-6 defaults (voice prepare step 5).
struct CloudParams {
    std::atomic<float> richness{0.70f};         ///< 200, [0, 1]
    std::atomic<float> tiltDb{-4.0f};           ///< 201, dB/oct [-12, 12]
    std::atomic<float> mutation{0.15f};         ///< 202, [0, 1]
    std::atomic<float> inharmonicity{0.015f};   ///< 203, [0, 0.1]
    std::atomic<float> driftCents{8.0f};        ///< 204, cents [0, 50]
    std::atomic<float> stereoSpread{0.45f};     ///< 205, [0, 1]
    std::atomic<float> spectralGravity{0.10f};  ///< 206, [-1, 1]
};

/// One row per Cloud parameter, ascending ID == stream order.
struct CloudParamSpec {
    Steinberg::Vst::ParamID id;
    double minPlain;
    double maxPlain;
    double defaultPlain;
};

inline constexpr std::array<CloudParamSpec, 7> kCloudParamSpecs = {{
    {kCloudRichnessId, 0.0, 1.0, 0.70},
    {kCloudTiltId, -12.0, 12.0, -4.0},
    {kCloudMutationId, 0.0, 1.0, 0.15},
    {kCloudInharmonicityId, 0.0, 0.1, 0.015},
    {kCloudDriftDepthId, 0.0, 50.0, 8.0},
    {kCloudStereoSpreadId, 0.0, 1.0, 0.45},
    {kCloudSpectralGravityId, -1.0, 1.0, 0.10},
}};

inline constexpr int kNumCloudParams = static_cast<int>(kCloudParamSpecs.size());

// ==============================================================================
// Field access by spec index (0 == Richness ... 6 == Spectral Gravity)
// ==============================================================================
// Only ever called with index in [0, kNumCloudParams); the default arm asserts.

[[nodiscard]] inline std::atomic<float>& cloudField(CloudParams& p, int index) noexcept {
    switch (index) {
        case 0: return p.richness;
        case 1: return p.tiltDb;
        case 2: return p.mutation;
        case 3: return p.inharmonicity;
        case 4: return p.driftCents;
        case 5: return p.stereoSpread;
        case 6: return p.spectralGravity;
        default:
            assert(false && "cloudField index out of range");
            return p.spectralGravity;
    }
}

[[nodiscard]] inline const std::atomic<float>& cloudField(const CloudParams& p,
                                                          int index) noexcept {
    switch (index) {
        case 0: return p.richness;
        case 1: return p.tiltDb;
        case 2: return p.mutation;
        case 3: return p.inharmonicity;
        case 4: return p.driftCents;
        case 5: return p.stereoSpread;
        case 6: return p.spectralGravity;
        default:
            assert(false && "cloudField index out of range");
            return p.spectralGravity;
    }
}

// ==============================================================================
// Parameter Change Handler
// ==============================================================================
// The caller has already rejected non-finite values and clamped to [0, 1] (plan 4.2).
// Unregistered in-band IDs fall through `default: break` and change nothing.

inline void handleCloudParamChange(CloudParams& params, Steinberg::Vst::ParamID id,
                                   Steinberg::Vst::ParamValue value) noexcept {
    const auto store = [&params, value](int index) noexcept {
        const auto& s = kCloudParamSpecs[static_cast<std::size_t>(index)];
        cloudField(params, index)
            .store(static_cast<float>(linearFromNormalized(value, s.minPlain, s.maxPlain)),
                   std::memory_order_relaxed);
    };
    switch (id) {
        case kCloudRichnessId: store(0); break;
        case kCloudTiltId: store(1); break;
        case kCloudMutationId: store(2); break;
        case kCloudInharmonicityId: store(3); break;
        case kCloudDriftDepthId: store(4); break;
        case kCloudStereoSpreadId: store(5); break;
        case kCloudSpectralGravityId: store(6); break;
        default: break;
    }
}

// ==============================================================================
// Parameter Registration (defaults from the inverse map)
// ==============================================================================

inline void registerCloudParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    struct CloudDef {
        const TChar* title;
        const TChar* units;
    };
    const std::array<CloudDef, static_cast<std::size_t>(kNumCloudParams)> defs{{
        {STR16("Cloud Richness"), STR16("%")},
        {STR16("Cloud Tilt"), STR16("dB/oct")},
        {STR16("Cloud Mutation"), STR16("%")},
        {STR16("Cloud Inharmonicity"), STR16("")},
        {STR16("Cloud Drift Depth"), STR16("ct")},
        {STR16("Cloud Stereo Spread"), STR16("%")},
        {STR16("Cloud Spectral Gravity"), STR16("")},
    }};

    for (int i = 0; i < kNumCloudParams; ++i) {
        const auto& d = defs[static_cast<std::size_t>(i)];
        const auto& s = kCloudParamSpecs[static_cast<std::size_t>(i)];
        parameters.addParameter(d.title, d.units, 0,
                                linearToNormalized(s.defaultPlain, s.minPlain, s.maxPlain),
                                ParameterInfo::kCanAutomate, static_cast<Steinberg::int32>(s.id));
    }
}

// ==============================================================================
// Display Formatting (continuous only; kResultFalse for unknown IDs)
// ==============================================================================

inline Steinberg::tresult formatCloudParam(Steinberg::Vst::ParamID id,
                                           Steinberg::Vst::ParamValue value,
                                           Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    for (int i = 0; i < kNumCloudParams; ++i) {
        const auto& s = kCloudParamSpecs[static_cast<std::size_t>(i)];
        if (s.id != id) { continue; }
        const double plain = linearFromNormalized(value, s.minPlain, s.maxPlain);
        char8 text[32];
        switch (id) {
            case kCloudRichnessId:
            case kCloudMutationId:
            case kCloudStereoSpreadId:
                snprintf(text, sizeof(text), "%.0f%%", plain * 100.0);
                break;
            case kCloudTiltId:
                snprintf(text, sizeof(text), "%.1f dB/oct", plain);
                break;
            case kCloudInharmonicityId:
                snprintf(text, sizeof(text), "%.3f", plain);
                break;
            case kCloudDriftDepthId:
                snprintf(text, sizeof(text), "%.1f ct", plain);
                break;
            default:  // kCloudSpectralGravityId
                snprintf(text, sizeof(text), "%+.2f", plain);
                break;
        }
        UString(string, 128).fromAscii(text);
        return kResultOk;
    }
    return kResultFalse;
}

// ==============================================================================
// State Persistence - 28 bytes (seven floats, ascending ID)
// ==============================================================================

inline void saveCloudParams(const CloudParams& params, Steinberg::IBStreamer& streamer) {
    for (int i = 0; i < kNumCloudParams; ++i) {
        streamer.writeFloat(cloudField(params, i).load(std::memory_order_relaxed));
    }
}

/// EOF-safe: stops at the first failed read and returns false, leaving that field
/// and every later one at its CURRENT value. A non-finite float is skipped (field
/// unchanged); finite values are clamped to the plain range.
inline bool loadCloudParams(CloudParams& params, Steinberg::IBStreamer& streamer) {
    for (int i = 0; i < kNumCloudParams; ++i) {
        const auto& s = kCloudParamSpecs[static_cast<std::size_t>(i)];
        float fv = 0.0f;
        if (!streamer.readFloat(fv)) { return false; }
        if (Krate::DSP::detail::isFinite(fv)) {  // fast-math-immune, db_utils.h:118
            cloudField(params, i)
                .store(std::clamp(fv, static_cast<float>(s.minPlain),
                                  static_cast<float>(s.maxPlain)),
                       std::memory_order_relaxed);
        }
    }
    return true;
}

// ==============================================================================
// Controller State Sync (inverse map of the handler)
// ==============================================================================

template <typename SetParamFunc>
inline void loadCloudParamsToController(Steinberg::IBStreamer& streamer,
                                        SetParamFunc setParam) {
    for (int i = 0; i < kNumCloudParams; ++i) {
        const auto& s = kCloudParamSpecs[static_cast<std::size_t>(i)];
        float fv = 0.0f;
        if (!streamer.readFloat(fv)) { return; }
        if (Krate::DSP::detail::isFinite(fv)) {
            setParam(s.id, linearToNormalized(static_cast<double>(fv), s.minPlain, s.maxPlain));
        }
    }
}

}  // namespace Vorago
