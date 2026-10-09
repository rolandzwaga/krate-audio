#pragma once

// ==============================================================================
// Vorago Phase 12 - Resonance Parameters (ID 400-499)   FR-011, FR-012, FR-013
// ==============================================================================
// Six-function pack contract (plan section 3.4; shape of global_params.h).
//
//   400 Resonance Gravity      [-1, 1]        default 0.0   lin   (MB ResonanceGravity)
//   401 Resonance Mix          [0, 1] %       default 0.45  lin   (MB ResonanceMix)
//   402 Resonance Wander Rate  [0.002, 1] Hz  default 0.03  log   (MB ResonanceWanderRate)
//   403 Resonance Anchor       L(3) Free, Keyed, Hybrid; default Hybrid (2)   (VP)
//
// Plain-range clamps are the destination setter clamps (plan section 3.2):
// ResonanceDriftNetwork setGravity [-1, 1], setMix [0, 1], setWanderRate
// [kMinWanderRateHz, kMaxWanderRateHz] (resonance_drift_network.h:148-149).
// List index == ResonanceDriftNetwork::AnchorMode value (resonance_drift_network.h:293).
//
// Stream: float gravity, float mix, float wanderRateHz, int32 anchorMode = 16 bytes.
// ==============================================================================

#include "parameters/param_mapping.h"
#include "plugin_ids.h"

