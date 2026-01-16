#pragma once

// ==============================================================================
// Freeze Mode Parameters
// ==============================================================================
// Parameter pack for Freeze Mode (spec 031) and Pattern Freeze (spec 069)
// ID Range: 1000-1099
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
// Parameter Storage
// ==============================================================================

struct FreezeParams {
    // Dry/Wet Mix (shared by all pattern types)
    std::atomic<float> dryWet{0.5f};             // 0-1

    // Pattern Freeze parameters (spec 069)
    std::atomic<int> patternType{static_cast<int>(Krate::DSP::kDefaultPatternType)};
    std::atomic<float> sliceLengthMs{Krate::DSP::PatternFreezeConstants::kDefaultSliceLengthMs};
    std::atomic<int> sliceMode{static_cast<int>(Krate::DSP::kDefaultSliceMode)};

    std::atomic<int> euclideanSteps{Krate::DSP::PatternFreezeConstants::kDefaultEuclideanSteps};
    std::atomic<int> euclideanHits{Krate::DSP::PatternFreezeConstants::kDefaultEuclideanHits};
    std::atomic<int> euclideanRotation{Krate::DSP::PatternFreezeConstants::kDefaultEuclideanRotation};
    std::atomic<int> patternRate{Parameters::kNoteValueDefaultIndex};

    std::atomic<float> granularDensity{Krate::DSP::PatternFreezeConstants::kDefaultGranularDensityHz};
    std::atomic<float> granularPositionJitter{Krate::DSP::PatternFreezeConstants::kDefaultPositionJitter};
    std::atomic<float> granularSizeJitter{Krate::DSP::PatternFreezeConstants::kDefaultSizeJitter};
    std::atomic<float> granularGrainSize{Krate::DSP::PatternFreezeConstants::kDefaultGranularGrainSizeMs};

    std::atomic<int> droneVoiceCount{Krate::DSP::PatternFreezeConstants::kDefaultDroneVoices};
    std::atomic<int> droneInterval{static_cast<int>(Krate::DSP::kDefaultPitchInterval)};
    std::atomic<float> droneDrift{Krate::DSP::PatternFreezeConstants::kDefaultDroneDrift};
    std::atomic<float> droneDriftRate{Krate::DSP::PatternFreezeConstants::kDefaultDroneDriftRateHz};

    std::atomic<int> noiseColor{static_cast<int>(Krate::DSP::kDefaultNoiseColor)};
    std::atomic<int> noiseBurstRate{Parameters::kNoteValueDefaultIndex};
    std::atomic<int> noiseFilterType{0};
    std::atomic<float> noiseFilterCutoff{Krate::DSP::PatternFreezeConstants::kDefaultNoiseFilterCutoffHz};
    std::atomic<float> noiseFilterSweep{Krate::DSP::PatternFreezeConstants::kDefaultNoiseFilterSweep};

    std::atomic<float> envelopeAttackMs{Krate::DSP::PatternFreezeConstants::kDefaultEnvelopeAttackMs};
    std::atomic<float> envelopeReleaseMs{Krate::DSP::PatternFreezeConstants::kDefaultEnvelopeReleaseMs};
    std::atomic<int> envelopeShape{static_cast<int>(Krate::DSP::kDefaultEnvelopeShape)};
};

// ==============================================================================
// Parameter Change Handler
// ==============================================================================

