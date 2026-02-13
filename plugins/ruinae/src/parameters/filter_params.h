#pragma once

// ==============================================================================
// Filter Parameters (ID 400-499)
// ==============================================================================

#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/dropdown_mappings.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace Ruinae {

struct RuinaeFilterParams {
    std::atomic<int> type{0};            // RuinaeFilterType (0-6)
    std::atomic<float> cutoffHz{20000.0f}; // 20-20000 Hz (exponential)
    std::atomic<float> resonance{0.1f};  // 0.1-30.0
    std::atomic<float> envAmount{0.0f};  // -48 to +48 semitones
    std::atomic<float> keyTrack{0.0f};   // 0-1
    // Type-specific params
    std::atomic<int> ladderSlope{4};     // 1-4 poles
    std::atomic<float> ladderDrive{0.0f}; // 0-24 dB
    std::atomic<float> formantMorph{0.0f}; // 0-4 (A=0, E=1, I=2, O=3, U=4)
    std::atomic<float> formantGender{0.0f}; // -1 to +1
    std::atomic<float> combDamping{0.0f}; // 0-1
};

inline void handleFilterParamChange(
    RuinaeFilterParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kFilterTypeId:
            params.type.store(
                std::clamp(static_cast<int>(value * (kFilterTypeCount - 1) + 0.5), 0, kFilterTypeCount - 1),
                std::memory_order_relaxed);
            break;
        case kFilterCutoffId: {
            // Exponential mapping: 0->20Hz, 1->20000Hz
            float hz = 20.0f * std::pow(1000.0f, static_cast<float>(value));
            params.cutoffHz.store(
                std::clamp(hz, 20.0f, 20000.0f),
                std::memory_order_relaxed);
            break;
        }
        case kFilterResonanceId:
            // 0-1 -> 0.1-30.0
            params.resonance.store(
                std::clamp(static_cast<float>(0.1 + value * 29.9), 0.1f, 30.0f),
                std::memory_order_relaxed);
            break;
        case kFilterEnvAmountId:
            // 0-1 -> -48 to +48 semitones
            params.envAmount.store(
                std::clamp(static_cast<float>(value * 96.0 - 48.0), -48.0f, 48.0f),
                std::memory_order_relaxed);
            break;
        case kFilterKeyTrackId:
            params.keyTrack.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        // Type-specific params
        case kFilterLadderSlopeId:
            // 0-1 -> 1-4 poles (stepCount=3)
            params.ladderSlope.store(
                std::clamp(static_cast<int>(value * 3.0 + 0.5) + 1, 1, 4),
                std::memory_order_relaxed);
            break;
        case kFilterLadderDriveId:
            // 0-1 -> 0-24 dB
            params.ladderDrive.store(
                std::clamp(static_cast<float>(value * 24.0), 0.0f, 24.0f),
                std::memory_order_relaxed);
            break;
        case kFilterFormantMorphId:
            // 0-1 -> 0-4
            params.formantMorph.store(
                std::clamp(static_cast<float>(value * 4.0), 0.0f, 4.0f),
                std::memory_order_relaxed);
            break;
        case kFilterFormantGenderId:
            // 0-1 -> -1 to +1
            params.formantGender.store(
                std::clamp(static_cast<float>(value * 2.0 - 1.0), -1.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        case kFilterCombDampingId:
            params.combDamping.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        default: break;
    }
}

inline void registerFilterParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(createDropdownParameter(
        STR16("Filter Type"), kFilterTypeId,
        {STR16("SVF LP"), STR16("SVF HP"), STR16("SVF BP"), STR16("SVF Notch"),
         STR16("Ladder"), STR16("Formant"), STR16("Comb")}
    ));
    parameters.addParameter(STR16("Filter Cutoff"), STR16("Hz"), 0, 1.0,
        ParameterInfo::kCanAutomate, kFilterCutoffId);
    parameters.addParameter(STR16("Filter Resonance"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kFilterResonanceId);
    parameters.addParameter(STR16("Filter Env Amount"), STR16("st"), 0, 0.5,
        ParameterInfo::kCanAutomate, kFilterEnvAmountId);
    parameters.addParameter(STR16("Filter Key Track"), STR16("%"), 0, 0.0,
        ParameterInfo::kCanAutomate, kFilterKeyTrackId);
    // Ladder-specific
    parameters.addParameter(createDropdownParameter(
        STR16("Ladder Slope"), kFilterLadderSlopeId,
        {STR16("6 dB"), STR16("12 dB"), STR16("18 dB"), STR16("24 dB")}
    ));
    parameters.addParameter(STR16("Ladder Drive"), STR16("dB"), 0, 0.0,
        ParameterInfo::kCanAutomate, kFilterLadderDriveId);
    // Formant-specific
    parameters.addParameter(STR16("Formant Vowel"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kFilterFormantMorphId);
    parameters.addParameter(STR16("Formant Gender"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kFilterFormantGenderId);
    // Comb-specific
    parameters.addParameter(STR16("Comb Damping"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kFilterCombDampingId);
}

inline Steinberg::tresult formatFilterParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kFilterCutoffId: {
            float hz = 20.0f * std::pow(1000.0f, static_cast<float>(value));
            char8 text[32];
            if (hz >= 1000.0f) snprintf(text, sizeof(text), "%.1f kHz", hz / 1000.0f);
            else snprintf(text, sizeof(text), "%.1f Hz", hz);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kFilterResonanceId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f", 0.1 + value * 29.9);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kFilterEnvAmountId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%+.0f st", value * 96.0 - 48.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kFilterKeyTrackId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kFilterLadderDriveId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f dB", value * 24.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kFilterFormantMorphId: {
            char8 text[32];
            float morph = static_cast<float>(value * 4.0);
            static const char* vowels[] = {"A", "E", "I", "O", "U"};
            int idx = std::clamp(static_cast<int>(morph + 0.5f), 0, 4);
            float frac = morph - static_cast<float>(static_cast<int>(morph));
            if (frac < 0.05f || frac > 0.95f || morph >= 3.95f)
                snprintf(text, sizeof(text), "%s", vowels[idx]);
            else {
                int lo = std::clamp(static_cast<int>(morph), 0, 3);
                snprintf(text, sizeof(text), "%s>%s", vowels[lo], vowels[lo + 1]);
            }
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kFilterFormantGenderId: {
            char8 text[32];
            float g = static_cast<float>(value * 2.0 - 1.0);
            snprintf(text, sizeof(text), "%+.0f%%", g * 100.0f);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kFilterCombDampingId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    return kResultFalse;
}

inline void saveFilterParams(const RuinaeFilterParams& params, Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.type.load(std::memory_order_relaxed));
    streamer.writeFloat(params.cutoffHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.resonance.load(std::memory_order_relaxed));
    streamer.writeFloat(params.envAmount.load(std::memory_order_relaxed));
    streamer.writeFloat(params.keyTrack.load(std::memory_order_relaxed));
    // Type-specific (v4+)
    streamer.writeInt32(params.ladderSlope.load(std::memory_order_relaxed));
    streamer.writeFloat(params.ladderDrive.load(std::memory_order_relaxed));
    streamer.writeFloat(params.formantMorph.load(std::memory_order_relaxed));
    streamer.writeFloat(params.formantGender.load(std::memory_order_relaxed));
    streamer.writeFloat(params.combDamping.load(std::memory_order_relaxed));
}

inline bool loadFilterParams(RuinaeFilterParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;
    if (!streamer.readInt32(intVal)) return false;
    params.type.store(intVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.cutoffHz.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.resonance.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.envAmount.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.keyTrack.store(floatVal, std::memory_order_relaxed);
    return true;
}

inline bool loadFilterParamsV4(RuinaeFilterParams& params, Steinberg::IBStreamer& streamer) {
    if (!loadFilterParams(params, streamer)) return false;
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;
    if (!streamer.readInt32(intVal)) return false;
    params.ladderSlope.store(intVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.ladderDrive.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.formantMorph.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.formantGender.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.combDamping.store(floatVal, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadFilterParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;
    if (streamer.readInt32(intVal))
        setParam(kFilterTypeId, static_cast<double>(intVal) / (kFilterTypeCount - 1));
    if (streamer.readFloat(floatVal)) {
        // Inverse of exponential: normalized = log(hz/20) / log(1000)
        double norm = (floatVal > 20.0f) ? std::log(floatVal / 20.0f) / std::log(1000.0f) : 0.0;
        setParam(kFilterCutoffId, std::clamp(norm, 0.0, 1.0));
    }
    if (streamer.readFloat(floatVal))
        setParam(kFilterResonanceId, static_cast<double>((floatVal - 0.1f) / 29.9f));
    if (streamer.readFloat(floatVal))
        setParam(kFilterEnvAmountId, static_cast<double>((floatVal + 48.0f) / 96.0f));
    if (streamer.readFloat(floatVal))
        setParam(kFilterKeyTrackId, static_cast<double>(floatVal));
}

template<typename SetParamFunc>
inline void loadFilterParamsToControllerV4(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    loadFilterParamsToController(streamer, setParam);
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;
    if (streamer.readInt32(intVal))
        setParam(kFilterLadderSlopeId, static_cast<double>(intVal - 1) / 3.0);
    if (streamer.readFloat(floatVal))
        setParam(kFilterLadderDriveId, static_cast<double>(floatVal / 24.0f));
    if (streamer.readFloat(floatVal))
        setParam(kFilterFormantMorphId, static_cast<double>(floatVal / 4.0f));
    if (streamer.readFloat(floatVal))
        setParam(kFilterFormantGenderId, static_cast<double>((floatVal + 1.0f) / 2.0f));
    if (streamer.readFloat(floatVal))
        setParam(kFilterCombDampingId, static_cast<double>(floatVal));
}

} // namespace Ruinae
