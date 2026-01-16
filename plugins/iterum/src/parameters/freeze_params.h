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
    // Write placeholder values for backwards compatibility with older versions
    // that expect the legacy freeze parameters in the state
    streamer.writeInt32(1);       // freezeEnabled (always on)
    streamer.writeFloat(500.0f);  // delayTime
    streamer.writeInt32(0);       // timeMode
    streamer.writeInt32(4);       // noteValue
    streamer.writeFloat(0.5f);    // feedback
    streamer.writeFloat(0.0f);    // pitchSemitones
    streamer.writeFloat(0.0f);    // pitchCents
    streamer.writeFloat(0.0f);    // shimmerMix
    streamer.writeFloat(0.5f);    // decay
    streamer.writeFloat(0.3f);    // diffusionAmount
    streamer.writeFloat(0.5f);    // diffusionSize
    streamer.writeInt32(0);       // filterEnabled
    streamer.writeInt32(0);       // filterType
    streamer.writeFloat(1000.0f); // filterCutoff
    streamer.writeFloat(params.dryWet.load(std::memory_order_relaxed));
}

inline void loadFreezeParams(FreezeParams& params, Steinberg::IBStreamer& streamer) {
    // Read and discard legacy freeze parameters for backwards compatibility
    Steinberg::int32 intVal = 0;
    float floatVal = 0.0f;

    streamer.readInt32(intVal);   // freezeEnabled (discarded)
    streamer.readFloat(floatVal); // delayTime (discarded)
    streamer.readInt32(intVal);   // timeMode (discarded)
    streamer.readInt32(intVal);   // noteValue (discarded)
    streamer.readFloat(floatVal); // feedback (discarded)
    streamer.readFloat(floatVal); // pitchSemitones (discarded)
    streamer.readFloat(floatVal); // pitchCents (discarded)
    streamer.readFloat(floatVal); // shimmerMix (discarded)
    streamer.readFloat(floatVal); // decay (discarded)
    streamer.readFloat(floatVal); // diffusionAmount (discarded)
    streamer.readFloat(floatVal); // diffusionSize (discarded)
    streamer.readInt32(intVal);   // filterEnabled (discarded)
    streamer.readInt32(intVal);   // filterType (discarded)
    streamer.readFloat(floatVal); // filterCutoff (discarded)

    // Read actual dryWet parameter
    float dryWet = 0.5f;
    streamer.readFloat(dryWet);
    params.dryWet.store(dryWet, std::memory_order_relaxed);
}

// ==============================================================================
// State Synchronization (Controller -> Processor state sync)
// ==============================================================================

inline void syncFreezeParamsToController(
    const FreezeParams& params,
    Steinberg::Vst::IEditController* controller) {

    using namespace Steinberg::Vst;

    // Dry/wet: 0-1
    controller->setParamNormalized(kFreezeMixId,
        params.dryWet.load(std::memory_order_relaxed));
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

    // Read and discard legacy freeze parameters for backwards compatibility
    streamer.readInt32(intVal);   // freezeEnabled (discarded)
    streamer.readFloat(floatVal); // delayTime (discarded)
    streamer.readInt32(intVal);   // timeMode (discarded)
    streamer.readInt32(intVal);   // noteValue (discarded)
    streamer.readFloat(floatVal); // feedback (discarded)
    streamer.readFloat(floatVal); // pitchSemitones (discarded)
    streamer.readFloat(floatVal); // pitchCents (discarded)
    streamer.readFloat(floatVal); // shimmerMix (discarded)
    streamer.readFloat(floatVal); // decay (discarded)
    streamer.readFloat(floatVal); // diffusionAmount (discarded)
    streamer.readFloat(floatVal); // diffusionSize (discarded)
    streamer.readInt32(intVal);   // filterEnabled (discarded)
    streamer.readInt32(intVal);   // filterType (discarded)
    streamer.readFloat(floatVal); // filterCutoff (discarded)

    // Dry/Wet: 0-1 -> normalized = val
    if (streamer.readFloat(floatVal)) {
        setParam(kFreezeMixId,
            static_cast<double>(floatVal));
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
