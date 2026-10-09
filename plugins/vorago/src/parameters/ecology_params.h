#pragma once

// ==============================================================================
// Vorago Phase 12 - Ecology Parameters (ID 500-599)   FR-011 / FR-012 / FR-013
// ==============================================================================
// Pack contract of global_params.h (plan section 3.4). T019.
//
//   500      Ecology Mix             %  [0, 1]    default 0.15  n0 0.15  lin
//   501      Ecology Loop Gain       %  [0, 0.9]  default 0.72  n0 0.8   lin
//   510-515  Ecology Loop 1..6 Filter   L(3) Lowpass, Bandpass, Highpass
//                                       (index == FeedbackEcology::FilterMode,
//                                       feedback_ecology.h:597), default 0
//
// Plain-range clamps are the destination setter clamps: FeedbackEcology::setMix
// [0, 1] (feedback_ecology.h:1314-1318), setLoopGain [kMinLoopGain, kMaxLoopGain]
// = [0, 0.90] (:264-266, :1111-1115).
//
// Stream (ascending ID): float mix, float loopGain, 6 x int32 filter = 32 bytes.
// ==============================================================================

#include "plugin_ids.h"

#include "parameters/param_mapping.h"
#include "ui/parameter_helpers.h"  // plugins/shared/src/ui/parameter_helpers.h

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/systems/feedback_ecology.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>

