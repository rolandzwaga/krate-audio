#pragma once

// ==============================================================================
// Vorago - Edit Controller (UI thread)   Plan section 2.6
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation. This header includes
// NO processor header except the POD processor/ecosystem_frame.h (FR-020).
//
// IMidiMapping (Phase 12 T034, FR-031): on bus 0, any channel, CC64
// (kCtrlSustainOnOff) maps to kSustainPedalId and channel aftertouch
// (kAfterTouch) maps to kChannelPressureId; every other controller is
// unmapped. NO INoteExpressionController (FR-019, OQ-7 ruling).
//
// Phase 13 (specs/vorago-phase13-ui, plan section 5):
//   - IDataExchangeReceiver consumer of the one-way 'VECO' EcosystemFrame queue
//     (FR-040): the newest valid block is cached on the UI thread, nothing else;
//     the IMessage fallback arrives through notify().
//   - createCustomView creates the EcosystemView and the header
//     "PresetBrowserButton" (a shared OutlineBrowserButton, FR-042, FR-071).
//   - createSubController("VoragoPanel") returns VoragoPanelSubController, which
//     routes the session-tag controls (page strip, preset button) and captures
//     the Gravity macro views (FR-043).
//   - The PresetBrowserView overlay is built in didOpen and dropped in willClose;
//     the preset providers save via IComponent::getState and load with host
//     edits (FR-070 - FR-073).
//   - IDependent observer of kResonanceAnchorModeId: the Gravity macro's label
//     and tooltip show "inert" outside Hybrid (FR-080, FR-081). Display only.
// ==============================================================================

#include "preset/preset_manager.h"
#include "processor/ecosystem_frame.h"  // POD only (FR-020)

#include "pluginterfaces/vst/ivstdataexchange.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/utility/dataexchange.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include <memory>

namespace Krate::Plugins {
class PresetBrowserView;
class ArcKnob;
}  // namespace Krate::Plugins

namespace VSTGUI {
class CTextLabel;
}  // namespace VSTGUI

namespace Vorago::UI {
class EcosystemView;
}  // namespace Vorago::UI

namespace Vorago {

class Controller : public Steinberg::Vst::EditControllerEx1,
                   public Steinberg::Vst::IMidiMapping,
                   public Steinberg::Vst::IDataExchangeReceiver,
                   public VSTGUI::VST3EditorDelegate {
public:
    Controller() = default;
    ~Controller() override = default;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue valueNormalized,
        Steinberg::Vst::String128 string) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

    // IMidiMapping (FR-031)
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(
        Steinberg::int32 busIndex, Steinberg::int16 channel,
        Steinberg::Vst::CtrlNumber midiControllerNumber, Steinberg::Vst::ParamID& id) override;

    // IDataExchangeReceiver (FR-040)
    void PLUGIN_API queueOpened(Steinberg::Vst::DataExchangeUserContextID userContextID,
                                Steinberg::uint32 blockSize,
                                Steinberg::TBool& dispatchOnBackgroundThread) override;
    void PLUGIN_API queueClosed(Steinberg::Vst::DataExchangeUserContextID userContextID) override;
    void PLUGIN_API onDataExchangeBlocksReceived(
        Steinberg::Vst::DataExchangeUserContextID userContextID, Steinberg::uint32 numBlocks,
        Steinberg::Vst::DataExchangeBlock* blocks, Steinberg::TBool onBackgroundThread) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    // IDependent (FR-081): EditControllerEx1::update runs first.
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override;

    // VST3EditorDelegate (FR-042, FR-043, FR-071)
    VSTGUI::CView* createCustomView(VSTGUI::UTF8StringPtr name,
                                    const VSTGUI::UIAttributes& attributes,
                                    const VSTGUI::IUIDescription* description,
                                    VSTGUI::VST3Editor* editor) override;
    VSTGUI::IController* createSubController(VSTGUI::UTF8StringPtr name,
                                             const VSTGUI::IUIDescription* description,
                                             VSTGUI::VST3Editor* editor) override;
    void didOpen(VSTGUI::VST3Editor* editor) override;
    void willClose(VSTGUI::VST3Editor* editor) override;

    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
        DEF_INTERFACE(Steinberg::Vst::IDataExchangeReceiver)
    END_DEFINE_INTERFACES(EditControllerEx1)

    DELEGATE_REFCOUNT(EditControllerEx1)

    // Session surface the sub-controller drives (C-5; never a ParamID, never persisted)
    [[nodiscard]] int activePage() const noexcept { return activePage_; }
    void setActivePage(int page) noexcept;  // clamps to [0, 6]
    void openPresetBrowser();
    /// Merges the non-null arguments into the captured Gravity views.
    void registerGravityViews(Krate::Plugins::ArcKnob* knob, VSTGUI::CTextLabel* label) noexcept;
    void refreshGravityDisplay() noexcept;  // FR-080

    // Preset providers (FR-072, FR-073) - public for SC-023
    /// The processor's own getState bytes via the host's IComponent; the caller
    /// owns the result (null when the host exposes no IComponent).
    [[nodiscard]] Steinberg::IBStream* createComponentStateStream();
    /// setComponentState with one begin/perform/end edit per restored value.
    bool loadComponentStateWithEdits(Steinberg::IBStream* state);

    // Test seams
    [[nodiscard]] Krate::Plugins::PresetManager* presetManagerForTest() const noexcept {
        return presetManager_.get();  // model plugins/seraphis/src/controller/controller.h:196
    }
    [[nodiscard]] const EcosystemFrame& cachedEcosystemFrame() const noexcept { return cachedFrame_; }
    [[nodiscard]] const UI::EcosystemView* ecosystemViewForTest() const noexcept {
        return ecosystemView_;
    }
    [[nodiscard]] bool gravityDisplayInertForTest() const noexcept { return gravityInert_; }
    [[nodiscard]] const Krate::Plugins::PresetBrowserView* presetBrowserViewForTest() const noexcept {
        return presetBrowserView_;
    }

private:
    template <typename SetParam>
    Steinberg::tresult applyStateStream(Steinberg::IBStream* state, const SetParam& setParam);

    std::unique_ptr<Krate::Plugins::PresetManager> presetManager_;

    EcosystemFrame cachedFrame_{};  // UI thread only (queueOpened asks for UI dispatch)
    Steinberg::Vst::DataExchangeReceiverHandler dataExchangeReceiver_{this};

    // Frame-owned views; every pointer is zeroed in willClose.
    UI::EcosystemView* ecosystemView_ = nullptr;
    Krate::Plugins::PresetBrowserView* presetBrowserView_ = nullptr;
    VSTGUI::VST3Editor* activeEditor_ = nullptr;
    Krate::Plugins::ArcKnob* gravityKnob_ = nullptr;
    VSTGUI::CTextLabel* gravityLabel_ = nullptr;

    Steinberg::Vst::Parameter* anchorParamObserved_ = nullptr;  // non-null only while a dependent
    int activePage_ = 0;
    bool gravityInert_ = false;
};

}  // namespace Vorago
