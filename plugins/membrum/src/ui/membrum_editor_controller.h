// ==============================================================================
// MembrumEditorController -- Phase 6 editor sub-controller
// ==============================================================================
// Wires the kUiModeId parameter to the UIViewSwitchContainer's current-view
// index (Acoustic / Extended). IDependent-based so parameter automation
// reaches the UI without polling. Deregisters in the destructor.
// ==============================================================================

#pragma once

#include "base/source/fobject.h"
#include "pluginterfaces/base/ftypes.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

#include "vstgui/lib/vstguifwd.h"

namespace VSTGUI {
class VST3Editor;
class UIViewSwitchContainer;
} // namespace VSTGUI

namespace Steinberg::Vst { class EditController; }

namespace Membrum::UI {

/// Sub-controller attached to the UIDescription editor. Listens to
/// kUiModeId and drives the Acoustic / Extended view swap.
class MembrumEditorController final : public Steinberg::FObject
{
public:
    MembrumEditorController(VSTGUI::VST3Editor* editor,
                            Steinberg::Vst::EditController* editController) noexcept;
    ~MembrumEditorController() override;

    MembrumEditorController(const MembrumEditorController&) = delete;
    MembrumEditorController& operator=(const MembrumEditorController&) = delete;

    /// Called when the view-switch container hosting Acoustic/Extended
    /// becomes known to the sub-controller (via createSubController).
    void attachUiModeSwitch(VSTGUI::UIViewSwitchContainer* container) noexcept;

    /// FObject: receive parameter-change notifications.
    void PLUGIN_API update(FUnknown* changedUnknown, Steinberg::int32 message) override;

    OBJ_METHODS(MembrumEditorController, FObject)

private:
    VSTGUI::VST3Editor*              editor_         = nullptr;
    Steinberg::Vst::EditController*  editController_ = nullptr;
    VSTGUI::UIViewSwitchContainer*   uiModeSwitch_   = nullptr;

    // Cached Parameter pointer (not owned).
    Steinberg::Vst::Parameter* uiModeParam_ = nullptr;
};

} // namespace Membrum::UI
