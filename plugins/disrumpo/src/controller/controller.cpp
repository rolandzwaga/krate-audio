// ==============================================================================
// Edit Controller Implementation
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation
// Constitution Principle V: VSTGUI Development
// ==============================================================================

#include "controller.h"
#include "controller/sub_controllers.h"
#include "plugin_ids.h"
#include "state_codec.h"
#include <krate/dsp/core/modulation_types.h>
#include "version.h"
#include "dsp/band_state.h"
#include "preset/disrumpo_preset_config.h"
#include "controller/views/spectrum_display.h"
#include "controller/views/morph_pad.h"
#include "controller/views/dynamic_node_selector.h"
#include "controller/views/node_editor_border.h"
#include "controller/views/sweep_indicator.h"
#include "controller/views/mod_slider.h"
#include "controller/views/level_meter_view.h"
#include "controller/animated_expand_controller.h"
#include "controller/keyboard_shortcut_handler.h"
#include "controller/visibility_controllers.h"
#include "controller/format_helpers.h"
#include "controller/custom_buttons.h"
#include "dsp/sweep_morph_link.h"
#include "dsp/custom_curve.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"
#include "ui/update_banner_view.h"
#include "update/disrumpo_update_config.h"
#include "platform/accessibility_helper.h"
#include "midi/midi_cc_manager.h"
#include "display/shared_display_bridge.h"
#include "display/display_bridge_log.h"

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

#include <algorithm>
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
    editorClosing_.store(true, std::memory_order_release);
    sharedDisplay_ = nullptr;
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

namespace {

/// Param ID of custom-curve breakpoint `index`'s X (or Y) coordinate.
Steinberg::Vst::ParamID customCurvePointParamId(int index, bool y) {
    const auto base = static_cast<uint8_t>(y ? SweepParamType::kSweepCustomCurveP0Y : SweepParamType::kSweepCustomCurveP0X);
    return makeSweepParamId(static_cast<SweepParamType>(base + index * 2));
}

}  // namespace

