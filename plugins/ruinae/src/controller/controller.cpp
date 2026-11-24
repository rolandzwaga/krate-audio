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
#include "ui/krate_vst3_editor.h"
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
#include "parameters/voice_lfo_params.h"
#include "parameters/arpeggiator_params.h"
#include "parameters/state_prefix_validator.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/events.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Custom editor that intercepts right-clicks over StepPatternEditor.
// VST3Editor::onMouseEvent consumes ALL right-clicks for context menus
// before views see them, so we must handle it at this level.
class RuinaeEditor : public Krate::Plugins::KrateVST3Editor {
public:
    RuinaeEditor(Steinberg::Vst::EditController* controller,
                 VSTGUI::UTF8StringPtr templateName,
                 VSTGUI::UTF8StringPtr xmlFile)
        : KrateVST3Editor(controller, templateName, xmlFile) {}

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

    // UI-only: Mod source view mode dropdown (10 entries), ephemeral, not persisted.
    // Hidden and not automatable: host automation would switch templates from outside the control.
    {
        auto* modViewParam = new Steinberg::Vst::StringListParameter(
            STR16("Mod Source View"), kModSourceViewModeTag, nullptr,
            Steinberg::Vst::ParameterInfo::kIsList | Steinberg::Vst::ParameterInfo::kIsHidden);
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

    // UI-only: Main tab selector (4 entries), ephemeral, not persisted; hidden, not automatable
    {
        auto* tabParam = new Steinberg::Vst::StringListParameter(
            STR16("Main Tab"), kMainTabTag, nullptr,
            Steinberg::Vst::ParameterInfo::kIsList | Steinberg::Vst::ParameterInfo::kIsHidden);
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
    registerVoiceLfoParams(parameters);
    registerArpParams(parameters);

    // Sidechain active status (hidden, read-only output parameter)
    parameters.addParameter(STR16("Sidechain Active"), STR16(""), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kIsHidden | Steinberg::Vst::ParameterInfo::kIsReadOnly,
        kSidechainActiveId);

    // Arp skip highlights (hidden, read-only output parameters; R-069)
    {
        static const Steinberg::Vst::TChar* const kSkipTitles[kArpLaneCount] = {
            STR16("Arp Skip Velocity"), STR16("Arp Skip Gate"), STR16("Arp Skip Pitch"),
            STR16("Arp Skip Ratchet"), STR16("Arp Skip Modifier"), STR16("Arp Skip Condition"),
            STR16("Arp Skip Chord"), STR16("Arp Skip Inversion")};
        for (int lane = 0; lane < kArpLaneCount; ++lane) {
            parameters.addParameter(kSkipTitles[lane], STR16(""), 0, 0.0,
                Steinberg::Vst::ParameterInfo::kIsHidden | Steinberg::Vst::ParameterInfo::kIsReadOnly,
                kArpSkipLane0Id + static_cast<Steinberg::Vst::ParamID>(lane));
        }
    }

    // Processor display data (hidden, read-only output parameters; R-005)
    {
        const auto addDisplayParam = [this](const Steinberg::Vst::TChar* title,
                                            Steinberg::Vst::ParamID id, double defaultValue) {
            parameters.addParameter(title, STR16(""), 0, defaultValue,
                Steinberg::Vst::ParameterInfo::kIsHidden | Steinberg::Vst::ParameterInfo::kIsReadOnly,
                id);
        };
        addDisplayParam(STR16("Trance Gate Step"), kTranceGatePlaybackStepId, 0.0);
        addDisplayParam(STR16("Transport Playing"), kTransportPlayingId, 0.0);
        addDisplayParam(STR16("Amp Env Output"), kAmpEnvDisplayOutputId, 0.0);
        addDisplayParam(STR16("Amp Env Stage"), kAmpEnvDisplayStageId, 0.0);
        addDisplayParam(STR16("Filter Env Output"), kFilterEnvDisplayOutputId, 0.0);
        addDisplayParam(STR16("Filter Env Stage"), kFilterEnvDisplayStageId, 0.0);
        addDisplayParam(STR16("Mod Env Output"), kModEnvDisplayOutputId, 0.0);
        addDisplayParam(STR16("Mod Env Stage"), kModEnvDisplayStageId, 0.0);
        addDisplayParam(STR16("Env Voice Active"), kEnvDisplayVoiceActiveId, 0.0);
        addDisplayParam(STR16("Morph Pad Mod X"), kMorphPadModXId, 0.5);
        addDisplayParam(STR16("Morph Pad Mod Y"), kMorphPadModYId, 0.5);
    }

    // MIDI performance controllers, reached through IMidiMapping (R-030, R-042).
    // Hidden and not part of the state: the host owns the wheel positions.
    parameters.addParameter(STR16("Pitch Bend"), STR16(""), 0, 0.5,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsHidden,
        kMidiPitchBendId);
    parameters.addParameter(STR16("Aftertouch"), STR16(""), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsHidden,
        kMidiAftertouchId);
    parameters.addParameter(STR16("Mod Wheel"), STR16(""), 0, 0.0,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsHidden,
        kMidiModWheelId);

    // ==========================================================================
    // Initialize Preset Manager
    // ==========================================================================
    createPresetManager({}, {});

    // Update checker
    updateChecker_ = std::make_unique<Krate::Plugins::UpdateChecker>(makeRuinaeUpdateConfig());

    return Steinberg::kResultTrue;
}

void Controller::createPresetManager(std::filesystem::path userDirOverride,
                                     std::filesystem::path factoryDirOverride) {
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(
        makeRuinaePresetConfig(), nullptr, this, std::move(userDirOverride),
        std::move(factoryDirOverride));

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
}

Steinberg::tresult PLUGIN_API Controller::terminate() {
    playbackPollTimer_ = nullptr;
    trailTimer_ = nullptr;
    // An editor the host has not closed yet keeps its views; stop caching them.
    viewRegistry_.clear();
    // updateChecker_ and presetManager_ stay until the destructor: an editor a host has not
    // closed yet still polls the checker (update banner, Check for Updates button) and its
    // preset browsers and save dialog hold the manager, and that editor holds a reference
    // to this controller, so the destructor runs only after its views are gone
    return EditControllerEx1::terminate();
}

// ==============================================================================
// IEditController
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::setComponentState(
    Steinberg::IBStream* state) {

    if (!state)
        return Steinberg::kResultFalse;

    // Read the whole stream first (an IBStream may not seek): the truncation
    // check and loadStateCore read the same buffer.
    std::vector<char> bytes;
    {
        constexpr Steinberg::int32 kChunkSize = 4096;
        char chunk[kChunkSize];
        Steinberg::int32 bytesRead = 0;
        while (state->read(chunk, kChunkSize, &bytesRead) == Steinberg::kResultTrue &&
               bytesRead > 0) {
            bytes.insert(bytes.end(), chunk, chunk + bytesRead);
        }
    }
    Steinberg::MemoryStream memStream(bytes.data(), static_cast<Steinberg::TSize>(bytes.size()));
    Steinberg::IBStreamer streamer(&memStream, kLittleEndian);

    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version))
        return Steinberg::kResultTrue; // Empty stream, keep defaults

