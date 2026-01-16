#pragma once

// ==============================================================================
// Pattern Freeze Mode Parameters Extension
// ==============================================================================
// Parameter pack extension for Pattern Freeze (spec 069)
// Extends FreezeParams with rhythmic pattern-based freeze parameters.
// ID Range: 1015-1062
// ==============================================================================

#include "plugin_ids.h"
#include "controller/parameter_helpers.h"
#include "parameters/note_value_ui.h"
#include "pluginterfaces/base/ftypes.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fstreamer.h"

#include <krate/dsp/core/pattern_freeze_types.h>

#include <atomic>
#include <cmath>

namespace Iterum {

// ==============================================================================
// Pattern Freeze Parameter Storage
// ==============================================================================

struct PatternFreezeParams {
    // Pattern Type & Core
    std::atomic<int> patternType{static_cast<int>(Krate::DSP::kDefaultPatternType)};
    std::atomic<float> sliceLengthMs{Krate::DSP::PatternFreezeConstants::kDefaultSliceLengthMs};
    std::atomic<int> sliceMode{static_cast<int>(Krate::DSP::kDefaultSliceMode)};

    // Euclidean Parameters
    std::atomic<int> euclideanSteps{Krate::DSP::PatternFreezeConstants::kDefaultEuclideanSteps};
    std::atomic<int> euclideanHits{Krate::DSP::PatternFreezeConstants::kDefaultEuclideanHits};
    std::atomic<int> euclideanRotation{Krate::DSP::PatternFreezeConstants::kDefaultEuclideanRotation};
    std::atomic<int> patternRate{Parameters::kNoteValueDefaultIndex};

    // Granular Scatter Parameters
    std::atomic<float> granularDensity{Krate::DSP::PatternFreezeConstants::kDefaultGranularDensity};
    std::atomic<float> granularPositionJitter{Krate::DSP::PatternFreezeConstants::kDefaultGranularPositionJitter};
    std::atomic<float> granularSizeJitter{Krate::DSP::PatternFreezeConstants::kDefaultGranularSizeJitter};
    std::atomic<float> granularGrainSize{Krate::DSP::PatternFreezeConstants::kDefaultGranularGrainSize};

    // Harmonic Drones Parameters
    std::atomic<int> droneVoiceCount{Krate::DSP::PatternFreezeConstants::kDefaultDroneVoiceCount};
    std::atomic<int> droneInterval{static_cast<int>(Krate::DSP::kDefaultPitchInterval)};
    std::atomic<float> droneDrift{Krate::DSP::PatternFreezeConstants::kDefaultDroneDrift};
    std::atomic<float> droneDriftRate{Krate::DSP::PatternFreezeConstants::kDefaultDroneDriftRate};

    // Noise Bursts Parameters
    std::atomic<int> noiseColor{static_cast<int>(Krate::DSP::kDefaultNoiseColor)};
    std::atomic<int> noiseBurstRate{Parameters::kNoteValueDefaultIndex};
    std::atomic<int> noiseFilterType{0};  // 0=LowPass
    std::atomic<float> noiseFilterCutoff{Krate::DSP::PatternFreezeConstants::kDefaultNoiseFilterCutoff};
    std::atomic<float> noiseFilterSweep{Krate::DSP::PatternFreezeConstants::kDefaultNoiseFilterSweep};

