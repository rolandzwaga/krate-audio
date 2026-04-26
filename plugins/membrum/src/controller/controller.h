#pragma once

// ==============================================================================
// Membrum Controller -- Phase 4 (per-pad parameters + proxy logic)
// ==============================================================================

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/utility/dataexchange.h"
#include "pluginterfaces/vst/ivstdataexchange.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "dsp/pad_config.h"
#include "dsp/pad_glow_publisher.h"
#include "processor/meters_block.h"

#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/iviewlistener.h"
#include "../ui/membrum_editor_controller.h"

#include <array>
#include <memory>
#include <string>

namespace Krate::Plugins {
class PresetManager;
class PresetBrowserView;
class SavePresetDialogView;
} // namespace Krate::Plugins

namespace Steinberg { class IBStream; }

namespace Membrum::State { struct KitSnapshot; }
namespace Membrum::UI { class PadGridView; class KitMetersView; class OutlineActionButton; class InlinePresetBrowserView; class ADSRExpandedOverlayView; }
namespace Krate::Plugins { class PitchEnvelopeDisplay; class XYMorphPad; class ADSRDisplay; }

namespace VSTGUI { class CTextLabel; class CControl; }

namespace Membrum {

class Controller : public Steinberg::Vst::EditControllerEx1,
                    public VSTGUI::VST3EditorDelegate,
                    public Steinberg::Vst::IDataExchangeReceiver,
                    public VSTGUI::ViewListenerAdapter
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

    // IMessage receiver pass-through (DataExchange IMessage fallback).
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

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

    // ViewListenerAdapter override: VSTGUI calls this just before destroying a
    // view we registered as a listener on. We use it to clear any cached raw
    // pointer matching the dying view -- otherwise quickly switching the
    // SelectedPad{Simple,Advanced} templates leaves dangling pointers in
    // {xyMorphPad_, morph*View_, filterEnv*Display_, pitchEnvelopeDisplay_,
    // outputBusSelView_, ...}, and the next setParamNormalized() crashes.
    void viewWillDelete(VSTGUI::CView* view) override;

