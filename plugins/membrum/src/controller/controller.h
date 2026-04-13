#pragma once

// ==============================================================================
// Membrum Controller -- Phase 4 (per-pad parameters + proxy logic)
// ==============================================================================

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/vst/ivstdataexchange.h"
#include "dsp/pad_config.h"
#include "processor/meters_block.h"

#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/lib/cvstguitimer.h"

#include <array>
#include <memory>

namespace Krate::Plugins {
class PresetManager;
class PresetBrowserView;
class SavePresetDialogView;
} // namespace Krate::Plugins

namespace Steinberg { class IBStream; }

namespace Membrum::UI { class PadGridView; class KitMetersView; }

namespace VSTGUI { class CTextLabel; }

namespace Membrum {

class Controller : public Steinberg::Vst::EditControllerEx1,
                    public VSTGUI::VST3EditorDelegate,
                    public Steinberg::Vst::IDataExchangeReceiver
{
public:
    Controller();
    ~Controller() override;

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView(const char* name) override;

    // ==========================================================================
    // Phase 6 (T028) -- IVST3EditorDelegate hooks.
    // VST3Editor auto-discovers the delegate via dynamic_cast on the controller
    // supplied to its constructor, so these methods are called by the editor.
    // ==========================================================================
    VSTGUI::CView* createCustomView(
        VSTGUI::UTF8StringPtr name,
        const VSTGUI::UIAttributes& attributes,
        const VSTGUI::IUIDescription* description,
        VSTGUI::VST3Editor* editor) override;

    void didOpen(VSTGUI::VST3Editor* editor) override;
    void willClose(VSTGUI::VST3Editor* editor) override;

    // T046: capture references to named/tagged views as the editor builds the
    // view tree so the 30 Hz timer can push MetersBlock values into them.
    VSTGUI::CView* verifyView(VSTGUI::CView* view,
                              const VSTGUI::UIAttributes& attributes,
                              const VSTGUI::IUIDescription* description,
                              VSTGUI::VST3Editor* editor) override;

    // Phase 4: Override setParamNormalized to implement proxy logic
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
                                                      Steinberg::Vst::ParamValue value) override;

    // Phase 4: Track bus activation state (FR-043).
    // Note: In VST3 separate-component mode, activateBus is only called on the
    // processor (IComponent). The controller can be notified via IMessage or
    // by the host syncing component state. This method is provided for testing
    // and can be called when bus activation changes are communicated.
    void notifyBusActivation(Steinberg::int32 busIndex, bool active);

    /// Query whether a specific output bus is currently active.
    [[nodiscard]] bool isBusActive(int busIndex) const noexcept
    {
        if (busIndex < 0 || busIndex >= static_cast<int>(busActive_.size()))
            return false;
        return busActive_[static_cast<std::size_t>(busIndex)];
    }

    // Phase 4: kit preset providers (FR-052)
    /// Produces a 9036-byte kit preset blob (v4 without selectedPadIndex)
    Steinberg::IBStream* kitPresetStateProvider();
    /// Loads a 9036-byte kit preset blob and syncs all controller params
    bool kitPresetLoadProvider(Steinberg::IBStream* stream);

    // Phase 6 (T046): IDataExchangeReceiver for MetersBlock.
    void PLUGIN_API queueOpened(
        Steinberg::Vst::DataExchangeUserContextID userContextID,
        Steinberg::uint32 blockSize,
        Steinberg::TBool& dispatchOnBackgroundThread) override;
    void PLUGIN_API queueClosed(
        Steinberg::Vst::DataExchangeUserContextID userContextID) override;
    void PLUGIN_API onDataExchangeBlocksReceived(
        Steinberg::Vst::DataExchangeUserContextID userContextID,
        Steinberg::uint32 numBlocks,
        Steinberg::Vst::DataExchangeBlock* blocks,
        Steinberg::TBool onBackgroundThread) override;

