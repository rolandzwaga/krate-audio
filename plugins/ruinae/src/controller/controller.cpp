// ==============================================================================
// Edit Controller Implementation
// ==============================================================================
// Core controller methods: initialization, lifecycle, state management,
// parameter sync, view wiring, and action button handling.
//
// Additional Controller methods are split across:
//   controller_param_display.cpp  - getParamStringByValue/getParamValueByString
//   controller_presets.cpp        - preset browser, custom views, state loading
//   controller_adsr.cpp           - ADSR display wiring/sync
//   controller_mod_matrix.cpp     - mod matrix grid & ring indicator wiring
//   controller_arp.cpp            - arp skip events, copy/paste
//   controller_settings.cpp       - settings drawer animation, tab changed
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
#include "ui/xy_morph_pad.h"
#include "ui/adsr_display.h"
#include "ui/mod_matrix_grid.h"
#include "ui/mod_ring_indicator.h"
#include "ui/mod_heatmap.h"
#include "ui/euclidean_dot_display.h"
#include "ui/category_tab_bar.h"
#include "ui/preset_browser_view.h"
#include "ui/save_preset_dialog_view.h"

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

// Maps destination index to the actual VST parameter ID of that knob.
// Tab-dependent: voice tab and global tab have different mappings.
// Sizes derived from central registry in mod_matrix_types.h.
// Used by ModRingIndicator base value sync (T069-T072).
static constexpr std::array<Steinberg::Vst::ParamID,
    Krate::Plugins::kNumVoiceDestinations> kVoiceDestParamIds = {{
    kFilterCutoffId,          // 0: Filter Cutoff
    kFilterResonanceId,       // 1: Filter Resonance
    kMixerPositionId,         // 2: Morph Position
    kDistortionDriveId,       // 3: Distortion Drive
    kTranceGateDepthId,       // 4: TranceGate Depth
    kOscATuneId,              // 5: OSC A Pitch
    kOscBTuneId,              // 6: OSC B Pitch
    kMixerTiltId,             // 7: Spectral Tilt
}};

static constexpr std::array<Steinberg::Vst::ParamID,
    Krate::Plugins::kNumGlobalDestinations> kGlobalDestParamIds = {{
    kGlobalFilterCutoffId,    // 0: Global Filter Cutoff
    kGlobalFilterResonanceId, // 1: Global Filter Resonance
    kMasterGainId,            // 2: Master Volume
    kDelayMixId,              // 3: Effect Mix
    kFilterCutoffId,          // 4: All Voice Filter Cutoff
    kMixerPositionId,         // 5: All Voice Morph Position
    kTranceGateDepthId,       // 6: All Voice TranceGate Rate
    kMixerTiltId,             // 7: All Voice Spectral Tilt
    kFilterResonanceId,       // 8: All Voice Resonance
    kFilterEnvAmountId,       // 9: All Voice Filter Env Amount
    // Arpeggiator destinations (078-modulation-integration)
    kArpFreeRateId,           // 10: Arp Rate
    kArpGateLengthId,         // 11: Arp Gate Length
    kArpOctaveRangeId,        // 12: Arp Octave Range
    kArpSwingId,              // 13: Arp Swing
    kArpSpiceId,              // 14: Arp Spice
}};

// Compile-time validation: param ID arrays must match destination registries
static_assert(kVoiceDestParamIds.size() == Krate::Plugins::kVoiceDestNames.size(),
    "kVoiceDestParamIds must match kVoiceDestNames size");
static_assert(kGlobalDestParamIds.size() == Krate::Plugins::kGlobalDestNames.size(),
    "kGlobalDestParamIds must match kGlobalDestNames size");

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
    presetManager_.reset();
    return EditControllerEx1::terminate();
}

// ==============================================================================
// IEditController
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::setComponentState(
    Steinberg::IBStream* state) {

    if (!state) {
        return Steinberg::kResultFalse;
    }

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version)) {
        return Steinberg::kResultTrue; // Empty stream, keep defaults
    }

    if (version != 1) {
        return Steinberg::kResultTrue; // Unknown version, keep defaults
    }

    bulkParamLoad_ = true;  // Suppress per-param view updates during bulk load
    FrameInvalidationGuard frameGuard(activeEditor_);  // Suppress VSTGUI invalidRect

    auto setParam = [this](Steinberg::Vst::ParamID id, double value) {
        setParamNormalized(id, value);
    };

    // Load all parameter packs in deterministic order (matching Processor::getState)
    loadGlobalParamsToController(streamer, setParam);
    loadOscAParamsToController(streamer, setParam);
    loadOscBParamsToController(streamer, setParam);
    loadMixerParamsToController(streamer, setParam);
    loadFilterParamsToController(streamer, setParam);
    loadDistortionParamsToController(streamer, setParam);
    loadTranceGateParamsToController(streamer, setParam);
    loadAmpEnvParamsToController(streamer, setParam);
    loadFilterEnvParamsToController(streamer, setParam);
    loadModEnvParamsToController(streamer, setParam);
    loadLFO1ParamsToController(streamer, setParam);
    loadLFO2ParamsToController(streamer, setParam);
    loadChaosModParamsToController(streamer, setParam);
    loadModMatrixParamsToController(streamer, setParam);
    loadGlobalFilterParamsToController(streamer, setParam);
    loadDelayParamsToController(streamer, setParam);
    loadReverbParamsToController(streamer, setParam);
    loadMonoModeParamsToController(streamer, setParam);

    // Skip voice routes (16 slots, processor-only data)
    for (int i = 0; i < 16; ++i) {
        Steinberg::int8 i8 = 0; float fv = 0;
        streamer.readInt8(i8);   // source
        streamer.readInt8(i8);   // destination
        streamer.readFloat(fv);  // amount
        streamer.readInt8(i8);   // curve
        streamer.readFloat(fv);  // smoothMs
        streamer.readInt8(i8);   // scale
        streamer.readInt8(i8);   // bypass
        streamer.readInt8(i8);   // active
    }

    // FX enable flags
    Steinberg::int8 i8 = 0;
    if (streamer.readInt8(i8))
        setParam(kDelayEnabledId, i8 != 0 ? 1.0 : 0.0);
    if (streamer.readInt8(i8))
        setParam(kReverbEnabledId, i8 != 0 ? 1.0 : 0.0);

    // Phaser params + enable flag
    loadPhaserParamsToController(streamer, setParam);
    if (streamer.readInt8(i8))
        setParam(kPhaserEnabledId, i8 != 0 ? 1.0 : 0.0);

    // Extended LFO params
    loadLFO1ExtendedParamsToController(streamer, setParam);
    loadLFO2ExtendedParamsToController(streamer, setParam);

    // Macro and Rungler params
    loadMacroParamsToController(streamer, setParam);
    loadRunglerParamsToController(streamer, setParam);

    // Settings params
    loadSettingsParamsToController(streamer, setParam);

    // Mod source params
    loadEnvFollowerParamsToController(streamer, setParam);
    loadSampleHoldParamsToController(streamer, setParam);
    loadRandomParamsToController(streamer, setParam);
    loadPitchFollowerParamsToController(streamer, setParam);
    loadTransientParamsToController(streamer, setParam);

    // Harmonizer params + enable flag
    loadHarmonizerParamsToController(streamer, setParam);
    if (streamer.readInt8(i8))
        setParam(kHarmonizerEnabledId, i8 != 0 ? 1.0 : 0.0);

    // Arpeggiator params (FR-012) -- backward compat: silently returns on
    // truncated/old streams, leaving arp controller params at defaults
    loadArpParamsToController(streamer, setParam);

    bulkParamLoad_ = false;  // Re-enable per-param view updates
    syncAllViews();           // Single batch sync of all custom views

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

                if (modMatrixGrid_) {
                    modMatrixGrid_->setVoiceRoute(i, route);
                }
            }
        }

        return Steinberg::kResultOk;
    }

    return EditControllerEx1::notify(message);
}

