// ==============================================================================
// Edit Controller Implementation
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation
// Constitution Principle V: VSTGUI Development
// ==============================================================================

#include "controller.h"
#include "controller/sub_controllers.h"
#include "plugin_ids.h"
#include <krate/dsp/core/modulation_types.h>
#include "version.h"
#include "dsp/band_state.h"
#include "preset/disrumpo_preset_config.h"
#include "controller/views/spectrum_display.h"
#include "controller/views/morph_pad.h"
#include "controller/views/dynamic_node_selector.h"
#include "controller/views/node_editor_border.h"
#include "controller/views/sweep_indicator.h"
#include "controller/views/custom_curve_editor.h"
#include "controller/views/mod_slider.h"
#include "controller/morph_link.h"
#include "controller/animated_expand_controller.h"
#include "controller/keyboard_shortcut_handler.h"
#include "controller/visibility_controllers.h"
#include "controller/format_helpers.h"
#include "controller/custom_buttons.h"
#include "dsp/sweep_morph_link.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"
#include "ui/update_banner_view.h"
#include "update/disrumpo_update_config.h"
#include "platform/accessibility_helper.h"
#include "midi/midi_cc_manager.h"

#include "base/source/fstreamer.h"
#include "base/source/fobject.h"
#include "base/source/fstring.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/iuidescription.h"

#include <cmath>
#include <cstring>
#include <atomic>
#include <functional>
#include <vector>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Disrumpo {

// ==============================================================================
// Custom Button onClick implementations (need Controller definition)
// ==============================================================================

void PresetBrowserButton::onClick() {
    if (controller_) controller_->openPresetBrowser();
}

void SavePresetButton::onClick() {
    if (controller_) controller_->openSavePresetDialog();
}

// ==============================================================================
// Controller Implementation
// ==============================================================================

Controller::~Controller() {
    // Ensure visibility controllers are cleaned up
    for (auto& vc : bandVisibilityControllers_) {
        if (vc) {
            if (auto* cvc = dynamic_cast<ContainerVisibilityController*>(vc.get())) {
                cvc->deactivate();
            }
        }
    }
}

// ==============================================================================
// IPluginBase
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    // Always call parent first
    Steinberg::tresult result = EditControllerEx1::initialize(context);
    if (result != Steinberg::kResultTrue) {
        return result;
    }

    // Register all parameters (FR-004 through FR-006)
    registerGlobalParams();
    registerSweepParams();
    registerModulationParams();
    registerBandParams();
    registerNodeParams();

    // MIDI CC Manager (Spec 012)
    midiCCManager_ = std::make_unique<Krate::Plugins::MidiCCManager>();

    // Preset Manager (Spec 010)
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(
        makeDisrumpoPresetConfig(), nullptr, this);

    presetManager_->setStateProvider([this]() -> Steinberg::IBStream* {
        return this->createComponentStateStream();
    });

    presetManager_->setLoadProvider(
        [this](Steinberg::IBStream* state,
               const Krate::Plugins::PresetInfo& /*info*/) -> bool {
            return this->loadComponentStateWithNotify(state);
        });

    // Update checker
    updateChecker_ = std::make_unique<Krate::Plugins::UpdateChecker>(makeDisrumpoUpdateConfig());

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::terminate() {
    updateChecker_.reset();
    return EditControllerEx1::terminate();
}

// ==============================================================================
// IEditController - State (setComponentState, getState, setState)
// ==============================================================================

// ==============================================================================
// Shared Component State Parser
// ==============================================================================
// Single implementation of binary state parsing, parameterized by how each
// parameter value is applied. setComponentState() passes setParamNormalized;
// loadComponentStateWithNotify() passes editParamWithNotify.

