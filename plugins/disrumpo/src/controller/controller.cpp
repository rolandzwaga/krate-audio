// ==============================================================================
// Edit Controller Implementation
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation
// Constitution Principle V: VSTGUI Development
// ==============================================================================

#include "controller.h"
#include "plugin_ids.h"
#include "version.h"
#include "dsp/band_state.h"
#include "controller/views/spectrum_display.h"
#include "controller/views/morph_pad.h"

#include "base/source/fstreamer.h"
#include "base/source/fobject.h"
#include "base/source/fstring.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/iuidescription.h"

#include <cmath>
#include <cstring>
#include <atomic>
#include <functional>
#include <vector>
#include <sstream>

namespace Disrumpo {

// ==============================================================================
// VisibilityController: Thread-safe control visibility manager
// ==============================================================================
// Uses IDependent mechanism to receive parameter change notifications on UI thread.
// This is the CORRECT pattern for updating VSTGUI controls based on parameter values.
//
// CRITICAL Threading Rules:
// - setParamNormalized() can be called from ANY thread (automation, state load, etc.)
// - VSTGUI controls MUST only be manipulated on the UI thread
// - Solution: Use Parameter::addDependent() + deferred updates via UpdateHandler
// ==============================================================================
class VisibilityController : public Steinberg::FObject {
public:
    VisibilityController(
        VSTGUI::VST3Editor** editorPtr,
        Steinberg::Vst::Parameter* watchedParam,
        std::initializer_list<Steinberg::int32> controlTags,
        float visibilityThreshold = 0.5f,
        bool showWhenBelow = true)
    : editorPtr_(editorPtr)
    , watchedParam_(watchedParam)
    , controlTags_(controlTags)
    , visibilityThreshold_(visibilityThreshold)
    , showWhenBelow_(showWhenBelow)
    {
        if (watchedParam_) {
            watchedParam_->addRef();
            watchedParam_->addDependent(this);
            watchedParam_->deferUpdate();
        }
    }

    ~VisibilityController() override {
        deactivate();
        if (watchedParam_) {
            watchedParam_->release();
            watchedParam_ = nullptr;
        }
    }

    void deactivate() {
        if (isActive_.exchange(false, std::memory_order_acq_rel)) {
            if (watchedParam_) {
                watchedParam_->removeDependent(this);
            }
        }
    }

    void PLUGIN_API update(Steinberg::FUnknown* /*changedUnknown*/, Steinberg::int32 message) override {
        if (!isActive_.load(std::memory_order_acquire)) {
            return;
        }

        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (message == IDependent::kChanged && watchedParam_ && editor) {
            float normalizedValue = watchedParam_->getNormalized();
            bool shouldBeVisible = showWhenBelow_ ?
                (normalizedValue < visibilityThreshold_) :
                (normalizedValue >= visibilityThreshold_);

            for (Steinberg::int32 tag : controlTags_) {
                auto controls = findAllControlsByTag(tag);
                for (auto* control : controls) {
                    control->setVisible(shouldBeVisible);
                    if (control->getFrame()) {
                        control->invalid();
                    }
                }
            }
        }
    }

    OBJ_METHODS(VisibilityController, FObject)

private:
    std::vector<VSTGUI::CControl*> findAllControlsByTag(Steinberg::int32 tag) {
        std::vector<VSTGUI::CControl*> results;
        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (!editor) return results;
        auto* frame = editor->getFrame();
        if (!frame) return results;

        std::function<void(VSTGUI::CViewContainer*)> search;
        search = [tag, &results, &search](VSTGUI::CViewContainer* container) {
            if (!container) return;
            VSTGUI::ViewIterator it(container);
            while (*it) {
                if (auto* control = dynamic_cast<VSTGUI::CControl*>(*it)) {
                    if (control->getTag() == tag) {
                        results.push_back(control);
                    }
                }
                if (auto* childContainer = (*it)->asViewContainer()) {
                    search(childContainer);
                }
                ++it;
            }
        };
        search(frame);
        return results;
    }

    VSTGUI::VST3Editor** editorPtr_;
    Steinberg::Vst::Parameter* watchedParam_;
    std::vector<Steinberg::int32> controlTags_;
    float visibilityThreshold_;
    bool showWhenBelow_;
    std::atomic<bool> isActive_{true};
};

// ==============================================================================
// ContainerVisibilityController: Hide/show entire CViewContainer by child tag
// ==============================================================================
class ContainerVisibilityController : public Steinberg::FObject {
public:
    ContainerVisibilityController(
        VSTGUI::VST3Editor** editorPtr,
        Steinberg::Vst::Parameter* watchedParam,
        Steinberg::int32 containerTag,
        float threshold,
        bool showWhenBelow = true)
    : editorPtr_(editorPtr)
    , watchedParam_(watchedParam)
    , containerTag_(containerTag)
    , threshold_(threshold)
    , showWhenBelow_(showWhenBelow)
    {
        if (watchedParam_) {
            watchedParam_->addRef();
            watchedParam_->addDependent(this);
            watchedParam_->deferUpdate();
        }
    }

    ~ContainerVisibilityController() override {
        deactivate();
        if (watchedParam_) {
            watchedParam_->release();
            watchedParam_ = nullptr;
        }
    }

    void deactivate() {
        if (isActive_.exchange(false, std::memory_order_acq_rel)) {
            if (watchedParam_) {
                watchedParam_->removeDependent(this);
            }
        }
    }

