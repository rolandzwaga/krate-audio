#pragma once

// ==============================================================================
// Global Parameters (ID 0-99)
// ==============================================================================

#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace Ruinae {

// ==============================================================================
// Parameter Storage
// ==============================================================================

struct GlobalParams {
    std::atomic<float> masterGain{1.0f};    // 0-2 (linear gain)
    std::atomic<int> voiceMode{0};          // 0=Poly, 1=Mono
    std::atomic<int> polyphony{8};          // 1-16
    std::atomic<bool> softLimit{true};      // on/off
    std::atomic<float> width{1.0f};         // 0-2 (stereo width: 0=mono, 1=natural, 2=extra-wide)
    std::atomic<float> spread{0.0f};        // 0-1 (voice spread: 0=center, 1=full)
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================

inline void handleGlobalParamChange(
    GlobalParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

    switch (id) {
        case kMasterGainId:
            // 0-1 normalized -> 0-2 linear gain
            params.masterGain.store(
                std::clamp(static_cast<float>(value * 2.0), 0.0f, 2.0f),
                std::memory_order_relaxed);
            break;
        case kVoiceModeId:
            // 0-1 normalized -> 0 or 1
            params.voiceMode.store(
                static_cast<int>(value + 0.5),
                std::memory_order_relaxed);
            break;
        case kPolyphonyId:
            // 0-1 normalized -> 1-16
            params.polyphony.store(
                std::clamp(static_cast<int>(value * 15.0 + 1.0 + 0.5), 1, 16),
                std::memory_order_relaxed);
            break;
        case kSoftLimitId:
            params.softLimit.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kWidthId:
            // 0-1 normalized -> 0-2 stereo width
            params.width.store(
                std::clamp(static_cast<float>(value * 2.0), 0.0f, 2.0f),
                std::memory_order_relaxed);
            break;
        case kSpreadId:
            // 0-1 normalized -> 0-1 spread (1:1 mapping)
            params.spread.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

// ==============================================================================
// Parameter Registration
// ==============================================================================

inline void registerGlobalParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;

    // Master Gain (0-200%)
    parameters.addParameter(
        STR16("Master Gain"), STR16("dB"), 0, 0.5,
        ParameterInfo::kCanAutomate, kMasterGainId);

    // Voice Mode
    parameters.addParameter(createDropdownParameter(
        STR16("Voice Mode"), kVoiceModeId,
        {STR16("Polyphonic"), STR16("Mono")}
    ));

    // Polyphony (1-16, default 8 => index 7)
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Polyphony"), kPolyphonyId, 7,
        {STR16("1"), STR16("2"), STR16("3"), STR16("4"),
         STR16("5"), STR16("6"), STR16("7"), STR16("8"),
         STR16("9"), STR16("10"), STR16("11"), STR16("12"),
         STR16("13"), STR16("14"), STR16("15"), STR16("16")}
    ));

    // Soft Limit (on/off, default on)
    parameters.addParameter(
        STR16("Soft Limit"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kSoftLimitId);

    // Width (0-200%, default 100% = normalized 0.5)
    parameters.addParameter(
        STR16("Width"), STR16("%"), 0, 0.5,
        ParameterInfo::kCanAutomate, kWidthId);

    // Spread (0-100%, default 0%)
    parameters.addParameter(
        STR16("Spread"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kSpreadId);
}

// ==============================================================================
// Display Formatting
// ==============================================================================

inline Steinberg::tresult formatGlobalParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    switch (id) {
        case kMasterGainId: {
            float gain = static_cast<float>(value * 2.0);
            float dB = (gain > 0.0001f) ? 20.0f * std::log10(gain) : -80.0f;
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f dB", dB);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kPolyphonyId: {
            int voices = std::clamp(static_cast<int>(value * 15.0 + 1.0 + 0.5), 1, 16);
            char8 text[32];
            snprintf(text, sizeof(text), "%d", voices);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kWidthId: {
            int pct = static_cast<int>(value * 200.0 + 0.5);
            char8 text[32];
            snprintf(text, sizeof(text), "%d%%", pct);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kSpreadId: {
            int pct = static_cast<int>(value * 100.0 + 0.5);
            char8 text[32];
            snprintf(text, sizeof(text), "%d%%", pct);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        // VoiceMode and SoftLimit handled by StringListParameter/default
        default:
            break;
    }
    return Steinberg::kResultFalse;
}

// ==============================================================================
// State Persistence
// ==============================================================================

inline void saveGlobalParams(const GlobalParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeFloat(params.masterGain.load(std::memory_order_relaxed));
    streamer.writeInt32(params.voiceMode.load(std::memory_order_relaxed));
    streamer.writeInt32(params.polyphony.load(std::memory_order_relaxed));
    streamer.writeInt32(params.softLimit.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeFloat(params.width.load(std::memory_order_relaxed));
    streamer.writeFloat(params.spread.load(std::memory_order_relaxed));
}

inline bool loadGlobalParams(GlobalParams& params, Steinberg::IBStreamer& streamer) {
    float floatVal = 1.0f;
    Steinberg::int32 intVal = 0;

    if (!streamer.readFloat(floatVal)) return false;
    params.masterGain.store(floatVal, std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    params.voiceMode.store(intVal, std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    params.polyphony.store(intVal, std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    params.softLimit.store(intVal != 0, std::memory_order_relaxed);

    // Width (new - EOF-safe for old presets)
    if (streamer.readFloat(floatVal))
        params.width.store(floatVal, std::memory_order_relaxed);
    // else: keep default 1.0f (natural stereo width)

    // Spread (new - EOF-safe for old presets)
    if (streamer.readFloat(floatVal))
        params.spread.store(floatVal, std::memory_order_relaxed);
    // else: keep default 0.0f (all voices centered)

    return true;
}

// ==============================================================================
// Controller State Sync
// ==============================================================================

template<typename SetParamFunc>
inline void loadGlobalParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {

    float floatVal = 0.0f;
    Steinberg::int32 intVal = 0;

    if (streamer.readFloat(floatVal))
        setParam(kMasterGainId, static_cast<double>(floatVal / 2.0f));
    if (streamer.readInt32(intVal))
        setParam(kVoiceModeId, static_cast<double>(intVal));
    if (streamer.readInt32(intVal))
        setParam(kPolyphonyId, (static_cast<double>(intVal) - 1.0) / 15.0);
    if (streamer.readInt32(intVal))
        setParam(kSoftLimitId, intVal != 0 ? 1.0 : 0.0);

    // Width: engine value (0-2) -> normalized (0-1)
    if (streamer.readFloat(floatVal))
        setParam(kWidthId, static_cast<double>(floatVal / 2.0f));

    // Spread: stored value (0-1) = normalized (0-1)
    if (streamer.readFloat(floatVal))
        setParam(kSpreadId, static_cast<double>(floatVal));
}

} // namespace Ruinae
