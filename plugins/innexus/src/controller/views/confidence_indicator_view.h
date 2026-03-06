#pragma once

// ==============================================================================
// ConfidenceIndicatorView - F0 Confidence Meter Custom View
// ==============================================================================
// FR-013: Confidence bar with color coding
// FR-014: Green/yellow/red color zones
// FR-015: Detected note name display
// ==============================================================================

#include "controller/display_data.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/ccolor.h"

#include <string>

namespace Innexus {

class ConfidenceIndicatorView : public VSTGUI::CView
{
public:
    explicit ConfidenceIndicatorView(const VSTGUI::CRect& size);

    /// Called from timer callback with latest display data
    void updateData(const DisplayData& data);

    void draw(VSTGUI::CDrawContext* context) override;

    CLASS_METHODS_NOCOPY(ConfidenceIndicatorView, CView)

    // Test accessors
    float getConfidence() const { return confidence_; }
    float getF0() const { return f0_; }

    /// Color for the confidence bar (public for testing)
    static VSTGUI::CColor getConfidenceColor(float confidence);

    /// Convert frequency to note name (public for testing)
    static std::string freqToNoteName(float freq);

private:
    float confidence_ = 0.0f;
    float f0_ = 0.0f;
};

} // namespace Innexus
