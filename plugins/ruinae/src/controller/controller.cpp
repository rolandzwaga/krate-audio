// ==============================================================================
// Edit Controller Implementation
// ==============================================================================
// Core controller methods: initialization, lifecycle, state management,
// editor open/close, IMessage handling, and action button handling.
//
// Additional Controller methods are split across:
//   controller_view_sync.cpp     - setParamNormalized, syncAllViews
//   controller_verify_view.cpp   - verifyView (UIDescription view wiring)
//   controller_param_display.cpp - getParamStringByValue/getParamValueByString
//   controller_presets.cpp       - preset browser, custom views, state loading
//   controller_adsr.cpp          - ADSR display wiring/sync
//   controller_mod_matrix.cpp    - mod matrix grid & ring indicator wiring
//   controller_arp.cpp           - arp skip events, copy/paste
//   controller_settings.cpp      - settings drawer animation, tab changed
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "version.h"
#include "preset/ruinae_preset_config.h"
#include "ui/step_pattern_editor.h"
#include "ui/arp_lane_editor.h"
#include "ui/arp_lane_container.h"
#include "ui/arp_modifier_lane.h"
#include "ui/arp_condition_lane.h"
#include "ui/arp_chord_lane.h"
#include "ui/arp_inversion_lane.h"
#include "ui/xy_morph_pad.h"
#include "adsr_expanded_overlay.h"
#include "ui/adsr_display.h"
#include "ui/mod_matrix_grid.h"
#include "ui/mod_ring_indicator.h"
#include "ui/mod_heatmap.h"
#include "ui/euclidean_dot_display.h"
#include "ui/lfo_waveform_display.h"
#include "ui/chaos_mod_display.h"
#include "ui/rungler_display.h"
#include "ui/sample_hold_display.h"
#include "ui/random_mod_display.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include <krate/dsp/core/note_value.h>
#include "ui/category_tab_bar.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"
#include "ui/update_banner_view.h"
#include "update/ruinae_update_config.h"

// Parameter pack headers (for registration and controller sync)
#include "parameters/global_params.h"
#include "parameters/osc_a_params.h"
#include "parameters/osc_b_params.h"
#include "parameters/mixer_params.h"
#include "parameters/filter_params.h"
#include "parameters/distortion_params.h"
#include "parameters/trance_gate_params.h"
#include "parameters/amp_env_params.h"
#include "parameters/filter_env_params.h"
#include "parameters/mod_env_params.h"
#include "parameters/lfo1_params.h"
#include "parameters/lfo2_params.h"
#include "parameters/chaos_mod_params.h"
#include "parameters/mod_matrix_params.h"
#include "parameters/global_filter_params.h"
#include "parameters/delay_params.h"
#include "parameters/fx_enable_params.h"
#include "parameters/reverb_params.h"
#include "parameters/phaser_params.h"
#include "parameters/flanger_params.h"
#include "parameters/chorus_params.h"
#include "parameters/harmonizer_params.h"
#include "parameters/mono_mode_params.h"
#include "parameters/macro_params.h"
#include "parameters/rungler_params.h"
#include "parameters/settings_params.h"
#include "parameters/env_follower_params.h"
#include "parameters/sample_hold_params.h"
#include "parameters/random_params.h"
#include "parameters/pitch_follower_params.h"
#include "parameters/transient_params.h"
#include "parameters/arpeggiator_params.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/events.h"

#include <cmath>
#include <cstring>
#include <string>

namespace {

// Custom editor that intercepts right-clicks over StepPatternEditor.
// VST3Editor::onMouseEvent consumes ALL right-clicks for context menus
// before views see them, so we must handle it at this level.
class RuinaeEditor : public VSTGUI::VST3Editor {
public:
    RuinaeEditor(Steinberg::Vst::EditController* controller,
                 VSTGUI::UTF8StringPtr templateName,
                 VSTGUI::UTF8StringPtr xmlFile)
        : VST3Editor(controller, templateName, xmlFile) {}