template <typename ParamSetter>
bool Controller::parseComponentState(Steinberg::IBStreamer& streamer, int32_t version,
                                     ParamSetter&& setter) {
    // Global parameters (v1+)
    float inputGain = 0.5f;
    float outputGain = 0.5f;
    float globalMix = 1.0f;

    if (!streamer.readFloat(inputGain)) return false;
    if (!streamer.readFloat(outputGain)) return false;
    if (!streamer.readFloat(globalMix)) return false;

    setter(makeGlobalParamId(GlobalParamType::kGlobalInputGain), inputGain);
    setter(makeGlobalParamId(GlobalParamType::kGlobalOutputGain), outputGain);
    setter(makeGlobalParamId(GlobalParamType::kGlobalMix), globalMix);

    // Band management (v2+)
    if (version >= 2) {
        int32_t bandCount = 4;
        if (streamer.readInt32(bandCount)) {
            int clampedCount = std::clamp(bandCount, 1, 4);
            float normalizedBandCount = static_cast<float>(clampedCount - 1) / 3.0f;
            setter(makeGlobalParamId(GlobalParamType::kGlobalBandCount), normalizedBandCount);
        }

        constexpr int kV7MaxBands = 8;
        const int streamBands = (version <= 7) ? kV7MaxBands : kMaxBands;
        for (int b = 0; b < streamBands; ++b) {
            float gain = 0.0f;
            float pan = 0.0f;
            Steinberg::int8 soloInt = 0;
            Steinberg::int8 bypassInt = 0;
            Steinberg::int8 muteInt = 0;

            streamer.readFloat(gain);
            streamer.readFloat(pan);
            streamer.readInt8(soloInt);
            streamer.readInt8(bypassInt);
            streamer.readInt8(muteInt);

            if (b < kMaxBands) {
                auto* gainParam = getParameterObject(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandGain));
                if (gainParam)
                    setter(gainParam->getInfo().id, gainParam->toNormalized(gain));
                auto* panParam = getParameterObject(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandPan));
                if (panParam)
                    setter(panParam->getInfo().id, panParam->toNormalized(pan));
                setter(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandSolo), soloInt != 0 ? 1.0 : 0.0);
                setter(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandBypass), bypassInt != 0 ? 1.0 : 0.0);
                setter(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMute), muteInt != 0 ? 1.0 : 0.0);
            }
        }

        const int streamCrossovers = (version <= 7) ? 7 : (kMaxBands - 1);
        for (int i = 0; i < streamCrossovers; ++i) {
            float freq = 1000.0f;
            if (streamer.readFloat(freq)) {
                if (i < kMaxBands - 1) {
                    auto* param = getParameterObject(makeCrossoverParamId(static_cast<uint8_t>(i)));
                    if (param)
                        setter(param->getInfo().id, param->toNormalized(freq));
                }
            }
        }
    }

    // Sweep System (v4+)
    if (version >= 4) {
        Steinberg::int8 sweepEnable = 0;
        float sweepFreqNorm = 0.566f;
        float sweepWidthNorm = 0.286f;
        float sweepIntensityNorm = 0.25f;
        Steinberg::int8 sweepFalloff = 1;
        Steinberg::int8 sweepMorphLink = 0;

        if (streamer.readInt8(sweepEnable))
            setter(makeSweepParamId(SweepParamType::kSweepEnable), sweepEnable != 0 ? 1.0 : 0.0);
        if (streamer.readFloat(sweepFreqNorm))
            setter(makeSweepParamId(SweepParamType::kSweepFrequency), sweepFreqNorm);
        if (streamer.readFloat(sweepWidthNorm))
            setter(makeSweepParamId(SweepParamType::kSweepWidth), sweepWidthNorm);
        if (streamer.readFloat(sweepIntensityNorm))
            setter(makeSweepParamId(SweepParamType::kSweepIntensity), sweepIntensityNorm);
        if (streamer.readInt8(sweepFalloff))
            setter(makeSweepParamId(SweepParamType::kSweepFalloff), sweepFalloff != 0 ? 1.0 : 0.0);
        if (streamer.readInt8(sweepMorphLink))
            setter(makeSweepParamId(SweepParamType::kSweepMorphLink),
                   static_cast<double>(sweepMorphLink) / (kMorphLinkModeCount - 1));

        // Sweep LFO
        Steinberg::int8 lfoEnable = 0;
        float lfoRateNorm = 0.606f;
        Steinberg::int8 lfoWaveform = 0;
        float lfoDepth = 0.0f;
        Steinberg::int8 lfoSync = 0;
        Steinberg::int8 lfoNoteIndex = 0;

        if (streamer.readInt8(lfoEnable))
            setter(makeSweepParamId(SweepParamType::kSweepLFOEnable), lfoEnable != 0 ? 1.0 : 0.0);
        if (streamer.readFloat(lfoRateNorm))
            setter(makeSweepParamId(SweepParamType::kSweepLFORate), lfoRateNorm);
        if (streamer.readInt8(lfoWaveform))
            setter(makeSweepParamId(SweepParamType::kSweepLFOWaveform), static_cast<double>(lfoWaveform) / 5.0);
        if (streamer.readFloat(lfoDepth))
            setter(makeSweepParamId(SweepParamType::kSweepLFODepth), lfoDepth);
        if (streamer.readInt8(lfoSync))
            setter(makeSweepParamId(SweepParamType::kSweepLFOSync), lfoSync != 0 ? 1.0 : 0.0);
        if (streamer.readInt8(lfoNoteIndex))
            setter(makeSweepParamId(SweepParamType::kSweepLFONoteValue), static_cast<double>(lfoNoteIndex) / 14.0);

        // Sweep Envelope
        Steinberg::int8 envEnable = 0;
        float envAttackNorm = 0.091f;
        float envReleaseNorm = 0.184f;
        float envSensitivity = 0.5f;

        if (streamer.readInt8(envEnable))
            setter(makeSweepParamId(SweepParamType::kSweepEnvEnable), envEnable != 0 ? 1.0 : 0.0);
        if (streamer.readFloat(envAttackNorm))
            setter(makeSweepParamId(SweepParamType::kSweepEnvAttack), envAttackNorm);
        if (streamer.readFloat(envReleaseNorm))
            setter(makeSweepParamId(SweepParamType::kSweepEnvRelease), envReleaseNorm);
        if (streamer.readFloat(envSensitivity))
            setter(makeSweepParamId(SweepParamType::kSweepEnvSensitivity), envSensitivity);

        // Custom Curve - skip breakpoint data
        int32_t pointCount = 2;
        if (streamer.readInt32(pointCount)) {
            pointCount = std::clamp(pointCount, 2, 8);
            for (int32_t i = 0; i < pointCount; ++i) {
                float px = 0.0f;
                float py = 0.0f;
                streamer.readFloat(px);
                streamer.readFloat(py);
            }
        }
    }

    // Modulation System (v5+)
    if (version >= 5) {
        // LFO 1
        float lfo1RateNorm = 0.5f;
        if (streamer.readFloat(lfo1RateNorm))
            setter(makeModParamId(ModParamType::kLFO1Rate), lfo1RateNorm);
        Steinberg::int8 lfo1Shape = 0;
        if (streamer.readInt8(lfo1Shape))
            setter(makeModParamId(ModParamType::kLFO1Shape), static_cast<double>(lfo1Shape) / 5.0);
        float lfo1Phase = 0.0f;
        if (streamer.readFloat(lfo1Phase))
            setter(makeModParamId(ModParamType::kLFO1Phase), lfo1Phase);
        Steinberg::int8 lfo1Sync = 0;
        if (streamer.readInt8(lfo1Sync))
            setter(makeModParamId(ModParamType::kLFO1Sync), lfo1Sync != 0 ? 1.0 : 0.0);
        Steinberg::int8 lfo1NoteIdx = 0;
        if (streamer.readInt8(lfo1NoteIdx))
            setter(makeModParamId(ModParamType::kLFO1NoteValue), static_cast<double>(lfo1NoteIdx) / 14.0);
        Steinberg::int8 lfo1Unipolar = 0;
        if (streamer.readInt8(lfo1Unipolar))
            setter(makeModParamId(ModParamType::kLFO1Unipolar), lfo1Unipolar != 0 ? 1.0 : 0.0);
        Steinberg::int8 lfo1Retrigger = 1;
        if (streamer.readInt8(lfo1Retrigger))
            setter(makeModParamId(ModParamType::kLFO1Retrigger), lfo1Retrigger != 0 ? 1.0 : 0.0);

        // LFO 2
        float lfo2RateNorm = 0.5f;
        if (streamer.readFloat(lfo2RateNorm))
            setter(makeModParamId(ModParamType::kLFO2Rate), lfo2RateNorm);
        Steinberg::int8 lfo2Shape = 0;
        if (streamer.readInt8(lfo2Shape))
            setter(makeModParamId(ModParamType::kLFO2Shape), static_cast<double>(lfo2Shape) / 5.0);
        float lfo2Phase = 0.0f;
        if (streamer.readFloat(lfo2Phase))
            setter(makeModParamId(ModParamType::kLFO2Phase), lfo2Phase);
        Steinberg::int8 lfo2Sync = 0;
        if (streamer.readInt8(lfo2Sync))
            setter(makeModParamId(ModParamType::kLFO2Sync), lfo2Sync != 0 ? 1.0 : 0.0);
        Steinberg::int8 lfo2NoteIdx = 0;
        if (streamer.readInt8(lfo2NoteIdx))
            setter(makeModParamId(ModParamType::kLFO2NoteValue), static_cast<double>(lfo2NoteIdx) / 14.0);
        Steinberg::int8 lfo2Unipolar = 0;
        if (streamer.readInt8(lfo2Unipolar))
            setter(makeModParamId(ModParamType::kLFO2Unipolar), lfo2Unipolar != 0 ? 1.0 : 0.0);
        Steinberg::int8 lfo2Retrigger = 1;
        if (streamer.readInt8(lfo2Retrigger))
            setter(makeModParamId(ModParamType::kLFO2Retrigger), lfo2Retrigger != 0 ? 1.0 : 0.0);

        // Envelope Follower
        float envAttackNorm = 0.0f;
        if (streamer.readFloat(envAttackNorm))
            setter(makeModParamId(ModParamType::kEnvFollowerAttack), envAttackNorm);
        float envReleaseNorm = 0.0f;
        if (streamer.readFloat(envReleaseNorm))
            setter(makeModParamId(ModParamType::kEnvFollowerRelease), envReleaseNorm);
        float envSensitivity = 0.5f;
        if (streamer.readFloat(envSensitivity))
            setter(makeModParamId(ModParamType::kEnvFollowerSensitivity), envSensitivity);
        Steinberg::int8 envSource = 0;
        if (streamer.readInt8(envSource))
            setter(makeModParamId(ModParamType::kEnvFollowerSource), static_cast<double>(envSource) / 4.0);

        // Random
        float randomRateNorm = 0.0f;
        if (streamer.readFloat(randomRateNorm))
            setter(makeModParamId(ModParamType::kRandomRate), randomRateNorm);
        float randomSmoothness = 0.0f;
        if (streamer.readFloat(randomSmoothness))
            setter(makeModParamId(ModParamType::kRandomSmoothness), randomSmoothness);
        Steinberg::int8 randomSync = 0;
        if (streamer.readInt8(randomSync))
            setter(makeModParamId(ModParamType::kRandomSync), randomSync != 0 ? 1.0 : 0.0);

        // Chaos
        Steinberg::int8 chaosModel = 0;
        if (streamer.readInt8(chaosModel))
            setter(makeModParamId(ModParamType::kChaosModel), static_cast<double>(chaosModel) / 3.0);
        float chaosSpeedNorm = 0.0f;
        if (streamer.readFloat(chaosSpeedNorm))
            setter(makeModParamId(ModParamType::kChaosSpeed), chaosSpeedNorm);
        float chaosCoupling = 0.0f;
        if (streamer.readFloat(chaosCoupling))
            setter(makeModParamId(ModParamType::kChaosCoupling), chaosCoupling);

        // Sample & Hold
        Steinberg::int8 shSource = 0;
        if (streamer.readInt8(shSource))
            setter(makeModParamId(ModParamType::kSampleHoldSource), static_cast<double>(shSource) / 3.0);
        float shRateNorm = 0.0f;
        if (streamer.readFloat(shRateNorm))
            setter(makeModParamId(ModParamType::kSampleHoldRate), shRateNorm);
        float shSlewNorm = 0.0f;
        if (streamer.readFloat(shSlewNorm))
            setter(makeModParamId(ModParamType::kSampleHoldSlew), shSlewNorm);

        // Pitch Follower
        float pitchMinNorm = 0.0f;
        if (streamer.readFloat(pitchMinNorm))
            setter(makeModParamId(ModParamType::kPitchFollowerMinHz), pitchMinNorm);
        float pitchMaxNorm = 0.0f;
        if (streamer.readFloat(pitchMaxNorm))
            setter(makeModParamId(ModParamType::kPitchFollowerMaxHz), pitchMaxNorm);
        float pitchConfidence = 0.5f;
        if (streamer.readFloat(pitchConfidence))
            setter(makeModParamId(ModParamType::kPitchFollowerConfidence), pitchConfidence);
        float pitchTrackNorm = 0.0f;
        if (streamer.readFloat(pitchTrackNorm))
            setter(makeModParamId(ModParamType::kPitchFollowerTrackingSpeed), pitchTrackNorm);

        // Transient
        float transSensitivity = 0.5f;
        if (streamer.readFloat(transSensitivity))
            setter(makeModParamId(ModParamType::kTransientSensitivity), transSensitivity);
        float transAttackNorm = 0.0f;
        if (streamer.readFloat(transAttackNorm))
            setter(makeModParamId(ModParamType::kTransientAttack), transAttackNorm);
        float transDecayNorm = 0.0f;
        if (streamer.readFloat(transDecayNorm))
            setter(makeModParamId(ModParamType::kTransientDecay), transDecayNorm);

        // Macros
        constexpr ModParamType macroParams[4][4] = {
            {ModParamType::kMacro1Value, ModParamType::kMacro1Min, ModParamType::kMacro1Max, ModParamType::kMacro1Curve},
            {ModParamType::kMacro2Value, ModParamType::kMacro2Min, ModParamType::kMacro2Max, ModParamType::kMacro2Curve},
            {ModParamType::kMacro3Value, ModParamType::kMacro3Min, ModParamType::kMacro3Max, ModParamType::kMacro3Curve},
            {ModParamType::kMacro4Value, ModParamType::kMacro4Min, ModParamType::kMacro4Max, ModParamType::kMacro4Curve},
        };
        for (const auto& macro : macroParams) {
            float macroValue = 0.0f;
            if (streamer.readFloat(macroValue))
                setter(makeModParamId(macro[0]), macroValue);
            float macroMin = 0.0f;
            if (streamer.readFloat(macroMin))
                setter(makeModParamId(macro[1]), macroMin);
            float macroMax = 1.0f;
            if (streamer.readFloat(macroMax))
                setter(makeModParamId(macro[2]), macroMax);
            Steinberg::int8 macroCurve = 0;
            if (streamer.readInt8(macroCurve))
                setter(makeModParamId(macro[3]), static_cast<double>(macroCurve) / 3.0);
        }

        // Routing
        for (uint8_t r = 0; r < 32; ++r) {
            Steinberg::int8 source = 0;
            if (streamer.readInt8(source))
                setter(makeRoutingParamId(r, 0),
                       static_cast<double>(std::clamp(static_cast<int>(source), 0, kUIModSourceCount - 1))
                       / static_cast<double>(kUIModSourceCount - 1));
            int32_t dest = 0;
            if (streamer.readInt32(dest))
                setter(makeRoutingParamId(r, 1),
                       static_cast<double>(std::clamp(dest, 0, static_cast<int32_t>(ModDest::kTotalDestinations - 1)))
                       / static_cast<double>(ModDest::kTotalDestinations - 1));
            float amount = 0.0f;
            if (streamer.readFloat(amount))
                setter(makeRoutingParamId(r, 2), static_cast<double>(amount + 1.0f) / 2.0);
            Steinberg::int8 curve = 0;
            if (streamer.readInt8(curve))
                setter(makeRoutingParamId(r, 3), static_cast<double>(curve) / 3.0);
        }
    }

    // Morph Node State (v6+)
    if (version >= 6) {
        constexpr int kV7MorphBands = 8;
        const int streamMorphBands = (version <= 7) ? kV7MorphBands : kMaxBands;
        for (int b = 0; b < streamMorphBands; ++b) {
            const auto band = static_cast<uint8_t>(b);

            float morphX = 0.5f;
            float morphY = 0.5f;
            Steinberg::int8 morphMode = 0;
            auto activeNodes = static_cast<Steinberg::int8>(kDefaultActiveNodes);
            float morphSmoothing = 0.0f;

            bool readMorphX = streamer.readFloat(morphX);
            bool readMorphY = streamer.readFloat(morphY);
            bool readMorphMode = streamer.readInt8(morphMode);
            bool readActiveNodes = streamer.readInt8(activeNodes);
            bool readMorphSmoothing = streamer.readFloat(morphSmoothing);

            if (b < kMaxBands) {
                if (readMorphX)
                    setter(makeBandParamId(band, BandParamType::kBandMorphX), static_cast<double>(morphX));
                if (readMorphY)
                    setter(makeBandParamId(band, BandParamType::kBandMorphY), static_cast<double>(morphY));
                if (readMorphMode)
                    setter(makeBandParamId(band, BandParamType::kBandMorphMode), static_cast<double>(morphMode) / 2.0);
                if (readActiveNodes) {
                    int count = std::clamp(static_cast<int>(activeNodes), kMinActiveNodes, kMaxMorphNodes);
                    setter(makeBandParamId(band, BandParamType::kBandActiveNodes), static_cast<double>(count - 1) / 3.0);
                }
                if (readMorphSmoothing)
                    setter(makeBandParamId(band, BandParamType::kBandMorphSmoothing), static_cast<double>(morphSmoothing) / 500.0);
            }

            for (int n = 0; n < kMaxMorphNodes; ++n) {
                const auto node = static_cast<uint8_t>(n);

                Steinberg::int8 nodeType = 0;
                float drive = 1.0f;
                float mix = 1.0f;
                float tone = 4000.0f;
                float bias = 0.0f;
                float folds = 1.0f;
                float bitDepth = 16.0f;

                bool rType = streamer.readInt8(nodeType);
                bool rDrive = streamer.readFloat(drive);
                bool rMix = streamer.readFloat(mix);
                bool rTone = streamer.readFloat(tone);
                bool rBias = streamer.readFloat(bias);
                bool rFolds = streamer.readFloat(folds);
                bool rBitDepth = streamer.readFloat(bitDepth);

                if (b < kMaxBands) {
                    if (rType)
                        setter(makeNodeParamId(band, node, NodeParamType::kNodeType), static_cast<double>(nodeType) / 25.0);
                    if (rDrive)
                        setter(makeNodeParamId(band, node, NodeParamType::kNodeDrive), static_cast<double>(drive) / 10.0);
                    if (rMix)
                        setter(makeNodeParamId(band, node, NodeParamType::kNodeMix), static_cast<double>(mix));
                    if (rTone)
                        setter(makeNodeParamId(band, node, NodeParamType::kNodeTone), static_cast<double>(tone - 200.0f) / 7800.0);
                    if (rBias)
                        setter(makeNodeParamId(band, node, NodeParamType::kNodeBias), static_cast<double>(bias + 1.0f) / 2.0);
                    if (rFolds)
                        setter(makeNodeParamId(band, node, NodeParamType::kNodeFolds), static_cast<double>(folds - 1.0f) / 11.0);
                    if (rBitDepth)
                        setter(makeNodeParamId(band, node, NodeParamType::kNodeBitDepth), static_cast<double>(bitDepth - 4.0f) / 20.0);
                }

                if (version >= 9) {
                    for (int s = 0; s < 10; ++s) {
                        float slotValue;
                        if (streamer.readFloat(slotValue)) {
                            if (b < kMaxBands) {
                                auto shapeType = static_cast<NodeParamType>(
                                    static_cast<uint8_t>(NodeParamType::kNodeShape0) + s);
                                setter(makeNodeParamId(band, node, shapeType), static_cast<double>(slotValue));
                            }
                        }
                    }

                    // NOLINTNEXTLINE(modernize-loop-convert)
                    for (int t = 0; t < kDistortionTypeCount; ++t) {
                        for (int s = 0; s < MorphNode::kShapeSlotCount; ++s) {
                            float shadowValue;
                            if (streamer.readFloat(shadowValue)) {
                                if (b < kMaxBands) {
                                    shapeShadowStorage_[b][n].typeSlots[t][s] = shadowValue;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Sync proxy params from selected node's actual values
        for (int b = 0; b < kMaxBands; ++b) {
            auto band = static_cast<uint8_t>(b);
            auto* selParam = getParameterObject(makeBandParamId(band, BandParamType::kBandSelectedNode));
            int selNode = 0;
            if (selParam)
                selNode = std::clamp(static_cast<int>(selParam->getNormalized() * 3.0 + 0.5), 0, 3);
            auto nodeIdx = static_cast<uint8_t>(selNode);

            auto* nodeTypeParam = getParameterObject(makeNodeParamId(band, nodeIdx, NodeParamType::kNodeType));
            if (nodeTypeParam)
                setter(makeBandParamId(band, BandParamType::kBandDisplayedType), nodeTypeParam->getNormalized());

            for (int i = 0; i < kNumDisplayedProxyParams; ++i) {
                auto* actualParam = getParameterObject(makeNodeParamId(band, nodeIdx, kProxyIndexToNodeParam[i]));
                if (actualParam)
                    setter(makeBandParamId(band, kProxyIndexToBandParam[i]), actualParam->getNormalized());
            }
        }
    }

    return true;
}

Steinberg::tresult PLUGIN_API Controller::setComponentState(Steinberg::IBStream* state) {
    if (!state) return Steinberg::kResultFalse;

    Steinberg::IBStreamer streamer(state, kLittleEndian);
    int32_t version = 0;
    if (!streamer.readInt32(version)) return Steinberg::kResultFalse;
    if (version < 1) return Steinberg::kResultFalse;

    bulkParamLoad_ = true;
    FrameInvalidationGuard frameGuard(activeEditor_);

    auto setter = [this](Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
        setParamNormalized(id, value);
    };

    if (!parseComponentState(streamer, version, setter))
        return Steinberg::kResultFalse;

    bulkParamLoad_ = false;
    syncAllViews();
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::getState(Steinberg::IBStream* state) {
    Steinberg::IBStreamer streamer(state, kLittleEndian);
    if (!streamer.writeInt32(2)) return Steinberg::kResultFalse;
    if (!streamer.writeDouble(0.0)) return Steinberg::kResultFalse;
    if (!streamer.writeDouble(0.0)) return Steinberg::kResultFalse;

    if (midiCCManager_) {
        auto midiData = midiCCManager_->serializeGlobalMappings();
        auto midiDataSize = static_cast<Steinberg::int32>(midiData.size());
        if (!streamer.writeInt32(midiDataSize)) return Steinberg::kResultFalse;
        if (midiDataSize > 0) {
            if (state->write(midiData.data(), midiDataSize, nullptr) != Steinberg::kResultOk)
                return Steinberg::kResultFalse;
        }
    } else {
        if (!streamer.writeInt32(0)) return Steinberg::kResultFalse;
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::setState(Steinberg::IBStream* state) {
    Steinberg::IBStreamer streamer(state, kLittleEndian);
    int32_t version = 0;
    if (!streamer.readInt32(version)) return Steinberg::kResultOk;

    if (version >= 2) {
        double unusedWidth = 0.0, unusedHeight = 0.0;
        streamer.readDouble(unusedWidth);
        streamer.readDouble(unusedHeight);

        Steinberg::int32 midiDataSize = 0;
        if (streamer.readInt32(midiDataSize) && midiDataSize > 0) {
            std::vector<uint8_t> midiData(static_cast<size_t>(midiDataSize));
            if (state->read(midiData.data(), midiDataSize, nullptr) == Steinberg::kResultOk) {
                if (midiCCManager_)
                    midiCCManager_->deserializeGlobalMappings(midiData.data(), midiData.size());
            }
        }
    }

    return Steinberg::kResultOk;
}

// ==============================================================================
// IMidiMapping (FR-028, FR-029)
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::getMidiControllerAssignment(
    Steinberg::int32 busIndex, Steinberg::int16 /*channel*/,
    Steinberg::Vst::CtrlNumber midiControllerNumber,
    Steinberg::Vst::ParamID& id) {

    if (busIndex != 0) return Steinberg::kResultFalse;

    if (midiCCManager_) {
        auto ccNum = static_cast<uint8_t>(midiControllerNumber);
        if (midiCCManager_->getMidiControllerAssignment(ccNum, id))
            return Steinberg::kResultTrue;
    }

    if (assignedMidiCC_ < 128 && midiControllerNumber == assignedMidiCC_) {
        id = makeSweepParamId(SweepParamType::kSweepFrequency);
        return Steinberg::kResultTrue;
    }

    return Steinberg::kResultFalse;
}

Steinberg::IPlugView* PLUGIN_API Controller::createView(Steinberg::FIDString name) {
    if (std::strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0) {
        auto* editor = new VSTGUI::VST3Editor(this, "editor", "editor.uidesc");

        {
            auto* mpParam = getParameterObject(
                makeGlobalParamId(GlobalParamType::kGlobalModPanelVisible));
            bool modVis = (mpParam != nullptr) && (mpParam->getNormalized() >= 0.5);
            double extraH = modVis ? ModPanelToggleController::kModPanelHeight : 0.0;
            editor->setEditorSizeConstrains(
                VSTGUI::CPoint(834, 500 + extraH),
                VSTGUI::CPoint(1400, 840 + extraH));
        }

        return editor;
    }
    return nullptr;
}

Steinberg::tresult PLUGIN_API Controller::getParamStringByValue(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized,
    Steinberg::Vst::String128 string) {

    // Check for node parameters
    if (isNodeParamId(id)) {
        NodeParamType paramType = extractNodeParamType(id);

        if (paramType == NodeParamType::kNodeDrive) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);
                floatToString128(plainValue, 1, string);
                return Steinberg::kResultTrue;
            }
        }

        const auto paramByte = static_cast<uint8_t>(paramType);
        if (paramByte >= static_cast<uint8_t>(NodeParamType::kNodeShape0) &&
            paramByte <= static_cast<uint8_t>(NodeParamType::kNodeShape9)) {
            uint8_t band = extractBandFromNodeParam(id);
            auto displayedTypeId = makeBandParamId(band, BandParamType::kBandDisplayedType);
            auto* dtParam = getParameterObject(displayedTypeId);
            if (dtParam) {
                int typeIdx = static_cast<int>(dtParam->getNormalized() * 25.0 + 0.5);
                auto distType = static_cast<DistortionType>(std::clamp(typeIdx, 0, 25));
                int slot = paramByte - static_cast<uint8_t>(NodeParamType::kNodeShape0);
                if (formatShapeSlot(distType, slot, static_cast<float>(valueNormalized), string))
                    return Steinberg::kResultTrue;
            }
        }

        if (paramType == NodeParamType::kNodeMix) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);
                int percent = static_cast<int>(std::round(plainValue));
                intToString128(percent, string);
                appendToString128(string, STR16("%"));
                return Steinberg::kResultTrue;
            }
        }
    }

    // Check for band parameters
    if (isBandParamId(id)) {
        BandParamType paramType = extractBandParamType(id);

        if (paramType == BandParamType::kBandGain) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);
                floatToString128(plainValue, 1, string);
                appendToString128(string, STR16(" dB"));
                return Steinberg::kResultTrue;
            }
        }

        if (paramType == BandParamType::kBandDisplayedDrive) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);
                floatToString128(plainValue, 1, string);
                return Steinberg::kResultTrue;
            }
        }

        if (paramType == BandParamType::kBandDisplayedMix) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);
                int percent = static_cast<int>(std::round(plainValue));
                intToString128(percent, string);
                appendToString128(string, STR16("%"));
                return Steinberg::kResultTrue;
            }
        }

        {
            const auto paramByte = static_cast<uint8_t>(paramType);
            const auto shape0Byte = static_cast<uint8_t>(BandParamType::kBandDisplayedShape0);
            const auto shape9Byte = static_cast<uint8_t>(BandParamType::kBandDisplayedShape9);
            if (paramByte >= shape0Byte && paramByte <= shape9Byte) {
                uint8_t band = extractBandIndex(id);
                auto displayedTypeId = makeBandParamId(band, BandParamType::kBandDisplayedType);
                auto* dtParam = getParameterObject(displayedTypeId);
                if (dtParam) {
                    int typeIdx = static_cast<int>(dtParam->getNormalized() * 25.0 + 0.5);
                    auto distType = static_cast<DistortionType>(std::clamp(typeIdx, 0, 25));
                    int slot = paramByte - shape0Byte;
                    if (formatShapeSlot(distType, slot, static_cast<float>(valueNormalized), string))
                        return Steinberg::kResultTrue;
                }
            }
        }

        if (paramType == BandParamType::kBandPan) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);
                if (std::abs(plainValue) < 0.01) {
                    const Steinberg::Vst::TChar* center = STR16("Center");
                    for (int i = 0; center[i] && i < 127; ++i) {
                        string[i] = center[i];
                        string[i + 1] = 0;
                    }
                } else if (plainValue < 0) {
                    int percent = static_cast<int>(std::round(std::abs(plainValue) * 100.0));
                    intToString128(percent, string);
                    appendToString128(string, STR16("% L"));
                } else {
                    int percent = static_cast<int>(std::round(plainValue * 100.0));
                    intToString128(percent, string);
                    appendToString128(string, STR16("% R"));
                }
                return Steinberg::kResultTrue;
            }
        }
    }

    // Check for global parameters
    if (isGlobalParamId(id)) {
        if (id == makeGlobalParamId(GlobalParamType::kGlobalMix)) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);
                int percent = static_cast<int>(std::round(plainValue));
                intToString128(percent, string);
                appendToString128(string, STR16("%"));
                return Steinberg::kResultTrue;
            }
        }

        if (id == makeGlobalParamId(GlobalParamType::kGlobalInputGain) ||
            id == makeGlobalParamId(GlobalParamType::kGlobalOutputGain)) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);
                floatToString128(plainValue, 1, string);
                appendToString128(string, STR16(" dB"));
                return Steinberg::kResultTrue;
            }
        }
    }

    return EditControllerEx1::getParamStringByValue(id, valueNormalized, string);
}