inline void handleFreezeParamChange(
    FreezeParams& params,
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue) {

    using namespace Steinberg;

    switch (id) {
        case kFreezeMixId:
            // 0-1
            params.dryWet.store(
                static_cast<float>(normalizedValue),
                std::memory_order_relaxed);
            break;

        // Pattern Freeze parameters (spec 069)
        case kFreezePatternTypeId:
            // 4 pattern types: Euclidean(0), GranularScatter(1), HarmonicDrones(2), NoiseBursts(3)
            // Normalized range is 0.0-1.0 for N items, formula: index = normalizedValue * (N-1) + 0.5
            params.patternType.store(
                static_cast<int>(normalizedValue * 3.0 + 0.5),
                std::memory_order_relaxed);
            break;

        case kFreezeSliceLengthId:
            params.sliceLengthMs.store(
                static_cast<float>(
                    Krate::DSP::PatternFreezeConstants::kMinSliceLengthMs +
                    normalizedValue * (Krate::DSP::PatternFreezeConstants::kMaxSliceLengthMs -
                                       Krate::DSP::PatternFreezeConstants::kMinSliceLengthMs)),
                std::memory_order_relaxed);
            break;

        case kFreezeSliceModeId:
            params.sliceMode.store(
                normalizedValue >= 0.5 ? 1 : 0,
                std::memory_order_relaxed);
            break;

        case kFreezeEuclideanStepsId:
            params.euclideanSteps.store(
                static_cast<int>(
                    Krate::DSP::PatternFreezeConstants::kMinEuclideanSteps +
                    normalizedValue * (Krate::DSP::PatternFreezeConstants::kMaxEuclideanSteps -
                                       Krate::DSP::PatternFreezeConstants::kMinEuclideanSteps) + 0.5),
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

        case kFreezeGranularDensityId:
            params.granularDensity.store(
                static_cast<float>(
                    Krate::DSP::PatternFreezeConstants::kMinGranularDensityHz +
                    normalizedValue * (Krate::DSP::PatternFreezeConstants::kMaxGranularDensityHz -
                                       Krate::DSP::PatternFreezeConstants::kMinGranularDensityHz)),
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
                    Krate::DSP::PatternFreezeConstants::kMinGranularGrainSizeMs +
                    normalizedValue * (Krate::DSP::PatternFreezeConstants::kMaxGranularGrainSizeMs -
                                       Krate::DSP::PatternFreezeConstants::kMinGranularGrainSizeMs)),
                std::memory_order_relaxed);
            break;

        case kFreezeDroneVoiceCountId:
            params.droneVoiceCount.store(
                static_cast<int>(
                    Krate::DSP::PatternFreezeConstants::kMinDroneVoices +
                    normalizedValue * (Krate::DSP::PatternFreezeConstants::kMaxDroneVoices -
                                       Krate::DSP::PatternFreezeConstants::kMinDroneVoices) + 0.5),
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
                    Krate::DSP::PatternFreezeConstants::kMinDroneDriftRateHz +
                    normalizedValue * (Krate::DSP::PatternFreezeConstants::kMaxDroneDriftRateHz -
                                       Krate::DSP::PatternFreezeConstants::kMinDroneDriftRateHz)),
                std::memory_order_relaxed);
            break;

        case kFreezeNoiseColorId:
            // 8 noise types: White, Pink, Brown, Blue, Violet, Grey, Velvet, Radio
            params.noiseColor.store(
                static_cast<int>(normalizedValue * 7.0 + 0.5),
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

        case kFreezeEnvelopeAttackId:
            params.envelopeAttackMs.store(
                static_cast<float>(
                    Krate::DSP::PatternFreezeConstants::kMinEnvelopeAttackMs +
                    normalizedValue * (Krate::DSP::PatternFreezeConstants::kMaxEnvelopeAttackMs -
                                       Krate::DSP::PatternFreezeConstants::kMinEnvelopeAttackMs)),
                std::memory_order_relaxed);
            break;

        case kFreezeEnvelopeReleaseId:
            params.envelopeReleaseMs.store(
                static_cast<float>(
                    Krate::DSP::PatternFreezeConstants::kMinEnvelopeReleaseMs +
                    normalizedValue * (Krate::DSP::PatternFreezeConstants::kMaxEnvelopeReleaseMs -
                                       Krate::DSP::PatternFreezeConstants::kMinEnvelopeReleaseMs)),
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

inline void registerFreezeParams(Steinberg::Vst::ParameterContainer& parameters) {
    using namespace Steinberg;
    using namespace Steinberg::Vst;

    // Dry/Wet Mix (0-100%)
    parameters.addParameter(
        STR16("Freeze Dry/Wet"),
        STR16("%"),
        0,
        0.5,  // default: 50%
        ParameterInfo::kCanAutomate,
        kFreezeMixId);

    // ==========================================================================
    // Pattern Freeze Parameters (spec 069)
    // ==========================================================================

    // Pattern Type dropdown (Euclidean, Granular Scatter, Harmonic Drones, Noise Bursts)
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Freeze Pattern Type"), kFreezePatternTypeId,
        0,  // default: Euclidean
        {STR16("Euclidean"), STR16("Granular Scatter"), STR16("Harmonic Drones"),
         STR16("Noise Bursts")}
    ));

    // Slice Length (10-2000ms)
    parameters.addParameter(
        STR16("Freeze Slice Length"),
        STR16("ms"),
        0,
        (Krate::DSP::PatternFreezeConstants::kDefaultSliceLengthMs -
         Krate::DSP::PatternFreezeConstants::kMinSliceLengthMs) /
        (Krate::DSP::PatternFreezeConstants::kMaxSliceLengthMs -
         Krate::DSP::PatternFreezeConstants::kMinSliceLengthMs),
        ParameterInfo::kCanAutomate,
        kFreezeSliceLengthId);

    // Slice Mode (Fixed/Variable)
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Freeze Slice Mode"), kFreezeSliceModeId,
        0,  // default: Fixed
        {STR16("Fixed"), STR16("Variable")}
    ));

    // Euclidean Steps (2-32)
    parameters.addParameter(
        STR16("Freeze Euclidean Steps"),
        nullptr,
        30,  // stepCount: 30 steps (2-32 = 30 values)
        static_cast<ParamValue>(Krate::DSP::PatternFreezeConstants::kDefaultEuclideanSteps -
                                 Krate::DSP::PatternFreezeConstants::kMinEuclideanSteps) /
        static_cast<ParamValue>(Krate::DSP::PatternFreezeConstants::kMaxEuclideanSteps -
                                 Krate::DSP::PatternFreezeConstants::kMinEuclideanSteps),
        ParameterInfo::kCanAutomate,
        kFreezeEuclideanStepsId);

    // Euclidean Hits (1-steps)
    parameters.addParameter(
        STR16("Freeze Euclidean Hits"),
        nullptr,
        0,  // continuous - will be clamped to steps
        static_cast<ParamValue>(Krate::DSP::PatternFreezeConstants::kDefaultEuclideanHits - 1) /
        static_cast<ParamValue>(Krate::DSP::PatternFreezeConstants::kDefaultEuclideanSteps - 1),
        ParameterInfo::kCanAutomate,
        kFreezeEuclideanHitsId);

    // Euclidean Rotation (0-steps-1)
    parameters.addParameter(
        STR16("Freeze Euclidean Rotation"),
        nullptr,
        0,
        0,  // default: 0 rotation
        ParameterInfo::kCanAutomate,
        kFreezeEuclideanRotationId);

    // Pattern Rate (note value)
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
        (Krate::DSP::PatternFreezeConstants::kDefaultGranularDensityHz -
         Krate::DSP::PatternFreezeConstants::kMinGranularDensityHz) /
        (Krate::DSP::PatternFreezeConstants::kMaxGranularDensityHz -
         Krate::DSP::PatternFreezeConstants::kMinGranularDensityHz),
        ParameterInfo::kCanAutomate,
        kFreezeGranularDensityId);

    // Granular Position Jitter (0-100%)
    parameters.addParameter(
        STR16("Freeze Position Jitter"),
        STR16("%"),
        0,
        Krate::DSP::PatternFreezeConstants::kDefaultPositionJitter,
        ParameterInfo::kCanAutomate,
        kFreezeGranularPositionJitterId);

    // Granular Size Jitter (0-100%)
    parameters.addParameter(
        STR16("Freeze Size Jitter"),
        STR16("%"),
        0,
        Krate::DSP::PatternFreezeConstants::kDefaultSizeJitter,
        ParameterInfo::kCanAutomate,
        kFreezeGranularSizeJitterId);

    // Granular Grain Size (10-500ms)
    parameters.addParameter(
        STR16("Freeze Grain Size"),
        STR16("ms"),
        0,
        (Krate::DSP::PatternFreezeConstants::kDefaultGranularGrainSizeMs -
         Krate::DSP::PatternFreezeConstants::kMinGranularGrainSizeMs) /
        (Krate::DSP::PatternFreezeConstants::kMaxGranularGrainSizeMs -
         Krate::DSP::PatternFreezeConstants::kMinGranularGrainSizeMs),
        ParameterInfo::kCanAutomate,
        kFreezeGranularGrainSizeId);

    // Drone Voice Count (1-4)
    parameters.addParameter(
        STR16("Freeze Drone Voices"),
        nullptr,
        3,  // stepCount: 3 (1-4 = 3 steps)
        static_cast<ParamValue>(Krate::DSP::PatternFreezeConstants::kDefaultDroneVoices -
                                 Krate::DSP::PatternFreezeConstants::kMinDroneVoices) /
        static_cast<ParamValue>(Krate::DSP::PatternFreezeConstants::kMaxDroneVoices -
                                 Krate::DSP::PatternFreezeConstants::kMinDroneVoices),
        ParameterInfo::kCanAutomate,
        kFreezeDroneVoiceCountId);

    // Drone Interval (pitch intervals)
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Freeze Drone Interval"), kFreezeDroneIntervalId,
        5,  // default: Octave (index 5)
        {STR16("Unison"), STR16("Minor 3rd"), STR16("Major 3rd"),
         STR16("Perfect 4th"), STR16("Perfect 5th"), STR16("Octave")}
    ));

    // Drone Drift (0-100%)
    parameters.addParameter(
        STR16("Freeze Drone Drift"),
        STR16("%"),
        0,
        Krate::DSP::PatternFreezeConstants::kDefaultDroneDrift,
        ParameterInfo::kCanAutomate,
        kFreezeDroneDriftId);

    // Drone Drift Rate (0.1-2.0 Hz)
    parameters.addParameter(
        STR16("Freeze Drift Rate"),
        STR16("Hz"),
        0,
        (Krate::DSP::PatternFreezeConstants::kDefaultDroneDriftRateHz -
         Krate::DSP::PatternFreezeConstants::kMinDroneDriftRateHz) /
        (Krate::DSP::PatternFreezeConstants::kMaxDroneDriftRateHz -
         Krate::DSP::PatternFreezeConstants::kMinDroneDriftRateHz),
        ParameterInfo::kCanAutomate,
        kFreezeDroneDriftRateId);

    // Noise Color (8 types: White, Pink, Brown, Blue, Violet, Grey, Velvet, Radio)
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Freeze Noise Color"), kFreezeNoiseColorId,
        1,  // default: Pink (index 1)
        {STR16("White"), STR16("Pink"), STR16("Brown"), STR16("Blue"),
         STR16("Violet"), STR16("Grey"), STR16("Velvet"), STR16("Radio")}
    ));

    // Noise Burst Rate (note value)
    parameters.addParameter(createNoteValueDropdown(
        STR16("Freeze Noise Burst Rate"), kFreezeNoiseBurstRateId,
        Parameters::kNoteValueDropdownStrings,
        Parameters::kNoteValueDropdownCount,
        Parameters::kNoteValueDefaultIndex
    ));

    // Noise Filter Type
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Freeze Noise Filter Type"), kFreezeNoiseFilterTypeId,
        0,  // default: LowPass
        {STR16("LowPass"), STR16("HighPass"), STR16("BandPass")}
    ));

    // Noise Filter Cutoff (20-20000Hz)
    parameters.addParameter(
        STR16("Freeze Noise Filter Cutoff"),
        STR16("Hz"),
        0,
        std::log(Krate::DSP::PatternFreezeConstants::kDefaultNoiseFilterCutoffHz / 20.0f) /
        std::log(1000.0f),  // Log scale for frequency
        ParameterInfo::kCanAutomate,
        kFreezeNoiseFilterCutoffId);

    // Noise Filter Sweep (0-100%)
    parameters.addParameter(
        STR16("Freeze Noise Filter Sweep"),
        STR16("%"),
        0,
        Krate::DSP::PatternFreezeConstants::kDefaultNoiseFilterSweep,
        ParameterInfo::kCanAutomate,
        kFreezeNoiseFilterSweepId);

    // Envelope Attack (0-500ms)
    parameters.addParameter(
        STR16("Freeze Envelope Attack"),
        STR16("ms"),
        0,
        Krate::DSP::PatternFreezeConstants::kDefaultEnvelopeAttackMs /
        Krate::DSP::PatternFreezeConstants::kMaxEnvelopeAttackMs,
        ParameterInfo::kCanAutomate,
        kFreezeEnvelopeAttackId);

    // Envelope Release (0-2000ms)
    parameters.addParameter(
        STR16("Freeze Envelope Release"),
        STR16("ms"),
        0,
        Krate::DSP::PatternFreezeConstants::kDefaultEnvelopeReleaseMs /
        Krate::DSP::PatternFreezeConstants::kMaxEnvelopeReleaseMs,
        ParameterInfo::kCanAutomate,
        kFreezeEnvelopeReleaseId);

    // Envelope Shape (Linear/Exponential)
    parameters.addParameter(createDropdownParameterWithDefault(
        STR16("Freeze Envelope Shape"), kFreezeEnvelopeShapeId,
        0,  // default: Linear
        {STR16("Linear"), STR16("Exponential")}
    ));
}

