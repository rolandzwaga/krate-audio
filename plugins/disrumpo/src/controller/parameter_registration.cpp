// ==============================================================================
// Controller Parameter Registration
// ==============================================================================
// Split from controller.cpp for maintainability.
// Contains: registerGlobalParams, registerSweepParams, registerModulationParams,
//           registerBandParams, registerNodeParams
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "controller/format_helpers.h"
#include "controller/log_range_parameter.h"
#include "dsp/distortion_types.h"
#include "state_codec.h"
#include "ui/note_value_ui.h"
#include <krate/dsp/core/modulation_types.h>

#include "base/source/fstring.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <cstddef>
#include <iterator>
#include <string>
#include <sstream>

namespace Disrumpo {

namespace {

/// RangeParameter over a state_codec.h LinearRange, so the host's plain value and text
/// are the processor's own decode (D-169). `unitScale` shows the range in another unit
/// (100: a 0..2 range as 0..200 %).
Steinberg::Vst::RangeParameter* makeLinearRangeParameter(const Steinberg::Vst::TChar* title,
                                                         Steinberg::Vst::ParamID id,
                                                         const Steinberg::Vst::TChar* units,
                                                         const LinearRange& range, double defaultPlain,
                                                         double unitScale = 1.0) {
    const double minPlain = unitScale * static_cast<double>(range.min);
    const double maxPlain = unitScale * static_cast<double>(range.min + range.span);
    return new Steinberg::Vst::RangeParameter(title, id, units, minPlain, maxPlain, defaultPlain, 0,
                                              Steinberg::Vst::ParameterInfo::kCanAutomate);
}

/// LFO rate [0.01, 20] Hz on the processor's ln curve (lfoRateNormToHz).
LogRangeParameter* makeLfoRateParameter(const Steinberg::Vst::TChar* title, Steinberg::Vst::ParamID id,
                                        double defaultHz) {
    return new LogRangeParameter(title, id, STR16("Hz"), static_cast<double>(kMinLfoRateHz),
                                 static_cast<double>(kMaxLfoRateHz), defaultHz, &lfoRateNormToHz,
                                 &lfoRateHzToNorm, Steinberg::Vst::ParameterInfo::kCanAutomate);
}

/// Note value list: the shared dropdown getNoteValueFromDropdown decodes, starting on
/// its default entry (1/8) (D-092).
Steinberg::Vst::StringListParameter* makeNoteValueParameter(const Steinberg::Vst::TChar* title,
                                                            Steinberg::Vst::ParamID id,
                                                            Steinberg::int32 flags) {
    namespace Shared = Krate::Plugins::Parameters;
    static_assert(std::size(Shared::kNoteValueDropdownStrings) == static_cast<size_t>(kNoteValueCount),
                  "the shared note-value strings must cover the DSP dropdown the processor decodes");
    auto* param = new Steinberg::Vst::StringListParameter(title, id, nullptr, flags);
    for (const auto* entry : Shared::kNoteValueDropdownStrings) param->appendString(entry);
    const double defaultNorm = listIndexToNorm(Shared::kNoteValueDefaultIndex, Shared::kNoteValueDropdownCount);
    param->setNormalized(defaultNorm);
    param->getInfo().defaultNormalizedValue = defaultNorm;
    return param;
}

/// The 26 distortion type names in DistortionType order, from getTypeName: the
/// processor decodes the list index as the enum value (D-093).
void appendDistortionTypeNames(Steinberg::Vst::StringListParameter& param) {
    for (int i = 0; i < kDistortionTypeCount; ++i) {
        Steinberg::Vst::String128 name{};
        asciiToString128(getTypeName(static_cast<DistortionType>(i)), name);
        param.appendString(name);
    }
}

}  // namespace

// ==============================================================================
// Global Parameters
// ==============================================================================

void Controller::registerGlobalParams() {
    // FR-004: Register all global parameters

    // Input Gain: RangeParameter [-24, +24] dB, default 0
    auto* inputGainParam = new Steinberg::Vst::RangeParameter(
        STR16("Input Gain"),
        makeGlobalParamId(GlobalParamType::kGlobalInputGain),
        STR16("dB"),
        -24.0, 24.0, 0.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(inputGainParam);

    // Output Gain: RangeParameter [-24, +24] dB, default 0
    auto* outputGainParam = new Steinberg::Vst::RangeParameter(
        STR16("Output Gain"),
        makeGlobalParamId(GlobalParamType::kGlobalOutputGain),
        STR16("dB"),
        -24.0, 24.0, 0.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(outputGainParam);

    // Mix: RangeParameter [0, 100] %, default 100
    auto* mixParam = new Steinberg::Vst::RangeParameter(
        STR16("Mix"),
        makeGlobalParamId(GlobalParamType::kGlobalMix),
        STR16("%"),
        0.0, 100.0, 100.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(mixParam);

    // Band Count: StringListParameter ["1".."4"], default "4"
    auto* bandCountParam = new Steinberg::Vst::StringListParameter(
        STR16("Band Count"),
        makeGlobalParamId(GlobalParamType::kGlobalBandCount),
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
    );
    for (int i = 1; i <= 4; ++i) {
        Steinberg::Vst::String128 str;
        intToString128(i, str);
        bandCountParam->appendString(str);
    }
    // Default index 3 = "4": the registered default too, or a host reset gives 1 band (D-072)
    bandCountParam->setNormalized(1.0);
    bandCountParam->getInfo().defaultNormalizedValue = 1.0;
    parameters.addParameter(bandCountParam);

    // Oversample Max: StringListParameter ["1x","2x","4x","8x"], default "4x"
    auto* oversampleParam = new Steinberg::Vst::StringListParameter(
        STR16("Oversample Max"),
        makeGlobalParamId(GlobalParamType::kGlobalOversample),
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
    );
    oversampleParam->appendString(STR16("1x"));
    oversampleParam->appendString(STR16("2x"));
    oversampleParam->appendString(STR16("4x"));
    oversampleParam->appendString(STR16("8x"));
    // Default index 2 = "4x": the registered default too, or a host reset gives 1x (D-072)
    oversampleParam->setNormalized(2.0 / 3.0);
    oversampleParam->getInfo().defaultNormalizedValue = 2.0 / 3.0;
    parameters.addParameter(oversampleParam);

    // Spectrum View Mode: StringListParameter ["Wet","Dry","Both"], default "Wet"
    auto* spectrumModeParam = new Steinberg::Vst::StringListParameter(
        STR16("Spectrum Mode"),
        makeGlobalParamId(GlobalParamType::kGlobalSpectrumMode),
        nullptr,
        Steinberg::Vst::ParameterInfo::kNoFlags  // UI-only, not automatable
    );
    spectrumModeParam->appendString(STR16("Wet"));
    spectrumModeParam->appendString(STR16("Dry"));
    spectrumModeParam->appendString(STR16("Both"));
    spectrumModeParam->setNormalized(0.0);  // Default: Wet (index 0)
    parameters.addParameter(spectrumModeParam);

    // T033: Modulation Panel Visible (Spec 012 FR-007, FR-009)
    auto* modPanelParam = new Steinberg::Vst::Parameter(
        STR16("Mod Panel Visible"),
        makeGlobalParamId(GlobalParamType::kGlobalModPanelVisible),
        nullptr,
        0.0,   // Default: hidden
        1,     // stepCount = 1 (boolean)
        Steinberg::Vst::ParameterInfo::kNoFlags  // Not automatable (UI-only)
    );
    parameters.addParameter(modPanelParam);

    // T055: MIDI Learn Active (Spec 012 FR-031)
    auto* midiLearnActiveParam = new Steinberg::Vst::Parameter(
        STR16("MIDI Learn Active"),
        makeGlobalParamId(GlobalParamType::kGlobalMidiLearnActive),
        nullptr,
        0.0,
        1,
        Steinberg::Vst::ParameterInfo::kNoFlags
    );
    parameters.addParameter(midiLearnActiveParam);

    // T055: MIDI Learn Target (Spec 012 FR-031)
    auto* midiLearnTargetParam = new Steinberg::Vst::Parameter(
        STR16("MIDI Learn Target"),
        makeGlobalParamId(GlobalParamType::kGlobalMidiLearnTarget),
        nullptr,
        0.0,
        0,
        Steinberg::Vst::ParameterInfo::kNoFlags
    );
    parameters.addParameter(midiLearnTargetParam);

    // Crossover frequency parameters (3 crossovers for 4 bands)
    const Steinberg::Vst::TChar* crossoverNames[] = {
        STR16("Crossover 1"), STR16("Crossover 2"), STR16("Crossover 3")
    };

    for (int i = 0; i < kMaxBands - 1; ++i) {
        // Default: even log spacing, crossover i at norm (i + 1) / kMaxBands
        const float defaultFreq =
            crossoverNormToHz(static_cast<float>(i + 1) / static_cast<float>(kMaxBands));

        // Log10-mapped like the processor's decode (state_codec.h)
        auto* crossoverParam = new LogRangeParameter(
            crossoverNames[i],
            makeCrossoverParamId(static_cast<uint8_t>(i)),
            STR16("Hz"),
            static_cast<double>(kMinCrossoverHz),
            static_cast<double>(kMaxCrossoverHz),
            static_cast<double>(defaultFreq),
            &crossoverNormToHz,
            &crossoverHzToNorm,
            Steinberg::Vst::ParameterInfo::kCanAutomate
        );
        parameters.addParameter(crossoverParam);
    }
}

// ==============================================================================
// Sweep Parameters
// ==============================================================================

void Controller::registerSweepParams() {
    // FR-004: Register sweep parameters (T4.10)

    // Sweep Enable: boolean toggle
    parameters.addParameter(
        STR16("Sweep Enable"),
        nullptr,
        1,  // stepCount = 1 for boolean
        0.0,  // default off
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeSweepParamId(SweepParamType::kSweepEnable)
    );

    // Sweep Frequency: [20, 20000] Hz, log2-mapped like the processor's decode (state_codec.h)
    auto* sweepFreqParam = new LogRangeParameter(
        STR16("Sweep Frequency"),
        makeSweepParamId(SweepParamType::kSweepFrequency),
        STR16("Hz"),
        static_cast<double>(kNormMinFreqHz), static_cast<double>(kNormMaxFreqHz), 1000.0,
        &sweepFreqNormToHz,
        &sweepFreqHzToNorm,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(sweepFreqParam);

    // Sweep Width: [0.5, 4.0] octaves, default 1.5 oct as the processor starts (D-101)
    parameters.addParameter(makeLinearRangeParameter(
        STR16("Sweep Width"), makeSweepParamId(SweepParamType::kSweepWidth), STR16("oct"),
        kSweepWidthOctRange, 1.5));

    // Sweep Intensity: [0, 200] %, the processor's norm * 2 (FR-SWEEP-002); default 50 % (D-101)
    parameters.addParameter(makeLinearRangeParameter(
        STR16("Sweep Intensity"), makeSweepParamId(SweepParamType::kSweepIntensity), STR16("%"),
        kSweepIntensityRange, 50.0, 100.0));

    // Sweep Morph Link: StringListParameter
    auto* morphLinkParam = new Steinberg::Vst::StringListParameter(
        STR16("Sweep Morph Link"),
        makeSweepParamId(SweepParamType::kSweepMorphLink),
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
    );
    // The 8 MorphLinkMode entries in enum order (D-066)
    morphLinkParam->appendString(STR16("None"));
    morphLinkParam->appendString(STR16("Sweep Freq"));
    morphLinkParam->appendString(STR16("Inverse Sweep"));
    morphLinkParam->appendString(STR16("Ease In"));
    morphLinkParam->appendString(STR16("Ease Out"));
    morphLinkParam->appendString(STR16("Hold-Rise"));
    morphLinkParam->appendString(STR16("Stepped"));
    morphLinkParam->appendString(STR16("Custom"));
    parameters.addParameter(morphLinkParam);

    // Sweep Falloff: StringListParameter
    auto* falloffParam = new Steinberg::Vst::StringListParameter(
        STR16("Sweep Falloff"),
        makeSweepParamId(SweepParamType::kSweepFalloff),
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
    );
    falloffParam->appendString(STR16("Hard"));
    falloffParam->appendString(STR16("Soft"));
    parameters.addParameter(falloffParam);

    // Sweep LFO Parameters (FR-024, FR-025)
    parameters.addParameter(STR16("Sweep LFO Enable"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeSweepParamId(SweepParamType::kSweepLFOEnable));

    parameters.addParameter(
        makeLfoRateParameter(STR16("Sweep LFO Rate"), makeSweepParamId(SweepParamType::kSweepLFORate), 1.0));

    auto* lfoWaveformParam = new Steinberg::Vst::StringListParameter(
        STR16("Sweep LFO Waveform"), makeSweepParamId(SweepParamType::kSweepLFOWaveform),
        nullptr, Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    lfoWaveformParam->appendString(STR16("Sine"));
    lfoWaveformParam->appendString(STR16("Triangle"));
    lfoWaveformParam->appendString(STR16("Sawtooth"));
    lfoWaveformParam->appendString(STR16("Square"));
    lfoWaveformParam->appendString(STR16("S&H"));
    lfoWaveformParam->appendString(STR16("Random"));
    parameters.addParameter(lfoWaveformParam);

    auto* lfoDepthParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep LFO Depth"), makeSweepParamId(SweepParamType::kSweepLFODepth),
        STR16("%"), 0.0, 100.0, 50.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(lfoDepthParam);

    parameters.addParameter(STR16("Sweep LFO Sync"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeSweepParamId(SweepParamType::kSweepLFOSync));

    parameters.addParameter(makeNoteValueParameter(
        STR16("Sweep LFO Note"), makeSweepParamId(SweepParamType::kSweepLFONoteValue),
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList));

    // Sweep Envelope Follower Parameters (FR-026, FR-027)
    parameters.addParameter(STR16("Sweep Env Enable"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeSweepParamId(SweepParamType::kSweepEnvEnable));

    auto* envAttackParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep Env Attack"), makeSweepParamId(SweepParamType::kSweepEnvAttack),
        STR16("ms"), 1.0, 100.0, 10.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(envAttackParam);

    auto* envReleaseParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep Env Release"), makeSweepParamId(SweepParamType::kSweepEnvRelease),
        STR16("ms"), 10.0, 500.0, 100.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(envReleaseParam);

    auto* envSensParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep Env Sensitivity"), makeSweepParamId(SweepParamType::kSweepEnvSensitivity),
        STR16("%"), 0.0, 100.0, 50.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(envSensParam);

    // Output Parameters (Processor -> Controller)
    parameters.addParameter(STR16("Sweep Mod Freq"), nullptr, 0, 0.5,
        Steinberg::Vst::ParameterInfo::kIsReadOnly, kSweepModulatedFrequencyOutputId);

    // Reported latency (D-026, T2.10): written by the processor when it changes; the
    // controller's setParamNormalized turns it into restartComponent(kLatencyChanged)
    parameters.addParameter(STR16("Latency"), STR16("samples"), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kIsReadOnly | Steinberg::Vst::ParameterInfo::kIsHidden,
        kLatencyOutputId);

    // Legacy sweep-only MIDI learn (D-094): hidden, no processor action; kept so the ID is never reused
    parameters.addParameter(STR16("Sweep Detected CC"), nullptr, 0, 0.0,
        Steinberg::Vst::ParameterInfo::kIsReadOnly | Steinberg::Vst::ParameterInfo::kIsHidden,
        kSweepDetectedCCOutputId);

    // Custom Curve Parameters (FR-039a, FR-039b, FR-039c)
    auto* curvePointCountParam = new Steinberg::Vst::RangeParameter(
        STR16("Curve Point Count"), makeSweepParamId(SweepParamType::kSweepCustomCurvePointCount),
        nullptr, 2.0, 8.0, 2.0, 6, Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(curvePointCountParam);

    static const Steinberg::Vst::TChar* pointNames[] = {
        STR16("Curve P0 X"), STR16("Curve P0 Y"),
        STR16("Curve P1 X"), STR16("Curve P1 Y"),
        STR16("Curve P2 X"), STR16("Curve P2 Y"),
        STR16("Curve P3 X"), STR16("Curve P3 Y"),
        STR16("Curve P4 X"), STR16("Curve P4 Y"),
        STR16("Curve P5 X"), STR16("Curve P5 Y"),
        STR16("Curve P6 X"), STR16("Curve P6 Y"),
        STR16("Curve P7 X"), STR16("Curve P7 Y")
    };

    for (int p = 0; p < 8; ++p) {
        auto idx = static_cast<size_t>(p);
        float defaultX = 0.0f;
        if (p == 7) defaultX = 1.0f;
        else if (p > 0) defaultX = static_cast<float>(p) / 7.0f;

        auto xType = static_cast<SweepParamType>(
            static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0X) + p * 2);
        parameters.addParameter(pointNames[idx * 2], nullptr, 0, defaultX,
            Steinberg::Vst::ParameterInfo::kCanAutomate, makeSweepParamId(xType));

        auto yType = static_cast<SweepParamType>(
            static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0Y) + p * 2);
        float defaultY = defaultX;
        parameters.addParameter(pointNames[idx * 2 + 1], nullptr, 0, defaultY,
            Steinberg::Vst::ParameterInfo::kCanAutomate, makeSweepParamId(yType));
    }

    // Legacy sweep-only MIDI learn (FR-028, FR-029; D-094): MIDI learn is the context menu's
    // (MidiCCManager + IMidiLearn). Hidden, no processor action; kept so the IDs are never reused.
    parameters.addParameter(STR16("Sweep MIDI Learn"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kIsReadOnly | Steinberg::Vst::ParameterInfo::kIsHidden,
        makeSweepParamId(SweepParamType::kSweepMidiLearnActive));

    auto* midiCCParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep MIDI CC"), makeSweepParamId(SweepParamType::kSweepMidiCCNumber),
        nullptr, 0.0, 128.0, 128.0, 128,
        Steinberg::Vst::ParameterInfo::kIsReadOnly | Steinberg::Vst::ParameterInfo::kIsHidden);
    parameters.addParameter(midiCCParam);
}

// ==============================================================================
// Modulation Parameters
// ==============================================================================

void Controller::registerModulationParams() {
    // spec 008-modulation-system: Register all modulation parameters

    // LFO 1 Parameters
    parameters.addParameter(makeLfoRateParameter(STR16("LFO 1 Rate"), makeModParamId(ModParamType::kLFO1Rate), 1.0));

    auto* lfo1Shape = new Steinberg::Vst::StringListParameter(
        STR16("LFO 1 Shape"), makeModParamId(ModParamType::kLFO1Shape));
    lfo1Shape->appendString(STR16("Sine"));
    lfo1Shape->appendString(STR16("Triangle"));
    lfo1Shape->appendString(STR16("Saw"));
    lfo1Shape->appendString(STR16("Square"));
    lfo1Shape->appendString(STR16("S&H"));
    lfo1Shape->appendString(STR16("Smooth Random"));
    parameters.addParameter(lfo1Shape);

    parameters.addParameter(makeLinearRangeParameter(
        STR16("LFO 1 Phase"), makeModParamId(ModParamType::kLFO1Phase), STR16("deg"), kLfoPhaseDegRange, 0.0));
    parameters.addParameter(STR16("LFO 1 Sync"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kLFO1Sync));

    parameters.addParameter(makeNoteValueParameter(
        STR16("LFO 1 Note Value"), makeModParamId(ModParamType::kLFO1NoteValue),
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList));

    parameters.addParameter(STR16("LFO 1 Unipolar"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kLFO1Unipolar));
    parameters.addParameter(STR16("LFO 1 Retrigger"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kLFO1Retrigger));

    // LFO 2 Parameters
    // 1 Hz as the processor's LFO 2 starts (the old 0.5 Hz default, linear, decoded as 0.012 Hz)
    parameters.addParameter(makeLfoRateParameter(STR16("LFO 2 Rate"), makeModParamId(ModParamType::kLFO2Rate), 1.0));

    auto* lfo2Shape = new Steinberg::Vst::StringListParameter(
        STR16("LFO 2 Shape"), makeModParamId(ModParamType::kLFO2Shape));
    lfo2Shape->appendString(STR16("Sine"));
    lfo2Shape->appendString(STR16("Triangle"));
    lfo2Shape->appendString(STR16("Saw"));
    lfo2Shape->appendString(STR16("Square"));
    lfo2Shape->appendString(STR16("S&H"));
    lfo2Shape->appendString(STR16("Smooth Random"));
    parameters.addParameter(lfo2Shape);

    parameters.addParameter(makeLinearRangeParameter(
        STR16("LFO 2 Phase"), makeModParamId(ModParamType::kLFO2Phase), STR16("deg"), kLfoPhaseDegRange, 0.0));
    parameters.addParameter(STR16("LFO 2 Sync"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kLFO2Sync));

    parameters.addParameter(makeNoteValueParameter(
        STR16("LFO 2 Note Value"), makeModParamId(ModParamType::kLFO2NoteValue),
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList));

    parameters.addParameter(STR16("LFO 2 Unipolar"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kLFO2Unipolar));
    parameters.addParameter(STR16("LFO 2 Retrigger"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kLFO2Retrigger));

    // Envelope Follower Parameters: plain ranges are the processor's decode (D-169), defaults its own
    parameters.addParameter(makeLinearRangeParameter(
        STR16("Env Attack"), makeModParamId(ModParamType::kEnvFollowerAttack), STR16("ms"),
        kEnvFollowerAttackMsRange, 10.0));
    parameters.addParameter(makeLinearRangeParameter(
        STR16("Env Release"), makeModParamId(ModParamType::kEnvFollowerRelease), STR16("ms"),
        kEnvFollowerReleaseMsRange, 100.0));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Env Sensitivity"), makeModParamId(ModParamType::kEnvFollowerSensitivity), STR16("%"),
        0.0, 100.0, 50.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate));

    auto* envSource = new Steinberg::Vst::StringListParameter(
        STR16("Env Source"), makeModParamId(ModParamType::kEnvFollowerSource));
    envSource->appendString(STR16("Input L"));
    envSource->appendString(STR16("Input R"));
    envSource->appendString(STR16("Input Sum"));
    envSource->appendString(STR16("Mid"));
    envSource->appendString(STR16("Side"));
    parameters.addParameter(envSource);

    // Random Source Parameters
    parameters.addParameter(makeLinearRangeParameter(
        STR16("Random Rate"), makeModParamId(ModParamType::kRandomRate), STR16("Hz"), kRandomRateHzRange, 4.0));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Random Smoothness"), makeModParamId(ModParamType::kRandomSmoothness), STR16("%"),
        0.0, 100.0, 0.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate));
    parameters.addParameter(STR16("Random Sync"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kRandomSync));

    // Chaos Source Parameters
    auto* chaosModel = new Steinberg::Vst::StringListParameter(
        STR16("Chaos Model"), makeModParamId(ModParamType::kChaosModel));
    chaosModel->appendString(STR16("Lorenz"));
    chaosModel->appendString(STR16("Rossler"));
    chaosModel->appendString(STR16("Chua"));
    chaosModel->appendString(STR16("Henon"));
    parameters.addParameter(chaosModel);

    parameters.addParameter(STR16("Chaos Speed"), nullptr, 0, 0.048,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kChaosSpeed));
    parameters.addParameter(STR16("Chaos Coupling"), nullptr, 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kChaosCoupling));

    // Sample & Hold Parameters
    auto* shSource = new Steinberg::Vst::StringListParameter(
        STR16("S&H Source"), makeModParamId(ModParamType::kSampleHoldSource));
    shSource->appendString(STR16("Random"));
    shSource->appendString(STR16("LFO 1"));
    shSource->appendString(STR16("LFO 2"));
    shSource->appendString(STR16("External"));
    parameters.addParameter(shSource);

    parameters.addParameter(makeLinearRangeParameter(
        STR16("S&H Rate"), makeModParamId(ModParamType::kSampleHoldRate), STR16("Hz"), kSampleHoldRateHzRange, 4.0));
    parameters.addParameter(makeLinearRangeParameter(
        STR16("S&H Slew"), makeModParamId(ModParamType::kSampleHoldSlew), STR16("ms"), kSampleHoldSlewMsRange, 0.0));

    // Pitch Follower Parameters
    parameters.addParameter(makeLinearRangeParameter(
        STR16("Pitch Min Hz"), makeModParamId(ModParamType::kPitchFollowerMinHz), STR16("Hz"),
        kPitchFollowerMinHzRange, 80.0));
    parameters.addParameter(makeLinearRangeParameter(
        STR16("Pitch Max Hz"), makeModParamId(ModParamType::kPitchFollowerMaxHz), STR16("Hz"),
        kPitchFollowerMaxHzRange, 2000.0));
    parameters.addParameter(STR16("Pitch Confidence"), nullptr, 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kPitchFollowerConfidence));
    parameters.addParameter(makeLinearRangeParameter(
        STR16("Pitch Tracking"), makeModParamId(ModParamType::kPitchFollowerTrackingSpeed), STR16("ms"),
        kPitchFollowerTrackingMsRange, 50.0));

    // Transient Detector Parameters
    parameters.addParameter(STR16("Transient Sensitivity"), nullptr, 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kTransientSensitivity));
    parameters.addParameter(makeLinearRangeParameter(
        STR16("Transient Attack"), makeModParamId(ModParamType::kTransientAttack), STR16("ms"),
        kTransientAttackMsRange, 2.0));
    parameters.addParameter(makeLinearRangeParameter(
        STR16("Transient Decay"), makeModParamId(ModParamType::kTransientDecay), STR16("ms"),
        kTransientDecayMsRange, 50.0));

