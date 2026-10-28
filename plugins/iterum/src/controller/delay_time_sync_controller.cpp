// ==============================================================================
// DelayTimeSyncController Implementation
// ==============================================================================
//
// Key VSTGUI insight: DelegationController::getControlListener() delegates to
// the parent (VST3Editor), which returns itself. So controls in the sub-
// controller's scope have the VST3Editor as their main listener. To observe
// the sync toggle's value changes, we register as a "sub listener" via
// CControl::registerControlListener().
// ==============================================================================

#include "delay_time_sync_controller.h"

#include "public.sdk/source/vst/vstguieditor.h"

namespace Iterum {

Steinberg::Vst::EditController* DelayTimeSyncController::editController() const
{
    // The parent controller is the VST3Editor (Controller::createSubController passes it). Resolve the edit
    // controller through it, never through a control's frame: UIDescription calls verifyView before the view is
    // added to its parent, so while a panel is being built every control's getFrame() is null.
    auto* editor = dynamic_cast<Steinberg::Vst::VSTGUIEditor*>(controller);
    return editor ? editor->getController() : nullptr;
}

void DelayTimeSyncController::syncTimeControlValues()
{
    if (timeControls_.empty())
        return;

    auto* ec = editController();
    if (!ec)
        return;

    // All time controls share the same tag, so fetch value once
    auto paramId =
        static_cast<Steinberg::Vst::ParamID>(timeControls_[0]->getTag());
    float paramVal =
        static_cast<float>(ec->getParamNormalized(paramId));

    for (auto* ctrl : timeControls_)
    {
        ctrl->setValueNormalized(paramVal);
        ctrl->invalid();
    }
}

VSTGUI::CView* DelayTimeSyncController::verifyView(
    VSTGUI::CView* view,
    const VSTGUI::UIAttributes& attrs,
    const VSTGUI::IUIDescription* desc)
{
    if (auto* control = dynamic_cast<VSTGUI::CControl*>(view))
    {
        // Detect any control with the free time tag (ArcKnob + CParamDisplay)
        if (control->getTag() == freeTimeTag_)
        {
            timeControls_.push_back(control);

            // Check current sync state to set initial tag
            // Default is free (0.0), so keep freeTimeTag unless synced
            if (auto* ec = editController())
            {
                auto syncVal = ec->getParamNormalized(
                    static_cast<Steinberg::Vst::ParamID>(syncTag_));
                if (syncVal > 0.5)
                {
                    control->setTag(noteValueTag_);
                }
            }
        }

        // Register as sub-listener on the sync toggle
        if (control->getTag() == syncTag_)
        {
            control->registerControlListener(this);
            syncToggle_ = control;
        }
    }

    return DelegationController::verifyView(view, attrs, desc);
}

void DelayTimeSyncController::valueChanged(VSTGUI::CControl* control)
{
    // When the sync toggle changes, swap all time controls' tags
    if (!timeControls_.empty() && control && control->getTag() == syncTag_)
    {
        bool synced = control->getValueNormalized() > 0.5f;
        int32_t newTag = synced ? noteValueTag_ : freeTimeTag_;

        if (timeControls_[0]->getTag() != newTag)
        {
            for (auto* ctrl : timeControls_)
                ctrl->setTag(newTag);
            syncTimeControlValues();
        }
    }

    DelegationController::valueChanged(control);
}

} // namespace Iterum