    void PLUGIN_API update(Steinberg::FUnknown* /*changedUnknown*/, Steinberg::int32 message) override {
        if (!isActive_.load(std::memory_order_acquire)) {
            return;
        }

        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (message == IDependent::kChanged && watchedParam_ && editor) {
            float normalizedValue = watchedParam_->getNormalized();
            bool shouldBeVisible = showWhenBelow_ ?
                (normalizedValue < threshold_) :
                (normalizedValue >= threshold_);

            auto* container = findContainerByTag(containerTag_);
            if (container) {
                container->setVisible(shouldBeVisible);
                if (container->getFrame()) {
                    container->invalid();
                }
            }
        }
    }

    OBJ_METHODS(ContainerVisibilityController, FObject)

private:
    VSTGUI::CViewContainer* findContainerByTag(Steinberg::int32 tag) {
        VSTGUI::VST3Editor* editor = editorPtr_ ? *editorPtr_ : nullptr;
        if (!editor) return nullptr;
        auto* frame = editor->getFrame();
        if (!frame) return nullptr;

        std::function<VSTGUI::CViewContainer*(VSTGUI::CViewContainer*)> search;
        search = [tag, &search](VSTGUI::CViewContainer* container) -> VSTGUI::CViewContainer* {
            if (!container) return nullptr;
            VSTGUI::ViewIterator it(container);
            while (*it) {
                if (auto* ctrl = dynamic_cast<VSTGUI::CControl*>(*it)) {
                    if (ctrl->getTag() == tag) {
                        return container;
                    }
                }
                if (auto* childContainer = (*it)->asViewContainer()) {
                    if (auto* found = search(childContainer)) {
                        return found;
                    }
                }
                ++it;
            }
            return nullptr;
        };
        return search(frame);
    }

    VSTGUI::VST3Editor** editorPtr_;
    Steinberg::Vst::Parameter* watchedParam_;
    Steinberg::int32 containerTag_;
    float threshold_;
    bool showWhenBelow_;
    std::atomic<bool> isActive_{true};
};

// ==============================================================================
// Helper: Convert int to TChar string
// ==============================================================================
static void intToString128(int value, Steinberg::Vst::String128 dest) {
    char temp[32];
    snprintf(temp, sizeof(temp), "%d", value);
    for (int i = 0; temp[i] && i < 127; ++i) {
        dest[i] = static_cast<Steinberg::Vst::TChar>(temp[i]);
    }
    dest[std::min(static_cast<int>(strlen(temp)), 127)] = 0;
}

static void floatToString128(double value, int precision, Steinberg::Vst::String128 dest) {
    char temp[64];
    snprintf(temp, sizeof(temp), "%.*f", precision, value);
    for (int i = 0; temp[i] && i < 127; ++i) {
        dest[i] = static_cast<Steinberg::Vst::TChar>(temp[i]);
    }
    dest[std::min(static_cast<int>(strlen(temp)), 127)] = 0;
}

static void appendToString128(Steinberg::Vst::String128 dest, const Steinberg::Vst::TChar* suffix) {
    int len = 0;
    while (dest[len] && len < 127) ++len;
    int suffixLen = 0;
    while (suffix[suffixLen] && (len + suffixLen) < 127) {
        dest[len + suffixLen] = suffix[suffixLen];
        ++suffixLen;
    }
    dest[len + suffixLen] = 0;
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

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Controller::terminate() {
    // Cleanup any resources allocated in initialize()
    return EditControllerEx1::terminate();
}

// ==============================================================================
// Parameter Registration Helpers
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

    // Band Count: StringListParameter ["1".."8"], default "4"
    auto* bandCountParam = new Steinberg::Vst::StringListParameter(
        STR16("Band Count"),
        makeGlobalParamId(GlobalParamType::kGlobalBandCount),
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
    );
    for (int i = 1; i <= 8; ++i) {
        Steinberg::Vst::String128 str;
        intToString128(i, str);
        bandCountParam->appendString(str);
    }
    bandCountParam->setNormalized(3.0 / 7.0);  // Default to index 3 = "4"
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

    // Crossover frequency parameters (7 crossovers for 8 bands)
    const Steinberg::Vst::TChar* crossoverNames[] = {
        STR16("Crossover 1"), STR16("Crossover 2"), STR16("Crossover 3"), STR16("Crossover 4"),
        STR16("Crossover 5"), STR16("Crossover 6"), STR16("Crossover 7")
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
}

void Controller::registerModulationParams() {
    // FR-004: Register modulation parameters (T4.11)
    // Placeholder for Week 9 modulation spec
    // These are stub registrations with correct ID range
}

void Controller::registerBandParams() {
    // FR-005: Register per-band parameters for 8 bands (T4.12)

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
    const Steinberg::Vst::TChar* bandMorphXNames[] = {
        STR16("Band 1 Morph X"), STR16("Band 2 Morph X"), STR16("Band 3 Morph X"), STR16("Band 4 Morph X"),
        STR16("Band 5 Morph X"), STR16("Band 6 Morph X"), STR16("Band 7 Morph X"), STR16("Band 8 Morph X")
    };
    const Steinberg::Vst::TChar* bandMorphYNames[] = {
        STR16("Band 1 Morph Y"), STR16("Band 2 Morph Y"), STR16("Band 3 Morph Y"), STR16("Band 4 Morph Y"),
        STR16("Band 5 Morph Y"), STR16("Band 6 Morph Y"), STR16("Band 7 Morph Y"), STR16("Band 8 Morph Y")
    };
    const Steinberg::Vst::TChar* bandMorphModeNames[] = {
        STR16("Band 1 Morph Mode"), STR16("Band 2 Morph Mode"), STR16("Band 3 Morph Mode"), STR16("Band 4 Morph Mode"),
        STR16("Band 5 Morph Mode"), STR16("Band 6 Morph Mode"), STR16("Band 7 Morph Mode"), STR16("Band 8 Morph Mode")
    };
    const Steinberg::Vst::TChar* bandExpandedNames[] = {
        STR16("Band 1 Expanded"), STR16("Band 2 Expanded"), STR16("Band 3 Expanded"), STR16("Band 4 Expanded"),
        STR16("Band 5 Expanded"), STR16("Band 6 Expanded"), STR16("Band 7 Expanded"), STR16("Band 8 Expanded")
    };

    for (int b = 0; b < kMaxBands; ++b) {
        // Band Gain: RangeParameter [-24, +24] dB, default 0
        auto* gainParam = new Steinberg::Vst::RangeParameter(
            bandGainNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandGain),
            STR16("dB"),
            static_cast<double>(kMinBandGainDb),
            static_cast<double>(kMaxBandGainDb),
            0.0,
            0,
            Steinberg::Vst::ParameterInfo::kCanAutomate
        );
        parameters.addParameter(gainParam);

        // Band Pan: RangeParameter [-1, +1], default 0 (center)
        auto* panParam = new Steinberg::Vst::RangeParameter(
            bandPanNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandPan),
            STR16(""),
            -1.0, 1.0, 0.0,
            0,
            Steinberg::Vst::ParameterInfo::kCanAutomate
        );
        parameters.addParameter(panParam);

        // Band Solo: boolean toggle, stepCount=1
        parameters.addParameter(
            bandSoloNames[b],
            nullptr,
            1,  // stepCount = 1 for boolean (FR-030)
            0.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandSolo)
        );