namespace Vorago {

inline constexpr std::size_t kEcologyNumLoops = 6;
inline constexpr int kEcologyNumFilterModes = 3;
inline constexpr double kEcologyLoopGainMaxPlain = 0.9;

static_assert(kEcologyNumLoops == Krate::DSP::FeedbackEcology::kMaxLoops,
              "one filter parameter per ecology loop");
// Float literal vs float constant: MSVC's constant evaluator does not round
// static_cast<float>(0.9) to float before comparing.
static_assert(Krate::DSP::FeedbackEcology::kMaxLoopGain == 0.9f,
              "loop gain range must equal the setter clamp");
static_assert(Krate::DSP::FeedbackEcology::kMinLoopGain == 0.0f);
static_assert(static_cast<int>(Krate::DSP::FeedbackEcology::FilterMode::Lowpass) == 0 &&
                  static_cast<int>(Krate::DSP::FeedbackEcology::FilterMode::Bandpass) == 1 &&
                  static_cast<int>(Krate::DSP::FeedbackEcology::FilterMode::Highpass) == 2,
              "list index == FilterMode value");
static_assert(kEcologyLoop5FilterModeId - kEcologyLoop0FilterModeId == kEcologyNumLoops - 1);

struct EcologyParams {
    std::atomic<float> mix{0.15f};       ///< [0, 1] == FeedbackEcology::kDefaultMix
    std::atomic<float> loopGain{0.72f};  ///< [0, 0.9] == FeedbackEcology::kDefaultLoopGain
    /// FilterMode index per loop, [0, 2]; default Lowpass (0).
    std::array<std::atomic<int>, kEcologyNumLoops> loopFilterMode{{0, 0, 0, 0, 0, 0}};
};

// ==============================================================================
// Parameter Change Handler (caller has rejected non-finite and clamped [0, 1])
// ==============================================================================

inline void handleEcologyParamChange(EcologyParams& params, Steinberg::Vst::ParamID id,
                                     Steinberg::Vst::ParamValue value) noexcept {
    switch (id) {
        case kEcologyMixId:
            params.mix.store(static_cast<float>(linearFromNormalized(value, 0.0, 1.0)),
                             std::memory_order_relaxed);
            break;
        case kEcologyLoopGainId:
            params.loopGain.store(
                static_cast<float>(linearFromNormalized(value, 0.0, kEcologyLoopGainMaxPlain)),
                std::memory_order_relaxed);
            break;
        case kEcologyLoop0FilterModeId:
        case kEcologyLoop1FilterModeId:
        case kEcologyLoop2FilterModeId:
        case kEcologyLoop3FilterModeId:
        case kEcologyLoop4FilterModeId:
        case kEcologyLoop5FilterModeId:
            params.loopFilterMode[static_cast<std::size_t>(id - kEcologyLoop0FilterModeId)].store(
                indexFromNormalized(value, kEcologyNumFilterModes), std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================

inline void registerEcologyParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Ecology Mix"), STR16("%"), 0, 0.15,
                            ParameterInfo::kCanAutomate, kEcologyMixId);
    parameters.addParameter(STR16("Ecology Loop Gain"), STR16("%"), 0, 0.72 / 0.9,
                            ParameterInfo::kCanAutomate, kEcologyLoopGainId);

    static constexpr std::array<const TChar*, kEcologyNumLoops> kTitles = {
        STR16("Ecology Loop 1 Filter"), STR16("Ecology Loop 2 Filter"),
        STR16("Ecology Loop 3 Filter"), STR16("Ecology Loop 4 Filter"),
        STR16("Ecology Loop 5 Filter"), STR16("Ecology Loop 6 Filter")};
    for (std::size_t l = 0; l < kEcologyNumLoops; ++l) {
        auto* p = Krate::Plugins::createDropdownParameterWithDefault(
            kTitles[l], static_cast<ParamID>(kEcologyLoop0FilterModeId + l),
            /*defaultIndex=*/0, {STR16("Lowpass"), STR16("Bandpass"), STR16("Highpass")});
        // P-1 (global_params.h:82-84): pin the REGISTERED default too.
        p->getInfo().defaultNormalizedValue = 0.0;
        parameters.addParameter(p);
    }
}

// ==============================================================================
// Display Formatting (continuous only; list parameters format themselves)
// ==============================================================================

inline Steinberg::tresult formatEcologyParam(Steinberg::Vst::ParamID id,
                                             Steinberg::Vst::ParamValue value,
                                             Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    double plain = 0.0;
    switch (id) {
        case kEcologyMixId:
            plain = linearFromNormalized(value, 0.0, 1.0);
            break;
        case kEcologyLoopGainId:
            plain = linearFromNormalized(value, 0.0, kEcologyLoopGainMaxPlain);
            break;
        default:
            return kResultFalse;
    }
    char8 text[32];
    snprintf(text, sizeof(text), "%.0f%%", plain * 100.0);
    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// ==============================================================================
// State Persistence - 32 bytes (2 x float + 6 x int32)
// ==============================================================================

inline void saveEcologyParams(const EcologyParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.loopGain.load(std::memory_order_relaxed));
    for (const auto& f : params.loopFilterMode) {
        streamer.writeInt32(static_cast<Steinberg::int32>(f.load(std::memory_order_relaxed)));
    }
}

/// EOF-safe: the first failed read returns false and leaves every later field at
/// its CURRENT value. Non-finite floats are skipped (field unchanged); finite
/// floats clamp to the plain range; indices clamp to [0, 2].
inline bool loadEcologyParams(EcologyParams& params, Steinberg::IBStreamer& streamer) {
    float f = 0.0f;
    Steinberg::int32 n = 0;

    if (!streamer.readFloat(f)) { return false; }
    if (Krate::DSP::detail::isFinite(f)) {
        params.mix.store(std::clamp(f, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    if (!streamer.readFloat(f)) { return false; }
    if (Krate::DSP::detail::isFinite(f)) {
        params.loopGain.store(
            std::clamp(f, 0.0f, static_cast<float>(kEcologyLoopGainMaxPlain)),
            std::memory_order_relaxed);
    }

    for (auto& mode : params.loopFilterMode) {
        if (!streamer.readInt32(n)) { return false; }
        mode.store(std::clamp(static_cast<int>(n), 0, kEcologyNumFilterModes - 1),
                   std::memory_order_relaxed);
    }
    return true;
}

// ==============================================================================
// Controller State Sync (inverts every mapping above)
// ==============================================================================

template <typename SetParamFunc>
inline void loadEcologyParamsToController(Steinberg::IBStreamer& streamer,
                                          SetParamFunc setParam) {
    float f = 0.0f;
    Steinberg::int32 n = 0;

    if (!streamer.readFloat(f)) { return; }
    if (Krate::DSP::detail::isFinite(f)) {
        setParam(kEcologyMixId,
                 linearToNormalized(static_cast<double>(std::clamp(f, 0.0f, 1.0f)), 0.0, 1.0));
    }

    if (!streamer.readFloat(f)) { return; }
    if (Krate::DSP::detail::isFinite(f)) {
        const float g = std::clamp(f, 0.0f, static_cast<float>(kEcologyLoopGainMaxPlain));
        setParam(kEcologyLoopGainId,
                 linearToNormalized(static_cast<double>(g), 0.0, kEcologyLoopGainMaxPlain));
    }

    for (std::size_t l = 0; l < kEcologyNumLoops; ++l) {
        if (!streamer.readInt32(n)) { return; }
        setParam(static_cast<Steinberg::Vst::ParamID>(kEcologyLoop0FilterModeId + l),
                 indexToNormalized(static_cast<int>(n), kEcologyNumFilterModes));
    }
}

}  // namespace Vorago
