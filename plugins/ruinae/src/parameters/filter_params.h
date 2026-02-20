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
    std::atomic<int> type{0};            // RuinaeFilterType (0-12)
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
    // SVF-specific
    std::atomic<int> svfSlope{1};        // 1=12dB (single), 2=24dB (cascaded)
    std::atomic<float> svfDrive{0.0f};   // 0-24 dB
    std::atomic<float> svfGain{0.0f};    // -24 to +24 dB (Peak/LowShelf/HighShelf)
    // Envelope filter-specific
    std::atomic<int> envSubType{0};      // 0=LP, 1=BP, 2=HP
    std::atomic<float> envSensitivity{0.0f}; // -24 to +24 dB
    std::atomic<float> envDepth{1.0f};   // 0-1
    std::atomic<float> envAttack{10.0f}; // 0.1-500 ms
    std::atomic<float> envRelease{100.0f}; // 1-5000 ms
    std::atomic<int> envDirection{0};    // 0=Up, 1=Down
    // Self-oscillating filter-specific
    std::atomic<float> selfOscGlide{0.0f};   // 0-5000 ms
    std::atomic<float> selfOscExtMix{0.5f};  // 0-1 (0=pure osc, 1=external only)
    std::atomic<float> selfOscShape{0.0f};   // 0-1 saturation
    std::atomic<float> selfOscRelease{500.0f}; // 10-2000 ms
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
        // SVF-specific params
        case kFilterSvfSlopeId:
            // 0-1 -> 1-2 (stepCount=1)
            params.svfSlope.store(
                std::clamp(static_cast<int>(value + 0.5) + 1, 1, 2),
                std::memory_order_relaxed);
            break;
        case kFilterSvfDriveId:
            // 0-1 -> 0-24 dB
            params.svfDrive.store(
                std::clamp(static_cast<float>(value * 24.0), 0.0f, 24.0f),
                std::memory_order_relaxed);
            break;
        case kFilterSvfGainId:
            // 0-1 -> -24 to +24 dB
            params.svfGain.store(
                std::clamp(static_cast<float>(value * 48.0 - 24.0), -24.0f, 24.0f),
                std::memory_order_relaxed);
            break;
        // Envelope filter params
        case kFilterEnvFltSubTypeId:
            // 0-1 -> 0-2 (stepCount=2)
            params.envSubType.store(
                std::clamp(static_cast<int>(value * 2.0 + 0.5), 0, 2),
                std::memory_order_relaxed);
            break;
        case kFilterEnvFltSensitivityId:
            // 0-1 -> -24 to +24 dB
            params.envSensitivity.store(
                std::clamp(static_cast<float>(value * 48.0 - 24.0), -24.0f, 24.0f),
                std::memory_order_relaxed);
            break;
        case kFilterEnvFltDepthId:
            params.envDepth.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        case kFilterEnvFltAttackId: {
            // 0-1 -> 0.1-500 ms (exponential)
            float ms = 0.1f * std::pow(5000.0f, static_cast<float>(value));
            params.envAttack.store(
                std::clamp(ms, 0.1f, 500.0f),
                std::memory_order_relaxed);
            break;
        }
        case kFilterEnvFltReleaseId: {
            // 0-1 -> 1-5000 ms (exponential)
            float ms = 1.0f * std::pow(5000.0f, static_cast<float>(value));
            params.envRelease.store(
                std::clamp(ms, 1.0f, 5000.0f),
                std::memory_order_relaxed);
            break;
        }
        case kFilterEnvFltDirectionId:
            // 0-1 -> 0 or 1 (stepCount=1)
            params.envDirection.store(
                std::clamp(static_cast<int>(value + 0.5), 0, 1),
                std::memory_order_relaxed);
            break;
        // Self-oscillating filter params
        case kFilterSelfOscGlideId:
            // 0-1 -> 0-5000 ms
            params.selfOscGlide.store(
                std::clamp(static_cast<float>(value * 5000.0), 0.0f, 5000.0f),
                std::memory_order_relaxed);
            break;
        case kFilterSelfOscExtMixId:
            params.selfOscExtMix.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        case kFilterSelfOscShapeId:
            params.selfOscShape.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        case kFilterSelfOscReleaseId: {
            // 0-1 -> 10-2000 ms (exponential)
            float ms = 10.0f * std::pow(200.0f, static_cast<float>(value));
            params.selfOscRelease.store(
                std::clamp(ms, 10.0f, 2000.0f),
                std::memory_order_relaxed);
            break;
        }
        default: break;
    }
}