    void onMouseEvent(VSTGUI::MouseEvent& event, VSTGUI::CFrame* frame) override {
        if (event.type == VSTGUI::EventType::MouseDown && event.buttonState.isRight()) {
            auto nonScaledPos = event.mousePosition;
            frame->getTransform().transform(nonScaledPos);
            VSTGUI::CViewContainer::ViewList views;
            if (frame->getViewsAt(nonScaledPos, views,
                    VSTGUI::GetViewOptions().deep())) {
                for (const auto& view : views) {
                    if (auto* spe = dynamic_cast<Krate::Plugins::StepPatternEditor*>(view.get())) {
                        auto localPos = spe->translateToLocal(nonScaledPos);
                        spe->handleRightClick(localPos);
                        event.consumed = true;
                        return;
                    }
                }
            }
        }
        VST3Editor::onMouseEvent(event, frame);
    }
};

} // anonymous namespace

namespace Ruinae {

// State version must match processor
constexpr Steinberg::int32 kControllerStateVersion = 1;

// ==============================================================================
// Destructor
// ==============================================================================

Controller::~Controller() = default;

// ==============================================================================
// IPluginBase
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    Steinberg::tresult result = EditControllerEx1::initialize(context);
    if (result != Steinberg::kResultTrue) {
        return result;
    }

    // ==========================================================================
    // Register All Parameters (19 sections)
    // ==========================================================================

    registerGlobalParams(parameters);
    registerOscAParams(parameters);
    registerOscBParams(parameters);
    registerMixerParams(parameters);
    registerFilterParams(parameters);
    registerDistortionParams(parameters);
    registerTranceGateParams(parameters);
    registerAmpEnvParams(parameters);
    registerFilterEnvParams(parameters);
    registerModEnvParams(parameters);
    registerLFO1Params(parameters);
    registerLFO2Params(parameters);
    registerChaosModParams(parameters);

    // UI-only: Mod source view mode dropdown (10 entries), ephemeral, not persisted
    {
        auto* modViewParam = new Steinberg::Vst::StringListParameter(
            STR16("Mod Source View"), kModSourceViewModeTag);
        modViewParam->appendString(STR16("LFO 1"));
        modViewParam->appendString(STR16("LFO 2"));
        modViewParam->appendString(STR16("Chaos"));
        modViewParam->appendString(STR16("Macros"));
        modViewParam->appendString(STR16("Rungler"));
        modViewParam->appendString(STR16("Env Follower"));
        modViewParam->appendString(STR16("Sample & Hold"));
        modViewParam->appendString(STR16("Random"));
        modViewParam->appendString(STR16("Pitch Follower"));
        modViewParam->appendString(STR16("Transient"));
        parameters.addParameter(modViewParam);
    }

    // UI-only: Main tab selector (4 entries), ephemeral, not persisted
    {
        auto* tabParam = new Steinberg::Vst::StringListParameter(
            STR16("Main Tab"), kMainTabTag);
        tabParam->appendString(STR16("SOUND"));
        tabParam->appendString(STR16("MOD"));
        tabParam->appendString(STR16("FX"));
        tabParam->appendString(STR16("SEQ"));
        parameters.addParameter(tabParam);
    }

    registerModMatrixParams(parameters);
    registerGlobalFilterParams(parameters);
    registerFxEnableParams(parameters);
    registerDelayParams(parameters);
    registerReverbParams(parameters);
    registerPhaserParams(parameters);
    registerFlangerParams(parameters);
    registerChorusParams(parameters);
    registerHarmonizerParams(parameters);
    registerMonoModeParams(parameters);
    registerMacroParams(parameters);
    registerRunglerParams(parameters);
    registerSettingsParams(parameters);
    registerEnvFollowerParams(parameters);
    registerSampleHoldParams(parameters);
    registerRandomParams(parameters);
    registerPitchFollowerParams(parameters);
    registerTransientParams(parameters);
    registerArpParams(parameters);

    // Sidechain active status (hidden, read-only output parameter)
    parameters.addParameter(STR16("Sidechain Active"), STR16(""), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kIsHidden | Steinberg::Vst::ParameterInfo::kIsReadOnly,
        kSidechainActiveId);

    // ==========================================================================
    // Initialize Preset Manager
    // ==========================================================================
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(
        makeRuinaePresetConfig(), nullptr, this);

    // Wire state provider callback for preset saving (Spec 083, FR-003)
    presetManager_->setStateProvider([this]() -> Steinberg::IBStream* {
        return this->createComponentStateStream();
    });

