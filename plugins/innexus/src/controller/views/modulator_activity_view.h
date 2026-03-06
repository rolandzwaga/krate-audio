#pragma once

// ==============================================================================
// ModulatorActivityView - Modulator Activity Indicator Custom View
// ==============================================================================
// FR-038, FR-040: Animated modulation indicator
// ==============================================================================

#include "vstgui/lib/cview.h"

namespace Innexus {

class ModulatorActivityView : public VSTGUI::CView
{
public:
    explicit ModulatorActivityView(const VSTGUI::CRect& size);

    /// Set which modulator this view represents (0 or 1)
    void setModIndex(int index) { modIndex_ = index; }

    /// Called from timer callback with modulator phase and active state
    void updateData(float phase, bool active);

    void draw(VSTGUI::CDrawContext* context) override;

    CLASS_METHODS_NOCOPY(ModulatorActivityView, CView)

    // Test accessors
    int getModIndex() const { return modIndex_; }
    float getPhase() const { return phase_; }
    bool isActive() const { return active_; }

private:
    float phase_ = 0.0f;
    bool active_ = false;
    int modIndex_ = 0;
};

} // namespace Innexus
