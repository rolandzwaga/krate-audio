// ==============================================================================
// Edit Controller Implementation
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "version.h"
#include "preset/ruinae_preset_config.h"
#include "ui/step_pattern_editor.h"
#include "ui/xy_morph_pad.h"
#include "ui/adsr_display.h"
#include "ui/mod_matrix_grid.h"
#include "ui/mod_ring_indicator.h"
#include "ui/mod_heatmap.h"
#include "ui/category_tab_bar.h"

// Parameter pack headers (for registration, display, and controller sync)
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
#include "parameters/freeze_params.h"
#include "parameters/delay_params.h"
#include "parameters/reverb_params.h"
#include "parameters/mono_mode_params.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"

#include <cstring>

namespace Ruinae {

// State version must match processor
constexpr Steinberg::int32 kControllerStateVersion = 3;

// Maps destination index to the actual VST parameter ID of that knob.
// Tab-dependent: voice tab (0-6) and global tab (0-6) have different mappings.
// Used by ModRingIndicator base value sync (T069-T072).
static constexpr std::array<Steinberg::Vst::ParamID, 8> kVoiceDestParamIds = {{
    kFilterCutoffId,          // 0: Filter Cutoff
    kFilterResonanceId,       // 1: Filter Resonance
    kMixerPositionId,         // 2: Morph Position
    kDistortionDriveId,       // 3: Distortion Drive
    kTranceGateDepthId,       // 4: TranceGate Depth
    kOscATuneId,              // 5: OSC A Pitch
    kOscBTuneId,              // 6: OSC B Pitch
    kMixerTiltId,             // 7: Spectral Tilt
}};

static constexpr std::array<Steinberg::Vst::ParamID, 8> kGlobalDestParamIds = {{
    kGlobalFilterCutoffId,    // 0: Global Filter Cutoff
    kGlobalFilterResonanceId, // 1: Global Filter Resonance
    kMasterGainId,            // 2: Master Volume
    kDelayMixId,              // 3: Effect Mix
    kFilterCutoffId,          // 4: All Voice Filter Cutoff
    kMixerPositionId,         // 5: All Voice Morph Position
    kTranceGateDepthId,       // 6: All Voice TranceGate Rate
    kMixerTiltId,             // 7: All Voice Spectral Tilt
}};

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
    registerModMatrixParams(parameters);
    registerGlobalFilterParams(parameters);
    registerFreezeParams(parameters);
    registerDelayParams(parameters);
    registerReverbParams(parameters);
    registerMonoModeParams(parameters);

    // ==========================================================================
    // Initialize Preset Manager
    // ==========================================================================
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(
        makeRuinaePresetConfig(), nullptr, this);

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::terminate() {
    playbackPollTimer_ = nullptr;
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

    // Read state version (must match Processor::getState format)
    Steinberg::int32 version = 0;
    if (!streamer.readInt32(version)) {
        return Steinberg::kResultTrue; // Empty stream, keep defaults
    }

    // Lambda to sync controller parameter display
    auto setParam = [this](Steinberg::Vst::ParamID id, double value) {
        setParamNormalized(id, value);
    };

    // Helper: load common packs before mod matrix (version-aware mixer loading)
    auto loadCommonPacks = [&](Steinberg::int32 ver) {
        loadGlobalParamsToController(streamer, setParam);
        loadOscAParamsToController(streamer, setParam);
        loadOscBParamsToController(streamer, setParam);
        // v4 added MixerShift field to mixer pack
        if (ver >= 4)
            loadMixerParamsToController(streamer, setParam);
        else
            loadMixerParamsToControllerV3(streamer, setParam);
        // v5 added type-specific filter params (ladder/formant/comb)
        if (ver >= 5)
            loadFilterParamsToControllerV4(streamer, setParam);
        else
            loadFilterParamsToController(streamer, setParam);
        loadDistortionParamsToController(streamer, setParam);
        loadTranceGateParamsToController(streamer, setParam);
        loadAmpEnvParamsToController(streamer, setParam);
        loadFilterEnvParamsToController(streamer, setParam);
        loadModEnvParamsToController(streamer, setParam);
        loadLFO1ParamsToController(streamer, setParam);
        loadLFO2ParamsToController(streamer, setParam);
        loadChaosModParamsToController(streamer, setParam);
    };

    auto loadPostModMatrix = [&]() {
        loadGlobalFilterParamsToController(streamer, setParam);
        loadFreezeParamsToController(streamer, setParam);
        loadDelayParamsToController(streamer, setParam);
        loadReverbParamsToController(streamer, setParam);
        loadMonoModeParamsToController(streamer, setParam);
    };

    if (version == 1) {
        // v1: base mod matrix only (source, dest, amount per slot)
        loadCommonPacks(version);
        loadModMatrixParamsToControllerV1(streamer, setParam);
        loadPostModMatrix();
    } else if (version >= 2) {
        // v2+: extended mod matrix (source, dest, amount, curve, smooth, scale, bypass)
        // v3+ adds voice routes via IMessage from processor (not from stream)
        // v4+ adds MixerShift to mixer pack (handled by loadCommonPacks)
        loadCommonPacks(version);
        loadModMatrixParamsToController(streamer, setParam);
        loadPostModMatrix();
    }
    // Unknown versions (v0 or negative): keep defaults (fail closed)

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
        auto* editor = new VSTGUI::VST3Editor(this, "editor", "editor.uidesc");
        return editor;
    }
    return nullptr;
}