    // Wire load provider callback for preset loading (Spec 083, FR-003)
    // Arp-only presets (subcategory starting with "Arp") load only arpeggiator params
    presetManager_->setLoadProvider(
        [this](Steinberg::IBStream* state,
               const Krate::Plugins::PresetInfo& info) -> bool {
            bool arpOnly = info.subcategory.starts_with("Arp");
            return this->loadComponentStateWithNotify(state, arpOnly);
        });

    // Update checker
    updateChecker_ = std::make_unique<Krate::Plugins::UpdateChecker>(makeRuinaeUpdateConfig());

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::terminate() {
    modulatedMorphXPtr_ = nullptr;
    modulatedMorphYPtr_ = nullptr;
    playbackPollTimer_ = nullptr;
    trailTimer_ = nullptr;
    tranceGatePlaybackStepPtr_ = nullptr;
    isTransportPlayingPtr_ = nullptr;
    ampEnvOutputPtr_ = nullptr;
    ampEnvStagePtr_ = nullptr;
    filterEnvOutputPtr_ = nullptr;
    filterEnvStagePtr_ = nullptr;
    modEnvOutputPtr_ = nullptr;
    modEnvStagePtr_ = nullptr;
    envVoiceActivePtr_ = nullptr;
    updateChecker_.reset();
    presetManager_.reset();
    return EditControllerEx1::terminate();
}

// ==============================================================================
// IEditController
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::setComponentState(
    Steinberg::IBStream* state) {

    if (!state)
        return Steinberg::kResultFalse;

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version))
        return Steinberg::kResultTrue; // Empty stream, keep defaults

    if (version < 1 || version > Ruinae::kCurrentStateVersion)
        return Steinberg::kResultTrue; // Unknown version, keep defaults

    bulkParamLoad_ = true;
    FrameInvalidationGuard frameGuard(activeEditor_);

    SetParamFunc setParam = [this](Steinberg::Vst::ParamID id, double value) {
        setParamNormalized(id, value);
    };

    loadStateCore(streamer, version, setParam, /*arpOnly=*/false);

    bulkParamLoad_ = false;
    syncAllViews();

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::getState(Steinberg::IBStream* /*state*/) {
    // Controller-specific state (UI settings, etc.)
    // Currently no controller-only state to save
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::setState(Steinberg::IBStream* /*state*/) {
    // Controller-specific state restore
    return Steinberg::kResultTrue;
}

Steinberg::IPlugView* PLUGIN_API Controller::createView(Steinberg::FIDString name) {
    if (Steinberg::FIDStringsEqual(name, Steinberg::Vst::ViewType::kEditor)) {
        auto* editor = new RuinaeEditor(this, "editor", "editor.uidesc");
        return editor;
    }
    return nullptr;
}

// ==============================================================================
// IMessage: Receive processor messages
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::notify(Steinberg::Vst::IMessage* message) {
    if (!message)
        return Steinberg::kInvalidArgument;

    if (strcmp(message->getMessageID(), "TranceGatePlayback") == 0) {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        Steinberg::int64 stepPtr = 0;
        Steinberg::int64 playingPtr = 0;

        if (attrs->getInt("stepPtr", stepPtr) == Steinberg::kResultOk) {
            tranceGatePlaybackStepPtr_ = reinterpret_cast<std::atomic<int>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(stepPtr));
        }
        if (attrs->getInt("playingPtr", playingPtr) == Steinberg::kResultOk) {
            isTransportPlayingPtr_ = reinterpret_cast<std::atomic<bool>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(playingPtr));
        }

        return Steinberg::kResultOk;
    }

    if (strcmp(message->getMessageID(), "EnvelopeDisplayState") == 0) {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        Steinberg::int64 val = 0;

        if (attrs->getInt("ampOutputPtr", val) == Steinberg::kResultOk) {
            ampEnvOutputPtr_ = reinterpret_cast<std::atomic<float>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(val));
        }
        if (attrs->getInt("ampStagePtr", val) == Steinberg::kResultOk) {
            ampEnvStagePtr_ = reinterpret_cast<std::atomic<int>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(val));
        }
        if (attrs->getInt("filterOutputPtr", val) == Steinberg::kResultOk) {
            filterEnvOutputPtr_ = reinterpret_cast<std::atomic<float>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(val));
        }
        if (attrs->getInt("filterStagePtr", val) == Steinberg::kResultOk) {
            filterEnvStagePtr_ = reinterpret_cast<std::atomic<int>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(val));
        }
        if (attrs->getInt("modOutputPtr", val) == Steinberg::kResultOk) {
            modEnvOutputPtr_ = reinterpret_cast<std::atomic<float>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(val));
        }
        if (attrs->getInt("modStagePtr", val) == Steinberg::kResultOk) {
            modEnvStagePtr_ = reinterpret_cast<std::atomic<int>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(val));
        }
        if (attrs->getInt("voiceActivePtr", val) == Steinberg::kResultOk) {
            envVoiceActivePtr_ = reinterpret_cast<std::atomic<bool>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(val));
        }

        // Wire the atomic pointers to existing ADSRDisplay instances
        wireEnvDisplayPlayback();

        return Steinberg::kResultOk;
    }

    if (strcmp(message->getMessageID(), "MorphPadModulation") == 0) {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        Steinberg::int64 val = 0;
        if (attrs->getInt("morphXPtr", val) == Steinberg::kResultOk) {
            modulatedMorphXPtr_ = reinterpret_cast<std::atomic<float>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(val));
        }
        if (attrs->getInt("morphYPtr", val) == Steinberg::kResultOk) {
            modulatedMorphYPtr_ = reinterpret_cast<std::atomic<float>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(val));
        }

        return Steinberg::kResultOk;
    }

    // 081-interaction-polish: Arp skip event from processor (FR-007, FR-008)
    if (strcmp(message->getMessageID(), "ArpSkipEvent") == 0) {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        Steinberg::int64 lane = 0;
        Steinberg::int64 step = 0;
        if (attrs->getInt("lane", lane) == Steinberg::kResultOk &&
            attrs->getInt("step", step) == Steinberg::kResultOk) {
            if (lane >= 0 && lane < kArpLaneCount && step >= 0 && step < 32) {
                handleArpSkipEvent(static_cast<int>(lane), static_cast<int>(step));
            }
        }
        return Steinberg::kResultOk;
    }

    if (strcmp(message->getMessageID(), "VoiceModRouteState") == 0) {
        auto* attrs = message->getAttributes();
        if (!attrs)
            return Steinberg::kResultFalse;

        // Decode binary route data (T087)
        const void* data = nullptr;
        Steinberg::uint32 dataSize = 0;
        if (attrs->getBinary("routeData", data, dataSize) == Steinberg::kResultOk &&
            data && dataSize >= 224) {

            static constexpr size_t kBytesPerRoute = 14;
            const auto* bytes = static_cast<const uint8_t*>(data);

            for (int i = 0; i < Krate::Plugins::kMaxVoiceRoutes; ++i) {
                const auto* ptr = &bytes[static_cast<size_t>(i) * kBytesPerRoute];

                Krate::Plugins::ModRoute route;
                route.source = ptr[0];
                route.destination = static_cast<Krate::Plugins::ModDestination>(ptr[1]);
                std::memcpy(&route.amount, &ptr[2], sizeof(float));
                route.curve = ptr[6];
                std::memcpy(&route.smoothMs, &ptr[7], sizeof(float));
                route.scale = ptr[11];
                route.bypass = (ptr[12] != 0);
                route.active = (ptr[13] != 0);

                cachedVoiceRoutes_[static_cast<size_t>(i)] = route;
                if (modMatrixGrid_ && !suppressVoiceRouteSync_) {
                    modMatrixGrid_->setVoiceRoute(i, route);
                }
            }
        }

        return Steinberg::kResultOk;
    }

    return EditControllerEx1::notify(message);
}