    // --- Interface support (Phase 6) ---
    OBJ_METHODS(Controller, EditControllerEx1)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IDataExchangeReceiver)
    END_DEFINE_INTERFACES(EditControllerEx1)
    DELEGATE_REFCOUNT(EditControllerEx1)

    // Test-only accessors for cached meters.
    [[nodiscard]] const MetersBlock& cachedMetersForTest() const noexcept { return cachedMeters_; }

    // Phase 4: pad preset providers (FR-060 through FR-063)
    /// Produces a 284-byte pad preset blob for the currently selected pad
    Steinberg::IBStream* padPresetStateProvider();
    /// Loads a 284-byte pad preset blob and applies to the currently selected pad only
    bool padPresetLoadProvider(Steinberg::IBStream* stream);

private:
    // Phase 4: selected pad proxy logic helpers
    void syncGlobalProxyFromPad(int padIndex);
    void forwardGlobalToPad(Steinberg::Vst::ParamID globalId,
                            Steinberg::Vst::ParamValue value);

    int selectedPadIndex_ = 0;
    bool suppressProxyForward_ = false;  // prevent re-entrancy during pad switch
    std::array<bool, kMaxOutputBuses> busActive_ = {true}; // bus 0 always active

    // Phase 6 (T028): editor lifecycle tracking. Raw pointers are owned by the
    // host / VSTGUI; we only cache them for use inside the 30 Hz poll timer.
    // Zeroed in willClose() so we never dereference a dead view (SC-014).
    VSTGUI::VST3Editor*              activeEditor_   = nullptr;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> pollTimer_;

    // Phase 6 (T042): raw pointer to the active PadGridView. Lifetime is
    // owned by VSTGUI's view tree; zeroed in willClose().
    Membrum::UI::PadGridView*        padGridView_    = nullptr;

    // Phase 6 (T046): cached view pointers (lifetime owned by VSTGUI view
    // tree; zeroed in willClose()). The KitMetersView is constructed from
    // createCustomView; the CPU label is a CTextLabel discovered via
    // verifyView() by matching its initial title prefix "CPU".
    Membrum::UI::KitMetersView*      kitMetersView_  = nullptr;
    VSTGUI::CTextLabel*              cpuLabel_       = nullptr;

    // Phase 6 (T046): last MetersBlock received via DataExchange. Updated on
    // the UI thread by onDataExchangeBlocksReceived(); read by the 30 Hz
    // poll timer to push values into the Kit Column meter/CPU views.
    MetersBlock                      cachedMeters_{};

    /// T046: push `cachedMeters_` values into the kit-column meter / CPU label
    /// views. Tolerant of missing views (safe when editor is not open).
    void updateMeterViews(const MetersBlock& meters) noexcept;

    /// T054: after a per-pad preset load, force the selected pad's MacroMapper
    /// to re-apply its current macro values on top of the freshly loaded
    /// underlying parameters. Implemented as a no-op-valued performEdit on
    /// each of the five macro params -- the processor's processParameterChanges
    /// path detects the parameter event and calls `macroMapper_.apply()` so
    /// underlying tension/cutoff/etc. reflect macro positions after the load.
    void triggerSelectedPadMacroReapply();

    // ==========================================================================
    // Phase 6 (T052..T056): two PresetManager instances (kit + per-pad scope).
    // Created in initialize(); destroyed in terminate(). Two corresponding
    // PresetBrowserView + SavePresetDialogView instances are added to the
    // frame in didOpen() and zeroed in willClose() (VSTGUI owns the views).
    // ==========================================================================
    std::unique_ptr<Krate::Plugins::PresetManager> kitPresetManager_;
    std::unique_ptr<Krate::Plugins::PresetManager> padPresetManager_;
    Krate::Plugins::PresetBrowserView*    kitPresetBrowserView_ = nullptr;
    Krate::Plugins::PresetBrowserView*    padPresetBrowserView_ = nullptr;
    Krate::Plugins::SavePresetDialogView* kitSaveDialogView_    = nullptr;
    Krate::Plugins::SavePresetDialogView* padSaveDialogView_    = nullptr;

    // T056: latched flag indicating the most recent PresetManager load failed
    // (e.g. malformed/truncated blob). Tests can poll this; UI surfaces it via
    // the browser view's error indicator.
    bool padPresetLoadFailed_ = false;
    bool kitPresetLoadFailed_ = false;
};

} // namespace Membrum