Steinberg::tresult PLUGIN_API Controller::getParamStringByValue(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized,
    Steinberg::Vst::String128 string) {

    // Route to appropriate parameter pack formatter by ID range
    Steinberg::tresult result = Steinberg::kResultFalse;

    if (id <= kGlobalEndId) {
        result = formatGlobalParam(id, valueNormalized, string);
    } else if (id >= kOscABaseId && id <= kOscAEndId) {
        result = formatOscAParam(id, valueNormalized, string);
    } else if (id >= kOscBBaseId && id <= kOscBEndId) {
        result = formatOscBParam(id, valueNormalized, string);
    } else if (id >= kMixerBaseId && id <= kMixerEndId) {
        result = formatMixerParam(id, valueNormalized, string);
    } else if (id >= kFilterBaseId && id <= kFilterEndId) {
        result = formatFilterParam(id, valueNormalized, string);
    } else if (id >= kDistortionBaseId && id <= kDistortionEndId) {
        result = formatDistortionParam(id, valueNormalized, string);
    } else if (id >= kTranceGateBaseId && id <= kTranceGateEndId) {
        result = formatTranceGateParam(id, valueNormalized, string);
    } else if (id >= kAmpEnvBaseId && id <= kAmpEnvEndId) {
        result = formatAmpEnvParam(id, valueNormalized, string);
    } else if (id >= kFilterEnvBaseId && id <= kFilterEnvEndId) {
        result = formatFilterEnvParam(id, valueNormalized, string);
    } else if (id >= kModEnvBaseId && id <= kModEnvEndId) {
        result = formatModEnvParam(id, valueNormalized, string);
    } else if (id >= kLFO1BaseId && id <= kLFO1EndId) {
        result = formatLFO1Param(id, valueNormalized, string);
    } else if (id >= kLFO2BaseId && id <= kLFO2EndId) {
        result = formatLFO2Param(id, valueNormalized, string);
    } else if (id >= kChaosModBaseId && id <= kChaosModEndId) {
        result = formatChaosModParam(id, valueNormalized, string);
    } else if (id >= kModMatrixBaseId && id <= kModMatrixEndId) {
        result = formatModMatrixParam(id, valueNormalized, string);
    } else if (id >= kGlobalFilterBaseId && id <= kGlobalFilterEndId) {
        result = formatGlobalFilterParam(id, valueNormalized, string);
    } else if (id >= kFreezeBaseId && id <= kFreezeEndId) {
        result = formatFreezeParam(id, valueNormalized, string);
    } else if (id >= kDelayBaseId && id <= kDelayEndId) {
        result = formatDelayParam(id, valueNormalized, string);
    } else if (id >= kReverbBaseId && id <= kReverbEndId) {
        result = formatReverbParam(id, valueNormalized, string);
    } else if (id >= kMonoBaseId && id <= kMonoEndId) {
        result = formatMonoModeParam(id, valueNormalized, string);
    }

    // Fall back to default implementation for unhandled parameters
    // (StringListParameter handles its own formatting)
    if (result != Steinberg::kResultOk) {
        return EditControllerEx1::getParamStringByValue(id, valueNormalized, string);
    }
    return result;
}

Steinberg::tresult PLUGIN_API Controller::getParamValueByString(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::TChar* string,
    Steinberg::Vst::ParamValue& valueNormalized) {
    // Use default implementation for now
    return EditControllerEx1::getParamValueByString(id, string, valueNormalized);
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
            // IMessage only supports int64 for pointer transport (VST3 SDK limitation)
            tranceGatePlaybackStepPtr_ = reinterpret_cast<std::atomic<int>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(stepPtr));
        }
        if (attrs->getInt("playingPtr", playingPtr) == Steinberg::kResultOk) {
            isTransportPlayingPtr_ = reinterpret_cast<std::atomic<bool>*>( // NOLINT(performance-no-int-to-ptr)
                static_cast<intptr_t>(playingPtr));
        }

        // Start a poll timer to push playback state to the editor (~30fps)
        if (tranceGatePlaybackStepPtr_ && !playbackPollTimer_) {
            playbackPollTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
                [this](VSTGUI::CVSTGUITimer*) {
                    if (!stepPatternEditor_) return;
                    if (tranceGatePlaybackStepPtr_) {
                        stepPatternEditor_->setPlaybackStep(
                            tranceGatePlaybackStepPtr_->load(std::memory_order_relaxed));
                    }
                    if (isTransportPlayingPtr_) {
                        stepPatternEditor_->setPlaying(
                            isTransportPlayingPtr_->load(std::memory_order_relaxed));
                    }
                }, 33); // ~30fps
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
            if (euclideanRegenButton_)
                euclideanRegenButton_->setVisible(value >= 0.5);
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

    // Toggle LFO Rate knob visibility based on sync state
    if (tag == kLFO1SyncId && lfo1RateGroup_)
        lfo1RateGroup_->setVisible(value < 0.5);
    if (tag == kLFO2SyncId && lfo2RateGroup_)
        lfo2RateGroup_->setVisible(value < 0.5);

    // Push mixer parameter changes to XYMorphPad
    if (xyMorphPad_) {
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
    // Skip sync when the grid itself is the source (reentrancy guard)
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
}