Steinberg::tresult PLUGIN_API Controller::getParamValueByString(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::TChar* string,
    Steinberg::Vst::ParamValue& valueNormalized) {
    return EditControllerEx1::getParamValueByString(id, string, valueNormalized);
}

// ==============================================================================
// VST3EditorDelegate - Custom Views, Sub-Controllers, didOpen, willClose
// ==============================================================================
// These methods remain in controller.cpp as they use many Controller members
// and are the core UI lifecycle. See the original file for full implementations.
// The createCustomView, createSubController, didOpen, willClose methods
// are preserved exactly as they were (lines 3665-4692 of original).

VSTGUI::CView* Controller::createCustomView(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* editor) {

    if (std::strcmp(name, "SpectrumDisplay") == 0) {
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;
        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");
        if (originStr) {
            double x = 0.0, y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2)
                origin = VSTGUI::CPoint(x, y);
        }
        if (sizeStr) {
            double w = 980.0, h = 200.0;
            if (sscanf(sizeStr->c_str(), "%lf, %lf", &w, &h) == 2)
                size = VSTGUI::CPoint(w, h);
        } else {
            size = VSTGUI::CPoint(980.0, 200.0);
        }

        VSTGUI::CRect rect(origin, size);
        auto* spectrumDisplay = new SpectrumDisplay(rect);

        auto* bandCountParam = getParameterObject(makeGlobalParamId(GlobalParamType::kGlobalBandCount));
        if (bandCountParam) {
            float normalized = static_cast<float>(bandCountParam->getNormalized());
            int bandCount = static_cast<int>(std::round(normalized * 3.0f)) + 1;
            spectrumDisplay->setNumBands(bandCount);
        }

        for (int i = 0; i < kMaxBands - 1; ++i) {
            auto* crossoverParam = getParameterObject(makeCrossoverParamId(static_cast<uint8_t>(i)));
            if (crossoverParam) {
                float freq = static_cast<float>(crossoverParam->toPlain(crossoverParam->getNormalized()));
                spectrumDisplay->setCrossoverFrequency(i, freq);
            }
        }

        spectrumDisplay_ = spectrumDisplay;
        if (spectrumDataAvailable_) {
            spectrumDisplay_->setSpectrumFIFOs(&localInputFIFO_, &localOutputFIFO_);
            if (cachedSpectrumSampleRate_ > 0.0)
                spectrumDisplay_->startAnalysis(cachedSpectrumSampleRate_);
        }

        auto* bridge = new CrossoverDragBridge(this);
        crossoverDragBridge_ = Steinberg::owned(bridge);
        spectrumDisplay->setListener(bridge);

        return spectrumDisplay;
    }

    if (std::strcmp(name, "MorphPad") == 0) {
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;
        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");
        if (originStr) {
            double x = 0.0, y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2)
                origin = VSTGUI::CPoint(x, y);
        }
        if (sizeStr) {
            double w = 250.0, h = 200.0;
            if (sscanf(sizeStr->c_str(), "%lf, %lf", &w, &h) == 2)
                size = VSTGUI::CPoint(w, h);
        } else {
            size = VSTGUI::CPoint(250.0, 200.0);
        }

        int bandIndex = 0;
        const std::string* bandStr = attributes.getAttributeValue("band");
        if (bandStr) {
            bandIndex = std::stoi(*bandStr);
            bandIndex = std::clamp(bandIndex, 0, kMaxBands - 1);
        }

        Steinberg::Vst::ParamID activeNodesParamId = makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandActiveNodes);

        VSTGUI::CRect rect(origin, size);
        auto* morphPad = new MorphPad(rect, this, activeNodesParamId);

        Steinberg::Vst::ParamID morphXParamId = makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandMorphX);
        morphPad->setTag(static_cast<int32_t>(morphXParamId));
        morphPad->setListener(editor);

        Steinberg::Vst::ParamID morphYParamId = makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandMorphY);
        morphPad->setMorphYParamId(morphYParamId);

        auto* morphXParam = getParameterObject(morphXParamId);
        auto* morphYParam = getParameterObject(makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandMorphY));

        if (morphXParam && morphYParam) {
            float morphX = static_cast<float>(morphXParam->getNormalized());
            float morphY = static_cast<float>(morphYParam->getNormalized());
            morphPad->setMorphPosition(morphX, morphY);
            morphPad->setValue(morphX);
        }

        for (int n = 0; n < 4; ++n) {
            auto* nodeTypeParam = getParameterObject(makeNodeParamId(
                static_cast<uint8_t>(bandIndex), static_cast<uint8_t>(n), NodeParamType::kNodeType));
            if (nodeTypeParam) {
                int typeIndex = static_cast<int>(std::round(nodeTypeParam->getNormalized() * 25.0));
                morphPad->setNodeType(n, static_cast<DistortionType>(typeIndex));
            }
        }

        morphPads_[bandIndex] = morphPad;
        return morphPad;
    }

    if (std::strcmp(name, "DynamicNodeSelector") == 0) {
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;
        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");
        if (originStr) {
            double x = 0.0, y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2)
                origin = VSTGUI::CPoint(x, y);
        }
        if (sizeStr) {
            double w = 140.0, h = 22.0;
            if (sscanf(sizeStr->c_str(), "%lf, %lf", &w, &h) == 2)
                size = VSTGUI::CPoint(w, h);
        } else {
            size = VSTGUI::CPoint(140.0, 22.0);
        }

        int bandIndex = 0;
        const std::string* bandStr = attributes.getAttributeValue("band");
        if (bandStr) {
            bandIndex = std::stoi(*bandStr);
            bandIndex = std::clamp(bandIndex, 0, kMaxBands - 1);
        }

        Steinberg::Vst::ParamID activeNodesParamId = makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandActiveNodes);
        Steinberg::Vst::ParamID selectedNodeParamId = makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandSelectedNode);

        VSTGUI::CRect rect(origin, size);
        auto* nodeSelector = new DynamicNodeSelector(
            rect, this, activeNodesParamId, selectedNodeParamId);
        nodeSelector->setTag(static_cast<int32_t>(selectedNodeParamId));

        auto* selectedNodeParam = getParameterObject(selectedNodeParamId);
        if (selectedNodeParam) {
            float normalized = static_cast<float>(selectedNodeParam->getNormalized());
            nodeSelector->setValueNormalized(normalized);
        }

        const std::string* autoHideStr = attributes.getAttributeValue("auto-hide-single");
        if (autoHideStr && *autoHideStr == "true") {
            nodeSelector->setAutoHideForSingleNode(true);
            compactNodeSelectors_[bandIndex] = nodeSelector;
        } else {
            dynamicNodeSelectors_[bandIndex] = nodeSelector;
        }

        return nodeSelector;
    }

    if (std::strcmp(name, "NodeEditorBorder") == 0) {
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;
        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");
        if (originStr) {
            double x = 0.0, y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2)
                origin = VSTGUI::CPoint(x, y);
        }
        if (sizeStr) {
            double w = 280.0, h = 230.0;
            if (sscanf(sizeStr->c_str(), "%lf, %lf", &w, &h) == 2)
                size = VSTGUI::CPoint(w, h);
        } else {
            size = VSTGUI::CPoint(280.0, 230.0);
        }

        int bandIndex = 0;
        const std::string* bandStr = attributes.getAttributeValue("band");
        if (bandStr) {
            bandIndex = std::stoi(*bandStr);
            bandIndex = std::clamp(bandIndex, 0, kMaxBands - 1);
        }

        Steinberg::Vst::ParamID selectedNodeParamId = makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandSelectedNode);

        VSTGUI::CRect rect(origin, size);
        return new NodeEditorBorder(rect, this, selectedNodeParamId);
    }

    if (std::strcmp(name, "CustomCurveEditor") == 0) {
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;
        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");
        if (originStr) {
            double x = 0.0, y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2)
                origin = VSTGUI::CPoint(x, y);
        }
        if (sizeStr) {
            double w = 200.0, h = 150.0;
            if (sscanf(sizeStr->c_str(), "%lf, %lf", &w, &h) == 2)
                size = VSTGUI::CPoint(w, h);
        } else {
            size = VSTGUI::CPoint(200.0, 150.0);
        }

        VSTGUI::CRect rect(origin, size);
        auto* curveEditor = new CustomCurveEditor(rect, nullptr, 9200);

        std::array<std::pair<float, float>, 8> points{};
        auto* pointCountParam = getParameterObject(
            makeSweepParamId(SweepParamType::kSweepCustomCurvePointCount));
        int pointCount = 2;
        if (pointCountParam) {
            pointCount = static_cast<int>(std::round(pointCountParam->toPlain(
                pointCountParam->getNormalized())));
            pointCount = std::clamp(pointCount, 2, 8);
        }

        for (int p = 0; p < pointCount; ++p) {
            auto xType = static_cast<SweepParamType>(
                static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0X) + p * 2);
            auto yType = static_cast<SweepParamType>(
                static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0Y) + p * 2);
            auto* xParam = getParameterObject(makeSweepParamId(xType));
            auto* yParam = getParameterObject(makeSweepParamId(yType));
            float px = 0.0f, py = 0.0f;
            if (p == 0) px = 0.0f;
            else if (p == 7) px = 1.0f;
            if (xParam) px = static_cast<float>(xParam->getNormalized());
            if (yParam) py = static_cast<float>(yParam->getNormalized());
            points[static_cast<size_t>(p)] = {px, py};
        }
        curveEditor->setBreakpoints(points, pointCount);

        curveEditor->setOnChange([this](int pointIndex, float x, float y) {
            auto xType = static_cast<SweepParamType>(
                static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0X) + pointIndex * 2);
            auto yType = static_cast<SweepParamType>(
                static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0Y) + pointIndex * 2);
            auto xId = makeSweepParamId(xType);
            auto yId = makeSweepParamId(yType);
            beginEdit(xId); setParamNormalized(xId, x); performEdit(xId, x); endEdit(xId);
            beginEdit(yId); setParamNormalized(yId, y); performEdit(yId, y); endEdit(yId);
        });

        curveEditor->setOnAdd([this](float x, float y) {
            auto pointCountId = makeSweepParamId(SweepParamType::kSweepCustomCurvePointCount);
            auto* pcParam = getParameterObject(pointCountId);
            if (pcParam) {
                int count = static_cast<int>(std::round(pcParam->toPlain(pcParam->getNormalized())));
                if (count < 8) {
                    count++;
                    double norm = pcParam->toNormalized(static_cast<double>(count));
                    beginEdit(pointCountId); setParamNormalized(pointCountId, norm);
                    performEdit(pointCountId, norm); endEdit(pointCountId);
                    int newIdx = count - 1;
                    auto xType = static_cast<SweepParamType>(
                        static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0X) + newIdx * 2);
                    auto yType = static_cast<SweepParamType>(
                        static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0Y) + newIdx * 2);
                    auto xId = makeSweepParamId(xType);
                    auto yId = makeSweepParamId(yType);
                    beginEdit(xId); setParamNormalized(xId, x); performEdit(xId, x); endEdit(xId);
                    beginEdit(yId); setParamNormalized(yId, y); performEdit(yId, y); endEdit(yId);
                }
            }
        });

        curveEditor->setOnRemove([this](int pointIndex) {
            auto pointCountId = makeSweepParamId(SweepParamType::kSweepCustomCurvePointCount);
            auto* pcParam = getParameterObject(pointCountId);
            if (pcParam) {
                int count = static_cast<int>(std::round(pcParam->toPlain(pcParam->getNormalized())));
                if (count > 2 && pointIndex > 0 && pointIndex < count - 1) {
                    for (int i = pointIndex; i < count - 1; ++i) {
                        auto srcXType = static_cast<SweepParamType>(
                            static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0X) + (i + 1) * 2);
                        auto srcYType = static_cast<SweepParamType>(
                            static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0Y) + (i + 1) * 2);
                        auto dstXType = static_cast<SweepParamType>(
                            static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0X) + i * 2);
                        auto dstYType = static_cast<SweepParamType>(
                            static_cast<uint8_t>(SweepParamType::kSweepCustomCurveP0Y) + i * 2);
                        auto* srcXParam = getParameterObject(makeSweepParamId(srcXType));
                        auto* srcYParam = getParameterObject(makeSweepParamId(srcYType));
                        auto dstXId = makeSweepParamId(dstXType);
                        auto dstYId = makeSweepParamId(dstYType);
                        if (srcXParam) {
                            double val = srcXParam->getNormalized();
                            beginEdit(dstXId); setParamNormalized(dstXId, val);
                            performEdit(dstXId, val); endEdit(dstXId);
                        }
                        if (srcYParam) {
                            double val = srcYParam->getNormalized();
                            beginEdit(dstYId); setParamNormalized(dstYId, val);
                            performEdit(dstYId, val); endEdit(dstYId);
                        }
                    }
                    count--;
                    double norm = pcParam->toNormalized(static_cast<double>(count));
                    beginEdit(pointCountId); setParamNormalized(pointCountId, norm);
                    performEdit(pointCountId, norm); endEdit(pointCountId);
                }
            }
        });

        return curveEditor;
    }

    if (std::strcmp(name, "SweepIndicator") == 0) {
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;
        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");
        if (originStr) {
            double x = 0.0, y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2)
                origin = VSTGUI::CPoint(x, y);
        }
        if (sizeStr) {
            double w = 980.0, h = 200.0;
            if (sscanf(sizeStr->c_str(), "%lf, %lf", &w, &h) == 2)
                size = VSTGUI::CPoint(w, h);
        } else {
            size = VSTGUI::CPoint(980.0, 200.0);
        }

        VSTGUI::CRect rect(origin, size);
        auto* sweepIndicator = new SweepIndicator(rect);

        auto* sweepEnableParam = getParameterObject(makeSweepParamId(SweepParamType::kSweepEnable));
        if (sweepEnableParam)
            sweepIndicator->setEnabled(sweepEnableParam->getNormalized() >= 0.5);

        auto* sweepFreqParam = getParameterObject(makeSweepParamId(SweepParamType::kSweepFrequency));
        auto* sweepWidthParam = getParameterObject(makeSweepParamId(SweepParamType::kSweepWidth));
        auto* sweepIntensityParam = getParameterObject(makeSweepParamId(SweepParamType::kSweepIntensity));
        auto* sweepFalloffParam = getParameterObject(makeSweepParamId(SweepParamType::kSweepFalloff));

        if (sweepFreqParam && sweepWidthParam && sweepIntensityParam) {
            constexpr float kSweepLog2Min = 4.321928f;
            constexpr float kSweepLog2Max = 14.287712f;
            constexpr float kSweepLog2Range = kSweepLog2Max - kSweepLog2Min;
            float freqNorm = static_cast<float>(sweepFreqParam->getNormalized());
            float log2Freq = kSweepLog2Min + freqNorm * kSweepLog2Range;
            float freqHz = std::pow(2.0f, log2Freq);
            constexpr float kMinWidth = 0.5f;
            constexpr float kMaxWidth = 4.0f;
            float widthNorm = static_cast<float>(sweepWidthParam->getNormalized());
            float widthOct = kMinWidth + widthNorm * (kMaxWidth - kMinWidth);
            float intensityNorm = static_cast<float>(sweepIntensityParam->getNormalized());
            float intensity = intensityNorm * 2.0f;
            sweepIndicator->setPosition(freqHz, widthOct, intensity);
        }

        if (sweepFalloffParam) {
            sweepIndicator->setFalloffMode(
                sweepFalloffParam->getNormalized() >= 0.5
                    ? SweepFalloff::Smooth : SweepFalloff::Sharp);
        }

        sweepIndicator_ = sweepIndicator;
        return sweepIndicator;
    }

    if (std::strcmp(name, "PresetBrowserButton") == 0) {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(80, 25);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new PresetBrowserButton(rect, this);
    }

    if (std::strcmp(name, "SavePresetButton") == 0) {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(60, 25);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new SavePresetButton(rect, this);
    }

    if (VSTGUI::UTF8StringView(name) == "UpdateBanner") {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(100, 30);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        auto* banner = new Krate::Plugins::UpdateBannerView(rect, updateChecker_.get());
        updateBannerView_ = banner;
        return banner;
    }

    if (VSTGUI::UTF8StringView(name) == "CheckForUpdatesButton") {
        VSTGUI::CPoint origin(0, 0);
        VSTGUI::CPoint size(120, 24);
        attributes.getPointAttribute("origin", origin);
        attributes.getPointAttribute("size", size);
        VSTGUI::CRect rect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);
        return new Krate::Plugins::CheckForUpdatesButton(rect, updateChecker_.get());
    }

    return nullptr;
}