        // Band Bypass: boolean toggle, stepCount=1
        parameters.addParameter(
            bandBypassNames[b],
            nullptr,
            1,
            0.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandBypass)
        );

        // Band Mute: boolean toggle, stepCount=1
        parameters.addParameter(
            bandMuteNames[b],
            nullptr,
            1,
            0.0,
            Steinberg::Vst::ParameterInfo::kCanAutomate,
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMute)
        );

        // Band MorphX: RangeParameter [0, 1], default 0.5
        auto* morphXParam = new Steinberg::Vst::RangeParameter(
            bandMorphXNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMorphX),
            STR16(""),
            0.0, 1.0, 0.5,
            0,
            Steinberg::Vst::ParameterInfo::kCanAutomate
        );
        parameters.addParameter(morphXParam);

        // Band MorphY: RangeParameter [0, 1], default 0.5
        auto* morphYParam = new Steinberg::Vst::RangeParameter(
            bandMorphYNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMorphY),
            STR16(""),
            0.0, 1.0, 0.5,
            0,
            Steinberg::Vst::ParameterInfo::kCanAutomate
        );
        parameters.addParameter(morphYParam);

        // Band MorphMode: StringListParameter ["1D Linear","2D Planar","2D Radial"]
        auto* morphModeParam = new Steinberg::Vst::StringListParameter(
            bandMorphModeNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMorphMode),
            nullptr,
            Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
        );
        morphModeParam->appendString(STR16("1D Linear"));
        morphModeParam->appendString(STR16("2D Planar"));
        morphModeParam->appendString(STR16("2D Radial"));
        parameters.addParameter(morphModeParam);

        // Band ActiveNodes: StringListParameter ["2","3","4"], default "4" (US6)
        static const Steinberg::Vst::TChar* activeNodesParamNames[] = {
            STR16("Band 1 Active Nodes"), STR16("Band 2 Active Nodes"),
            STR16("Band 3 Active Nodes"), STR16("Band 4 Active Nodes"),
            STR16("Band 5 Active Nodes"), STR16("Band 6 Active Nodes"),
            STR16("Band 7 Active Nodes"), STR16("Band 8 Active Nodes")
        };
        auto* activeNodesParam = new Steinberg::Vst::StringListParameter(
            activeNodesParamNames[b],
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandActiveNodes),
            nullptr,
            Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
        );
        activeNodesParam->appendString(STR16("2"));
        activeNodesParam->appendString(STR16("3"));
        activeNodesParam->appendString(STR16("4"));
        activeNodesParam->setNormalized(1.0);  // Default to "4" (index 2 = 1.0)
        parameters.addParameter(activeNodesParam);

        // Band Expanded: boolean toggle for expand/collapse state (UI only)
        // stepCount=1 for boolean, default 0 (collapsed)
        parameters.addParameter(
            bandExpandedNames[b],
            nullptr,
            1,
            0.0,
            Steinberg::Vst::ParameterInfo::kNoFlags,  // UI-only, not automatable
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandExpanded)
        );
    }
}

