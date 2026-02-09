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
#include <array>
#include <atomic>
#include <cstdio>

namespace Ruinae {

// State version for trance gate serialization
inline constexpr Steinberg::int32 kTranceGateStateVersion = 2;

struct RuinaeTranceGateParams {
    std::atomic<bool> enabled{false};
    std::atomic<int> numSteps{16};         // 2-32 (actual step count, not index)
    std::atomic<float> rateHz{4.0f};       // 0.1-100 Hz
    std::atomic<float> depth{1.0f};        // 0-1
    std::atomic<float> attackMs{2.0f};     // 1-20 ms
    std::atomic<float> releaseMs{10.0f};   // 1-50 ms
    std::atomic<bool> tempoSync{true};
    std::atomic<int> noteValue{Parameters::kNoteValueDefaultIndex};

    // Step levels (32 steps, default 1.0)
    std::array<std::atomic<float>, 32> stepLevels{};

    // Euclidean mode
    std::atomic<bool> euclideanEnabled{false};
    std::atomic<int> euclideanHits{4};
    std::atomic<int> euclideanRotation{0};

    // Phase offset
    std::atomic<float> phaseOffset{0.0f};

    RuinaeTranceGateParams() {
        for (auto& level : stepLevels) {
            level.store(1.0f, std::memory_order_relaxed);
        }
    }
};

inline void handleTranceGateParamChange(
    RuinaeTranceGateParams& params, Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {
    switch (id) {
        case kTranceGateEnabledId:
            params.enabled.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kTranceGateNumStepsId:
            // RangeParameter: 0-1 -> 2-32 (stepCount=30)
            params.numSteps.store(
                std::clamp(static_cast<int>(2.0 + std::round(value * 30.0)), 2, 32),
                std::memory_order_relaxed);
            break;
        case kTranceGateRateId:
            // 0-1 -> 0.1-100 Hz
            params.rateHz.store(
                std::clamp(static_cast<float>(0.1 + value * 99.9), 0.1f, 100.0f),
                std::memory_order_relaxed);
            break;
        case kTranceGateDepthId:
            params.depth.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), std::memory_order_relaxed);
            break;
        case kTranceGateAttackId:
            // 0-1 -> 1-20 ms
            params.attackMs.store(
                std::clamp(static_cast<float>(1.0 + value * 19.0), 1.0f, 20.0f),
                std::memory_order_relaxed);
            break;
        case kTranceGateReleaseId:
            // 0-1 -> 1-50 ms
            params.releaseMs.store(
                std::clamp(static_cast<float>(1.0 + value * 49.0), 1.0f, 50.0f),
                std::memory_order_relaxed);
            break;
        case kTranceGateTempoSyncId:
            params.tempoSync.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kTranceGateNoteValueId:
            params.noteValue.store(
                std::clamp(static_cast<int>(value * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                    0, Parameters::kNoteValueDropdownCount - 1),
                std::memory_order_relaxed);
            break;
        case kTranceGateEuclideanEnabledId:
            params.euclideanEnabled.store(value >= 0.5, std::memory_order_relaxed);
            break;
        case kTranceGateEuclideanHitsId:
            // RangeParameter: 0-1 -> 0-32 (stepCount=32)
            params.euclideanHits.store(
                std::clamp(static_cast<int>(std::round(value * 32.0)), 0, 32),
                std::memory_order_relaxed);
            break;
        case kTranceGateEuclideanRotationId:
            // RangeParameter: 0-1 -> 0-31 (stepCount=31)
            params.euclideanRotation.store(
                std::clamp(static_cast<int>(std::round(value * 31.0)), 0, 31),
                std::memory_order_relaxed);
            break;
        case kTranceGatePhaseOffsetId:
            params.phaseOffset.store(
                std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                std::memory_order_relaxed);
            break;
        default: {
            // Check if it's a step level parameter (668-699)
            if (id >= kTranceGateStepLevel0Id && id <= kTranceGateStepLevel31Id) {
                int stepIndex = static_cast<int>(id - kTranceGateStepLevel0Id);
                params.stepLevels[static_cast<size_t>(stepIndex)].store(
                    std::clamp(static_cast<float>(value), 0.0f, 1.0f),
                    std::memory_order_relaxed);
            }
            break;
        }
    }
}

inline void registerTranceGateParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg::Vst;
    parameters.addParameter(STR16("Trance Gate"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kTranceGateEnabledId);

    // NumSteps: RangeParameter 2-32, default 16, stepCount 30
    parameters.addParameter(
        new RangeParameter(STR16("Gate Steps"), kTranceGateNumStepsId,
                          STR16(""), 2, 32, 16, 30,
                          ParameterInfo::kCanAutomate));

    parameters.addParameter(STR16("Gate Rate"), STR16("Hz"), 0, 0.039,
        ParameterInfo::kCanAutomate, kTranceGateRateId);
    parameters.addParameter(STR16("Gate Depth"), STR16("%"), 0, 1.0,
        ParameterInfo::kCanAutomate, kTranceGateDepthId);
    parameters.addParameter(STR16("Gate Attack"), STR16("ms"), 0, 0.053,
        ParameterInfo::kCanAutomate, kTranceGateAttackId);
    parameters.addParameter(STR16("Gate Release"), STR16("ms"), 0, 0.184,
        ParameterInfo::kCanAutomate, kTranceGateReleaseId);
    parameters.addParameter(STR16("Gate Tempo Sync"), STR16(""), 1, 1.0,
        ParameterInfo::kCanAutomate, kTranceGateTempoSyncId);
    parameters.addParameter(createNoteValueDropdown(
        STR16("Gate Note Value"), kTranceGateNoteValueId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));

    // Euclidean parameters
    parameters.addParameter(STR16("Gate Euclidean"), STR16(""), 1, 0.0,
        ParameterInfo::kCanAutomate, kTranceGateEuclideanEnabledId);
    parameters.addParameter(
        new RangeParameter(STR16("Gate Euclidean Hits"), kTranceGateEuclideanHitsId,
                          STR16(""), 0, 32, 4, 32,
                          ParameterInfo::kCanAutomate));
    parameters.addParameter(
        new RangeParameter(STR16("Gate Euclidean Rotation"), kTranceGateEuclideanRotationId,
                          STR16(""), 0, 31, 0, 31,
                          ParameterInfo::kCanAutomate));

    // Phase offset
    parameters.addParameter(STR16("Gate Phase Offset"), STR16(""), 0, 0.0,
        ParameterInfo::kCanAutomate, kTranceGatePhaseOffsetId);

    // 32 step level parameters (hidden from generic UI)
    for (int i = 0; i < 32; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "Gate Step %d", i + 1);
        Steinberg::Vst::String128 name16;
        Steinberg::UString(name16, 128).fromAscii(name);
        parameters.addParameter(
            new RangeParameter(name16,
                static_cast<ParamID>(kTranceGateStepLevel0Id + i),
                STR16(""), 0.0, 1.0, 1.0, 0,
                ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden));
    }
}

