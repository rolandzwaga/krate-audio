#pragma once
#include "plugin_ids.h"
#include "parameters/state_load_helpers.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace Ruinae {

struct RuinaeReverbParams {
    std::atomic<float> size{0.5f};        // 0-1
    std::atomic<float> damping{0.5f};     // 0-1
    std::atomic<float> width{1.0f};       // 0-1
    std::atomic<float> mix{0.5f};         // 0-1
    std::atomic<float> preDelayMs{0.0f};  // 0-100 ms
    std::atomic<float> diffusion{0.7f};   // 0-1
    std::atomic<bool> freeze{false};
    std::atomic<float> modRateHz{0.5f};   // 0-2 Hz
    std::atomic<float> modDepth{0.0f};    // 0-1
    std::atomic<int32_t> reverbType{0};   // 0=Plate, 1=Hall
};

inline void handleReverbParamChange(
    RuinaeReverbParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kReverbSizeId:
            params.size.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kReverbDampingId:
            params.damping.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kReverbWidthId:
            params.width.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kReverbMixId:
            params.mix.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kReverbPreDelayId:
            // 0-1 -> 0-100 ms
            params.preDelayMs.store(
                std::clamp(static_cast<float>(value * 100.0), 0.0f, 100.0f),
                std::memory_order_relaxed); break;
        case kReverbDiffusionId:
            params.diffusion.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kReverbFreezeId:
            params.freeze.store(value >= 0.5, std::memory_order_relaxed); break;
        case kReverbModRateId:
            // 0-1 -> 0-2 Hz
            params.modRateHz.store(
                std::clamp(static_cast<float>(value * 2.0), 0.0f, 2.0f),
                std::memory_order_relaxed); break;
        case kReverbModDepthId:
            params.modDepth.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kReverbTypeId:
            params.reverbType.store(
                static_cast<int32_t>(std::round(value)),
                std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerReverbParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("Reverb Size"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kReverbSizeId);
    parameters.addParameter(STR16("Reverb Damping"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kReverbDampingId);
    parameters.addParameter(STR16("Reverb Width"), STR16(""), 0, 1.0,
        ParameterInfo::kCanAutomate, kReverbWidthId);
    parameters.addParameter(STR16("Reverb Mix"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kReverbMixId);
    parameters.addParameter(STR16("Reverb Pre-Delay"), STR16("ms"), 0, 0.0,
        ParameterInfo::kCanAutomate, kReverbPreDelayId);
    parameters.addParameter(STR16("Reverb Diffusion"), STR16(""), 0, 0.7,
        ParameterInfo::kCanAutomate, kReverbDiffusionId);
    parameters.addParameter(STR16("Reverb Freeze"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kReverbFreezeId);
    parameters.addParameter(STR16("Reverb Mod Rate"), STR16("Hz"), 0, 0.25,
        ParameterInfo::kCanAutomate, kReverbModRateId);
    parameters.addParameter(STR16("Reverb Mod Depth"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kReverbModDepthId);

    // Reverb Type selector (125-dual-reverb, FR-023)
    auto* reverbTypeParam = new StringListParameter(
        STR16("Reverb Type"), kReverbTypeId);
    reverbTypeParam->appendString(STR16("Plate"));
    reverbTypeParam->appendString(STR16("Hall"));
    parameters.addParameter(reverbTypeParam);
}

inline Steinberg::tresult formatReverbParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kReverbSizeId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kReverbDampingId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kReverbWidthId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kReverbMixId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kReverbPreDelayId: {
            float ms = static_cast<float>(value * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kReverbDiffusionId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kReverbModRateId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.2f Hz", value * 2.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kReverbModDepthId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveReverbParams(const RuinaeReverbParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.size.load(std::memory_order_relaxed));
    streamer.writeFloat(params.damping.load(std::memory_order_relaxed));
    streamer.writeFloat(params.width.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.preDelayMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.diffusion.load(std::memory_order_relaxed));
    streamer.writeInt32(params.freeze.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeFloat(params.modRateHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.modDepth.load(std::memory_order_relaxed));
}

// Saved-field ranges, read by both parsers (R-211)
namespace ReverbLoad {
inline constexpr LoadedFloat kSize{.lo = 0.0f, .hi = 1.0f, .def = 0.5f};
inline constexpr LoadedFloat kDamping{.lo = 0.0f, .hi = 1.0f, .def = 0.5f};
inline constexpr LoadedFloat kWidth{.lo = 0.0f, .hi = 1.0f, .def = 1.0f};
inline constexpr LoadedFloat kMix{.lo = 0.0f, .hi = 1.0f, .def = 0.5f};
inline constexpr LoadedFloat kPreDelayMs{.lo = 0.0f, .hi = 100.0f, .def = 0.0f};
inline constexpr LoadedFloat kDiffusion{.lo = 0.0f, .hi = 1.0f, .def = 0.7f};
inline constexpr LoadedFloat kModRateHz{.lo = 0.0f, .hi = 2.0f, .def = 0.5f};
inline constexpr LoadedFloat kModDepth{.lo = 0.0f, .hi = 1.0f, .def = 0.0f};
// The reverb-type int32 after the pack (state v5+): 0 = Plate, 1 = Hall
inline constexpr LoadedInt kType{.lo = 0, .hi = 1};
} // namespace ReverbLoad

/// Size and damping that give a state saved before v9 the Hall decay it was saved
/// with (R-220). Before v9 the Hall (FDNReverb) ignored its own T60 map: the loop
/// gain was 0.78 + 0.17 * size^2 per pass of its mean 724.25-sample (48 kHz)
/// loop, so the tail ran 0.44 s (size 0) to 2.2 s (size 1), and Damping only
/// darkened lines by the ratio its 0.5 + 9.5 * size target implied. Since v9 the
/// Hall decays at 0.5 + 9.5 * size (measured 0.99x) and Damping sets the
/// Nyquist T60 to T60 * 0.05^damping.
///
/// The mapping keeps the old broadband T60 (measured 1.043x-1.079x the loop-gain
/// model, the kOld fit below) and the old total decay rate at Nyquist. Measured
/// on the standalone reverb at 44.1 / 48 / 96 kHz, old size 0.4-1.0 x damping
/// 0-1: broadband T60 within 5.5 %, octave T60 (125 Hz-8 kHz) within 15.5 %.
/// Old tails shorter than the new 0.5 s minimum (old size below about 0.365)
/// load size 0 and a damping that keeps their high-frequency decay; their
/// broadband T60 is 0.36-0.49 s where it was 0.22-0.48 s.
inline void migratePreV9HallDecay(float& size, float& damping) noexcept {
    constexpr float kLoopSeconds = 724.25f / 48000.0f;  // legacy mean loop
    constexpr float kLineShare = 399.75f / 724.25f;    // damped line share of the loop
    constexpr float kNewTimeScale = 0.99f;              // new measured T60 / target
    const float s = std::clamp(size, 0.0f, 1.0f);
    const float d = std::clamp(damping, 0.0f, 1.0f);
    const float oldLoopGain = 0.78f + (0.17f * s * s);
    const float modelT60 = 60.0f * kLoopSeconds / (-20.0f * std::log10(oldLoopGain));
    const float kOld = 1.043f + (0.036f * s * s * s * s);
    const float newT60 = std::max(0.5f, modelT60 * kOld / kNewTimeScale);
    size = std::clamp((newT60 - 0.5f) / 9.5f, 0.0f, 1.0f);
    // Nyquist decay rate of the old loop (60 dB per modelT60, plus the old
    // damping filters' share), as a multiple of the new DC rate.
    const float oldTarget = 0.5f + (9.5f * s);
    const float nyquistRateRatio = newT60 * (kNewTimeScale / kOld)
        * ((1.0f / modelT60) + ((std::pow(20.0f, d) - 1.0f) * kLineShare / oldTarget));
    damping = std::clamp(std::log(nyquistRateRatio) / std::log(20.0f), 0.0f, 1.0f);
}

inline bool loadReverbParams(RuinaeReverbParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (!readLoaded(streamer, fv, ReverbLoad::kSize)) { return false; } params.size.store(fv, std::memory_order_relaxed);
    if (!readLoaded(streamer, fv, ReverbLoad::kDamping)) { return false; } params.damping.store(fv, std::memory_order_relaxed);
    if (!readLoaded(streamer, fv, ReverbLoad::kWidth)) { return false; } params.width.store(fv, std::memory_order_relaxed);
    if (!readLoaded(streamer, fv, ReverbLoad::kMix)) { return false; } params.mix.store(fv, std::memory_order_relaxed);
    if (!readLoaded(streamer, fv, ReverbLoad::kPreDelayMs)) { return false; } params.preDelayMs.store(fv, std::memory_order_relaxed);
    if (!readLoaded(streamer, fv, ReverbLoad::kDiffusion)) { return false; } params.diffusion.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.freeze.store(iv != 0, std::memory_order_relaxed);
    if (!readLoaded(streamer, fv, ReverbLoad::kModRateHz)) { return false; } params.modRateHz.store(fv, std::memory_order_relaxed);
    if (!readLoaded(streamer, fv, ReverbLoad::kModDepth)) { return false; } params.modDepth.store(fv, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadReverbParamsToController(
    Steinberg::IBStreamer& streamer, const SetParamFunc& setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (readLoaded(streamer, fv, ReverbLoad::kSize)) setParam(kReverbSizeId, static_cast<double>(fv));
    if (readLoaded(streamer, fv, ReverbLoad::kDamping)) setParam(kReverbDampingId, static_cast<double>(fv));
    if (readLoaded(streamer, fv, ReverbLoad::kWidth)) setParam(kReverbWidthId, static_cast<double>(fv));
    if (readLoaded(streamer, fv, ReverbLoad::kMix)) setParam(kReverbMixId, static_cast<double>(fv));
    if (readLoaded(streamer, fv, ReverbLoad::kPreDelayMs)) setParam(kReverbPreDelayId, static_cast<double>(fv / 100.0f));
    if (readLoaded(streamer, fv, ReverbLoad::kDiffusion)) setParam(kReverbDiffusionId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kReverbFreezeId, iv != 0 ? 1.0 : 0.0);
    if (readLoaded(streamer, fv, ReverbLoad::kModRateHz)) setParam(kReverbModRateId, static_cast<double>(fv / 2.0f));
    if (readLoaded(streamer, fv, ReverbLoad::kModDepth)) setParam(kReverbModDepthId, static_cast<double>(fv));
}

} // namespace Ruinae
