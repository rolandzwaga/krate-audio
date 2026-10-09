// ==============================================================================
// Vorago - Edit Controller implementation   Plan section 2.6
// ==============================================================================
// T017: registration, formatting and setComponentState (Phase 11: 14 parameters).
// Phase 12 T033: registration, description and formatting of all 108 parameters
// (FR-041, SC-018) - global, macros, then the 14 packs in band order.
// T018: createView on the stock-view placeholder editor.uidesc (FR-054, FR-055).
// Phase 12 T034: IMidiMapping - CC64 and channel aftertouch (FR-031, SC-013 (5)).
// Phase 12 T042: setComponentState mirrors the v2 state (FR-040, plan 4.9); a v1
// stream returns every Phase 12 field to its registered default (C-7).
// Phase 13 T014: DataExchange consumer, custom views, sub-controller, preset
// browser overlay and providers, Gravity anchor-mode display (plan 5.1 - 5.6).
// ==============================================================================

#include "controller/controller.h"

#include "parameters/bloom_params.h"
#include "parameters/body_params.h"
#include "parameters/cloud_params.h"
#include "parameters/ecology_params.h"
#include "parameters/ecosystem_params.h"
#include "parameters/envelope_params.h"
#include "parameters/events_params.h"
#include "parameters/ghost_params.h"
#include "parameters/global_params.h"
#include "parameters/life_params.h"
#include "parameters/macro_params.h"
#include "parameters/noise_params.h"
#include "parameters/resonance_params.h"
#include "parameters/smear_params.h"
#include "parameters/space_params.h"
#include "parameters/sub_params.h"
#include "plugin_ids.h"
#include "preset/vorago_preset_config.h"
#include "ui/ecosystem_view.h"
#include "ui/panel_sub_controller.h"

#include "ui/arc_knob.h"
#include "ui/outline_button.h"
#include "ui/preset_browser_view.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "public.sdk/source/common/memorystream.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/crect.h"
#include "vstgui/uidescription/uiattributes.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// Compile touch point for the update config (FR-052, plan section 2.6): the
// header is included by nothing else, and a .h in a CMake source list is
// HEADER_FILE_ONLY, so this static_assert is the ONLY thing that compiles it.
// No UpdateChecker is ever instantiated (FR-052).
#include "update/vorago_update_config.h"
static_assert(std::is_same_v<decltype(Vorago::makeVoragoUpdateConfig()),
                             Krate::Plugins::UpdateCheckerConfig>);

namespace Vorago {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

// FR-080. kGravityTip is the SAME literal the uidesc gives the MacroGravity knob
// (T015): the uidesc attribute is the initial value, the controller sets it back.
constexpr const char* kGravityTip = "Gravity: pulls the resonances toward the harmonic grid";
constexpr const char* kGravityInertTip =
    "Gravity (inert): acts only when Resonance Anchor is Hybrid";
constexpr const char* kGravityLabel = "Gravity";
constexpr const char* kGravityInertLabel = "Gravity (inert)";

}  // namespace

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    const tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk) {
        return result;
    }

    // Band order == ascending ID (plan section 3.2): 6 + 12 + 90 = 108 (FR-041:
    // no soft-limit, nothing else).
    registerGlobalParams(parameters);     // 0-5
    registerMacroParams(parameters);      // 100-111
    registerCloudParams(parameters);      // 200-206
    registerNoiseParams(parameters);      // 300-353
    registerResonanceParams(parameters);  // 400-403
    registerEcologyParams(parameters);    // 500-515
    registerSubParams(parameters);        // 600-612
    registerSmearParams(parameters);      // 700-702
    registerEventsParams(parameters);     // 800
    registerEcosystemParams(parameters);  // 900
    registerBodyParams(parameters);       // 1000-1005
    registerSpaceParams(parameters);      // 1100-1115
    registerEnvelopeParams(parameters);   // 1200-1206
    registerBloomParams(parameters);      // 1300-1301
    registerGhostParams(parameters);      // 1400-1403
    registerLifeParams(parameters);       // 1500-1502

    // FR-050. No UpdateChecker (FR-052). Phase 13 (FR-072, FR-073): the browser
    // saves the processor's own getState bytes and loads with host edits.
    presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(makeVoragoPresetConfig(),
                                                                     nullptr, this);
    presetManager_->setStateProvider(
        [this]() -> IBStream* { return createComponentStateStream(); });
    presetManager_->setLoadProvider(
        [this](IBStream* stream, const Krate::Plugins::PresetInfo& /*info*/) {
            return loadComponentStateWithEdits(stream);
        });
    return kResultOk;
}