// ==============================================================================
// VST3EditorDelegate
// ==============================================================================

void Controller::didOpen(VSTGUI::VST3Editor* editor) {
    activeEditor_ = editor;

    // Create a unified UI poll timer (~30fps) for all processor→UI feedback.
    if (!playbackPollTimer_) {
        playbackPollTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) {
                // Trance gate step indicator
                if (stepPatternEditor_) {
                    if (tranceGatePlaybackStepPtr_) {
                        stepPatternEditor_->setPlaybackStep(
                            tranceGatePlaybackStepPtr_->load(std::memory_order_relaxed));
                    }
                    if (isTransportPlayingPtr_) {
                        stepPatternEditor_->setPlaying(
                            isTransportPlayingPtr_->load(std::memory_order_relaxed));
                    }
                }
                // Morph pad modulation animation
                if (xyMorphPad_ && modulatedMorphXPtr_ && modulatedMorphYPtr_
                    && !xyMorphPad_->isDragging()) {
                    const float modX = modulatedMorphXPtr_->load(std::memory_order_relaxed);
                    const float modY = modulatedMorphYPtr_->load(std::memory_order_relaxed);
                    xyMorphPad_->setMorphPosition(modX, modY);
                }

                // 081-interaction-polish US1: Detect transport stop and clear trails
                if (isTransportPlayingPtr_) {
                    bool playing = isTransportPlayingPtr_->load(std::memory_order_relaxed);
                    if (wasTransportPlaying_ && !playing) {
                        for (auto& ts : laneTrailStates_) ts.clear();
                        lastPolledSteps_.fill(-1);
                        if (velocityLane_) velocityLane_->clearOverlays();
                        if (gateLane_) gateLane_->clearOverlays();
                        if (pitchLane_) pitchLane_->clearOverlays();
                        if (ratchetLane_) ratchetLane_->clearOverlays();
                        if (modifierLane_) modifierLane_->clearOverlays();
                        if (conditionLane_) conditionLane_->clearOverlays();
                        if (chordLane_) chordLane_->clearOverlays();
                        if (inversionLane_) inversionLane_->clearOverlays();
                    }
                    wasTransportPlaying_ = playing;
                }

                // Poll arp lane playhead parameters
                auto pollLanePlayhead = [&](Krate::Plugins::IArpLane* lane,
                                            Steinberg::Vst::ParamID playheadParamId,
                                            int laneIdx) {
                    if (!lane) return;
                    auto* param = getParameterObject(playheadParamId);
                    if (!param) return;

                    constexpr long kMaxArpSteps = 32;
                    double normalized = param->getNormalized();
                    long rawStep = std::lround(normalized * kMaxArpSteps);
                    int32_t step = rawStep >= kMaxArpSteps ? -1 : static_cast<int32_t>(rawStep);

                    if (step != lastPolledSteps_[static_cast<size_t>(laneIdx)]) {
                        lastPolledSteps_[static_cast<size_t>(laneIdx)] = step;
                        if (step >= 0) {
                            laneTrailStates_[static_cast<size_t>(laneIdx)].advance(step);
                        }
                    }

                    laneTrailStates_[static_cast<size_t>(laneIdx)].clearPassedSkips();

                    auto& ts = laneTrailStates_[static_cast<size_t>(laneIdx)];
                    lane->setTrailSteps(ts.steps, Krate::Plugins::PlayheadTrailState::kTrailAlphas);
                    lane->setPlayheadStep(step);
                };

                bool transportPlaying = isTransportPlayingPtr_ &&
                    isTransportPlayingPtr_->load(std::memory_order_relaxed);
                if (transportPlaying) {
                    pollLanePlayhead(velocityLane_, kArpVelocityPlayheadId, 0);
                    pollLanePlayhead(gateLane_, kArpGatePlayheadId, 1);
                    pollLanePlayhead(pitchLane_, kArpPitchPlayheadId, 2);
                    pollLanePlayhead(ratchetLane_, kArpRatchetPlayheadId, 3);
                    pollLanePlayhead(modifierLane_, kArpModifierPlayheadId, 4);
                    pollLanePlayhead(conditionLane_, kArpConditionPlayheadId, 5);
                    pollLanePlayhead(chordLane_, kArpChordPlayheadId, 6);
                    pollLanePlayhead(inversionLane_, kArpInversionPlayheadId, 7);
                }
            }, 33); // ~30fps

        trailTimer_ = playbackPollTimer_;
    }

    // Reset trail states on editor open
    for (auto& ts : laneTrailStates_) ts.clear();
    lastPolledSteps_.fill(-1);

    // Notify processor that editor is open (FR-012)
    {
        auto msg = Steinberg::owned(allocateMessage());
        if (msg) {
            msg->setMessageID("EditorState");
            auto* attrs = msg->getAttributes();
            if (attrs) {
                attrs->setInt("open", 1);
                sendMessage(msg);
            }
        }
    }

    // Create preset browser and save dialog overlay views
    if (presetManager_) {
        auto* frame = editor->getFrame();
        if (frame) {
            auto frameSize = frame->getViewSize();
            presetBrowserView_ = new Krate::Plugins::PresetBrowserView(
                frameSize, presetManager_.get(), getRuinaeTabLabels());
            frame->addView(presetBrowserView_);

            arpPresetBrowserView_ = new Krate::Plugins::PresetBrowserView(
                frameSize, presetManager_.get(), getRuinaeArpTabLabels());
            frame->addView(arpPresetBrowserView_);

            savePresetDialogView_ = new Krate::Plugins::SavePresetDialogView(
                frameSize, presetManager_.get(),
                {"Pads", "Leads", "Bass", "Textures", "Rhythmic", "Experimental"});
            frame->addView(savePresetDialogView_);
        }
    }

    // Create ADSR expanded overlay (single shared overlay for all 3 envelopes)
    {
        auto* frame = editor->getFrame();
        if (frame && !adsrExpandedOverlay_) {
            auto frameSize = frame->getViewSize();
            adsrExpandedOverlay_ = new ADSRExpandedOverlayView(frameSize);

            // Wire the expanded display's callbacks
            auto* expandedDisplay = adsrExpandedOverlay_->getDisplay();
            if (expandedDisplay) {
                expandedDisplay->setParameterCallback(
                    [this](uint32_t paramId, float normalizedValue) {
                        setParamNormalized(paramId, static_cast<double>(normalizedValue));
                        performEdit(paramId, static_cast<double>(normalizedValue));
                    });
                expandedDisplay->setBeginEditCallback(
                    [this](uint32_t paramId) { beginEdit(paramId); });
                expandedDisplay->setEndEditCallback(
                    [this](uint32_t paramId) { endEdit(paramId); });
            }

            adsrExpandedOverlay_->setCloseCallback(
                [this]() { closeAdsrExpandedOverlay(); });

            frame->addView(adsrExpandedOverlay_);
        }
    }

    // Start update check and banner polling
    if (updateChecker_) {
        updateChecker_->checkForUpdate(false);
    }
    if (updateBannerView_) {
        updateBannerView_->startPolling();
    }
}