    // Rungler Parameters (default kept at its registered normalized 0.08)
    parameters.addParameter(makeLinearRangeParameter(
        STR16("Rungler Rate"), makeModParamId(ModParamType::kRunglerRate), STR16("Hz"), kRunglerRateHzRange,
        static_cast<double>(kRunglerRateHzRange.toPlain(0.08f))));
    parameters.addParameter(STR16("Rungler Depth"), nullptr, 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kRunglerDepth));
    parameters.addParameter(STR16("Rungler Bits"), nullptr, 12, 0.333,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kRunglerBits));
    parameters.addParameter(STR16("Rungler Loop"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kRunglerLoop));

    // Macro Parameters (4 macros x 4 params)
    const Steinberg::Vst::TChar* macroNames[] = {
        STR16("Macro 1"), STR16("Macro 2"), STR16("Macro 3"), STR16("Macro 4")
    };
    const ModParamType macroValueTypes[] = {
        ModParamType::kMacro1Value, ModParamType::kMacro2Value,
        ModParamType::kMacro3Value, ModParamType::kMacro4Value
    };
    const ModParamType macroMinTypes[] = {
        ModParamType::kMacro1Min, ModParamType::kMacro2Min,
        ModParamType::kMacro3Min, ModParamType::kMacro4Min
    };
    const ModParamType macroMaxTypes[] = {
        ModParamType::kMacro1Max, ModParamType::kMacro2Max,
        ModParamType::kMacro3Max, ModParamType::kMacro4Max
    };
    const ModParamType macroCurveTypes[] = {
        ModParamType::kMacro1Curve, ModParamType::kMacro2Curve,
        ModParamType::kMacro3Curve, ModParamType::kMacro4Curve
    };

    for (int m = 0; m < 4; ++m) {
        parameters.addParameter(macroNames[m], nullptr, 0, 0.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(macroValueTypes[m]));

        Steinberg::UString128 minStr("Macro ");
        minStr.append(Steinberg::UString128(std::to_string(m + 1).c_str()));
        minStr.append(Steinberg::UString128(" Min"));
        parameters.addParameter(minStr, nullptr, 0, 0.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(macroMinTypes[m]));

        Steinberg::UString128 maxStr("Macro ");
        maxStr.append(Steinberg::UString128(std::to_string(m + 1).c_str()));
        maxStr.append(Steinberg::UString128(" Max"));
        parameters.addParameter(maxStr, nullptr, 0, 1.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(macroMaxTypes[m]));

        auto* macroCurve = new Steinberg::Vst::StringListParameter(
            STR16("Macro Curve"), makeModParamId(macroCurveTypes[m]));
        macroCurve->appendString(STR16("Linear"));
        macroCurve->appendString(STR16("Exponential"));
        macroCurve->appendString(STR16("S-Curve"));
        macroCurve->appendString(STR16("Stepped"));
        parameters.addParameter(macroCurve);
    }

    // Routing Parameters (32 routings x 4 params)
    for (uint8_t r = 0; r < 32; ++r) {
        auto* routeSource = new Steinberg::Vst::StringListParameter(
            STR16("Route Source"), makeRoutingParamId(r, 0));
        routeSource->appendString(STR16("None"));
        routeSource->appendString(STR16("LFO 1"));
        routeSource->appendString(STR16("LFO 2"));
        routeSource->appendString(STR16("Env Follower"));
        routeSource->appendString(STR16("Random"));
        routeSource->appendString(STR16("Macro 1"));
        routeSource->appendString(STR16("Macro 2"));
        routeSource->appendString(STR16("Macro 3"));
        routeSource->appendString(STR16("Macro 4"));
        routeSource->appendString(STR16("Chaos"));
        routeSource->appendString(STR16("Rungler"));
        routeSource->appendString(STR16("S&H"));
        routeSource->appendString(STR16("Pitch"));
        routeSource->appendString(STR16("Transient"));
        parameters.addParameter(routeSource);

        auto* routeDest = new Steinberg::Vst::StringListParameter(
            STR16("Route Dest"), makeRoutingParamId(r, 1));
        routeDest->appendString(STR16("Input Gain"));
        routeDest->appendString(STR16("Output Gain"));
        routeDest->appendString(STR16("Global Mix"));
        routeDest->appendString(STR16("Sweep Freq"));
        routeDest->appendString(STR16("Sweep Width"));
        routeDest->appendString(STR16("Sweep Intensity"));
        for (int b = 1; b <= kMaxBands; ++b) {
            routeDest->appendString(Steinberg::String().printf("Band %d Morph X", b));
            routeDest->appendString(Steinberg::String().printf("Band %d Morph Y", b));
            routeDest->appendString(Steinberg::String().printf("Band %d Drive", b));
            routeDest->appendString(Steinberg::String().printf("Band %d Mix", b));
            routeDest->appendString(Steinberg::String().printf("Band %d Gain", b));
            routeDest->appendString(Steinberg::String().printf("Band %d Pan", b));
            routeDest->appendString(Steinberg::String().printf("Band %d Tone", b));
            routeDest->appendString(Steinberg::String().printf("Band %d Bias", b));
        }
        parameters.addParameter(routeDest);

        // -100..100 %: the processor's amount = norm * 2 - 1 (D-169)
        auto* routeAmount = new Steinberg::Vst::RangeParameter(
            STR16("Route Amount"), makeRoutingParamId(r, 2),
            STR16("%"), -100.0, 100.0, 0.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
        parameters.addParameter(routeAmount);

        auto* routeCurve = new Steinberg::Vst::StringListParameter(
            STR16("Route Curve"), makeRoutingParamId(r, 3));
        routeCurve->appendString(STR16("Linear"));
        routeCurve->appendString(STR16("Exponential"));
        routeCurve->appendString(STR16("S-Curve"));
        routeCurve->appendString(STR16("Stepped"));
        parameters.addParameter(routeCurve);
    }
}