tresult PLUGIN_API Controller::terminate() {
    // A host that tears down without willClose must not leave this controller
    // registered as a dependent of a parameter it is about to destroy.
    if (anchorParamObserved_ != nullptr) {
        anchorParamObserved_->removeDependent(this);
        anchorParamObserved_ = nullptr;
    }
    presetManager_ = nullptr;
    return EditControllerEx1::terminate();
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state) {
    // FR-073: the body lives in applyStateStream; this path talks to no host.
    return applyStateStream(state, [this](ParamID id, double v) { setParamNormalized(id, v); });
}

bool Controller::loadComponentStateWithEdits(IBStream* state) {
    // FR-073: every restored value reaches the host as one Begin/Perform/End
    // triple, carrying the (clamped) value setParamNormalized stored. A stream
    // with version > kCurrentStateVersion is rejected before any setter runs.
    return applyStateStream(state, [this](ParamID id, double v) {
               beginEdit(id);
               setParamNormalized(id, v);
               performEdit(id, getParamNormalized(id));
               endEdit(id);
           }) == kResultOk;
}

IBStream* Controller::createComponentStateStream() {
    // Ruinae controller_presets.cpp:372-385: delegate to the processor via the
    // host's IComponent::getState - never re-serialize from the controller.
    FUnknownPtr<IComponent> component(getComponentHandler());
    if (!component) {
        return nullptr;
    }

    auto* stream = new MemoryStream();
    if (component->getState(stream) != kResultOk) {
        stream->release();
        return nullptr;
    }

    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return stream;
}