void Controller::willClose(VSTGUI::VST3Editor* editor) {
    if (activeEditor_ == editor) {
        stepPatternEditor_ = nullptr;
        arpLaneContainer_ = nullptr;
        velocityLane_ = nullptr;
        gateLane_ = nullptr;
        pitchLane_ = nullptr;
        ratchetLane_ = nullptr;
        modifierLane_ = nullptr;
        conditionLane_ = nullptr;
        chordLane_ = nullptr;
        inversionLane_ = nullptr;
        presetDropdown_ = nullptr;
        xyMorphPad_ = nullptr;
        modMatrixGrid_ = nullptr;
        ringIndicators_.fill(nullptr);
        ampEnvDisplay_ = nullptr;
        filterEnvDisplay_ = nullptr;
        modEnvDisplay_ = nullptr;
        euclideanControlsGroup_ = nullptr;
        euclideanDotDisplay_ = nullptr;
        arpEuclideanGroup_ = nullptr;
        diceButton_ = nullptr;
        lfo1RateGroup_ = nullptr;
        lfo2RateGroup_ = nullptr;
        lfo1NoteValueGroup_ = nullptr;
        lfo2NoteValueGroup_ = nullptr;
        chaosRateGroup_ = nullptr;
        chaosNoteValueGroup_ = nullptr;
        shRateGroup_ = nullptr;
        shNoteValueGroup_ = nullptr;
        randomRateGroup_ = nullptr;
        randomNoteValueGroup_ = nullptr;
        delayTimeGroup_ = nullptr;
        delayNoteValueGroup_ = nullptr;
        phaserRateGroup_ = nullptr;
        phaserNoteValueGroup_ = nullptr;
        noModulationGroup_ = nullptr;
        phaserControlsGroup_ = nullptr;
        flangerControlsGroup_ = nullptr;
        chorusControlsGroup_ = nullptr;
        flangerRateGroup_ = nullptr;
        flangerNoteValueGroup_ = nullptr;
        chorusRateGroup_ = nullptr;
        chorusNoteValueGroup_ = nullptr;
        tranceGateRateGroup_ = nullptr;
        tranceGateNoteValueGroup_ = nullptr;
        arpRateGroup_ = nullptr;
        arpNoteValueGroup_ = nullptr;
        polyGroup_ = nullptr;
        monoGroup_ = nullptr;

        harmonizerVoiceRows_.fill(nullptr);
        oscAPWKnob_ = nullptr;
        oscBPWKnob_ = nullptr;
        arpRootNoteGroup_ = nullptr;
        arpQuantizeInputGroup_ = nullptr;
        spectralCurveDropdown_ = nullptr;
        spectralBitsGroup_ = nullptr;

        settingsDrawer_ = nullptr;
        settingsOverlay_ = nullptr;
        gearButton_ = nullptr;
        settingsAnimTimer_ = nullptr;
        settingsDrawerOpen_ = false;
        settingsDrawerProgress_ = 0.0f;
        settingsDrawerTargetOpen_ = false;

        playbackPollTimer_ = nullptr;
        trailTimer_ = nullptr;

        for (auto& ts : laneTrailStates_) ts.clear();
        lastPolledSteps_.fill(-1);
        wasTransportPlaying_ = false;

        clipboard_.clear();

        presetBrowserView_ = nullptr;
        arpPresetBrowserView_ = nullptr;
        savePresetDialogView_ = nullptr;
        adsrExpandedOverlay_ = nullptr;

        // Stop update banner polling (view is owned by frame)
        if (updateBannerView_) {
            updateBannerView_->stopPolling();
            updateBannerView_ = nullptr;
        }

        // Notify processor that editor is closing (FR-012)
        {
            auto msg = Steinberg::owned(allocateMessage());
            if (msg) {
                msg->setMessageID("EditorState");
                auto* attrs = msg->getAttributes();
                if (attrs) {
                    attrs->setInt("open", 0);
                    sendMessage(msg);
                }
            }
        }

        activeEditor_ = nullptr;
    }
}