inline Steinberg::tresult formatTranceGateParam(
    Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    using namespace Steinberg;
    switch (id) {
        case kTranceGateNumStepsId: {
            char8 text[32];
            int steps = std::clamp(static_cast<int>(2.0 + std::round(value * 30.0)), 2, 32);
            snprintf(text, sizeof(text), "%d", steps);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kTranceGateRateId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f Hz", 0.1 + value * 99.9);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kTranceGateDepthId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kTranceGateAttackId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f ms", 1.0 + value * 19.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kTranceGateReleaseId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f ms", 1.0 + value * 49.0);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kTranceGateEuclideanHitsId: {
            char8 text[32];
            int hits = std::clamp(static_cast<int>(std::round(value * 32.0)), 0, 32);
            snprintf(text, sizeof(text), "%d", hits);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kTranceGateEuclideanRotationId: {
            char8 text[32];
            int rot = std::clamp(static_cast<int>(std::round(value * 31.0)), 0, 31);
            snprintf(text, sizeof(text), "%d", rot);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        case kTranceGatePhaseOffsetId: {
            char8 text[32];
            snprintf(text, sizeof(text), "%.2f", value);
            UString(string, 128).fromAscii(text);
            return kResultOk;
        }
        default: break;
    }
    // Step level params: show as percentage
    if (id >= kTranceGateStepLevel0Id && id <= kTranceGateStepLevel31Id) {
        char8 text[32];
        snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
        Steinberg::UString(string, 128).fromAscii(text);
        return Steinberg::kResultOk;
    }
    return kResultFalse;
}

inline void saveTranceGateParams(const RuinaeTranceGateParams& params, Steinberg::IBStreamer& streamer) {
    // v1 fields (unchanged format for backward compat reading)
    streamer.writeInt32(params.enabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.numSteps.load(std::memory_order_relaxed));
    streamer.writeFloat(params.rateHz.load(std::memory_order_relaxed));
    streamer.writeFloat(params.depth.load(std::memory_order_relaxed));
    streamer.writeFloat(params.attackMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.releaseMs.load(std::memory_order_relaxed));
    streamer.writeInt32(params.tempoSync.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.noteValue.load(std::memory_order_relaxed));

    // v2 marker and new fields
    streamer.writeInt32(kTranceGateStateVersion);
    streamer.writeInt32(params.euclideanEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    streamer.writeInt32(params.euclideanHits.load(std::memory_order_relaxed));
    streamer.writeInt32(params.euclideanRotation.load(std::memory_order_relaxed));
    streamer.writeFloat(params.phaseOffset.load(std::memory_order_relaxed));

    // All 32 step levels
    for (int i = 0; i < 32; ++i) {
        streamer.writeFloat(params.stepLevels[static_cast<size_t>(i)].load(std::memory_order_relaxed));
    }
}

inline bool loadTranceGateParams(RuinaeTranceGateParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;

    // Read v1 fields
    if (!streamer.readInt32(intVal)) return false;
    params.enabled.store(intVal != 0, std::memory_order_relaxed);

    if (!streamer.readInt32(intVal)) return false;
    // This field is now the actual step count (2-32) in v2 saves,
    // or a dropdown index (0/1/2) in v1 saves. We detect by reading the
    // version marker after the base fields.
    int numStepsRaw = intVal;

    if (!streamer.readFloat(floatVal)) return false;
    params.rateHz.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.depth.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.attackMs.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readFloat(floatVal)) return false;
    params.releaseMs.store(floatVal, std::memory_order_relaxed);
    if (!streamer.readInt32(intVal)) return false;
    params.tempoSync.store(intVal != 0, std::memory_order_relaxed);
    if (!streamer.readInt32(intVal)) return false;
    params.noteValue.store(intVal, std::memory_order_relaxed);

    // Try to read v2 version marker
    Steinberg::int32 versionMarker = 0;
    if (streamer.readInt32(versionMarker) && versionMarker == kTranceGateStateVersion) {
        // v2 format: numStepsRaw is actual step count
        params.numSteps.store(std::clamp(numStepsRaw, 2, 32), std::memory_order_relaxed);

        // Read Euclidean params
        if (streamer.readInt32(intVal))
            params.euclideanEnabled.store(intVal != 0, std::memory_order_relaxed);
        if (streamer.readInt32(intVal))
            params.euclideanHits.store(std::clamp(intVal, 0, 32), std::memory_order_relaxed);
        if (streamer.readInt32(intVal))
            params.euclideanRotation.store(std::clamp(intVal, 0, 31), std::memory_order_relaxed);
        if (streamer.readFloat(floatVal))
            params.phaseOffset.store(std::clamp(floatVal, 0.0f, 1.0f), std::memory_order_relaxed);

        // Read 32 step levels
        for (int i = 0; i < 32; ++i) {
            if (streamer.readFloat(floatVal)) {
                params.stepLevels[static_cast<size_t>(i)].store(
                    std::clamp(floatVal, 0.0f, 1.0f), std::memory_order_relaxed);
            }
        }
    } else {
        // v1 format: numStepsRaw is a dropdown index (0=8, 1=16, 2=32)
        params.numSteps.store(numStepsFromIndex(numStepsRaw), std::memory_order_relaxed);

        // Initialize v2 fields to defaults
        params.euclideanEnabled.store(false, std::memory_order_relaxed);
        params.euclideanHits.store(4, std::memory_order_relaxed);
        params.euclideanRotation.store(0, std::memory_order_relaxed);
        params.phaseOffset.store(0.0f, std::memory_order_relaxed);
        for (auto& level : params.stepLevels) {
            level.store(1.0f, std::memory_order_relaxed);
        }
    }

    return true;
}

template<typename SetParamFunc>
inline void loadTranceGateParamsToController(
    Steinberg::IBStreamer& streamer, SetParamFunc setParam) {
    Steinberg::int32 intVal = 0; float floatVal = 0.0f;

    if (streamer.readInt32(intVal))
        setParam(kTranceGateEnabledId, intVal != 0 ? 1.0 : 0.0);

    int numStepsRaw = 0;
    if (streamer.readInt32(numStepsRaw)) {
        // Will be resolved after checking version marker
    }

    if (streamer.readFloat(floatVal))
        setParam(kTranceGateRateId, static_cast<double>((floatVal - 0.1f) / 99.9f));
    if (streamer.readFloat(floatVal))
        setParam(kTranceGateDepthId, static_cast<double>(floatVal));
    if (streamer.readFloat(floatVal))
        setParam(kTranceGateAttackId, static_cast<double>((floatVal - 1.0f) / 19.0f));
    if (streamer.readFloat(floatVal))
        setParam(kTranceGateReleaseId, static_cast<double>((floatVal - 1.0f) / 49.0f));
    if (streamer.readInt32(intVal))
        setParam(kTranceGateTempoSyncId, intVal != 0 ? 1.0 : 0.0);
    if (streamer.readInt32(intVal))
        setParam(kTranceGateNoteValueId, static_cast<double>(intVal) / (Parameters::kNoteValueDropdownCount - 1));

    // Try to read v2 version marker
    Steinberg::int32 versionMarker = 0;
    if (streamer.readInt32(versionMarker) && versionMarker == kTranceGateStateVersion) {
        // v2: numStepsRaw is actual step count
        int steps = std::clamp(numStepsRaw, 2, 32);
        setParam(kTranceGateNumStepsId, static_cast<double>(steps - 2) / 30.0);

        if (streamer.readInt32(intVal))
            setParam(kTranceGateEuclideanEnabledId, intVal != 0 ? 1.0 : 0.0);
        if (streamer.readInt32(intVal))
            setParam(kTranceGateEuclideanHitsId, static_cast<double>(intVal) / 32.0);
        if (streamer.readInt32(intVal))
            setParam(kTranceGateEuclideanRotationId, static_cast<double>(intVal) / 31.0);
        if (streamer.readFloat(floatVal))
            setParam(kTranceGatePhaseOffsetId, static_cast<double>(floatVal));

        for (int i = 0; i < 32; ++i) {
            if (streamer.readFloat(floatVal))
                setParam(static_cast<Steinberg::Vst::ParamID>(kTranceGateStepLevel0Id + i),
                         static_cast<double>(floatVal));
        }
    } else {
        // v1: numStepsRaw is dropdown index
        int steps = numStepsFromIndex(numStepsRaw);
        setParam(kTranceGateNumStepsId, static_cast<double>(steps - 2) / 30.0);
        // v2 fields keep their defaults (no params to set)
    }
}

} // namespace Ruinae