#include "ui/parameter_helpers.h"  // plugins/shared/src/ui/parameter_helpers.h

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/resonance_drift_network.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Vorago {

// ------------------------------------------------------------------------------
// Ranges and defaults (plain units)
// ------------------------------------------------------------------------------
inline constexpr double kResonanceGravityMin = -1.0;
inline constexpr double kResonanceGravityMax = 1.0;
inline constexpr double kResonanceMixMin = 0.0;
inline constexpr double kResonanceMixMax = 1.0;
// Double literals (not the widened float constants: 0.002f != 0.002 in double, which
// would move n0 by ~4e-9); pinned to the setter clamps by the static_asserts below.
inline constexpr double kResonanceWanderRateMinHz = 0.002;
inline constexpr double kResonanceWanderRateMaxHz = 1.0;
inline constexpr int kNumResonanceAnchorModes = 3;

// Defaults are double so the registered n0 (inverse map in double) is exact to 1e-9;
// the atomics store them cast to float once.
inline constexpr double kResonanceGravityDefault = 0.0;
inline constexpr double kResonanceMixDefault = 0.45;
inline constexpr double kResonanceWanderRateDefaultHz = 0.03;  // == kDefaultWanderRateHz
inline constexpr int kResonanceAnchorModeDefault =
    static_cast<int>(Krate::DSP::ResonanceDriftNetwork::AnchorMode::Hybrid);

static_assert(static_cast<int>(Krate::DSP::ResonanceDriftNetwork::AnchorMode::Free) == 0);
static_assert(static_cast<int>(Krate::DSP::ResonanceDriftNetwork::AnchorMode::Keyed) == 1);
static_assert(static_cast<int>(Krate::DSP::ResonanceDriftNetwork::AnchorMode::Hybrid) == 2);
static_assert(Krate::DSP::ResonanceDriftNetwork::kMinWanderRateHz == 0.002f);
static_assert(Krate::DSP::ResonanceDriftNetwork::kMaxWanderRateHz == 1.0f);
static_assert(Krate::DSP::ResonanceDriftNetwork::kDefaultWanderRateHz == 0.03f);

struct ResonanceParams {
    std::atomic<float> gravity{static_cast<float>(kResonanceGravityDefault)};  ///< [-1, 1]
    std::atomic<float> mix{static_cast<float>(kResonanceMixDefault)};          ///< [0, 1]
    std::atomic<float> wanderRateHz{
        static_cast<float>(kResonanceWanderRateDefaultHz)};  ///< [0.002, 1] Hz
    std::atomic<int> anchorMode{kResonanceAnchorModeDefault};       ///< AnchorMode index [0, 2]
};

// ------------------------------------------------------------------------------
// Mappings (one place, used by handler, registration, format and controller sync)
// ------------------------------------------------------------------------------
[[nodiscard]] inline double resonanceGravityFromNormalized(double n) noexcept {
    return linearFromNormalized(n, kResonanceGravityMin, kResonanceGravityMax);
}
[[nodiscard]] inline double resonanceGravityToNormalized(double g) noexcept {
    return linearToNormalized(g, kResonanceGravityMin, kResonanceGravityMax);
}
[[nodiscard]] inline double resonanceMixFromNormalized(double n) noexcept {
    return linearFromNormalized(n, kResonanceMixMin, kResonanceMixMax);
}
[[nodiscard]] inline double resonanceMixToNormalized(double m) noexcept {
    return linearToNormalized(m, kResonanceMixMin, kResonanceMixMax);
}
[[nodiscard]] inline double resonanceWanderRateFromNormalized(double n) noexcept {
    return Krate::Plugins::logMapFromNormalized(n, kResonanceWanderRateMinHz,
                                                kResonanceWanderRateMaxHz);
}
[[nodiscard]] inline double resonanceWanderRateToNormalized(double hz) noexcept {
    return Krate::Plugins::logMapToNormalized(hz, kResonanceWanderRateMinHz,
                                              kResonanceWanderRateMaxHz);
}

// ==============================================================================
// Parameter Change Handler (caller has rejected non-finite and clamped to [0, 1])
// ==============================================================================

inline void handleResonanceParamChange(ResonanceParams& params, Steinberg::Vst::ParamID id,
                                       Steinberg::Vst::ParamValue value) noexcept {
    switch (id) {
        case kResonanceGravityId:
            params.gravity.store(static_cast<float>(resonanceGravityFromNormalized(value)),
                                 std::memory_order_relaxed);
            break;
        case kResonanceMixId:
            params.mix.store(static_cast<float>(resonanceMixFromNormalized(value)),
                             std::memory_order_relaxed);
            break;
        case kResonanceWanderRateId:
            params.wanderRateHz.store(static_cast<float>(resonanceWanderRateFromNormalized(value)),
                                      std::memory_order_relaxed);
            break;
        case kResonanceAnchorModeId:
            params.anchorMode.store(indexFromNormalized(value, kNumResonanceAnchorModes),
                                    std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================

inline void registerResonanceParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(
        STR16("Resonance Gravity"), STR16(""), 0,
        resonanceGravityToNormalized(kResonanceGravityDefault),
        ParameterInfo::kCanAutomate, kResonanceGravityId);
    parameters.addParameter(STR16("Resonance Mix"), STR16("%"), 0,
                            resonanceMixToNormalized(kResonanceMixDefault),
                            ParameterInfo::kCanAutomate, kResonanceMixId);
    parameters.addParameter(
        STR16("Resonance Wander Rate"), STR16("Hz"), 0,
        resonanceWanderRateToNormalized(kResonanceWanderRateDefaultHz),
        ParameterInfo::kCanAutomate, kResonanceWanderRateId);

    auto* anchor = Krate::Plugins::createDropdownParameterWithDefault(
        STR16("Resonance Anchor"), kResonanceAnchorModeId, kResonanceAnchorModeDefault,
        {STR16("Free"), STR16("Keyed"), STR16("Hybrid")});
    // P-1 (global_params.h:82-84): the helper sets only the CURRENT value; the
    // REGISTERED default must be pinned here.
    anchor->getInfo().defaultNormalizedValue =
        indexToNormalized(kResonanceAnchorModeDefault, kNumResonanceAnchorModes);
    parameters.addParameter(anchor);
}

// ==============================================================================
// Display Formatting (continuous IDs only; the anchor list formats itself)
// ==============================================================================

inline Steinberg::tresult formatResonanceParam(Steinberg::Vst::ParamID id,
                                               Steinberg::Vst::ParamValue value,
                                               Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    char8 text[32];
    switch (id) {
        case kResonanceGravityId:
            snprintf(text, sizeof(text), "%+.2f", resonanceGravityFromNormalized(value));
            break;
        case kResonanceMixId:
            snprintf(text, sizeof(text), "%.0f%%", resonanceMixFromNormalized(value) * 100.0);
            break;
        case kResonanceWanderRateId:
            snprintf(text, sizeof(text), "%.3f Hz", resonanceWanderRateFromNormalized(value));
            break;
        default:
            return kResultFalse;
    }
    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// ==============================================================================
// State Persistence - 16 bytes (3 x float + int32), ascending ID order
// ==============================================================================

inline void saveResonanceParams(const ResonanceParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.gravity.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.wanderRateHz.load(std::memory_order_relaxed));
    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.anchorMode.load(std::memory_order_relaxed)));
}

[[nodiscard]] inline float clampResonanceGravity(float g) noexcept {
    return std::clamp(g, static_cast<float>(kResonanceGravityMin),
                      static_cast<float>(kResonanceGravityMax));
}
[[nodiscard]] inline float clampResonanceMix(float m) noexcept {
    return std::clamp(m, static_cast<float>(kResonanceMixMin),
                      static_cast<float>(kResonanceMixMax));
}
[[nodiscard]] inline float clampResonanceWanderRate(float hz) noexcept {
    return std::clamp(hz, Krate::DSP::ResonanceDriftNetwork::kMinWanderRateHz,
                      Krate::DSP::ResonanceDriftNetwork::kMaxWanderRateHz);
}
[[nodiscard]] inline int clampResonanceAnchorMode(Steinberg::int32 i) noexcept {
    return static_cast<int>(std::clamp(i, Steinberg::int32{0},
                                       Steinberg::int32{kNumResonanceAnchorModes - 1}));
}

/// EOF-safe: a failed read returns false and leaves the failed field and every later
/// field at its CURRENT value. Non-finite floats are skipped (field unchanged);
/// finite floats clamp to the plain range; the index clamps to [0, 2].
inline bool loadResonanceParams(ResonanceParams& params, Steinberg::IBStreamer& streamer) {
    float f = 0.0f;
    Steinberg::int32 i = 0;

    if (!streamer.readFloat(f)) { return false; }
    if (Krate::DSP::detail::isFinite(f)) {  // fast-math-immune, db_utils.h:118
        params.gravity.store(clampResonanceGravity(f), std::memory_order_relaxed);
    }

    if (!streamer.readFloat(f)) { return false; }
    if (Krate::DSP::detail::isFinite(f)) {
        params.mix.store(clampResonanceMix(f), std::memory_order_relaxed);
    }

    if (!streamer.readFloat(f)) { return false; }
    if (Krate::DSP::detail::isFinite(f)) {
        params.wanderRateHz.store(clampResonanceWanderRate(f), std::memory_order_relaxed);
    }

    if (!streamer.readInt32(i)) { return false; }
    params.anchorMode.store(clampResonanceAnchorMode(i), std::memory_order_relaxed);

    return true;
}

// ==============================================================================
// Controller State Sync (inverts every mapping above; same finite/clamp rules)
// ==============================================================================

template <typename SetParamFunc>
inline void loadResonanceParamsToController(Steinberg::IBStreamer& streamer,
                                            SetParamFunc setParam) {
    float f = 0.0f;
    Steinberg::int32 i = 0;

    if (!streamer.readFloat(f)) { return; }
    if (Krate::DSP::detail::isFinite(f)) {
        setParam(kResonanceGravityId,
                 resonanceGravityToNormalized(static_cast<double>(clampResonanceGravity(f))));
    }

    if (!streamer.readFloat(f)) { return; }
    if (Krate::DSP::detail::isFinite(f)) {
        setParam(kResonanceMixId,
                 resonanceMixToNormalized(static_cast<double>(clampResonanceMix(f))));
    }

    if (!streamer.readFloat(f)) { return; }
    if (Krate::DSP::detail::isFinite(f)) {
        setParam(kResonanceWanderRateId, resonanceWanderRateToNormalized(
                                             static_cast<double>(clampResonanceWanderRate(f))));
    }

    if (!streamer.readInt32(i)) { return; }
    setParam(kResonanceAnchorModeId,
             indexToNormalized(clampResonanceAnchorMode(i), kNumResonanceAnchorModes));
}

}  // namespace Vorago
