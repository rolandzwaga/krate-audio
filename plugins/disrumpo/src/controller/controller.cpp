// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "version.h"
#include "dsp/band_state.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <cmath>

namespace Disrumpo {

// ==============================================================================
// IPluginBase
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    // Always call parent first
    Steinberg::tresult result = EditControllerEx1::initialize(context);
    if (result != Steinberg::kResultTrue) {
        return result;
    }

    // ==========================================================================
    // FR-015: Register skeleton parameters
    // Constitution Principle V: All values are normalized 0.0 to 1.0
    // ==========================================================================

    // Input Gain: 0.0-1.0 (normalized), default 0.5 (0 dB)
    parameters.addParameter(
        STR16("Input Gain"),           // title
        STR16("dB"),                   // units
        0,                             // stepCount (0 = continuous)
        0.5,                           // defaultValueNormalized
        Steinberg::Vst::ParameterInfo::kCanAutomate,  // flags
        kInputGainId                   // tag (parameter ID)
    );

    // Output Gain: 0.0-1.0 (normalized), default 0.5 (0 dB)
    parameters.addParameter(
        STR16("Output Gain"),          // title
        STR16("dB"),                   // units
        0,                             // stepCount (0 = continuous)
        0.5,                           // defaultValueNormalized
        Steinberg::Vst::ParameterInfo::kCanAutomate,  // flags
        kOutputGainId                  // tag (parameter ID)
    );

    // Global Mix: 0.0-1.0 (normalized), default 1.0 (100% wet)
    parameters.addParameter(
        STR16("Mix"),                  // title
        STR16("%"),                    // units
        0,                             // stepCount (0 = continuous)
        1.0,                           // defaultValueNormalized
        Steinberg::Vst::ParameterInfo::kCanAutomate,  // flags
        kGlobalMixId                   // tag (parameter ID)
    );

    // ==========================================================================
    // Band Management Parameters (spec 002-band-management)
    // ==========================================================================

    // Band Count: discrete parameter [1, 8], default 4
    auto* bandCountParam = new Steinberg::Vst::RangeParameter(
        STR16("Band Count"),           // title
        kBandCountId,                  // tag
        STR16(""),                     // units
        1.0,                           // minPlain
        8.0,                           // maxPlain
        4.0,                           // defaultValuePlain
        7,                             // stepCount (8 values: 1-8)
        Steinberg::Vst::ParameterInfo::kCanAutomate
    );
    parameters.addParameter(bandCountParam);

    // Per-band parameters (8 bands)
    // Use char16_t literals for VST3 TChar compatibility
    const Steinberg::Vst::TChar* bandGainNames[] = {
        STR16("Band 1 Gain"), STR16("Band 2 Gain"), STR16("Band 3 Gain"), STR16("Band 4 Gain"),
        STR16("Band 5 Gain"), STR16("Band 6 Gain"), STR16("Band 7 Gain"), STR16("Band 8 Gain")
    };
    const Steinberg::Vst::TChar* bandPanNames[] = {
        STR16("Band 1 Pan"), STR16("Band 2 Pan"), STR16("Band 3 Pan"), STR16("Band 4 Pan"),
        STR16("Band 5 Pan"), STR16("Band 6 Pan"), STR16("Band 7 Pan"), STR16("Band 8 Pan")
    };
    const Steinberg::Vst::TChar* bandSoloNames[] = {
        STR16("Band 1 Solo"), STR16("Band 2 Solo"), STR16("Band 3 Solo"), STR16("Band 4 Solo"),
        STR16("Band 5 Solo"), STR16("Band 6 Solo"), STR16("Band 7 Solo"), STR16("Band 8 Solo")
    };
    const Steinberg::Vst::TChar* bandBypassNames[] = {
        STR16("Band 1 Bypass"), STR16("Band 2 Bypass"), STR16("Band 3 Bypass"), STR16("Band 4 Bypass"),
        STR16("Band 5 Bypass"), STR16("Band 6 Bypass"), STR16("Band 7 Bypass"), STR16("Band 8 Bypass")
    };
    const Steinberg::Vst::TChar* bandMuteNames[] = {
        STR16("Band 1 Mute"), STR16("Band 2 Mute"), STR16("Band 3 Mute"), STR16("Band 4 Mute"),
        STR16("Band 5 Mute"), STR16("Band 6 Mute"), STR16("Band 7 Mute"), STR16("Band 8 Mute")
    };

    for (int b = 0; b < kMaxBands; ++b) {
        // Band Gain: [-24, +24] dB, default 0 dB
        auto* gainParam = new Steinberg::Vst::RangeParameter(
            bandGainNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandGain),
            STR16("dB"),
            static_cast<double>(kMinBandGainDb),  // -24
            static_cast<double>(kMaxBandGainDb),  // +24
            0.0,                                   // default 0 dB
            0,                                     // stepCount (continuous)
            Steinberg::Vst::ParameterInfo::kCanAutomate
        );
        parameters.addParameter(gainParam);

