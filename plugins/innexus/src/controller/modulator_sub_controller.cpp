// ==============================================================================
// ModulatorSubController Implementation
// ==============================================================================
//
// Key VSTGUI insight: DelegationController::getControlListener() delegates to
// the parent (VST3Editor), which returns itself. So all controls in the sub-
// controller's scope have the VST3Editor as their main listener — NOT the sub-
// controller. This means the sub-controller's valueChanged() is never called
// via the normal listener path.
//
// To observe the sync toggle's value changes, we register as a "sub listener"
// via CControl::registerControlListener(). This notifies us alongside the
// VST3Editor's main listener, without disrupting parameter forwarding.
// ==============================================================================

#include "modulator_sub_controller.h"
#include "controller/views/modulator_activity_view.h"

#include "public.sdk/source/vst/vstguieditor.h"

namespace Innexus {

// Helper: after swapping the Rate knob's tag, manually sync its value with
// the new parameter. VST3Editor::controlTagDidChange() only re-binds controls
// whose listener is the VST3Editor itself — sub-controller-owned controls
// are skipped, so we must fetch and apply the value ourselves.
void ModulatorSubController::syncRateKnobValue()
{
    if (!rateKnob_)
        return;

    auto* frame = rateKnob_->getFrame();
    if (!frame)
        return;

    auto* editor =
        dynamic_cast<Steinberg::Vst::VSTGUIEditor*>(frame->getEditor());
    if (!editor)
        return;

    auto* editController = editor->getController();
    if (!editController)
        return;

    auto paramId =
        static_cast<Steinberg::Vst::ParamID>(rateKnob_->getTag());
    float paramVal =
        static_cast<float>(editController->getParamNormalized(paramId));

    rateKnob_->setValueNormalized(paramVal);
    rateKnob_->invalid();
}

VSTGUI::CView* ModulatorSubController::verifyView(
    VSTGUI::CView* view,
    const VSTGUI::UIAttributes& attrs,
    const VSTGUI::IUIDescription* desc)
{
    // Set modIndex on any ModulatorActivityView child so the controller
    // knows which modulator's data to feed it (FR-046)
    if (auto* activityView = dynamic_cast<ModulatorActivityView*>(view))
    {
        activityView->setModIndex(modIndex_);
    }

    if (auto* control = dynamic_cast<VSTGUI::CControl*>(view))
    {
        const int32_t offset = modIndex_ * 10;
        const int32_t rateTag = static_cast<int32_t>(kMod1RateId) + offset;
        const int32_t syncToggleTag =
            static_cast<int32_t>(kMod1RateSyncId) + offset;

        // Detect the Rate knob by its control-tag and set up tag swapping.
        if (control->getTag() == rateTag)
        {
            syncTag_ = syncToggleTag;
            freeRateTag_ = rateTag;
            noteValueTag_ = static_cast<int32_t>(kMod1NoteValueId) + offset;
            rateKnob_ = control;

            // Default is synced (1.0), so show note value tag.
            control->setTag(noteValueTag_);
            syncRateKnobValue();
        }

        // Register as a sub-listener on the sync toggle so we receive
        // valueChanged() alongside the VST3Editor's main listener.
        if (control->getTag() == syncToggleTag)
        {
            syncTag_ = syncToggleTag;
            freeRateTag_ = static_cast<int32_t>(kMod1RateId) + offset;
            noteValueTag_ = static_cast<int32_t>(kMod1NoteValueId) + offset;
            control->registerControlListener(this);
            syncToggle_ = control;
        }
    }

    return DelegationController::verifyView(view, attrs, desc);
}

void ModulatorSubController::valueChanged(VSTGUI::CControl* control)
{
    // When the sync toggle changes, swap the Rate knob's tag
    if (rateKnob_ && control && control->getTag() == syncTag_)
    {
        bool synced = control->getValueNormalized() > 0.5f;
        int32_t newTag = synced ? noteValueTag_ : freeRateTag_;

        if (rateKnob_->getTag() != newTag)
        {
            rateKnob_->setTag(newTag);
            syncRateKnobValue();
        }
    }

    DelegationController::valueChanged(control);
}

} // namespace Innexus
