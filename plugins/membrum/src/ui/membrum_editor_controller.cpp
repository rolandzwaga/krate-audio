// ==============================================================================
// MembrumEditorController -- Implementation (Phase 6)
// ==============================================================================

#include "ui/membrum_editor_controller.h"
#include "ui/editor_size_policy.h"
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
        uiModeParam_     = editController_->getParameterObject(kUiModeId);
        editorSizeParam_ = editController_->getParameterObject(kEditorSizeId);
        if (uiModeParam_)
            uiModeParam_->addDependent(this);
        if (editorSizeParam_)
            editorSizeParam_->addDependent(this);
    }
}

MembrumEditorController::~MembrumEditorController()
{
    // CRITICAL: deregister before Parameter is destroyed or the editor rebuilds
    // the view tree via exchangeView() (gotcha documented in plan.md).
    if (uiModeParam_)
        uiModeParam_->removeDependent(this);
    if (editorSizeParam_)
        editorSizeParam_->removeDependent(this);

    // T030: cancel any still-pending deferred exchangeView() so its callback
    // does not fire into a destroyed sub-controller.
    if (pendingExchangeTimer_)
    {
        pendingExchangeTimer_->stop();
        pendingExchangeTimer_ = nullptr;
    }
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
        return;
    }

    if (param == editorSizeParam_ && editor_)
    {
        // T030: calling exchangeView() synchronously from IDependent::update()
        // destroys the view tree that is currently dispatching this very
        // notification (VSTGUI crashes deep in CFrame::removeView). Defer the
        // swap onto a zero-delay one-shot CVSTGUITimer so the current
        // notification chain unwinds first. The destructor cancels the timer.
        const auto norm = static_cast<float>(param->getNormalized());
        pendingEditorSize_ = (norm < 0.5f) ? EditorSize::Default
                                            : EditorSize::Compact;

        // Replace any still-pending exchange -- latest value wins.
        if (pendingExchangeTimer_)
            pendingExchangeTimer_->stop();

        pendingExchangeTimer_ = VSTGUI::owned(new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer* timer) {
                timer->stop();  // one-shot
                if (editor_)
                    editor_->exchangeView(templateNameFor(pendingEditorSize_));
            },
            1 /* fireTime ms; one-shot after current event unwinds */,
            true /* start */));
        return;
    }
}

} // namespace Membrum::UI