        // Band Pan: [-1, +1], default 0 (center)
        auto* panParam = new Steinberg::Vst::RangeParameter(
            bandPanNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandPan),
            STR16(""),
            -1.0,   // minPlain
            1.0,    // maxPlain
            0.0,    // defaultValuePlain (center)
            0,      // stepCount (continuous)
            Steinberg::Vst::ParameterInfo::kCanAutomate
        );
        parameters.addParameter(panParam);

        // Band Solo: boolean, default off
        parameters.addParameter(
            bandSoloNames[b],
            nullptr,    // units
            1,          // stepCount (boolean: 0 or 1)
            0.0,        // default off
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandSolo)
        );

        // Band Bypass: boolean, default off
        parameters.addParameter(
            bandBypassNames[b],
            nullptr,
            1,
            0.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandBypass)
        );

        // Band Mute: boolean, default off
        parameters.addParameter(
            bandMuteNames[b],
            nullptr,
            1,
            0.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMute)
        );
    }

    // Crossover frequency parameters (7 crossovers for 8 bands)
    const Steinberg::Vst::TChar* crossoverNames[] = {
        STR16("Crossover 1"), STR16("Crossover 2"), STR16("Crossover 3"), STR16("Crossover 4"),
        STR16("Crossover 5"), STR16("Crossover 6"), STR16("Crossover 7")
    };

    for (int i = 0; i < kMaxBands - 1; ++i) {
        // Calculate default frequency using logarithmic distribution
        const float logMin = std::log10(kMinCrossoverHz);
        const float logMax = std::log10(kMaxCrossoverHz);
        const float step = (logMax - logMin) / static_cast<float>(kMaxBands);
        const float logDefault = logMin + step * static_cast<float>(i + 1);
        const float defaultFreq = std::pow(10.0f, logDefault);

        auto* crossoverParam = new Steinberg::Vst::RangeParameter(
            crossoverNames[i],
            makeCrossoverParamId(static_cast<uint8_t>(i)),
            STR16("Hz"),
            static_cast<double>(kMinCrossoverHz),   // 20 Hz
            static_cast<double>(kMaxCrossoverHz),   // 20000 Hz
            static_cast<double>(defaultFreq),
            0,
            Steinberg::Vst::ParameterInfo::kCanAutomate
        );
        parameters.addParameter(crossoverParam);
    }

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::terminate() {
    // Cleanup any resources allocated in initialize()
    return EditControllerEx1::terminate();
}

// ==============================================================================
// IEditController
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::setComponentState(Steinberg::IBStream* state) {
    // FR-016: Sync from processor state
    // This method receives the processor's state and synchronizes the controller

    if (!state) {
        return Steinberg::kResultFalse;
    }

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read version first (same format as Processor::setState)
    int32_t version = 0;
    if (!streamer.readInt32(version)) {
        return Steinberg::kResultFalse;
    }

    if (version < 1) {
        return Steinberg::kResultFalse;
    }

    // Read parameters in same order as Processor
    float inputGain = 0.5f;
    float outputGain = 0.5f;
    float globalMix = 1.0f;

    if (!streamer.readFloat(inputGain)) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.readFloat(outputGain)) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.readFloat(globalMix)) {
        return Steinberg::kResultFalse;
    }

    // Update controller's parameter values (for UI display)
    setParamNormalized(kInputGainId, inputGain);
    setParamNormalized(kOutputGainId, outputGain);
    setParamNormalized(kGlobalMixId, globalMix);

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::getState(Steinberg::IBStream* state) {
    // Save controller-specific state (UI settings, etc.)
    // For the skeleton, we have no controller-specific state to save
    // Just write a version marker for future extensibility

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Write controller state version (separate from preset version)
    if (!streamer.writeInt32(1)) {
        return Steinberg::kResultFalse;
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::setState(Steinberg::IBStream* state) {
    // Restore controller-specific state
    // For the skeleton, we have no controller-specific state to restore

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read controller state version
    int32_t version = 0;
    if (!streamer.readInt32(version)) {
        // No controller state is OK - just use defaults
        return Steinberg::kResultOk;
    }

    // Future: handle version migration for controller-specific settings

    return Steinberg::kResultOk;
}

Steinberg::IPlugView* PLUGIN_API Controller::createView(Steinberg::FIDString name) {
    // FR-017: Return nullptr (no UI in skeleton)
    // UI will be added in Week 4-5 per roadmap

    (void)name;  // Suppress unused parameter warning
    return nullptr;
}

} // namespace Disrumpo