void Controller::willClose(VSTGUI::VST3Editor* editor) {
    if (activeEditor_ == editor) {
        stepPatternEditor_ = nullptr;
        presetDropdown_ = nullptr;
        xyMorphPad_ = nullptr;
        modMatrixGrid_ = nullptr;
        ringIndicators_.fill(nullptr);
        ampEnvDisplay_ = nullptr;
        filterEnvDisplay_ = nullptr;
        modEnvDisplay_ = nullptr;
        euclideanRegenButton_ = nullptr;
        lfo1RateGroup_ = nullptr;
        lfo2RateGroup_ = nullptr;

        // FX detail panel cleanup (T092)
        fxDetailFreeze_ = nullptr;
        fxDetailDelay_ = nullptr;
        fxDetailReverb_ = nullptr;
        expandedFxPanel_ = -1;

        // Envelope expand/collapse cleanup
        envGroupAmp_ = nullptr;
        envGroupFilter_ = nullptr;
        envGroupMod_ = nullptr;
        expandedEnvPanel_ = -1;

        activeEditor_ = nullptr;
    }
}



VSTGUI::CView* Controller::verifyView(
    VSTGUI::CView* view,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* /*editor*/) {

    // Register as sub-listener for action buttons (transforms, FX/env chevrons)
    auto* control = dynamic_cast<VSTGUI::CControl*>(view);
    if (control) {
        auto tag = control->getTag();
        if (tag >= static_cast<int32_t>(kActionTransformInvertTag) && tag <= static_cast<int32_t>(kActionEnvExpandModTag)) {
            control->registerControlListener(this);
        }

        // Track the Euclidean regen button for visibility toggling
        if (tag == static_cast<int32_t>(kActionEuclideanRegenTag)) {
            euclideanRegenButton_ = view;
            // Set initial visibility from current Euclidean enabled state
            auto* param = getParameterObject(kTranceGateEuclideanEnabledId);
            bool enabled = param && param->getNormalized() >= 0.5;
            view->setVisible(enabled);
        }
    }

    // Populate the pattern preset dropdown (identified by custom-id, no control-tag)
    if (auto customId = attributes.getAttributeValue("custom-id")) {
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

        // Configure base parameter ID so editor knows which VST params to use
        spe->setStepLevelBaseParamId(kTranceGateStepLevel0Id);

        // Wire performEdit callback (editor -> host)
        spe->setParameterCallback(
            [this](uint32_t paramId, float normalizedValue) {
                performEdit(paramId, static_cast<double>(normalizedValue));
            });

        // Wire beginEdit/endEdit for gesture management
        spe->setBeginEditCallback(
            [this](uint32_t paramId) {
                beginEdit(paramId);
            });

        spe->setEndEditCallback(
            [this](uint32_t paramId) {
                endEdit(paramId);
            });

        // Sync current parameter values to the editor
        for (int i = 0; i < 32; ++i) {
            auto paramId = static_cast<Steinberg::Vst::ParamID>(
                kTranceGateStepLevel0Id + i);
            auto* paramObj = getParameterObject(paramId);
            if (paramObj) {
                spe->setStepLevel(i,
                    static_cast<float>(paramObj->getNormalized()));
            }
        }

        // Sync numSteps
        auto* numStepsParam = getParameterObject(kTranceGateNumStepsId);
        if (numStepsParam) {
            double val = numStepsParam->getNormalized();
            int steps = std::clamp(
                static_cast<int>(2.0 + std::round(val * 30.0)), 2, 32);
            spe->setNumSteps(steps);
        }

        // Sync Euclidean params
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

        // Sync phase offset
        auto* phaseParam = getParameterObject(kTranceGatePhaseOffsetId);
        if (phaseParam) {
            spe->setPhaseOffset(
                static_cast<float>(phaseParam->getNormalized()));
        }
    }

    // Wire XYMorphPad callbacks
    auto* xyPad = dynamic_cast<Krate::Plugins::XYMorphPad*>(view);
    if (xyPad) {
        xyMorphPad_ = xyPad;
        xyPad->setController(this);
        xyPad->setSecondaryParamId(kMixerTiltId);

        // Sync initial position from current parameter state
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

        // Wire heatmap to ModMatrixGrid if available
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

    // Wire named containers by custom-view-name
    auto* container = dynamic_cast<VSTGUI::CViewContainer*>(view);
    if (container) {
        const auto* name = attributes.getAttributeValue("custom-view-name");
        if (name) {
            // FX detail panels (T089)
            if (*name == "FreezeDetail") {
                fxDetailFreeze_ = container;
                container->setVisible(false);
            } else if (*name == "DelayDetail") {
                fxDetailDelay_ = container;
                container->setVisible(false);
            } else if (*name == "ReverbDetail") {
                fxDetailReverb_ = container;
                container->setVisible(false);
            }
            // LFO Rate groups (hidden when tempo sync is active)
            else if (*name == "LFO1RateGroup") {
                lfo1RateGroup_ = container;
                auto* syncParam = getParameterObject(kLFO1SyncId);
                bool syncOn = syncParam && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            } else if (*name == "LFO2RateGroup") {
                lfo2RateGroup_ = container;
                auto* syncParam = getParameterObject(kLFO2SyncId);
                bool syncOn = syncParam && syncParam->getNormalized() >= 0.5;
                container->setVisible(!syncOn);
            }
            // Envelope expand/collapse groups
            else if (*name == "EnvGroupAmp") {
                envGroupAmp_ = container;
            } else if (*name == "EnvGroupFilter") {
                envGroupFilter_ = container;
            } else if (*name == "EnvGroupMod") {
                envGroupMod_ = container;
            }
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
        case kActionFxExpandFreezeTag:  toggleFxDetail(0); return;
        case kActionFxExpandDelayTag:   toggleFxDetail(1); return;
        case kActionFxExpandReverbTag:  toggleFxDetail(2); return;
        case kActionEnvExpandAmpTag:    toggleEnvExpand(0); return;
        case kActionEnvExpandFilterTag: toggleEnvExpand(1); return;
        case kActionEnvExpandModTag:    toggleEnvExpand(2); return;
        default: break;
    }

    // Pattern preset dropdown (identified by pointer, no control-tag)
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
// ADSRDisplay Wiring
// ==============================================================================

void Controller::wireAdsrDisplay(Krate::Plugins::ADSRDisplay* display) {
    if (!display) return;

    auto tag = display->getTag();

    // Identify which envelope this display belongs to based on control-tag
    Krate::Plugins::ADSRDisplay** displayPtr = nullptr;
    uint32_t adsrBaseId = 0;
    uint32_t curveBaseId = 0;
    uint32_t bezierEnabledId = 0;
    uint32_t bezierBaseId = 0;

    if (tag == kAmpEnvAttackId) {
        displayPtr = &ampEnvDisplay_;
        adsrBaseId = kAmpEnvAttackId;
        curveBaseId = kAmpEnvAttackCurveId;
        bezierEnabledId = kAmpEnvBezierEnabledId;
        bezierBaseId = kAmpEnvBezierAttackCp1XId;
    } else if (tag == kFilterEnvAttackId) {
        displayPtr = &filterEnvDisplay_;
        adsrBaseId = kFilterEnvAttackId;
        curveBaseId = kFilterEnvAttackCurveId;
        bezierEnabledId = kFilterEnvBezierEnabledId;
        bezierBaseId = kFilterEnvBezierAttackCp1XId;
    } else if (tag == kModEnvAttackId) {
        displayPtr = &modEnvDisplay_;
        adsrBaseId = kModEnvAttackId;
        curveBaseId = kModEnvAttackCurveId;
        bezierEnabledId = kModEnvBezierEnabledId;
        bezierBaseId = kModEnvBezierAttackCp1XId;
    } else {
        return; // Unknown tag - not an envelope display
    }

    *displayPtr = display;

    // Configure parameter IDs
    display->setAdsrBaseParamId(adsrBaseId);
    display->setCurveBaseParamId(curveBaseId);
    display->setBezierEnabledParamId(bezierEnabledId);
    display->setBezierBaseParamId(bezierBaseId);

    // Wire performEdit callback (display -> host)
    display->setParameterCallback(
        [this](uint32_t paramId, float normalizedValue) {
            performEdit(paramId, static_cast<double>(normalizedValue));
        });

    // Wire beginEdit/endEdit for gesture management
    display->setBeginEditCallback(
        [this](uint32_t paramId) {
            beginEdit(paramId);
        });

    display->setEndEditCallback(
        [this](uint32_t paramId) {
            endEdit(paramId);
        });

    // Sync current parameter values to the display
    syncAdsrDisplay(display, adsrBaseId, curveBaseId, bezierEnabledId, bezierBaseId);

    // Wire playback state pointers if already available
    wireEnvDisplayPlayback();
}

void Controller::syncAdsrDisplay(Krate::Plugins::ADSRDisplay* display,
                                  uint32_t adsrBaseId, uint32_t curveBaseId,
                                  uint32_t bezierEnabledId, uint32_t bezierBaseId) {
    if (!display) return;

    // Sync ADSR time/level parameters
    auto* attackParam = getParameterObject(adsrBaseId);
    auto* decayParam = getParameterObject(adsrBaseId + 1);
    auto* sustainParam = getParameterObject(adsrBaseId + 2);
    auto* releaseParam = getParameterObject(adsrBaseId + 3);

    if (attackParam) {
        display->setAttackMs(envTimeFromNormalized(attackParam->getNormalized()));
    }
    if (decayParam) {
        display->setDecayMs(envTimeFromNormalized(decayParam->getNormalized()));
    }
    if (sustainParam) {
        display->setSustainLevel(static_cast<float>(sustainParam->getNormalized()));
    }
    if (releaseParam) {
        display->setReleaseMs(envTimeFromNormalized(releaseParam->getNormalized()));
    }

    // Sync curve amounts
    auto* attackCurveParam = getParameterObject(curveBaseId);
    auto* decayCurveParam = getParameterObject(curveBaseId + 1);
    auto* releaseCurveParam = getParameterObject(curveBaseId + 2);

    if (attackCurveParam) {
        display->setAttackCurve(envCurveFromNormalized(attackCurveParam->getNormalized()));
    }
    if (decayCurveParam) {
        display->setDecayCurve(envCurveFromNormalized(decayCurveParam->getNormalized()));
    }
    if (releaseCurveParam) {
        display->setReleaseCurve(envCurveFromNormalized(releaseCurveParam->getNormalized()));
    }

    // Sync Bezier enabled
    auto* bezierEnabledParam = getParameterObject(bezierEnabledId);
    if (bezierEnabledParam) {
        display->setBezierEnabled(bezierEnabledParam->getNormalized() >= 0.5);
    }

    // Sync Bezier control points (12 consecutive values: 3 segments x 4 values)
    for (int seg = 0; seg < 3; ++seg) {
        for (int idx = 0; idx < 4; ++idx) {
            auto paramId = static_cast<Steinberg::Vst::ParamID>(
                bezierBaseId + static_cast<uint32_t>(seg * 4 + idx));
            auto* param = getParameterObject(paramId);
            if (param) {
                int handle = idx / 2;  // 0=cp1, 1=cp2
                int axis = idx % 2;    // 0=x, 1=y
                display->setBezierHandleValue(seg, handle, axis,
                    static_cast<float>(param->getNormalized()));
            }
        }
    }
}

void Controller::syncAdsrParamToDisplay(
    Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value,
    Krate::Plugins::ADSRDisplay* display,
    uint32_t adsrBaseId, uint32_t curveBaseId,
    uint32_t bezierEnabledId, uint32_t bezierBaseId) {

    if (!display) return;

    // ADSR time/level parameters
    if (tag == adsrBaseId) {
        display->setAttackMs(envTimeFromNormalized(value));
    } else if (tag == adsrBaseId + 1) {
        display->setDecayMs(envTimeFromNormalized(value));
    } else if (tag == adsrBaseId + 2) {
        display->setSustainLevel(static_cast<float>(value));
    } else if (tag == adsrBaseId + 3) {
        display->setReleaseMs(envTimeFromNormalized(value));
    }
    // Curve amounts
    else if (tag == curveBaseId) {
        display->setAttackCurve(envCurveFromNormalized(value));
    } else if (tag == curveBaseId + 1) {
        display->setDecayCurve(envCurveFromNormalized(value));
    } else if (tag == curveBaseId + 2) {
        display->setReleaseCurve(envCurveFromNormalized(value));
    }
    // Bezier enabled
    else if (tag == bezierEnabledId) {
        display->setBezierEnabled(value >= 0.5);
    }
    // Bezier control points (12 consecutive: 3 segments x 4 values)
    else if (tag >= bezierBaseId && tag < bezierBaseId + 12) {
        uint32_t offset = tag - bezierBaseId;
        int seg = static_cast<int>(offset / 4);
        int idx = static_cast<int>(offset % 4);
        int handle = idx / 2;  // 0=cp1, 1=cp2
        int axis = idx % 2;    // 0=x, 1=y
        display->setBezierHandleValue(seg, handle, axis,
            static_cast<float>(value));
    }
}

void Controller::wireEnvDisplayPlayback() {
    // Wire atomic pointers to each ADSRDisplay instance for playback visualization
    if (ampEnvDisplay_ && ampEnvOutputPtr_ && ampEnvStagePtr_ && envVoiceActivePtr_) {
        ampEnvDisplay_->setPlaybackStatePointers(
            ampEnvOutputPtr_, ampEnvStagePtr_, envVoiceActivePtr_);
    }
    if (filterEnvDisplay_ && filterEnvOutputPtr_ && filterEnvStagePtr_ && envVoiceActivePtr_) {
        filterEnvDisplay_->setPlaybackStatePointers(
            filterEnvOutputPtr_, filterEnvStagePtr_, envVoiceActivePtr_);
    }
    if (modEnvDisplay_ && modEnvOutputPtr_ && modEnvStagePtr_ && envVoiceActivePtr_) {
        modEnvDisplay_->setPlaybackStatePointers(
            modEnvOutputPtr_, modEnvStagePtr_, envVoiceActivePtr_);
    }
}

// ==============================================================================
// ModMatrixGrid Wiring (T047, T048, T049)
// ==============================================================================

void Controller::wireModMatrixGrid(Krate::Plugins::ModMatrixGrid* grid) {
    if (!grid) return;

    modMatrixGrid_ = grid;

    // T048: Set ParameterCallback for direct parameter changes (T039-T041)
    // Suppress sync: the grid is the source of truth during user interaction
    grid->setParameterCallback(
        [this](int32_t paramId, float normalizedValue) {
            suppressModMatrixSync_ = true;
            performEdit(static_cast<Steinberg::Vst::ParamID>(paramId),
                        static_cast<double>(normalizedValue));
            suppressModMatrixSync_ = false;
        });

    // T048: Set BeginEditCallback (T042)
    grid->setBeginEditCallback(
        [this](int32_t paramId) {
            suppressModMatrixSync_ = true;
            beginEdit(static_cast<Steinberg::Vst::ParamID>(paramId));
            suppressModMatrixSync_ = false;
        });

    // T048: Set EndEditCallback (T042)
    grid->setEndEditCallback(
        [this](int32_t paramId) {
            suppressModMatrixSync_ = true;
            endEdit(static_cast<Steinberg::Vst::ParamID>(paramId));
            suppressModMatrixSync_ = false;
        });

    // T048: Set RouteChangedCallback (T049, T088)
    grid->setRouteChangedCallback(
        [this](int tab, int slot, const Krate::Plugins::ModRoute& route) {
            if (tab == 0) {
                // Global routes use VST params
                auto sourceId = static_cast<Steinberg::Vst::ParamID>(
                    Krate::Plugins::modSlotSourceId(slot));
                auto destId = static_cast<Steinberg::Vst::ParamID>(
                    Krate::Plugins::modSlotDestinationId(slot));
                auto amountId = static_cast<Steinberg::Vst::ParamID>(
                    Krate::Plugins::modSlotAmountId(slot));

                // UI source index 0-11 maps to DSP ModSource 1-12 (skip None=0)
                int dspSrcIdx = static_cast<int>(route.source) + 1;
                int dstIdx = static_cast<int>(route.destination);

                // Suppress sync-back: grid is the source of truth here
                suppressModMatrixSync_ = true;

                double srcNorm = (kModSourceCount > 1)
                    ? static_cast<double>(dspSrcIdx) / static_cast<double>(kModSourceCount - 1)
                    : 0.0;
                setParamNormalized(sourceId, srcNorm);
                beginEdit(sourceId);
                performEdit(sourceId, srcNorm);
                endEdit(sourceId);

                double dstNorm = (kModDestCount > 1)
                    ? static_cast<double>(dstIdx) / static_cast<double>(kModDestCount - 1)
                    : 0.0;
                setParamNormalized(destId, dstNorm);
                beginEdit(destId);
                performEdit(destId, dstNorm);
                endEdit(destId);

                double amtNorm = static_cast<double>((route.amount + 1.0f) / 2.0f);
                setParamNormalized(amountId, amtNorm);
                beginEdit(amountId);
                performEdit(amountId, amtNorm);
                endEdit(amountId);

                suppressModMatrixSync_ = false;
            } else {
                // Voice routes use IMessage (T088)
                auto msg = Steinberg::owned(allocateMessage());
                if (msg) {
                    msg->setMessageID("VoiceModRouteUpdate");
                    auto* attrs = msg->getAttributes();
                    if (attrs) {
                        attrs->setInt("slotIndex", static_cast<Steinberg::int64>(slot));
                        attrs->setInt("source", static_cast<Steinberg::int64>(route.source));
                        attrs->setInt("destination", static_cast<Steinberg::int64>(route.destination));
                        attrs->setFloat("amount", static_cast<double>(route.amount));
                        attrs->setInt("curve", static_cast<Steinberg::int64>(route.curve));
                        attrs->setFloat("smoothMs", static_cast<double>(route.smoothMs));
                        attrs->setInt("scale", static_cast<Steinberg::int64>(route.scale));
                        attrs->setInt("bypass", route.bypass ? 1 : 0);
                        attrs->setInt("active", route.active ? 1 : 0);
                        sendMessage(msg);
                    }
                }
            }
        });

    // T048: Set RouteRemovedCallback (T088)
    grid->setRouteRemovedCallback(
        [this](int tab, [[maybe_unused]] int slot) {
            if (tab == 0) {
                // Grid has already shifted routes up after removal,
                // so ALL slot parameters must be re-synced from the grid's
                // current state — not just the removed slot.
                suppressModMatrixSync_ = true;
                pushAllGlobalRouteParams();
                suppressModMatrixSync_ = false;
            } else {
                // Voice routes: send full re-sync via IMessage
                for (int i = 0; i < Krate::Plugins::kMaxVoiceRoutes; ++i) {
                    auto route = modMatrixGrid_->getVoiceRoute(i);
                    auto msg = Steinberg::owned(allocateMessage());
                    if (msg) {
                        msg->setMessageID("VoiceModRouteUpdate");
                        auto* attrs = msg->getAttributes();
                        if (attrs) {
                            attrs->setInt("slotIndex", static_cast<Steinberg::int64>(i));
                            attrs->setInt("source", static_cast<Steinberg::int64>(route.source));
                            attrs->setInt("destination", static_cast<Steinberg::int64>(route.destination));
                            attrs->setFloat("amount", static_cast<double>(route.amount));
                            attrs->setInt("curve", static_cast<Steinberg::int64>(route.curve));
                            attrs->setFloat("smoothMs", static_cast<double>(route.smoothMs));
                            attrs->setInt("scale", static_cast<Steinberg::int64>(route.scale));
                            attrs->setInt("bypass", route.bypass ? 1 : 0);
                            attrs->setInt("active", route.active ? 1 : 0);
                            sendMessage(msg);
                        }
                    }
                }
            }
        });

    // Sync initial state from current parameters to the grid
    syncModMatrixGrid();
}

void Controller::syncModMatrixGrid() {
    if (!modMatrixGrid_) return;

    // Read current parameter values and build ModRoute for each slot
    for (int i = 0; i < Krate::Plugins::kMaxGlobalRoutes; ++i) {
        Krate::Plugins::ModRoute route;

        // Source: DSP index 0-12 → UI index (dspIdx - 1), clamped to 0-11
        auto* srcParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotSourceId(i)));
        int dspSrcIdx = 0;
        if (srcParam) {
            dspSrcIdx = static_cast<int>(
                std::round(srcParam->getNormalized() * (kModSourceCount - 1)));
            int uiSrcIdx = std::clamp(dspSrcIdx - 1, 0, Krate::Plugins::kNumGlobalSources - 1);
            route.source = static_cast<uint8_t>(uiSrcIdx);
        }

        // Destination: DSP index 0-6 maps directly to global tab dest index
        auto* dstParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotDestinationId(i)));
        if (dstParam) {
            int dstIdx = static_cast<int>(
                std::round(dstParam->getNormalized() * (kModDestCount - 1)));
            route.destination = static_cast<Krate::Plugins::ModDestination>(
                std::clamp(dstIdx, 0, Krate::Plugins::kNumGlobalDestinations - 1));
        }

        // Amount
        auto* amtParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotAmountId(i)));
        if (amtParam) {
            route.amount = static_cast<float>(amtParam->getNormalized() * 2.0 - 1.0);
        }

        // Detail params
        auto* curveParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotCurveId(i)));
        if (curveParam) {
            route.curve = static_cast<uint8_t>(std::clamp(
                static_cast<int>(std::round(curveParam->getNormalized() * 3.0)),
                0, 3));
        }

        auto* smoothParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotSmoothId(i)));
        if (smoothParam) {
            route.smoothMs = static_cast<float>(smoothParam->getNormalized() * 100.0);
        }

        auto* scaleParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotScaleId(i)));
        if (scaleParam) {
            route.scale = static_cast<uint8_t>(std::clamp(
                static_cast<int>(std::round(scaleParam->getNormalized() * 4.0)),
                0, 4));
        }

        auto* bypassParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotBypassId(i)));
        if (bypassParam) {
            route.bypass = bypassParam->getNormalized() >= 0.5;
        }

        // Route is active if DSP source is not None (0) — None means empty slot
        route.active = (dspSrcIdx > 0);

        modMatrixGrid_->setGlobalRoute(i, route);
    }
}