VSTGUI::IController* Controller::createSubController(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* editor) {

    std::string_view sv(name);
    if (sv.size() > 1) {
        char lastChar = sv.back();
        if (lastChar >= '0' && lastChar <= '3') {
            int bandIndex = lastChar - '0';
            auto prefix = sv.substr(0, sv.size() - 1);
            if (prefix == "BandShapeTab" || prefix == "BandMainTab")
                return new BandSubController(bandIndex, editor);
            if (prefix == "BandExpandedStrip")
                return new BandExpandedStripController(bandIndex, editor);
        }
    }
    return nullptr;
}

void Controller::didOpen(VSTGUI::VST3Editor* editor) {
    activeEditor_ = editor;

    auto* bandCountParam = getParameterObject(makeGlobalParamId(GlobalParamType::kGlobalBandCount));
    if (bandCountParam) {
        for (int b = 0; b < kMaxBands; ++b) {
            float threshold = (static_cast<float>(b) - 0.5f) / 3.0f;
            Steinberg::int32 containerTag = 9000 + b;
            bandVisibilityControllers_[b] = new ContainerVisibilityController(
                &activeEditor_, bandCountParam, containerTag, threshold, false);
        }
        bandCountDisplayController_ = new BandCountDisplayController(&spectrumDisplay_, bandCountParam);
    }

    if (auto* spectrumModeParam = getParameterObject(makeGlobalParamId(GlobalParamType::kGlobalSpectrumMode)))
        spectrumModeController_ = new SpectrumModeController(&spectrumDisplay_, spectrumModeParam);

    for (int b = 0; b < kMaxBands; ++b) {
        auto* expandedParam = getParameterObject(
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandExpanded));
        if (expandedParam) {
            Steinberg::int32 expandedContainerTag = 9100 + b;
            Steinberg::int32 parentBandTag = 9000 + b;
            expandedVisibilityControllers_[b] = new AnimatedExpandController(
                &activeEditor_, expandedParam, expandedContainerTag,
                290.0f, 250, parentBandTag, b, 690.0f);
        }
    }

    auto* modPanelParam = getParameterObject(makeGlobalParamId(GlobalParamType::kGlobalModPanelVisible));
    if (modPanelParam)
        modPanelVisController_ = new ModPanelToggleController(&activeEditor_, modPanelParam, 9300);

    {
        double initWidth = 1000.0;
        double initHeight = 600.0;
        auto* mpParam = getParameterObject(makeGlobalParamId(GlobalParamType::kGlobalModPanelVisible));
        if (mpParam && mpParam->getNormalized() >= 0.5)
            initHeight += ModPanelToggleController::kModPanelHeight;
        editor->requestResize(VSTGUI::CPoint(initWidth, initHeight));
    }

    {
        auto* frame = editor->getFrame();
        if (frame) {
            int bandCount = 4;
            auto* bcParam = getParameterObject(makeGlobalParamId(GlobalParamType::kGlobalBandCount));
            if (bcParam)
                bandCount = static_cast<int>(std::round(bcParam->getNormalized() * 3.0)) + 1;

            keyboardHandler_ = std::make_unique<KeyboardShortcutHandler>(this, frame, bandCount);

            if (midiCCManager_) {
                keyboardHandler_->setEscapeCallback([this]() {
                    if (midiCCManager_ && midiCCManager_->isLearning()) {
                        midiCCManager_->cancelLearn();
                        setParamNormalized(makeGlobalParamId(GlobalParamType::kGlobalMidiLearnActive), 0.0);
                    }
                });
            }

            frame->registerKeyboardHook(keyboardHandler_.get());
            frame->setFocusDrawingEnabled(true);
            frame->setFocusColor(VSTGUI::CColor(0x3A, 0x96, 0xDD));
            frame->setFocusWidth(2.0);
        }
    }

    accessibilityPrefs_ = Krate::Plugins::queryAccessibilityPreferences();
    if (accessibilityPrefs_.reducedMotionPreferred) {
        for (auto& vc : expandedVisibilityControllers_) {
            if (auto* aec = dynamic_cast<AnimatedExpandController*>(vc.get()))
                aec->setAnimationsEnabled(false);
        }
    }

    if (accessibilityPrefs_.highContrastEnabled) {
        auto& colors = accessibilityPrefs_.colors;
        auto toCColor = [](uint32_t argb) {
            return VSTGUI::CColor(
                static_cast<uint8_t>((argb >> 16) & 0xFF),
                static_cast<uint8_t>((argb >> 8) & 0xFF),
                static_cast<uint8_t>(argb & 0xFF),
                static_cast<uint8_t>((argb >> 24) & 0xFF));
        };
        auto borderColor = toCColor(colors.border);
        auto bgColor = toCColor(colors.background);
        auto accentColor = toCColor(colors.accent);

        auto* frame = editor->getFrame();
        if (frame) frame->setBackgroundColor(bgColor);
        if (spectrumDisplay_) spectrumDisplay_->setHighContrastMode(true, borderColor, bgColor, accentColor);
        for (auto* mp : morphPads_) { if (mp) mp->setHighContrastMode(true, borderColor, accentColor); }
        if (sweepIndicator_) sweepIndicator_->setHighContrastMode(true, accentColor);
        for (auto* dns : dynamicNodeSelectors_) { if (dns) dns->setHighContrastMode(true); }
        for (auto* dns : compactNodeSelectors_) { if (dns) dns->setHighContrastMode(true); }
    }

    auto* sweepFreqParam = getParameterObject(makeSweepParamId(SweepParamType::kSweepFrequency));
    if (sweepFreqParam)
        morphSweepLinkController_ = new MorphSweepLinkController(this, sweepFreqParam);

    for (int b = 0; b < kMaxBands; ++b) {
        nodeSelectionControllers_[b] = new NodeSelectionController(
            this, static_cast<uint8_t>(b), shapeShadowStorage_[b].data());
    }

    sweepVisualizationController_ = new SweepVisualizationController(
        this, &sweepIndicator_, &spectrumDisplay_);

    sweepVisualizationTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
        [this](VSTGUI::CVSTGUITimer* /*timer*/) {
            if (sweepIndicator_ && sweepIndicator_->isEnabled())
                sweepIndicator_->setDirty();
            if (cachedModOffsets_ && activeEditor_) {
                if (auto* frame = activeEditor_->getFrame())
                    updateModSliders(frame);
            }
        }, 33);

    auto* morphLinkParam = getParameterObject(makeSweepParamId(SweepParamType::kSweepMorphLink));
    if (morphLinkParam)
        customCurveVisController_ = new ContainerVisibilityController(
            &activeEditor_, morphLinkParam, 9200, 0.93f, false);

    if (presetManager_) {
        auto* frame = editor->getFrame();
        if (frame) {
            auto frameSize = frame->getViewSize();
            presetBrowserView_ = new Krate::Plugins::PresetBrowserView(
                frameSize, presetManager_.get(), getDisrumpoTabLabels());
            frame->addView(presetBrowserView_);
            savePresetDialogView_ = new Krate::Plugins::SavePresetDialogView(
                frameSize, presetManager_.get());
            frame->addView(savePresetDialogView_);
        }
    }

    if (updateChecker_) updateChecker_->checkForUpdate(false);
    if (updateBannerView_) updateBannerView_->startPolling();
}