// ==============================================================================
// Band Parameters
// ==============================================================================

void Controller::registerBandParams() {
    // This method is unchanged from the original - see original controller.cpp
    // for the full implementation. It registers all per-band parameters for 4 bands.
    // (The full body is preserved exactly as it was in controller.cpp lines 2307-2630)

    // FR-005: Register per-band parameters for 4 bands

    const Steinberg::Vst::TChar* bandGainNames[] = {
        STR16("Band 1 Gain"), STR16("Band 2 Gain"), STR16("Band 3 Gain"), STR16("Band 4 Gain")
    };
    const Steinberg::Vst::TChar* bandPanNames[] = {
        STR16("Band 1 Pan"), STR16("Band 2 Pan"), STR16("Band 3 Pan"), STR16("Band 4 Pan")
    };
    const Steinberg::Vst::TChar* bandSoloNames[] = {
        STR16("Band 1 Solo"), STR16("Band 2 Solo"), STR16("Band 3 Solo"), STR16("Band 4 Solo")
    };
    const Steinberg::Vst::TChar* bandBypassNames[] = {
        STR16("Band 1 Bypass"), STR16("Band 2 Bypass"), STR16("Band 3 Bypass"), STR16("Band 4 Bypass")
    };
    const Steinberg::Vst::TChar* bandMuteNames[] = {
        STR16("Band 1 Mute"), STR16("Band 2 Mute"), STR16("Band 3 Mute"), STR16("Band 4 Mute")
    };
    const Steinberg::Vst::TChar* bandMorphXNames[] = {
        STR16("Band 1 Morph X"), STR16("Band 2 Morph X"), STR16("Band 3 Morph X"), STR16("Band 4 Morph X")
    };
    const Steinberg::Vst::TChar* bandMorphYNames[] = {
        STR16("Band 1 Morph Y"), STR16("Band 2 Morph Y"), STR16("Band 3 Morph Y"), STR16("Band 4 Morph Y")
    };
    const Steinberg::Vst::TChar* bandMorphModeNames[] = {
        STR16("Band 1 Morph Mode"), STR16("Band 2 Morph Mode"), STR16("Band 3 Morph Mode"), STR16("Band 4 Morph Mode")
    };
    const Steinberg::Vst::TChar* bandExpandedNames[] = {
        STR16("Band 1 Expanded"), STR16("Band 2 Expanded"), STR16("Band 3 Expanded"), STR16("Band 4 Expanded")
    };

    for (int b = 0; b < kMaxBands; ++b) {
        auto* gainParam = new Steinberg::Vst::RangeParameter(
            bandGainNames[b], makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandGain),
            STR16("dB"), static_cast<double>(kMinBandGainDb), static_cast<double>(kMaxBandGainDb),
            0.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
        parameters.addParameter(gainParam);

        auto* panParam = new Steinberg::Vst::RangeParameter(
            bandPanNames[b], makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandPan),
            STR16(""), -1.0, 1.0, 0.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
        parameters.addParameter(panParam);

        parameters.addParameter(bandSoloNames[b], nullptr, 1, 0.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandSolo));
        parameters.addParameter(bandBypassNames[b], nullptr, 1, 0.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandBypass));
        parameters.addParameter(bandMuteNames[b], nullptr, 1, 0.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMute));

        auto* morphXParam = new Steinberg::Vst::RangeParameter(
            bandMorphXNames[b], makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMorphX),
            STR16(""), 0.0, 1.0, 0.5, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
        parameters.addParameter(morphXParam);

        auto* morphYParam = new Steinberg::Vst::RangeParameter(
            bandMorphYNames[b], makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMorphY),
            STR16(""), 0.0, 1.0, 0.5, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
        parameters.addParameter(morphYParam);

        auto* morphModeParam = new Steinberg::Vst::StringListParameter(
            bandMorphModeNames[b], makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMorphMode),
            nullptr, Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
        morphModeParam->appendString(STR16("1D Linear"));
        morphModeParam->appendString(STR16("2D Planar"));
        morphModeParam->appendString(STR16("2D Radial"));
        parameters.addParameter(morphModeParam);

        static const Steinberg::Vst::TChar* activeNodesParamNames[] = {
            STR16("Band 1 Active Nodes"), STR16("Band 2 Active Nodes"),
            STR16("Band 3 Active Nodes"), STR16("Band 4 Active Nodes")
        };
        auto* activeNodesParam = new Steinberg::Vst::StringListParameter(
            activeNodesParamNames[b], makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandActiveNodes),
            nullptr, Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
        activeNodesParam->appendString(STR16("1"));
        activeNodesParam->appendString(STR16("2"));
        activeNodesParam->appendString(STR16("3"));
        activeNodesParam->appendString(STR16("4"));
        activeNodesParam->setNormalized(0.0);
        parameters.addParameter(activeNodesParam);

        parameters.addParameter(bandExpandedNames[b], nullptr, 1, 0.0,
            Steinberg::Vst::ParameterInfo::kNoFlags,
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandExpanded));

        static const Steinberg::Vst::TChar* morphSmoothingNames[] = {
            STR16("Band 1 Morph Smoothing"), STR16("Band 2 Morph Smoothing"),
            STR16("Band 3 Morph Smoothing"), STR16("Band 4 Morph Smoothing")
        };
        auto* morphSmoothingParam = new Steinberg::Vst::RangeParameter(
            morphSmoothingNames[b], makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMorphSmoothing),
            STR16("ms"), 0.0, 500.0, 0.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
        parameters.addParameter(morphSmoothingParam);

        static const Steinberg::Vst::TChar* morphXLinkNames[] = {
            STR16("Band 1 Morph X Link"), STR16("Band 2 Morph X Link"),
            STR16("Band 3 Morph X Link"), STR16("Band 4 Morph X Link")
        };
        auto* morphXLinkParam = new Steinberg::Vst::StringListParameter(
            morphXLinkNames[b], makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMorphXLink),
            nullptr, Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
        morphXLinkParam->appendString(STR16("None"));
        morphXLinkParam->appendString(STR16("Sweep Freq"));
        morphXLinkParam->appendString(STR16("Inverse Sweep"));
        morphXLinkParam->appendString(STR16("Ease In"));
        morphXLinkParam->appendString(STR16("Ease Out"));
        morphXLinkParam->appendString(STR16("Hold-Rise"));
        morphXLinkParam->appendString(STR16("Stepped"));
        morphXLinkParam->appendString(STR16("Custom"));
        parameters.addParameter(morphXLinkParam);

        static const Steinberg::Vst::TChar* morphYLinkNames[] = {
            STR16("Band 1 Morph Y Link"), STR16("Band 2 Morph Y Link"),
            STR16("Band 3 Morph Y Link"), STR16("Band 4 Morph Y Link")
        };
        auto* morphYLinkParam = new Steinberg::Vst::StringListParameter(
            morphYLinkNames[b], makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMorphYLink),
            nullptr, Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
        morphYLinkParam->appendString(STR16("None"));
        morphYLinkParam->appendString(STR16("Sweep Freq"));
        morphYLinkParam->appendString(STR16("Inverse Sweep"));
        morphYLinkParam->appendString(STR16("Ease In"));
        morphYLinkParam->appendString(STR16("Ease Out"));
        morphYLinkParam->appendString(STR16("Hold-Rise"));
        morphYLinkParam->appendString(STR16("Stepped"));
        morphYLinkParam->appendString(STR16("Custom"));
        parameters.addParameter(morphYLinkParam);

        static const Steinberg::Vst::TChar* selectedNodeNames[] = {
            STR16("Band 1 Selected Node"), STR16("Band 2 Selected Node"),
            STR16("Band 3 Selected Node"), STR16("Band 4 Selected Node")
        };
        auto* selectedNodeParam = new Steinberg::Vst::StringListParameter(
            selectedNodeNames[b], makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandSelectedNode),
            nullptr, Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
        selectedNodeParam->appendString(STR16("Node A"));
        selectedNodeParam->appendString(STR16("Node B"));
        selectedNodeParam->appendString(STR16("Node C"));
        selectedNodeParam->appendString(STR16("Node D"));
        parameters.addParameter(selectedNodeParam);

        static const Steinberg::Vst::TChar* displayedTypeNames[] = {
            STR16("Band 1 Displayed Type"), STR16("Band 2 Displayed Type"),
            STR16("Band 3 Displayed Type"), STR16("Band 4 Displayed Type")
        };
        auto* displayedTypeParam = new Steinberg::Vst::StringListParameter(
            displayedTypeNames[b], makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandDisplayedType),
            nullptr, Steinberg::Vst::ParameterInfo::kIsList);
        appendDistortionTypeNames(*displayedTypeParam);
        parameters.addParameter(displayedTypeParam);

        {
            auto band = static_cast<uint8_t>(b);
            parameters.addParameter(new Steinberg::Vst::RangeParameter(
                STR16("Displayed Drive"), makeBandParamId(band, BandParamType::kBandDisplayedDrive),
                STR16(""), 0.0, 10.0, 1.0, 0, Steinberg::Vst::ParameterInfo::kNoFlags));
            parameters.addParameter(new Steinberg::Vst::RangeParameter(
                STR16("Displayed Mix"), makeBandParamId(band, BandParamType::kBandDisplayedMix),
                STR16("%"), 0.0, 100.0, 100.0, 0, Steinberg::Vst::ParameterInfo::kNoFlags));
            parameters.addParameter(new Steinberg::Vst::RangeParameter(
                STR16("Displayed Tone"), makeBandParamId(band, BandParamType::kBandDisplayedTone),
                STR16("Hz"), 200.0, 8000.0, 4000.0, 0, Steinberg::Vst::ParameterInfo::kNoFlags));
            parameters.addParameter(new Steinberg::Vst::RangeParameter(
                STR16("Displayed Bias"), makeBandParamId(band, BandParamType::kBandDisplayedBias),
                STR16(""), -1.0, 1.0, 0.0, 0, Steinberg::Vst::ParameterInfo::kNoFlags));

            for (int s = 0; s < 10; ++s) {
                auto proxyType = static_cast<BandParamType>(
                    static_cast<uint8_t>(BandParamType::kBandDisplayedShape0) + s);
                // Continuous for every slot: a step count makes VSTGUI replace the
                // TypeParams menu-items with numeric entries (D-135). Temporal/RingSat
                // slot-2 curve selectors quantise in formatShapeSlot and the processor.
                int32_t stepCount = 0;
                parameters.addParameter(new Steinberg::Vst::RangeParameter(
                    STR16("Displayed Shape"), makeBandParamId(band, proxyType),
                    STR16(""), 0.0, 1.0, 0.5, stepCount, Steinberg::Vst::ParameterInfo::kNoFlags));
            }
        }

        static const Steinberg::Vst::TChar* bandTabViewNames[] = {
            STR16("Band 1 Tab View"), STR16("Band 2 Tab View"),
            STR16("Band 3 Tab View"), STR16("Band 4 Tab View")
        };
        auto* tabViewParam = new Steinberg::Vst::StringListParameter(
            bandTabViewNames[b], makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandTabView),
            nullptr, Steinberg::Vst::ParameterInfo::kNoFlags);
        tabViewParam->appendString(STR16("Main"));
        tabViewParam->appendString(STR16("Shape"));
        parameters.addParameter(tabViewParam);
    }
}

