#pragma once

// ==============================================================================
// ModulatorSubController - DelegationController for Mod Template Remapping
// ==============================================================================
// FR-046: Reusable modulator template with sub-controller for tag remapping
// Maps generic "Mod.*" control-tag names to modulator-specific parameter IDs
// based on the modulator index (0 or 1).
//
// Tag mapping: "Mod.Enable" -> kMod1EnableId + modIndex_ * 10
// ==============================================================================

#include "plugin_ids.h"
#include "vstgui/uidescription/delegationcontroller.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/iuidescription.h"
#include "vstgui/lib/controls/ccontrol.h"

#include <cstring>

namespace Innexus {

// Forward declaration
class ModulatorActivityView;

class ModulatorSubController : public VSTGUI::DelegationController
{
public:
    ModulatorSubController(int modIndex, VSTGUI::IController* parent)
        : DelegationController(parent)
        , modIndex_(modIndex)
    {
    }

    ~ModulatorSubController() override
    {
        // Unregister as sub-listener to avoid dangling pointer
        if (syncToggle_)
            syncToggle_->unregisterControlListener(this);
    }

    int32_t getTagForName(VSTGUI::UTF8StringPtr name,
                          int32_t registeredTag) const override
    {
        if (!name)
            return registeredTag;

        // Base ID offset: Mod1 starts at 610, Mod2 starts at 620
        const int32_t offset = modIndex_ * 10;

        if (std::strcmp(name, "Mod.Enable") == 0)
            return static_cast<int32_t>(kMod1EnableId) + offset;

        if (std::strcmp(name, "Mod.Waveform") == 0)
            return static_cast<int32_t>(kMod1WaveformId) + offset;

        if (std::strcmp(name, "Mod.Rate") == 0)
            return static_cast<int32_t>(kMod1RateId) + offset;

        if (std::strcmp(name, "Mod.Depth") == 0)
            return static_cast<int32_t>(kMod1DepthId) + offset;

        if (std::strcmp(name, "Mod.RangeStart") == 0)
            return static_cast<int32_t>(kMod1RangeStartId) + offset;

        if (std::strcmp(name, "Mod.RangeEnd") == 0)
            return static_cast<int32_t>(kMod1RangeEndId) + offset;

        if (std::strcmp(name, "Mod.Target") == 0)
            return static_cast<int32_t>(kMod1TargetId) + offset;

        if (std::strcmp(name, "Mod.RateSync") == 0)
            return static_cast<int32_t>(kMod1RateSyncId) + offset;

        if (std::strcmp(name, "Mod.NoteValue") == 0)
            return static_cast<int32_t>(kMod1NoteValueId) + offset;

        // Unrecognized: delegate to parent if available, otherwise return as-is
        if (controller)
            return DelegationController::getTagForName(name, registeredTag);
        return registeredTag;
    }

    VSTGUI::CView* verifyView(VSTGUI::CView* view,
                               const VSTGUI::UIAttributes& attrs,
                               const VSTGUI::IUIDescription* desc) override;

    void valueChanged(VSTGUI::CControl* control) override;

    int getModIndex() const { return modIndex_; }

private:
    int modIndex_ = 0;

    /// Sync the Rate knob's displayed value with its current tag's parameter.
    void syncRateKnobValue();

    /// Pointer to the Rate knob. VSTGUI-owned, not ours.
    VSTGUI::CControl* rateKnob_ = nullptr;

    /// The free-running rate param ID (kMod1RateId or kMod2RateId)
    int32_t freeRateTag_ = -1;

    /// The note value param ID (kMod1NoteValueId or kMod2NoteValueId)
    int32_t noteValueTag_ = -1;

    /// The sync toggle param ID (kMod1RateSyncId or kMod2RateSyncId)
    int32_t syncTag_ = -1;

    /// Pointer to sync toggle control (for unregistering sub-listener)
    VSTGUI::CControl* syncToggle_ = nullptr;
};

} // namespace Innexus