// ==============================================================================
// IControlListener: Action Button Handling
// ==============================================================================

void Controller::valueChanged(VSTGUI::CControl* control) {
    if (!control) return;

    auto tag = control->getTag();

    // Toggle buttons: respond to both on/off clicks (no value guard)
    switch (tag) {
        case kActionSettingsToggleTag:  toggleSettingsDrawer(); return;
        case kActionSettingsOverlayTag:
            if (settingsDrawerOpen_) toggleSettingsDrawer();
            return;
        default: break;
    }

    // Dice trigger button
    if (tag == static_cast<int32_t>(kArpDiceTriggerId) &&
        control->getValue() >= 0.5f) {
        performEdit(kArpDiceTriggerId, 0.0);
        setParamNormalized(kArpDiceTriggerId, 0.0);
        return;
    }

    // Pattern preset dropdown
    if (control == presetDropdown_) {
        if (!stepPatternEditor_) return;
        int index = static_cast<int>(control->getValue());
        switch (index) {
            case 0: stepPatternEditor_->applyPresetAll(); break;
            case 1: stepPatternEditor_->applyPresetOff(); break;
            case 2: stepPatternEditor_->applyPresetAlternate(); break;
            case 3: stepPatternEditor_->applyPresetRampUp(); break;
            case 4: stepPatternEditor_->applyPresetRampDown(); break;
            case 5: stepPatternEditor_->applyPresetRandom(); break;
            default: break;
        }
        return;
    }

    // Momentary buttons: only respond to press (value > 0.5), not release
    if (control->getValue() < 0.5f) return;

    switch (tag) {
        case kActionTransformInvertTag:
            if (stepPatternEditor_) stepPatternEditor_->applyTransformInvert();
            break;
        case kActionTransformShiftRightTag:
            if (stepPatternEditor_) stepPatternEditor_->applyTransformShiftRight();
            break;
        case kActionTransformShiftLeftTag:
            if (stepPatternEditor_) stepPatternEditor_->applyTransformShiftLeft();
            break;
        case kActionEuclideanRegenTag:
            if (stepPatternEditor_) stepPatternEditor_->regenerateEuclidean();
            break;

        default:
            break;
    }
}

