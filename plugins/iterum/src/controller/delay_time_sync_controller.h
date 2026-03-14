#pragma once

// ==============================================================================
// DelayTimeSyncController - DelegationController for Delay Time + Sync Toggle
// ==============================================================================
// Manages an ArcKnob that dynamically swaps between free delay time and note
// value parameters when the sync toggle is changed. Follows the Innexus
// ModulatorSubController pattern.
//
// Each delay mode instantiates this with mode-specific parameter IDs.
// ==============================================================================

#include "vstgui/uidescription/delegationcontroller.h"
#include "vstgui/lib/controls/ccontrol.h"

#include <vector>

namespace Iterum {

class DelayTimeSyncController : public VSTGUI::DelegationController
{
public:
    /// @param freeTimeTag  Parameter tag for free-running delay time (e.g. kGranularDelayTimeId)
    /// @param syncTag      Parameter tag for the time mode toggle (e.g. kGranularTimeModeId)
    /// @param noteValueTag Parameter tag for the note value selector (e.g. kGranularNoteValueId)
    /// @param parent       Parent controller (VST3Editor)
    DelayTimeSyncController(int32_t freeTimeTag, int32_t syncTag, int32_t noteValueTag,
                            VSTGUI::IController* parent)
        : DelegationController(parent)
        , freeTimeTag_(freeTimeTag)
        , syncTag_(syncTag)
        , noteValueTag_(noteValueTag)
    {
    }

    ~DelayTimeSyncController() override
    {
        if (syncToggle_)
            syncToggle_->unregisterControlListener(this);
    }

    VSTGUI::CView* verifyView(VSTGUI::CView* view,
                               const VSTGUI::UIAttributes& attrs,
                               const VSTGUI::IUIDescription* desc) override;

    void valueChanged(VSTGUI::CControl* control) override;

private:
    /// Sync all time controls' displayed values with their current tag's parameter.
    void syncTimeControlValues();

    int32_t freeTimeTag_ = -1;
    int32_t syncTag_ = -1;
    int32_t noteValueTag_ = -1;

    /// All controls bound to the delay time parameter (ArcKnob + CParamDisplay).
    /// Their tags are swapped together when sync toggles.
    std::vector<VSTGUI::CControl*> timeControls_;

    /// Pointer to sync toggle control (for unregistering sub-listener).
    VSTGUI::CControl* syncToggle_ = nullptr;
};

} // namespace Iterum