void Controller::willClose(VSTGUI::VST3Editor* editor) {
    for (auto& vc : bandVisibilityControllers_) {
        if (vc) {
            if (auto* cvc = dynamic_cast<ContainerVisibilityController*>(vc.get())) cvc->deactivate();
            vc = nullptr;
        }
    }
    for (auto& vc : expandedVisibilityControllers_) {
        if (vc) {
            if (auto* aec = dynamic_cast<AnimatedExpandController*>(vc.get())) aec->deactivate();
            vc = nullptr;
        }
    }
    if (morphSweepLinkController_) {
        if (auto* mslc = dynamic_cast<MorphSweepLinkController*>(morphSweepLinkController_.get())) mslc->deactivate();
        morphSweepLinkController_ = nullptr;
    }
    for (auto& nsc : nodeSelectionControllers_) {
        if (nsc) {
            if (auto* c = dynamic_cast<NodeSelectionController*>(nsc.get())) c->deactivate();
            nsc = nullptr;
        }
    }
    for (auto& dns : dynamicNodeSelectors_) { if (dns) { dns->deactivate(); dns = nullptr; } }
    for (auto& dns : compactNodeSelectors_) { if (dns) { dns->deactivate(); dns = nullptr; } }
    for (auto& mp : morphPads_) { if (mp) { mp->deactivate(); mp = nullptr; } }
    if (sweepVisualizationController_) {
        if (auto* svc = dynamic_cast<SweepVisualizationController*>(sweepVisualizationController_.get())) svc->deactivate();
        sweepVisualizationController_ = nullptr;
    }
    if (sweepVisualizationTimer_) { sweepVisualizationTimer_->stop(); sweepVisualizationTimer_ = nullptr; }
    if (customCurveVisController_) {
        if (auto* cvc = dynamic_cast<ContainerVisibilityController*>(customCurveVisController_.get())) cvc->deactivate();
        customCurveVisController_ = nullptr;
    }
    if (bandCountDisplayController_) {
        if (auto* bcdc = dynamic_cast<BandCountDisplayController*>(bandCountDisplayController_.get())) bcdc->deactivate();
        bandCountDisplayController_ = nullptr;
    }
    if (spectrumModeController_) {
        if (auto* smc = dynamic_cast<SpectrumModeController*>(spectrumModeController_.get())) smc->deactivate();
        spectrumModeController_ = nullptr;
    }
    if (keyboardHandler_) {
        auto* frame = editor->getFrame();
        if (frame) frame->unregisterKeyboardHook(keyboardHandler_.get());
        keyboardHandler_.reset();
    }
    if (modPanelVisController_) {
        if (auto* mtc = dynamic_cast<ModPanelToggleController*>(modPanelVisController_.get())) mtc->deactivate();
        modPanelVisController_ = nullptr;
    }
    presetBrowserView_ = nullptr;
    savePresetDialogView_ = nullptr;
    if (crossoverDragBridge_) {
        if (auto* bridge = dynamic_cast<CrossoverDragBridge*>(crossoverDragBridge_.get())) bridge->deactivate();
        crossoverDragBridge_ = nullptr;
    }
    if (spectrumDisplay_) spectrumDisplay_->stopAnalysis();
    sweepIndicator_ = nullptr;
    spectrumDisplay_ = nullptr;
    activeEditor_ = nullptr;
    if (updateBannerView_) { updateBannerView_->stopPolling(); updateBannerView_ = nullptr; }
    (void)editor;
}

