#pragma once
#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "parameters/note_value_ui.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"
#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

// =============================================================================
// Phaser Parameter Struct
// =============================================================================

struct RuinaePhaserParams {
    std::atomic<float> rateHz{0.5f};        // 0.01-20 Hz
    std::atomic<float> depth{0.5f};         // 0-1
    std::atomic<float> feedback{0.5f};      // -1 to +1 (default +50%)
    std::atomic<float> mix{0.5f};           // 0-1
    std::atomic<int> stages{1};             // dropdown index (0-5), default 1 = 4 stages
    std::atomic<float> centerFreqHz{1000.0f}; // 100-10000 Hz
    std::atomic<float> stereoSpread{0.0f};  // 0-360 degrees
    std::atomic<int> waveform{0};           // PhaserWaveform (0-3)
    std::atomic<bool> sync{false};          // tempo sync
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex};
};

// =============================================================================
// Parameter Change Handler (denormalization)
// =============================================================================

inline void handlePhaserParamChange(
    RuinaePhaserParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kPhaserRateId:
            // 0-1 -> 0.01-20 Hz
            params.rateHz.store(
                std::clamp(static_cast<float>(0.01 + value * 19.99), 0.01f, 20.0f),
                std::memory_order_relaxed); break;
        case kPhaserDepthId:
            params.depth.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kPhaserFeedbackId:
            // 0-1 -> -1 to +1
            params.feedback.store(
                std::clamp(static_cast<float>(value * 2.0 - 1.0), -1.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kPhaserMixId:
            params.mix.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kPhaserStagesId:
            params.stages.store(
                std::clamp(static_cast<int>(value * (kPhaserStagesCount - 1) + 0.5), 0, kPhaserStagesCount - 1),
                std::memory_order_relaxed); break;
        case kPhaserCenterFreqId:
            // 0-1 -> 100-10000 Hz
            params.centerFreqHz.store(
                std::clamp(static_cast<float>(100.0 + value * 9900.0), 100.0f, 10000.0f),
                std::memory_order_relaxed); break;
        case kPhaserStereoSpreadId:
            // 0-1 -> 0-360 degrees
            params.stereoSpread.store(
                std::clamp(static_cast<float>(value * 360.0), 0.0f, 360.0f),
                std::memory_order_relaxed); break;
        case kPhaserWaveformId:
            params.waveform.store(
                std::clamp(static_cast<int>(value * (kPhaserWaveformCount - 1) + 0.5), 0, kPhaserWaveformCount - 1),
                std::memory_order_relaxed); break;
        case kPhaserSyncId:
            params.sync.store(value >= 0.5, std::memory_order_relaxed); break;
        case kPhaserNoteValueId:
            params.noteValue.store(
                std::clamp(static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                    0, Parameters::kNoteValueDropdownCount - 1),
                std::memory_order_relaxed); break;
        default: break;
    }
}

// =============================================================================
// Phaser Parameter Registration
// =============================================================================

inline void registerPhaserParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Phaser Rate"), STR16("Hz"), 0, 0.0245,
        ParameterInfo::kCanAutomate, kPhaserRateId);  // default ~0.5 Hz
    parameters.addParameter(STR16("Phaser Depth"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kPhaserDepthId);
    parameters.addParameter(STR16("Phaser Feedback"), STR16(""), 0, 0.75,
        ParameterInfo::kCanAutomate, kPhaserFeedbackId);  // default 0.75 norm = +50% feedback
    parameters.addParameter(STR16("Phaser Mix"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kPhaserMixId);
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Phaser Stages"), kPhaserStagesId, 1,
        {STR16("2"), STR16("4"), STR16("6"),
         STR16("8"), STR16("10"), STR16("12")}));
    parameters.addParameter(STR16("Phaser Center Freq"), STR16("Hz"), 0, 0.0909,
        ParameterInfo::kCanAutomate, kPhaserCenterFreqId);  // default ~1000 Hz
    parameters.addParameter(STR16("Phaser Spread"), STR16("\xC2\xB0"), 0, 0.0,
        ParameterInfo::kCanAutomate, kPhaserStereoSpreadId);
    parameters.addParameter(createDropdownParameter(
        STR16("Phaser Waveform"), kPhaserWaveformId,
        {STR16("Sine"), STR16("Triangle"), STR16("Sawtooth"), STR16("Square")}));
    parameters.addParameter(STR16("Phaser Sync"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kPhaserSyncId);
    parameters.addParameter(createNoteValueDropdown(
        STR16("Phaser Note Value"), kPhaserNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));
}

// =============================================================================
// Display Formatting
// =============================================================================

inline Steinberg::tresult formatPhaserParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    char8 text[32];
    switch (id) {
        case kPhaserRateId: {
            float hz = static_cast<float>(0.01 + value * 19.99);
            snprintf(text, sizeof(text), "%.2f Hz", hz);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kPhaserDepthId:
        case kPhaserMixId:
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        case kPhaserFeedbackId:
            snprintf(text, sizeof(text), "%+.0f%%", (value * 2.0 - 1.0) * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        case kPhaserCenterFreqId: {
            float hz = static_cast<float>(100.0 + value * 9900.0);
            if (hz >= 1000.0f) snprintf(text, sizeof(text), "%.1f kHz", hz / 1000.0f);
            else snprintf(text, sizeof(text), "%.0f Hz", hz);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kPhaserStereoSpreadId: {
            float deg = static_cast<float>(value * 360.0);
            snprintf(text, sizeof(text), "%.0f\xC2\xB0", deg);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

// =============================================================================
// State Save/Load
// =============================================================================

inline void savePhaserParams(const RuinaePhaserParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
    streamer.writeInt32(params.stages.load(std::memory_order_relaxed));
    streamer.writeFloat(params.centerFreqHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.stereoSpread.load(std::memory_order_relaxed));
    streamer.writeInt32(params.waveform.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sync.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
}

inline bool loadPhaserParams(RuinaePhaserParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (!streamer.readFloat(fv)) { return false; } params.rateHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.depth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.feedback.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.mix.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.stages.store(iv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.centerFreqHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.stereoSpread.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.waveform.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.sync.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.noteValue.store(iv, std::memory_order_relaxed);
    return true;
}

// =============================================================================
// Controller State Restore
// =============================================================================

template<typename SetParamFunc>
inline void loadPhaserParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (streamer.readFloat(fv)) setParam(kPhaserRateId, static_cast<double>((fv - 0.01f) / 19.99f));
    if (streamer.readFloat(fv)) setParam(kPhaserDepthId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kPhaserFeedbackId, static_cast<double>((fv + 1.0f) / 2.0f));
    if (streamer.readFloat(fv)) setParam(kPhaserMixId, static_cast<double>(fv));
    if (streamer.readInt32(iv)) setParam(kPhaserStagesId, static_cast<double>(iv) / (kPhaserStagesCount - 1));
    if (streamer.readFloat(fv)) setParam(kPhaserCenterFreqId, static_cast<double>((fv - 100.0f) / 9900.0f));
    if (streamer.readFloat(fv)) setParam(kPhaserStereoSpreadId, static_cast<double>(fv / 360.0f));
    if (streamer.readInt32(iv)) setParam(kPhaserWaveformId, static_cast<double>(iv) / (kPhaserWaveformCount - 1));
    if (streamer.readInt32(iv)) setParam(kPhaserSyncId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(iv)) setParam(kPhaserNoteValueId, static_cast<double>(iv) / (Parameters::kNoteValueDropdownCount - 1));
}

} // namespace Ruinae
