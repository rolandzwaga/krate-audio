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
// Chorus Parameter Struct
// =============================================================================

struct RuinaeChorusParams {
    std::atomic<float> rateHz{0.5f};        // 0.05-10.0 Hz
    std::atomic<float> depth{0.5f};         // 0-1
    std::atomic<float> feedback{0.0f};      // -1 to +1
    std::atomic<float> mix{0.5f};           // 0-1 (true crossfade)
    std::atomic<float> stereoSpread{180.0f}; // 0-360 degrees
    std::atomic<int> voices{2};             // 1-4
    std::atomic<int> waveform{1};           // 0=Sine, 1=Triangle
    std::atomic<bool> sync{false};          // tempo sync
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex};
};

// =============================================================================
// Parameter Change Handler (denormalization)
// =============================================================================

inline void handleChorusParamChange(
    RuinaeChorusParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kChorusRateId:
            // 0-1 -> 0.05-10.0 Hz
            params.rateHz.store(
                std::clamp(static_cast<float>(0.05 + value * 9.95), 0.05f, 10.0f),
                std::memory_order_relaxed); break;
        case kChorusDepthId:
            params.depth.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kChorusFeedbackId:
            // 0-1 -> -1 to +1
            params.feedback.store(
                std::clamp(static_cast<float>(value * 2.0 - 1.0), -1.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kChorusMixId:
            params.mix.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed); break;
        case kChorusStereoSpreadId:
            // 0-1 -> 0-360 degrees
            params.stereoSpread.store(
                std::clamp(static_cast<float>(value * 360.0), 0.0f, 360.0f),
                std::memory_order_relaxed); break;
        case kChorusVoicesId:
            // 0-1 -> 1-4 (4-step: 0=1, 0.33=2, 0.67=3, 1.0=4)
            params.voices.store(
                std::clamp(static_cast<int>(value * 3.0 + 0.5) + 1, 1, 4),
                std::memory_order_relaxed); break;
        case kChorusWaveformId:
            params.waveform.store(
                std::clamp(static_cast<int>(value * 1.0 + 0.5), 0, 1),
                std::memory_order_relaxed); break;
        case kChorusSyncId:
            params.sync.store(value >= 0.5, std::memory_order_relaxed); break;
        case kChorusNoteValueId:
            params.noteValue.store(
                std::clamp(static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                    0, Parameters::kNoteValueDropdownCount - 1),
                std::memory_order_relaxed); break;
        default: break;
    }
}

// =============================================================================
// Chorus Parameter Registration
// =============================================================================

inline void registerChorusParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    parameters.addParameter(STR16("Chorus Rate"), STR16("Hz"), 0, 0.04523,
        ParameterInfo::kCanAutomate, kChorusRateId);  // default ~0.5 Hz: (0.5-0.05)/9.95
    parameters.addParameter(STR16("Chorus Depth"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kChorusDepthId);
    parameters.addParameter(STR16("Chorus Feedback"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kChorusFeedbackId);  // 0.5 norm = 0.0 feedback
    parameters.addParameter(STR16("Chorus Mix"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kChorusMixId);
    parameters.addParameter(STR16("Chorus Spread"), STR16("\xC2\xB0"), 0, 0.5,
        ParameterInfo::kCanAutomate, kChorusStereoSpreadId);  // default 180/360 = 0.5
    parameters.addParameter(createDropdownParameter(
        STR16("Chorus Voices"), kChorusVoicesId,
        {STR16("1"), STR16("2"), STR16("3"), STR16("4")}));
    parameters.addParameter(createDropdownParameter(
        STR16("Chorus Waveform"), kChorusWaveformId,
        {STR16("Sine"), STR16("Triangle")}));
    parameters.addParameter(STR16("Chorus Sync"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kChorusSyncId);
    parameters.addParameter(createNoteValueDropdown(
        STR16("Chorus Note Value"), kChorusNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));
}

// =============================================================================
// Display Formatting
// =============================================================================

inline Steinberg::tresult formatChorusParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    char8 text[32];
    switch (id) {
        case kChorusRateId: {
            float hz = static_cast<float>(0.05 + value * 9.95);
            snprintf(text, sizeof(text), "%.2f Hz", hz);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kChorusDepthId:
        case kChorusMixId:
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        case kChorusFeedbackId:
            snprintf(text, sizeof(text), "%+.0f%%", (value * 2.0 - 1.0) * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        case kChorusStereoSpreadId: {
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

inline void saveChorusParams(const RuinaeChorusParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.feedback.load(std::memory_order_relaxed));
    streamer.writeFloat(params.mix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.stereoSpread.load(std::memory_order_relaxed));
    streamer.writeInt32(params.voices.load(std::memory_order_relaxed));
    streamer.writeInt32(params.waveform.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sync.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));
}

inline bool loadChorusParams(RuinaeChorusParams& params, Steinberg::IBStreamer& streamer) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (!streamer.readFloat(fv)) { return false; } params.rateHz.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.depth.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.feedback.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.mix.store(fv, std::memory_order_relaxed);
    if (!streamer.readFloat(fv)) { return false; } params.stereoSpread.store(fv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.voices.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.waveform.store(iv, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.sync.store(iv != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(iv)) { return false; } params.noteValue.store(iv, std::memory_order_relaxed);
    return true;
}

// =============================================================================
// Controller State Restore
// =============================================================================

template<typename SetParamFunc>
inline void loadChorusParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    float fv = 0.0f; Steinberg::int32 iv = 0;
    if (streamer.readFloat(fv)) setParam(kChorusRateId, static_cast<double>((fv - 0.05f) / 9.95f));
    if (streamer.readFloat(fv)) setParam(kChorusDepthId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kChorusFeedbackId, static_cast<double>((fv + 1.0f) / 2.0f));
    if (streamer.readFloat(fv)) setParam(kChorusMixId, static_cast<double>(fv));
    if (streamer.readFloat(fv)) setParam(kChorusStereoSpreadId, static_cast<double>(fv / 360.0f));
    if (streamer.readInt32(iv)) setParam(kChorusVoicesId, static_cast<double>(iv - 1) / 3.0);
    if (streamer.readInt32(iv)) setParam(kChorusWaveformId, static_cast<double>(iv) / 1.0);
    if (streamer.readInt32(iv)) setParam(kChorusSyncId, iv != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(iv)) setParam(kChorusNoteValueId, static_cast<double>(iv) / (Parameters::kNoteValueDropdownCount - 1));
}

} // namespace Ruinae