    // Phase 4: Override setParamNormalized to implement proxy logic
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
                                                      Steinberg::Vst::ParamValue value) override;

    // Override to produce human-readable value-popup strings for the ArcKnobs
    // in the Acoustic view (Hz / dB / bipolar % / Wood-Metal labels, etc.).
    // Falls back to EditControllerEx1's default formatting for all other tags.
    Steinberg::tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID tag,
        Steinberg::Vst::ParamValue valueNormalized,
        Steinberg::Vst::String128 string) override;

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

    // T056: latched preset-load failure status (read by tests / UI). Cleared
    // after the status label countdown expires (see poll timer).
    [[nodiscard]] bool kitPresetLoadFailed() const noexcept { return kitPresetLoadFailed_; }
    [[nodiscard]] bool padPresetLoadFailed() const noexcept { return padPresetLoadFailed_; }

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

    // Phase 6 sub-controller: listens to kUiModeId and drives the
    // Acoustic/Extended UIViewSwitchContainer. Created in didOpen();
    // released in willClose() (FObject refcount).
    Steinberg::IPtr<Membrum::UI::MembrumEditorController> editorSubController_;

    // Phase 6 (T042): raw pointer to the active PadGridView. Lifetime is
    // owned by VSTGUI's view tree; zeroed in willClose().
    Membrum::UI::PadGridView*        padGridView_    = nullptr;

    // Phase 6 (T046): cached view pointers (lifetime owned by VSTGUI view
    // tree; zeroed in willClose()). The KitMetersView is constructed from
    // createCustomView; the CPU label is a CTextLabel discovered via
    // verifyView() by matching its initial title prefix "CPU".
    Membrum::UI::KitMetersView*      kitMetersView_  = nullptr;

    // Phase 9 (T080 / US8): raw pointer to the active PitchEnvelopeDisplay.
    // The view is now a shared, registered VSTGUI class instantiated by the
    // uidesc factory; we cache a pointer via verifyView() and wire the edit
    // callbacks there. Lifetime is owned by VSTGUI's view tree; zeroed in
    // willClose().
    Krate::Plugins::PitchEnvelopeDisplay* pitchEnvelopeDisplay_ = nullptr;

    // Mode toggle button (Acoustic / Extended). Lifetime owned by VSTGUI view tree.
    Membrum::UI::OutlineActionButton*  modeToggleButton_ = nullptr;

    VSTGUI::CTextLabel*              cpuLabel_       = nullptr;

    // T060/T062 (Phase 6 / US5): active-voices readout label. Discovered in
    // verifyView() by the title prefix "ActiveVoices". Updated on the 30 Hz
    // timer from cachedMeters_.activeVoices. Tolerant of a missing label.
    VSTGUI::CTextLabel*              activeVoicesLabel_ = nullptr;

    // T056: small status label surfacing preset load failures. Discovered in
    // verifyView() by its title prefix "PresetStatus". The 30 Hz poll timer
    // inspects kitPresetLoadFailed_ / padPresetLoadFailed_, sets the label
    // text on failure, and clears it after ~3 seconds. Tolerant of a missing
    // label view (templates without a PresetStatus label simply see no-ops).
    VSTGUI::CTextLabel*              presetStatusLabel_ = nullptr;

    // Phase 8 (T074 / US7 / FR-066): cached pointer to the Output Bus
    // selector COptionMenu (discovered via verifyView by matching
    // control-tag == kOutputBusId). Used to set a warning tooltip when the
    // selected aux bus is not activated by the host. Lifetime owned by
    // VSTGUI; zeroed in willClose().
    VSTGUI::CControl*                outputBusSelView_ = nullptr;

    // Material Morph section views (Advanced template). Cached in verifyView()
    // so that toggling the MorphEnabled (power) button can cascade a
    // disabled/dimmed visual state across every control inside the fieldset.
    // All four pointers are owned by VSTGUI's view tree; willClose() zeros them.
    Krate::Plugins::XYMorphPad*      xyMorphPad_           = nullptr;
    VSTGUI::CControl*                morphDurationView_    = nullptr;
    VSTGUI::CControl*                morphCurveView_       = nullptr;
    VSTGUI::CTextLabel*              morphDurLabel_        = nullptr;

    // Tone Shaper Filter ADSR display (Advanced template). Cached in
    // verifyView() so setParamNormalized() / syncGlobalProxyFromPad can push
    // the current attack/decay/sustain/release values into the display.
    // Membrum's filter envelope uses asymmetric linear ranges (attack x500 ms,
    // decay/release x2000 ms, see processor.cpp:72-75). The shared
    // ADSRDisplay accepts per-segment max-time overrides via setAttackMaxMs /
    // setDecayMaxMs / setReleaseMaxMs, which align its cubic drag->normalised
    // encoding with those DSP ranges so drag edits round-trip correctly.
    Krate::Plugins::ADSRDisplay*     filterEnvDisplay_     = nullptr;

    // Expanded overlay for the Tone Shaper filter envelope. Opened via the
    // FilterEnvExpandButton in the inline section; the overlay hosts its own
    // large ADSRDisplay wired to the same param IDs and max-time ranges as
    // the inline display. Owned by the frame; nulled in willClose.
    Membrum::UI::ADSRExpandedOverlayView* filterEnvOverlay_         = nullptr;
    Krate::Plugins::ADSRDisplay*          filterEnvOverlayDisplay_  = nullptr;

    /// Reflect the current kMorphEnabledId value onto the cached Material
    /// Morph views: ON -> alpha 1.0, mouse enabled; OFF -> alpha 0.35, mouse
    /// disabled. Tolerant of missing views (safe before didOpen / after
    /// willClose). Called from didOpen, syncGlobalProxyFromPad, and
    /// setParamNormalized on kMorphEnabledId writes.
    void updateMorphControlsEnabled() noexcept;

    /// Push the current tone-shaper filter-envelope norm values into the
    /// cached ADSRDisplay, converting to true DSP milliseconds (attack x500,
    /// decay/release x2000) so the displayed shape and time labels match
    /// what the processor actually applies. Tolerant of a null display.
    void updateFilterEnvDisplay() noexcept;

    /// Phase 8F: push the per-pad enable flags from a freshly-loaded
    /// KitSnapshot into the PadGridView mirror. Both load paths
    /// (setComponentState, kitPresetLoadProvider) write through setters
    /// that bypass the derived setParamNormalized override, so the per-pad
    /// enable hook in the override never fires during preset load --
    /// hence this explicit push. Tolerant of a null grid view.
    void pushKitEnabledToGrid(const Membrum::State::KitSnapshot& kit) noexcept;

    /// Phase 8 (T074 / US7 / FR-066): push a warning tooltip onto the cached
    /// Output Bus selector view when the selected aux bus index >= 1 and the
    /// corresponding bus is not host-activated. Clears the tooltip otherwise.
    void updateOutputBusTooltip() noexcept;
    // Frames-until-clear counter for the status label. Decremented by the
    // poll timer; when it reaches 0 the label text is cleared and the
    // failure flags reset.
    int                              presetStatusClearTicks_ = 0;

    // Phase 6 (T046): last MetersBlock received via DataExchange. Updated on
    // the UI thread by onDataExchangeBlocksReceived(); read by the 30 Hz
    // poll timer to push values into the Kit Column meter/CPU views.
    MetersBlock                      cachedMeters_{};

    // Controller-side mirror of the processor's PadGlowPublisher. The real
    // publisher lives on the audio thread inside the Processor; the UI cannot
    // reach it across the separate-component boundary. Each MetersBlock we
    // receive carries a snapshot of the publisher's buckets, which we re-apply
    // here so PadGridView's existing publisher-based glow path keeps working.
    PadGlowPublisher                 padGlowMirror_{};

    // SDK helper that routes host DataExchange deliveries (and the IMessage
    // fallback for hosts without the DataExchange API) into this controller's
    // IDataExchangeReceiver entry points. Without this member the receiver
    // interface is advertised but never actually invoked.
    Steinberg::Vst::DataExchangeReceiverHandler dataExchangeReceiver_{this};

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

    // Inline kit-column browser widgets (name + prev/next/Browse). Constructed
    // by createCustomView() for the "KitBrowserView" and "PadBrowserView"
    // uidesc slots. Lifetime owned by VSTGUI's view tree; zeroed in
    // willClose(). The Browse button opens the corresponding modal overlay;
    // the load providers push the loaded preset name back into these widgets
    // so prev/next cycling and the modal load both keep the name display in
    // sync.
    Membrum::UI::InlinePresetBrowserView* kitInlineBrowser_ = nullptr;
    Membrum::UI::InlinePresetBrowserView* padInlineBrowser_ = nullptr;
    std::string lastKitPresetName_;
    std::string lastPadPresetName_;

    // T056: latched flag indicating the most recent PresetManager load failed
    // (e.g. malformed/truncated blob). Tests can poll this; UI surfaces it via
    // the browser view's error indicator.
    bool padPresetLoadFailed_ = false;
    bool kitPresetLoadFailed_ = false;
};

} // namespace Membrum