    // Envelope Parameters
    std::atomic<float> envelopeAttackMs{Krate::DSP::PatternFreezeConstants::kDefaultEnvelopeAttackMs};
    std::atomic<float> envelopeReleaseMs{Krate::DSP::PatternFreezeConstants::kDefaultEnvelopeReleaseMs};
    std::atomic<int> envelopeShape{static_cast<int>(Krate::DSP::kDefaultEnvelopeShape)};
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================

inline void handlePatternFreezeParamChange(
    PatternFreezeParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue) {

    using namespace Krate::DSP;
    using namespace Steinberg;

    switch (id) {
        // Pattern Type & Core
        case kFreezePatternTypeId:
            params.patternType.store(
                static_cast<int>(normalizedValue * 3.0 + 0.5),
                std::memory_order_relaxed);
            break;

        case kFreezeSliceLengthId:
            params.sliceLengthMs.store(
                static_cast<float>(
                    PatternFreezeConstants::kMinSliceLengthMs +
                    normalizedValue * (PatternFreezeConstants::kMaxSliceLengthMs -
                                       PatternFreezeConstants::kMinSliceLengthMs)),
                std::memory_order_relaxed);
            break;

        case kFreezeSliceModeId:
            params.sliceMode.store(
                normalizedValue >= 0.5 ? 1 : 0,
                std::memory_order_relaxed);
            break;

        // Euclidean Parameters
        case kFreezeEuclideanStepsId:
            params.euclideanSteps.store(
                static_cast<int>(
                    PatternFreezeConstants::kMinEuclideanSteps +
                    normalizedValue * (PatternFreezeConstants::kMaxEuclideanSteps -
                                       PatternFreezeConstants::kMinEuclideanSteps) + 0.5),
                std::memory_order_relaxed);
            break;

        case kFreezeEuclideanHitsId: {
            int steps = params.euclideanSteps.load(std::memory_order_relaxed);
            params.euclideanHits.store(
                static_cast<int>(1 + normalizedValue * (steps - 1) + 0.5),
                std::memory_order_relaxed);
            break;
        }

        case kFreezeEuclideanRotationId: {
            int steps = params.euclideanSteps.load(std::memory_order_relaxed);
            params.euclideanRotation.store(
                static_cast<int>(normalizedValue * (steps - 1) + 0.5),
                std::memory_order_relaxed);
            break;
        }

        case kFreezePatternRateId:
            params.patternRate.store(
                static_cast<int>(normalizedValue * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                std::memory_order_relaxed);
            break;

        // Granular Scatter Parameters
        case kFreezeGranularDensityId:
            params.granularDensity.store(
                static_cast<float>(
                    PatternFreezeConstants::kMinGranularDensity +
                    normalizedValue * (PatternFreezeConstants::kMaxGranularDensity -
                                       PatternFreezeConstants::kMinGranularDensity)),
                std::memory_order_relaxed);
            break;

        case kFreezeGranularPositionJitterId:
            params.granularPositionJitter.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        case kFreezeGranularSizeJitterId:
            params.granularSizeJitter.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        case kFreezeGranularGrainSizeId:
            params.granularGrainSize.store(
                static_cast<float>(
                    PatternFreezeConstants::kMinGranularGrainSize +
                    normalizedValue * (PatternFreezeConstants::kMaxGranularGrainSize -
                                       PatternFreezeConstants::kMinGranularGrainSize)),
                std::memory_order_relaxed);
            break;

        // Harmonic Drones Parameters
        case kFreezeDroneVoiceCountId:
            params.droneVoiceCount.store(
                static_cast<int>(
                    PatternFreezeConstants::kMinDroneVoiceCount +
                    normalizedValue * (PatternFreezeConstants::kMaxDroneVoiceCount -
                                       PatternFreezeConstants::kMinDroneVoiceCount) + 0.5),
                std::memory_order_relaxed);
            break;

        case kFreezeDroneIntervalId:
            params.droneInterval.store(
                static_cast<int>(normalizedValue * 5.0 + 0.5),
                std::memory_order_relaxed);
            break;

        case kFreezeDroneDriftId:
            params.droneDrift.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        case kFreezeDroneDriftRateId:
            params.droneDriftRate.store(
                static_cast<float>(
                    PatternFreezeConstants::kMinDroneDriftRate +
                    normalizedValue * (PatternFreezeConstants::kMaxDroneDriftRate -
                                       PatternFreezeConstants::kMinDroneDriftRate)),
                std::memory_order_relaxed);
            break;

        // Noise Bursts Parameters
        case kFreezeNoiseColorId:
            params.noiseColor.store(
                static_cast<int>(normalizedValue * 2.0 + 0.5),
                std::memory_order_relaxed);
            break;

        case kFreezeNoiseBurstRateId:
            params.noiseBurstRate.store(
                static_cast<int>(normalizedValue * (Parameters::kNoteValueDropdownCount - 1) + 0.5),
                std::memory_order_relaxed);
            break;

        case kFreezeNoiseFilterTypeId:
            params.noiseFilterType.store(
                static_cast<int>(normalizedValue * 2.0 + 0.5),
                std::memory_order_relaxed);
            break;

        case kFreezeNoiseFilterCutoffId:
            params.noiseFilterCutoff.store(
                static_cast<float>(20.0 * std::pow(1000.0, normalizedValue)),
                std::memory_order_relaxed);
            break;

        case kFreezeNoiseFilterSweepId:
            params.noiseFilterSweep.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        // Envelope Parameters
        case kFreezeEnvelopeAttackId:
            params.envelopeAttackMs.store(
                static_cast<float>(
                    PatternFreezeConstants::kMinEnvelopeAttackMs +
                    normalizedValue * (PatternFreezeConstants::kMaxEnvelopeAttackMs -
                                       PatternFreezeConstants::kMinEnvelopeAttackMs)),
                std::memory_order_relaxed);
            break;

        case kFreezeEnvelopeReleaseId:
            params.envelopeReleaseMs.store(
                static_cast<float>(
                    PatternFreezeConstants::kMinEnvelopeReleaseMs +
                    normalizedValue * (PatternFreezeConstants::kMaxEnvelopeReleaseMs -
                                       PatternFreezeConstants::kMinEnvelopeReleaseMs)),
                std::memory_order_relaxed);
            break;

        case kFreezeEnvelopeShapeId:
            params.envelopeShape.store(
                normalizedValue >= 0.5 ? 1 : 0,
                std::memory_order_relaxed);
            break;
    }
}

// ==============================================================================
// Parameter Registration (for Controller)
// ==============================================================================

inline void registerPatternFreezeParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    // Pattern Type (4 types)
    parameters.addParameter(createDropdownParameter(
        STR16("Freeze Pattern Type"), kFreezePatternTypeId,
        {STR16("Euclidean"), STR16("Granular"), STR16("Drones"),
         STR16("Noise")}
    ));

    // Slice Length (10-2000ms)
    parameters.addParameter(
        STR16("Freeze Slice Length"),
        STR16("ms"),
        0,
        0.045,  // default: ~100ms
        ParameterInfo::kCanAutomate,
        kFreezeSliceLengthId);

    // Slice Mode (Fixed/Variable)
    parameters.addParameter(createDropdownParameter(
        STR16("Freeze Slice Mode"), kFreezeSliceModeId,
        {STR16("Fixed"), STR16("Variable")}
    ));

    // Euclidean Steps (2-32)
    parameters.addParameter(
        STR16("Freeze Euclidean Steps"),
        nullptr,
        30,  // stepCount = 32-2 = 30
        0.2,  // default: 8 steps
        ParameterInfo::kCanAutomate,
        kFreezeEuclideanStepsId);

    // Euclidean Hits (1-steps)
    parameters.addParameter(
        STR16("Freeze Euclidean Hits"),
        nullptr,
        0,
        0.28,  // default: ~3 hits
        ParameterInfo::kCanAutomate,
        kFreezeEuclideanHitsId);

    // Euclidean Rotation (0-steps-1)
    parameters.addParameter(
        STR16("Freeze Euclidean Rotation"),
        nullptr,
        0,
        0,  // default: 0
        ParameterInfo::kCanAutomate,
        kFreezeEuclideanRotationId);

    // Pattern Rate (note value dropdown)
    parameters.addParameter(createNoteValueDropdown(
        STR16("Freeze Pattern Rate"), kFreezePatternRateId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));

    // Granular Density (1-50 Hz)
    parameters.addParameter(
        STR16("Freeze Granular Density"),
        STR16("Hz"),
        0,
        0.184,  // default: ~10 Hz
        ParameterInfo::kCanAutomate,
        kFreezeGranularDensityId);

    // Granular Position Jitter (0-100%)
    parameters.addParameter(
        STR16("Freeze Position Jitter"),
        STR16("%"),
        0,
        0.2,  // default: 20%
        ParameterInfo::kCanAutomate,
        kFreezeGranularPositionJitterId);

    // Granular Size Jitter (0-100%)
    parameters.addParameter(
        STR16("Freeze Size Jitter"),
        STR16("%"),
        0,
        0.2,  // default: 20%
        ParameterInfo::kCanAutomate,
        kFreezeGranularSizeJitterId);

    // Granular Grain Size (10-500ms)
    parameters.addParameter(
        STR16("Freeze Grain Size"),
        STR16("ms"),
        0,
        0.163,  // default: ~100ms
        ParameterInfo::kCanAutomate,
        kFreezeGranularGrainSizeId);

    // Drone Voice Count (1-4)
    parameters.addParameter(
        STR16("Freeze Drone Voices"),
        nullptr,
        3,  // stepCount = 4-1 = 3
        0.333,  // default: 2 voices
        ParameterInfo::kCanAutomate,
        kFreezeDroneVoiceCountId);

    // Drone Interval (6 intervals)
    parameters.addParameter(createDropdownParameter(
        STR16("Freeze Drone Interval"), kFreezeDroneIntervalId,
        {STR16("Unison"), STR16("Fifth"), STR16("Octave"),
         STR16("Fifth Up"), STR16("Oct Up"), STR16("Two Oct")}
    ));

    // Drone Drift (0-100%)
    parameters.addParameter(
        STR16("Freeze Drone Drift"),
        STR16("%"),
        0,
        0.1,  // default: 10%
        ParameterInfo::kCanAutomate,
        kFreezeDroneDriftId);

    // Drone Drift Rate (0.1-2.0 Hz)
    parameters.addParameter(
        STR16("Freeze Drift Rate"),
        STR16("Hz"),
        0,
        0.263,  // default: 0.5 Hz
        ParameterInfo::kCanAutomate,
        kFreezeDroneDriftRateId);

    // Noise Color (3 types)
    parameters.addParameter(createDropdownParameter(
        STR16("Freeze Noise Color"), kFreezeNoiseColorId,
        {STR16("White"), STR16("Pink"), STR16("Brown")}
    ));

    // Noise Burst Rate (note value dropdown)
    parameters.addParameter(createNoteValueDropdown(
        STR16("Freeze Burst Rate"), kFreezeNoiseBurstRateId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));

    // Noise Filter Type (3 types)
    parameters.addParameter(createDropdownParameter(
        STR16("Freeze Noise Filter"), kFreezeNoiseFilterTypeId,
        {STR16("LowPass"), STR16("HighPass"), STR16("BandPass")}
    ));

    // Noise Filter Cutoff (20-20000 Hz)
    parameters.addParameter(
        STR16("Freeze Noise Cutoff"),
        STR16("Hz"),
        0,
        0.333,  // default: ~1000 Hz
        ParameterInfo::kCanAutomate,
        kFreezeNoiseFilterCutoffId);

    // Noise Filter Sweep (0-100%)
    parameters.addParameter(
        STR16("Freeze Noise Sweep"),
        STR16("%"),
        0,
        0.3,  // default: 30%
        ParameterInfo::kCanAutomate,
        kFreezeNoiseFilterSweepId);

    // Envelope Attack (0-500ms)
    parameters.addParameter(
        STR16("Freeze Env Attack"),
        STR16("ms"),
        0,
        0.02,  // default: ~10ms
        ParameterInfo::kCanAutomate,
        kFreezeEnvelopeAttackId);

    // Envelope Release (0-2000ms)
    parameters.addParameter(
        STR16("Freeze Env Release"),
        STR16("ms"),
        0,
        0.025,  // default: ~50ms
        ParameterInfo::kCanAutomate,
        kFreezeEnvelopeReleaseId);

    // Envelope Shape (Linear/Exponential)
    parameters.addParameter(createDropdownParameter(
        STR16("Freeze Env Shape"), kFreezeEnvelopeShapeId,
        {STR16("Linear"), STR16("Exponential")}
    ));
}

// ==============================================================================
// Parameter Display Formatting
// ==============================================================================

inline Steinberg::tresult formatPatternFreezeParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;
    using namespace Krate::DSP;