inline void registerFilterParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(createDropdownParameter(
        STR16("Filter Type"), kFilterTypeId,
        {STR16("SVF LP"), STR16("SVF HP"), STR16("SVF BP"), STR16("SVF Notch"),
         STR16("Ladder"), STR16("Formant"), STR16("Comb"),
         STR16("SVF Allpass"), STR16("SVF Peak"), STR16("SVF Lo Shelf"),
         STR16("SVF Hi Shelf"), STR16("Env Filter"), STR16("Self-Osc")}
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
    // SVF-specific
    parameters.addParameter(createDropdownParameter(
        STR16("SVF Slope"), kFilterSvfSlopeId,
        {STR16("12 dB"), STR16("24 dB")}
    ));
    parameters.addParameter(STR16("SVF Drive"), STR16("dB"), 0, 0.0,
        ParameterInfo::kCanAutomate, kFilterSvfDriveId);
    parameters.addParameter(STR16("SVF Gain"), STR16("dB"), 0, 0.5,
        ParameterInfo::kCanAutomate, kFilterSvfGainId);
    // Envelope filter-specific
    parameters.addParameter(createDropdownParameter(
        STR16("Env Filter Type"), kFilterEnvFltSubTypeId,
        {STR16("LP"), STR16("BP"), STR16("HP")}
    ));
    parameters.addParameter(STR16("Env Sensitivity"), STR16("dB"), 0, 0.5,
        ParameterInfo::kCanAutomate, kFilterEnvFltSensitivityId);
    parameters.addParameter(STR16("Env Depth"), STR16(""), 0, 1.0,
        ParameterInfo::kCanAutomate, kFilterEnvFltDepthId);
    parameters.addParameter(STR16("Env Attack"), STR16("ms"), 0, 0.35,
        ParameterInfo::kCanAutomate, kFilterEnvFltAttackId);
    parameters.addParameter(STR16("Env Release"), STR16("ms"), 0, 0.54,
        ParameterInfo::kCanAutomate, kFilterEnvFltReleaseId);
    parameters.addParameter(createDropdownParameter(
        STR16("Env Direction"), kFilterEnvFltDirectionId,
        {STR16("Up"), STR16("Down")}
    ));
    // Self-oscillating filter-specific
    parameters.addParameter(STR16("Self-Osc Glide"), STR16("ms"), 0, 0.0,
        ParameterInfo::kCanAutomate, kFilterSelfOscGlideId);
    parameters.addParameter(STR16("Self-Osc Ext Mix"), STR16(""), 0, 0.5,
        ParameterInfo::kCanAutomate, kFilterSelfOscExtMixId);
    parameters.addParameter(STR16("Self-Osc Shape"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kFilterSelfOscShapeId);
    parameters.addParameter(STR16("Self-Osc Release"), STR16("ms"), 0, 0.47,
        ParameterInfo::kCanAutomate, kFilterSelfOscReleaseId);
    // UI-only: Filter view mode tab (General/Type), ephemeral, not persisted
    auto* viewModeParam = new StringListParameter(
        STR16("Filter View"), kFilterViewModeTag);
    viewModeParam->appendString(STR16("General"));
    viewModeParam->appendString(STR16("Type"));
    parameters.addParameter(viewModeParam);
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
        case kFilterLadderDriveId:
        case kFilterSvfDriveId: {
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
        case kFilterCombDampingId:
        case kFilterEnvFltDepthId:
        case kFilterSelfOscExtMixId:
        case kFilterSelfOscShapeId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kFilterSvfGainId:
        case kFilterEnvFltSensitivityId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%+.1f dB", value * 48.0 - 24.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kFilterEnvFltAttackId: {
            float ms = 0.1f * std::pow(5000.0f, static_cast<float>(value));
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kFilterEnvFltReleaseId: {
            float ms = 1.0f * std::pow(5000.0f, static_cast<float>(value));
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kFilterSelfOscGlideId: {
            float ms = static_cast<float>(value * 5000.0);
            char8 text[32];
            if (ms < 1.0f) snprintf(text, sizeof(text), "Off");
            else snprintf(text, sizeof(text), "%.0f ms", ms);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kFilterSelfOscReleaseId: {
            float ms = 10.0f * std::pow(200.0f, static_cast<float>(value));
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f ms", ms);
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
    // Type-specific
    streamer.writeInt32(params.ladderSlope.load(std::memory_order_relaxed));
    streamer.writeFloat(params.ladderDrive.load(std::memory_order_relaxed));
    streamer.writeFloat(params.formantMorph.load(std::memory_order_relaxed));
    streamer.writeFloat(params.formantGender.load(std::memory_order_relaxed));
    streamer.writeFloat(params.combDamping.load(std::memory_order_relaxed));
    // SVF-specific
    streamer.writeInt32(params.svfSlope.load(std::memory_order_relaxed));
    streamer.writeFloat(params.svfDrive.load(std::memory_order_relaxed));
    streamer.writeFloat(params.svfGain.load(std::memory_order_relaxed));
    streamer.writeInt32(params.envSubType.load(std::memory_order_relaxed));
    streamer.writeFloat(params.envSensitivity.load(std::memory_order_relaxed));
    streamer.writeFloat(params.envDepth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.envAttack.load(std::memory_order_relaxed));
    streamer.writeFloat(params.envRelease.load(std::memory_order_relaxed));
    streamer.writeInt32(params.envDirection.load(std::memory_order_relaxed));
    streamer.writeFloat(params.selfOscGlide.load(std::memory_order_relaxed));
    streamer.writeFloat(params.selfOscExtMix.load(std::memory_order_relaxed));
    streamer.writeFloat(params.selfOscShape.load(std::memory_order_relaxed));
    streamer.writeFloat(params.selfOscRelease.load(std::memory_order_relaxed));
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
    // Type-specific
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
    // SVF-specific
    if (!streamer.readInt32(intVal)) return false;
    params.svfSlope.store(intVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.svfDrive.store(floatVal, std::memory_order_relaxed);
    // SVF gain
    if (!streamer.readFloat(floatVal)) return false;
    params.svfGain.store(floatVal, std::memory_order_relaxed);
    // Envelope filter
    if (!streamer.readInt32(intVal)) return false;
    params.envSubType.store(intVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.envSensitivity.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.envDepth.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.envAttack.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.envRelease.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readInt32(intVal)) return false;
    params.envDirection.store(intVal, std::memory_order_relaxed);
    // Self-oscillating filter
    if (!streamer.readFloat(floatVal)) return false;
    params.selfOscGlide.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.selfOscExtMix.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.selfOscShape.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.selfOscRelease.store(floatVal, std::memory_order_relaxed);
    return true;
}

template<typename SetParamFunc>
inline void loadFilterParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;
    if (streamer.readInt32(intVal))
        setParam(kFilterTypeId, static_cast<double>(intVal) / (kFilterTypeCount - 1));
    if (streamer.readFloat(floatVal)) {
        double norm = (floatVal > 20.0f) ? std::log(floatVal / 20.0f) / std::log(1000.0f) : 0.0;
        setParam(kFilterCutoffId, std::clamp(norm, 0.0, 1.0));
    }
    if (streamer.readFloat(floatVal))
        setParam(kFilterResonanceId, static_cast<double>((floatVal - 0.1f) / 29.9f));
    if (streamer.readFloat(floatVal))
        setParam(kFilterEnvAmountId, static_cast<double>((floatVal + 48.0f) / 96.0f));
    if (streamer.readFloat(floatVal))
        setParam(kFilterKeyTrackId, static_cast<double>(floatVal));
    // Type-specific
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
    // SVF-specific
    if (streamer.readInt32(intVal))
        setParam(kFilterSvfSlopeId, static_cast<double>(intVal - 1));
    if (streamer.readFloat(floatVal))
        setParam(kFilterSvfDriveId, static_cast<double>(floatVal / 24.0f));
    // SVF gain
    if (streamer.readFloat(floatVal))
        setParam(kFilterSvfGainId, static_cast<double>((floatVal + 24.0f) / 48.0f));
    // Envelope filter
    if (streamer.readInt32(intVal))
        setParam(kFilterEnvFltSubTypeId, static_cast<double>(intVal) / 2.0);
    if (streamer.readFloat(floatVal))
        setParam(kFilterEnvFltSensitivityId, static_cast<double>((floatVal + 24.0f) / 48.0f));
    if (streamer.readFloat(floatVal))
        setParam(kFilterEnvFltDepthId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal)) {
        double norm = (floatVal > 0.1f) ? std::log(floatVal / 0.1) / std::log(5000.0) : 0.0;
        setParam(kFilterEnvFltAttackId, std::clamp(norm, 0.0, 1.0));
    }
    if (streamer.readFloat(floatVal)) {
        double norm = (floatVal > 1.0f) ? std::log(static_cast<double>(floatVal)) / std::log(5000.0) : 0.0;
        setParam(kFilterEnvFltReleaseId, std::clamp(norm, 0.0, 1.0));
    }
    if (streamer.readInt32(intVal))
        setParam(kFilterEnvFltDirectionId, static_cast<double>(intVal));
    // Self-oscillating filter
    if (streamer.readFloat(floatVal))
        setParam(kFilterSelfOscGlideId, static_cast<double>(floatVal / 5000.0f));
    if (streamer.readFloat(floatVal))
        setParam(kFilterSelfOscExtMixId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam(kFilterSelfOscShapeId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal)) {
        double norm = (floatVal > 10.0f) ? std::log(floatVal / 10.0) / std::log(200.0) : 0.0;
        setParam(kFilterSelfOscReleaseId, std::clamp(norm, 0.0, 1.0));
    }
}

} // namespace Ruinae