// ==============================================================================
// Parameter Display Formatting (for Controller)
// ==============================================================================

inline Steinberg::tresult formatFreezeParam(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue normalizedValue,
    Steinberg::Vst::String128 string) {

    using namespace Steinberg;

    switch (id) {
        case kFreezeMixId: {
            float percent = static_cast<float>(normalizedValue * 100.0);
            char8 text[32];
            snprintf(text, sizeof(text), "%.0f%%", percent);
            Steinberg::UString(string, 128).fromAscii(text);
            return kResultOk;
        }
    }

    return Steinberg::kResultFalse;
}

// ==============================================================================
// State Persistence
// ==============================================================================

inline void saveFreezeParams(const FreezeParams& params, Steinberg::IBStreamer& streamer) {
    // Pattern Freeze state format (spec 069)
    // All parameters saved for full preset support

    // Core parameters
    streamer.writeFloat(params.dryWet.load(std::memory_order_relaxed));
    streamer.writeInt32(params.patternType.load(std::memory_order_relaxed));
    streamer.writeFloat(params.sliceLengthMs.load(std::memory_order_relaxed));
    streamer.writeInt32(params.sliceMode.load(std::memory_order_relaxed));

    // Euclidean parameters
    streamer.writeInt32(params.euclideanSteps.load(std::memory_order_relaxed));
    streamer.writeInt32(params.euclideanHits.load(std::memory_order_relaxed));
    streamer.writeInt32(params.euclideanRotation.load(std::memory_order_relaxed));
    streamer.writeInt32(params.patternRate.load(std::memory_order_relaxed));

    // Granular parameters
    streamer.writeFloat(params.granularDensity.load(std::memory_order_relaxed));
    streamer.writeFloat(params.granularPositionJitter.load(std::memory_order_relaxed));
    streamer.writeFloat(params.granularSizeJitter.load(std::memory_order_relaxed));
    streamer.writeFloat(params.granularGrainSize.load(std::memory_order_relaxed));

    // Drone parameters
    streamer.writeInt32(params.droneVoiceCount.load(std::memory_order_relaxed));
    streamer.writeInt32(params.droneInterval.load(std::memory_order_relaxed));
    streamer.writeFloat(params.droneDrift.load(std::memory_order_relaxed));
    streamer.writeFloat(params.droneDriftRate.load(std::memory_order_relaxed));

    // Noise parameters
    streamer.writeInt32(params.noiseColor.load(std::memory_order_relaxed));
    streamer.writeInt32(params.noiseBurstRate.load(std::memory_order_relaxed));
    streamer.writeInt32(params.noiseFilterType.load(std::memory_order_relaxed));
    streamer.writeFloat(params.noiseFilterCutoff.load(std::memory_order_relaxed));
    streamer.writeFloat(params.noiseFilterSweep.load(std::memory_order_relaxed));

    // Envelope parameters
    streamer.writeFloat(params.envelopeAttackMs.load(std::memory_order_relaxed));
    streamer.writeFloat(params.envelopeReleaseMs.load(std::memory_order_relaxed));
    streamer.writeInt32(params.envelopeShape.load(std::memory_order_relaxed));
}