void Controller::registerNodeParams() {
    // FR-006: Register per-node parameters for 4 nodes x 8 bands (T4.13)

    // 26 distortion type names from dsp-details.md
    static const Steinberg::Vst::TChar* kDistortionTypeNames[] = {
        STR16("Soft Clip"),
        STR16("Hard Clip"),
        STR16("Tube"),
        STR16("Tape"),
        STR16("Fuzz"),
        STR16("Asymmetric Fuzz"),
        STR16("Sine Fold"),
        STR16("Triangle Fold"),
        STR16("Serge Fold"),
        STR16("Full Rectify"),
        STR16("Half Rectify"),
        STR16("Bitcrush"),
        STR16("Sample Reduce"),
        STR16("Quantize"),
        STR16("Temporal"),
        STR16("Ring Saturation"),
        STR16("Feedback"),
        STR16("Aliasing"),
        STR16("Bitwise Mangler"),
        STR16("Chaos"),
        STR16("Formant"),
        STR16("Granular"),
        STR16("Spectral"),
        STR16("Fractal"),
        STR16("Stochastic"),
        STR16("Allpass Resonant")
    };
    static constexpr int kNumDistortionTypes = 26;

    // Pre-defined parameter names for all band/node combinations
    static const Steinberg::Vst::TChar* nodeTypeNames[8][4] = {
        { STR16("B1 N1 Type"), STR16("B1 N2 Type"), STR16("B1 N3 Type"), STR16("B1 N4 Type") },
        { STR16("B2 N1 Type"), STR16("B2 N2 Type"), STR16("B2 N3 Type"), STR16("B2 N4 Type") },
        { STR16("B3 N1 Type"), STR16("B3 N2 Type"), STR16("B3 N3 Type"), STR16("B3 N4 Type") },
        { STR16("B4 N1 Type"), STR16("B4 N2 Type"), STR16("B4 N3 Type"), STR16("B4 N4 Type") },
        { STR16("B5 N1 Type"), STR16("B5 N2 Type"), STR16("B5 N3 Type"), STR16("B5 N4 Type") },
        { STR16("B6 N1 Type"), STR16("B6 N2 Type"), STR16("B6 N3 Type"), STR16("B6 N4 Type") },
        { STR16("B7 N1 Type"), STR16("B7 N2 Type"), STR16("B7 N3 Type"), STR16("B7 N4 Type") },
        { STR16("B8 N1 Type"), STR16("B8 N2 Type"), STR16("B8 N3 Type"), STR16("B8 N4 Type") }
    };
    static const Steinberg::Vst::TChar* nodeDriveNames[8][4] = {
        { STR16("B1 N1 Drive"), STR16("B1 N2 Drive"), STR16("B1 N3 Drive"), STR16("B1 N4 Drive") },
        { STR16("B2 N1 Drive"), STR16("B2 N2 Drive"), STR16("B2 N3 Drive"), STR16("B2 N4 Drive") },
        { STR16("B3 N1 Drive"), STR16("B3 N2 Drive"), STR16("B3 N3 Drive"), STR16("B3 N4 Drive") },
        { STR16("B4 N1 Drive"), STR16("B4 N2 Drive"), STR16("B4 N3 Drive"), STR16("B4 N4 Drive") },
        { STR16("B5 N1 Drive"), STR16("B5 N2 Drive"), STR16("B5 N3 Drive"), STR16("B5 N4 Drive") },
        { STR16("B6 N1 Drive"), STR16("B6 N2 Drive"), STR16("B6 N3 Drive"), STR16("B6 N4 Drive") },
        { STR16("B7 N1 Drive"), STR16("B7 N2 Drive"), STR16("B7 N3 Drive"), STR16("B7 N4 Drive") },
        { STR16("B8 N1 Drive"), STR16("B8 N2 Drive"), STR16("B8 N3 Drive"), STR16("B8 N4 Drive") }
    };
    static const Steinberg::Vst::TChar* nodeMixNames[8][4] = {
        { STR16("B1 N1 Mix"), STR16("B1 N2 Mix"), STR16("B1 N3 Mix"), STR16("B1 N4 Mix") },
        { STR16("B2 N1 Mix"), STR16("B2 N2 Mix"), STR16("B2 N3 Mix"), STR16("B2 N4 Mix") },
        { STR16("B3 N1 Mix"), STR16("B3 N2 Mix"), STR16("B3 N3 Mix"), STR16("B3 N4 Mix") },
        { STR16("B4 N1 Mix"), STR16("B4 N2 Mix"), STR16("B4 N3 Mix"), STR16("B4 N4 Mix") },
        { STR16("B5 N1 Mix"), STR16("B5 N2 Mix"), STR16("B5 N3 Mix"), STR16("B5 N4 Mix") },
        { STR16("B6 N1 Mix"), STR16("B6 N2 Mix"), STR16("B6 N3 Mix"), STR16("B6 N4 Mix") },
        { STR16("B7 N1 Mix"), STR16("B7 N2 Mix"), STR16("B7 N3 Mix"), STR16("B7 N4 Mix") },
        { STR16("B8 N1 Mix"), STR16("B8 N2 Mix"), STR16("B8 N3 Mix"), STR16("B8 N4 Mix") }
    };
    static const Steinberg::Vst::TChar* nodeToneNames[8][4] = {
        { STR16("B1 N1 Tone"), STR16("B1 N2 Tone"), STR16("B1 N3 Tone"), STR16("B1 N4 Tone") },
        { STR16("B2 N1 Tone"), STR16("B2 N2 Tone"), STR16("B2 N3 Tone"), STR16("B2 N4 Tone") },
        { STR16("B3 N1 Tone"), STR16("B3 N2 Tone"), STR16("B3 N3 Tone"), STR16("B3 N4 Tone") },
        { STR16("B4 N1 Tone"), STR16("B4 N2 Tone"), STR16("B4 N3 Tone"), STR16("B4 N4 Tone") },
        { STR16("B5 N1 Tone"), STR16("B5 N2 Tone"), STR16("B5 N3 Tone"), STR16("B5 N4 Tone") },
        { STR16("B6 N1 Tone"), STR16("B6 N2 Tone"), STR16("B6 N3 Tone"), STR16("B6 N4 Tone") },
        { STR16("B7 N1 Tone"), STR16("B7 N2 Tone"), STR16("B7 N3 Tone"), STR16("B7 N4 Tone") },
        { STR16("B8 N1 Tone"), STR16("B8 N2 Tone"), STR16("B8 N3 Tone"), STR16("B8 N4 Tone") }
    };
    static const Steinberg::Vst::TChar* nodeBiasNames[8][4] = {
        { STR16("B1 N1 Bias"), STR16("B1 N2 Bias"), STR16("B1 N3 Bias"), STR16("B1 N4 Bias") },
        { STR16("B2 N1 Bias"), STR16("B2 N2 Bias"), STR16("B2 N3 Bias"), STR16("B2 N4 Bias") },
        { STR16("B3 N1 Bias"), STR16("B3 N2 Bias"), STR16("B3 N3 Bias"), STR16("B3 N4 Bias") },
        { STR16("B4 N1 Bias"), STR16("B4 N2 Bias"), STR16("B4 N3 Bias"), STR16("B4 N4 Bias") },
        { STR16("B5 N1 Bias"), STR16("B5 N2 Bias"), STR16("B5 N3 Bias"), STR16("B5 N4 Bias") },
        { STR16("B6 N1 Bias"), STR16("B6 N2 Bias"), STR16("B6 N3 Bias"), STR16("B6 N4 Bias") },
        { STR16("B7 N1 Bias"), STR16("B7 N2 Bias"), STR16("B7 N3 Bias"), STR16("B7 N4 Bias") },
        { STR16("B8 N1 Bias"), STR16("B8 N2 Bias"), STR16("B8 N3 Bias"), STR16("B8 N4 Bias") }
    };
    static const Steinberg::Vst::TChar* nodeFoldsNames[8][4] = {
        { STR16("B1 N1 Folds"), STR16("B1 N2 Folds"), STR16("B1 N3 Folds"), STR16("B1 N4 Folds") },
        { STR16("B2 N1 Folds"), STR16("B2 N2 Folds"), STR16("B2 N3 Folds"), STR16("B2 N4 Folds") },
        { STR16("B3 N1 Folds"), STR16("B3 N2 Folds"), STR16("B3 N3 Folds"), STR16("B3 N4 Folds") },
        { STR16("B4 N1 Folds"), STR16("B4 N2 Folds"), STR16("B4 N3 Folds"), STR16("B4 N4 Folds") },
        { STR16("B5 N1 Folds"), STR16("B5 N2 Folds"), STR16("B5 N3 Folds"), STR16("B5 N4 Folds") },
        { STR16("B6 N1 Folds"), STR16("B6 N2 Folds"), STR16("B6 N3 Folds"), STR16("B6 N4 Folds") },
        { STR16("B7 N1 Folds"), STR16("B7 N2 Folds"), STR16("B7 N3 Folds"), STR16("B7 N4 Folds") },
        { STR16("B8 N1 Folds"), STR16("B8 N2 Folds"), STR16("B8 N3 Folds"), STR16("B8 N4 Folds") }
    };
    static const Steinberg::Vst::TChar* nodeBitDepthNames[8][4] = {
        { STR16("B1 N1 BitDepth"), STR16("B1 N2 BitDepth"), STR16("B1 N3 BitDepth"), STR16("B1 N4 BitDepth") },
        { STR16("B2 N1 BitDepth"), STR16("B2 N2 BitDepth"), STR16("B2 N3 BitDepth"), STR16("B2 N4 BitDepth") },
        { STR16("B3 N1 BitDepth"), STR16("B3 N2 BitDepth"), STR16("B3 N3 BitDepth"), STR16("B3 N4 BitDepth") },
        { STR16("B4 N1 BitDepth"), STR16("B4 N2 BitDepth"), STR16("B4 N3 BitDepth"), STR16("B4 N4 BitDepth") },
        { STR16("B5 N1 BitDepth"), STR16("B5 N2 BitDepth"), STR16("B5 N3 BitDepth"), STR16("B5 N4 BitDepth") },
        { STR16("B6 N1 BitDepth"), STR16("B6 N2 BitDepth"), STR16("B6 N3 BitDepth"), STR16("B6 N4 BitDepth") },
        { STR16("B7 N1 BitDepth"), STR16("B7 N2 BitDepth"), STR16("B7 N3 BitDepth"), STR16("B7 N4 BitDepth") },
        { STR16("B8 N1 BitDepth"), STR16("B8 N2 BitDepth"), STR16("B8 N3 BitDepth"), STR16("B8 N4 BitDepth") }
    };

    for (int b = 0; b < kMaxBands; ++b) {
        for (int n = 0; n < 4; ++n) {  // 4 nodes per band
            // Node Type: StringListParameter with 26 distortion types
            auto* typeParam = new Steinberg::Vst::StringListParameter(
                nodeTypeNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeType),
                nullptr,
                Steinberg::Vst::ParameterInfo::kCanAutomate | Steinberg::Vst::ParameterInfo::kIsList
            );
            for (auto & kDistortionTypeName : kDistortionTypeNames) {
                typeParam->appendString(kDistortionTypeName);
            }
            parameters.addParameter(typeParam);

            // Node Drive: RangeParameter [0, 10], default 1
            auto* driveParam = new Steinberg::Vst::RangeParameter(
                nodeDriveNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeDrive),
                STR16(""),
                0.0, 10.0, 1.0,
                0,
                Steinberg::Vst::ParameterInfo::kCanAutomate
            );
            parameters.addParameter(driveParam);

            // Node Mix: RangeParameter [0, 100] %, default 100
            auto* nodeMixParam = new Steinberg::Vst::RangeParameter(
                nodeMixNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeMix),
                STR16("%"),
                0.0, 100.0, 100.0,
                0,
                Steinberg::Vst::ParameterInfo::kCanAutomate
            );
            parameters.addParameter(nodeMixParam);

            // Node Tone: RangeParameter [200, 8000] Hz
            auto* toneParam = new Steinberg::Vst::RangeParameter(
                nodeToneNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeTone),
                STR16("Hz"),
                200.0, 8000.0, 4000.0,
                0,
                Steinberg::Vst::ParameterInfo::kCanAutomate
            );
            parameters.addParameter(toneParam);

            // Node Bias: RangeParameter [-1, +1], default 0
            auto* biasParam = new Steinberg::Vst::RangeParameter(
                nodeBiasNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeBias),
                STR16(""),
                -1.0, 1.0, 0.0,
                0,
                Steinberg::Vst::ParameterInfo::kCanAutomate
            );
            parameters.addParameter(biasParam);

            // Node Folds: RangeParameter [1, 12], integer steps
            auto* foldsParam = new Steinberg::Vst::RangeParameter(
                nodeFoldsNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeFolds),
                STR16(""),
                1.0, 12.0, 2.0,
                11,  // 12 integer steps (1-12)
                Steinberg::Vst::ParameterInfo::kCanAutomate
            );
            parameters.addParameter(foldsParam);

            // Node BitDepth: RangeParameter [4, 24], integer steps
            auto* bitDepthParam = new Steinberg::Vst::RangeParameter(
                nodeBitDepthNames[b][n],
                makeNodeParamId(static_cast<uint8_t>(b), static_cast<uint8_t>(n), NodeParamType::kNodeBitDepth),
                STR16("bit"),
                4.0, 24.0, 16.0,
                20,  // 21 integer steps (4-24)
                Steinberg::Vst::ParameterInfo::kCanAutomate
            );
            parameters.addParameter(bitDepthParam);
        }
    }
}

