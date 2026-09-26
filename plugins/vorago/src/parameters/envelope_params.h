#pragma once

// ==============================================================================
// Vorago - Envelope Parameters (ID 1200-1299)   T026, plan section 3.2 / 3.4
// ==============================================================================
// Group 7 pack contract (shape of global_params.h). Route ENG: the processor
// forwards these to VoragoEngine::setEnvelopeMode / setEnvelopeStageTimeMs /
// setEnvelopeReleaseMs / setGrowthDurationSeconds.
//
// Defaults are the voice's shipped constants (vorago_voice.h:322-333), not
// re-typed literals. Plain ranges are the destination clamps:
//   stage/release times [0, VoragoVoice::kEnvelopeMaxStageTimeMs] ms
//     (multi_stage_envelope.h setStage/setReleaseTime clamp), offset-log eps 10 ms;
//   growth duration [GrowthEnvelope::kMinDuration, VoragoVoice::kGrowthMaxDurationSeconds] s,
//     plain log (non-zero floor).
//
// Stream (ascending ID): int32 mode + 4 x float stage time + float release
// + float growth duration = 28 bytes.
// ==============================================================================

#include "plugin_ids.h"
#include "parameters/param_mapping.h"

#include "ui/parameter_helpers.h"  // plugins/shared/src/ui/parameter_helpers.h

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/growth_envelope.h>
#include <krate/dsp/systems/vorago_voice.h>

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Vorago {

inline constexpr int kNumEnvelopeModes = 2;  // Standard, Growth (VoragoVoice::EnvelopeMode)

inline constexpr double kEnvelopeTimeMinMs = 0.0;
inline constexpr double kEnvelopeTimeMaxMs =
    static_cast<double>(Krate::DSP::VoragoVoice::kEnvelopeMaxStageTimeMs);  // 120000
inline constexpr double kEnvelopeTimeEpsMs = 10.0;

inline constexpr double kGrowthDurationMinS =
    static_cast<double>(Krate::DSP::GrowthEnvelope::kMinDuration);  // 1
inline constexpr double kGrowthDurationMaxS =
    static_cast<double>(Krate::DSP::VoragoVoice::kGrowthMaxDurationSeconds);  // 120

static_assert(static_cast<int>(Krate::DSP::VoragoVoice::EnvelopeMode::Standard) == 0 &&
                  static_cast<int>(Krate::DSP::VoragoVoice::EnvelopeMode::Growth) == 1,
              "envelope mode list index must equal the EnvelopeMode value");
static_assert(kEnvelopeTimeMaxMs == 120000.0, "plan section 3.2: stage-time range [0, 120000] ms");
static_assert(kGrowthDurationMinS == 1.0 && kGrowthDurationMaxS == 120.0,
              "plan section 3.2: growth duration range [1, 120] s");

struct EnvelopeParams {
    std::atomic<int> mode{0};  ///< 0 Standard, 1 Growth
    std::atomic<float> stage0TimeMs{Krate::DSP::VoragoVoice::kDefaultStageTimesMs[0]};  // 20000
    std::atomic<float> stage1TimeMs{Krate::DSP::VoragoVoice::kDefaultStageTimesMs[1]};  // 30000
    std::atomic<float> stage2TimeMs{Krate::DSP::VoragoVoice::kDefaultStageTimesMs[2]};  // 45000
    std::atomic<float> stage3TimeMs{Krate::DSP::VoragoVoice::kDefaultStageTimesMs[3]};  // 60000
    std::atomic<float> releaseMs{Krate::DSP::VoragoVoice::kDefaultReleaseMs};           // 45000
    std::atomic<float> growthDurationSeconds{
        Krate::DSP::VoragoVoice::kDefaultGrowthDurationSeconds};  // 120
};

namespace detail {

[[nodiscard]] inline double envelopeTimeFromNormalized(double n) noexcept {
    return offsetLogFromNormalized(n, kEnvelopeTimeMinMs, kEnvelopeTimeMaxMs, kEnvelopeTimeEpsMs);
}

[[nodiscard]] inline double envelopeTimeToNormalized(double ms) noexcept {
    return offsetLogToNormalized(ms, kEnvelopeTimeMinMs, kEnvelopeTimeMaxMs, kEnvelopeTimeEpsMs);
}

[[nodiscard]] inline double growthDurationFromNormalized(double n) noexcept {
    return Krate::Plugins::logMapFromNormalized(n, kGrowthDurationMinS, kGrowthDurationMaxS);
}

[[nodiscard]] inline double growthDurationToNormalized(double s) noexcept {
    return Krate::Plugins::logMapToNormalized(s, kGrowthDurationMinS, kGrowthDurationMaxS);
}

/// The time field for stage/release IDs 1201-1205; nullptr for any other ID.
[[nodiscard]] inline std::atomic<float>* envelopeTimeField(EnvelopeParams& p,
                                                           Steinberg::Vst::ParamID id) noexcept {
    switch (id) {
        case kEnvelopeStage0TimeId: return &p.stage0TimeMs;
        case kEnvelopeStage1TimeId: return &p.stage1TimeMs;
        case kEnvelopeStage2TimeId: return &p.stage2TimeMs;
        case kEnvelopeStage3TimeId: return &p.stage3TimeMs;
        case kEnvelopeReleaseId: return &p.releaseMs;
        default: return nullptr;
    }
}

}  // namespace detail

// ==============================================================================
// Parameter Change Handler (caller has rejected non-finite and clamped [0, 1])
// ==============================================================================

inline void handleEnvelopeParamChange(EnvelopeParams& params, Steinberg::Vst::ParamID id,
                                      Steinberg::Vst::ParamValue value) noexcept {
    switch (id) {
        case kEnvelopeModeId:
            params.mode.store(indexFromNormalized(value, kNumEnvelopeModes),
                              std::memory_order_relaxed);
            break;
        case kEnvelopeStage0TimeId:
        case kEnvelopeStage1TimeId:
        case kEnvelopeStage2TimeId:
        case kEnvelopeStage3TimeId:
        case kEnvelopeReleaseId:
            detail::envelopeTimeField(params, id)
                ->store(static_cast<float>(detail::envelopeTimeFromNormalized(value)),
                        std::memory_order_relaxed);
            break;
        case kEnvelopeGrowthDurationId:
            params.growthDurationSeconds.store(
                static_cast<float>(detail::growthDurationFromNormalized(value)),
                std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================

inline void registerEnvelopeParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    using Voice = Krate::DSP::VoragoVoice;

    auto* mode = Krate::Plugins::createDropdownParameterWithDefault(
        STR16("Envelope Mode"), kEnvelopeModeId, /*defaultIndex=*/0,
        {STR16("Standard"), STR16("Growth")});
    // P-1 (global_params.h:82-84): pin the REGISTERED default, not only the current value.
    mode->getInfo().defaultNormalizedValue = indexToNormalized(0, kNumEnvelopeModes);
    parameters.addParameter(mode);

    const auto timeN0 = [](float ms) {
        return detail::envelopeTimeToNormalized(static_cast<double>(ms));
    };
    parameters.addParameter(STR16("Envelope Stage 1 Time"), STR16("ms"), 0,
                            timeN0(Voice::kDefaultStageTimesMs[0]), ParameterInfo::kCanAutomate,
                            kEnvelopeStage0TimeId);
    parameters.addParameter(STR16("Envelope Stage 2 Time"), STR16("ms"), 0,
                            timeN0(Voice::kDefaultStageTimesMs[1]), ParameterInfo::kCanAutomate,
                            kEnvelopeStage1TimeId);
    parameters.addParameter(STR16("Envelope Stage 3 Time"), STR16("ms"), 0,
                            timeN0(Voice::kDefaultStageTimesMs[2]), ParameterInfo::kCanAutomate,
                            kEnvelopeStage2TimeId);
    parameters.addParameter(STR16("Envelope Stage 4 Time"), STR16("ms"), 0,
                            timeN0(Voice::kDefaultStageTimesMs[3]), ParameterInfo::kCanAutomate,
                            kEnvelopeStage3TimeId);
    parameters.addParameter(STR16("Envelope Release"), STR16("ms"), 0,
                            timeN0(Voice::kDefaultReleaseMs), ParameterInfo::kCanAutomate,
                            kEnvelopeReleaseId);
    parameters.addParameter(
        STR16("Envelope Growth Duration"), STR16("s"), 0,
        detail::growthDurationToNormalized(
            static_cast<double>(Voice::kDefaultGrowthDurationSeconds)),
        ParameterInfo::kCanAutomate, kEnvelopeGrowthDurationId);
}

// ==============================================================================
// Display Formatting (continuous IDs only; the mode list formats itself)
// ==============================================================================

inline Steinberg::tresult formatEnvelopeParam(Steinberg::Vst::ParamID id,
                                              Steinberg::Vst::ParamValue value,
                                              Steinberg::Vst::String128 string) {
    using namespace Steinberg;

    char8 text[32];
    switch (id) {
        case kEnvelopeStage0TimeId:
        case kEnvelopeStage1TimeId:
        case kEnvelopeStage2TimeId:
        case kEnvelopeStage3TimeId:
        case kEnvelopeReleaseId:
            snprintf(text, sizeof(text), "%.0f ms", detail::envelopeTimeFromNormalized(value));
            break;
        case kEnvelopeGrowthDurationId:
            snprintf(text, sizeof(text), "%.1f s", detail::growthDurationFromNormalized(value));
            break;
        default:
            return kResultFalse;
    }
    UString(string, 128).fromAscii(text);
    return kResultOk;
}

// ==============================================================================
// State Persistence - 28 bytes (int32 + 6 x float), ascending ID order
// ==============================================================================

inline void saveEnvelopeParams(const EnvelopeParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(
        static_cast<Steinberg::int32>(params.mode.load(std::memory_order_relaxed)));
    streamer.writeFloat(params.stage0TimeMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.stage1TimeMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.stage2TimeMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.stage3TimeMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.releaseMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.growthDurationSeconds.load(std::memory_order_relaxed));
}

/// EOF-safe: the first failed read returns false and leaves every later field at
/// its CURRENT value. Non-finite floats are skipped (field unchanged); finite floats
/// clamp to the plain range; the mode index clamps to [0, 1].
inline bool loadEnvelopeParams(EnvelopeParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 m = 0;
    if (!streamer.readInt32(m)) { return false; }
    params.mode.store(std::clamp(static_cast<int>(m), 0, kNumEnvelopeModes - 1),
                      std::memory_order_relaxed);

    const auto loadFloat = [&streamer](std::atomic<float>& field, double mn, double mx) {
        float v = 0.0f;
        if (!streamer.readFloat(v)) { return false; }
        if (Krate::DSP::detail::isFinite(v)) {  // fast-math-immune, db_utils.h:118
            field.store(std::clamp(v, static_cast<float>(mn), static_cast<float>(mx)),
                        std::memory_order_relaxed);
        }
        return true;
    };

    if (!loadFloat(params.stage0TimeMs, kEnvelopeTimeMinMs, kEnvelopeTimeMaxMs)) { return false; }
    if (!loadFloat(params.stage1TimeMs, kEnvelopeTimeMinMs, kEnvelopeTimeMaxMs)) { return false; }
    if (!loadFloat(params.stage2TimeMs, kEnvelopeTimeMinMs, kEnvelopeTimeMaxMs)) { return false; }
    if (!loadFloat(params.stage3TimeMs, kEnvelopeTimeMinMs, kEnvelopeTimeMaxMs)) { return false; }
    if (!loadFloat(params.releaseMs, kEnvelopeTimeMinMs, kEnvelopeTimeMaxMs)) { return false; }
    if (!loadFloat(params.growthDurationSeconds, kGrowthDurationMinS, kGrowthDurationMaxS)) {
        return false;
    }
    return true;
}

// ==============================================================================
// Controller State Sync (same read order and finite/clamp rule; inverse maps)
// ==============================================================================

template <typename SetParamFunc>
inline void loadEnvelopeParamsToController(Steinberg::IBStreamer& streamer,
                                           SetParamFunc setParam) {
    Steinberg::int32 m = 0;
    if (!streamer.readInt32(m)) { return; }
    setParam(kEnvelopeModeId, indexToNormalized(static_cast<int>(m), kNumEnvelopeModes));

    constexpr Steinberg::Vst::ParamID kTimeIds[] = {kEnvelopeStage0TimeId, kEnvelopeStage1TimeId,
                                                    kEnvelopeStage2TimeId, kEnvelopeStage3TimeId,
                                                    kEnvelopeReleaseId};
    for (const Steinberg::Vst::ParamID id : kTimeIds) {
        float v = 0.0f;
        if (!streamer.readFloat(v)) { return; }
        if (Krate::DSP::detail::isFinite(v)) {
            const float clamped = std::clamp(v, static_cast<float>(kEnvelopeTimeMinMs),
                                             static_cast<float>(kEnvelopeTimeMaxMs));
            setParam(id, detail::envelopeTimeToNormalized(static_cast<double>(clamped)));
        }
    }

    float g = 0.0f;
    if (!streamer.readFloat(g)) { return; }
    if (Krate::DSP::detail::isFinite(g)) {
        const float clamped = std::clamp(g, static_cast<float>(kGrowthDurationMinS),
                                         static_cast<float>(kGrowthDurationMaxS));
        setParam(kEnvelopeGrowthDurationId,
                 detail::growthDurationToNormalized(static_cast<double>(clamped)));
    }
}

}  // namespace Vorago