    if (version < 1 || version > Ruinae::kCurrentStateVersion)
        return Steinberg::kResultTrue; // Unknown version, keep defaults

    // Cut inside the required prefix: rejected before any parameter is set,
    // as Processor::setState rejects it (R-076)
    if (!Ruinae::isRequiredStatePrefixComplete(bytes.data(), bytes.size(), version))
        return Steinberg::kResultFalse;

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
// IMidiMapping (R-030, R-042)
// ==============================================================================
// VST3 hosts and the AU wrapper deliver pitch bend, channel pressure and CCs only
// as parameter changes on the IDs named here (model: Profundum's controller).

Steinberg::tresult PLUGIN_API Controller::getMidiControllerAssignment(
    Steinberg::int32 busIndex, Steinberg::int16 /*channel*/,
    Steinberg::Vst::CtrlNumber midiControllerNumber, Steinberg::Vst::ParamID& id) {
    if (busIndex != 0) {
        return Steinberg::kResultFalse;
    }
    switch (midiControllerNumber) {
        case Steinberg::Vst::kPitchBend:
            id = kMidiPitchBendId;
            return Steinberg::kResultOk;
        case Steinberg::Vst::kAfterTouch:
            id = kMidiAftertouchId;
            return Steinberg::kResultOk;
        case Steinberg::Vst::kCtrlModWheel:
            id = kMidiModWheelId;
            return Steinberg::kResultOk;
        default:
            return Steinberg::kResultFalse;
    }
}

// ==============================================================================
// IMessage: Receive processor messages
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::notify(Steinberg::Vst::IMessage* message) {
    if (!message)
        return Steinberg::kInvalidArgument;

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
            [this](VSTGUI::CVSTGUITimer*) { pollProcessorDisplays(); }, 33); // ~30fps

