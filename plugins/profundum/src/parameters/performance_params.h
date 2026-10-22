#pragma once

// ==============================================================================
// Profundum - Performance parameters (IDs 4-7)   Phase 11 T012
// FR-004, FR-046, FR-050 - FR-053, FR-055; plan 4.2
// ==============================================================================
//   4 Pitch Bend        [0, 1] norm, 0.5 = centre; the engine gets 2v - 1 (FR-051)   kCanAutomate | kIsHidden
//   5 Sustain Pedal     [0, 1], >= 0.5 = down; display mirror (pedal points drive the latch, FR-052)
//                                                                                      kCanAutomate | kIsHidden
//   6 Mod Wheel         [0, 1] linear, default 0; Mass offset at assembly (FR-055)    kIsHidden only
//   7 Channel Pressure  [0, 1] linear, default 0; Mass offset at assembly (FR-055)    kIsHidden only
//
// NEVER persisted (FR-046, FR-056): no save / load here. setState resets them to
// centre / up / 0 / 0 (resetPerformanceParams). Vorago precedent:
// plugins/vorago/src/parameters/global_params.h:131-138, :160-163.
// ==============================================================================

#include "plugin_ids.h"

#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Profundum {

inline constexpr float kDefaultPitchBendNorm = 0.5f;  // centre
inline constexpr float kDefaultSustainNorm = 0.0f;    // up
inline constexpr float kDefaultModWheel = 0.0f;
inline constexpr float kDefaultChannelPressure = 0.0f;

struct PerformanceParams {
    std::atomic<float> pitchBendNorm{kDefaultPitchBendNorm};      // centre; the engine gets 2v - 1 (FR-051)
    std::atomic<float> sustainNorm{kDefaultSustainNorm};          // display mirror only; pedal points drive the latch
    std::atomic<float> modWheel{kDefaultModWheel};                // [0, 1]
    std::atomic<float> channelPressure{kDefaultChannelPressure};  // [0, 1]
};

/// value arrives already finite and clamped to [0, 1] (processParameterChanges); clamped again here. Relaxed stores.
/// Returns true if the ID is one of 4-7.
inline bool handlePerformanceParamChange(PerformanceParams& params, Steinberg::Vst::ParamID id,
                                         Steinberg::Vst::ParamValue value) noexcept {
    const float v = std::clamp(static_cast<float>(value), 0.0f, 1.0f);
    switch (id) {
        case kPitchBendId:
            params.pitchBendNorm.store(v, std::memory_order_relaxed);
            return true;
        case kSustainPedalId:
            params.sustainNorm.store(v, std::memory_order_relaxed);
            return true;
        case kModWheelId:
            params.modWheel.store(v, std::memory_order_relaxed);
            return true;
        case kChannelPressureId:
            params.channelPressure.store(v, std::memory_order_relaxed);
            return true;
        default:
            return false;
    }
}

/// FR-004: 4 and 5 kCanAutomate | kIsHidden; 6 and 7 kIsHidden only (not automatable). Defaults 0.5 / 0 / 0 / 0.
inline void registerPerformanceParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Pitch Bend"), STR16(""), 0, static_cast<ParamValue>(kDefaultPitchBendNorm),
                            ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden,
                            static_cast<Steinberg::int32>(kPitchBendId));
    parameters.addParameter(STR16("Sustain Pedal"), STR16(""), 0, static_cast<ParamValue>(kDefaultSustainNorm),
                            ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden,
                            static_cast<Steinberg::int32>(kSustainPedalId));
    parameters.addParameter(STR16("Mod Wheel"), STR16("%"), 0, static_cast<ParamValue>(kDefaultModWheel),
                            ParameterInfo::kIsHidden, static_cast<Steinberg::int32>(kModWheelId));
    parameters.addParameter(STR16("Channel Pressure"), STR16("%"), 0,
                            static_cast<ParamValue>(kDefaultChannelPressure), ParameterInfo::kIsHidden,
                            static_cast<Steinberg::int32>(kChannelPressureId));
}

/// ASCII. Pitch Bend "%+.0f%%" of 100 (2v - 1) (centre "+0%"); Sustain "Down" / "Up"; Mod Wheel and Channel
/// Pressure "%.0f%%".
inline Steinberg::tresult formatPerformanceParam(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
                                                 Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    const double v = std::clamp(static_cast<double>(value), 0.0, 1.0);
    char8 text[64];
    switch (id) {
        case kPitchBendId:
            std::snprintf(text, sizeof(text), "%+.0f%%", 100.0 * (2.0 * v - 1.0) + 0.0);
            break;
        case kSustainPedalId:
            UString(string, 128).fromAscii(v >= 0.5 ? "Down" : "Up");
            return kResultOk;
        case kModWheelId:
        case kChannelPressureId:
            std::snprintf(text, sizeof(text), "%.0f%%", 100.0 * v);
            break;
        default:
            return kResultFalse;
    }
    UString(string, 128).fromAscii(text);
    return kResultOk;
}

/// FR-046: setState resets the hidden performance parameters to centre / up / 0 / 0.
inline void resetPerformanceParams(PerformanceParams& params) noexcept {
    params.pitchBendNorm.store(kDefaultPitchBendNorm, std::memory_order_relaxed);
    params.sustainNorm.store(kDefaultSustainNorm, std::memory_order_relaxed);
    params.modWheel.store(kDefaultModWheel, std::memory_order_relaxed);
    params.channelPressure.store(kDefaultChannelPressure, std::memory_order_relaxed);
}

}  // namespace Profundum
