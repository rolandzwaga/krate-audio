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

    if (version == 1) {
        // Sync all 19 parameter packs in same order as Processor::getState
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
        loadFreezeParamsToController(streamer, setParam);
        loadDelayParamsToController(streamer, setParam);
        loadReverbParamsToController(streamer, setParam);
        loadMonoModeParamsToController(streamer, setParam);
    }
    // Unknown versions: keep defaults (fail closed)

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
        xyMorphPad_ = nullptr;
        ampEnvDisplay_ = nullptr;
        filterEnvDisplay_ = nullptr;
        modEnvDisplay_ = nullptr;
        activeEditor_ = nullptr;
    }
}



VSTGUI::CView* Controller::verifyView(
    VSTGUI::CView* view,
    const VSTGUI::UIAttributes& /*attributes*/,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* /*editor*/) {

    // Register as sub-listener for action buttons (presets, transforms)
    auto* control = dynamic_cast<VSTGUI::CControl*>(view);
    if (control) {
        auto tag = control->getTag();
        if (tag >= kActionPresetAllTag && tag <= kActionEuclideanRegenTag) {
            control->registerControlListener(this);
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

    return view;
}

// ==============================================================================
// IControlListener: Action Button Handling
// ==============================================================================

void Controller::valueChanged(VSTGUI::CControl* control) {
    if (!control || !stepPatternEditor_) return;

    // Only respond to button press (value > 0.5), not release
    if (control->getValue() < 0.5f) return;

    auto tag = control->getTag();
    switch (tag) {
        case kActionPresetAllTag:
            stepPatternEditor_->applyPresetAll();
            break;
        case kActionPresetOffTag:
            stepPatternEditor_->applyPresetOff();
            break;
        case kActionPresetAlternateTag:
            stepPatternEditor_->applyPresetAlternate();
            break;
        case kActionPresetRampUpTag:
            stepPatternEditor_->applyPresetRampUp();
            break;
        case kActionPresetRampDownTag:
            stepPatternEditor_->applyPresetRampDown();
            break;
        case kActionPresetRandomTag:
            stepPatternEditor_->applyPresetRandom();
            break;
        case kActionTransformInvertTag:
            stepPatternEditor_->applyTransformInvert();
            break;
        case kActionTransformShiftRightTag:
            stepPatternEditor_->applyTransformShiftRight();
            break;
        case kActionTransformShiftLeftTag:
            stepPatternEditor_->applyTransformShiftLeft();
            break;
        case kActionEuclideanRegenTag:
            stepPatternEditor_->regenerateEuclidean();
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

} // namespace Ruinae