        trailTimer_ = playbackPollTimer_;
    }

    // Reset trail states on editor open
    for (auto& ts : laneTrailStates_) ts.clear();
    lastPolledSteps_.fill(-1);
    // A skip published before the editor opened is not a new one (R-069)
    for (int lane = 0; lane < kArpLaneCount; ++lane)
        lastSkipCodes_[static_cast<size_t>(lane)] = readArpSkipCode(lane);

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

void Controller::pollProcessorDisplays() {
    // Everything here comes from the processor's display-data output parameters
    // (R-005): values the host delivered to this controller, never the processor's memory.
    const auto norm = [this](Steinberg::Vst::ParamID id) { return getParamNormalized(id); };
    const bool playing = norm(kTransportPlayingId) >= 0.5;

    // Trance gate step indicator
    if (stepPatternEditor_) {
        const long encodedStep = std::lround(norm(kTranceGatePlaybackStepId) * kTranceGateDisplayStepScale);
        stepPatternEditor_->setPlaybackStep(static_cast<int>(encodedStep) - 1);
        stepPatternEditor_->setPlaying(playing);
    }

    // Morph pad modulation animation
    if (xyMorphPad_ && morphPadFollowsProcessor_ && !xyMorphPad_->isDragging()) {
        xyMorphPad_->setMorphPosition(static_cast<float>(norm(kMorphPadModXId)),
                                      static_cast<float>(norm(kMorphPadModYId)));
    }

    // ADSR playback dots (the three strip displays and the expanded overlay's)
    const bool voiceActive = norm(kEnvDisplayVoiceActiveId) >= 0.5;
    const auto pushEnv = [&](Krate::Plugins::ADSRDisplay* display, Steinberg::Vst::ParamID outputId,
                             Steinberg::Vst::ParamID stageId) {
        if (!display) return;
        display->setPlaybackState(static_cast<float>(norm(outputId)),
                                  static_cast<int>(std::lround(norm(stageId) * kEnvDisplayStageScale)),
                                  voiceActive);
    };
    const auto pushEnvType = [&](Krate::Plugins::ADSRDisplay* display, EnvelopeType env) {
        switch (env) {
        case EnvelopeType::kAmp:
            pushEnv(display, kAmpEnvDisplayOutputId, kAmpEnvDisplayStageId);
            break;
        case EnvelopeType::kFilter:
            pushEnv(display, kFilterEnvDisplayOutputId, kFilterEnvDisplayStageId);
            break;
        case EnvelopeType::kMod:
            pushEnv(display, kModEnvDisplayOutputId, kModEnvDisplayStageId);
            break;
        }
    };
    pushEnvType(ampEnvDisplay_, EnvelopeType::kAmp);
    pushEnvType(filterEnvDisplay_, EnvelopeType::kFilter);
    pushEnvType(modEnvDisplay_, EnvelopeType::kMod);
    if (adsrExpandedOverlay_ && adsrExpandedOverlay_->isOpen())
        pushEnvType(adsrExpandedOverlay_->getDisplay(), adsrExpandedEnv_);

    // 081-interaction-polish US1: Detect transport stop and clear trails
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

    pollArpLanePlayheads();
}

