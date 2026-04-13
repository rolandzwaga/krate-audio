// ==============================================================================
// MembrumEditorController -- Phase 6 editor sub-controller
// ==============================================================================
// Spec: specs/141-membrum-phase6-ui/spec.md (FR-030, FR-031, FR-001)
// Data model: specs/141-membrum-phase6-ui/data-model.md sections 7, 8
//
// Wires the kUiModeId parameter to the UIViewSwitchContainer's current-view
// index (Acoustic/Extended) and the kEditorSizeId parameter to the
// VST3Editor::exchangeView() call that swaps the top-level template.
// Both wirings are IDependent-based so parameter automation reaches the UI
// without polling. Deregisters in the destructor to prevent use-after-free
// when VST3Editor::exchangeView() rebuilds the view tree.
// ==============================================================================

#pragma once

#include "base/source/fobject.h"
#include "pluginterfaces/base/ftypes.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"

namespace VSTGUI {
class CFrame;
class VST3Editor;
class UIViewSwitchContainer;
class CControl;
class IController;
} // namespace VSTGUI

namespace Steinberg::Vst { class EditController; }

namespace Membrum::UI {

/// Sub-controller attached to the UIDescription editor. Listens to
/// kUiModeId and kEditorSizeId and drives the Acoustic/Extended view swap and
/// the Default/Compact editor template exchange.
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

    // Cached Parameter pointers (not owned).
    Steinberg::Vst::Parameter* uiModeParam_     = nullptr;
    Steinberg::Vst::Parameter* editorSizeParam_ = nullptr;
};

} // namespace Membrum::UI
