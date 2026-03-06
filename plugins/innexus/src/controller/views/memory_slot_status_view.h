#pragma once

// ==============================================================================
// MemorySlotStatusView - Memory Slot Status Display Custom View
// ==============================================================================
// FR-029: 8-slot occupied/empty indicators
// ==============================================================================

#include "controller/display_data.h"
#include "vstgui/lib/cview.h"

namespace Innexus {

class MemorySlotStatusView : public VSTGUI::CView
{
public:
    explicit MemorySlotStatusView(const VSTGUI::CRect& size);

    /// Called from timer callback with latest display data
    void updateData(const DisplayData& data);

    void draw(VSTGUI::CDrawContext* context) override;

    CLASS_METHODS_NOCOPY(MemorySlotStatusView, CView)

    // Test accessors
    bool isSlotOccupied(int index) const
    {
        if (index >= 0 && index < 8) return slotOccupied_[index];
        return false;
    }

private:
    bool slotOccupied_[8]{};
};

} // namespace Innexus