// ==============================================================================
// IEditController: Parameter Sync to Custom Views
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::setParamNormalized(
    Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) {

    // Let the base class handle its bookkeeping first
    auto result = EditControllerEx1::setParamNormalized(tag, value);

    // CRITICAL: Pointer-nulling for view lifecycle must happen even during bulk
    // loads. The base class setParamNormalized triggers IDependent notifications
    // which cause UIViewSwitchContainer to destroy old tab views. If we skip
    // onTabChanged(), the cached pointers become dangling and syncAllViews()
    // will dereference freed memory (use-after-free -> ACCESS_VIOLATION).
    if (tag == kMainTabTag) {
        int newTab = static_cast<int>(std::round(value * 3.0));
        onTabChanged(newTab);
    }
    if (tag == kOscATypeId && value > 0.01) {
        oscAPWKnob_ = nullptr;
    }
    if (tag == kOscBTypeId && value > 0.01) {
        oscBPWKnob_ = nullptr;
    }
    if (tag == kDistortionTypeId) {
        spectralCurveDropdown_ = nullptr;
        spectralBitsGroup_ = nullptr;
    }

    // During bulk parameter loads (preset switching), skip per-param view updates.
    // syncAllViews() will do a single batch sync afterwards.
    if (bulkParamLoad_)
        return result;

    // Push trance gate parameter changes to StepPatternEditor
    if (stepPatternEditor_) {
        if (tag >= kTranceGateStepLevel0Id && tag <= kTranceGateStepLevel31Id) {
            int stepIndex = static_cast<int>(tag - kTranceGateStepLevel0Id);
            stepPatternEditor_->setStepLevel(stepIndex, static_cast<float>(value));
        } else if (tag == kTranceGateNumStepsId) {
            int steps = std::clamp(
                static_cast<int>(2.0 + std::round(value * 30.0)), 2, 32);
            stepPatternEditor_->setNumSteps(steps);
        } else if (tag == kTranceGateEuclideanEnabledId) {
            stepPatternEditor_->setEuclideanEnabled(value >= 0.5);
            if (euclideanControlsGroup_)
                euclideanControlsGroup_->setVisible(value >= 0.5);
        } else if (tag == kTranceGateEuclideanHitsId) {
            int hits = std::clamp(
                static_cast<int>(std::round(value * 32.0)), 0, 32);
            stepPatternEditor_->setEuclideanHits(hits);
        } else if (tag == kTranceGateEuclideanRotationId) {
            int rot = std::clamp(
                static_cast<int>(std::round(value * 31.0)), 0, 31);
            stepPatternEditor_->setEuclideanRotation(rot);
        } else if (tag == kTranceGatePhaseOffsetId) {
            stepPatternEditor_->setPhaseOffset(static_cast<float>(value));
        }
    }

    // Push velocity lane parameter changes to ArpLaneEditor (079-layout-framework)
    if (velocityLane_) {
        if (tag >= kArpVelocityLaneStep0Id && tag <= kArpVelocityLaneStep31Id) {
            int stepIndex = static_cast<int>(tag - kArpVelocityLaneStep0Id);
            velocityLane_->setStepLevel(stepIndex, static_cast<float>(value));
            velocityLane_->setDirty(true);
        } else if (tag == kArpVelocityLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            velocityLane_->setNumSteps(steps);
            velocityLane_->setDirty(true);
        }
    }

    // Push gate lane parameter changes to ArpLaneEditor (079-layout-framework, US2)
    if (gateLane_) {
        if (tag >= kArpGateLaneStep0Id && tag <= kArpGateLaneStep31Id) {
            int stepIndex = static_cast<int>(tag - kArpGateLaneStep0Id);
            gateLane_->setStepLevel(stepIndex, static_cast<float>(value));
            gateLane_->setDirty(true);
        } else if (tag == kArpGateLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            gateLane_->setNumSteps(steps);
            gateLane_->setDirty(true);
        }
    }

    // Push pitch lane parameter changes (080-specialized-lane-types)
    if (pitchLane_) {
        if (tag >= kArpPitchLaneStep0Id && tag < kArpPitchLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpPitchLaneStep0Id);
            pitchLane_->setStepLevel(stepIndex, static_cast<float>(value));
            pitchLane_->setDirty(true);
        } else if (tag == kArpPitchLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            pitchLane_->setNumSteps(steps);
            pitchLane_->setDirty(true);
        }
    }

    // Push ratchet lane parameter changes (080-specialized-lane-types)
    if (ratchetLane_) {
        if (tag >= kArpRatchetLaneStep0Id && tag < kArpRatchetLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpRatchetLaneStep0Id);
            ratchetLane_->setStepLevel(stepIndex, static_cast<float>(value));
            ratchetLane_->setDirty(true);
        } else if (tag == kArpRatchetLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            ratchetLane_->setNumSteps(steps);
            ratchetLane_->setDirty(true);
        }
    }

    // Push modifier lane parameter changes (080-specialized-lane-types)
    if (modifierLane_) {
        if (tag >= kArpModifierLaneStep0Id && tag < kArpModifierLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpModifierLaneStep0Id);
            auto flags = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(value * 255.0)), 0, 255));
            modifierLane_->setStepFlags(stepIndex, flags);
            modifierLane_->setDirty(true);
        } else if (tag == kArpModifierLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            modifierLane_->setNumSteps(steps);
            modifierLane_->setDirty(true);
        }
    }

    // Push condition lane parameter changes (080-specialized-lane-types)
    if (conditionLane_) {
        if (tag >= kArpConditionLaneStep0Id && tag < kArpConditionLaneStep0Id + 32) {
            int stepIndex = static_cast<int>(tag - kArpConditionLaneStep0Id);
            auto condIndex = static_cast<uint8_t>(
                std::clamp(static_cast<int>(std::round(value * 17.0)), 0, 17));
            conditionLane_->setStepCondition(stepIndex, condIndex);
            conditionLane_->setDirty(true);
        } else if (tag == kArpConditionLaneLengthId) {
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
            conditionLane_->setNumSteps(steps);
            conditionLane_->setDirty(true);
        }
    }

    // 081-interaction-polish US5: Push arp Euclidean parameter changes to
    // EuclideanDotDisplay and linear overlays on bar lanes
    if (tag == kArpEuclideanHitsId || tag == kArpEuclideanStepsId ||
        tag == kArpEuclideanRotationId || tag == kArpEuclideanEnabledId) {
        auto readInt = [this](Steinberg::Vst::ParamID pid, double scale,
                              double offset, int lo, int hi) -> int {
            auto* p = getParameterObject(pid);
            if (!p) return lo;
            return std::clamp(
                static_cast<int>(offset + std::round(p->getNormalized() * scale)),
                lo, hi);
        };
        int hits = readInt(kArpEuclideanHitsId, 32.0, 0.0, 0, 32);
        int steps = readInt(kArpEuclideanStepsId, 30.0, 2.0, 2, 32);
        int rot = readInt(kArpEuclideanRotationId, 31.0, 0.0, 0, 31);
        auto* enabledParam = getParameterObject(kArpEuclideanEnabledId);
        bool enabled = (enabledParam != nullptr) && enabledParam->getNormalized() >= 0.5;

        if (euclideanDotDisplay_) {
            euclideanDotDisplay_->setSteps(steps);
            euclideanDotDisplay_->setHits(hits);
            euclideanDotDisplay_->setRotation(rot);
            euclideanDotDisplay_->invalid();
        }

        Krate::Plugins::IArpLane* lanes[] = {
            velocityLane_, gateLane_, pitchLane_, ratchetLane_,
            modifierLane_, conditionLane_};
        for (auto* lane : lanes) {
            if (lane) lane->setEuclideanOverlay(hits, steps, rot, enabled);
        }

        if (tag == kArpEuclideanEnabledId && arpEuclideanGroup_) {
            arpEuclideanGroup_->setVisible(enabled);
        }
    }

    // Toggle LFO Rate/NoteValue visibility based on sync state
    if (tag == kLFO1SyncId) {
        if (lfo1RateGroup_) lfo1RateGroup_->setVisible(value < 0.5);
        if (lfo1NoteValueGroup_) lfo1NoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kLFO2SyncId) {
        if (lfo2RateGroup_) lfo2RateGroup_->setVisible(value < 0.5);
        if (lfo2NoteValueGroup_) lfo2NoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kChaosModSyncId) {
        if (chaosRateGroup_) chaosRateGroup_->setVisible(value < 0.5);
        if (chaosNoteValueGroup_) chaosNoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kSampleHoldSyncId) {
        if (shRateGroup_) shRateGroup_->setVisible(value < 0.5);
        if (shNoteValueGroup_) shNoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kRandomSyncId) {
        if (randomRateGroup_) randomRateGroup_->setVisible(value < 0.5);
        if (randomNoteValueGroup_) randomNoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kDelaySyncId) {
        if (delayTimeGroup_) delayTimeGroup_->setVisible(value < 0.5);
        if (delayNoteValueGroup_) delayNoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kPhaserSyncId) {
        if (phaserRateGroup_) phaserRateGroup_->setVisible(value < 0.5);
        if (phaserNoteValueGroup_) phaserNoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kTranceGateTempoSyncId) {
        if (tranceGateRateGroup_) tranceGateRateGroup_->setVisible(value < 0.5);
        if (tranceGateNoteValueGroup_) tranceGateNoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kArpTempoSyncId) {
        if (arpRateGroup_) arpRateGroup_->setVisible(value < 0.5);
        if (arpNoteValueGroup_) arpNoteValueGroup_->setVisible(value >= 0.5);
    }
    if (tag == kVoiceModeId) {
        if (polyGroup_) polyGroup_->setVisible(value < 0.5);
        if (monoGroup_) monoGroup_->setVisible(value >= 0.5);
    }

    // Harmonizer voice row dimming based on NumVoices
    if (tag == kHarmonizerNumVoicesId) {
        int numVoices = static_cast<int>(
            value * (kHarmonizerNumVoicesCount - 1) + 0.5) + 1;
        for (int i = 0; i < 4; ++i) {
            if (harmonizerVoiceRows_[static_cast<size_t>(i)]) {
                harmonizerVoiceRows_[static_cast<size_t>(i)]->setAlphaValue(
                    i < numVoices ? 1.0f : 0.3f);
            }
        }
    }

    // Arp Scale Mode dimming (084-arp-scale-mode FR-011)
    if (tag == kArpScaleTypeId) {
        bool isChromatic = (value < 0.01);
        float alpha = isChromatic ? 0.35f : 1.0f;
        if (arpRootNoteGroup_) {
            arpRootNoteGroup_->setAlphaValue(alpha);
            arpRootNoteGroup_->setMouseEnabled(!isChromatic);
        }
        if (arpQuantizeInputGroup_) {
            arpQuantizeInputGroup_->setAlphaValue(alpha);
            arpQuantizeInputGroup_->setMouseEnabled(!isChromatic);
        }
        if (pitchLane_) {
            int uiIndex = std::clamp(
                static_cast<int>(value * (kArpScaleTypeCount - 1) + 0.5),
                0, kArpScaleTypeCount - 1);
            int enumValue = kArpScaleDisplayOrder[static_cast<size_t>(uiIndex)];
            pitchLane_->setScaleType(enumValue);
        }
    }

    // PW knob visual disable (068-osc-type-params FR-016)
    if (tag == kOscAWaveformId && oscAPWKnob_) {
        int wf = static_cast<int>(value * 4.0 + 0.5);
        oscAPWKnob_->setAlphaValue(wf == 3 ? 1.0f : 0.3f);
    }
    if (tag == kOscBWaveformId && oscBPWKnob_) {
        int wf = static_cast<int>(value * 4.0 + 0.5);
        oscBPWKnob_->setAlphaValue(wf == 3 ? 1.0f : 0.3f);
    }
    // Spectral distortion control dimming
    if (tag == kDistortionSpectralModeId) {
        int mode = std::clamp(
            static_cast<int>(value * (kSpectralModeCount - 1) + 0.5),
            0, kSpectralModeCount - 1);
        bool isBitcrush = (mode == 3);
        if (spectralCurveDropdown_) {
            spectralCurveDropdown_->setAlphaValue(isBitcrush ? 0.35f : 1.0f);
            spectralCurveDropdown_->setMouseEnabled(!isBitcrush);
        }
        if (spectralBitsGroup_) {
            spectralBitsGroup_->setAlphaValue(isBitcrush ? 1.0f : 0.35f);
            spectralBitsGroup_->setMouseEnabled(isBitcrush);
        }
    }
    // Push mixer parameter changes to XYMorphPad
    if (xyMorphPad_ && !modulatedMorphXPtr_) {
        if (tag == kMixerPositionId) {
            xyMorphPad_->setMorphPosition(
                static_cast<float>(value), xyMorphPad_->getMorphY());
        } else if (tag == kMixerTiltId) {
            xyMorphPad_->setMorphPosition(
                xyMorphPad_->getMorphX(), static_cast<float>(value));
        }
    }

    // Push envelope parameter changes to ADSRDisplay instances
    syncAdsrParamToDisplay(tag, value, ampEnvDisplay_,
        kAmpEnvAttackId, kAmpEnvAttackCurveId,
        kAmpEnvBezierEnabledId, kAmpEnvBezierAttackCp1XId);
    syncAdsrParamToDisplay(tag, value, filterEnvDisplay_,
        kFilterEnvAttackId, kFilterEnvAttackCurveId,
        kFilterEnvBezierEnabledId, kFilterEnvBezierAttackCp1XId);
    syncAdsrParamToDisplay(tag, value, modEnvDisplay_,
        kModEnvAttackId, kModEnvAttackCurveId,
        kModEnvBezierEnabledId, kModEnvBezierAttackCp1XId);

    // Push mod matrix parameter changes to ModMatrixGrid and ModRingIndicators
    if (tag >= kModMatrixBaseId && tag <= kModMatrixDetailEndId) {
        if (modMatrixGrid_ && !suppressModMatrixSync_) {
            syncModMatrixGrid();
        }
        rebuildRingIndicators();
    }

    // Sync destination knob value to ModRingIndicator base value
    for (int i = 0; i < kMaxRingIndicators; ++i) {
        if (ringIndicators_[static_cast<size_t>(i)] &&
            kVoiceDestParamIds[static_cast<size_t>(i)] == tag) {
            ringIndicators_[static_cast<size_t>(i)]->setBaseValue(
                static_cast<float>(value));
            break;
        }
    }

    return result;
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
                frameSize, presetManager_.get());
            frame->addView(savePresetDialogView_);
        }
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