template <typename SetParam>
tresult Controller::applyStateStream(IBStream* state, const SetParam& setParam) {
    if (state == nullptr) {
        return kResultFalse;
    }

    IBStreamer streamer(state, kLittleEndian);

    int32 version = 0;
    if (!streamer.readInt32(version)) {
        return kResultFalse;
    }
    if (version > kCurrentStateVersion) {
        return kResultFalse;
    }

    // FR-047 / FR-040: the ...ToController helpers invert every processor mapping
    // and are EOF-safe, in getState's write order (plan 4.9). After a failed read
    // every later read fails too (the stream is at its end), matching the
    // processor's short-circuit chain. `setParam` is the caller's setter
    // (setComponentState: setParamNormalized; the load provider: with edits).
    const auto loadV2Tail = [&setParam](IBStreamer& in) {
        loadGlobalParamsV2ExtToController(in, setParam);
        loadCloudParamsToController(in, setParam);
        loadNoiseParamsToController(in, setParam);
        loadResonanceParamsToController(in, setParam);
        loadEcologyParamsToController(in, setParam);
        loadSubParamsToController(in, setParam);
        loadSmearParamsToController(in, setParam);
        loadEventsParamsToController(in, setParam);
        loadEcosystemParamsToController(in, setParam);
        loadBodyParamsToController(in, setParam);
        loadSpaceParamsToController(in, setParam);
        loadEnvelopeParamsToController(in, setParam);
        loadBloomParamsToController(in, setParam);
        loadGhostParamsToController(in, setParam);
        loadLifeParamsToController(in, setParam);
    };
    loadGlobalParamsToController(streamer, setParam);
    loadMacroParamsToController(streamer, setParam);
    if (version >= 2) {
        loadV2Tail(streamer);
    } else {
        // C-7 / FR-040 mirror of Processor::setState: a version < 2 stream leaves
        // every Phase 12 field at its registered default. The default-constructed
        // packs' v2 tail is serialized and fed through the same inverse mappings.
        auto tail = owned(new MemoryStream());
        IBStreamer out(tail, kLittleEndian);
        saveGlobalParamsV2Ext(GlobalParams{}, out);
        saveCloudParams(CloudParams{}, out);
        saveNoiseParams(NoiseParams{}, out);
        saveResonanceParams(ResonanceParams{}, out);
        saveEcologyParams(EcologyParams{}, out);
        saveSubParams(SubParams{}, out);
        saveSmearParams(SmearParams{}, out);
        saveEventsParams(EventsParams{}, out);
        saveEcosystemParams(EcosystemParams{}, out);
        saveBodyParams(BodyParams{}, out);
        saveSpaceParams(SpaceParams{}, out);
        saveEnvelopeParams(EnvelopeParams{}, out);
        saveBloomParams(BloomParams{}, out);
        saveGhostParams(GhostParams{}, out);
        saveLifeParams(LifeParams{}, out);
        tail->seek(0, IBStream::kIBSeekSet, nullptr);
        loadV2Tail(out);
    }
    // Phase 14 FR-072 / FR-074 mirror of Processor::setState: the v3 extension
    // (ecosystem roster) follows the life pack; an older stream returns both
    // roster fields to their registered defaults through the same inverse mapping.
    if (version >= 3) {
        loadEcosystemParamsV3ExtToController(streamer, setParam);
    } else {
        auto ext = owned(new MemoryStream());
        IBStreamer out(ext, kLittleEndian);
        saveEcosystemParamsV3Ext(EcosystemParams{}, out);
        ext->seek(0, IBStream::kIBSeekSet, nullptr);
        loadEcosystemParamsV3ExtToController(out, setParam);
    }

    return kResultOk;
}

tresult PLUGIN_API Controller::getParamStringByValue(ParamID tag, ParamValue valueNormalized,
                                                     String128 string) {
    // Each pack formats only its own continuous IDs (kResultFalse otherwise).
    using Formatter = tresult (*)(ParamID, ParamValue, String128);
    static constexpr std::array<Formatter, 16> kFormatters = {
        formatGlobalParam,    formatMacroParam,     formatCloudParam,    formatNoiseParam,
        formatResonanceParam, formatEcologyParam,   formatSubParam,      formatSmearParam,
        formatEventsParam,    formatEcosystemParam, formatBodyParam,     formatSpaceParam,
        formatEnvelopeParam,  formatBloomParam,     formatGhostParam,    formatLifeParam};
    for (const Formatter format : kFormatters) {
        if (format(tag, valueNormalized, string) == kResultOk) {
            return kResultOk;
        }
    }
    // The StringListParameters (polyphony, seed, noise model/type, anchor, loop
    // filters, materials, freeze, envelope mode, ghost triggers) format themselves.
    return EditControllerEx1::getParamStringByValue(tag, valueNormalized, string);
}

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    // Model plugins/seraphis/src/controller/controller.cpp:434-436. The custom
    // views and the sub-controller come from the VST3EditorDelegate hooks below.
    if (FIDStringsEqual(name, Vst::ViewType::kEditor)) {
        return new VSTGUI::VST3Editor(this, "editor", "editor.uidesc");
    }
    return nullptr;
}

tresult PLUGIN_API Controller::getMidiControllerAssignment(int32 busIndex, int16 /*channel*/,
                                                           CtrlNumber midiControllerNumber,
                                                           ParamID& id) {
    // Model plugins/disrumpo/src/controller/controller.cpp:711-726. Bus 0 only;
    // every channel maps to the same two performance parameters (FR-031).
    if (busIndex != 0) {
        return kResultFalse;
    }
    if (midiControllerNumber == kCtrlSustainOnOff) {
        id = kSustainPedalId;
        return kResultOk;
    }
    if (midiControllerNumber == kAfterTouch) {
        id = kChannelPressureId;
        return kResultOk;
    }
    return kResultFalse;
}

