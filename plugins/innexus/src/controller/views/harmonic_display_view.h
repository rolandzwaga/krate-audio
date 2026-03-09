#pragma once

// ==============================================================================
// HarmonicDisplayView - Spectral Display Custom View
// ==============================================================================
// FR-009: Adaptive-bar spectral display for harmonic partials (up to 96)
// FR-011: Empty/placeholder state when no analysis data
// FR-012: Attenuated partials shown differently from active ones
// ==============================================================================

#include "controller/display_data.h"
#include "vstgui/lib/cview.h"

namespace Innexus {

class HarmonicDisplayView : public VSTGUI::CView
{
public:
    explicit HarmonicDisplayView(const VSTGUI::CRect& size);

    /// Called from timer callback with latest display data
    void updateData(const DisplayData& data);

    void draw(VSTGUI::CDrawContext* context) override;

    CLASS_METHODS_NOCOPY(HarmonicDisplayView, CView)

    // Test accessors
    bool hasData() const { return hasData_; }
    float getAmplitude(int index) const
    {
        if (index >= 0 && index < 96) return amplitudes_[index];
        return 0.0f;
    }
    bool isActive(int index) const
    {
        if (index >= 0 && index < 96) return active_[index];
        return false;
    }

    /// Convert linear amplitude to bar height in pixels (public for testing)
    static float amplitudeToBarHeight(float amp, float viewHeight);

private:
    float amplitudes_[96]{};
    bool active_[96]{};
    int activeCount_ = 48;
    bool hasData_ = false;
};

} // namespace Innexus