// ==============================================================================
// MIDI Learn Context Menu (Spec 012 FR-031)
// ==============================================================================

bool Controller::findParameter(
    const VSTGUI::CPoint& pos,
    Steinberg::Vst::ParamID& paramID,
    VSTGUI::VST3Editor* editor) {
    if (!editor) return false;
    auto* frame = editor->getFrame();
    if (!frame) return false;
    VSTGUI::CPoint localPos(pos);
    auto* hitView = frame->getViewAt(localPos, VSTGUI::GetViewOptions().deep());
    if (!hitView) return false;
    auto* control = dynamic_cast<VSTGUI::CControl*>(hitView);
    if (!control) return false;
    auto tag = control->getTag();
    if (tag < 0) return false;
    paramID = static_cast<Steinberg::Vst::ParamID>(tag);
    return true;
}

VSTGUI::COptionMenu* Controller::createContextMenu(
    const VSTGUI::CPoint& pos,
    VSTGUI::VST3Editor* editor) {

    Steinberg::Vst::ParamID paramId = 0;
    if (!findParameter(pos, paramId, editor)) return nullptr;
    if (!midiCCManager_) return nullptr;

    auto* menu = new VSTGUI::COptionMenu();
    {
        VSTGUI::CCommandMenuItem::Desc desc("MIDI Learn");
        auto* learnItem = new VSTGUI::CCommandMenuItem(std::move(desc));
        learnItem->setActions([this, paramId](VSTGUI::CCommandMenuItem*) {
            if (midiCCManager_) {
                midiCCManager_->startLearn(paramId);
                setParamNormalized(makeGlobalParamId(GlobalParamType::kGlobalMidiLearnActive), 1.0);
            }
        });
        menu->addEntry(learnItem);
    }

    uint8_t existingCC = 0;
    if (midiCCManager_->getCCForParam(paramId, existingCC)) {
        {
            VSTGUI::CCommandMenuItem::Desc clearDesc("Clear MIDI Learn");
            auto* clearItem = new VSTGUI::CCommandMenuItem(std::move(clearDesc));
            clearItem->setActions([this, paramId](VSTGUI::CCommandMenuItem*) {
                if (midiCCManager_) midiCCManager_->removeMappingsForParam(paramId);
            });
            menu->addEntry(clearItem);
        }
        Krate::Plugins::MidiCCMapping mapping;
        if (midiCCManager_->getMapping(existingCC, mapping)) {
            VSTGUI::CCommandMenuItem::Desc presetDesc("Save Mapping with Preset");
            auto* presetItem = new VSTGUI::CCommandMenuItem(std::move(presetDesc));
            presetItem->setActions([this, existingCC, paramId, mapping](VSTGUI::CCommandMenuItem*) {
                if (midiCCManager_) {
                    if (!mapping.isPerPreset) {
                        midiCCManager_->removeGlobalMapping(existingCC);
                        midiCCManager_->addPresetMapping(existingCC, paramId, mapping.is14Bit);
                    } else {
                        midiCCManager_->removePresetMapping(existingCC);
                        midiCCManager_->addGlobalMapping(existingCC, paramId, mapping.is14Bit);
                    }
                }
            });
            menu->addEntry(presetItem);
        }
    }

    return menu;
}