// ==============================================================================
// ModRingIndicator Wiring (T069, T070, T071, T072)
// ==============================================================================

void Controller::pushAllGlobalRouteParams() {
    if (!modMatrixGrid_) return;

    for (int i = 0; i < Krate::Plugins::kMaxGlobalRoutes; ++i) {
        auto route = modMatrixGrid_->getGlobalRoute(i);

        auto sourceId = static_cast<Steinberg::Vst::ParamID>(
            Krate::Plugins::modSlotSourceId(i));
        auto destId = static_cast<Steinberg::Vst::ParamID>(
            Krate::Plugins::modSlotDestinationId(i));
        auto amountId = static_cast<Steinberg::Vst::ParamID>(
            Krate::Plugins::modSlotAmountId(i));

        if (route.active) {
            // UI source index 0-11 maps to DSP ModSource 1-12 (skip None=0)
            int dspSrcIdx = static_cast<int>(route.source) + 1;
            int dstIdx = static_cast<int>(route.destination);

            double srcNorm = (kModSourceCount > 1)
                ? static_cast<double>(dspSrcIdx) / static_cast<double>(kModSourceCount - 1)
                : 0.0;
            setParamNormalized(sourceId, srcNorm);
            beginEdit(sourceId);
            performEdit(sourceId, srcNorm);
            endEdit(sourceId);

            double dstNorm = (kModDestCount > 1)
                ? static_cast<double>(dstIdx) / static_cast<double>(kModDestCount - 1)
                : 0.0;
            setParamNormalized(destId, dstNorm);
            beginEdit(destId);
            performEdit(destId, dstNorm);
            endEdit(destId);

            double amtNorm = static_cast<double>((route.amount + 1.0f) / 2.0f);
            setParamNormalized(amountId, amtNorm);
            beginEdit(amountId);
            performEdit(amountId, amtNorm);
            endEdit(amountId);
        } else {
            // Inactive slot: reset to defaults (source=None)
            beginEdit(sourceId);
            performEdit(sourceId, 0.0);
            endEdit(sourceId);

            beginEdit(destId);
            performEdit(destId, 0.0);
            endEdit(destId);

            beginEdit(amountId);
            performEdit(amountId, 0.5); // 0.5 normalized = 0.0 bipolar
            endEdit(amountId);
        }
    }
}

