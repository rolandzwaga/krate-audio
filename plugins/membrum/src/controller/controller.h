#pragma once

// ==============================================================================
// Membrum Controller -- Phase 4 (per-pad parameters + proxy logic)
// ==============================================================================

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "dsp/pad_config.h"

#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/lib/cvstguitimer.h"

#include <array>

namespace Steinberg { class IBStream; }

namespace Membrum {

class Controller : public Steinberg::Vst::EditControllerEx1,
                    public VSTGUI::VST3EditorDelegate
{
public:
    Controller() = default;

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
};

} // namespace Membrum
