#pragma once
#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace Ruinae {

struct SettingsParams {
    std::atomic<float> pitchBendRangeSemitones{2.0f};  // 0-24 semitones
    std::atomic<int> velocityCurve{0};                  // VelocityCurve index (0-3)
    std::atomic<float> tuningReferenceHz{440.0f};       // 400-480 Hz
    std::atomic<int> voiceAllocMode{1};                 // AllocationMode index (0-3), default=Oldest(1)
    std::atomic<int> voiceStealMode{0};                 // StealMode index (0-1), default=Hard(0)
    std::atomic<bool> gainCompensation{true};           // default=ON for new presets
};

inline void handleSettingsParamChange(
    SettingsParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kSettingsPitchBendRangeId:
            // Linear: 0-1 -> 0-24 semitones, integer steps (stepCount=24)
            params.pitchBendRangeSemitones.store(
                std::clamp(static_cast<float>(std::round(value * 24.0)), 0.0f, 24.0f),
                std::memory_order_relaxed); break;
        case kSettingsVelocityCurveId:
            params.velocityCurve.store(
                std::clamp(static_cast<int>(value * 3.0 + 0.5), 0, 3),
                std::memory_order_relaxed); break;
        case kSettingsTuningReferenceId:
            // Linear: 0-1 -> 400-480 Hz
            params.tuningReferenceHz.store(
                std::clamp(400.0f + static_cast<float>(value) * 80.0f, 400.0f, 480.0f),
                std::memory_order_relaxed); break;
        case kSettingsVoiceAllocModeId:
            params.voiceAllocMode.store(
                std::clamp(static_cast<int>(value * 3.0 + 0.5), 0, 3),
                std::memory_order_relaxed); break;
        case kSettingsVoiceStealModeId:
            params.voiceStealMode.store(
                std::clamp(static_cast<int>(value * 1.0 + 0.5), 0, 1),
                std::memory_order_relaxed); break;
        case kSettingsGainCompensationId:
            params.gainCompensation.store(value >= 0.5, std::memory_order_relaxed); break;
        default: break;
    }
}

inline void registerSettingsParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    // Pitch Bend Range: 0-24 semitones, integer steps, default 2
    parameters.addParameter(STR16("Pitch Bend Range"), STR16("st"), 24,
        2.0 / 24.0,  // normalized default for 2 semitones
        ParameterInfo::kCanAutomate, kSettingsPitchBendRangeId);

    // Velocity Curve: 4 options, default Linear (0)
    parameters.addParameter(createDropdownParameter(
        STR16("Velocity Curve"), kSettingsVelocityCurveId,
        {STR16("Linear"), STR16("Soft"), STR16("Hard"), STR16("Fixed")}
    ));

    // Tuning Reference: 400-480 Hz, continuous, default 440 Hz
    parameters.addParameter(STR16("Tuning Reference"), STR16("Hz"), 0,
        0.5,  // normalized default: (440 - 400) / 80 = 0.5
        ParameterInfo::kCanAutomate, kSettingsTuningReferenceId);

    // Voice Allocation: 4 options, default Oldest (1)
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Voice Allocation"), kSettingsVoiceAllocModeId, 1,
        {STR16("Round Robin"), STR16("Oldest"), STR16("Lowest Velocity"), STR16("Highest Note")}
    ));

    // Voice Steal Mode: 2 options, default Hard (0)
    parameters.addParameter(createDropdownParameter(
        STR16("Voice Steal"), kSettingsVoiceStealModeId,
        {STR16("Hard"), STR16("Soft")}
    ));

    // Gain Compensation: on/off, default ON (1.0)
    parameters.addParameter(STR16("Gain Compensation"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kSettingsGainCompensationId);
}

inline Steinberg::tresult formatSettingsParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kSettingsPitchBendRangeId: {
            int st = static_cast<int>(std::round(value * 24.0));
            char8 text[32];
            snprintf(text, sizeof(text), "%d st", st);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kSettingsTuningReferenceId: {
            float hz = 400.0f + static_cast<float>(value) * 80.0f;
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f Hz", static_cast<double>(hz));
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveSettingsParams(const SettingsParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.pitchBendRangeSemitones.load(std::memory_order_relaxed));
    streamer.writeInt32(params.velocityCurve.load(std::memory_order_relaxed));
    streamer.writeFloat(params.tuningReferenceHz.load(std::memory_order_relaxed));
    streamer.writeInt32(params.voiceAllocMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.voiceStealMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.gainCompensation.load(std::memory_order_relaxed) ? 1 : 0);
}

inline bool loadSettingsParams(SettingsParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    if (!streamer.readFloat(fv)) { return false; } params.pitchBendRangeSemitones.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.velocityCurve.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.tuningReferenceHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.voiceAllocMode.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.voiceStealMode.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.gainCompensation.store(iv != 0, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadSettingsParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 iv = 0; float fv = 0.0f;
    // Pitch Bend Range: inverse of (normalized * 24)
    if (streamer.readFloat(fv)) setParam(kSettingsPitchBendRangeId, static_cast<double>(fv) / 24.0);
    // Velocity Curve: index / 3
    if (streamer.readInt32(iv)) setParam(kSettingsVelocityCurveId, static_cast<double>(iv) / 3.0);
    // Tuning Reference: inverse of (400 + normalized * 80)
    if (streamer.readFloat(fv)) setParam(kSettingsTuningReferenceId, static_cast<double>((fv - 400.0f) / 80.0f));
    // Voice Allocation: index / 3
    if (streamer.readInt32(iv)) setParam(kSettingsVoiceAllocModeId, static_cast<double>(iv) / 3.0);
    // Voice Steal: index / 1
    if (streamer.readInt32(iv)) setParam(kSettingsVoiceStealModeId, static_cast<double>(iv));
    // Gain Compensation: bool -> 0.0 or 1.0
    if (streamer.readInt32(iv)) setParam(kSettingsGainCompensationId, iv != 0 ? 1.0 : 0.0);
}

} // namespace Ruinae