VSTGUI::CView* Controller::verifyView(
    VSTGUI::CView* view,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* /*editor*/) {

    // Register as sub-listener for action buttons (transforms, Euclidean regen)
    auto* control = dynamic_cast<VSTGUI::CControl*>(view);
    if (control) {
        auto tag = control->getTag();
        if (tag >= static_cast<int32_t>(kActionTransformInvertTag) &&
            tag <= static_cast<int32_t>(kActionEuclideanRegenTag)) {
            control->registerControlListener(this);
        }
    }

    // Populate the pattern preset dropdown (identified by custom-id, no control-tag)
    if (const auto* customId = attributes.getAttributeValue("custom-id")) {
        if (*customId == "preset-dropdown") {
            if (auto* menu = dynamic_cast<VSTGUI::COptionMenu*>(view)) {
                menu->addEntry("All On");
                menu->addEntry("All Off");
                menu->addEntry("Alternate");
                menu->addEntry("Ramp Up");
                menu->addEntry("Ramp Down");
                menu->addEntry("Random");
                menu->registerControlListener(this);
                presetDropdown_ = menu;
            }
        }
    }

    // Wire StepPatternEditor callbacks
    auto* spe = dynamic_cast<Krate::Plugins::StepPatternEditor*>(view);
    if (spe) {
        stepPatternEditor_ = spe;
        spe->setStepLevelBaseParamId(kTranceGateStepLevel0Id);

        spe->setParameterCallback(
            [this](uint32_t paramId, float normalizedValue) {
                setParamNormalized(paramId, static_cast<double>(normalizedValue));
                performEdit(paramId, static_cast<double>(normalizedValue));
            });
        spe->setBeginEditCallback(
            [this](uint32_t paramId) { beginEdit(paramId); });
        spe->setEndEditCallback(
            [this](uint32_t paramId) { endEdit(paramId); });

        // Sync current parameter values
        for (int i = 0; i < 32; ++i) {
            auto paramId = static_cast<Steinberg::Vst::ParamID>(
                kTranceGateStepLevel0Id + i);
            auto* paramObj = getParameterObject(paramId);
            if (paramObj) {
                spe->setStepLevel(i,
                    static_cast<float>(paramObj->getNormalized()));
            }
        }

        auto* numStepsParam = getParameterObject(kTranceGateNumStepsId);
        if (numStepsParam) {
            double val = numStepsParam->getNormalized();
            int steps = std::clamp(
                static_cast<int>(2.0 + std::round(val * 30.0)), 2, 32);
            spe->setNumSteps(steps);
        }

        auto* euclEnabledParam = getParameterObject(kTranceGateEuclideanEnabledId);
        if (euclEnabledParam) {
            spe->setEuclideanEnabled(euclEnabledParam->getNormalized() >= 0.5);
        }
        auto* euclHitsParam = getParameterObject(kTranceGateEuclideanHitsId);
        if (euclHitsParam) {
            int hits = std::clamp(
                static_cast<int>(std::round(euclHitsParam->getNormalized() * 32.0)), 0, 32);
            spe->setEuclideanHits(hits);
        }
        auto* euclRotParam = getParameterObject(kTranceGateEuclideanRotationId);
        if (euclRotParam) {
            int rot = std::clamp(
                static_cast<int>(std::round(euclRotParam->getNormalized() * 31.0)), 0, 31);
            spe->setEuclideanRotation(rot);
        }

        auto* phaseParam = getParameterObject(kTranceGatePhaseOffsetId);
        if (phaseParam) {
            spe->setPhaseOffset(
                static_cast<float>(phaseParam->getNormalized()));
        }
    }

    // Wire ArpLaneContainer and construct arp lanes (079-layout-framework)
    auto* arpContainer = dynamic_cast<Krate::Plugins::ArpLaneContainer*>(view);
    if (arpContainer) {
        arpLaneContainer_ = arpContainer;

        // Helper: wire standard callbacks for a bar-type lane
        auto wireLaneCallbacks = [this](Krate::Plugins::ArpLaneEditor* lane) {
            lane->setParameterCallback(
                [this](uint32_t paramId, float normalizedValue) {
                    setParamNormalized(paramId, static_cast<double>(normalizedValue));
                    performEdit(paramId, static_cast<double>(normalizedValue));
                });
            lane->setBeginEditCallback(
                [this](uint32_t paramId) { beginEdit(paramId); });
            lane->setEndEditCallback(
                [this](uint32_t paramId) { endEdit(paramId); });
            lane->setLengthParamCallback(
                [this](uint32_t paramId, float normalizedValue) {
                    beginEdit(paramId);
                    setParamNormalized(paramId, static_cast<double>(normalizedValue));
                    performEdit(paramId, static_cast<double>(normalizedValue));
                    endEdit(paramId);
                });
        };

        // Helper: sync bar lane steps from current parameters
        auto syncBarLane = [this](Krate::Plugins::ArpLaneEditor* lane,
                                   uint32_t stepBaseId, uint32_t lengthId) {
            for (int i = 0; i < 32; ++i) {
                auto paramId = static_cast<Steinberg::Vst::ParamID>(stepBaseId + i);
                auto* paramObj = getParameterObject(paramId);
                if (paramObj) {
                    lane->setStepLevel(i,
                        static_cast<float>(paramObj->getNormalized()));
                }
            }
            auto* lenParam = getParameterObject(lengthId);
            if (lenParam) {
                double val = lenParam->getNormalized();
                int steps = std::clamp(
                    static_cast<int>(1.0 + std::round(val * 31.0)), 1, 32);
                lane->setNumSteps(steps);
            }
        };

        // Construct velocity lane
        velocityLane_ = new Krate::Plugins::ArpLaneEditor(
            VSTGUI::CRect(0, 0, 500, 105), nullptr, -1);
        velocityLane_->setLaneName("VEL");
        velocityLane_->setLaneType(Krate::Plugins::ArpLaneType::kVelocity);
        velocityLane_->setAccentColor(VSTGUI::CColor{208, 132, 92, 255});
        velocityLane_->setDisplayRange(0.0f, 1.0f, "1.0", "0.0");
        velocityLane_->setStepLevelBaseParamId(kArpVelocityLaneStep0Id);
        velocityLane_->setLengthParamId(kArpVelocityLaneLengthId);
        velocityLane_->setPlayheadParamId(kArpVelocityPlayheadId);
        wireLaneCallbacks(velocityLane_);
        syncBarLane(velocityLane_, kArpVelocityLaneStep0Id, kArpVelocityLaneLengthId);
        arpLaneContainer_->addLane(velocityLane_);

        // Construct gate lane
        gateLane_ = new Krate::Plugins::ArpLaneEditor(
            VSTGUI::CRect(0, 0, 500, 105), nullptr, -1);
        gateLane_->setLaneName("GATE");
        gateLane_->setLaneType(Krate::Plugins::ArpLaneType::kGate);
        gateLane_->setAccentColor(VSTGUI::CColor{200, 164, 100, 255});
        gateLane_->setDisplayRange(0.0f, 2.0f, "200%", "0%");
        gateLane_->setStepLevelBaseParamId(kArpGateLaneStep0Id);
        gateLane_->setLengthParamId(kArpGateLaneLengthId);
        gateLane_->setPlayheadParamId(kArpGatePlayheadId);
        wireLaneCallbacks(gateLane_);
        syncBarLane(gateLane_, kArpGateLaneStep0Id, kArpGateLaneLengthId);
        arpLaneContainer_->addLane(gateLane_);

        // Construct pitch lane
        pitchLane_ = new Krate::Plugins::ArpLaneEditor(
            VSTGUI::CRect(0, 0, 500, 105), nullptr, -1);
        pitchLane_->setLaneName("PITCH");
        pitchLane_->setLaneType(Krate::Plugins::ArpLaneType::kPitch);
        pitchLane_->setAccentColor(VSTGUI::CColor{108, 168, 160, 255});
        pitchLane_->setDisplayRange(-24.0f, 24.0f, "+24", "-24");
        pitchLane_->setStepLevelBaseParamId(kArpPitchLaneStep0Id);
        pitchLane_->setLengthParamId(kArpPitchLaneLengthId);
        pitchLane_->setPlayheadParamId(kArpPitchPlayheadId);
        wireLaneCallbacks(pitchLane_);
        syncBarLane(pitchLane_, kArpPitchLaneStep0Id, kArpPitchLaneLengthId);

        // Sync scale type for popup suffix (084-arp-scale-mode FR-018)
        {
            auto* scaleParam = getParameterObject(kArpScaleTypeId);
            if (scaleParam) {
                double scaleNorm = scaleParam->getNormalized();
                int uiIndex = std::clamp(
                    static_cast<int>(scaleNorm * (kArpScaleTypeCount - 1) + 0.5),
                    0, kArpScaleTypeCount - 1);
                int enumValue = kArpScaleDisplayOrder[static_cast<size_t>(uiIndex)];
                pitchLane_->setScaleType(enumValue);
            }
        }
        arpLaneContainer_->addLane(pitchLane_);

        // Construct ratchet lane
        ratchetLane_ = new Krate::Plugins::ArpLaneEditor(
            VSTGUI::CRect(0, 0, 500, 105), nullptr, -1);
        ratchetLane_->setLaneName("RATCH");
        ratchetLane_->setLaneType(Krate::Plugins::ArpLaneType::kRatchet);
        ratchetLane_->setAccentColor(VSTGUI::CColor{152, 128, 176, 255});
        ratchetLane_->setDisplayRange(1.0f, 4.0f, "4", "1");
        ratchetLane_->setStepLevelBaseParamId(kArpRatchetLaneStep0Id);
        ratchetLane_->setLengthParamId(kArpRatchetLaneLengthId);
        ratchetLane_->setPlayheadParamId(kArpRatchetPlayheadId);
        wireLaneCallbacks(ratchetLane_);
        syncBarLane(ratchetLane_, kArpRatchetLaneStep0Id, kArpRatchetLaneLengthId);
        arpLaneContainer_->addLane(ratchetLane_);

        // Construct modifier lane
        modifierLane_ = new Krate::Plugins::ArpModifierLane(
            VSTGUI::CRect(0, 0, 500, 79), nullptr, -1);
        modifierLane_->setLaneName("MOD");
        modifierLane_->setAccentColor(VSTGUI::CColor{192, 112, 124, 255});
        modifierLane_->setStepFlagBaseParamId(kArpModifierLaneStep0Id);
        modifierLane_->setLengthParamId(kArpModifierLaneLengthId);
        modifierLane_->setPlayheadParamId(kArpModifierPlayheadId);

        modifierLane_->setParameterCallback(
            [this](uint32_t paramId, float normalizedValue) {
                setParamNormalized(paramId, static_cast<double>(normalizedValue));
                performEdit(paramId, static_cast<double>(normalizedValue));
            });
        modifierLane_->setBeginEditCallback(
            [this](uint32_t paramId) { beginEdit(paramId); });
        modifierLane_->setEndEditCallback(
            [this](uint32_t paramId) { endEdit(paramId); });
        modifierLane_->setLengthParamCallback(
            [this](uint32_t paramId, float normalizedValue) {
                beginEdit(paramId);
                setParamNormalized(paramId, static_cast<double>(normalizedValue));
                performEdit(paramId, static_cast<double>(normalizedValue));
                endEdit(paramId);
            });

        for (int i = 0; i < 32; ++i) {
            auto paramId = static_cast<Steinberg::Vst::ParamID>(
                kArpModifierLaneStep0Id + i);
            auto* paramObj = getParameterObject(paramId);
            if (paramObj) {
                float normalized = static_cast<float>(paramObj->getNormalized());
                auto flags = static_cast<uint8_t>(
                    std::clamp(static_cast<int>(std::round(normalized * 255.0f)), 0, 255));
                modifierLane_->setStepFlags(i, flags);
            }
        }
        auto* modLenParam = getParameterObject(kArpModifierLaneLengthId);
        if (modLenParam) {
            double val = modLenParam->getNormalized();
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(val * 31.0)), 1, 32);
            modifierLane_->setNumSteps(steps);
        }
        arpLaneContainer_->addLane(modifierLane_);

        // Construct condition lane
        conditionLane_ = new Krate::Plugins::ArpConditionLane(
            VSTGUI::CRect(0, 0, 500, 63), nullptr, -1);
        conditionLane_->setLaneName("COND");
        conditionLane_->setAccentColor(VSTGUI::CColor{124, 144, 176, 255});
        conditionLane_->setStepConditionBaseParamId(kArpConditionLaneStep0Id);
        conditionLane_->setLengthParamId(kArpConditionLaneLengthId);
        conditionLane_->setPlayheadParamId(kArpConditionPlayheadId);

        conditionLane_->setParameterCallback(
            [this](uint32_t paramId, float normalizedValue) {
                setParamNormalized(paramId, static_cast<double>(normalizedValue));
                performEdit(paramId, static_cast<double>(normalizedValue));
            });
        conditionLane_->setBeginEditCallback(
            [this](uint32_t paramId) { beginEdit(paramId); });
        conditionLane_->setEndEditCallback(
            [this](uint32_t paramId) { endEdit(paramId); });
        conditionLane_->setLengthParamCallback(
            [this](uint32_t paramId, float normalizedValue) {
                beginEdit(paramId);
                setParamNormalized(paramId, static_cast<double>(normalizedValue));
                performEdit(paramId, static_cast<double>(normalizedValue));
                endEdit(paramId);
            });

        for (int i = 0; i < 32; ++i) {
            auto paramId = static_cast<Steinberg::Vst::ParamID>(
                kArpConditionLaneStep0Id + i);
            auto* paramObj = getParameterObject(paramId);
            if (paramObj) {
                float normalized = static_cast<float>(paramObj->getNormalized());
                auto condIndex = static_cast<uint8_t>(
                    std::clamp(static_cast<int>(std::round(normalized * 17.0f)), 0, 17));
                conditionLane_->setStepCondition(i, condIndex);
            }
        }
        auto* condLenParam = getParameterObject(kArpConditionLaneLengthId);
        if (condLenParam) {
            double val = condLenParam->getNormalized();
            int steps = std::clamp(
                static_cast<int>(1.0 + std::round(val * 31.0)), 1, 32);
            conditionLane_->setNumSteps(steps);
        }
        arpLaneContainer_->addLane(conditionLane_);

        // Wire transform callbacks for all 6 lanes (081-interaction-polish, T049)
        auto wireBarLaneTransform = [this](
            Krate::Plugins::ArpLaneEditor* lane, uint32_t stepBaseParamId) {
            if (!lane) return;
            lane->setTransformCallback(
                [this, lane, stepBaseParamId](int transformType) {
                    auto type = static_cast<Krate::Plugins::TransformType>(transformType);
                    auto newValues = lane->computeTransform(type);
                    int32_t len = lane->getActiveLength();
                    for (int32_t i = 0; i < len; ++i) {
                        uint32_t paramId = stepBaseParamId + static_cast<uint32_t>(i);
                        beginEdit(paramId);
                        performEdit(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        setParamNormalized(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        endEdit(paramId);
                    }
                    for (int32_t i = 0; i < len; ++i) {
                        lane->setNormalizedStepValue(i, newValues[static_cast<size_t>(i)]);
                    }
                    lane->setDirty(true);
                });
        };

        wireBarLaneTransform(velocityLane_, kArpVelocityLaneStep0Id);
        wireBarLaneTransform(gateLane_, kArpGateLaneStep0Id);
        wireBarLaneTransform(pitchLane_, kArpPitchLaneStep0Id);
        wireBarLaneTransform(ratchetLane_, kArpRatchetLaneStep0Id);

        if (modifierLane_) {
            modifierLane_->setTransformCallback(
                [this](int transformType) {
                    auto type = static_cast<Krate::Plugins::TransformType>(transformType);
                    auto newValues = modifierLane_->computeTransform(type);
                    int32_t len = modifierLane_->getActiveLength();
                    for (int32_t i = 0; i < len; ++i) {
                        uint32_t paramId = kArpModifierLaneStep0Id +
                            static_cast<uint32_t>(i);
                        beginEdit(paramId);
                        performEdit(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        setParamNormalized(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        endEdit(paramId);
                    }
                    for (int32_t i = 0; i < len; ++i) {
                        modifierLane_->setNormalizedStepValue(i,
                            newValues[static_cast<size_t>(i)]);
                    }
                    modifierLane_->setDirty(true);
                });
        }

        if (conditionLane_) {
            conditionLane_->setTransformCallback(
                [this](int transformType) {
                    auto type = static_cast<Krate::Plugins::TransformType>(transformType);
                    auto newValues = conditionLane_->computeTransform(type);
                    int32_t len = conditionLane_->getActiveLength();
                    for (int32_t i = 0; i < len; ++i) {
                        uint32_t paramId = kArpConditionLaneStep0Id +
                            static_cast<uint32_t>(i);
                        beginEdit(paramId);
                        performEdit(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        setParamNormalized(paramId, static_cast<double>(newValues[static_cast<size_t>(i)]));
                        endEdit(paramId);
                    }
                    for (int32_t i = 0; i < len; ++i) {
                        conditionLane_->setNormalizedStepValue(i,
                            newValues[static_cast<size_t>(i)]);
                    }
                    conditionLane_->setDirty(true);
                });
        }

        wireCopyPasteCallbacks();
    }

    // Wire XYMorphPad callbacks
    auto* xyPad = dynamic_cast<Krate::Plugins::XYMorphPad*>(view);
    if (xyPad) {
        xyMorphPad_ = xyPad;
        xyPad->setController(this);
        xyPad->setSecondaryParamId(kMixerTiltId);

        auto* posParam = getParameterObject(kMixerPositionId);
        auto* tiltParam = getParameterObject(kMixerTiltId);
        float initX = posParam
            ? static_cast<float>(posParam->getNormalized()) : 0.5f;
        float initY = tiltParam
            ? static_cast<float>(tiltParam->getNormalized()) : 0.5f;
        xyPad->setMorphPosition(initX, initY);
    }

    // Wire ADSRDisplay callbacks
    auto* adsrDisplay = dynamic_cast<Krate::Plugins::ADSRDisplay*>(view);
    if (adsrDisplay) {
        wireAdsrDisplay(adsrDisplay);
    }

    // Wire ModMatrixGrid callbacks (T047, T048, T049)
    auto* modGrid = dynamic_cast<Krate::Plugins::ModMatrixGrid*>(view);
    if (modGrid) {
        wireModMatrixGrid(modGrid);
    }

    // Wire ModRingIndicator overlays (T069)
    auto* ringIndicator = dynamic_cast<Krate::Plugins::ModRingIndicator*>(view);
    if (ringIndicator) {
        wireModRingIndicator(ringIndicator);
    }

    // Wire ModHeatmap cell click callback (T155)
    auto* heatmap = dynamic_cast<Krate::Plugins::ModHeatmap*>(view);
    if (heatmap) {
        heatmap->setCellClickCallback(
            [this](int sourceIndex, int destIndex) {
                selectModulationRoute(sourceIndex, destIndex);
            });
        if (modMatrixGrid_) {
            modMatrixGrid_->setHeatmap(heatmap);
        }
    }

    // Wire CategoryTabBar selection callback (T075)
    auto* tabBar = dynamic_cast<Krate::Plugins::CategoryTabBar*>(view);
    if (tabBar) {
        tabBar->setSelectionCallback([this](int tab) {
            if (modMatrixGrid_) {
                modMatrixGrid_->setActiveTab(tab);
            }
        });
    }

    // PW knob visual disable (068-osc-type-params FR-016)
    {
        const auto* viewName = attributes.getAttributeValue("custom-view-name");
        if (viewName) {
            if (*viewName == "OscAPWKnob") {
                oscAPWKnob_ = view;
                auto* wfParam = getParameterObject(kOscAWaveformId);
                if (wfParam) {
                    int wf = static_cast<int>(wfParam->getNormalized() * 4.0 + 0.5);
                    view->setAlphaValue(wf == 3 ? 1.0f : 0.3f);
                }
            } else if (*viewName == "OscBPWKnob") {
                oscBPWKnob_ = view;
                auto* wfParam = getParameterObject(kOscBWaveformId);
                if (wfParam) {
                    int wf = static_cast<int>(wfParam->getNormalized() * 4.0 + 0.5);
                    view->setAlphaValue(wf == 3 ? 1.0f : 0.3f);
                }
            } else if (*viewName == "SpectralCurveDropdown") {
                spectralCurveDropdown_ = view;
                auto* modeParam = getParameterObject(kDistortionSpectralModeId);
                int mode = 0;
                if (modeParam) {
                    mode = std::clamp(
                        static_cast<int>(modeParam->getNormalized() * (kSpectralModeCount - 1) + 0.5),
                        0, kSpectralModeCount - 1);
                }
                bool isBitcrush = (mode == 3);
                view->setAlphaValue(isBitcrush ? 0.35f : 1.0f);
                view->setMouseEnabled(!isBitcrush);
            }
        }
    }

    // Wire named containers by custom-view-name
    auto* container = dynamic_cast<VSTGUI::CViewContainer*>(view);
    if (container) {
        const auto* name = attributes.getAttributeValue("custom-view-name");
        if (name) {
            if (*name == "HarmonizerVoice1") { harmonizerVoiceRows_[0] = container; }
            else if (*name == "HarmonizerVoice2") { harmonizerVoiceRows_[1] = container; }
            else if (*name == "HarmonizerVoice3") { harmonizerVoiceRows_[2] = container; }
            else if (*name == "HarmonizerVoice4") { harmonizerVoiceRows_[3] = container; }
            else if (*name == "LFO1RateGroup") {
                lfo1RateGroup_ = container;
                auto* syncParam = getParameterObject(kLFO1SyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "LFO2RateGroup") {
                lfo2RateGroup_ = container;
                auto* syncParam = getParameterObject(kLFO2SyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "LFO1NoteValueGroup") {
                lfo1NoteValueGroup_ = container;
                auto* syncParam = getParameterObject(kLFO1SyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "LFO2NoteValueGroup") {
                lfo2NoteValueGroup_ = container;
                auto* syncParam = getParameterObject(kLFO2SyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "ChaosRateGroup") {
                chaosRateGroup_ = container;
                auto* syncParam = getParameterObject(kChaosModSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "ChaosNoteValueGroup") {
                chaosNoteValueGroup_ = container;
                auto* syncParam = getParameterObject(kChaosModSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "SHRateGroup") {
                shRateGroup_ = container;
                auto* syncParam = getParameterObject(kSampleHoldSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "SHNoteValueGroup") {
                shNoteValueGroup_ = container;
                auto* syncParam = getParameterObject(kSampleHoldSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "RandomRateGroup") {
                randomRateGroup_ = container;
                auto* syncParam = getParameterObject(kRandomSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "RandomNoteValueGroup") {
                randomNoteValueGroup_ = container;
                auto* syncParam = getParameterObject(kRandomSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "DelayTimeGroup") {
                delayTimeGroup_ = container;
                auto* syncParam = getParameterObject(kDelaySyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "DelayNoteValueGroup") {
                delayNoteValueGroup_ = container;
                auto* syncParam = getParameterObject(kDelaySyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "PhaserRateGroup") {
                phaserRateGroup_ = container;
                auto* syncParam = getParameterObject(kPhaserSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "PhaserNoteValueGroup") {
                phaserNoteValueGroup_ = container;
                auto* syncParam = getParameterObject(kPhaserSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "TranceGateRateGroup") {
                tranceGateRateGroup_ = container;
                auto* syncParam = getParameterObject(kTranceGateTempoSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "TranceGateNoteValueGroup") {
                tranceGateNoteValueGroup_ = container;
                auto* syncParam = getParameterObject(kTranceGateTempoSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "ArpRateGroup") {
                arpRateGroup_ = container;
                auto* syncParam = getParameterObject(kArpTempoSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "ArpNoteValueGroup") {
                arpNoteValueGroup_ = container;
                auto* syncParam = getParameterObject(kArpTempoSyncId);
                bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
                container->setVisible(syncOn);
            } else if (*name == "EuclideanControlsGroup") {
                euclideanControlsGroup_ = container;
                auto* param = getParameterObject(kTranceGateEuclideanEnabledId);
                bool enabled = (param != nullptr) && param->getNormalized() >= 0.5;
                container->setVisible(enabled);
            } else if (*name == "PolyGroup") {
                polyGroup_ = container;
                auto* voiceModeParam = getParameterObject(kVoiceModeId);
                bool isMono = (voiceModeParam != nullptr) && voiceModeParam->getNormalized() >= 0.5;
                container->setVisible(!isMono);
            } else if (*name == "MonoGroup") {
                monoGroup_ = container;
                auto* voiceModeParam = getParameterObject(kVoiceModeId);
                bool isMono = (voiceModeParam != nullptr) && voiceModeParam->getNormalized() >= 0.5;
                container->setVisible(isMono);
            } else if (*name == "SettingsDrawer") {
                settingsDrawer_ = container;
            } else if (*name == "ArpEuclideanGroup") {
                arpEuclideanGroup_ = container;
                auto* param = getParameterObject(kArpEuclideanEnabledId);
                bool enabled = (param != nullptr) && param->getNormalized() >= 0.5;
                container->setVisible(enabled);
            } else if (*name == "ArpRootNoteGroup") {
                arpRootNoteGroup_ = container;
                auto* scaleParam = getParameterObject(kArpScaleTypeId);
                bool isChromaticInit = (scaleParam == nullptr) || scaleParam->getNormalized() < 0.01;
                container->setAlphaValue(isChromaticInit ? 0.35f : 1.0f);
                container->setMouseEnabled(!isChromaticInit);
            } else if (*name == "ArpQuantizeInputGroup") {
                arpQuantizeInputGroup_ = container;
                auto* scaleParam = getParameterObject(kArpScaleTypeId);
                bool isChromaticInit = (scaleParam == nullptr) || scaleParam->getNormalized() < 0.01;
                container->setAlphaValue(isChromaticInit ? 0.35f : 1.0f);
                container->setMouseEnabled(!isChromaticInit);
            } else if (*name == "SpectralBitsGroup") {
                spectralBitsGroup_ = container;
                auto* modeParam = getParameterObject(kDistortionSpectralModeId);
                int mode = 0;
                if (modeParam) {
                    mode = std::clamp(
                        static_cast<int>(modeParam->getNormalized() * (kSpectralModeCount - 1) + 0.5),
                        0, kSpectralModeCount - 1);
                }
                bool isBitcrush = (mode == 3);
                container->setAlphaValue(isBitcrush ? 1.0f : 0.35f);
                container->setMouseEnabled(isBitcrush);
            }
        }
    }

    // Settings drawer: capture gear button and register as listener
    auto* ctrl = dynamic_cast<VSTGUI::CControl*>(view);
    if (ctrl) {
        auto tag = ctrl->getTag();
        if (tag == static_cast<int32_t>(kActionSettingsToggleTag)) {
            gearButton_ = ctrl;
            ctrl->registerControlListener(this);
        }
        if (tag == static_cast<int32_t>(kActionSettingsOverlayTag)) {
            settingsOverlay_ = view;
            view->setVisible(false);
            ctrl->registerControlListener(this);
        }
        if (tag == static_cast<int32_t>(kArpDiceTriggerId)) {
            diceButton_ = ctrl;
            ctrl->registerControlListener(this);
        }
    }

    // Wire EuclideanDotDisplay
    auto* eucDotDisplay = dynamic_cast<Krate::Plugins::EuclideanDotDisplay*>(view);
    if (eucDotDisplay) {
        euclideanDotDisplay_ = eucDotDisplay;

        auto* hitsParam = getParameterObject(kArpEuclideanHitsId);
        if (hitsParam) {
            int hits = std::clamp(
                static_cast<int>(std::round(hitsParam->getNormalized() * 32.0)),
                0, 32);
            eucDotDisplay->setHits(hits);
        }
        auto* stepsParam = getParameterObject(kArpEuclideanStepsId);
        if (stepsParam) {
            int steps = std::clamp(
                static_cast<int>(2.0 + std::round(stepsParam->getNormalized() * 30.0)),
                2, 32);
            eucDotDisplay->setSteps(steps);
        }
        auto* rotParam = getParameterObject(kArpEuclideanRotationId);
        if (rotParam) {
            int rot = std::clamp(
                static_cast<int>(std::round(rotParam->getNormalized() * 31.0)),
                0, 31);
            eucDotDisplay->setRotation(rot);
        }
    }

    return view;
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
// Batch View Sync (bulk parameter load guard)
// ==============================================================================
// Called once after setComponentState() or loadComponentStateWithNotify() to
// sync all custom views from current parameter state. Replaces thousands of
// per-param invalidRect() calls with a single full-frame repaint.

void Controller::syncAllViews() {
    // Helper: read a normalized param value, return 0.0 if not found
    auto paramNorm = [this](Steinberg::Vst::ParamID id) -> double {
        auto* p = getParameterObject(id);
        return p ? p->getNormalized() : 0.0;
    };
    // Helper: read normalized param → clamped int
    auto paramInt = [&](Steinberg::Vst::ParamID id, double scale,
                        double offset, int lo, int hi) -> int {
        return std::clamp(
            static_cast<int>(offset + std::round(paramNorm(id) * scale)),
            lo, hi);
    };

    // ---- StepPatternEditor (trance gate) ----
    if (stepPatternEditor_) {
        for (int i = 0; i < 32; ++i) {
            auto stepVal = static_cast<float>(
                paramNorm(kTranceGateStepLevel0Id + static_cast<uint32_t>(i)));
            stepPatternEditor_->setStepLevel(i, stepVal);
        }
        int numSteps = paramInt(kTranceGateNumStepsId, 30.0, 2.0, 2, 32);
        stepPatternEditor_->setNumSteps(numSteps);
        bool eucEnabled = paramNorm(kTranceGateEuclideanEnabledId) >= 0.5;
        stepPatternEditor_->setEuclideanEnabled(eucEnabled);
        int eucHits = paramInt(kTranceGateEuclideanHitsId, 32.0, 0.0, 0, 32);
        stepPatternEditor_->setEuclideanHits(eucHits);
        int eucRot = paramInt(kTranceGateEuclideanRotationId, 31.0, 0.0, 0, 31);
        stepPatternEditor_->setEuclideanRotation(eucRot);
        stepPatternEditor_->setPhaseOffset(
            static_cast<float>(paramNorm(kTranceGatePhaseOffsetId)));

        if (euclideanControlsGroup_)
            euclideanControlsGroup_->setVisible(eucEnabled);
    }

    // ---- Arp lane editors (velocity, gate, pitch, ratchet, modifier, condition) ----
    auto syncBarLane = [&](Krate::Plugins::ArpLaneEditor* lane,
                           Steinberg::Vst::ParamID stepBase,
                           Steinberg::Vst::ParamID lengthId) {
        if (!lane) return;
        for (int i = 0; i < 32; ++i) {
            lane->setStepLevel(i, static_cast<float>(
                paramNorm(stepBase + static_cast<uint32_t>(i))));
        }
        int steps = paramInt(lengthId, 31.0, 1.0, 1, 32);
        lane->setNumSteps(steps);
        lane->setDirty(true);
    };
    syncBarLane(velocityLane_, kArpVelocityLaneStep0Id, kArpVelocityLaneLengthId);
    syncBarLane(gateLane_, kArpGateLaneStep0Id, kArpGateLaneLengthId);
    syncBarLane(pitchLane_, kArpPitchLaneStep0Id, kArpPitchLaneLengthId);
    syncBarLane(ratchetLane_, kArpRatchetLaneStep0Id, kArpRatchetLaneLengthId);

    // Modifier lane (flags, not float levels)
    if (modifierLane_) {
        for (int i = 0; i < 32; ++i) {
            auto flags = static_cast<uint8_t>(std::clamp(
                static_cast<int>(std::round(
                    paramNorm(kArpModifierLaneStep0Id + static_cast<uint32_t>(i)) * 255.0)),
                0, 255));
            modifierLane_->setStepFlags(i, flags);
        }
        int steps = paramInt(kArpModifierLaneLengthId, 31.0, 1.0, 1, 32);
        modifierLane_->setNumSteps(steps);
        modifierLane_->setDirty(true);
    }

    // Condition lane (condition indices)
    if (conditionLane_) {
        for (int i = 0; i < 32; ++i) {
            auto condIdx = static_cast<uint8_t>(std::clamp(
                static_cast<int>(std::round(
                    paramNorm(kArpConditionLaneStep0Id + static_cast<uint32_t>(i)) * 17.0)),
                0, 17));
            conditionLane_->setStepCondition(i, condIdx);
        }
        int steps = paramInt(kArpConditionLaneLengthId, 31.0, 1.0, 1, 32);
        conditionLane_->setNumSteps(steps);
        conditionLane_->setDirty(true);
    }

    // ---- Arp Euclidean overlay + dot display ----
    {
        int arpEucHits = paramInt(kArpEuclideanHitsId, 32.0, 0.0, 0, 32);
        int arpEucSteps = paramInt(kArpEuclideanStepsId, 30.0, 2.0, 2, 32);
        int arpEucRot = paramInt(kArpEuclideanRotationId, 31.0, 0.0, 0, 31);
        bool arpEucEnabled = paramNorm(kArpEuclideanEnabledId) >= 0.5;

        if (euclideanDotDisplay_) {
            euclideanDotDisplay_->setSteps(arpEucSteps);
            euclideanDotDisplay_->setHits(arpEucHits);
            euclideanDotDisplay_->setRotation(arpEucRot);
        }

        Krate::Plugins::IArpLane* lanes[] = {
            velocityLane_, gateLane_, pitchLane_, ratchetLane_,
            modifierLane_, conditionLane_};
        for (auto* lane : lanes) {
            if (lane) lane->setEuclideanOverlay(arpEucHits, arpEucSteps, arpEucRot, arpEucEnabled);
        }

        if (arpEuclideanGroup_)
            arpEuclideanGroup_->setVisible(arpEucEnabled);
    }

    // ---- Visibility toggles (sync groups) ----
    auto syncVisGroup = [&](Steinberg::Vst::ParamID syncId,
                            VSTGUI::CView* freeGroup,
                            VSTGUI::CView* syncGroup) {
        double v = paramNorm(syncId);
        if (freeGroup) freeGroup->setVisible(v < 0.5);
        if (syncGroup) syncGroup->setVisible(v >= 0.5);
    };
    syncVisGroup(kLFO1SyncId, lfo1RateGroup_, lfo1NoteValueGroup_);
    syncVisGroup(kLFO2SyncId, lfo2RateGroup_, lfo2NoteValueGroup_);
    syncVisGroup(kChaosModSyncId, chaosRateGroup_, chaosNoteValueGroup_);
    syncVisGroup(kSampleHoldSyncId, shRateGroup_, shNoteValueGroup_);
    syncVisGroup(kRandomSyncId, randomRateGroup_, randomNoteValueGroup_);
    syncVisGroup(kDelaySyncId, delayTimeGroup_, delayNoteValueGroup_);
    syncVisGroup(kPhaserSyncId, phaserRateGroup_, phaserNoteValueGroup_);
    syncVisGroup(kTranceGateTempoSyncId, tranceGateRateGroup_, tranceGateNoteValueGroup_);
    syncVisGroup(kArpTempoSyncId, arpRateGroup_, arpNoteValueGroup_);

    // Poly/Mono
    {
        double vm = paramNorm(kVoiceModeId);
        if (polyGroup_) polyGroup_->setVisible(vm < 0.5);
        if (monoGroup_) monoGroup_->setVisible(vm >= 0.5);
    }

    // ---- Harmonizer voice row dimming ----
    {
        double hv = paramNorm(kHarmonizerNumVoicesId);
        int numVoices = static_cast<int>(hv * (kHarmonizerNumVoicesCount - 1) + 0.5) + 1;
        for (int i = 0; i < 4; ++i) {
            if (harmonizerVoiceRows_[static_cast<size_t>(i)]) {
                harmonizerVoiceRows_[static_cast<size_t>(i)]->setAlphaValue(
                    i < numVoices ? 1.0f : 0.3f);
            }
        }
    }

    // ---- Arp Scale Mode dimming ----
    {
        double scaleNorm = paramNorm(kArpScaleTypeId);
        bool isChromatic = (scaleNorm < 0.01);
        float alpha = isChromatic ? 0.35f : 1.0f;
        if (arpRootNoteGroup_) {
            arpRootNoteGroup_->setAlphaValue(alpha);
            arpRootNoteGroup_->setMouseEnabled(!isChromatic);
        }
        if (arpQuantizeInputGroup_) {
            arpQuantizeInputGroup_->setAlphaValue(alpha);
            arpQuantizeInputGroup_->setMouseEnabled(!isChromatic);
        }
        if (pitchLane_) {
            int uiIndex = std::clamp(
                static_cast<int>(scaleNorm * (kArpScaleTypeCount - 1) + 0.5),
                0, kArpScaleTypeCount - 1);
            int enumValue = kArpScaleDisplayOrder[static_cast<size_t>(uiIndex)];
            pitchLane_->setScaleType(enumValue);
        }
    }

    // ---- PW knob dimming ----
    if (oscAPWKnob_) {
        int wf = static_cast<int>(paramNorm(kOscAWaveformId) * 4.0 + 0.5);
        oscAPWKnob_->setAlphaValue(wf == 3 ? 1.0f : 0.3f);
    }
    if (oscBPWKnob_) {
        int wf = static_cast<int>(paramNorm(kOscBWaveformId) * 4.0 + 0.5);
        oscBPWKnob_->setAlphaValue(wf == 3 ? 1.0f : 0.3f);
    }

    // ---- Spectral distortion control dimming ----
    {
        double modeNorm = paramNorm(kDistortionSpectralModeId);
        int mode = std::clamp(
            static_cast<int>(modeNorm * (kSpectralModeCount - 1) + 0.5),
            0, kSpectralModeCount - 1);
        bool isBitcrush = (mode == 3);
        if (spectralCurveDropdown_) {
            spectralCurveDropdown_->setAlphaValue(isBitcrush ? 0.35f : 1.0f);
            spectralCurveDropdown_->setMouseEnabled(!isBitcrush);
        }
        if (spectralBitsGroup_) {
            spectralBitsGroup_->setAlphaValue(isBitcrush ? 1.0f : 0.35f);
            spectralBitsGroup_->setMouseEnabled(isBitcrush);
        }
    }

    // ---- XYMorphPad ----
    if (xyMorphPad_ && !modulatedMorphXPtr_) {
        xyMorphPad_->setMorphPosition(
            static_cast<float>(paramNorm(kMixerPositionId)),
            static_cast<float>(paramNorm(kMixerTiltId)));
    }

    // ---- ADSR Displays ----
    syncAdsrDisplay(ampEnvDisplay_,
        kAmpEnvAttackId, kAmpEnvAttackCurveId,
        kAmpEnvBezierEnabledId, kAmpEnvBezierAttackCp1XId);
    syncAdsrDisplay(filterEnvDisplay_,
        kFilterEnvAttackId, kFilterEnvAttackCurveId,
        kFilterEnvBezierEnabledId, kFilterEnvBezierAttackCp1XId);
    syncAdsrDisplay(modEnvDisplay_,
        kModEnvAttackId, kModEnvAttackCurveId,
        kModEnvBezierEnabledId, kModEnvBezierAttackCp1XId);

    // ---- Mod Matrix Grid + Ring Indicators ----
    if (modMatrixGrid_ && !suppressModMatrixSync_) {
        syncModMatrixGrid();
    }
    rebuildRingIndicators();

    // Sync destination knob base values to ring indicators
    for (int i = 0; i < kMaxRingIndicators; ++i) {
        if (ringIndicators_[static_cast<size_t>(i)]) {
            auto* param = getParameterObject(kVoiceDestParamIds[static_cast<size_t>(i)]);
            if (param) {
                ringIndicators_[static_cast<size_t>(i)]->setBaseValue(
                    static_cast<float>(param->getNormalized()));
            }
        }
    }

    // ---- Single full-frame repaint instead of thousands of invalidRects ----
    if (activeEditor_ && activeEditor_->getFrame()) {
        activeEditor_->getFrame()->invalid();
    }
}

} // namespace Ruinae