// ==============================================================================
// Node Parameters
// ==============================================================================

void Controller::registerNodeParams() {
    // FR-006: Register per-node parameters for 4 nodes x 4 bands

    static const Steinberg::Vst::TChar* nodeTypeNames[4][4] = {
        { STR16("B1 N1 Type"), STR16("B1 N2 Type"), STR16("B1 N3 Type"), STR16("B1 N4 Type") },
        { STR16("B2 N1 Type"), STR16("B2 N2 Type"), STR16("B2 N3 Type"), STR16("B2 N4 Type") },
        { STR16("B3 N1 Type"), STR16("B3 N2 Type"), STR16("B3 N3 Type"), STR16("B3 N4 Type") },
        { STR16("B4 N1 Type"), STR16("B4 N2 Type"), STR16("B4 N3 Type"), STR16("B4 N4 Type") }
    };
    static const Steinberg::Vst::TChar* nodeDriveNames[4][4] = {
        { STR16("B1 N1 Drive"), STR16("B1 N2 Drive"), STR16("B1 N3 Drive"), STR16("B1 N4 Drive") },
        { STR16("B2 N1 Drive"), STR16("B2 N2 Drive"), STR16("B2 N3 Drive"), STR16("B2 N4 Drive") },
        { STR16("B3 N1 Drive"), STR16("B3 N2 Drive"), STR16("B3 N3 Drive"), STR16("B3 N4 Drive") },
        { STR16("B4 N1 Drive"), STR16("B4 N2 Drive"), STR16("B4 N3 Drive"), STR16("B4 N4 Drive") }
    };
    static const Steinberg::Vst::TChar* nodeMixNames[4][4] = {
        { STR16("B1 N1 Mix"), STR16("B1 N2 Mix"), STR16("B1 N3 Mix"), STR16("B1 N4 Mix") },
        { STR16("B2 N1 Mix"), STR16("B2 N2 Mix"), STR16("B2 N3 Mix"), STR16("B2 N4 Mix") },
        { STR16("B3 N1 Mix"), STR16("B3 N2 Mix"), STR16("B3 N3 Mix"), STR16("B3 N4 Mix") },
        { STR16("B4 N1 Mix"), STR16("B4 N2 Mix"), STR16("B4 N3 Mix"), STR16("B4 N4 Mix") }
    };
    static const Steinberg::Vst::TChar* nodeToneNames[4][4] = {
        { STR16("B1 N1 Tone"), STR16("B1 N2 Tone"), STR16("B1 N3 Tone"), STR16("B1 N4 Tone") },
        { STR16("B2 N1 Tone"), STR16("B2 N2 Tone"), STR16("B2 N3 Tone"), STR16("B2 N4 Tone") },
        { STR16("B3 N1 Tone"), STR16("B3 N2 Tone"), STR16("B3 N3 Tone"), STR16("B3 N4 Tone") },
        { STR16("B4 N1 Tone"), STR16("B4 N2 Tone"), STR16("B4 N3 Tone"), STR16("B4 N4 Tone") }
    };
    static const Steinberg::Vst::TChar* nodeBiasNames[4][4] = {
        { STR16("B1 N1 Bias"), STR16("B1 N2 Bias"), STR16("B1 N3 Bias"), STR16("B1 N4 Bias") },
        { STR16("B2 N1 Bias"), STR16("B2 N2 Bias"), STR16("B2 N3 Bias"), STR16("B2 N4 Bias") },
        { STR16("B3 N1 Bias"), STR16("B3 N2 Bias"), STR16("B3 N3 Bias"), STR16("B3 N4 Bias") },
        { STR16("B4 N1 Bias"), STR16("B4 N2 Bias"), STR16("B4 N3 Bias"), STR16("B4 N4 Bias") }
    };
    static const Steinberg::Vst::TChar* nodeFoldsNames[4][4] = {
        { STR16("B1 N1 Folds"), STR16("B1 N2 Folds"), STR16("B1 N3 Folds"), STR16("B1 N4 Folds") },
        { STR16("B2 N1 Folds"), STR16("B2 N2 Folds"), STR16("B2 N3 Folds"), STR16("B2 N4 Folds") },
        { STR16("B3 N1 Folds"), STR16("B3 N2 Folds"), STR16("B3 N3 Folds"), STR16("B3 N4 Folds") },
        { STR16("B4 N1 Folds"), STR16("B4 N2 Folds"), STR16("B4 N3 Folds"), STR16("B4 N4 Folds") }
    };
    static const Steinberg::Vst::TChar* nodeBitDepthNames[4][4] = {
        { STR16("B1 N1 BitDepth"), STR16("B1 N2 BitDepth"), STR16("B1 N3 BitDepth"), STR16("B1 N4 BitDepth") },
        { STR16("B2 N1 BitDepth"), STR16("B2 N2 BitDepth"), STR16("B2 N3 BitDepth"), STR16("B2 N4 BitDepth") },
        { STR16("B3 N1 BitDepth"), STR16("B3 N2 BitDepth"), STR16("B3 N3 BitDepth"), STR16("B3 N4 BitDepth") },
        { STR16("B4 N1 BitDepth"), STR16("B4 N2 BitDepth"), STR16("B4 N3 BitDepth"), STR16("B4 N4 BitDepth") }
    };

    for (int b = 0; b < kMaxBands; ++b) {
        for (int n = 0; n < 4; ++n) {
            auto* typeParam = new Steinberg::Vst::StringListParameter(
                nodeTypeNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeType),
                nullptr, Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
            appendDistortionTypeNames(*typeParam);
            parameters.addParameter(typeParam);

            auto* driveParam = new Steinberg::Vst::RangeParameter(
                nodeDriveNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeDrive),
                STR16(""), 0.0, 10.0, 1.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
            parameters.addParameter(driveParam);

            auto* nodeMixParam = new Steinberg::Vst::RangeParameter(
                nodeMixNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeMix),
                STR16("%"), 0.0, 100.0, 100.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
            parameters.addParameter(nodeMixParam);

            auto* toneParam = new Steinberg::Vst::RangeParameter(
                nodeToneNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeTone),
                STR16("Hz"), 200.0, 8000.0, 4000.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
            parameters.addParameter(toneParam);

            auto* biasParam = new Steinberg::Vst::RangeParameter(
                nodeBiasNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeBias),
                STR16(""), -1.0, 1.0, 0.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
            parameters.addParameter(biasParam);

            auto* foldsParam = new Steinberg::Vst::RangeParameter(
                nodeFoldsNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeFolds),
                STR16(""), 1.0, 12.0, 2.0, 11, Steinberg::Vst::ParameterInfo::kCanAutomate);
            parameters.addParameter(foldsParam);

            auto* bitDepthParam = new Steinberg::Vst::RangeParameter(
                nodeBitDepthNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeBitDepth),
                STR16("bit"), 4.0, 24.0, 16.0, 20, Steinberg::Vst::ParameterInfo::kCanAutomate);
            parameters.addParameter(bitDepthParam);

            for (int s = 0; s < 10; ++s) {
                auto shapeType = static_cast<NodeParamType>(
                    static_cast<uint8_t>(NodeParamType::kNodeShape0) + s);
                Steinberg::UString128 shapeName("B");
                shapeName.append(Steinberg::UString128(std::to_string(b + 1).c_str()));
                shapeName.append(Steinberg::UString128(" N"));
                shapeName.append(Steinberg::UString128(std::to_string(n + 1).c_str()));
                shapeName.append(Steinberg::UString128(" Shape "));
                shapeName.append(Steinberg::UString128(std::to_string(s).c_str()));
                int32_t stepCount = 0;  // continuous, like the band proxies above (D-135)
                auto* shapeParam = new Steinberg::Vst::RangeParameter(
                    shapeName,
                    makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), shapeType),
                    STR16(""), 0.0, 1.0, 0.5, stepCount, Steinberg::Vst::ParameterInfo::kCanAutomate);
                parameters.addParameter(shapeParam);
            }
        }
    }
}

} // namespace Disrumpo