// ==============================================================================
// Phase 13 - DataExchange consumer (FR-040, plan 5.2)
// ==============================================================================

void PLUGIN_API Controller::queueOpened(DataExchangeUserContextID /*userContextID*/,
                                        uint32 /*blockSize*/, TBool& dispatchOnBackgroundThread) {
    // UI-thread dispatch: cachedFrame_ is written and read on the UI thread only,
    // so it needs no lock (Membrum controller.cpp:1697-1706).
    dispatchOnBackgroundThread = static_cast<TBool>(false);
}

void PLUGIN_API Controller::queueClosed(DataExchangeUserContextID /*userContextID*/) {
    // Nothing to release: cachedFrame_ is a POD value.
}

void PLUGIN_API Controller::onDataExchangeBlocksReceived(DataExchangeUserContextID userContextID,
                                                         uint32 numBlocks,
                                                         DataExchangeBlock* blocks,
                                                         TBool /*onBackgroundThread*/) {
    if (userContextID != static_cast<DataExchangeUserContextID>(kEcosystemFrameUserContextId) ||
        blocks == nullptr) {
        return;
    }
    // The newest valid block wins; older ones are stale (Membrum controller.cpp:1712-1726).
    for (uint32 i = 0; i < numBlocks; ++i) {
        if (blocks[i].data != nullptr && blocks[i].size >= sizeof(EcosystemFrame)) {
            std::memcpy(&cachedFrame_, blocks[i].data, sizeof(EcosystemFrame));
        }
    }
}

tresult PLUGIN_API Controller::notify(IMessage* message) {
    // The IMessage fallback for hosts without the DataExchange API: the SDK
    // helper decodes it into onDataExchangeBlocksReceived. Everything else goes
    // to the base class unchanged.
    if (message != nullptr && dataExchangeReceiver_.onMessage(message)) {
        return kResultOk;
    }
    return EditControllerEx1::notify(message);
}

// ==============================================================================
// Phase 13 - VST3EditorDelegate: custom views, sub-controller, lifecycle (plan 5.4)
// ==============================================================================

VSTGUI::CView* Controller::createCustomView(VSTGUI::UTF8StringPtr name,
                                            const VSTGUI::UIAttributes& attributes,
                                            const VSTGUI::IUIDescription* /*description*/,
                                            VSTGUI::VST3Editor* /*editor*/) {
    if (name == nullptr) {
        return nullptr;
    }

    // The view factory does not decorate a createCustomView view with the node's
    // geometry, so the rect is built from the attributes (Seraphis controller.cpp:453-460).
    VSTGUI::CPoint origin(0.0, 0.0);
    VSTGUI::CPoint size(100.0, 100.0);
    attributes.getPointAttribute("origin", origin);
    attributes.getPointAttribute("size", size);
    const VSTGUI::CRect viewRect(origin.x, origin.y, origin.x + size.x, origin.y + size.y);

    if (std::strcmp(name, "EcosystemView") == 0) {
        ecosystemView_ = new UI::EcosystemView(viewRect, &cachedFrame_);
        return ecosystemView_;
    }
    if (std::strcmp(name, "PresetBrowserButton") == 0) {
        // Plan D-1: the shared outline button; VoragoPanelSubController gives it
        // its session tag and listener in verifyView.
        return new Krate::Plugins::OutlineBrowserButton(viewRect, nullptr, -1, "PRESETS");
    }
    return nullptr;
}

VSTGUI::IController* Controller::createSubController(VSTGUI::UTF8StringPtr name,
                                                     const VSTGUI::IUIDescription* /*description*/,
                                                     VSTGUI::VST3Editor* editor) {
    if (name == nullptr || std::strcmp(name, "VoragoPanel") != 0) {
        return nullptr;
    }
    // Owned by the template root view (uidescription.cpp:741-755).
    return new UI::VoragoPanelSubController(this, editor);
}