// ==============================================================================
// IEditController
// ==============================================================================

Steinberg::tresult PLUGIN_API Controller::setComponentState(Steinberg::IBStream* state) {
    // FR-026: Sync from processor state
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

    // Read global parameters
    float inputGain = 0.5f;
    float outputGain = 0.5f;
    float globalMix = 1.0f;

    if (!streamer.readFloat(inputGain)) return Steinberg::kResultFalse;
    if (!streamer.readFloat(outputGain)) return Steinberg::kResultFalse;
    if (!streamer.readFloat(globalMix)) return Steinberg::kResultFalse;

    // Update controller's parameter values (for UI display)
    setParamNormalized(makeGlobalParamId(GlobalParamType::kGlobalInputGain), inputGain);
    setParamNormalized(makeGlobalParamId(GlobalParamType::kGlobalOutputGain), outputGain);
    setParamNormalized(makeGlobalParamId(GlobalParamType::kGlobalMix), globalMix);

    // Read band count if version >= 2
    if (version >= 2) {
        int32_t bandCount = 4;
        if (streamer.readInt32(bandCount)) {
            // Convert band count to normalized value (1-8 maps to 0.0-1.0)
            float normalizedBandCount = static_cast<float>(bandCount - 1) / 7.0f;
            setParamNormalized(makeGlobalParamId(GlobalParamType::kGlobalBandCount), normalizedBandCount);
        }

        // Read band states
        for (int b = 0; b < kMaxBands; ++b) {
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

            // Convert gain from dB to normalized
            auto* gainParam = getParameterObject(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandGain));
            if (gainParam) {
                setParamNormalized(gainParam->getInfo().id, gainParam->toNormalized(gain));
            }

            // Convert pan from [-1,1] to normalized
            auto* panParam = getParameterObject(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandPan));
            if (panParam) {
                setParamNormalized(panParam->getInfo().id, panParam->toNormalized(pan));
            }

            setParamNormalized(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandSolo), soloInt != 0 ? 1.0f : 0.0f);
            setParamNormalized(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandBypass), bypassInt != 0 ? 1.0f : 0.0f);
            setParamNormalized(makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandMute), muteInt != 0 ? 1.0f : 0.0f);
        }

        // Read crossover frequencies
        for (int i = 0; i < kMaxBands - 1; ++i) {
            float freq = 1000.0f;
            if (streamer.readFloat(freq)) {
                auto* param = getParameterObject(makeCrossoverParamId(static_cast<uint8_t>(i)));
                if (param) {
                    setParamNormalized(param->getInfo().id, param->toNormalized(freq));
                }
            }
        }
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::getState(Steinberg::IBStream* state) {
    // Save controller-specific state (UI settings, etc.)
    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Write controller state version
    if (!streamer.writeInt32(1)) {
        return Steinberg::kResultFalse;
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Controller::setState(Steinberg::IBStream* state) {
    // Restore controller-specific state
    Steinberg::IBStreamer streamer(state, kLittleEndian);

    int32_t version = 0;
    if (!streamer.readInt32(version)) {
        return Steinberg::kResultOk;
    }

    return Steinberg::kResultOk;
}

Steinberg::IPlugView* PLUGIN_API Controller::createView(Steinberg::FIDString name) {
    // FR-011: Create VST3Editor with editor.uidesc
    if (std::strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0) {
        auto* editor = new VSTGUI::VST3Editor(this, "editor", "editor.uidesc");
        return editor;
    }
    return nullptr;
}

Steinberg::tresult PLUGIN_API Controller::getParamStringByValue(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue valueNormalized,
    Steinberg::Vst::String128 string) {

    // FR-027: Custom formatting for Drive, Mix, Gain, Type, Pan

    // Check for node parameters
    if (isNodeParamId(id)) {
        NodeParamType paramType = extractNodeParamType(id);

        // Drive: plain number, one decimal, no unit (e.g., "5.2")
        if (paramType == NodeParamType::kNodeDrive) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);
                floatToString128(plainValue, 1, string);
                return Steinberg::kResultTrue;
            }
        }

        // Node Mix: percentage with no decimal (e.g., "75%")
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

        // Band Gain: dB with one decimal (e.g., "4.5 dB")
        if (paramType == BandParamType::kBandGain) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);
                floatToString128(plainValue, 1, string);
                appendToString128(string, STR16(" dB"));
                return Steinberg::kResultTrue;
            }
        }

        // Band Pan: percentage with L/R suffix (e.g., "30% L", "30% R", "Center")
        if (paramType == BandParamType::kBandPan) {
            auto* param = getParameterObject(id);
            if (param) {
                double plainValue = param->toPlain(valueNormalized);  // -1 to +1

                if (std::abs(plainValue) < 0.01) {
                    // Use manual copy for "Center"
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
        // Global Mix: percentage with no decimal (e.g., "75%")
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

        // Input/Output Gain: dB with one decimal (e.g., "4.5 dB")
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

    // Fall back to default formatting
    return EditControllerEx1::getParamStringByValue(id, valueNormalized, string);
}

Steinberg::tresult PLUGIN_API Controller::getParamValueByString(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::TChar* string,
    Steinberg::Vst::ParamValue& valueNormalized) {
    // Fall back to default parsing
    return EditControllerEx1::getParamValueByString(id, string, valueNormalized);
}

// ==============================================================================
// VST3EditorDelegate
// ==============================================================================

VSTGUI::CView* Controller::createCustomView(
    VSTGUI::UTF8StringPtr name,
    const VSTGUI::UIAttributes& attributes,
    const VSTGUI::IUIDescription* /*description*/,
    VSTGUI::VST3Editor* /*editor*/) {

    // FR-013: Create custom views
    if (std::strcmp(name, "SpectrumDisplay") == 0) {
        // Read size from UIAttributes
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;

        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");

        if (originStr) {
            double x = 0.0;
            double y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2) {
                origin = VSTGUI::CPoint(x, y);
            }
        }

        if (sizeStr) {
            double w = 980.0;
            double h = 200.0;  // Default size
            if (sscanf(sizeStr->c_str(), "%lf, %lf", &w, &h) == 2) {
                size = VSTGUI::CPoint(w, h);
            }
        } else {
            size = VSTGUI::CPoint(980.0, 200.0);  // Default size
        }

        VSTGUI::CRect rect(origin, size);
        auto* spectrumDisplay = new SpectrumDisplay(rect);

        // Initialize with current band count from parameter
        auto* bandCountParam = getParameterObject(makeGlobalParamId(GlobalParamType::kGlobalBandCount));
        if (bandCountParam) {
            float normalized = static_cast<float>(bandCountParam->getNormalized());
            int bandCount = static_cast<int>(std::round(normalized * 7.0f)) + 1;
            spectrumDisplay->setNumBands(bandCount);
        }

        // Initialize crossover frequencies from parameters
        for (int i = 0; i < kMaxBands - 1; ++i) {
            auto* crossoverParam = getParameterObject(makeCrossoverParamId(static_cast<uint8_t>(i)));
            if (crossoverParam) {
                float freq = static_cast<float>(crossoverParam->toPlain(crossoverParam->getNormalized()));
                spectrumDisplay->setCrossoverFrequency(i, freq);
            }
        }

        // Store reference for later access (e.g., in willClose)
        spectrumDisplay_ = spectrumDisplay;

        return spectrumDisplay;
    }

    if (std::strcmp(name, "MorphPad") == 0) {
        // FR-010: Create MorphPad custom control
        VSTGUI::CPoint origin;
        VSTGUI::CPoint size;

        const std::string* originStr = attributes.getAttributeValue("origin");
        const std::string* sizeStr = attributes.getAttributeValue("size");

        if (originStr) {
            double x = 0.0;
            double y = 0.0;
            if (sscanf(originStr->c_str(), "%lf, %lf", &x, &y) == 2) {
                origin = VSTGUI::CPoint(x, y);
            }
        }

        if (sizeStr) {
            double w = 250.0;  // Default full size
            double h = 200.0;
            if (sscanf(sizeStr->c_str(), "%lf, %lf", &w, &h) == 2) {
                size = VSTGUI::CPoint(w, h);
            }
        } else {
            size = VSTGUI::CPoint(250.0, 200.0);  // Default size
        }

        VSTGUI::CRect rect(origin, size);
        auto* morphPad = new MorphPad(rect);

        // FR-011: Wire to band-specific morph parameters
        // Read band index from "band" attribute (0-7, default 0)
        int bandIndex = 0;
        const std::string* bandStr = attributes.getAttributeValue("band");
        if (bandStr) {
            bandIndex = std::stoi(*bandStr);
            bandIndex = std::clamp(bandIndex, 0, kMaxBands - 1);
        }

        // Set the control tag to the MorphX parameter ID for this band
        // MorphPad uses CControl::getValue()/setValue() for X position
        Steinberg::Vst::ParamID morphXParamId = makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandMorphX);
        morphPad->setTag(static_cast<int32_t>(morphXParamId));

        // Initialize morph position from current parameter values
        auto* morphXParam = getParameterObject(morphXParamId);
        auto* morphYParam = getParameterObject(makeBandParamId(
            static_cast<uint8_t>(bandIndex), BandParamType::kBandMorphY));

        if (morphXParam && morphYParam) {
            float morphX = static_cast<float>(morphXParam->getNormalized());
            float morphY = static_cast<float>(morphYParam->getNormalized());
            morphPad->setMorphPosition(morphX, morphY);
            morphPad->setValue(morphX);
        }

        // Initialize node types from the band's node type parameters
        for (int n = 0; n < 4; ++n) {
            auto* nodeTypeParam = getParameterObject(makeNodeParamId(
                static_cast<uint8_t>(bandIndex), static_cast<uint8_t>(n), NodeParamType::kNodeType));
            if (nodeTypeParam) {
                int typeIndex = static_cast<int>(std::round(nodeTypeParam->getNormalized() * 25.0));
                morphPad->setNodeType(n, static_cast<DistortionType>(typeIndex));
            }
        }

        return morphPad;
    }

    return nullptr;
}