template <typename ParamSetter>
bool Controller::parseComponentState(Steinberg::IBStreamer& streamer, int32_t version,
                                     ParamSetter&& setter) { // NOLINT(cppcoreguidelines-missing-std-forward): setter is intentionally used as callable without forwarding
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
            setter(makeGlobalParamId(GlobalParamType::kGlobalBandCount), listIndexToNorm(bandCount - 1, kMaxBands));
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
                if (i < kMaxBands - 1)
                    setter(makeCrossoverParamId(static_cast<uint8_t>(i)), crossoverHzToNorm(freq));
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
            setter(makeSweepParamId(SweepParamType::kSweepMorphLink), listIndexToNorm(sweepMorphLink, kMorphLinkModeCount));

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
            setter(makeSweepParamId(SweepParamType::kSweepLFOWaveform), listIndexToNorm(lfoWaveform, kLfoShapeCount));
        if (streamer.readFloat(lfoDepth))
            setter(makeSweepParamId(SweepParamType::kSweepLFODepth), lfoDepth);
        if (streamer.readInt8(lfoSync))
            setter(makeSweepParamId(SweepParamType::kSweepLFOSync), lfoSync != 0 ? 1.0 : 0.0);
        if (streamer.readInt8(lfoNoteIndex)) {
            int noteIdx = static_cast<int>(static_cast<unsigned char>(lfoNoteIndex));
            if (version <= 9)
                noteIdx = kOldNoteIdxToNewDropdown[std::clamp(noteIdx, 0, 14)];
            setter(makeSweepParamId(SweepParamType::kSweepLFONoteValue), listIndexToNorm(noteIdx, kNoteValueCount));
        }

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

        // Custom Curve: mirrored into the curve params, which the preset writer stores
        int32_t pointCount = kMinBreakpoints;
        if (streamer.readInt32(pointCount)) {
            pointCount = std::clamp(pointCount, kMinBreakpoints, kMaxBreakpoints);
            setter(makeSweepParamId(SweepParamType::kSweepCustomCurvePointCount),
                   listIndexToNorm(pointCount - kMinBreakpoints, kMaxBreakpoints - kMinBreakpoints + 1));
            for (int32_t i = 0; i < pointCount; ++i) {
                float px = 0.0f;
                float py = 0.0f;
                const bool readX = streamer.readFloat(px);
                const bool readY = streamer.readFloat(py);
                if (readX)
                    setter(customCurvePointParamId(i, false), static_cast<double>(px));
                if (readY)
                    setter(customCurvePointParamId(i, true), static_cast<double>(py));
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
            setter(makeModParamId(ModParamType::kLFO1Shape), listIndexToNorm(lfo1Shape, kLfoShapeCount));
        float lfo1Phase = 0.0f;
        if (streamer.readFloat(lfo1Phase))
            setter(makeModParamId(ModParamType::kLFO1Phase), lfo1Phase);
        Steinberg::int8 lfo1Sync = 0;
        if (streamer.readInt8(lfo1Sync))
            setter(makeModParamId(ModParamType::kLFO1Sync), lfo1Sync != 0 ? 1.0 : 0.0);
        Steinberg::int8 lfo1NoteIdx = 0;
        if (streamer.readInt8(lfo1NoteIdx)) {
            int noteIdx = static_cast<int>(static_cast<unsigned char>(lfo1NoteIdx));
            if (version <= 9)
                noteIdx = kOldNoteIdxToNewDropdown[std::clamp(noteIdx, 0, 14)];
            setter(makeModParamId(ModParamType::kLFO1NoteValue), listIndexToNorm(noteIdx, kNoteValueCount));
        }
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
            setter(makeModParamId(ModParamType::kLFO2Shape), listIndexToNorm(lfo2Shape, kLfoShapeCount));
        float lfo2Phase = 0.0f;
        if (streamer.readFloat(lfo2Phase))
            setter(makeModParamId(ModParamType::kLFO2Phase), lfo2Phase);
        Steinberg::int8 lfo2Sync = 0;
        if (streamer.readInt8(lfo2Sync))
            setter(makeModParamId(ModParamType::kLFO2Sync), lfo2Sync != 0 ? 1.0 : 0.0);
        Steinberg::int8 lfo2NoteIdx = 0;
        if (streamer.readInt8(lfo2NoteIdx)) {
            int noteIdx = static_cast<int>(static_cast<unsigned char>(lfo2NoteIdx));
            if (version <= 9)
                noteIdx = kOldNoteIdxToNewDropdown[std::clamp(noteIdx, 0, 14)];
            setter(makeModParamId(ModParamType::kLFO2NoteValue), listIndexToNorm(noteIdx, kNoteValueCount));
        }
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
            setter(makeModParamId(ModParamType::kEnvFollowerSource), listIndexToNorm(envSource, kEnvFollowerSourceCount));

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
            setter(makeModParamId(ModParamType::kChaosModel), listIndexToNorm(chaosModel, kChaosModelCount));
        float chaosSpeedNorm = 0.0f;
        if (streamer.readFloat(chaosSpeedNorm))
            setter(makeModParamId(ModParamType::kChaosSpeed), chaosSpeedNorm);
        float chaosCoupling = 0.0f;
        if (streamer.readFloat(chaosCoupling))
            setter(makeModParamId(ModParamType::kChaosCoupling), chaosCoupling);

        // Sample & Hold
        Steinberg::int8 shSource = 0;
        if (streamer.readInt8(shSource))
            setter(makeModParamId(ModParamType::kSampleHoldSource), listIndexToNorm(shSource, kSampleHoldSourceCount));
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

        // Rungler (v11+: 4 values: rate, depth, bits, loop)
        // Controller doesn't use these but must read them to keep stream aligned
        if (version >= 11) {
            float runglerRateNorm = 0.0f;
            streamer.readFloat(runglerRateNorm);
            setter(makeModParamId(ModParamType::kRunglerRate), runglerRateNorm);
            float runglerDepth = 0.5f;
            streamer.readFloat(runglerDepth);
            setter(makeModParamId(ModParamType::kRunglerDepth), runglerDepth);
            Steinberg::int8 runglerBits = 8;
            streamer.readInt8(runglerBits);
            setter(makeModParamId(ModParamType::kRunglerBits), listIndexToNorm(runglerBits - 4, kRunglerBitsCount));
            Steinberg::int8 runglerLoop = 0;
            streamer.readInt8(runglerLoop);
            setter(makeModParamId(ModParamType::kRunglerLoop), runglerLoop != 0 ? 1.0 : 0.0);
        }

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
                setter(makeModParamId(macro[3]), listIndexToNorm(macroCurve, Krate::DSP::kModCurveCount));
        }

        // Routing
        for (uint8_t r = 0; r < 32; ++r) {
            Steinberg::int8 source = 0;
            if (streamer.readInt8(source))  // v8 and older: pre-Rungler numbering (D-055)
                setter(makeRoutingParamId(r, 0),
                       listIndexToNorm(migrateModSourceIndex(source, version), kUIModSourceCount));
            int32_t dest = 0;
            if (streamer.readInt32(dest)) {
                // v12: migrate dest indices from old 6-per-band to new 8-per-band layout
                constexpr int32_t kBandBaseI = static_cast<int32_t>(ModDest::kBandBase);
                constexpr int32_t kParamsPerBandI = static_cast<int32_t>(ModDest::kParamsPerBand);
                constexpr int32_t kTotalDestsI = static_cast<int32_t>(ModDest::kTotalDestinations);
                if (version <= 11 && dest >= kBandBaseI) {
                    constexpr int32_t kOldParamsPerBand = 6;
                    const int32_t bandRelative = dest - kBandBaseI;
                    const int32_t oldBand = bandRelative / kOldParamsPerBand;
                    const int32_t oldOffset = bandRelative % kOldParamsPerBand;
                    dest = kBandBaseI
                         + oldBand * kParamsPerBandI
                         + oldOffset;
                }
                setter(makeRoutingParamId(r, 1), listIndexToNorm(dest, kTotalDestsI));
            }
            float amount = 0.0f;
            if (streamer.readFloat(amount))
                setter(makeRoutingParamId(r, 2), static_cast<double>(amount + 1.0f) / 2.0);
            Steinberg::int8 curve = 0;
            if (streamer.readInt8(curve))
                setter(makeRoutingParamId(r, 3), listIndexToNorm(curve, Krate::DSP::kModCurveCount));
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

            // v13: sweep -> morph link modes; older streams have none (None)
            Steinberg::int8 xLink = 0;
            Steinberg::int8 yLink = 0;
            if (version >= 13) {
                streamer.readInt8(xLink);
                streamer.readInt8(yLink);
            }

            if (b < kMaxBands) {
                setter(makeBandParamId(band, BandParamType::kBandMorphXLink), listIndexToNorm(xLink, kMorphLinkModeCount));
                setter(makeBandParamId(band, BandParamType::kBandMorphYLink), listIndexToNorm(yLink, kMorphLinkModeCount));
                if (readMorphX)
                    setter(makeBandParamId(band, BandParamType::kBandMorphX), static_cast<double>(morphX));
                if (readMorphY)
                    setter(makeBandParamId(band, BandParamType::kBandMorphY), static_cast<double>(morphY));
                if (readMorphMode)
                    setter(makeBandParamId(band, BandParamType::kBandMorphMode), listIndexToNorm(morphMode, kMorphModeCount));
                if (readActiveNodes)
                    setter(makeBandParamId(band, BandParamType::kBandActiveNodes), listIndexToNorm(activeNodes - 1, kMaxMorphNodes));
                // kMorphSmoothingMsRange.toNorm in double precision (the range starts at 0 ms)
                static_assert(kMorphSmoothingMsRange.min == 0.0f);
                if (readMorphSmoothing)
                    setter(makeBandParamId(band, BandParamType::kBandMorphSmoothing),
                           static_cast<double>(morphSmoothing) / static_cast<double>(kMorphSmoothingMsRange.span));
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
                        setter(makeNodeParamId(band, node, NodeParamType::kNodeType), listIndexToNorm(nodeType, kDistortionTypeCount));
                    if (rDrive)
                        setter(makeNodeParamId(band, node, NodeParamType::kNodeDrive), static_cast<double>(drive) / 10.0);
                    if (rMix)
                        setter(makeNodeParamId(band, node, NodeParamType::kNodeMix), static_cast<double>(mix));
                    if (rTone)
                        setter(makeNodeParamId(band, node, NodeParamType::kNodeTone), static_cast<double>(tone - 200.0f) / 7800.0);
                }
                // Bias, Folds and Bit Depth: before v9 as stored; from v9 after the shape slots,
                // as the node's type maps them from its slots, so the node parameters mirror the
                // slot that wins in the processor (follow-ups 34 and 87, user ruling 2026-11-09)
                const auto emitDualParams = [&](float biasValue, float foldsValue, float bitDepthValue) {
                    if (b >= kMaxBands) return;
                    if (rBias)
                        setter(makeNodeParamId(band, node, NodeParamType::kNodeBias),
                               static_cast<double>(biasValue + 1.0f) / 2.0);
                    if (rFolds)
                        setter(makeNodeParamId(band, node, NodeParamType::kNodeFolds),
                               static_cast<double>(foldsValue - 1.0f) / 11.0);
                    if (rBitDepth)
                        setter(makeNodeParamId(band, node, NodeParamType::kNodeBitDepth),
                               static_cast<double>(bitDepthValue - 4.0f) / 20.0);
                };
                if (version < 9) {
                    emitDualParams(bias, folds, bitDepth);
                }

                if (version >= 9) {
                    std::array<float, MorphNode::kShapeSlotCount> slots{};
                    for (int s = 0; s < 10; ++s) {
                        float slotValue;
                        if (streamer.readFloat(slotValue)) {
                            slots[static_cast<size_t>(s)] = slotValue;
                            if (b < kMaxBands) {
                                auto shapeType = static_cast<NodeParamType>(
                                    static_cast<uint8_t>(NodeParamType::kNodeShape0) + s);
                                setter(makeNodeParamId(band, node, shapeType), static_cast<double>(slotValue));
                            }
                        }
                    }
                    DistortionParams mirrored;
                    mirrored.bias = bias;
                    mirrored.folds = folds;
                    mirrored.bitDepth = bitDepth;
                    applyShapeSlots(
                        static_cast<DistortionType>(std::clamp(static_cast<int>(nodeType), 0, kDistortionTypeCount - 1)),
                        slots.data(), mirrored);
                    emitDualParams(mirrored.bias, mirrored.folds, mirrored.bitDepth);

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

    // SharedDisplayBridge: try to read instance ID from state trailer
    {
        Steinberg::int32 marker = 0;
        Steinberg::int64 storedId = 0;
        if (streamer.readInt32(marker) && marker == kInstanceIdMarker
            && streamer.readInt64(storedId))
        {
            instanceId_ = static_cast<uint64_t>(storedId);
            sharedDisplay_ = static_cast<SharedDisplay*>(
                Krate::Plugins::SharedDisplayBridge::instance().lookupInstance(instanceId_));
            KRATE_BRIDGE_LOG("Disrumpo::Controller::setComponentState() — id=0x%llx, bridge=%s",
                static_cast<unsigned long long>(instanceId_),
                sharedDisplay_ ? "found" : "NOT found");
        }
    }

    bulkParamLoad_ = false;
    syncAllViews();
    return Steinberg::kResultOk;
}

// Controller state: int32 version, two unused doubles (old window size), then
// int32 size + global MIDI CC mapping blob; version 3 adds int32 size + the
// per-preset mapping blob ("Save Mapping with Preset", D-058).
namespace {
constexpr Steinberg::int32 kControllerStateVersion = 3;

bool writeMidiBlob(Steinberg::IBStream* state, Steinberg::IBStreamer& streamer, std::vector<uint8_t> blob) {
    const auto size = static_cast<Steinberg::int32>(blob.size());
    if (!streamer.writeInt32(size)) return false;
    return size == 0 || state->write(blob.data(), size, nullptr) == Steinberg::kResultOk;
}

/// Largest MIDI blob accepted (D-059): a uint32 count plus at most 128 CC mappings.
/// MidiCCManager writes 6 bytes per mapping; the bound allows 8 (fix plan T4.21).
constexpr Steinberg::int32 kMaxMidiBlobSize = 4 + 128 * 8;

/// Reads one size-prefixed blob; empty on a missing or short blob. A size above
/// kMaxMidiBlobSize (corrupt or hostile state) sets `rejected` and allocates nothing.
std::vector<uint8_t> readMidiBlob(Steinberg::IBStream* state, Steinberg::IBStreamer& streamer, bool& rejected) {
    Steinberg::int32 size = 0;
    if (!streamer.readInt32(size) || size <= 0) return {};
    if (size > kMaxMidiBlobSize) {
        rejected = true;
        return {};
    }
    std::vector<uint8_t> blob(static_cast<size_t>(size));
    Steinberg::int32 got = 0;
    if (state->read(blob.data(), size, &got) != Steinberg::kResultOk || got != size) return {};
    return blob;
}
}  // namespace

Steinberg::tresult PLUGIN_API Controller::getState(Steinberg::IBStream* state) {
    Steinberg::IBStreamer streamer(state, kLittleEndian);
    if (!streamer.writeInt32(kControllerStateVersion)) return Steinberg::kResultFalse;
    if (!streamer.writeDouble(0.0)) return Steinberg::kResultFalse;
    if (!streamer.writeDouble(0.0)) return Steinberg::kResultFalse;

    if (!writeMidiBlob(state, streamer, midiCCManager_ ? midiCCManager_->serializeGlobalMappings() : std::vector<uint8_t>{}))
        return Steinberg::kResultFalse;
    if (!writeMidiBlob(state, streamer, midiCCManager_ ? midiCCManager_->serializePresetMappings() : std::vector<uint8_t>{}))
        return Steinberg::kResultFalse;

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::setState(Steinberg::IBStream* state) {
    Steinberg::IBStreamer streamer(state, kLittleEndian);
    int32_t version = 0;
    if (!streamer.readInt32(version)) return Steinberg::kResultOk;

    if (version >= 2) {
        double unusedWidth = 0.0;
        double unusedHeight = 0.0;
        streamer.readDouble(unusedWidth);
        streamer.readDouble(unusedHeight);

        bool rejected = false;
        const auto globalBlob = readMidiBlob(state, streamer, rejected);
        const auto presetBlob =
            (version >= 3 && !rejected) ? readMidiBlob(state, streamer, rejected) : std::vector<uint8_t>{};
        if (rejected) return Steinberg::kResultOk;  // corrupt MIDI section: keep the current mappings
        if (midiCCManager_) {
            if (!globalBlob.empty())
                midiCCManager_->deserializeGlobalMappings(globalBlob.data(), globalBlob.size());
            // Per-preset mappings are replaced by the state's (none before version 3)
            if (presetBlob.empty() || !midiCCManager_->deserializePresetMappings(presetBlob.data(), presetBlob.size()))
                midiCCManager_->clearPresetMappings();
            notifyMidiMappingChanged();
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

    return Steinberg::kResultFalse;
}

// ==============================================================================
// IMidiLearn: the host reports live CCs here; an armed context-menu learn completes
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::onLiveMIDIControllerInput(
    Steinberg::int32 busIndex, Steinberg::int16 /*channel*/,
    Steinberg::Vst::CtrlNumber midiCC) {
    if (busIndex != 0 || midiCC < 0 || midiCC >= 128) return Steinberg::kResultFalse;
    if (!midiCCManager_ || !midiCCManager_->isLearning()) return Steinberg::kResultOk;

    // processCCMessage completes the learn (an LSB CC 32-63 keeps it armed); no
    // value is reported here, so no parameter callback.
    if (midiCCManager_->processCCMessage(static_cast<uint8_t>(midiCC), 0, nullptr)) {
        setParamNormalized(makeGlobalParamId(GlobalParamType::kGlobalMidiLearnActive), 0.0);
        notifyMidiMappingChanged();
    }
    return Steinberg::kResultOk;
}

void Controller::notifyMidiMappingChanged() {
    if (componentHandler) componentHandler->restartComponent(Steinberg::Vst::kMidiCCAssignmentChanged);
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
            const uint8_t node = extractNode(id);
            // The slot's meaning is the owning node's type, not the selected node's (D-073)
            auto* typeParam = getParameterObject(makeNodeParamId(band, node, NodeParamType::kNodeType));
            if (typeParam) {
                int typeIdx = static_cast<int>(typeParam->getNormalized() * 25.0 + 0.5);
                auto distType = static_cast<DistortionType>(std::clamp(typeIdx, 0, 25));
                int slot = paramByte - static_cast<uint8_t>(NodeParamType::kNodeShape0);
                std::array<float, kShapeSlotDescriptorCount> slots{};
                for (int k = 0; k < kShapeSlotDescriptorCount; ++k) {
                    slots[static_cast<size_t>(k)] = static_cast<float>(getParamNormalized(makeNodeParamId(
                        band, node,
                        static_cast<NodeParamType>(static_cast<uint8_t>(NodeParamType::kNodeShape0) + k))));
                }
                slots[static_cast<size_t>(slot)] = static_cast<float>(valueNormalized);
                if (formatShapeSlot(distType, slot, static_cast<float>(valueNormalized), slots.data(), string))
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
                    std::array<float, kShapeSlotDescriptorCount> slots{};
                    for (int k = 0; k < kShapeSlotDescriptorCount; ++k) {
                        slots[static_cast<size_t>(k)] = static_cast<float>(getParamNormalized(makeBandParamId(
                            band, static_cast<BandParamType>(shape0Byte + k))));
                    }
                    slots[static_cast<size_t>(slot)] = static_cast<float>(valueNormalized);
                    if (formatShapeSlot(distType, slot, static_cast<float>(valueNormalized), slots.data(), string))
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

Steinberg::tresult PLUGIN_API Controller::setParamNormalized(
    Steinberg::Vst::ParamID tag,
    Steinberg::Vst::ParamValue value) {
    if (tag != kLatencyOutputId) {
        return EditControllerEx1::setParamNormalized(tag, value);
    }
    // The processor wrote a new reported latency (D-026): tell the host on this thread
    const bool changed = getParamNormalized(tag) != value;
    const auto result = EditControllerEx1::setParamNormalized(tag, value);
    if (changed && componentHandler) {
        componentHandler->restartComponent(Steinberg::Vst::kLatencyChanged);
    }
    return result;
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
    const VSTGUI::IUIDescription* description,
    VSTGUI::VST3Editor* editor) {

    // Level meters: InputLevelMeter, OutputLevelMeter, Band1LevelMeter ... Band4LevelMeter
    {
        using Source = LevelMeterView::Source;
        static constexpr std::array<std::pair<std::string_view, Source>, 6> kMeters{{
            {"InputLevelMeter", Source::Input},
            {"OutputLevelMeter", Source::Output},
            {"Band1LevelMeter", Source::Band0},
            {"Band2LevelMeter", Source::Band1},
            {"Band3LevelMeter", Source::Band2},
            {"Band4LevelMeter", Source::Band3},
        }};
        for (const auto& [meterName, source] : kMeters) {
            if (meterName != name) continue;
            VSTGUI::CPoint origin(0, 0);
            VSTGUI::CPoint size(14, 36);
            attributes.getPointAttribute("origin", origin);
            attributes.getPointAttribute("size", size);
            const auto color = [description](const char* colorName) {
                VSTGUI::CColor c{128, 128, 128, 255};
                if (description != nullptr) description->getColor(colorName, c);
                return c;
            };
            static constexpr std::array<const char*, 4> kBandColors{"band-1", "band-2", "band-3", "band-4"};
            const auto bandIndex = static_cast<size_t>(
                source >= Source::Band0 ? static_cast<int>(source) - static_cast<int>(Source::Band0) : 0);
            const LevelMeterView::Colors colors{.track = color("slider-track"),
                                                .low = color("meter-low"),
                                                .mid = color("meter-mid"),
                                                .clip = color("meter-clip"),
                                                .band = color(kBandColors[bandIndex])};
            return new LevelMeterView(VSTGUI::CRect(origin, size), source, colors);
        }
    }

    if (std::strcmp(name, "SpectrumDisplay") == 0) {
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;
        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");
        if (originStr) {
            double x = 0.0;
            double y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2)
                origin = VSTGUI::CPoint(x, y);
        }
        if (sizeStr) {
            double w = 980.0;
            double h = 200.0;
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
            if (crossoverParam)
                spectrumDisplay->setCrossoverFrequency(
                    i, crossoverNormToHz(static_cast<float>(crossoverParam->getNormalized())));
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
            double x = 0.0;
            double y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2)
                origin = VSTGUI::CPoint(x, y);
        }
        if (sizeStr) {
            double w = 250.0;
            double h = 200.0;
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
        morphPad->bindMorphParams(morphXParamId, morphYParamId);

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
            double x = 0.0;
            double y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2)
                origin = VSTGUI::CPoint(x, y);
        }
        if (sizeStr) {
            double w = 140.0;
            double h = 22.0;
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

        const std::string* autoHideStr = attributes.getAttributeValue("auto-hide-single");
        if (autoHideStr && *autoHideStr == "true") {
            // Lives on a Shape page that the UIViewSwitchContainer frees on every
            // switch back to Main: never cached (D-061). Its destructor deactivates it.
            nodeSelector->setAutoHideForSingleNode(true);
            if (accessibilityPrefs_.highContrastEnabled) nodeSelector->setHighContrastMode(true);
        } else {
            dynamicNodeSelectors_[bandIndex] = nodeSelector;
            nodeSelector->registerViewListener(this);  // viewWillDelete clears the slot
        }

        return nodeSelector;
    }

    if (std::strcmp(name, "NodeEditorBorder") == 0) {
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;
        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");
        if (originStr) {
            double x = 0.0;
            double y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2)
                origin = VSTGUI::CPoint(x, y);
        }
        if (sizeStr) {
            double w = 280.0;
            double h = 230.0;
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

    if (std::strcmp(name, "SweepIndicator") == 0) {
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;
        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");
        if (originStr) {
            double x = 0.0;
            double y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2)
                origin = VSTGUI::CPoint(x, y);
        }
        if (sizeStr) {
            double w = 980.0;
            double h = 200.0;
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
            const float freqHz = sweepFreqNormToHz(static_cast<float>(sweepFreqParam->getNormalized()));
            const float widthOct = kSweepWidthOctRange.toPlain(static_cast<float>(sweepWidthParam->getNormalized()));
            const float intensity =
                kSweepIntensityRange.toPlain(static_cast<float>(sweepIntensityParam->getNormalized()));
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

    // The update banner, the manual check button and the version label: the shared
    // views every Krate editor carries.
    if (auto* shared = Krate::Plugins::createSharedEditorView(
            name, attributes, updateChecker_.get(), VERSION_STR, updateBannerView_)) {
        return shared;
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
    editorClosing_.store(false, std::memory_order_release);
    spectrumDataAvailable_ = false;
    activeEditor_ = editor;
    // Level meters start from this session's audio
    pendingMeters_ = MeterWindow{};
    lastMeterTick_ = {};
    // Tier 3: look the processor's display up again in every editor session; the
    // processor may have been replaced or re-registered since the last one (D-057/D-070)
    if (instanceId_ != 0)
        sharedDisplay_ = static_cast<SharedDisplay*>(
            Krate::Plugins::SharedDisplayBridge::instance().lookupInstance(instanceId_));

    auto* bandCountParam = getParameterObject(makeGlobalParamId(GlobalParamType::kGlobalBandCount));
    if (bandCountParam) {
        for (int b = 0; b < kMaxBands; ++b) {
            float threshold = (static_cast<float>(b) - 0.5f) / 3.0f;
            Steinberg::int32 containerTag = 9000 + b;
            bandVisibilityControllers_[b] = new ContainerVisibilityController(
                &activeEditor_, bandCountParam, containerTag, threshold, false);
        }
        bandCountDisplayController_ = new BandCountDisplayController(
            &spectrumDisplay_, bandCountParam, &keyboardHandler_);
    }

    {
        std::array<Steinberg::Vst::Parameter*, CrossoverParamObserver::kNumCrossovers> crossoverParams{};
        for (int i = 0; i < CrossoverParamObserver::kNumCrossovers; ++i)
            crossoverParams[static_cast<size_t>(i)] =
                getParameterObject(makeCrossoverParamId(static_cast<uint8_t>(i)));
        crossoverParamObserver_ = Steinberg::owned(new CrossoverParamObserver(&spectrumDisplay_, crossoverParams));
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
                290.0f, 250, parentBandTag, b, 690.0f, bandCountParam);
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

            // The preset overlays own the keyboard while open (D-068)
            keyboardHandler_->setOverlayOpenQuery([this]() {
                return (presetBrowserView_ && presetBrowserView_->isOpen())
                    || (savePresetDialogView_ && savePresetDialogView_->isOpen());
            });

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
    }

    for (int b = 0; b < kMaxBands; ++b) {
        nodeSelectionControllers_[b] = new NodeSelectionController(
            this, static_cast<uint8_t>(b), &shapeShadowStorage_[b]);
    }

    sweepVisualizationController_ = new SweepVisualizationController(
        this, &sweepIndicator_, &spectrumDisplay_);

    sweepVisualizationTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
        [this](VSTGUI::CVSTGUITimer* /*timer*/) { onEditorTimer(); }, 33);

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

void Controller::onEditorTimer() {
    if (sweepIndicator_ && sweepIndicator_->isEnabled())
        sweepIndicator_->setDirty();
    if (activeEditor_) {
        if (auto* frame = activeEditor_->getFrame())
            updateModSliders(frame);
    }
    // Tier 3 fallback: if DataExchange hasn't delivered data after ~330ms,
    // wire the spectrum display directly to the processor's shared FIFOs
    if (!dataExchangeActive_ && sharedDisplay_ && !spectrumDataAvailable_) {
        ++fallbackTickCounter_;
        if (fallbackTickCounter_ > 10) { // ~330ms at 33ms timer
            KRATE_BRIDGE_LOG("Disrumpo::Controller — Tier 3 fallback ACTIVATED (no DataExchange after ~330ms)");
            spectrumDataAvailable_ = true;
            if (spectrumDisplay_) {
                spectrumDisplay_->setSpectrumFIFOs(
                    sharedDisplay_->inputFIFO, sharedDisplay_->outputFIFO);
                float sr = sharedDisplay_->sampleRate->load(std::memory_order_acquire);
                if (sr > 0.0f)
                    spectrumDisplay_->startAnalysis(sr);
            }
        }
    }

    // Level meters: Tier 3 reads the processor's SharedMeters once the fallback is on
    if (!dataExchangeActive_ && fallbackTickCounter_ > 10 && sharedDisplay_ && sharedDisplay_->meters) {
        MeterWindow window;
        if (sharedDisplay_->meters->consume(window)) {
            mergeMeterWindow(pendingMeters_, window);
            pendingMeterSampleRate_ = sharedDisplay_->sampleRate->load(std::memory_order_acquire);
        }
    }
    const auto now = std::chrono::steady_clock::now();
    const double dt = lastMeterTick_ == std::chrono::steady_clock::time_point{}
        ? 0.0
        : std::clamp(std::chrono::duration<double>(now - lastMeterTick_).count(), 0.0, 0.25);
    lastMeterTick_ = now;
    if (activeEditor_) {
        if (auto* frame = activeEditor_->getFrame())
            updateLevelMeters(frame, dt);
    }
    pendingMeters_ = MeterWindow{};
}

void Controller::updateLevelMeters(VSTGUI::CViewContainer* container, double dtSeconds) {
    if (!container) return;
    container->forEachChild([this, dtSeconds](VSTGUI::CView* child) {
        if (auto* meter = dynamic_cast<LevelMeterView*>(child)) {
            if (pendingMeters_.numSamples > 0)
                meter->pushWindow(pendingMeters_, pendingMeterSampleRate_);
            meter->advance(dtSeconds);
        }
        if (auto* vc = dynamic_cast<VSTGUI::CViewContainer*>(child))
            updateLevelMeters(vc, dtSeconds);
    });
}

void Controller::willClose(VSTGUI::VST3Editor* editor) {
    // FIRST: set guard flag and kill all timers/async callbacks to prevent
    // races with DataExchange or timer callbacks during teardown
    editorClosing_.store(true, std::memory_order_release);
    if (sweepVisualizationTimer_) { sweepVisualizationTimer_->stop(); sweepVisualizationTimer_ = nullptr; }
    if (spectrumDisplay_) spectrumDisplay_->stopAnalysis();

    // Now safe to tear down visibility controllers and views
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
    for (auto& nsc : nodeSelectionControllers_) {
        if (nsc) {
            if (auto* c = dynamic_cast<NodeSelectionController*>(nsc.get())) c->deactivate();
            nsc = nullptr;
        }
    }
    for (auto& dns : dynamicNodeSelectors_) {
        if (dns) {
            dns->unregisterViewListener(this);
            dns->deactivate();
            dns = nullptr;
        }
    }
    for (auto& mp : morphPads_) { if (mp) { mp->deactivate(); mp = nullptr; } }
    if (sweepVisualizationController_) {
        if (auto* svc = dynamic_cast<SweepVisualizationController*>(sweepVisualizationController_.get())) svc->deactivate();
        sweepVisualizationController_ = nullptr;
    }
    if (bandCountDisplayController_) {
        if (auto* bcdc = dynamic_cast<BandCountDisplayController*>(bandCountDisplayController_.get())) bcdc->deactivate();
        bandCountDisplayController_ = nullptr;
    }
    if (crossoverParamObserver_) {
        if (auto* cpo = dynamic_cast<CrossoverParamObserver*>(crossoverParamObserver_.get())) cpo->deactivate();
        crossoverParamObserver_ = nullptr;
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
    sweepIndicator_ = nullptr;
    spectrumDisplay_ = nullptr;
    dataExchangeActive_ = false;
    fallbackTickCounter_ = 0;
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
        learnItem->setActions([this, paramId](VSTGUI::CCommandMenuItem*) { startMidiLearn(paramId); });
        menu->addEntry(learnItem);
    }

    uint8_t existingCC = 0;
    if (midiCCManager_->getCCForParam(paramId, existingCC)) {
        {
            VSTGUI::CCommandMenuItem::Desc clearDesc("Clear MIDI Learn");
            auto* clearItem = new VSTGUI::CCommandMenuItem(std::move(clearDesc));
            clearItem->setActions([this, paramId](VSTGUI::CCommandMenuItem*) { clearMidiLearn(paramId); });
            menu->addEntry(clearItem);
        }
        Krate::Plugins::MidiCCMapping mapping;
        if (midiCCManager_->getMapping(existingCC, mapping)) {
            VSTGUI::CCommandMenuItem::Desc presetDesc("Save Mapping with Preset");
            auto* presetItem = new VSTGUI::CCommandMenuItem(std::move(presetDesc));
            presetItem->setActions([this, paramId](VSTGUI::CCommandMenuItem*) { toggleMidiMappingScope(paramId); });
            menu->addEntry(presetItem);
        }
    }

    return menu;
}

void Controller::startMidiLearn(Steinberg::Vst::ParamID paramId) {
    if (!midiCCManager_) return;
    midiCCManager_->startLearn(paramId);
    setParamNormalized(makeGlobalParamId(GlobalParamType::kGlobalMidiLearnActive), 1.0);
}

void Controller::clearMidiLearn(Steinberg::Vst::ParamID paramId) {
    if (!midiCCManager_) return;
    midiCCManager_->removeMappingsForParam(paramId);
    notifyMidiMappingChanged();
}

void Controller::toggleMidiMappingScope(Steinberg::Vst::ParamID paramId) {
    if (!midiCCManager_) return;
    uint8_t cc = 0;
    Krate::Plugins::MidiCCMapping mapping;
    if (!midiCCManager_->getCCForParam(paramId, cc) || !midiCCManager_->getMapping(cc, mapping)) return;
    if (!mapping.isPerPreset) {
        midiCCManager_->removeGlobalMapping(cc);
        midiCCManager_->addPresetMapping(cc, paramId, mapping.is14Bit);
    } else {
        midiCCManager_->removePresetMapping(cc);
        midiCCManager_->addGlobalMapping(cc, paramId, mapping.is14Bit);
    }
    notifyMidiMappingChanged();
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

std::vector<const VSTGUI::CView*> Controller::cachedNodeSelectorsForTest() const {
    std::vector<const VSTGUI::CView*> views;
    for (const auto* dns : dynamicNodeSelectors_) { if (dns) views.push_back(dns); }
    return views;
}

void Controller::viewWillDelete(VSTGUI::CView* view) {
    for (auto& dns : dynamicNodeSelectors_) {
        if (dns == view) dns = nullptr;
    }
    view->unregisterViewListener(this);
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
    auto getInt8FromList = [this](Steinberg::Vst::ParamID id, int count) -> Steinberg::int8 {
        if (auto* param = getParameterObject(id))
            return static_cast<Steinberg::int8>(normToListIndex(param->getNormalized(), count));
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

    streamer.writeInt32(getInt8FromList(makeGlobalParamId(GlobalParamType::kGlobalBandCount), kMaxBands) + 1);

    for (int b = 0; b < kMaxBands; ++b) {
        auto band = static_cast<uint8_t>(b);
        streamer.writeFloat(getFloat(makeBandParamId(band, BandParamType::kBandGain), 0.0f));
        streamer.writeFloat(getFloat(makeBandParamId(band, BandParamType::kBandPan), 0.0f));
        streamer.writeInt8(getBoolInt8(makeBandParamId(band, BandParamType::kBandSolo)));
        streamer.writeInt8(getBoolInt8(makeBandParamId(band, BandParamType::kBandBypass)));
        streamer.writeInt8(getBoolInt8(makeBandParamId(band, BandParamType::kBandMute)));
    }

    for (int c = 0; c < kMaxBands - 1; ++c)
        streamer.writeFloat(crossoverNormToHz(getParamNorm(makeCrossoverParamId(static_cast<uint8_t>(c)))));

    // Sweep
    streamer.writeInt8(getBoolInt8(makeSweepParamId(SweepParamType::kSweepEnable)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepFrequency)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepWidth)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepIntensity)));
    streamer.writeInt8(getBoolInt8(makeSweepParamId(SweepParamType::kSweepFalloff)));
    streamer.writeInt8(getInt8FromList(makeSweepParamId(SweepParamType::kSweepMorphLink), kMorphLinkModeCount));
    streamer.writeInt8(getBoolInt8(makeSweepParamId(SweepParamType::kSweepLFOEnable)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepLFORate)));
    streamer.writeInt8(getInt8FromList(makeSweepParamId(SweepParamType::kSweepLFOWaveform), kLfoShapeCount));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepLFODepth)));
    streamer.writeInt8(getBoolInt8(makeSweepParamId(SweepParamType::kSweepLFOSync)));
    streamer.writeInt8(getInt8FromList(makeSweepParamId(SweepParamType::kSweepLFONoteValue), kNoteValueCount));
    streamer.writeInt8(getBoolInt8(makeSweepParamId(SweepParamType::kSweepEnvEnable)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepEnvAttack)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepEnvRelease)));
    streamer.writeFloat(getParamNorm(makeSweepParamId(SweepParamType::kSweepEnvSensitivity)));
    {
        // Custom curve: the breakpoints parseComponentState mirrored into the curve params
        const int pointCount =
            kMinBreakpoints + getInt8FromList(makeSweepParamId(SweepParamType::kSweepCustomCurvePointCount),
                                              kMaxBreakpoints - kMinBreakpoints + 1);
        streamer.writeInt32(pointCount);
        for (int i = 0; i < pointCount; ++i) {
            streamer.writeFloat(getParamNorm(customCurvePointParamId(i, false)));
            streamer.writeFloat(getParamNorm(customCurvePointParamId(i, true)));
        }
    }

    // Modulation
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kLFO1Rate)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kLFO1Shape), kLfoShapeCount));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kLFO1Phase)));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO1Sync)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kLFO1NoteValue), kNoteValueCount));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO1Unipolar)));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO1Retrigger)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kLFO2Rate)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kLFO2Shape), kLfoShapeCount));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kLFO2Phase)));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO2Sync)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kLFO2NoteValue), kNoteValueCount));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO2Unipolar)));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kLFO2Retrigger)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kEnvFollowerAttack)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kEnvFollowerRelease)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kEnvFollowerSensitivity)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kEnvFollowerSource), kEnvFollowerSourceCount));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kRandomRate)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kRandomSmoothness)));
    streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kRandomSync)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kChaosModel), kChaosModelCount));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kChaosSpeed)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kChaosCoupling)));
    streamer.writeInt8(getInt8FromList(makeModParamId(ModParamType::kSampleHoldSource), kSampleHoldSourceCount));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kSampleHoldRate)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kSampleHoldSlew)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kPitchFollowerMinHz)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kPitchFollowerMaxHz)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kPitchFollowerConfidence)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kPitchFollowerTrackingSpeed)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kTransientSensitivity)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kTransientAttack)));
    streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kTransientDecay)));

    // Rungler (v11+: rate[float], depth[float], bits[int8], loop[int8])
    {
        float rateNorm = getParamNorm(makeModParamId(ModParamType::kRunglerRate));
        streamer.writeFloat(rateNorm);
        streamer.writeFloat(getParamNorm(makeModParamId(ModParamType::kRunglerDepth)));
        // Bits: parameter has stepCount=12 (values 4-16), convert normalized to raw int
        auto* bitsParam = getParameterObject(makeModParamId(ModParamType::kRunglerBits));
        Steinberg::int8 bits = bitsParam ? static_cast<Steinberg::int8>(
            4 + normToListIndex(bitsParam->getNormalized(), kRunglerBitsCount)) : 8;
        streamer.writeInt8(bits);
        streamer.writeInt8(getBoolInt8(makeModParamId(ModParamType::kRunglerLoop)));
    }

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
        streamer.writeInt8(getInt8FromList(makeModParamId(macro[3]), Krate::DSP::kModCurveCount));
    }

    for (uint8_t r = 0; r < 32; ++r) {
        streamer.writeInt8(getInt8FromList(makeRoutingParamId(r, 0), kUIModSourceCount));
        streamer.writeInt32(normToListIndex(getParamNorm(makeRoutingParamId(r, 1)),
                                            static_cast<int>(ModDest::kTotalDestinations)));
        float amountNorm = getParamNorm(makeRoutingParamId(r, 2));
        streamer.writeFloat(amountNorm * 2.0f - 1.0f);
        streamer.writeInt8(getInt8FromList(makeRoutingParamId(r, 3), Krate::DSP::kModCurveCount));
    }

    // Morph node state
    for (int b = 0; b < kMaxBands; ++b) {
        auto band = static_cast<uint8_t>(b);
        streamer.writeFloat(getParamNorm(makeBandParamId(band, BandParamType::kBandMorphX)));
        streamer.writeFloat(getParamNorm(makeBandParamId(band, BandParamType::kBandMorphY)));
        streamer.writeInt8(getInt8FromList(makeBandParamId(band, BandParamType::kBandMorphMode), kMorphModeCount));
        streamer.writeInt8(static_cast<Steinberg::int8>(
            getInt8FromList(makeBandParamId(band, BandParamType::kBandActiveNodes), kMaxMorphNodes) + 1));
        float smoothingNorm = getParamNorm(makeBandParamId(band, BandParamType::kBandMorphSmoothing));
        streamer.writeFloat(kMorphSmoothingMsRange.toPlain(smoothingNorm));
        streamer.writeInt8(getInt8FromList(makeBandParamId(band, BandParamType::kBandMorphXLink), kMorphLinkModeCount));
        streamer.writeInt8(getInt8FromList(makeBandParamId(band, BandParamType::kBandMorphYLink), kMorphLinkModeCount));

        for (int n = 0; n < kMaxMorphNodes; ++n) {
            auto node = static_cast<uint8_t>(n);
            streamer.writeInt8(getInt8FromList(makeNodeParamId(band, node, NodeParamType::kNodeType), kDistortionTypeCount));
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
    dispatchOnBackgroundThread = static_cast<Steinberg::TBool>(false);
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
    if (editorClosing_.load(std::memory_order_acquire))
        return;

    for (Steinberg::uint32 i = 0; i < numBlocks; ++i) {
        if (blocks[i].data && blocks[i].size >= sizeof(SpectrumBlock)) {
            const auto* specBlock = static_cast<const SpectrumBlock*>(blocks[i].data);
            if (specBlock->numSamples > 0 && specBlock->numSamples <= kSpectrumBlockMaxSamples) {
                localInputFIFO_.push(specBlock->inputSamples, specBlock->numSamples);
                localOutputFIFO_.push(specBlock->outputSamples, specBlock->numSamples);
                cachedSpectrumSampleRate_ = specBlock->sampleRate;
                std::copy_n(specBlock->modOffsets, kSpectrumBlockModOffsets, cachedModOffsets_.begin());
                mergeMeterWindow(pendingMeters_, specBlock->meters);
                pendingMeterSampleRate_ = specBlock->sampleRate;
                if (!dataExchangeActive_)
                    KRATE_BRIDGE_LOG("Disrumpo::Controller — DataExchange data received (Tier 1/2 active)");
                dataExchangeActive_ = true;
                fallbackTickCounter_ = 0;
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
    if (editorClosing_.load(std::memory_order_acquire))
        return Steinberg::Vst::EditControllerEx1::notify(message);
    if (dataExchangeReceiver_.onMessage(message)) return Steinberg::kResultOk;

    return Steinberg::Vst::EditControllerEx1::notify(message);
}

void Controller::updateModSliders(VSTGUI::CViewContainer* container) {
    if (!container) return;
    container->forEachChild([this](VSTGUI::CView* child) {
        if (auto* ms = dynamic_cast<ModSlider*>(child)) {
            if (ms->getModDestId() < cachedModOffsets_.size())
                ms->setModulationOffset(cachedModOffsets_[ms->getModDestId()]);
        }
        if (auto* vc = dynamic_cast<VSTGUI::CViewContainer*>(child))
            updateModSliders(vc);
    });
}

} // namespace Disrumpo