// ==============================================================================
// ModRingIndicator Wiring (T069, T070, T071, T072)
// ==============================================================================

void Controller::wireModRingIndicator(Krate::Plugins::ModRingIndicator* indicator) {
    if (!indicator) return;

    int destIdx = indicator->getDestinationIndex();
    if (destIdx < 0 || destIdx >= kMaxRingIndicators) return;

    ringIndicators_[static_cast<size_t>(destIdx)] = indicator;

    // Wire controller for cross-component communication
    indicator->setController(this);

    // Wire click-to-select callback (FR-027, T070)
    indicator->setSelectCallback(
        [this](int sourceIndex, int destIndex) {
            selectModulationRoute(sourceIndex, destIndex);
        });

    // Sync initial base value from destination knob parameter
    if (static_cast<size_t>(destIdx) < kVoiceDestParamIds.size()) {
        auto destParamId = kVoiceDestParamIds[static_cast<size_t>(destIdx)];
        auto* param = getParameterObject(destParamId);
        if (param) {
            indicator->setBaseValue(static_cast<float>(param->getNormalized()));
        }
    }

    // Sync initial arc state from current parameters
    rebuildRingIndicators();
}

void Controller::selectModulationRoute(int sourceIndex, int destIndex) {
    // Mediate selection to ModMatrixGrid (FR-027, T070)
    if (modMatrixGrid_) {
        modMatrixGrid_->selectRoute(sourceIndex, destIndex);
    }
}