void Controller::didOpen(VSTGUI::VST3Editor* editor) {
    // FR-023: Called when the editor is opened
    activeEditor_ = editor;

    // Create band visibility controllers (FR-025)
    // Show/hide band containers based on Band Count parameter
    auto* bandCountParam = getParameterObject(makeGlobalParamId(GlobalParamType::kGlobalBandCount));
    if (bandCountParam) {
        for (int b = 0; b < kMaxBands; ++b) {
            // Threshold for band visibility: band b is shown when bandCount >= b+1
            // Normalized value at index i = i / 7.0f (for 8 values 0-7)
            // Band 0 (always visible): threshold 0/7 = 0.0
            // Band 1: threshold 1/7 = 0.143
            // etc.
            float threshold = static_cast<float>(b) / 7.0f;

            // UI-only visibility tags are 9000 + band index
            Steinberg::int32 containerTag = 9000 + b;

            bandVisibilityControllers_[b] = new ContainerVisibilityController(
                &activeEditor_,
                bandCountParam,
                containerTag,
                threshold,
                false  // Show when value >= threshold
            );
        }
    }

    // Create expanded visibility controllers (T079, FR-015)
    // Show/hide BandStripExpanded based on Band*Expanded parameter
    for (int b = 0; b < kMaxBands; ++b) {
        auto* expandedParam = getParameterObject(
            makeBandParamId(static_cast<uint8_t>(b), BandParamType::kBandExpanded));
        if (expandedParam) {
            // UI tag for expanded container: 9100 + band index
            Steinberg::int32 expandedContainerTag = 9100 + b;

            expandedVisibilityControllers_[b] = new ContainerVisibilityController(
                &activeEditor_,
                expandedParam,
                expandedContainerTag,
                0.5f,   // Threshold at 0.5 (0 = collapsed, 1 = expanded)
                false   // Show when value >= threshold (i.e., when expanded)
            );
        }
    }
}

void Controller::willClose(VSTGUI::VST3Editor* editor) {
    // FR-024: Called when the editor is about to close
    // CRITICAL: Deactivate all visibility controllers BEFORE clearing them

    for (auto& vc : bandVisibilityControllers_) {
        if (vc) {
            if (auto* cvc = dynamic_cast<ContainerVisibilityController*>(vc.get())) {
                cvc->deactivate();
            }
            vc = nullptr;
        }
    }

    // T081: Deactivate expanded visibility controllers
    for (auto& vc : expandedVisibilityControllers_) {
        if (vc) {
            if (auto* cvc = dynamic_cast<ContainerVisibilityController*>(vc.get())) {
                cvc->deactivate();
            }
            vc = nullptr;
        }
    }

    spectrumDisplay_ = nullptr;
    activeEditor_ = nullptr;

    (void)editor;  // Suppress unused parameter warning
}

} // namespace Disrumpo