void Controller::pollArpLanePlayheads() {
    // The processor writes the playhead parameters (a step index, or the 1.0 sentinel when the
    // arp is off), so the lanes follow the parameters, not the host's transport flag: a host
    // without transport still runs the arp, and a stopped transport leaves the sentinel or the
    // last step as published (R-085, R-144).
    auto pollLanePlayhead = [&](Krate::Plugins::IArpLane* lane,
                                Steinberg::Vst::ParamID playheadParamId,
                                int laneIdx) {
        if (!lane) return;
        auto* param = getParameterObject(playheadParamId);
        if (!param) return;

        constexpr long kMaxArpStepsL = static_cast<long>(kMaxArpSteps);
        double normalized = param->getNormalized();
        long rawStep = std::lround(normalized * kMaxArpStepsL);
        int32_t step = rawStep >= kMaxArpStepsL ? -1 : static_cast<int32_t>(rawStep);

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

    pollLanePlayhead(velocityLane_, kArpVelocityPlayheadId, 0);
    pollLanePlayhead(gateLane_, kArpGatePlayheadId, 1);
    pollLanePlayhead(pitchLane_, kArpPitchPlayheadId, 2);
    pollLanePlayhead(ratchetLane_, kArpRatchetPlayheadId, 3);
    pollLanePlayhead(modifierLane_, kArpModifierPlayheadId, 4);
    pollLanePlayhead(conditionLane_, kArpConditionPlayheadId, 5);
    pollLanePlayhead(chordLane_, kArpChordPlayheadId, 6);
    pollLanePlayhead(inversionLane_, kArpInversionPlayheadId, 7);

    // Skip highlights (R-069): each lane's own skipped step, published as an output
    // parameter; a changed code is a new skip.
    for (int lane = 0; lane < kArpLaneCount; ++lane) {
        const long code = readArpSkipCode(lane);
        auto& last = lastSkipCodes_[static_cast<size_t>(lane)];
        if (code == last) continue;
        last = code;
        if (code > 0) handleArpSkipEvent(lane, static_cast<int>((code - 1) % 33));
    }
}

std::vector<std::pair<const char*, VSTGUI::CView*>> Controller::cachedViewPointerInventoryForTest() const {
    std::vector<std::pair<const char*, VSTGUI::CView*>> out;
    auto add = [&out](const char* name, VSTGUI::CView* view) { out.emplace_back(name, view); };
    add("stepPatternEditor_", stepPatternEditor_);
    add("presetDropdown_", presetDropdown_);
    add("xyMorphPad_", xyMorphPad_);
    add("modMatrixGrid_", modMatrixGrid_);
    static constexpr std::array<const char*, 7> kRingNames{
        "ringIndicators_[0]", "ringIndicators_[1]", "ringIndicators_[2]", "ringIndicators_[3]",
        "ringIndicators_[4]", "ringIndicators_[5]", "ringIndicators_[6]"};
    for (size_t i = 0; i < ringIndicators_.size(); ++i) add(kRingNames[i], ringIndicators_[i]);
    add("ampEnvDisplay_", ampEnvDisplay_);
    add("filterEnvDisplay_", filterEnvDisplay_);
    add("modEnvDisplay_", modEnvDisplay_);
    add("euclideanControlsGroup_", euclideanControlsGroup_);
    add("euclideanDotDisplay_", euclideanDotDisplay_);
    add("lfo1RateGroup_", lfo1RateGroup_);
    add("lfo2RateGroup_", lfo2RateGroup_);
    add("lfo1NoteValueGroup_", lfo1NoteValueGroup_);
    add("lfo2NoteValueGroup_", lfo2NoteValueGroup_);
    add("lfo1WaveformDisplay_", lfo1WaveformDisplay_);
    add("lfo2WaveformDisplay_", lfo2WaveformDisplay_);
    add("chaosModDisplay_", chaosModDisplay_);
    add("runglerDisplay_", runglerDisplay_);
    add("sampleHoldDisplay_", sampleHoldDisplay_);
    add("randomModDisplay_", randomModDisplay_);
    add("sidechainIndicatorEnvFollower_", sidechainIndicatorEnvFollower_);
    add("sidechainIndicatorPitchFollower_", sidechainIndicatorPitchFollower_);
    add("sidechainIndicatorTransient_", sidechainIndicatorTransient_);
    add("chaosRateGroup_", chaosRateGroup_);
    add("chaosNoteValueGroup_", chaosNoteValueGroup_);
    add("shRateGroup_", shRateGroup_);
    add("shNoteValueGroup_", shNoteValueGroup_);
    add("randomRateGroup_", randomRateGroup_);
    add("randomNoteValueGroup_", randomNoteValueGroup_);
    add("delayTimeGroup_", delayTimeGroup_);
    add("delayNoteValueGroup_", delayNoteValueGroup_);
    add("phaserRateGroup_", phaserRateGroup_);
    add("phaserNoteValueGroup_", phaserNoteValueGroup_);
    add("noModulationGroup_", noModulationGroup_);
    add("phaserControlsGroup_", phaserControlsGroup_);
    add("flangerControlsGroup_", flangerControlsGroup_);
    add("chorusControlsGroup_", chorusControlsGroup_);
    add("flangerRateGroup_", flangerRateGroup_);
    add("flangerNoteValueGroup_", flangerNoteValueGroup_);
    add("chorusRateGroup_", chorusRateGroup_);
    add("chorusNoteValueGroup_", chorusNoteValueGroup_);
    add("tranceGateRateGroup_", tranceGateRateGroup_);
    add("tranceGateNoteValueGroup_", tranceGateNoteValueGroup_);
    add("arpRateGroup_", arpRateGroup_);
    add("arpNoteValueGroup_", arpNoteValueGroup_);
    add("arpLaneContainer_", arpLaneContainer_);
    add("velocityLane_", velocityLane_);
    add("gateLane_", gateLane_);
    add("pitchLane_", pitchLane_);
    add("ratchetLane_", ratchetLane_);
    add("modifierLane_", modifierLane_);
    add("conditionLane_", conditionLane_);
    add("chordLane_", chordLane_);
    add("inversionLane_", inversionLane_);
    add("polyGroup_", polyGroup_);
    add("monoGroup_", monoGroup_);
    static constexpr std::array<const char*, 4> kHarmonizerRowNames{
        "harmonizerVoiceRows_[0]", "harmonizerVoiceRows_[1]", "harmonizerVoiceRows_[2]",
        "harmonizerVoiceRows_[3]"};
    for (size_t i = 0; i < harmonizerVoiceRows_.size(); ++i)
        add(kHarmonizerRowNames[i], harmonizerVoiceRows_[i]);
    add("oscAPWKnob_", oscAPWKnob_);
    add("oscBPWKnob_", oscBPWKnob_);
    add("spectralCurveDropdown_", spectralCurveDropdown_);
    add("spectralBitsGroup_", spectralBitsGroup_);
    add("settingsDrawer_", settingsDrawer_);
    add("settingsOverlay_", settingsOverlay_);
    add("gearButton_", gearButton_);
    add("arpEuclideanGroup_", arpEuclideanGroup_);
    add("diceButton_", diceButton_);
    add("arpRootNoteGroup_", arpRootNoteGroup_);
    add("arpQuantizeInputGroup_", arpQuantizeInputGroup_);
    add("adsrExpandedOverlay_", adsrExpandedOverlay_);
    add("presetBrowserView_", presetBrowserView_);
    add("arpPresetBrowserView_", arpPresetBrowserView_);
    add("savePresetDialogView_", savePresetDialogView_);
    add("updateBannerView_", updateBannerView_);
    return out;
}

void Controller::willClose(VSTGUI::VST3Editor* editor) {
    if (activeEditor_ == editor) {
        // Every view pointer verifyView() cached: the frame and its views are about to go.
        viewRegistry_.clear();

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

    // Dice trigger button: VST3Editor already sent the host performEdit(1.0).
    // Reset only the controller's own parameter; a performEdit(0.0) here would
    // land in the same block and cancel the trigger (R-142).
    if (tag == static_cast<int32_t>(kArpDiceTriggerId) &&
        control->getValue() >= 0.5f) {
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

    // The poll timer feeds this envelope's playback state to the expanded display
    adsrExpandedEnv_ = env;

    adsrExpandedOverlay_->open();
}

void Controller::closeAdsrExpandedOverlay() {
    if (adsrExpandedOverlay_ && adsrExpandedOverlay_->isOpen())
        adsrExpandedOverlay_->close();
}

} // namespace Ruinae