    switch (id) {
        case kFreezeSliceLengthId: {
            float ms = static_cast<float>(
                PatternFreezeConstants::kMinSliceLengthMs +
                normalizedValue * (PatternFreezeConstants::kMaxSliceLengthMs -
                                   PatternFreezeConstants::kMinSliceLengthMs));
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f ms", ms);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezeEuclideanStepsId: {
            int steps = static_cast<int>(
                PatternFreezeConstants::kMinEuclideanSteps +
                normalizedValue * (PatternFreezeConstants::kMaxEuclideanSteps -
                                   PatternFreezeConstants::kMinEuclideanSteps) + 0.5);
            char8 text[32];
            snprintf(text, sizeof(text), "%d", steps);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezeEuclideanHitsId: {
            // Hits depend on steps, simplified display
            int hits = static_cast<int>(normalizedValue * 31 + 1);
            char8 text[32];
            snprintf(text, sizeof(text), "%d", hits);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezeEuclideanRotationId: {
            int rotation = static_cast<int>(normalizedValue * 31);
            char8 text[32];
            snprintf(text, sizeof(text), "%d", rotation);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezeGranularDensityId: {
            float hz = static_cast<float>(
                PatternFreezeConstants::kMinGranularDensity +
                normalizedValue * (PatternFreezeConstants::kMaxGranularDensity -
                                   PatternFreezeConstants::kMinGranularDensity));
            char8 text[32];
            snprintf(text, sizeof(text), "%.1f Hz", hz);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezeGranularPositionJitterId:
        case kFreezeGranularSizeJitterId:
        case kFreezeDroneDriftId:
        case kFreezeNoiseFilterSweepId: {
            float percent = static_cast<float>(normalizedValue * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezeGranularGrainSizeId: {
            float ms = static_cast<float>(
                PatternFreezeConstants::kMinGranularGrainSize +
                normalizedValue * (PatternFreezeConstants::kMaxGranularGrainSize -
                                   PatternFreezeConstants::kMinGranularGrainSize));
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f ms", ms);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezeDroneVoiceCountId: {
            int voices = static_cast<int>(
                PatternFreezeConstants::kMinDroneVoiceCount +
                normalizedValue * (PatternFreezeConstants::kMaxDroneVoiceCount -
                                   PatternFreezeConstants::kMinDroneVoiceCount) + 0.5);
            char8 text[32];
            snprintf(text, sizeof(text), "%d", voices);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezeDroneDriftRateId: {
            float hz = static_cast<float>(
                PatternFreezeConstants::kMinDroneDriftRate +
                normalizedValue * (PatternFreezeConstants::kMaxDroneDriftRate -
                                   PatternFreezeConstants::kMinDroneDriftRate));
            char8 text[32];
            snprintf(text, sizeof(text), "%.2f Hz", hz);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezeNoiseFilterCutoffId: {
            float hz = static_cast<float>(20.0 * std::pow(1000.0, normalizedValue));
            char8 text[32];
            if (hz >= 1000.0f) {
                snprintf(text, sizeof(text), "%.2f kHz", hz / 1000.0f);
            } else {
                snprintf(text, sizeof(text), "%.0f Hz", hz);
            }
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezeEnvelopeAttackId: {
            float ms = static_cast<float>(
                PatternFreezeConstants::kMinEnvelopeAttackMs +
                normalizedValue * (PatternFreezeConstants::kMaxEnvelopeAttackMs -
                                   PatternFreezeConstants::kMinEnvelopeAttackMs));
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f ms", ms);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }

        case kFreezeEnvelopeReleaseId: {
            float ms = static_cast<float>(
                PatternFreezeConstants::kMinEnvelopeReleaseMs +
                normalizedValue * (PatternFreezeConstants::kMaxEnvelopeReleaseMs -
                                   PatternFreezeConstants::kMinEnvelopeReleaseMs));
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f ms", ms);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }
    }

    return Steinberg::kResultFalse;
}

// ==============================================================================
// State Persistence
// ==============================================================================

inline void savePatternFreezeParams(const PatternFreezeParams& params,
                                     Steinberg::IBStreamer& streamer) {
    streamer.writeInt32(params.patternType.load(std::memory_order_relaxed));
    streamer.writeFloat(params.sliceLengthMs.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sliceMode.load(std::memory_order_relaxed));

    streamer.writeInt32(params.euclideanSteps.load(std::memory_order_relaxed));
    streamer.writeInt32(params.euclideanHits.load(std::memory_order_relaxed));
    streamer.writeInt32(params.euclideanRotation.load(std::memory_order_relaxed));
    streamer.writeInt32(params.patternRate.load(std::memory_order_relaxed));

    streamer.writeFloat(params.granularDensity.load(std::memory_order_relaxed));
    streamer.writeFloat(params.granularPositionJitter.load(std::memory_order_relaxed));
    streamer.writeFloat(params.granularSizeJitter.load(std::memory_order_relaxed));
    streamer.writeFloat(params.granularGrainSize.load(std::memory_order_relaxed));

    streamer.writeInt32(params.droneVoiceCount.load(std::memory_order_relaxed));
    streamer.writeInt32(params.droneInterval.load(std::memory_order_relaxed));
    streamer.writeFloat(params.droneDrift.load(std::memory_order_relaxed));
    streamer.writeFloat(params.droneDriftRate.load(std::memory_order_relaxed));

    streamer.writeInt32(params.noiseColor.load(std::memory_order_relaxed));
    streamer.writeInt32(params.noiseBurstRate.load(std::memory_order_relaxed));
    streamer.writeInt32(params.noiseFilterType.load(std::memory_order_relaxed));
    streamer.writeFloat(params.noiseFilterCutoff.load(std::memory_order_relaxed));
    streamer.writeFloat(params.noiseFilterSweep.load(std::memory_order_relaxed));

    streamer.writeFloat(params.envelopeAttackMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.envelopeReleaseMs.load(std::memory_order_relaxed));
    streamer.writeInt32(params.envelopeShape.load(std::memory_order_relaxed));
}

inline void loadPatternFreezeParams(PatternFreezeParams& params,
                                     Steinberg::IBStreamer& streamer) {
    using namespace Steinberg;
    using namespace Krate::DSP;

    int32 intVal = 0;
    float floatVal = 0.0f;

    if (streamer.readInt32(intVal))
        params.patternType.store(intVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.sliceLengthMs.store(floatVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.sliceMode.store(intVal, std::memory_order_relaxed);

    if (streamer.readInt32(intVal))
        params.euclideanSteps.store(intVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.euclideanHits.store(intVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.euclideanRotation.store(intVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.patternRate.store(intVal, std::memory_order_relaxed);

    if (streamer.readFloat(floatVal))
        params.granularDensity.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.granularPositionJitter.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.granularSizeJitter.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.granularGrainSize.store(floatVal, std::memory_order_relaxed);

    if (streamer.readInt32(intVal))
        params.droneVoiceCount.store(intVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.droneInterval.store(intVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.droneDrift.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.droneDriftRate.store(floatVal, std::memory_order_relaxed);

    if (streamer.readInt32(intVal))
        params.noiseColor.store(intVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.noiseBurstRate.store(intVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.noiseFilterType.store(intVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.noiseFilterCutoff.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.noiseFilterSweep.store(floatVal, std::memory_order_relaxed);

    if (streamer.readFloat(floatVal))
        params.envelopeAttackMs.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.envelopeReleaseMs.store(floatVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.envelopeShape.store(intVal, std::memory_order_relaxed);
}

} // namespace Iterum
