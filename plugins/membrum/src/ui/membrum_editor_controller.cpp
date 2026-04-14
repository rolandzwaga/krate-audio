// ==============================================================================
// MembrumEditorController -- Implementation (Phase 6)
// ==============================================================================

#include "ui/membrum_editor_controller.h"
#include "ui/ui_mode.h"
#include "plugin_ids.h"

#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/uiviewswitchcontainer.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

namespace Membrum::UI {

MembrumEditorController::MembrumEditorController(
    VSTGUI::VST3Editor* editor,
    Steinberg::Vst::EditController* editController) noexcept
    : editor_(editor)
    , editController_(editController)
{
    if (editController_)
    {
        uiModeParam_ = editController_->getParameterObject(kUiModeId);
        if (uiModeParam_)
            uiModeParam_->addDependent(this);
    }
}

MembrumEditorController::~MembrumEditorController()
{
    // Deregister before Parameter is destroyed.
    if (uiModeParam_)
        uiModeParam_->removeDependent(this);
}

void MembrumEditorController::attachUiModeSwitch(
    VSTGUI::UIViewSwitchContainer* container) noexcept
{
    uiModeSwitch_ = container;
    if (uiModeSwitch_ && uiModeParam_)
    {
        const auto norm = static_cast<float>(uiModeParam_->getNormalized());
        uiModeSwitch_->setCurrentViewIndex(
            (uiModeFromNormalized(norm) == UiMode::Extended) ? 1 : 0);
    }
}

void PLUGIN_API MembrumEditorController::update(FUnknown* changedUnknown,
                                                Steinberg::int32 message)
{
    if (message != IDependent::kChanged)
        return;

    auto* param = Steinberg::FCast<Steinberg::Vst::Parameter>(changedUnknown);
    if (!param)
        return;

    if (param == uiModeParam_ && uiModeSwitch_)
    {
        const auto norm = static_cast<float>(param->getNormalized());
        const int idx = (uiModeFromNormalized(norm) == UiMode::Extended) ? 1 : 0;
        uiModeSwitch_->setCurrentViewIndex(idx);
    }
}

} // namespace Membrum::UI