// ==============================================================================
// Preset Browser (Spec 010)
// ==============================================================================

void Controller::openPresetBrowser() {
    if (presetBrowserView_ && !presetBrowserView_->isOpen()) presetBrowserView_->open();
}

void Controller::openSavePresetDialog() {
    if (savePresetDialogView_ && !savePresetDialogView_->isOpen()) savePresetDialogView_->open("");
}

void Controller::closePresetBrowser() {
    if (presetBrowserView_ && presetBrowserView_->isOpen()) presetBrowserView_->close();
}

// ==============================================================================
// State Serialization for Preset Saving
// ==============================================================================
// createComponentStateStream, editParamWithNotify, loadComponentStateWithNotify
// are defined in state_serialization.cpp (or kept here if not yet split)
// For now they remain in this file - see original lines 4812-5512.

Steinberg::MemoryStream* Controller::createComponentStateStream() {
    auto* stream = new Steinberg::MemoryStream();
    Steinberg::IBStreamer streamer(stream, kLittleEndian);

    auto getParamNorm = [this](Steinberg::Vst::ParamID id) -> float {
        if (auto* param = getParameterObject(id)) return static_cast<float>(param->getNormalized());
        return 0.0f;
    };
    auto getFloat = [this](Steinberg::Vst::ParamID id, float defaultVal) -> float {
        if (auto* param = getParameterObject(id)) return static_cast<float>(param->toPlain(param->getNormalized()));
        return defaultVal;
    };
    auto getInt8FromList = [this](Steinberg::Vst::ParamID id, int maxVal) -> Steinberg::int8 {
        if (auto* param = getParameterObject(id)) return static_cast<Steinberg::int8>(std::round(param->getNormalized() * maxVal));
        return 0;
    };
    auto getBoolInt8 = [this](Steinberg::Vst::ParamID id) -> Steinberg::int8 {
        if (auto* param = getParameterObject(id)) return static_cast<Steinberg::int8>(param->getNormalized() >= 0.5 ? 1 : 0);
        return 0;
    };

    streamer.writeInt32(kPresetVersion);

    // Global
    streamer.writeFloat(getParamNorm(makeGlobalParamId(GlobalParamType::kGlobalInputGain)));
    streamer.writeFloat(getParamNorm(makeGlobalParamId(GlobalParamType::kGlobalOutputGain)));
    streamer.writeFloat(getParamNorm(makeGlobalParamId(GlobalParamType::kGlobalMix)));

    int32_t bandCount = static_cast<int32_t>(std::round(
        getParamNorm(makeGlobalParamId(GlobalParamType::kGlobalBandCount)) * 3.0f)) + 1;
    streamer.writeInt32(bandCount);

    for (int b = 0; b < kMaxBands; ++b) {
        auto band = static_cast<uint8_t>(b);
        streamer.writeFloat(getFloat(makeBandParamId(band, BandParamType::kBandGain), 0.0f));
        streamer.writeFloat(getFloat(makeBandParamId(band, BandParamType::kBandPan), 0.0f));
        streamer.writeInt8(getBoolInt8(makeBandParamId(band, BandParamType::kBandSolo)));
        streamer.writeInt8(getBoolInt8(makeBandParamId(band, BandParamType::kBandBypass)));
        streamer.writeInt8(getBoolInt8(makeBandParamId(band, BandParamType::kBandMute)));
    }

    for (int c = 0; c < kMaxBands - 1; ++c)
        streamer.writeFloat(getFloat(makeCrossoverParamId(static_cast<uint8_t>(c)), 1000.0f));

    // Sweep
    streamer.writeInt8(getBoolInt8(makeSweepParamId(SweepParamType::kSweepEnable)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepFrequency)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepWidth)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepIntensity)));
    streamer.writeInt8(getBoolInt8(makeSweepParamId(SweepParamType::kSweepFalloff)));
    streamer.writeInt8(getInt8FromList(makeSweepParamId(SweepParamType::kSweepMorphLink), kMorphLinkModeCount - 1));
    streamer.writeInt8(getBoolInt8(makeSweepParamId(SweepParamType::kSweepLFOEnable)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepLFORate)));
    streamer.writeInt8(getInt8FromList(makeSweepParamId(SweepParamType::kSweepLFOWaveform), 5));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepLFODepth)));
    streamer.writeInt8(getBoolInt8(makeSweepParamId(SweepParamType::kSweepLFOSync)));
    streamer.writeInt8(getInt8FromList(makeSweepParamId(SweepParamType::kSweepLFONoteValue), 14));
    streamer.writeInt8(getBoolInt8(makeSweepParamId(SweepParamType::kSweepEnvEnable)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepEnvAttack)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepEnvRelease)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepEnvSensitivity)));
    streamer.writeInt32(2);
    streamer.writeFloat(0.0f); streamer.writeFloat(0.0f);
    streamer.writeFloat(1.0f); streamer.writeFloat(1.0f);

    // Modulation
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kLFO1Rate)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kLFO1Shape), 5));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kLFO1Phase)));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO1Sync)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kLFO1NoteValue), 14));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO1Unipolar)));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO1Retrigger)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kLFO2Rate)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kLFO2Shape), 5));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kLFO2Phase)));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO2Sync)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kLFO2NoteValue), 14));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO2Unipolar)));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO2Retrigger)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kEnvFollowerAttack)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kEnvFollowerRelease)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kEnvFollowerSensitivity)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kEnvFollowerSource), 4));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kRandomRate)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kRandomSmoothness)));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kRandomSync)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kChaosModel), 3));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kChaosSpeed)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kChaosCoupling)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kSampleHoldSource), 3));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kSampleHoldRate)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kSampleHoldSlew)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kPitchFollowerMinHz)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kPitchFollowerMaxHz)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kPitchFollowerConfidence)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kPitchFollowerTrackingSpeed)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kTransientSensitivity)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kTransientAttack)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kTransientDecay)));

    constexpr ModParamType macroParams[4][4] = {
        {ModParamType::kMacro1Value, ModParamType::kMacro1Min, ModParamType::kMacro1Max, ModParamType::kMacro1Curve},
        {ModParamType::kMacro2Value, ModParamType::kMacro2Min, ModParamType::kMacro2Max, ModParamType::kMacro2Curve},
        {ModParamType::kMacro3Value, ModParamType::kMacro3Min, ModParamType::kMacro3Max, ModParamType::kMacro3Curve},
        {ModParamType::kMacro4Value, ModParamType::kMacro4Min, ModParamType::kMacro4Max, ModParamType::kMacro4Curve},
    };
    for (const auto& macro : macroParams) {
        streamer.writeFloat(getParamNorm(makeModParamId(macro[0])));
        streamer.writeFloat(getParamNorm(makeModParamId(macro[1])));
        streamer.writeFloat(getParamNorm(makeModParamId(macro[2])));
        streamer.writeInt8(getInt8FromList(makeModParamId(macro[3]), 3));
    }

    for (uint8_t r = 0; r < 32; ++r) {
        streamer.writeInt8(getInt8FromList(makeRoutingParamId(r, 0), 12));
        auto destNorm = getParamNorm(makeRoutingParamId(r, 1));
        streamer.writeInt32(static_cast<int32_t>(
            std::round(destNorm * static_cast<float>(ModDest::kTotalDestinations - 1))));
        float amountNorm = getParamNorm(makeRoutingParamId(r, 2));
        streamer.writeFloat(amountNorm * 2.0f - 1.0f);
        streamer.writeInt8(getInt8FromList(makeRoutingParamId(r, 3), 3));
    }

    // Morph node state
    for (int b = 0; b < kMaxBands; ++b) {
        auto band = static_cast<uint8_t>(b);
        streamer.writeFloat(getParamNorm(makeBandParamId(band, BandParamType::kBandMorphX)));
        streamer.writeFloat(getParamNorm(makeBandParamId(band, BandParamType::kBandMorphY)));
        streamer.writeInt8(getInt8FromList(makeBandParamId(band, BandParamType::kBandMorphMode), 2));
        float activeNodesNorm = getParamNorm(makeBandParamId(band, BandParamType::kBandActiveNodes));
        int activeNodes = static_cast<int>(std::round(activeNodesNorm * 2.0f)) + 2;
        streamer.writeInt8(static_cast<Steinberg::int8>(activeNodes));
        float smoothingNorm = getParamNorm(makeBandParamId(band, BandParamType::kBandMorphSmoothing));
        streamer.writeFloat(smoothingNorm * 500.0f);

        for (int n = 0; n < kMaxMorphNodes; ++n) {
            auto node = static_cast<uint8_t>(n);
            streamer.writeInt8(getInt8FromList(makeNodeParamId(band, node, NodeParamType::kNodeType), 25));
            float driveNorm = getParamNorm(makeNodeParamId(band, node, NodeParamType::kNodeDrive));
            streamer.writeFloat(driveNorm * 10.0f);
            streamer.writeFloat(getParamNorm(makeNodeParamId(band, node, NodeParamType::kNodeMix)));
            float toneNorm = getParamNorm(makeNodeParamId(band, node, NodeParamType::kNodeTone));
            streamer.writeFloat(toneNorm * 7800.0f + 200.0f);
            float biasNorm = getParamNorm(makeNodeParamId(band, node, NodeParamType::kNodeBias));
            streamer.writeFloat(biasNorm * 2.0f - 1.0f);
            float foldsNorm = getParamNorm(makeNodeParamId(band, node, NodeParamType::kNodeFolds));
            streamer.writeFloat(foldsNorm * 11.0f + 1.0f);
            float bitDepthNorm = getParamNorm(makeNodeParamId(band, node, NodeParamType::kNodeBitDepth));
            streamer.writeFloat(bitDepthNorm * 20.0f + 4.0f);

            for (int s = 0; s < 10; ++s) {
                auto shapeType = static_cast<NodeParamType>(
                    static_cast<uint8_t>(NodeParamType::kNodeShape0) + s);
                streamer.writeFloat(getParamNorm(makeNodeParamId(band, node, shapeType)));
            }

            const auto& shadow = shapeShadowStorage_[b][n];
            for (const auto& typeSlot : shadow.typeSlots) {
                for (float slotValue : typeSlot)
                    streamer.writeFloat(slotValue);
            }
        }
    }

    return stream;
}