inline void loadFreezeParams(FreezeParams& params, Steinberg::IBStreamer& streamer) {
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;

    // Core parameters
    if (streamer.readFloat(floatVal))
        params.dryWet.store(floatVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.patternType.store(intVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.sliceLengthMs.store(floatVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.sliceMode.store(intVal, std::memory_order_relaxed);

    // Euclidean parameters
    if (streamer.readInt32(intVal))
        params.euclideanSteps.store(intVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.euclideanHits.store(intVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.euclideanRotation.store(intVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.patternRate.store(intVal, std::memory_order_relaxed);

    // Granular parameters
    if (streamer.readFloat(floatVal))
        params.granularDensity.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.granularPositionJitter.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.granularSizeJitter.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.granularGrainSize.store(floatVal, std::memory_order_relaxed);

    // Drone parameters
    if (streamer.readInt32(intVal))
        params.droneVoiceCount.store(intVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.droneInterval.store(intVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.droneDrift.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.droneDriftRate.store(floatVal, std::memory_order_relaxed);

    // Noise parameters
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

    // Envelope parameters
    if (streamer.readFloat(floatVal))
        params.envelopeAttackMs.store(floatVal, std::memory_order_relaxed);
    if (streamer.readFloat(floatVal))
        params.envelopeReleaseMs.store(floatVal, std::memory_order_relaxed);
    if (streamer.readInt32(intVal))
        params.envelopeShape.store(intVal, std::memory_order_relaxed);
}

// ==============================================================================
// State Synchronization (Controller -> Processor state sync)
// ==============================================================================

inline void syncFreezeParamsToController(
    const FreezeParams& params,
    Steinberg::Vst::IEditController* controller) {

    using namespace Steinberg::Vst;

    // Core parameters
    controller->setParamNormalized(kFreezeMixId,
        params.dryWet.load(std::memory_order_relaxed));

    // Pattern type (4 types: 0-3)
    controller->setParamNormalized(kFreezePatternTypeId,
        params.patternType.load(std::memory_order_relaxed) / 3.0);

    // Slice length (10-2000ms)
    controller->setParamNormalized(kFreezeSliceLengthId,
        (params.sliceLengthMs.load(std::memory_order_relaxed) -
         Krate::DSP::PatternFreezeConstants::kMinSliceLengthMs) /
        (Krate::DSP::PatternFreezeConstants::kMaxSliceLengthMs -
         Krate::DSP::PatternFreezeConstants::kMinSliceLengthMs));

    // Slice mode (0 or 1)
    controller->setParamNormalized(kFreezeSliceModeId,
        params.sliceMode.load(std::memory_order_relaxed) >= 1 ? 1.0 : 0.0);

    // Euclidean parameters
    controller->setParamNormalized(kFreezeEuclideanStepsId,
        static_cast<double>(params.euclideanSteps.load(std::memory_order_relaxed) -
                            Krate::DSP::PatternFreezeConstants::kMinEuclideanSteps) /
        static_cast<double>(Krate::DSP::PatternFreezeConstants::kMaxEuclideanSteps -
                            Krate::DSP::PatternFreezeConstants::kMinEuclideanSteps));

    int steps = params.euclideanSteps.load(std::memory_order_relaxed);
    controller->setParamNormalized(kFreezeEuclideanHitsId,
        static_cast<double>(params.euclideanHits.load(std::memory_order_relaxed) - 1) /
        static_cast<double>(steps - 1));

    controller->setParamNormalized(kFreezeEuclideanRotationId,
        static_cast<double>(params.euclideanRotation.load(std::memory_order_relaxed)) /
        static_cast<double>(steps - 1));

    controller->setParamNormalized(kFreezePatternRateId,
        params.patternRate.load(std::memory_order_relaxed) /
        static_cast<double>(Parameters::kNoteValueDropdownCount - 1));

    // Granular parameters
    controller->setParamNormalized(kFreezeGranularDensityId,
        (params.granularDensity.load(std::memory_order_relaxed) -
         Krate::DSP::PatternFreezeConstants::kMinGranularDensityHz) /
        (Krate::DSP::PatternFreezeConstants::kMaxGranularDensityHz -
         Krate::DSP::PatternFreezeConstants::kMinGranularDensityHz));

    controller->setParamNormalized(kFreezeGranularPositionJitterId,
        params.granularPositionJitter.load(std::memory_order_relaxed));

    controller->setParamNormalized(kFreezeGranularSizeJitterId,
        params.granularSizeJitter.load(std::memory_order_relaxed));

    controller->setParamNormalized(kFreezeGranularGrainSizeId,
        (params.granularGrainSize.load(std::memory_order_relaxed) -
         Krate::DSP::PatternFreezeConstants::kMinGranularGrainSizeMs) /
        (Krate::DSP::PatternFreezeConstants::kMaxGranularGrainSizeMs -
         Krate::DSP::PatternFreezeConstants::kMinGranularGrainSizeMs));

    // Drone parameters
    controller->setParamNormalized(kFreezeDroneVoiceCountId,
        static_cast<double>(params.droneVoiceCount.load(std::memory_order_relaxed) -
                            Krate::DSP::PatternFreezeConstants::kMinDroneVoices) /
        static_cast<double>(Krate::DSP::PatternFreezeConstants::kMaxDroneVoices -
                            Krate::DSP::PatternFreezeConstants::kMinDroneVoices));

    controller->setParamNormalized(kFreezeDroneIntervalId,
        params.droneInterval.load(std::memory_order_relaxed) / 5.0);

    controller->setParamNormalized(kFreezeDroneDriftId,
        params.droneDrift.load(std::memory_order_relaxed));

    controller->setParamNormalized(kFreezeDroneDriftRateId,
        (params.droneDriftRate.load(std::memory_order_relaxed) -
         Krate::DSP::PatternFreezeConstants::kMinDroneDriftRateHz) /
        (Krate::DSP::PatternFreezeConstants::kMaxDroneDriftRateHz -
         Krate::DSP::PatternFreezeConstants::kMinDroneDriftRateHz));

    // Noise parameters
    controller->setParamNormalized(kFreezeNoiseColorId,
        params.noiseColor.load(std::memory_order_relaxed) / 7.0);

    controller->setParamNormalized(kFreezeNoiseBurstRateId,
        params.noiseBurstRate.load(std::memory_order_relaxed) /
        static_cast<double>(Parameters::kNoteValueDropdownCount - 1));

    controller->setParamNormalized(kFreezeNoiseFilterTypeId,
        params.noiseFilterType.load(std::memory_order_relaxed) / 2.0);

    // Noise filter cutoff (log scale: 20-20000Hz)
    controller->setParamNormalized(kFreezeNoiseFilterCutoffId,
        std::log(params.noiseFilterCutoff.load(std::memory_order_relaxed) / 20.0f) /
        std::log(1000.0f));

    controller->setParamNormalized(kFreezeNoiseFilterSweepId,
        params.noiseFilterSweep.load(std::memory_order_relaxed));

    // Envelope parameters
    controller->setParamNormalized(kFreezeEnvelopeAttackId,
        (params.envelopeAttackMs.load(std::memory_order_relaxed) -
         Krate::DSP::PatternFreezeConstants::kMinEnvelopeAttackMs) /
        (Krate::DSP::PatternFreezeConstants::kMaxEnvelopeAttackMs -
         Krate::DSP::PatternFreezeConstants::kMinEnvelopeAttackMs));

    controller->setParamNormalized(kFreezeEnvelopeReleaseId,
        (params.envelopeReleaseMs.load(std::memory_order_relaxed) -
         Krate::DSP::PatternFreezeConstants::kMinEnvelopeReleaseMs) /
        (Krate::DSP::PatternFreezeConstants::kMaxEnvelopeReleaseMs -
         Krate::DSP::PatternFreezeConstants::kMinEnvelopeReleaseMs));

    controller->setParamNormalized(kFreezeEnvelopeShapeId,
        params.envelopeShape.load(std::memory_order_relaxed) >= 1 ? 1.0 : 0.0);
}

// ==============================================================================
// Controller State Sync (from IBStreamer) - Template Version
// ==============================================================================

template <typename SetParamFunc>
inline void loadFreezeParamsToController(
    Steinberg::IBStreamer& streamer,
    SetParamFunc setParam)
{
    using namespace Steinberg;

    int32 intVal = 0;
    float floatVal = 0.0f;

    // Core parameters - dryWet (0-1)
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeMixId, static_cast<double>(floatVal));
    }

    // Pattern type (0-3 -> normalize to 0-1)
    if (streamer.readInt32(intVal)) {
        setParam(kFreezePatternTypeId, intVal / 3.0);
    }

    // Slice length (10-2000ms)
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeSliceLengthId,
            (floatVal - Krate::DSP::PatternFreezeConstants::kMinSliceLengthMs) /
            (Krate::DSP::PatternFreezeConstants::kMaxSliceLengthMs -
             Krate::DSP::PatternFreezeConstants::kMinSliceLengthMs));
    }

    // Slice mode (0 or 1)
    if (streamer.readInt32(intVal)) {
        setParam(kFreezeSliceModeId, intVal >= 1 ? 1.0 : 0.0);
    }

    // Euclidean steps (2-32)
    int steps = Krate::DSP::PatternFreezeConstants::kDefaultEuclideanSteps;
    if (streamer.readInt32(intVal)) {
        steps = intVal;
        setParam(kFreezeEuclideanStepsId,
            static_cast<double>(intVal - Krate::DSP::PatternFreezeConstants::kMinEuclideanSteps) /
            static_cast<double>(Krate::DSP::PatternFreezeConstants::kMaxEuclideanSteps -
                                Krate::DSP::PatternFreezeConstants::kMinEuclideanSteps));
    }

    // Euclidean hits (1-steps)
    if (streamer.readInt32(intVal)) {
        setParam(kFreezeEuclideanHitsId,
            static_cast<double>(intVal - 1) / static_cast<double>(steps - 1));
    }

    // Euclidean rotation (0 to steps-1)
    if (streamer.readInt32(intVal)) {
        setParam(kFreezeEuclideanRotationId,
            static_cast<double>(intVal) / static_cast<double>(steps - 1));
    }

    // Pattern rate (note value index)
    if (streamer.readInt32(intVal)) {
        setParam(kFreezePatternRateId,
            intVal / static_cast<double>(Parameters::kNoteValueDropdownCount - 1));
    }

    // Granular density (1-50 Hz)
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeGranularDensityId,
            (floatVal - Krate::DSP::PatternFreezeConstants::kMinGranularDensityHz) /
            (Krate::DSP::PatternFreezeConstants::kMaxGranularDensityHz -
             Krate::DSP::PatternFreezeConstants::kMinGranularDensityHz));
    }

    // Granular position jitter (0-1)
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeGranularPositionJitterId, static_cast<double>(floatVal));
    }

    // Granular size jitter (0-1)
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeGranularSizeJitterId, static_cast<double>(floatVal));
    }

    // Granular grain size (10-500ms)
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeGranularGrainSizeId,
            (floatVal - Krate::DSP::PatternFreezeConstants::kMinGranularGrainSizeMs) /
            (Krate::DSP::PatternFreezeConstants::kMaxGranularGrainSizeMs -
             Krate::DSP::PatternFreezeConstants::kMinGranularGrainSizeMs));
    }

    // Drone voice count (1-4)
    if (streamer.readInt32(intVal)) {
        setParam(kFreezeDroneVoiceCountId,
            static_cast<double>(intVal - Krate::DSP::PatternFreezeConstants::kMinDroneVoices) /
            static_cast<double>(Krate::DSP::PatternFreezeConstants::kMaxDroneVoices -
                                Krate::DSP::PatternFreezeConstants::kMinDroneVoices));
    }

    // Drone interval (0-5)
    if (streamer.readInt32(intVal)) {
        setParam(kFreezeDroneIntervalId, intVal / 5.0);
    }

    // Drone drift (0-1)
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeDroneDriftId, static_cast<double>(floatVal));
    }

    // Drone drift rate (0.1-2.0 Hz)
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeDroneDriftRateId,
            (floatVal - Krate::DSP::PatternFreezeConstants::kMinDroneDriftRateHz) /
            (Krate::DSP::PatternFreezeConstants::kMaxDroneDriftRateHz -
             Krate::DSP::PatternFreezeConstants::kMinDroneDriftRateHz));
    }

    // Noise color (0-7)
    if (streamer.readInt32(intVal)) {
        setParam(kFreezeNoiseColorId, intVal / 7.0);
    }

    // Noise burst rate (note value index)
    if (streamer.readInt32(intVal)) {
        setParam(kFreezeNoiseBurstRateId,
            intVal / static_cast<double>(Parameters::kNoteValueDropdownCount - 1));
    }

    // Noise filter type (0-2)
    if (streamer.readInt32(intVal)) {
        setParam(kFreezeNoiseFilterTypeId, intVal / 2.0);
    }

    // Noise filter cutoff (20-20000 Hz, log scale)
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeNoiseFilterCutoffId,
            std::log(floatVal / 20.0f) / std::log(1000.0f));
    }

    // Noise filter sweep (0-1)
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeNoiseFilterSweepId, static_cast<double>(floatVal));
    }

    // Envelope attack (0-500ms)
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeEnvelopeAttackId,
            (floatVal - Krate::DSP::PatternFreezeConstants::kMinEnvelopeAttackMs) /
            (Krate::DSP::PatternFreezeConstants::kMaxEnvelopeAttackMs -
             Krate::DSP::PatternFreezeConstants::kMinEnvelopeAttackMs));
    }

    // Envelope release (0-2000ms)
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeEnvelopeReleaseId,
            (floatVal - Krate::DSP::PatternFreezeConstants::kMinEnvelopeReleaseMs) /
            (Krate::DSP::PatternFreezeConstants::kMaxEnvelopeReleaseMs -
             Krate::DSP::PatternFreezeConstants::kMinEnvelopeReleaseMs));
    }

    // Envelope shape (0 or 1)
    if (streamer.readInt32(intVal)) {
        setParam(kFreezeEnvelopeShapeId, intVal >= 1 ? 1.0 : 0.0);
    }
}

// Wrapper that calls the template with a controller lambda
inline void syncFreezeParamsToController(
    Steinberg::IBStreamer& streamer,
    Steinberg::Vst::EditControllerEx1& controller)
{
    loadFreezeParamsToController(streamer,
        [&controller](Steinberg::Vst::ParamID paramId, double normalizedValue) {
            controller.setParamNormalized(paramId, normalizedValue);
        });
}

} // namespace Iterum