// ==============================================================================
// Sidechain Indicator Update
// ==============================================================================

void Controller::updateSidechainIndicator(VSTGUI::CView* indicator) const {
    auto* label = dynamic_cast<VSTGUI::CTextLabel*>(indicator);
    if (!label) return;

    if (sidechainActive_) {
        label->setText("SIDECHAIN ACTIVE");
        label->setFontColor(VSTGUI::CColor(90, 200, 130, 255));  // green
    } else {
        label->setText("NO SIDECHAIN \xe2\x80\x94 analyzing synth output");
        label->setFontColor(VSTGUI::CColor(200, 160, 60, 255));  // amber
    }
    label->invalid();
}

// ==============================================================================
// ADSR Expanded Overlay
// ==============================================================================

void Controller::openAdsrExpandedOverlay(EnvelopeType env) {
    if (!adsrExpandedOverlay_)
        return;

    expandedEnvType_ = env;

    auto* display = adsrExpandedOverlay_->getDisplay();
    if (!display)
        return;

    // Configure title and colors based on envelope type
    uint32_t adsrBaseId = 0;
    uint32_t curveBaseId = 0;
    uint32_t bezierEnabledId = 0;
    uint32_t bezierBaseId = 0;

    switch (env) {
    case EnvelopeType::kAmp:
        adsrExpandedOverlay_->setTitle("AMP Envelope");
        adsrExpandedOverlay_->setColors(
            VSTGUI::CColor(0x50, 0x8C, 0xC8),       // amp-env stroke
            VSTGUI::CColor(0x50, 0x8C, 0xC8, 0x40),  // amp-env fill
            VSTGUI::CColor(0x50, 0x8C, 0xC8));        // amp-env control-point
        adsrBaseId = kAmpEnvAttackId;
        curveBaseId = kAmpEnvAttackCurveId;
        bezierEnabledId = kAmpEnvBezierEnabledId;
        bezierBaseId = kAmpEnvBezierAttackCp1XId;
        break;
    case EnvelopeType::kFilter:
        adsrExpandedOverlay_->setTitle("FILTER Envelope");
        adsrExpandedOverlay_->setColors(
            VSTGUI::CColor(0xDC, 0xAA, 0x3C),       // filter-env stroke
            VSTGUI::CColor(0xDC, 0xAA, 0x3C, 0x40),  // filter-env fill
            VSTGUI::CColor(0xDC, 0xAA, 0x3C));        // filter-env control-point
        adsrBaseId = kFilterEnvAttackId;
        curveBaseId = kFilterEnvAttackCurveId;
        bezierEnabledId = kFilterEnvBezierEnabledId;
        bezierBaseId = kFilterEnvBezierAttackCp1XId;
        break;
    case EnvelopeType::kMod:
        adsrExpandedOverlay_->setTitle("MOD Envelope");
        adsrExpandedOverlay_->setColors(
            VSTGUI::CColor(0xA0, 0x5A, 0xC8),       // mod-env stroke
            VSTGUI::CColor(0xA0, 0x5A, 0xC8, 0x40),  // mod-env fill
            VSTGUI::CColor(0xA0, 0x5A, 0xC8));        // mod-env control-point
        adsrBaseId = kModEnvAttackId;
        curveBaseId = kModEnvAttackCurveId;
        bezierEnabledId = kModEnvBezierEnabledId;
        bezierBaseId = kModEnvBezierAttackCp1XId;
        break;
    }

    // Configure param IDs on the expanded display
    display->setAdsrBaseParamId(adsrBaseId);
    display->setCurveBaseParamId(curveBaseId);
    display->setBezierEnabledParamId(bezierEnabledId);
    display->setBezierBaseParamId(bezierBaseId);

    // Sync current parameter values
    syncAdsrDisplay(display, adsrBaseId, curveBaseId, bezierEnabledId, bezierBaseId);

    // Wire playback state for the matching envelope
    switch (env) {
    case EnvelopeType::kAmp:
        if (ampEnvOutputPtr_ && ampEnvStagePtr_ && envVoiceActivePtr_)
            display->setPlaybackStatePointers(ampEnvOutputPtr_, ampEnvStagePtr_, envVoiceActivePtr_);
        break;
    case EnvelopeType::kFilter:
        if (filterEnvOutputPtr_ && filterEnvStagePtr_ && envVoiceActivePtr_)
            display->setPlaybackStatePointers(filterEnvOutputPtr_, filterEnvStagePtr_, envVoiceActivePtr_);
        break;
    case EnvelopeType::kMod:
        if (modEnvOutputPtr_ && modEnvStagePtr_ && envVoiceActivePtr_)
            display->setPlaybackStatePointers(modEnvOutputPtr_, modEnvStagePtr_, envVoiceActivePtr_);
        break;
    }

    adsrExpandedOverlay_->open();
}

void Controller::closeAdsrExpandedOverlay() {
    if (adsrExpandedOverlay_ && adsrExpandedOverlay_->isOpen())
        adsrExpandedOverlay_->close();
}

} // namespace Ruinae