void Controller::editParamWithNotify(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
    value = std::max(0.0, std::min(1.0, value));
    beginEdit(id);
    setParamNormalized(id, value);
    performEdit(id, value);
    endEdit(id);
}

bool Controller::loadComponentStateWithNotify(Steinberg::IBStream* state) {
    if (!state) return false;

    Steinberg::IBStreamer streamer(state, kLittleEndian);
    int32_t version = 0;
    if (!streamer.readInt32(version)) return false;
    if (version < 1) return false;

    bulkParamLoad_ = true;
    FrameInvalidationGuard frameGuard(activeEditor_);

    auto setter = [this](Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
        editParamWithNotify(id, value);
    };

    bool ok = parseComponentState(streamer, version, setter);

    bulkParamLoad_ = false;
    syncAllViews();
    return ok;
}

// ==============================================================================
// Bulk Parameter Load - Batch View Sync
// ==============================================================================

void Controller::syncAllViews() {
    // Custom views are driven by FIFOs/timers/IDependent, not per-param pushes.
    // The FrameInvalidationGuard's full-frame repaint handles VSTGUI controls.
}

// ==============================================================================
// IDataExchangeReceiver implementation (Spectrum Analyzer)
// ==============================================================================

void PLUGIN_API Controller::queueOpened(
    Steinberg::Vst::DataExchangeUserContextID /*userContextID*/,
    Steinberg::uint32 /*blockSize*/,
    Steinberg::TBool& dispatchOnBackgroundThread)
{
    dispatchOnBackgroundThread = false;
}

void PLUGIN_API Controller::queueClosed(
    Steinberg::Vst::DataExchangeUserContextID /*userContextID*/)
{
}

void PLUGIN_API Controller::onDataExchangeBlocksReceived(
    Steinberg::Vst::DataExchangeUserContextID /*userContextID*/,
    Steinberg::uint32 numBlocks,
    Steinberg::Vst::DataExchangeBlock* blocks,
    Steinberg::TBool /*onBackgroundThread*/)
{
    for (Steinberg::uint32 i = 0; i < numBlocks; ++i) {
        if (blocks[i].data && blocks[i].size >= sizeof(SpectrumBlock)) {
            const auto* specBlock = static_cast<const SpectrumBlock*>(blocks[i].data);
            if (specBlock->numSamples > 0 && specBlock->numSamples <= kSpectrumBlockMaxSamples) {
                localInputFIFO_.push(specBlock->inputSamples, specBlock->numSamples);
                localOutputFIFO_.push(specBlock->outputSamples, specBlock->numSamples);
                cachedSpectrumSampleRate_ = specBlock->sampleRate;
                if (!spectrumDataAvailable_) {
                    spectrumDataAvailable_ = true;
                    if (spectrumDisplay_) {
                        spectrumDisplay_->setSpectrumFIFOs(&localInputFIFO_, &localOutputFIFO_);
                        spectrumDisplay_->startAnalysis(cachedSpectrumSampleRate_);
                    }
                }
            }
        }
    }
}

// ==============================================================================
// IMessage Handling
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::notify(Steinberg::Vst::IMessage* message) {
    if (!message) return Steinberg::kInvalidArgument;
    if (dataExchangeReceiver_.onMessage(message)) return Steinberg::kResultOk;

    if (strcmp(message->getMessageID(), "ModOffsets") == 0) {
        auto* attrs = message->getAttributes();
        if (!attrs) return Steinberg::kResultFalse;
        Steinberg::int64 ptr = 0;
        attrs->getInt("ptr", ptr);
        cachedModOffsets_ = reinterpret_cast<const float*>(  // NOLINT(performance-no-int-to-ptr)
            static_cast<intptr_t>(ptr));
        return Steinberg::kResultOk;
    }

    return Steinberg::Vst::EditControllerEx1::notify(message);
}

void Controller::updateModSliders(VSTGUI::CViewContainer* container) {
    if (!container) return;
    container->forEachChild([this](VSTGUI::CView* child) {
        if (auto* ms = dynamic_cast<ModSlider*>(child)) {
            float offset = cachedModOffsets_[ms->getModDestId()];
            ms->setModulationOffset(offset);
        }
        if (auto* vc = dynamic_cast<VSTGUI::CViewContainer*>(child))
            updateModSliders(vc);
    });
}

} // namespace Disrumpo