void Controller::didOpen(VSTGUI::VST3Editor* editor) {
    activeEditor_ = editor;

    // FR-070 / FR-071: the browser overlay lives in the frame, closed until the
    // header button opens it. R-2: its own save dialog is the save path.
    VSTGUI::CFrame* frame = (editor != nullptr) ? editor->getFrame() : nullptr;
    if (frame != nullptr && presetManager_ != nullptr && presetBrowserView_ == nullptr) {
        presetBrowserView_ = new Krate::Plugins::PresetBrowserView(
            frame->getViewSize(), presetManager_.get(), makeVoragoPresetTabLabels());
        frame->addView(presetBrowserView_);
    }

    // FR-081: observe the anchor mode, and show the value already current now.
    if (anchorParamObserved_ == nullptr) {
        anchorParamObserved_ = parameters.getParameter(kResonanceAnchorModeId);
        if (anchorParamObserved_ != nullptr) {
            anchorParamObserved_->addDependent(this);
        }
    }
    refreshGravityDisplay();
}

void Controller::willClose(VSTGUI::VST3Editor* /*editor*/) {
    // The dependent goes first, so no update can reach a view pointer that is
    // about to dangle.
    if (anchorParamObserved_ != nullptr) {
        anchorParamObserved_->removeDependent(this);
        anchorParamObserved_ = nullptr;
    }
    if (presetBrowserView_ != nullptr) {
        if (presetBrowserView_->isOpen()) {
            // Unhooks the frame's keyboard hook while the frame is still alive
            // (Seraphis controller.cpp:529-533).
            presetBrowserView_->close();
        }
        presetBrowserView_ = nullptr;  // the frame owns and destroys it
    }
    ecosystemView_ = nullptr;
    gravityKnob_ = nullptr;
    gravityLabel_ = nullptr;
    activeEditor_ = nullptr;
}

// ==============================================================================
// Phase 13 - session surface the sub-controller drives (C-5)
// ==============================================================================

void Controller::setActivePage(int page) noexcept {
    activePage_ = std::clamp(page, 0, UI::kVoragoPageCount - 1);
}

void Controller::openPresetBrowser() {
    if (presetBrowserView_ == nullptr || presetBrowserView_->isOpen()) {
        return;
    }
    presetBrowserView_->open();
}

void Controller::registerGravityViews(Krate::Plugins::ArcKnob* knob,
                                      VSTGUI::CTextLabel* label) noexcept {
    if (knob != nullptr) {
        gravityKnob_ = knob;
    }
    if (label != nullptr) {
        gravityLabel_ = label;
    }
}

// ==============================================================================
// Phase 13 - Gravity anchor-mode display (FR-080, FR-081, plan 5.6)
// ==============================================================================

void PLUGIN_API Controller::update(FUnknown* changedUnknown, int32 message) {
    EditControllerEx1::update(changedUnknown, message);
    if (anchorParamObserved_ != nullptr && message == IDependent::kChanged &&
        FCast<Parameter>(changedUnknown) == anchorParamObserved_) {
        refreshGravityDisplay();
    }
}

void Controller::refreshGravityDisplay() noexcept {
    // Display only: no alpha, no mouse-enable change, no parameter write (FR-080).
    const int mode =
        indexFromNormalized(getParamNormalized(kResonanceAnchorModeId), kNumResonanceAnchorModes);
    gravityInert_ = (mode != kResonanceAnchorModeDefault);  // the default IS Hybrid

    if (gravityKnob_ != nullptr) {
        gravityKnob_->setTooltipText(gravityInert_ ? kGravityInertTip : kGravityTip);
    }
    if (gravityLabel_ != nullptr) {
        gravityLabel_->setText(gravityInert_ ? kGravityInertLabel : kGravityLabel);
        gravityLabel_->invalid();
    }
}

}  // namespace Vorago