// ==============================================================================
// FX Detail Panel Expand/Collapse (T090)
// ==============================================================================

void Controller::toggleFxDetail(int panelIndex) {
    auto panels = {fxDetailFreeze_, fxDetailDelay_, fxDetailReverb_};
    int idx = 0;
    for (auto* panel : panels) {
        if (panel) {
            panel->setVisible(idx == panelIndex && expandedFxPanel_ != panelIndex);
        }
        ++idx;
    }
    expandedFxPanel_ = (expandedFxPanel_ == panelIndex) ? -1 : panelIndex;
}

// ==============================================================================
// Envelope Expand/Collapse
// ==============================================================================

void Controller::toggleEnvExpand(int panelIndex) {
    VSTGUI::CViewContainer* groups[] = {envGroupAmp_, envGroupFilter_, envGroupMod_};
    Krate::Plugins::ADSRDisplay* displays[] = {ampEnvDisplay_, filterEnvDisplay_, modEnvDisplay_};

    // Original container rects (relative to fieldset)
    static constexpr VSTGUI::CRect kOrigGroupRects[] = {
        {8, 14, 178, 134},    // AMP
        {190, 14, 360, 134},  // FILTER
        {372, 14, 542, 134},  // MOD
    };
    // Original display rect (relative to group container) — same for all three
    static constexpr VSTGUI::CRect kOrigDisplayRect = {16, 0, 170, 120};
    // Expanded rects
    static constexpr VSTGUI::CRect kExpandedGroupRect = {8, 14, 542, 134};
    static constexpr VSTGUI::CRect kExpandedDisplayRect = {16, 0, 534, 120};

    if (expandedEnvPanel_ == panelIndex) {
        // Collapse: restore all three
        for (int i = 0; i < 3; ++i) {
            if (groups[i]) {
                groups[i]->setViewSize(kOrigGroupRects[i]);
                groups[i]->setVisible(true);
            }
            if (displays[i]) {
                displays[i]->setViewSize(kOrigDisplayRect);
            }
        }
        expandedEnvPanel_ = -1;
    } else {
        // Expand: hide others, resize target
        for (int i = 0; i < 3; ++i) {
            if (i == panelIndex) {
                if (groups[i]) {
                    groups[i]->setViewSize(kExpandedGroupRect);
                    groups[i]->setVisible(true);
                }
                if (displays[i]) {
                    displays[i]->setViewSize(kExpandedDisplayRect);
                }
            } else {
                if (groups[i]) groups[i]->setVisible(false);
            }
        }
        expandedEnvPanel_ = panelIndex;
    }
}

