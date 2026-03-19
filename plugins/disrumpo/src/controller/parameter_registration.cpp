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
#include <krate/dsp/core/modulation_types.h>

#include "base/source/fstring.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <cmath>
#include <string>
#include <sstream>

namespace Disrumpo {

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
    bandCountParam->setNormalized(1.0);  // Default to index 3 = "4" (3/3 steps)
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
    oversampleParam->setNormalized(2.0 / 3.0);  // Default to index 2 = "4x"
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
        const float logMin = std::log10(kMinCrossoverHz);
        const float logMax = std::log10(kMaxCrossoverHz);
        const float step = (logMax - logMin) / static_cast<float>(kMaxBands);
        const float logDefault = logMin + step * static_cast<float>(i + 1);
        const float defaultFreq = std::pow(10.0f, logDefault);

        auto* crossoverParam = new Steinberg::Vst::RangeParameter(
            crossoverNames[i],
            makeCrossoverParamId(static_cast<uint8_t>(i)),
            STR16("Hz"),
            static_cast<double>(kMinCrossoverHz),
            static_cast<double>(kMaxCrossoverHz),
            static_cast<double>(defaultFreq),
            0,
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

    // Sweep Frequency: RangeParameter [20, 20000] Hz, log scale
    auto* sweepFreqParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep Frequency"),
        makeSweepParamId(SweepParamType::kSweepFrequency),
        STR16("Hz"),
        20.0, 20000.0, 1000.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(sweepFreqParam);

    // Sweep Width: RangeParameter [0.5, 4.0] octaves
    auto* sweepWidthParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep Width"),
        makeSweepParamId(SweepParamType::kSweepWidth),
        STR16("oct"),
        0.5, 4.0, 1.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(sweepWidthParam);

    // Sweep Intensity: RangeParameter [0, 100] %
    auto* sweepIntensityParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep Intensity"),
        makeSweepParamId(SweepParamType::kSweepIntensity),
        STR16("%"),
        0.0, 100.0, 50.0,
        0,
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(sweepIntensityParam);

    // Sweep Morph Link: StringListParameter
    auto* morphLinkParam = new Steinberg::Vst::StringListParameter(
        STR16("Sweep Morph Link"),
        makeSweepParamId(SweepParamType::kSweepMorphLink),
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
    );
    morphLinkParam->appendString(STR16("None"));
    morphLinkParam->appendString(STR16("Linear"));
    morphLinkParam->appendString(STR16("Inverse"));
    morphLinkParam->appendString(STR16("Ease In"));
    morphLinkParam->appendString(STR16("Ease Out"));
    morphLinkParam->appendString(STR16("Ease In-Out"));
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

    auto* lfoRateParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep LFO Rate"), makeSweepParamId(SweepParamType::kSweepLFORate),
        STR16("Hz"), 0.01, 20.0, 1.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(lfoRateParam);

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

    auto* lfoNoteParam = new Steinberg::Vst::StringListParameter(
        STR16("Sweep LFO Note"), makeSweepParamId(SweepParamType::kSweepLFONoteValue),
        nullptr, Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList);
    for (const auto* n : {"1/64T","1/64","1/64D","1/32T","1/32","1/32D",
                           "1/16T","1/16","1/16D","1/8T","1/8","1/8D",
                           "1/4T","1/4","1/4D","1/2T","1/2","1/2D",
                           "1/1T","1/1","1/1D","2/1T","2/1","2/1D",
                           "3/1T","3/1","3/1D","4/1T","4/1","4/1D"})
        lfoNoteParam->appendString(Steinberg::String(n));
    parameters.addParameter(lfoNoteParam);

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

    parameters.addParameter(STR16("Sweep Detected CC"), nullptr, 0, 0.0,
        Steinberg::Vst::ParameterInfo::kIsReadOnly, kSweepDetectedCCOutputId);

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

    // MIDI Parameters (FR-028, FR-029)
    parameters.addParameter(STR16("Sweep MIDI Learn"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate,
        makeSweepParamId(SweepParamType::kSweepMidiLearnActive));

    auto* midiCCParam = new Steinberg::Vst::RangeParameter(
        STR16("Sweep MIDI CC"), makeSweepParamId(SweepParamType::kSweepMidiCCNumber),
        nullptr, 0.0, 128.0, 128.0, 128, Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(midiCCParam);
}

// ==============================================================================
// Modulation Parameters
// ==============================================================================

void Controller::registerModulationParams() {
    // spec 008-modulation-system: Register all modulation parameters

    // LFO 1 Parameters
    auto* lfo1Rate = new Steinberg::Vst::RangeParameter(
        STR16("LFO 1 Rate"), makeModParamId(ModParamType::kLFO1Rate),
        STR16("Hz"), 0.01, 20.0, 1.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(lfo1Rate);

    auto* lfo1Shape = new Steinberg::Vst::StringListParameter(
        STR16("LFO 1 Shape"), makeModParamId(ModParamType::kLFO1Shape));
    lfo1Shape->appendString(STR16("Sine"));
    lfo1Shape->appendString(STR16("Triangle"));
    lfo1Shape->appendString(STR16("Saw"));
    lfo1Shape->appendString(STR16("Square"));
    lfo1Shape->appendString(STR16("S&H"));
    lfo1Shape->appendString(STR16("Smooth Random"));
    parameters.addParameter(lfo1Shape);

    parameters.addParameter(STR16("LFO 1 Phase"), STR16("deg"), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kLFO1Phase));
    parameters.addParameter(STR16("LFO 1 Sync"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kLFO1Sync));

    auto* lfo1Note = new Steinberg::Vst::StringListParameter(
        STR16("LFO 1 Note Value"), makeModParamId(ModParamType::kLFO1NoteValue));
    for (const auto* n : {"1/64T","1/64","1/64D","1/32T","1/32","1/32D",
                           "1/16T","1/16","1/16D","1/8T","1/8","1/8D",
                           "1/4T","1/4","1/4D","1/2T","1/2","1/2D",
                           "1/1T","1/1","1/1D","2/1T","2/1","2/1D",
                           "3/1T","3/1","3/1D","4/1T","4/1","4/1D"})
        lfo1Note->appendString(Steinberg::String(n));
    parameters.addParameter(lfo1Note);

    parameters.addParameter(STR16("LFO 1 Unipolar"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kLFO1Unipolar));
    parameters.addParameter(STR16("LFO 1 Retrigger"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kLFO1Retrigger));

    // LFO 2 Parameters
    auto* lfo2Rate = new Steinberg::Vst::RangeParameter(
        STR16("LFO 2 Rate"), makeModParamId(ModParamType::kLFO2Rate),
        STR16("Hz"), 0.01, 20.0, 0.5, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
    parameters.addParameter(lfo2Rate);

    auto* lfo2Shape = new Steinberg::Vst::StringListParameter(
        STR16("LFO 2 Shape"), makeModParamId(ModParamType::kLFO2Shape));
    lfo2Shape->appendString(STR16("Sine"));
    lfo2Shape->appendString(STR16("Triangle"));
    lfo2Shape->appendString(STR16("Saw"));
    lfo2Shape->appendString(STR16("Square"));
    lfo2Shape->appendString(STR16("S&H"));
    lfo2Shape->appendString(STR16("Smooth Random"));
    parameters.addParameter(lfo2Shape);

    parameters.addParameter(STR16("LFO 2 Phase"), STR16("deg"), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kLFO2Phase));
    parameters.addParameter(STR16("LFO 2 Sync"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kLFO2Sync));

    auto* lfo2Note = new Steinberg::Vst::StringListParameter(
        STR16("LFO 2 Note Value"), makeModParamId(ModParamType::kLFO2NoteValue));
    for (const auto* n : {"1/64T","1/64","1/64D","1/32T","1/32","1/32D",
                           "1/16T","1/16","1/16D","1/8T","1/8","1/8D",
                           "1/4T","1/4","1/4D","1/2T","1/2","1/2D",
                           "1/1T","1/1","1/1D","2/1T","2/1","2/1D",
                           "3/1T","3/1","3/1D","4/1T","4/1","4/1D"})
        lfo2Note->appendString(Steinberg::String(n));
    parameters.addParameter(lfo2Note);

    parameters.addParameter(STR16("LFO 2 Unipolar"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kLFO2Unipolar));
    parameters.addParameter(STR16("LFO 2 Retrigger"), nullptr, 1, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kLFO2Retrigger));

    // Envelope Follower Parameters
    parameters.addParameter(STR16("Env Attack"), STR16("ms"), 0, 0.091,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kEnvFollowerAttack));
    parameters.addParameter(STR16("Env Release"), STR16("ms"), 0, 0.184,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kEnvFollowerRelease));
    parameters.addParameter(STR16("Env Sensitivity"), STR16("%"), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kEnvFollowerSensitivity));

    auto* envSource = new Steinberg::Vst::StringListParameter(
        STR16("Env Source"), makeModParamId(ModParamType::kEnvFollowerSource));
    envSource->appendString(STR16("Input L"));
    envSource->appendString(STR16("Input R"));
    envSource->appendString(STR16("Input Sum"));
    envSource->appendString(STR16("Mid"));
    envSource->appendString(STR16("Side"));
    parameters.addParameter(envSource);

    // Random Source Parameters
    parameters.addParameter(STR16("Random Rate"), STR16("Hz"), 0, 0.078,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kRandomRate));
    parameters.addParameter(STR16("Random Smoothness"), STR16("%"), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kRandomSmoothness));
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

    parameters.addParameter(STR16("S&H Rate"), STR16("Hz"), 0, 0.078,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kSampleHoldRate));
    parameters.addParameter(STR16("S&H Slew"), STR16("ms"), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kSampleHoldSlew));

    // Pitch Follower Parameters
    parameters.addParameter(STR16("Pitch Min Hz"), STR16("Hz"), 0, 0.125,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kPitchFollowerMinHz));
    parameters.addParameter(STR16("Pitch Max Hz"), STR16("Hz"), 0, 0.375,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kPitchFollowerMaxHz));
    parameters.addParameter(STR16("Pitch Confidence"), nullptr, 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kPitchFollowerConfidence));
    parameters.addParameter(STR16("Pitch Tracking"), STR16("ms"), 0, 0.138,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kPitchFollowerTrackingSpeed));

    // Transient Detector Parameters
    parameters.addParameter(STR16("Transient Sensitivity"), nullptr, 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kTransientSensitivity));
    parameters.addParameter(STR16("Transient Attack"), STR16("ms"), 0, 0.158,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kTransientAttack));
    parameters.addParameter(STR16("Transient Decay"), STR16("ms"), 0, 0.167,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kTransientDecay));

    // Rungler Parameters
    parameters.addParameter(STR16("Rungler Rate"), STR16("Hz"), 0, 0.08,
        Steinberg::Vst::ParameterInfo::kCanAutomate, makeModParamId(ModParamType::kRunglerRate));
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

        auto* routeAmount = new Steinberg::Vst::RangeParameter(
            STR16("Route Amount"), makeRoutingParamId(r, 2),
            STR16("%"), -1.0, 1.0, 0.0, 0, Steinberg::Vst::ParameterInfo::kCanAutomate);
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
        displayedTypeParam->appendString(STR16("Soft Clip"));
        displayedTypeParam->appendString(STR16("Hard Clip"));
        displayedTypeParam->appendString(STR16("Tube"));
        displayedTypeParam->appendString(STR16("Tape"));
        displayedTypeParam->appendString(STR16("Fuzz"));
        displayedTypeParam->appendString(STR16("Asymmetric Fuzz"));
        displayedTypeParam->appendString(STR16("Sine Fold"));
        displayedTypeParam->appendString(STR16("Triangle Fold"));
        displayedTypeParam->appendString(STR16("Serge Fold"));
        displayedTypeParam->appendString(STR16("Full Rectify"));
        displayedTypeParam->appendString(STR16("Half Rectify"));
        displayedTypeParam->appendString(STR16("Bitcrush"));
        displayedTypeParam->appendString(STR16("Sample Reduce"));
        displayedTypeParam->appendString(STR16("Quantize"));
        displayedTypeParam->appendString(STR16("Aliasing"));
        displayedTypeParam->appendString(STR16("Bitwise Mangler"));
        displayedTypeParam->appendString(STR16("Temporal"));
        displayedTypeParam->appendString(STR16("Ring Saturation"));
        displayedTypeParam->appendString(STR16("Feedback"));
        displayedTypeParam->appendString(STR16("Allpass Resonant"));
        displayedTypeParam->appendString(STR16("Chaos"));
        displayedTypeParam->appendString(STR16("Formant"));
        displayedTypeParam->appendString(STR16("Granular"));
        displayedTypeParam->appendString(STR16("Spectral"));
        displayedTypeParam->appendString(STR16("Fractal"));
        displayedTypeParam->appendString(STR16("Stochastic"));
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
                int32_t stepCount = (s == 2) ? 8 : 0;
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

    static const Steinberg::Vst::TChar* kDistortionTypeNames[] = {
        STR16("Soft Clip"), STR16("Hard Clip"), STR16("Tube"), STR16("Tape"),
        STR16("Fuzz"), STR16("Asymmetric Fuzz"), STR16("Sine Fold"),
        STR16("Triangle Fold"), STR16("Serge Fold"), STR16("Full Rectify"),
        STR16("Half Rectify"), STR16("Bitcrush"), STR16("Sample Reduce"),
        STR16("Quantize"), STR16("Aliasing"), STR16("Bitwise Mangler"),
        STR16("Temporal"), STR16("Ring Saturation"), STR16("Feedback"),
        STR16("Allpass Resonant"), STR16("Chaos"), STR16("Formant"),
        STR16("Granular"), STR16("Spectral"), STR16("Fractal"), STR16("Stochastic")
    };

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
            for (auto & kDistortionTypeName : kDistortionTypeNames) {
                typeParam->appendString(kDistortionTypeName);
            }
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
                int32_t stepCount = (s == 2) ? 8 : 0;
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
