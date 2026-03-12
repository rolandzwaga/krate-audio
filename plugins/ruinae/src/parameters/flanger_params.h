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
// Flanger Parameter Struct
// =============================================================================

struct RuinaeFlangerParams {
    std::atomic<float> rateHz{0.5f};        // 0.05-5.0 Hz
    std::atomic<float> depth{0.5f};         // 0-1
    std::atomic<float> feedback{0.0f};      // -1 to +1
    std::atomic<float> mix{0.5f};           // 0-1 (true crossfade)
    std::atomic<float> stereoSpread{90.0f}; // 0-360 degrees
    std::atomic<int> waveform{1};           // 0=Sine, 1=Triangle
    std::atomic<bool> sync{false};          // tempo sync
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex};
};

// =============================================================================
// Parameter Change Handler (denormalization)
// =============================================================================

inline void handleFlangerParamChange(
    RuinaeFlangerParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kFlangerRateId:
            // 0-1 -> 0.05-5.0 Hz
            params.rateHz.store(
                std::clamp(static_cast<float>(0.05 + value * 4.95), 0.05f, 5.0f),
                std::memory_order_relaxed); break;
        case kFlangerDepthId:
            params.depth.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kFlangerFeedbackId:
            // 0-1 -> -1 to +1
            params.feedback.store(
                std::clamp(static_cast<float>(value * 2.0 - 1.0), -1.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kFlangerMixId:
            params.mix.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kFlangerStereoSpreadId:
            // 0-1 -> 0-360 degrees
            params.stereoSpread.store(
                std::clamp(static_cast<float>(value * 360.0), 0.0f, 360.0f),
                std::memory_order_relaxed); break;
        case kFlangerWaveformId:
            params.waveform.store(
                std::clamp(static_cast<int>(value * 1.0 + 0.5), 0, 1),
                std::memory_order_relaxed); break;
        case kFlangerSyncId:
            params.sync.store(value >= 0.5, std::memory_order_relaxed); break;
        case kFlangerNoteValueId:
            params.noteValue.store(
                std::clamp(static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                    0, Parameters::kNoteValueDropdownCount - 1),
                std::memory_order_relaxed); break;
        default: break;
    }
}

// =============================================================================
// Flanger Parameter Registration
// =============================================================================

inline void registerFlangerParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Flanger Rate"), STR16("Hz"), 0, 0.0909,
        ParameterInfo::kCanAutomate, kFlangerRateId);  // default ~0.5 Hz: (0.5-0.05)/4.95
    parameters.addParameter(STR16("Flanger Depth"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kFlangerDepthId);
    parameters.addParameter(STR16("Flanger Feedback"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kFlangerFeedbackId);  // 0.5 norm = 0.0 feedback
    parameters.addParameter(STR16("Flanger Mix"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kFlangerMixId);
    parameters.addParameter(STR16("Flanger Spread"), STR16("\xC2\xB0"), 0, 0.25,
        ParameterInfo::kCanAutomate, kFlangerStereoSpreadId);  // default 90/360 = 0.25
    parameters.addParameter(createDropdownParameter(
        STR16("Flanger Waveform"), kFlangerWaveformId,
        {STR16("Sine"), STR16("Triangle")}));
    parameters.addParameter(STR16("Flanger Sync"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kFlangerSyncId);
    parameters.addParameter(createNoteValueDropdown(
        STR16("Flanger Note Value"), kFlangerNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));
}

// =============================================================================
// Display Formatting
// =============================================================================

inline Steinberg::tresult formatFlangerParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    char8 text[32];
    switch (id) {
        case kFlangerRateId: {
            float hz = static_cast<float>(0.05 + value * 4.95);
            snprintf(text, sizeof(text), "%.2f Hz", hz);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kFlangerDepthId:
        case kFlangerMixId:
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        case kFlangerFeedbackId:
            snprintf(text, sizeof(text), "%+.0f%%", (value * 2.0 - 1.0) * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        case kFlangerStereoSpreadId: {
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

inline void saveFlangerParams(const RuinaeFlangerParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.stereoSpread.load(std::memory_order_relaxed));
    streamer.writeInt32(params.waveform.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sync.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
}

inline bool loadFlangerParams(RuinaeFlangerParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (!streamer.readFloat(fv)) { return false; } params.rateHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.depth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.feedback.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.mix.store(fv, std::memory_order_relaxed);
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
inline void loadFlangerParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (streamer.readFloat(fv)) setParam(kFlangerRateId, static_cast<double>((fv - 0.05f) / 4.95f));
    if (streamer.readFloat(fv)) setParam(kFlangerDepthId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kFlangerFeedbackId, static_cast<double>((fv + 1.0f) / 2.0f));
    if (streamer.readFloat(fv)) setParam(kFlangerMixId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kFlangerStereoSpreadId, static_cast<double>(fv / 360.0f));
    if (streamer.readInt32(iv)) setParam(kFlangerWaveformId, static_cast<double>(iv) / 1.0);
    if (streamer.readInt32(iv)) setParam(kFlangerSyncId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(iv)) setParam(kFlangerNoteValueId, static_cast<double>(iv) / (Parameters::kNoteValueDropdownCount - 1));
}

} // namespace Ruinae