void Controller::rebuildRingIndicators() {
    // Read all global routes and build ArcInfo lists per destination (T071)
    // First, collect all active routes grouped by destination
    using ArcInfo = Krate::Plugins::ModRingIndicator::ArcInfo;

    // Build route data from current parameters
    struct RouteData {
        int sourceIndex = 0;
        int destIndex = 0;
        float amount = 0.0f;
        bool bypass = false;
        bool active = false;
    };

    std::array<RouteData, Krate::Plugins::kMaxGlobalRoutes> routes{};

    for (int i = 0; i < Krate::Plugins::kMaxGlobalRoutes; ++i) {
        auto* srcParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotSourceId(i)));
        auto* dstParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotDestinationId(i)));
        auto* amtParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotAmountId(i)));
        auto* bypassParam = getParameterObject(
            static_cast<Steinberg::Vst::ParamID>(Krate::Plugins::modSlotBypassId(i)));

        int dspSrcIdx = 0;
        if (srcParam) {
            dspSrcIdx = static_cast<int>(
                std::round(srcParam->getNormalized() * (kModSourceCount - 1)));
            // DSP index → UI index (subtract 1, clamp to 0-11)
            routes[static_cast<size_t>(i)].sourceIndex =
                std::clamp(dspSrcIdx - 1, 0, Krate::Plugins::kNumGlobalSources - 1);
        }
        if (dstParam) {
            routes[static_cast<size_t>(i)].destIndex = static_cast<int>(
                std::round(dstParam->getNormalized() * (kModDestCount - 1)));
        }
        if (amtParam) {
            routes[static_cast<size_t>(i)].amount =
                static_cast<float>(amtParam->getNormalized() * 2.0 - 1.0);
        }
        if (bypassParam) {
            routes[static_cast<size_t>(i)].bypass = bypassParam->getNormalized() >= 0.5;
        }

        // Route is active if DSP source is not None (0)
        routes[static_cast<size_t>(i)].active = (dspSrcIdx > 0);
    }

    // For each destination with a ring indicator, build the arc list.
    // Ring indicators use voice dest indices (0-6) and sit on voice knobs.
    // Match global routes to ring indicators via parameter ID so that
    // e.g. global dest 4 (All Voice Filter Cutoff) shows on ring indicator 0
    // (which sits on the per-voice filter cutoff knob).
    for (int destIdx = 0; destIdx < kMaxRingIndicators; ++destIdx) {
        auto* indicator = ringIndicators_[static_cast<size_t>(destIdx)];
        if (!indicator) continue;

        auto indicatorParamId = kVoiceDestParamIds[static_cast<size_t>(destIdx)];

        std::vector<ArcInfo> arcs;
        for (int i = 0; i < Krate::Plugins::kMaxGlobalRoutes; ++i) {
            const auto& r = routes[static_cast<size_t>(i)];
            if (!r.active) continue;
            if (r.destIndex < 0 ||
                r.destIndex >= static_cast<int>(kGlobalDestParamIds.size()))
                continue;
            if (kGlobalDestParamIds[static_cast<size_t>(r.destIndex)] != indicatorParamId)
                continue;

            ArcInfo arc;
            arc.amount = r.amount;
            arc.color = Krate::Plugins::sourceColorForTab(0, r.sourceIndex);
            arc.sourceIndex = r.sourceIndex;
            arc.destIndex = r.destIndex;
            arc.bypassed = r.bypass;
            arcs.push_back(arc);
        }

        indicator->setArcs(arcs);
    }
}

} // namespace Ruinae
