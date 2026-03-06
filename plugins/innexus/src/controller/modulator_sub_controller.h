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

        // Unrecognized: delegate to parent if available, otherwise return as-is
        if (controller)
            return DelegationController::getTagForName(name, registeredTag);
        return registeredTag;
    }

    VSTGUI::CView* verifyView(VSTGUI::CView* view,
                               const VSTGUI::UIAttributes& attrs,
                               const VSTGUI::IUIDescription* desc) override;

    int getModIndex() const { return modIndex_; }

private:
    int modIndex_ = 0;
};

} // namespace Innexus
