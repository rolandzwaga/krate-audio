// ==============================================================================
// ModulatorSubController Implementation
// ==============================================================================

#include "modulator_sub_controller.h"
#include "controller/views/modulator_activity_view.h"

namespace Innexus {

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

    return DelegationController::verifyView(view, attrs, desc);
}

} // namespace Innexus
